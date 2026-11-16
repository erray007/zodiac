// Fixture module for the working directory rule (dev: the project root, release: the executable's folder).
// Registers string cwd(): the process's working directory resolved with std::filesystem::canonical (on Windows that gives long names
// and the file system's letter case), with '/' separators, as UTF-8; the unresolved path when it cannot be resolved, empty when it cannot be read.
#include <zodiac/plugin.h>
#include <zodiac/plugin.hpp>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace {
std::string cwd()
{
std::error_code error;
std::filesystem::path path=std::filesystem::current_path(error);
if(error)return {};
std::filesystem::path resolved=std::filesystem::canonical(path,error);
if(!error)path=std::move(resolved);
const std::u8string text=path.generic_u8string();
return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
int register_module(zodiac_host* host)
{
return zodiac::plugin::function<&cwd>(host,"string cwd()");
}
const zodiac_module_info g_info={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"cwd_probe","0.0.1","Zlib",nullptr,0,&register_module,nullptr,ZODIAC_LINK_DYNAMIC};
}

ZODIAC_DEFINE_MODULE(g_info)
