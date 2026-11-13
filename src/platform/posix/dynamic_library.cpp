#include <core/dynamic_library.hpp>
#include <dlfcn.h>
#include <cstddef>
#include <string>

namespace zodiac {
std::expected<DynamicLibrary,std::string> DynamicLibrary::open(const std::filesystem::path& path)
{
void* handle=dlopen(path.c_str(),RTLD_NOW|RTLD_LOCAL);
if(handle)return DynamicLibrary(handle);
const char* reason=dlerror();
std::string message(reason?reason:"unknown error");
const size_t end=message.find_first_of("\r\n");
if(end!=std::string::npos)message.resize(end);
return std::unexpected(std::move(message));
}
void* DynamicLibrary::symbol(const char* name) const noexcept
{
return handle_?dlsym(handle_,name):nullptr;
}
void DynamicLibrary::close() noexcept
{
if(handle_)dlclose(handle_);
handle_=nullptr;
}
}
