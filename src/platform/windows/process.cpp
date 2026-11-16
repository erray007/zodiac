#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/process.hpp>
#include <windows.h>
#include <cstddef>
#include <string>
#include <vector>

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
