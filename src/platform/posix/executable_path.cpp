#include <platform/process.hpp>
#include <system_error>

namespace zodiac::platform {
std::filesystem::path executable_path()
{
std::error_code error;
std::filesystem::path path=std::filesystem::read_symlink("/proc/self/exe",error);
if(error)return {};
return path;
}
}
