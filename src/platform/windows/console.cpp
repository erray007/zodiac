#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/console.hpp>
#include <windows.h>
#include <io.h>
#include <climits>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <string>

namespace zodiac::platform {
namespace {
// The handle behind C runtime descriptor fd, so redirections made through the runtime (dup2, freopen) are honoured.
HANDLE stream_handle(int fd,DWORD standard) noexcept
{
const std::intptr_t handle=_get_osfhandle(fd);
if(handle!=-1&&handle!=-2)return reinterpret_cast<HANDLE>(handle);
return GetStdHandle(standard);
}
template<typename Char,typename Write>
void write_all(const Char* data,std::size_t size,Write write) noexcept
{
while(size>0){
const DWORD chunk=static_cast<DWORD>(size>0x7FFFFFFFu?0x7FFFFFFFu:size);
DWORD written=0;
if(!write(data,chunk,&written)||written==0)return;
data+=written;
size-=written;
}
}
}
void disable_error_dialogs() noexcept
{
SetErrorMode(GetErrorMode()|SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
// abort() (also reached through std::terminate) must neither print a message box nor start Windows error reporting.
_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
}
void setup_utf8_console() noexcept
{
SetConsoleOutputCP(CP_UTF8);
SetConsoleCP(CP_UTF8);
}
namespace {
void write_line(HANDLE handle,std::string_view line) noexcept
{
if(handle==nullptr||handle==INVALID_HANDLE_VALUE)return;
// MultiByteToWideChar counts in int; longer lines are cut, which no diagnostic ever reaches.
if(line.size()>static_cast<std::size_t>(INT_MAX)-1)line=line.substr(0,static_cast<std::size_t>(INT_MAX)-1);
try{
DWORD mode=0;
if(GetConsoleMode(handle,&mode)){
// The console gets UTF-16 so it shows every character whatever its code page is.
thread_local std::wstring wide;
const int source_size=static_cast<int>(line.size());
const int count=source_size?MultiByteToWideChar(CP_UTF8,0,line.data(),source_size,nullptr,0):0;
wide.resize_and_overwrite(static_cast<std::size_t>(count)+1,[&](wchar_t* buffer,std::size_t size){
if(count>0)MultiByteToWideChar(CP_UTF8,0,line.data(),source_size,buffer,count);
buffer[count]=L'\n';
return size;
});
write_all(wide.data(),wide.size(),[handle](const wchar_t* data,DWORD size,DWORD* written){return WriteConsoleW(handle,data,size,written,nullptr)!=0;});
return;
}
thread_local std::string bytes;
bytes.resize_and_overwrite(line.size()+1,[&](char* buffer,std::size_t size){
line.copy(buffer,line.size());
buffer[line.size()]='\n';
return size;
});
write_all(bytes.data(),bytes.size(),[handle](const char* data,DWORD size,DWORD* written){return WriteFile(handle,data,size,written,nullptr)!=0;});
}
catch(...){
}
}
}
void write_stderr_line(std::string_view line) noexcept
{
write_line(stream_handle(2,STD_ERROR_HANDLE),line);
}
void write_stdout_line(std::string_view line) noexcept
{
write_line(stream_handle(1,STD_OUTPUT_HANDLE),line);
}
}
