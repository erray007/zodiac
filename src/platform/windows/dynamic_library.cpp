#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <core/dynamic_library.hpp>
#include <windows.h>
#include <cstddef>
#include <string>

namespace zodiac {
namespace {
// The operating system's description of an error code as one UTF-8 line, followed by the code itself.
std::string describe_error(DWORD code)
{
std::string message;
wchar_t* text=nullptr;
const DWORD length=FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
if(text){
size_t end=length;
size_t start=0;
for(size_t i=0;i<end;++i){
if(text[i]==L'\r'||text[i]==L'\n'){
end=i;
break;
}
}
while(start<end&&text[start]==L' ')++start;
while(end>start&&(text[end-1]==L' '||text[end-1]==L'.'))--end;
const int size=end>start?WideCharToMultiByte(CP_UTF8,0,text+start,static_cast<int>(end-start),nullptr,0,nullptr,nullptr):0;
if(size>0){
message.resize(static_cast<size_t>(size));
WideCharToMultiByte(CP_UTF8,0,text+start,static_cast<int>(end-start),message.data(),size,nullptr,nullptr);
}
LocalFree(text);
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
return std::unexpected(describe_error(GetLastError()));
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
