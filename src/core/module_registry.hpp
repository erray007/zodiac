// zodiac::ModuleRegistry finds, loads, orders and registers Zodiac modules (shared libraries exporting zodiac_module_get_info).
//
// Teardown order (the registry owns the libraries, and the engine may point into module code):
// 1. call shutdown_all() while the Host and script engine are still alive, 2. destroy the engine and Host, 3. destroy the registry.
// The destructor only closes libraries; it never calls shutdown_module. Threads: every call runs on the main thread.
#pragma once
#include <zodiac/plugin.h>
#include <core/dynamic_library.hpp>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac {
class Host;
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
// On failure nothing from this call stays loaded and the error is one plain-text line.
std::expected<void,std::string> load(std::span<const std::string> names,const ModuleSearchPaths& search);
// Registers every loaded, not yet registered module in order with host.current_module() set to the module name.
// A module fails if register_module returns non-OK or the host rejected one of its registrations (Host::registration_failures). A module's own log(ERROR) is forwarded but does not fail it. Stops at the first failure.
// Messages logged during registration are still forwarded to the host's existing log handler.
std::expected<void,std::string> register_all(Host& host);
// Calls shutdown_module on the successfully registered modules in reverse registration order (once each). Libraries stay loaded until the registry is destroyed. Call it before destroying the engine and Host.
void shutdown_all() noexcept;
std::span<const zodiac_module_info* const> ordered() const noexcept{return infos_;}
private:
struct Entry{
std::string name;
DynamicLibrary library;
const zodiac_module_info* info;
bool registered;
};
std::expected<void,std::string> resolve(std::string_view name,const ModuleSearchPaths& search,std::vector<std::string>& stack);
std::vector<Entry> entries_;
std::vector<const zodiac_module_info*> infos_;
};
}
