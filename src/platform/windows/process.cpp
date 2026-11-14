#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/process.hpp>
#include <windows.h>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace zodiac::platform {
std::filesystem::path executable_path()
{
std::wstring buffer(MAX_PATH,L'\0');
for(;;){
const DWORD length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
if(length==0)return {};
if(length<buffer.size()){
buffer.resize(length);
return std::filesystem::path(std::move(buffer));
}
if(buffer.size()>=32768)return {};
buffer.resize(buffer.size()*2);
}
}
}

namespace {
std::string to_utf8(const wchar_t* text)
{
const int count=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
if(count<=1)return {};
std::string result(static_cast<std::size_t>(count)-1,'\0');
WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),count,nullptr,nullptr);
return result;
}
}

int wmain(int argc,wchar_t** argv)
{
std::vector<std::string> args;
args.reserve(argc>1?static_cast<std::size_t>(argc)-1:0);
for(int i=1;i<argc;++i)args.push_back(to_utf8(argv[i]));
return zodiac::cli::run(args);
}
