// The build result window on Windows (see platform/result_window.hpp): a modeless dialog (window class #32770, so DefDlgProc supplies the
// dialog manager: default button, WM_NEXTDLGCTL, focus restored on reactivation, Esc and the close box as IDCANCEL) driven by this file's
// IsDialogMessageW loop. Its children are plain STATIC, EDIT and BUTTON controls created in tab order, so screen readers expose them through
// their standard MSAA/UIA proxies and name the edit after the label in front of it.
// DPI: the window is per-monitor aware (v2, else system aware) on Windows 10 and later, through functions looked up at run time; it rebuilds its
// font and layout on WM_DPICHANGED. Older systems get a DPI-unaware window, scaled by the system.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/result_window.hpp>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstring>
#include <string>

namespace zodiac::platform {
namespace {
constexpr int id_label=100;
constexpr int id_output=101;
constexpr int id_copy=102;
constexpr int id_open=103;
// Close carries IDCANCEL, so the dialog manager routes Esc and the close box (WM_CLOSE) to it.
constexpr int id_close=IDCANCEL;
constexpr UINT message_dpi_changed=0x02E0;
// DIALOG_DPI_CHANGE_BEHAVIORS: DDC_DISABLE_ALL (the window scales its own controls).
constexpr int dialog_dpi_disable_all=1;
constexpr int base_dpi=96;

// Windows 10 functions, looked up at run time so the program still starts on older systems (where every pointer stays null).
struct DpiApi{
using SetThreadContext=HANDLE(WINAPI*)(HANDLE);
using DpiForWindow=UINT(WINAPI*)(HWND);
using ParametersForDpi=BOOL(WINAPI*)(UINT,UINT,PVOID,UINT,UINT);
using SetDialogBehavior=BOOL(WINAPI*)(HWND,int,int);
SetThreadContext set_thread_context=nullptr;
DpiForWindow dpi_for_window=nullptr;
ParametersForDpi parameters_for_dpi=nullptr;
SetDialogBehavior set_dialog_behavior=nullptr;
};

template<typename Function>
Function lookup(HMODULE module,const char* name) noexcept
{
return reinterpret_cast<Function>(reinterpret_cast<void*>(GetProcAddress(module,name)));
}

DpiApi load_dpi_api() noexcept
{
DpiApi api;
const HMODULE user32=GetModuleHandleW(L"user32.dll");
if(!user32)return api;
api.set_thread_context=lookup<DpiApi::SetThreadContext>(user32,"SetThreadDpiAwarenessContext");
api.dpi_for_window=lookup<DpiApi::DpiForWindow>(user32,"GetDpiForWindow");
api.parameters_for_dpi=lookup<DpiApi::ParametersForDpi>(user32,"SystemParametersInfoForDpi");
api.set_dialog_behavior=lookup<DpiApi::SetDialogBehavior>(user32,"SetDialogDpiChangeBehavior");
return api;
}

// UTF-8 to UTF-16; invalid sequences become U+FFFD. Text beyond INT_MAX bytes is cut (no build output comes near it).
std::wstring widen(std::string_view text)
{
std::wstring result;
const int size=static_cast<int>(std::min<std::size_t>(text.size(),INT_MAX));
const int count=size?MultiByteToWideChar(CP_UTF8,0,text.data(),size,nullptr,0):0;
if(count<=0)return result;
result.resize_and_overwrite(static_cast<std::size_t>(count),[&](wchar_t* buffer,std::size_t length){
return static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8,0,text.data(),size,buffer,static_cast<int>(length)));
});
return result;
}

// UTF-8 text with "\n" line ends to UTF-16 with "\r\n", in one allocation: the text is converted into the tail of the buffer, then moved
// forward while each "\n" gains its "\r" (the write position never passes the read position, since it trails by the line breaks still ahead).
std::wstring edit_text(std::string_view text)
{
std::wstring result;
const int size=static_cast<int>(std::min<std::size_t>(text.size(),INT_MAX));
if(size==0)return result;
const std::string_view used=text.substr(0,static_cast<std::size_t>(size));
const std::size_t breaks=static_cast<std::size_t>(std::count(used.begin(),used.end(),'\n'));
const int count=MultiByteToWideChar(CP_UTF8,0,used.data(),size,nullptr,0);
if(count<=0)return result;
result.resize_and_overwrite(static_cast<std::size_t>(count)+breaks,[&](wchar_t* buffer,std::size_t length){
const int converted=MultiByteToWideChar(CP_UTF8,0,used.data(),size,buffer+breaks,count);
std::size_t write=0;
for(std::size_t read=breaks;read<breaks+static_cast<std::size_t>(converted);++read){
const wchar_t c=buffer[read];
if(c==L'\n')buffer[write++]=L'\r';
buffer[write++]=c;
}
return std::min(write,length);
});
return result;
}

struct Window{
std::wstring title;
std::wstring text;
const std::filesystem::path* folder=nullptr;
DpiApi api;
UINT dpi=base_dpi;
HFONT font=nullptr;
bool owns_font=false;
int line_height=0;
HWND label=nullptr;
HWND output=nullptr;
HWND copy=nullptr;
HWND open=nullptr;
HWND close=nullptr;
int copy_width=0;
int open_width=0;
int close_width=0;
WNDPROC edit_proc=nullptr;

int scaled(int value) const noexcept{return MulDiv(value,static_cast<int>(dpi),base_dpi);}
};

UINT window_dpi(const Window& window,HWND hwnd) noexcept
{
if(window.api.dpi_for_window){
const UINT dpi=window.api.dpi_for_window(hwnd);
if(dpi)return dpi;
}
const HDC dc=GetDC(hwnd);
const int dpi=dc?GetDeviceCaps(dc,LOGPIXELSY):base_dpi;
if(dc)ReleaseDC(hwnd,dc);
return dpi>0?static_cast<UINT>(dpi):base_dpi;
}

void free_font(Window& window) noexcept
{
if(window.owns_font&&window.font)DeleteObject(window.font);
window.font=nullptr;
window.owns_font=false;
}

int text_width(HDC dc,const wchar_t* text) noexcept
{
SIZE size{};
GetTextExtentPoint32W(dc,text,static_cast<int>(std::wcslen(text)),&size);
return size.cx;
}

int button_width(const Window& window,HDC dc,HWND button) noexcept
{
if(!button)return 0;
wchar_t text[64];
GetWindowTextW(button,text,64);
return std::max(window.scaled(75),text_width(dc,text)+window.scaled(24));
}

// The system message font (not the dialog's bold System font) for the window's DPI, set on every control, and the sizes measured with it.
void apply_font(Window& window,HWND hwnd) noexcept
{
NONCLIENTMETRICSW metrics{};
metrics.cbSize=sizeof metrics;
const BOOL got=window.api.parameters_for_dpi?window.api.parameters_for_dpi(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0,window.dpi):SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof metrics,&metrics,0);
HFONT font=got?CreateFontIndirectW(&metrics.lfMessageFont):nullptr;
const bool owned=font!=nullptr;
if(!font)font=static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
for(HWND control:{window.label,window.output,window.copy,window.open,window.close})if(control)SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
free_font(window);
window.font=font;
window.owns_font=owned;
const HDC dc=GetDC(hwnd);
if(!dc)return;
const HGDIOBJ previous=SelectObject(dc,font);
TEXTMETRICW metric{};
GetTextMetricsW(dc,&metric);
window.line_height=metric.tmHeight;
window.copy_width=button_width(window,dc,window.copy);
window.open_width=button_width(window,dc,window.open);
window.close_width=button_width(window,dc,window.close);
SelectObject(dc,previous);
ReleaseDC(hwnd,dc);
}

int margin(const Window& window) noexcept{return window.scaled(11);}
int gap(const Window& window) noexcept{return window.scaled(7);}
int button_height(const Window& window) noexcept{return std::max(window.scaled(23),window.line_height+window.scaled(10));}

int buttons_width(const Window& window) noexcept
{
int width=window.copy_width+window.close_width+gap(window);
if(window.open)width+=window.open_width+gap(window);
return width;
}

// Label on top, the output below it filling the window, the buttons in a row at the bottom right in tab order.
void layout(const Window& window,int width,int height) noexcept
{
if(!window.output)return;
const int edge=margin(window);
const int space=gap(window);
const int buttons_y=height-edge-button_height(window);
const int output_y=edge+window.line_height+window.scaled(4);
const int output_height=std::max(0,buttons_y-space-output_y);
const int inner_width=std::max(0,width-2*edge);
HDWP batch=BeginDeferWindowPos(5);
const auto place=[&batch](HWND control,int x,int y,int cx,int cy){
if(control&&batch)batch=DeferWindowPos(batch,control,nullptr,x,y,cx,cy,SWP_NOZORDER|SWP_NOACTIVATE);
};
place(window.label,edge,edge,inner_width,window.line_height);
place(window.output,edge,output_y,inner_width,output_height);
int x=width-edge-buttons_width(window);
place(window.copy,x,buttons_y,window.copy_width,button_height(window));
x+=window.copy_width+space;
if(window.open){
place(window.open,x,buttons_y,window.open_width,button_height(window));
x+=window.open_width+space;
}
place(window.close,x,buttons_y,window.close_width,button_height(window));
if(batch)EndDeferWindowPos(batch);
}

void relayout(const Window& window,HWND hwnd) noexcept
{
RECT client{};
GetClientRect(hwnd,&client);
layout(window,client.right,client.bottom);
}

// Tabbing back into the output keeps the caret where the reader left it: the dialog manager selects all text only in DLGC_HASSETSEL controls.
LRESULT CALLBACK output_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam)
{
const Window* window=reinterpret_cast<const Window*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
const LRESULT result=CallWindowProcW(window->edit_proc,hwnd,message,wparam,lparam);
if(message==WM_GETDLGCODE)return result&~static_cast<LRESULT>(DLGC_HASSETSEL);
return result;
}

HWND create_control(HWND parent,const wchar_t* window_class,const wchar_t* text,DWORD style,DWORD extended_style,int id) noexcept
{
return CreateWindowExW(extended_style,window_class,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
}

bool copy_to_clipboard(HWND owner,const std::wstring& text) noexcept
{
// Another program may hold the clipboard for a moment.
bool opened=false;
for(int attempt=0;attempt<5&&!(opened=OpenClipboard(owner)!=FALSE);++attempt)Sleep(20);
if(!opened)return false;
bool done=false;
if(EmptyClipboard()){
const std::size_t bytes=(text.size()+1)*sizeof(wchar_t);
HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);
if(memory){
if(void* data=GlobalLock(memory)){
std::memcpy(data,text.c_str(),bytes);
GlobalUnlock(memory);
// On success the clipboard owns the memory.
if(SetClipboardData(CF_UNICODETEXT,memory)){
done=true;
memory=nullptr;
}
}
if(memory)GlobalFree(memory);
}
}
CloseClipboard();
return done;
}

bool open_folder(const std::filesystem::path& folder) noexcept
{
return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32;
}

// Sized for about 90 characters by 25 lines of output at 96 DPI, at most the work area, centered on the monitor the mouse is on.
void place_window(Window& window,HWND hwnd) noexcept
{
POINT cursor{};
GetCursorPos(&cursor);
MONITORINFO monitor{};
monitor.cbSize=sizeof monitor;
if(!GetMonitorInfoW(MonitorFromPoint(cursor,MONITOR_DEFAULTTOPRIMARY),&monitor))SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0);
const RECT work=monitor.rcWork;
// Moved onto the monitor first, so a WM_DPICHANGED (if its DPI differs) arrives before the size is chosen.
SetWindowPos(hwnd,nullptr,work.left,work.top,0,0,SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSIZE);
const UINT dpi=window_dpi(window,hwnd);
if(dpi!=window.dpi){
window.dpi=dpi;
apply_font(window,hwnd);
}
const int work_width=work.right-work.left;
const int work_height=work.bottom-work.top;
const int width=std::min(window.scaled(720),work_width);
const int height=std::min(window.scaled(520),work_height);
SetWindowPos(hwnd,nullptr,work.left+(work_width-width)/2,work.top+(work_height-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
relayout(window,hwnd);
}

BOOL initialize(HWND hwnd,Window& window) noexcept
{
SetWindowLongPtrW(hwnd,DWLP_USER,reinterpret_cast<LONG_PTR>(&window));
if(window.api.set_dialog_behavior)window.api.set_dialog_behavior(hwnd,dialog_dpi_disable_all,dialog_dpi_disable_all);
SetWindowTextW(hwnd,window.title.c_str());
// IDI_APPLICATION, spelled for the W function (the macro follows UNICODE, which this project does not define).
const HICON icon=LoadIconW(nullptr,MAKEINTRESOURCEW(32512));
SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(icon));
SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(icon));
// Creation order is z-order and tab order: the label comes right before the edit it names.
window.label=create_control(hwnd,L"STATIC",L"&Build output",SS_LEFT,0,id_label);
// No word wrap (ES_AUTOHSCROLL|WS_HSCROLL): every diagnostic stays one line, so each arrow press reads one whole message.
window.output=create_control(hwnd,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL|WS_VSCROLL|WS_HSCROLL|WS_TABSTOP,WS_EX_CLIENTEDGE,id_output);
window.copy=create_control(hwnd,L"BUTTON",L"&Copy all",BS_PUSHBUTTON|WS_TABSTOP,0,id_copy);
if(window.folder)window.open=create_control(hwnd,L"BUTTON",L"&Open output folder",BS_PUSHBUTTON|WS_TABSTOP,0,id_open);
// DM_SETDEFID below makes Close the default button (BS_DEFPUSHBUTTON, which screen readers report as "default").
window.close=create_control(hwnd,L"BUTTON",L"Close",BS_PUSHBUTTON|WS_TABSTOP,0,id_close);
if(!window.output)return FALSE;
// The default limit (32767 characters) applies to typing only, but a zero limit makes sure no long output is ever cut.
SendMessageW(window.output,EM_SETLIMITTEXT,0,0);
SetWindowTextW(window.output,window.text.c_str());
SetWindowLongPtrW(window.output,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&window));
window.edit_proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.output,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&output_proc)));
SendMessageW(hwnd,DM_SETDEFID,id_close,0);
window.dpi=window_dpi(window,hwnd);
apply_font(window,hwnd);
place_window(window,hwnd);
// Focus is set once the window is shown (show_result_window).
return FALSE;
}

INT_PTR CALLBACK dialog_proc(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam)
{
if(message==WM_INITDIALOG)return initialize(hwnd,*reinterpret_cast<Window*>(lparam));
Window* window=reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd,DWLP_USER));
if(!window)return FALSE;
switch(message){
case WM_COMMAND:
switch(LOWORD(wparam)){
case id_close:
DestroyWindow(hwnd);
return TRUE;
case id_copy:
if(HIWORD(wparam)==BN_CLICKED&&!copy_to_clipboard(hwnd,window->text))MessageBeep(MB_ICONERROR);
return TRUE;
case id_open:
if(HIWORD(wparam)==BN_CLICKED&&window->folder&&!open_folder(*window->folder))MessageBeep(MB_ICONERROR);
return TRUE;
default:
return FALSE;
}
case WM_SIZE:
layout(*window,LOWORD(lparam),HIWORD(lparam));
return TRUE;
case WM_GETMINMAXINFO:{
RECT outer{};
RECT client{};
GetWindowRect(hwnd,&outer);
GetClientRect(hwnd,&client);
MINMAXINFO* limits=reinterpret_cast<MINMAXINFO*>(lparam);
const int frame_width=(outer.right-outer.left)-client.right;
const int frame_height=(outer.bottom-outer.top)-client.bottom;
limits->ptMinTrackSize.x=frame_width+2*margin(*window)+buttons_width(*window);
limits->ptMinTrackSize.y=frame_height+2*margin(*window)+window->line_height+button_height(*window)+window->scaled(80);
return TRUE;
}
case message_dpi_changed:{
window->dpi=HIWORD(wparam);
apply_font(*window,hwnd);
const RECT* suggested=reinterpret_cast<const RECT*>(lparam);
SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
relayout(*window,hwnd);
return TRUE;
}
default:
return FALSE;
}
}

// An in-memory dialog template with no controls, menu, class, title or font: initialize() builds everything in pixels.
struct alignas(DWORD) DialogTemplate{
DLGTEMPLATE header;
WORD menu;
WORD window_class;
WORD title;
};

// Restores the thread's DPI awareness when the window is gone.
class ThreadDpiScope{
public:
explicit ThreadDpiScope(const DpiApi& api) noexcept:api_(api)
{
if(!api_.set_thread_context)return;
// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 (Windows 10 1703), else DPI_AWARENESS_CONTEXT_SYSTEM_AWARE.
previous_=api_.set_thread_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
if(!previous_)previous_=api_.set_thread_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-2)));
}
~ThreadDpiScope()
{
if(previous_)api_.set_thread_context(previous_);
}
ThreadDpiScope(const ThreadDpiScope&)=delete;
ThreadDpiScope& operator=(const ThreadDpiScope&)=delete;
private:
const DpiApi& api_;
HANDLE previous_=nullptr;
};

class ComScope{
public:
ComScope() noexcept:initialized_(SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE))){}
~ComScope()
{
if(initialized_)CoUninitialize();
}
ComScope(const ComScope&)=delete;
ComScope& operator=(const ComScope&)=delete;
private:
bool initialized_;
};

void run_window(Window& window,bool succeeded)
{
DialogTemplate dialog_template{};
dialog_template.header.style=WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN;
dialog_template.header.dwExtendedStyle=WS_EX_APPWINDOW;
const HWND dialog=CreateDialogIndirectParamW(GetModuleHandleW(nullptr),&dialog_template.header,nullptr,&dialog_proc,reinterpret_cast<LPARAM>(&window));
if(!dialog)return;
if(!window.output){
DestroyWindow(dialog);
return;
}
ShowWindow(dialog,SW_SHOWNORMAL);
// The first ShowWindow of a process may take the starter's show state; a window that would block unseen is shown anyway.
if(!IsWindowVisible(dialog))ShowWindow(dialog,SW_SHOWNORMAL);
// The foreground lock can refuse (the user kept working in another program during the build): a sound and a flashing taskbar
// button then make the window discoverable.
if(!SetForegroundWindow(dialog)||GetForegroundWindow()!=dialog){
MessageBeep(succeeded?MB_OK:MB_ICONERROR);
FLASHWINFO flash{};
flash.cbSize=sizeof flash;
flash.hwnd=dialog;
flash.dwFlags=FLASHW_ALL|FLASHW_TIMERNOFG;
FlashWindowEx(&flash);
}
SendMessageW(dialog,WM_NEXTDLGCTL,reinterpret_cast<WPARAM>(window.output),TRUE);
SendMessageW(window.output,EM_SETSEL,0,0);
SendMessageW(window.output,EM_SCROLLCARET,0,0);
MSG message;
while(IsWindow(dialog)){
const BOOL got=GetMessageW(&message,nullptr,0,0);
if(got==-1)break;
if(got==0){
// A WM_QUIT meant for an outer loop: the window closes and the quit is passed on.
PostQuitMessage(static_cast<int>(message.wParam));
break;
}
if(!IsDialogMessageW(dialog,&message)){
TranslateMessage(&message);
DispatchMessageW(&message);
}
}
if(IsWindow(dialog))DestroyWindow(dialog);
}
}

bool has_interactive_desktop() noexcept
{
wchar_t value[2]{};
if(GetEnvironmentVariableW(L"ZODIAC_FORCE_NO_DESKTOP",value,2)==1&&value[0]==L'1')return false;
const HWINSTA station=GetProcessWindowStation();
if(!station)return false;
USEROBJECTFLAGS flags{};
if(!GetUserObjectInformationW(station,UOI_FLAGS,&flags,sizeof flags,nullptr))return false;
return (flags.dwFlags&WSF_VISIBLE)!=0;
}

void show_result_window(std::string_view title,std::string_view text,bool succeeded,const std::filesystem::path* output_folder)
{
Window window;
window.title=widen(title);
window.text=edit_text(text);
window.folder=output_folder;
window.api=load_dpi_api();
const ComScope com;
{
const ThreadDpiScope dpi(window.api);
run_window(window,succeeded);
}
free_font(window);
}
}
