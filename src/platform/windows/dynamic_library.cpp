#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <core/dynamic_library.hpp>
#include <windows.h>
#include <cstddef>
#include <string>
#include <string_view>

namespace zodiac {
namespace {
// The operating system's description of an error code as one UTF-8 line, followed by the code itself.
// Inserts are not expanded by FormatMessageW, so every "%1" (error 193 and others name the file that way) becomes file_name.
std::string describe_error(DWORD code,std::wstring_view file_name)
{
std::string message;
wchar_t* text=nullptr;
const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
if(text){
std::wstring_view raw(text,length);
raw=raw.substr(0,raw.find_first_of(L"\r\n"));
while(!raw.empty()&&raw.front()==L' ')raw.remove_prefix(1);
while(!raw.empty()&&(raw.back()==L' '||raw.back()==L'.'))raw.remove_suffix(1);
std::wstring line;
line.reserve(raw.size()+file_name.size());
for(size_t at=0;at<raw.size();){
const size_t insert=raw.find(L"%1",at);
line.append(raw.substr(at,insert-at));
if(insert==std::wstring_view::npos)break;
line.append(file_name);
at=insert+2;
}
LocalFree(text);
const int size=line.empty()?0:WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),nullptr,0,nullptr,nullptr);
if(size>0){
message.resize(static_cast<size_t>(size));
WideCharToMultiByte(CP_UTF8,0,line.data(),static_cast<int>(line.size()),message.data(),size,nullptr,nullptr);
}
}
if(!message.empty())message+=' ';
message+="(error "+std::to_string(code)+")";
return message;
}
}

std::expected<DynamicLibrary,std::string> DynamicLibrary::open(const std::filesystem::path& path)
{
// The search flags require a fully qualified path with backslashes.
std::error_code error;
std::filesystem::path absolute=std::filesystem::absolute(path,error);
if(error)absolute=path;
absolute.make_preferred();
HMODULE handle=LoadLibraryExW(absolute.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
if(handle)return DynamicLibrary(handle);
const DWORD code=GetLastError();
return std::unexpected(describe_error(code,absolute.filename().native()));
}
void* DynamicLibrary::symbol(const char* name) const noexcept
{
return handle_?reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_),name)):nullptr;
}
void DynamicLibrary::close() noexcept
{
if(handle_)FreeLibrary(static_cast<HMODULE>(handle_));
handle_=nullptr;
}
}
