// zodiac::ModuleRegistry finds, loads, orders and registers Zodiac modules (shared libraries exporting zodiac_module_get_info, or infos linked into the executable).
//
// Teardown order (the registry owns the libraries, and the engine may point into module code):
// 1. release every script object (discard the script modules, run a full garbage collection), then call shutdown_all() while the Host and script engine are still alive, 2. destroy the engine and Host, 3. destroy the registry.
// The destructor only closes libraries; it never calls shutdown_module. Threads: every call runs on the main thread.
#pragma once
#include <zodiac/plugin.h>
#include <core/dynamic_library.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac {
class Host;
// Size of zodiac_module_info in ABI v1. It never changes when members are appended: a module whose struct_size is smaller is refused,
// and a member appended later is read only when the module's struct_size covers it (see module_info_provides).
// It is frozen at the end of the last v1 member (shutdown_module) instead of sizeof(zodiac_module_info), which would grow with the struct and make a newer host refuse every v1 module.
inline constexpr uint32_t module_info_v1_size=offsetof(zodiac_module_info,shutdown_module)+sizeof(void(*)(void));
static_assert(module_info_v1_size==64,"zodiac_module_info v1 size is frozen (64-bit targets)");
// True when info, as built by its module, contains the bytes up to field_end (offsetof plus sizeof of an appended member).
constexpr bool module_info_provides(const zodiac_module_info& info,std::size_t field_end) noexcept{return info.struct_size>=field_end;}
// End of the link_modes member, the first one appended after v1.
inline constexpr std::size_t module_info_link_modes_end=offsetof(zodiac_module_info,link_modes)+sizeof(uint32_t);
// The module's ZODIAC_LINK_* bits; ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC when the member is absent (a v1 info) or 0.
constexpr uint32_t module_link_modes(const zodiac_module_info& info) noexcept
{
const uint32_t modes=module_info_provides(info,module_info_link_modes_end)?info.link_modes:0u;
return modes?modes:(ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC);
}
// The shared library file name the registry looks for: "zodiac_mod_<name>" plus the platform's shared library extension.
std::string module_file_name(std::string_view name);
// A module library found in a module folder: the file and its name part (between "zodiac_mod_" and the extension, not validated).
struct InstalledModule{
std::filesystem::path file;
std::string name;
};
// Every regular file in dirs named "zodiac_mod_<name>" plus the platform's shared library extension, in search order; a file name already found in an
// earlier folder is skipped, as it is when a game loads the module. Unreadable folders are skipped.
std::vector<InstalledModule> find_installed_modules(std::span<const std::filesystem::path> dirs);
struct ModuleSearchPaths{
std::vector<std::filesystem::path> dirs;
// <exe_dir>/../modules, then <project_dir>/plugins.
static ModuleSearchPaths defaults(const std::filesystem::path& exe_dir,const std::filesystem::path& project_dir);
};
class ModuleRegistry{
public:
ModuleRegistry()=default;
ModuleRegistry(const ModuleRegistry&)=delete;
ModuleRegistry& operator=(const ModuleRegistry&)=delete;
~ModuleRegistry()=default;
// Loads the named modules and their dependencies transitively, ordered so dependencies come first. Names already loaded are skipped.
// A name matching the name of an info in linked (modules linked into the executable, e.g. linked_static_modules(); null entries are ignored) uses that info and loads no library;
// any other name is searched for as a shared library. Both kinds are validated identically (ABI version, struct_size, name) and may depend on each other.
// The linked infos must outlive the registry. On failure nothing from this call stays loaded and the error is one plain-text line.
std::expected<void,std::string> load(std::span<const std::string> names,const ModuleSearchPaths& search,std::span<const zodiac_module_info* const> linked={});
// Registers every loaded, not yet registered module in order with host.current_module() set to the module name.
// A module fails if register_module returns non-OK or the host rejected one of its registrations (Host::registration_failures). A module's own log(ERROR) is forwarded but does not fail it. Stops at the first failure.
// Messages logged during registration are still forwarded to the host's existing log handler.
std::expected<void,std::string> register_all(Host& host);
// Calls shutdown_module on the successfully registered modules in reverse registration order (once each). Libraries stay loaded until the registry is destroyed. Call it before destroying the engine and Host.
void shutdown_all() noexcept;
std::span<const zodiac_module_info* const> ordered() const noexcept{return infos_;}
// The loaded module names in registration order (parallel to ordered()).
std::span<const std::string> ordered_names() const noexcept{return names_;}
private:
struct Entry{
DynamicLibrary library; // empty for a linked module
const zodiac_module_info* info;
bool registered;
};
std::expected<void,std::string> resolve(std::string_view name,const ModuleSearchPaths& search,std::span<const zodiac_module_info* const> linked,std::vector<std::string>& stack);
std::vector<Entry> entries_;
std::vector<const zodiac_module_info*> infos_;
std::vector<std::string> names_;
};
}
