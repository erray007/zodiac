// zodiac::DynamicLibrary owns one loaded shared library. Move-only; the library is unloaded when the last owner is destroyed.
// The implementation lives in src/platform/<os>/dynamic_library.cpp, so this header stays platform neutral.
#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <utility>

namespace zodiac {
class DynamicLibrary{
public:
DynamicLibrary() noexcept=default;
DynamicLibrary(const DynamicLibrary&)=delete;
DynamicLibrary& operator=(const DynamicLibrary&)=delete;
DynamicLibrary(DynamicLibrary&& other) noexcept:handle_(std::exchange(other.handle_,nullptr)){}
DynamicLibrary& operator=(DynamicLibrary&& other) noexcept
{
if(this!=&other){
close();
handle_=std::exchange(other.handle_,nullptr);
}
return *this;
}
~DynamicLibrary(){close();}
// On failure the error is the operating system's reason on a single line (UTF-8).
static std::expected<DynamicLibrary,std::string> open(const std::filesystem::path& path);
// Returns nullptr when the symbol is missing or nothing is loaded.
void* symbol(const char* name) const noexcept;
explicit operator bool() const noexcept{return handle_!=nullptr;}
private:
explicit DynamicLibrary(void* handle) noexcept:handle_(handle){}
void close() noexcept;
void* handle_=nullptr;
};
}
