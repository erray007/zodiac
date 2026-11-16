// The zodiac command line: new, run, modules, help and --version. A thin front end over zodiac_core; the platform layer owns the entry point.
// Normal output (help, listings, --version) goes to standard output through platform::write_stdout_line; every error goes through Diagnostics.
#include <cli/templates.hpp>
#include <core/diagnostics.hpp>
#include <core/dynamic_library.hpp>
#include <core/module_registry.hpp>
#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <core/script_runtime.hpp>
#include <core/text.hpp>
#include <platform/console.hpp>
#include <platform/process.hpp>
#include <zodiac/plugin.h>
#include <algorithm>
#include <exception>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef ZODIAC_VERSION
#error ZODIAC_VERSION must be defined by the build (the project version)
#endif
#ifndef ZODIAC_MODULE_SUFFIX
#error ZODIAC_MODULE_SUFFIX must be defined by the build (the shared library extension of the target platform)
#endif

namespace zodiac::cli {
namespace {
namespace fs=std::filesystem;

constexpr int exit_ok=0;
constexpr int exit_usage_error=2;

struct Options{
std::string command;
std::vector<std::string> positional;
std::vector<fs::path> module_dirs;
fs::path log_file;
// Set when the command line is malformed; reported through Diagnostics once it exists.
std::string error;
};

std::string in_quotes(std::string_view text)
{
std::string result;
result.reserve(text.size()+2);
result.push_back('"');
result.append(text);
result.push_back('"');
return result;
}

void fail(Diagnostics& diagnostics,std::string_view message) noexcept
{
diagnostics.report(ZODIAC_LOG_ERROR,message);
}

Options parse_arguments(std::span<const std::string> args)
{
Options options;
for(std::size_t i=0;i<args.size();++i){
const std::string& arg=args[i];
if(arg=="--module-dir"||arg=="--log-file"){
if(i+1>=args.size()){
options.error="error: "+arg+" needs a value";
return options;
}
const std::string& value=args[++i];
if(arg=="--module-dir")options.module_dirs.push_back(path_from_utf8(value));
else options.log_file=path_from_utf8(value);
continue;
}
if(options.command.empty()&&(arg=="--version"||arg=="--help"||arg=="-h")){
options.command=arg=="--version"?"version":"help";
continue;
}
if(arg.size()>1&&arg.front()=='-'){
options.error="error: unknown option "+in_quotes(arg)+"; run zodiac help";
return options;
}
if(options.command.empty())options.command=arg;
else options.positional.push_back(arg);
}
return options;
}

// Adds the log file sink requested with --log-file. False (already reported) when it cannot be opened.
bool add_log_file(Diagnostics& diagnostics,const fs::path& file)
{
return file.empty()||diagnostics.add_file_sink(file);
}

std::vector<fs::path> search_dirs(const Options& options)
{
std::vector<fs::path> dirs=options.module_dirs;
const fs::path exe=platform::executable_path();
if(!exe.empty())dirs.push_back((exe.parent_path()/".."/"modules").lexically_normal());
return dirs;
}

int print_help()
{
platform::write_stdout_line("zodiac new <dir>: create a new game project in an empty directory");
platform::write_stdout_line("zodiac run [project_dir]: build and run the game in project_dir (default: the current directory)");
platform::write_stdout_line("zodiac modules: list the modules found in the module directories");
platform::write_stdout_line("zodiac help: show this list");
platform::write_stdout_line("zodiac --version: show the version");
platform::write_stdout_line("options for run and modules: --module-dir <dir> adds a module directory (repeatable), --log-file <path> also writes messages to a file");
platform::write_stdout_line("exit codes: 0 success, 1 script error, 2 usage/project/module error, otherwise the value returned by int main() (0-255)");
return exit_ok;
}

int print_version()
{
platform::write_stdout_line("zodiac " ZODIAC_VERSION);
return exit_ok;
}

int run_project(const Options& options,Diagnostics& diagnostics)
{
if(options.positional.size()>1){
fail(diagnostics,"error: run takes at most one project directory; run zodiac help");
return exit_usage_error;
}
// --log-file goes first so that a project file error is captured as well.
if(!add_log_file(diagnostics,options.log_file))return exit_usage_error;
const fs::path directory=options.positional.empty()?fs::path("."):path_from_utf8(options.positional.front());
auto project=load_project(directory);
if(!project){
fail(diagnostics,project.error());
return exit_usage_error;
}
if(options.log_file.empty()&&!project->diagnostics.file.empty()&&!diagnostics.add_file_sink(project->diagnostics.file))return exit_usage_error;
ScriptRuntime runtime(diagnostics,ModuleSearchPaths{search_dirs(options)});
if(const int code=runtime.build(*project);code!=0)return code;
// After the build: relative --module-dir and --log-file values are relative to the directory zodiac was started in.
if(const int code=enter_root(project->root,diagnostics);code!=0)return code;
return exit_code_of(runtime.run(),diagnostics);
}

struct ModuleLine{
std::string name;
std::string text;
};

std::string_view or_empty(const char* text) noexcept
{
return text?std::string_view(text):std::string_view();
}

// Loads the library just far enough to read its info; nothing is registered. False when the file is not a usable module (already reported).
bool read_module(const fs::path& file,Diagnostics& diagnostics,std::vector<ModuleLine>& lines)
{
const std::string shown=path_to_utf8(file);
auto library=DynamicLibrary::open(file);
if(!library){
fail(diagnostics,"error: cannot read module "+in_quotes(shown)+": "+library.error());
return false;
}
using EntryFunction=const zodiac_module_info*(*)(void);
void* symbol=library->symbol(ZODIAC_MODULE_ENTRY_NAME);
if(!symbol){
fail(diagnostics,"error: cannot read module "+in_quotes(shown)+": it has no "+ZODIAC_MODULE_ENTRY_NAME+" entry");
return false;
}
const zodiac_module_info* info=reinterpret_cast<EntryFunction>(symbol)();
if(!info){
fail(diagnostics,"error: cannot read module "+in_quotes(shown)+": it returned no module info");
return false;
}
if(!module_info_provides(*info,module_info_v1_size)){
fail(diagnostics,"error: cannot read module "+in_quotes(shown)+": it has an invalid info structure");
return false;
}
// The strings live in the library, so they are copied before it is unloaded.
ModuleLine line;
line.name=or_empty(info->name);
line.text.reserve(line.name.size()+shown.size()+32);
line.text.append(line.name).push_back(' ');
line.text.append(or_empty(info->version)).push_back(' ');
line.text.append(or_empty(info->license)).append(" (").append(shown).push_back(')');
lines.push_back(std::move(line));
return true;
}

int list_modules(const Options& options,Diagnostics& diagnostics)
{
if(!options.positional.empty()){
fail(diagnostics,"error: modules takes no arguments; run zodiac help");
return exit_usage_error;
}
if(!add_log_file(diagnostics,options.log_file))return exit_usage_error;
constexpr std::string_view prefix="zodiac_mod_";
std::vector<ModuleLine> lines;
std::vector<std::string> seen;
for(const fs::path& dir:search_dirs(options)){
std::error_code error;
fs::directory_iterator it(dir,error);
if(error)continue;
const fs::directory_iterator end;
for(;it!=end;it.increment(error)){
if(error)break;
const fs::path file=it->path();
const std::u8string name=file.filename().u8string();
const std::string_view text(reinterpret_cast<const char*>(name.data()),name.size());
if(!text.starts_with(prefix)||file.extension()!=ZODIAC_MODULE_SUFFIX)continue;
std::error_code status;
if(!it->is_regular_file(status))continue;
// The first directory in the search order wins, exactly as it does when a game loads the module.
if(std::ranges::find(seen,text)!=seen.end())continue;
seen.emplace_back(text);
read_module(file,diagnostics,lines);
}
}
std::ranges::sort(lines,[](const ModuleLine& a,const ModuleLine& b){return a.name!=b.name?a.name<b.name:a.text<b.text;});
for(const ModuleLine& line:lines)platform::write_stdout_line(line.text);
return exit_ok;
}

// A TOML basic string body: backslash, quote and control characters escaped, everything else (UTF-8) unchanged.
std::string toml_escaped(std::string_view text)
{
std::string result;
result.reserve(text.size());
for(const char c:text){
const unsigned char byte=static_cast<unsigned char>(c);
if(c=='\\'||c=='"')result.push_back('\\'),result.push_back(c);
else if(byte<0x20||byte==0x7F){
constexpr std::string_view digits="0123456789ABCDEF";
result.append("\\u00").push_back(digits[byte>>4]);
result.push_back(digits[byte&15]);
}
else result.push_back(c);
}
return result;
}

bool write_file(const fs::path& file,std::string_view first,std::string_view middle,std::string_view last,Diagnostics& diagnostics)
{
std::ofstream out(file,std::ios::binary|std::ios::trunc);
out.write(first.data(),static_cast<std::streamsize>(first.size()));
out.write(middle.data(),static_cast<std::streamsize>(middle.size()));
out.write(last.data(),static_cast<std::streamsize>(last.size()));
out.close();
if(out)return true;
fail(diagnostics,"error: cannot write "+in_quotes(path_to_utf8(file)));
return false;
}

int create_project(const Options& options,Diagnostics& diagnostics)
{
if(options.positional.size()!=1){
fail(diagnostics,options.positional.empty()?"error: new needs a directory; run zodiac help":"error: new takes one directory; run zodiac help");
return exit_usage_error;
}
if(!add_log_file(diagnostics,options.log_file))return exit_usage_error;
const std::string& shown=options.positional.front();
const fs::path directory=path_from_utf8(shown);
std::error_code error;
const bool exists=fs::exists(directory,error);
if(exists){
if(!fs::is_directory(directory,error)){
fail(diagnostics,"error: "+in_quotes(shown)+" is not a directory");
return exit_usage_error;
}
if(fs::directory_iterator(directory,error)!=fs::directory_iterator()){
fail(diagnostics,"error: directory "+in_quotes(shown)+" is not empty");
return exit_usage_error;
}
if(error){
fail(diagnostics,"error: cannot read directory "+in_quotes(shown)+": "+error_text(error));
return exit_usage_error;
}
}
else{
error.clear();
fs::create_directories(directory,error);
if(error){
fail(diagnostics,"error: cannot create directory "+in_quotes(shown)+": "+error_text(error));
return exit_usage_error;
}
}
// The game name is the directory's own name, also for ".", "..", trailing separators and relative paths.
fs::path absolute=fs::absolute(directory,error).lexically_normal();
if(!absolute.has_filename())absolute=absolute.parent_path();
const std::string name=path_to_utf8(absolute.filename());
if(!write_file(directory/"zodiac.toml",toml_before_name,toml_escaped(name.empty()?std::string_view("game"):std::string_view(name)),toml_after_name,diagnostics))return exit_usage_error;
if(!write_file(directory/"main.zs",main_script,{},{},diagnostics))return exit_usage_error;
platform::write_stdout_line("created project "+in_quotes(shown));
return exit_ok;
}
}

int run(std::span<const std::string> args)
{
platform::disable_error_dialogs();
platform::setup_utf8_console();
Diagnostics diagnostics;
bool console_ready=false;
try{
const Options options=parse_arguments(args);
// The console sink goes first: a log file that cannot be opened is reported to the sinks added before it.
diagnostics.add_sink(std::make_unique<ConsoleSink>());
console_ready=true;
if(!options.error.empty()){
fail(diagnostics,options.error);
return exit_usage_error;
}
if(options.command.empty()){
fail(diagnostics,"error: no command given; run zodiac help");
return exit_usage_error;
}
if(options.command=="help")return print_help();
if(options.command=="version")return print_version();
if(options.command=="run")return run_project(options,diagnostics);
if(options.command=="modules")return list_modules(options,diagnostics);
if(options.command=="new")return create_project(options,diagnostics);
fail(diagnostics,"error: unknown command "+in_quotes(options.command)+"; run zodiac help");
return exit_usage_error;
}
catch(...){
// Nothing may reach std::terminate: on Windows that could show an abort dialog.
std::string message="error: internal error: ";
try{
try{
throw;
}
catch(const std::exception& exception){
message.append(exception.what());
}
catch(...){
message.append("unknown exception");
}
}
catch(...){
message="error: internal error";
}
if(console_ready)diagnostics.report(ZODIAC_LOG_ERROR,message);
else platform::write_stderr_line(message);
return 1;
}
}
}
