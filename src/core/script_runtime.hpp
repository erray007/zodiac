// zodiac::ScriptRuntime owns one AngelScript engine with the std add-ons (string with utils, array, dictionary, exception routines),
// loads a project's modules into it, builds the project's scripts and runs main(). Every error goes to the Diagnostics it was given.
//
// build(): loads and registers the static and dynamic modules (all as shared libraries in dev mode, searched in the constructor's
// directories, then <project root>/plugins), then compiles main_script. #include paths resolve relative to the including file;
// a UTF-8 BOM is skipped. Script paths in messages are relative to Project::root with forward slashes. Returns 0, 1 (script error) or 2 (module error).
// run(): executes "int main()" or "void main()" and returns its value (0 for void), or 1 on an uncaught exception. Never exits the process.
// Threads: construct, build and run on the main thread.
#pragma once
#include <core/host.hpp>
#include <core/module_registry.hpp>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>

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
int run();
private:
friend struct RuntimeCallbacks;
void on_message(const asSMessageInfo& info) noexcept;
int on_include(const char* include,const char* from,CScriptBuilder& builder);
int add_section(const std::filesystem::path& file,const std::string& section,CScriptBuilder& builder,std::string_view requested_by,std::string_view requested_name);
int load_modules(const Project& project);
std::string section_name(const std::filesystem::path& file) const;
void report_exception();
Diagnostics& diagnostics_;
ModuleSearchPaths search_paths_;
// Teardown contract: the destructor calls registry_.shutdown_all(), releases the context, shuts the engine down, then host_ and registry_ (last, unloading module code) are destroyed in reverse declaration order.
ModuleRegistry registry_;
asIScriptEngine* engine_;
Host host_;
asIScriptContext* context_=nullptr;
asIScriptFunction* main_=nullptr;
std::filesystem::path root_;
std::string main_section_;
std::unordered_set<std::string> included_;
};
}
