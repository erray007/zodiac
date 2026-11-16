// The Windows entry points of the command line (see platform/process.hpp): wmain for zodiac.exe (console subsystem) and, when compiled with
// ZODIAC_GUI_ENTRY, wWinMain for zodiacw.exe (GUI subsystem, linked with -mwindows -municode), which attaches to the starter's console when it
// has one, so a zodiacw started from a terminal still reports there; started from Explorer it has none, and every console write is dropped.
// Both convert the arguments (without the program name) to UTF-8 and call zodiac::cli::run.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/process.hpp>
#include <windows.h>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>
#ifdef ZODIAC_GUI_ENTRY
#include <platform/console.hpp>
#endif

namespace {
std::string to_utf8(const wchar_t* text)
{
const int count=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
if(count<=1)return {};
std::string result(static_cast<std::size_t>(count)-1,'\0');
WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),count,nullptr,nullptr);
return result;
}

int run(int argc,wchar_t** argv)
{
std::vector<std::string> args;
args.reserve(argc>1?static_cast<std::size_t>(argc)-1:0);
for(int i=1;i<argc;++i)args.push_back(to_utf8(argv[i]));
return zodiac::cli::run(args);
}
}

#ifdef ZODIAC_GUI_ENTRY
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
zodiac::platform::attach_parent_console();
// The C runtime splits the command line exactly as it does for wmain.
return run(__argc,__wargv);
}
#else
int wmain(int argc,wchar_t** argv)
{
return run(argc,argv);
}
#endif
