#include <platform/process.hpp>
#include <string>
#include <system_error>
#include <vector>

namespace zodiac::platform {
std::filesystem::path executable_path()
{
std::error_code error;
std::filesystem::path path=std::filesystem::read_symlink("/proc/self/exe",error);
if(error)return {};
return path;
}
}

int main(int argc,char** argv)
{
std::vector<std::string> args;
args.reserve(argc>1?static_cast<std::size_t>(argc)-1:0);
for(int i=1;i<argc;++i)args.emplace_back(argv[i]);
return zodiac::cli::run(args);
}
