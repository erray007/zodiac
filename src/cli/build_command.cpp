// zodiac::cli::build_project (see cli/build_command.hpp): loads, checks and compiles a project, then links, copies and signs its release.
#include <cli/build_command.hpp>
#include <cli/build_report.hpp>
#include <core/path_utf8.hpp>
#include <platform/console.hpp>
#include <platform/result_window.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#ifdef _WIN32
#include <cli/linker.hpp>
#include <core/coff_object.hpp>
#include <core/kit_layout.hpp>
#include <core/module_registry.hpp>
#include <core/payload.hpp>
#include <core/project_file.hpp>
#include <core/script_runtime.hpp>
#include <core/text.hpp>
#include <platform/process.hpp>
#include <zodiac/plugin.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <expected>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <system_error>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#ifndef ZODIAC_VERSION
#error ZODIAC_VERSION must be defined by the build (the project version)
#endif

namespace zodiac::cli {
namespace {
constexpr int exit_build_error=2;

// What a build produced, also when it failed part way: the executable's path (empty until known) and the game's name (empty until the project loads).
struct BuildOutcome{
std::filesystem::path exe;
std::string game_name;
};

#ifdef _WIN32
namespace fs=std::filesystem;
constexpr int exit_ok=0;

std::string in_quotes(std::string_view text)
{
std::string result;
result.reserve(text.size()+2);
result.push_back('"');
result.append(text);
result.push_back('"');
return result;
}

std::string quoted_path(const fs::path& path)
{
return in_quotes(path_to_utf8(path));
}

int fail(Diagnostics& diagnostics,std::string_view message) noexcept
{
diagnostics.report(ZODIAC_LOG_ERROR,message);
return exit_build_error;
}

std::string_view or_empty(const char* text) noexcept
{
return text?std::string_view(text):std::string_view();
}

bool contains(std::span<const std::string> names,std::string_view name) noexcept
{
return std::ranges::find(names,name)!=names.end();
}

// Removes a temporary file of the build when the build is over, whatever happened; a file that is already gone is fine.
class TemporaryFile{
public:
TemporaryFile(fs::path path,Diagnostics& diagnostics):path_(std::move(path)),diagnostics_(diagnostics){}
~TemporaryFile()
{
std::error_code ec;
fs::remove(path_,ec);
if(ec)diagnostics_.report(ZODIAC_LOG_WARNING,"warning: cannot remove "+quoted_path(path_)+": "+error_text(ec));
}
TemporaryFile(const TemporaryFile&)=delete;
TemporaryFile& operator=(const TemporaryFile&)=delete;
const fs::path& path() const noexcept{return path_;}
private:
fs::path path_;
Diagnostics& diagnostics_;
};

// A whole file, or nullopt when it cannot be read.
std::optional<std::string> read_text(const fs::path& path)
{
std::ifstream in(path,std::ios::binary);
if(!in)return std::nullopt;
std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
if(in.bad())return std::nullopt;
return text;
}

std::string_view without_trailing_space(std::string_view text) noexcept
{
while(!text.empty()&&(text.back()=='\n'||text.back()=='\r'||text.back()==' '||text.back()=='\t'))text.remove_suffix(1);
return text;
}

bool write_binary(const fs::path& path,std::string_view bytes)
{
std::ofstream out(path,std::ios::binary|std::ios::trunc);
out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
out.close();
return !out.fail();
}

fs::path absolute_normal(const fs::path& path)
{
std::error_code ec;
fs::path result=fs::absolute(path,ec);
if(ec)result=path;
result=result.lexically_normal();
if(!result.has_filename()&&result.has_relative_path())result=result.parent_path();
return result;
}

// Windows file names compare case-insensitively with the file system's own (ordinal, upper-case table) rule.
bool same_name(const fs::path& a,const fs::path& b) noexcept
{
const std::wstring& left=a.native();
const std::wstring& right=b.native();
return CompareStringOrdinal(left.c_str(),static_cast<int>(left.size()),right.c_str(),static_cast<int>(right.size()),TRUE)==CSTR_EQUAL;
}

// True when path lies below dir; both absolute and normalized. Each element is compared as the file system does, so "D:\Game\Release" is inside "d:\game".
bool is_inside(const fs::path& path,const fs::path& dir)
{
auto element=path.begin();
for(const fs::path& part:dir){
if(element==path.end()||!same_name(*element,part))return false;
++element;
}
return element!=path.end();
}

// True when name+".exe" is a plain file name on Windows: no separators, reserved or control characters, no trailing dot or space, no device name.
bool usable_file_name(std::string_view name) noexcept
{
if(name.empty()||name=="."||name==".."||name.back()=='.'||name.back()==' ')return false;
for(const char c:name){
const unsigned char byte=static_cast<unsigned char>(c);
if(byte<0x20||byte==0x7F||std::string_view("<>:\"/\\|?*").find(c)!=std::string_view::npos)return false;
}
// Device names stay reserved with any extension, case-insensitively ("con.exe" is the console).
const std::string_view stem=name.substr(0,name.find('.'));
std::string upper(stem);
for(char& c:upper)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
constexpr std::array<std::string_view,4> devices{"CON","PRN","AUX","NUL"};
if(std::ranges::find(devices,upper)!=devices.end())return false;
if(upper.size()==4&&(upper.starts_with("COM")||upper.starts_with("LPT"))&&upper[3]>='0'&&upper[3]<='9')return false;
return true;
}

// The folder the project of target lives in: target itself, or for a file the nearest folder above it holding zodiac.toml.
std::optional<fs::path> project_root_of(const fs::path& target)
{
std::error_code ec;
if(fs::is_regular_file(target,ec))return find_project_root(target);
const fs::path root=absolute_normal(target);
if(!fs::is_regular_file(root/"zodiac.toml",ec)||ec)return std::nullopt;
return root;
}

struct StaticLibrary{
std::string name;
fs::path archive;
};

constexpr std::array<std::string_view,9> permissive_licenses{"Zlib","MIT","BSD-2-Clause","BSD-3-Clause","Apache-2.0","BSL-1.0","ISC","Unlicense","CC0-1.0"};

// Step 2 of the build: runs while the modules are loaded and none is registered yet. Fills libraries in registration order. Returns 0 or 2.
int check_static_modules(const Project& project,const KitLayout& kit,std::span<const zodiac_module_info* const> loaded,std::vector<StaticLibrary>& libraries,Diagnostics& diagnostics)
{
int result=exit_ok;
std::optional<std::optional<std::string>> toolchain;
for(const zodiac_module_info* info:loaded){
const std::string_view name=or_empty(info->name);
if(!contains(project.static_modules,name))continue;
const std::string_view license=or_empty(info->license);
const std::string quoted_name=in_quotes(name);
if(!(module_link_modes(*info)&ZODIAC_LINK_STATIC)){
result=fail(diagnostics,"error: module "+quoted_name+" ("+std::string(license)+") cannot be linked statically; list it under dynamic");
continue;
}
if(std::ranges::find(permissive_licenses,license)==permissive_licenses.end())diagnostics.report(ZODIAC_LOG_WARNING,"warning: module "+quoted_name+" is linked statically under license "+std::string(license)+"; check its terms");
const std::string stem="zodiac_mod_"+std::string(name);
std::error_code ec;
fs::path archive=kit.static_modules/(stem+".a");
if(fs::is_regular_file(archive,ec)){
libraries.push_back({std::string(name),std::move(archive)});
continue;
}
const fs::path plugin_dir=project.root/"plugins"/"static";
archive=plugin_dir/(stem+".a");
if(!fs::is_regular_file(archive,ec)){
result=fail(diagnostics,"error: module "+quoted_name+" has no static library in the kit or in plugins/static; list it under dynamic");
continue;
}
if(!toolchain)toolchain=read_text(kit.release_lib/"toolchain.txt");
if(!*toolchain){
result=fail(diagnostics,"error: cannot read the kit's toolchain identity "+quoted_path(kit.release_lib/"toolchain.txt"));
continue;
}
const std::optional<std::string> stamp=read_text(plugin_dir/(stem+".stamp"));
if(!stamp||without_trailing_space(*stamp)!=without_trailing_space(**toolchain)){
result=fail(diagnostics,"error: plugin "+quoted_name+" was not built with this kit's toolchain; list it under dynamic");
continue;
}
libraries.push_back({std::string(name),std::move(archive)});
}
return result;
}

GamePayload payload_of(const Project& project,const ScriptRuntime& runtime,std::string bytecode)
{
GamePayload payload;
payload.kit_version=ZODIAC_VERSION;
payload.game_name=project.name;
// Exactly the names ScriptRuntime::load_modules passes to the registry, in that order.
payload.requested=project.static_modules;
for(const std::string& name:project.dynamic_modules)if(!contains(payload.requested,name))payload.requested.push_back(name);
for(const zodiac_module_info* info:runtime.modules()){
const std::string_view name=or_empty(info->name);
payload.modules.push_back({std::string(name),std::string(or_empty(info->version)),contains(project.static_modules,name)?LinkMode::static_link:LinkMode::dynamic_link});
}
payload.core_exclude=project.core_exclude;
payload.diagnostics=project.diagnostics;
if(!payload.diagnostics.file.empty())payload.diagnostics.file=payload.diagnostics.file.lexically_relative(project.root);
const std::u8string section=project.main_script.lexically_relative(project.root).generic_u8string();
payload.scripts.push_back({std::string(reinterpret_cast<const char*>(section.data()),section.size()),true,std::move(bytecode)});
return payload;
}

// The first folder in the runtime's search order (module_dirs, then <root>/plugins) holding the module's shared library.
std::optional<fs::path> find_module_library(std::string_view name,const std::vector<fs::path>& dirs,const fs::path& root)
{
const std::string file=module_file_name(name);
std::error_code ec;
for(const fs::path& dir:dirs)if(fs::is_regular_file(dir/file,ec))return dir/file;
const fs::path plugin=root/"plugins"/file;
if(fs::is_regular_file(plugin,ec))return plugin;
return std::nullopt;
}

bool copy_into(const fs::path& from,const fs::path& to,Diagnostics& diagnostics)
{
std::error_code ec;
fs::create_directories(to.parent_path(),ec);
if(!ec)fs::copy_file(from,to,fs::copy_options::overwrite_existing,ec);
if(!ec)return true;
fail(diagnostics,"error: cannot copy "+quoted_path(from)+" to "+quoted_path(to)+": "+error_text(ec));
return false;
}

// True when the failed replace of exe means another process holds the old file open (a sharing or lock violation).
// MoveFileExW reports a target opened without sharing as ERROR_ACCESS_DENIED, like a read-only file or a folder, so access denied counts only
// when exe is a writable regular file that cannot even be opened for update.
bool is_in_use(const fs::path& exe,const std::error_code& ec)
{
constexpr int access_denied=5;
constexpr int sharing_violation=32;
constexpr int lock_violation=33;
bool denied=false;
if(ec.category()==std::system_category()){
const int value=ec.value();
if(value==sharing_violation||value==lock_violation)return true;
denied=value==access_denied;
}
else{
if(ec==std::errc::text_file_busy||ec==std::errc::device_or_resource_busy||ec==std::errc::no_lock_available)return true;
denied=ec==std::errc::permission_denied;
}
if(!denied)return false;
std::error_code status_error;
const fs::file_status status=fs::status(exe,status_error);
if(status_error||status.type()!=fs::file_type::regular||(status.permissions()&fs::perms::owner_write)==fs::perms::none)return false;
// Opening for update ("r+b") changes nothing; it fails only when another process denies sharing.
const std::fstream probe(exe,std::ios::in|std::ios::out|std::ios::binary);
return !probe.is_open();
}

// The sign command's program: absolute as given, relative to the project root when it names a folder, otherwise searched on PATH (as given, then with .exe and .com).
std::expected<fs::path,std::string> find_program(const std::string& program,const fs::path& root)
{
const fs::path path=path_from_utf8(program);
if(path.empty())return std::unexpected(std::string("the sign command names no program"));
if(path.is_absolute())return path;
if(path.has_parent_path()||path.has_root_path())return (root/path).lexically_normal();
const wchar_t* variable=_wgetenv(L"PATH");
std::wstring_view list=variable?std::wstring_view(variable):std::wstring_view();
std::error_code ec;
while(!list.empty()){
const std::size_t end=list.find(L';');
std::wstring_view entry=list.substr(0,end);
list=end==std::wstring_view::npos?std::wstring_view():list.substr(end+1);
if(entry.size()>=2&&entry.front()==L'"'&&entry.back()==L'"')entry=entry.substr(1,entry.size()-2);
if(entry.empty())continue;
const fs::path dir(entry);
if(fs::is_regular_file(dir/path,ec))return dir/path;
if(path.has_extension())continue;
for(const wchar_t* extension:{L".exe",L".com"}){
fs::path candidate=dir/path;
candidate+=extension;
if(fs::is_regular_file(candidate,ec))return candidate;
}
}
return std::unexpected("cannot find "+in_quotes(program)+" on PATH");
}

void report_indented(Diagnostics& diagnostics,std::string_view text)
{
while(!text.empty()){
const std::size_t end=text.find('\n');
const std::string_view line=text.substr(0,end);
if(!line.empty())diagnostics.report(ZODIAC_LOG_ERROR,"  "+std::string(line));
if(end==std::string_view::npos)break;
text.remove_prefix(end+1);
}
}

int sign(const std::vector<std::string>& command,const fs::path& file,const fs::path& root,Diagnostics& diagnostics)
{
const std::string shown=path_to_utf8(file);
std::vector<std::string> argv;
argv.reserve(command.size());
for(const std::string& part:command){
std::string argument=part;
for(std::size_t at=argument.find("{file}");at!=std::string::npos;at=argument.find("{file}",at+shown.size()))argument.replace(at,6,shown);
argv.push_back(std::move(argument));
}
const auto program=find_program(argv.front(),root);
std::expected<platform::ProcessResult,std::string> ran=std::unexpected(std::string());
if(program){
argv.front()=path_to_utf8(*program);
ran=platform::run_process(argv);
}
if(ran&&ran->exit_code==0)return exit_ok;
fail(diagnostics,"error: signing "+in_quotes(shown)+" failed");
report_indented(diagnostics,!program?program.error():!ran?ran.error():ran->output);
return exit_build_error;
}

// Everything after the compile: the payload object, the link, the rename, the copies and the signing. Returns 0 or 2.
int write_release(const Project& project,const ScriptRuntime& runtime,const KitLayout& kit,const std::vector<StaticLibrary>& libraries,const std::vector<fs::path>& module_dirs,const fs::path& output,const fs::path& exe,Diagnostics& diagnostics)
{
auto bytecode=runtime.save_bytecode(project.build.strip_debug_info);
if(!bytecode)return fail(diagnostics,bytecode.error());
const GamePayload payload=payload_of(project,runtime,std::move(*bytecode));
std::error_code ec;
fs::create_directories(output,ec);
if(ec)return fail(diagnostics,"error: cannot create directory "+quoted_path(output)+": "+error_text(ec));
const TemporaryFile object(output/path_from_utf8(project.name+".zodpay.obj"),diagnostics);
if(!write_binary(object.path(),write_payload_object(serialize_payload(payload))))return fail(diagnostics,"error: cannot write "+quoted_path(object.path()));
fs::path tmp_path=exe;
tmp_path+=".tmp";
const TemporaryFile tmp(std::move(tmp_path),diagnostics);
LinkRequest request;
request.output=tmp.path();
request.inputs.reserve(libraries.size()+4);
request.inputs.push_back(object.path());
request.inputs.push_back(kit.release_lib/"libzodiac_release_stub.a");
for(const StaticLibrary& library:libraries){
request.inputs.push_back(library.archive);
request.forced_symbols.push_back("zodiac_static_module_"+library.name);
}
request.inputs.push_back(kit.release_lib/"libzodiac_core_release.a");
request.inputs.push_back(kit.release_lib/"libangelscript.a");
// link_release reports its own failures.
if(!link_release(kit,request,diagnostics))return exit_build_error;
fs::rename(tmp.path(),exe,ec);
if(ec){
if(is_in_use(exe,ec))return fail(diagnostics,"error: cannot replace "+quoted_path(exe)+": it is in use (is the game still running?)");
return fail(diagnostics,"error: cannot replace "+quoted_path(exe)+": "+error_text(ec));
}
std::vector<fs::path> signed_files{exe};
for(const PayloadModule& module:payload.modules){
if(module.link!=LinkMode::dynamic_link)continue;
const std::optional<fs::path> library=find_module_library(module.name,module_dirs,project.root);
if(!library)return fail(diagnostics,"error: cannot find the library of module "+in_quotes(module.name)+" to copy it");
fs::path target=output/library->filename();
if(!copy_into(*library,target,diagnostics))return exit_build_error;
signed_files.push_back(std::move(target));
}
auto assets=expand_assets(project);
if(!assets)return fail(diagnostics,assets.error());
for(const fs::path& asset:*assets){
// An output folder inside the project never feeds a later build's assets.
if(is_inside(asset,output))continue;
if(!copy_into(asset,output/asset.lexically_relative(project.root),diagnostics))return exit_build_error;
}
if(!project.build.sign_command.empty()){
for(const fs::path& file:signed_files)if(const int code=sign(project.build.sign_command,file,project.root,diagnostics))return code;
}
return exit_ok;
}

int build_release(const BuildOptions& options,Diagnostics& diagnostics,BuildOutcome& outcome)
{
const fs::path target=options.target.empty()?fs::path("."):options.target;
const std::optional<fs::path> root=project_root_of(target);
if(!root)return fail(diagnostics,"error: zodiac.toml not found; create a project with zodiac new");
auto project=load_project(*root);
if(!project)return fail(diagnostics,project.error());
outcome.game_name=project->name;
if(!usable_file_name(project->name))return fail(diagnostics,"error: the game name "+in_quotes(project->name)+" cannot be used as a file name; change [game] name in zodiac.toml");
const fs::path kit_exe=platform::executable_path();
if(kit_exe.empty())return fail(diagnostics,"error: cannot find the folder of the zodiac executable");
const KitLayout kit=kit_layout(kit_exe);
// Every path is absolute before the project root becomes the working directory.
const fs::path output=options.output.empty()?project->root/"release":absolute_normal(options.output);
outcome.exe=output/path_from_utf8(project->name+".exe");
const fs::path& exe=outcome.exe;
ScriptRuntime runtime(diagnostics,ModuleSearchPaths{options.module_dirs});
// As in zodiac run: global initializers run during the compile, so they see the same working directory as in run and in the release.
if(const int code=enter_root(project->root,diagnostics))return code;
std::vector<StaticLibrary> libraries;
const ScriptRuntime::ModuleCheck check=[&project,&kit,&libraries,&diagnostics](std::span<const zodiac_module_info* const> loaded){return check_static_modules(*project,kit,loaded,libraries,diagnostics);};
if(const int code=runtime.build(*project,check))return code;
return write_release(*project,runtime,kit,libraries,options.module_dirs,output,exe,diagnostics);
}
#else
int build_release(const BuildOptions&,Diagnostics& diagnostics,BuildOutcome&)
{
// Release builds are Windows-only in this plan: the kit's link line, CRT subset and linker come from llvm-mingw.
diagnostics.report(ZODIAC_LOG_ERROR,"error: release builds are not supported on this host yet");
return exit_build_error;
}
#endif

// The last element of the target's absolute path ("game" for ".", "main.zs" for a script), for a window title when no project loaded.
std::string target_name(const std::filesystem::path& target)
{
std::error_code ec;
std::filesystem::path path=std::filesystem::absolute(target.empty()?std::filesystem::path("."):target,ec);
if(ec)path=target;
path=path.lexically_normal();
if(!path.has_filename()&&path.has_relative_path())path=path.parent_path();
return path_to_utf8(path.filename());
}
}

int build_project(const BuildOptions& options,Diagnostics& diagnostics)
{
auto owned=std::make_unique<BuildReport>();
const BuildReport& report=*owned;
diagnostics.add_sink(std::move(owned));
BuildOutcome outcome;
const int code=build_release(options,diagnostics,outcome);
const bool succeeded=code==0;
// Spec 8.2a: the console gets the result in every case, so nothing is lost when the window is closed (or there is no window).
platform::write_stdout_line(report.summary(succeeded,outcome.exe));
if(options.no_window||!platform::has_interactive_desktop())return code;
// A project that did not load leaves the cwd as it was, so the target still resolves as given.
const std::string name=outcome.game_name.empty()?target_name(options.target):outcome.game_name;
const std::filesystem::path folder=outcome.exe.parent_path();
platform::show_result_window("Zodiac build - "+name,report.text(succeeded,outcome.exe),succeeded?&folder:nullptr);
return code;
}
}
