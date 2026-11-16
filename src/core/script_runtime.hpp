// zodiac::ScriptRuntime owns one AngelScript engine with the std add-ons (string with utils, array, dictionary, exception routines),
// loads a project's modules into it, builds the project's scripts and runs main(). Every error goes to the Diagnostics it was given.
//
// build(): loads and registers the static and dynamic modules (all as shared libraries in dev mode, searched in the constructor's
// directories, then <project root>/plugins), then compiles main_script. #include paths resolve relative to the including file;
// a UTF-8 BOM is skipped. Script paths in messages are relative to Project::root with forward slashes. Returns 0, 1 (script error) or 2 (module error).
// save_bytecode(): after a successful build (or load_bytecode), the compiled module as AngelScript bytecode, with debug info (sections, lines) unless stripped.
// The bytecode is only valid against an identical registered interface: the same modules, registered in the same order (see module_order()).
// load_bytecode(): loads and registers modules exactly as build() does (names in linked use those infos, which must outlive the runtime), then loads bytecode
// into the script module and finds main() the same way. Returns 0, 1 (the bytecode does not match the registered interface or is incomplete:
// "error: the game data does not match the modules it was built with"; or a global variable failed to initialize) or 2 (module error).
// Never reads outside bytecode, which only has to stay alive during the call.
// run(): executes "int main()" or "void main()" and returns its value (0 for void), or 1 on an uncaught exception. Never exits the process.
// Runtime errors are reported identically from source and from bytecode with debug info; from stripped bytecode, a location without a line ends with " (line unknown)".
// Threads: construct, build, load_bytecode and run on the main thread.
#pragma once
#include <core/host.hpp>
#include <core/module_registry.hpp>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

class asIScriptContext;
class asIScriptEngine;
class asIScriptFunction;
class CScriptBuilder;
struct asSMessageInfo;

namespace zodiac {
class Diagnostics;
struct Project;
class ScriptRuntime{
public:
ScriptRuntime(Diagnostics& diagnostics,ModuleSearchPaths search_paths);
~ScriptRuntime();
ScriptRuntime(const ScriptRuntime&)=delete;
ScriptRuntime& operator=(const ScriptRuntime&)=delete;
Host& host() noexcept{return host_;}
int build(const Project& project);
std::expected<std::string,std::string> save_bytecode(bool strip_debug_info) const;
int load_bytecode(const Project& project,std::string_view bytecode,std::span<const zodiac_module_info* const> linked);
// The loaded module names in registration order; invalidated by the next build or load_bytecode.
std::span<const std::string> module_order() const noexcept{return registry_.ordered_names();}
int run();
private:
friend struct RuntimeCallbacks;
void on_message(const asSMessageInfo& info) noexcept;
int on_include(const char* include,const char* from,CScriptBuilder& builder);
int add_section(const std::filesystem::path& file,const std::string& section,CScriptBuilder& builder,std::string_view requested_by,std::string_view requested_name);
int load_modules(const Project& project,std::span<const zodiac_module_info* const> linked={});
// Resets the state shared by build and load_bytecode and loads the project's modules. Returns 0 or 2.
int prepare(const Project& project,std::span<const zodiac_module_info* const> linked);
// Sets main_ from the script module "main", or reports why it cannot. Returns 0 or 1.
int find_main();
std::string section_name(const std::filesystem::path& file) const;
void report_exception();
Diagnostics& diagnostics_;
ModuleSearchPaths search_paths_;
// Teardown contract: the destructor releases the context, discards the script module and runs a full garbage collection (so no script
// object outlives its module), calls registry_.shutdown_all(), shuts the engine down, then host_ and registry_ (last, unloading module code) are destroyed in reverse declaration order.
ModuleRegistry registry_;
asIScriptEngine* engine_;
Host host_;
asIScriptContext* context_=nullptr;
asIScriptFunction* main_=nullptr;
std::filesystem::path root_;
std::string main_section_;
std::unordered_set<std::string> included_;
// True when the script module came from bytecode saved without debug info.
bool debug_info_stripped_=false;
// While set, engine messages are kept here instead of being reported (load_bytecode decides afterwards whether they are meaningful).
std::vector<std::pair<zodiac_log_level,std::string>>* held_messages_=nullptr;
};
}
