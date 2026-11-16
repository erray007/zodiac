// Test fixture: prints each argument (UTF-8) on its own line to stdout, writes "err" to stderr and exits with the argument count.
// Used by test_process to prove that run_process passes arguments verbatim and merges both output streams.
// "--marker <file> <args...>" instead appends each of <args> (UTF-8) as one line to <file> and exits 0: the stand-in signing tool of the zodiac build tests.
#include <cstdio>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

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
if(argc>=3&&std::wstring(argv[1])==L"--marker"){
FILE* marker=_wfopen(argv[2],L"ab");
if(!marker)return 100;
for(int i=3;i<argc;++i){
const std::string line=to_utf8(argv[i])+"\n";
std::fwrite(line.data(),1,line.size(),marker);
}
return std::fclose(marker)==0?0:101;
}
for(int i=1;i<argc;++i){
const std::string line=to_utf8(argv[i])+"\n";
std::fwrite(line.data(),1,line.size(),stdout);
}
std::fflush(stdout);
std::fputs("err\n",stderr);
std::fflush(stderr);
return argc-1;
}
#else
int main(int argc,char** argv)
{
if(argc>=3&&std::string(argv[1])=="--marker"){
FILE* marker=std::fopen(argv[2],"ab");
if(!marker)return 100;
for(int i=3;i<argc;++i){
std::fputs(argv[i],marker);
std::fputc('\n',marker);
}
return std::fclose(marker)==0?0:101;
}
for(int i=1;i<argc;++i){
std::fputs(argv[i],stdout);
std::fputc('\n',stdout);
}
std::fflush(stdout);
std::fputs("err\n",stderr);
std::fflush(stderr);
return argc-1;
}
#endif
