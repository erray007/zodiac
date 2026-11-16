// toml++ is only included here, with exceptions disabled: every failure comes back as a toml::parse_result or a std::expected error.
#ifndef TOML_EXCEPTIONS
#define TOML_EXCEPTIONS 0
#endif
#include <core/project_file.hpp>
#include <core/path_utf8.hpp>
#include <core/text.hpp>
#include <toml++/toml.hpp>
#include <algorithm>
#include <fstream>
#include <ios>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace zodiac {
namespace {
namespace fs=std::filesystem;
using Failure=std::unexpected<std::string>;
Failure fail(std::string text)
{
return Failure(std::move(text));
}
Failure fail_line(std::string_view text)
{
std::string message;
message.reserve(text.size()+20);
message.append("zodiac.toml: error: ").append(text);
return Failure(std::move(message));
}
fs::path from_utf8(const std::string& text)
{
return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
bool valid_module_name(std::string_view name) noexcept
{
return !name.empty()&&std::ranges::all_of(name,[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
}
void add_unique(std::vector<std::string>& list,std::string value)
{
if(std::ranges::find(list,value)==list.end())list.push_back(std::move(value));
}
// Resolves a project-relative path from the file. Returns nullopt when it is absolute, rooted, empty or leaves root.
std::optional<fs::path> inside_root(const fs::path& root,const std::string& text)
{
const fs::path relative=from_utf8(text);
if(relative.empty()||relative.has_root_path())return std::nullopt;
fs::path full=(root/relative).lexically_normal();
const fs::path inner=full.lexically_relative(root);
if(inner.empty()||inner==fs::path(".")||*inner.begin()==fs::path(".."))return std::nullopt;
return full;
}
// Returns the table under key, an empty pointer when absent, or an error when it is something else.
std::expected<const toml::table*,std::string> section(const toml::table& root,std::string_view key)
{
const toml::node* node=root.get(key);
if(!node)return nullptr;
if(const toml::table* table=node->as_table())return table;
return fail("zodiac.toml: error: ["+std::string(key)+"] must be a table");
}
// Reads an optional array of strings into out. section_name and key are only used in the error text.
std::expected<void,std::string> string_array(const toml::table* table,std::string_view section_name,std::string_view key,std::vector<std::string>& out)
{
if(!table)return {};
const toml::node* node=table->get(key);
if(!node)return {};
const toml::array* array=node->as_array();
if(array)
{
bool all_strings=true;
for(const toml::node& element:*array)
{
const toml::value<std::string>* value=element.as_string();
if(!value)
{
all_strings=false;
break;
}
out.push_back(value->get());
}
if(all_strings)return {};
}
out.clear();
return fail("zodiac.toml: error: ["+std::string(section_name)+"] "+std::string(key)+" must be an array of strings");
}
std::expected<void,std::string> output_mode(const toml::table* table,std::string_view key,OutputMode& out)
{
if(!table)return {};
const toml::node* node=table->get(key);
if(!node)return {};
if(const toml::value<std::string>* value=node->as_string())
{
const std::string& text=value->get();
if(text=="auto")
{
out=OutputMode::automatic;
return {};
}
if(text=="always")
{
out=OutputMode::always;
return {};
}
if(text=="never")
{
out=OutputMode::never;
return {};
}
}
return fail("zodiac.toml: error: [diagnostics] "+std::string(key)+" must be \"auto\", \"always\" or \"never\"");
}
std::expected<std::string,std::string> read_file(const fs::path& path,const fs::path& dir)
{
std::error_code ec;
const bool exists=fs::is_regular_file(path,ec);
if(ec||!exists)return fail("zodiac.toml: error: no zodiac.toml in \""+one_line(path_to_utf8(dir))+"\"");
const std::uintmax_t size=fs::file_size(path,ec);
std::ifstream in(path,std::ios::binary);
if(ec||!in)return fail("zodiac.toml: error: cannot read \""+one_line(path_to_utf8(path))+"\"");
std::string content(static_cast<std::size_t>(size),'\0');
in.read(content.data(),static_cast<std::streamsize>(size));
if(in.gcount()!=static_cast<std::streamsize>(size))return fail("zodiac.toml: error: cannot read \""+one_line(path_to_utf8(path))+"\"");
return content;
}
}

std::expected<Project,std::string> load_project(const fs::path& dir)
{
std::error_code ec;
Project project;
project.root=fs::absolute(dir,ec).lexically_normal();
if(ec)return fail("zodiac.toml: error: invalid project directory \""+one_line(path_to_utf8(dir))+"\"");
if(!project.root.has_filename()&&project.root.has_relative_path())project.root=project.root.parent_path();
auto content=read_file(project.root/"zodiac.toml",project.root);
if(!content)return fail(std::move(content.error()));
toml::parse_result parsed=toml::parse(*content,std::string_view("zodiac.toml"));
if(!parsed)
{
const toml::parse_error& error=parsed.error();
const toml::source_position position=error.source().begin;
return fail("zodiac.toml:"+std::to_string(position.line)+":"+std::to_string(position.column)+": error: "+std::string(error.description()));
}
const toml::table& root=parsed.table();
const auto game=section(root,"game");
if(!game)return fail(std::move(game.error()));
const toml::node* main_node=*game?(*game)->get("main"):nullptr;
if(!main_node)return fail_line("[game] main is required");
const toml::value<std::string>* main_value=main_node->as_string();
if(!main_value)return fail_line("[game] main must be a string");
project.name=path_to_utf8(project.root.filename());
if(*game)
{
if(const toml::node* name_node=(*game)->get("name"))
{
const toml::value<std::string>* name_value=name_node->as_string();
if(!name_value)return fail_line("[game] name must be a string");
project.name=name_value->get();
}
}
const auto modules=section(root,"modules");
if(!modules)return fail(std::move(modules.error()));
if(auto result=string_array(*modules,"modules","static",project.static_modules);!result)return fail(std::move(result.error()));
if(auto result=string_array(*modules,"modules","dynamic",project.dynamic_modules);!result)return fail(std::move(result.error()));
for(std::vector<std::string>* list:{&project.static_modules,&project.dynamic_modules})
{
std::vector<std::string> cleaned;
cleaned.reserve(list->size());
for(std::string& name:*list)
{
if(name=="core")continue;
if(!valid_module_name(name))return fail("zodiac.toml: error: module name \""+one_line(name)+"\" is invalid");
add_unique(cleaned,std::move(name));
}
*list=std::move(cleaned);
}
for(const std::string& name:project.static_modules)
{
if(std::ranges::find(project.dynamic_modules,name)!=project.dynamic_modules.end())return fail("zodiac.toml: error: module \""+one_line(name)+"\" is listed as both static and dynamic");
}
const auto core=section(root,"core");
if(!core)return fail(std::move(core.error()));
std::vector<std::string> excluded;
if(auto result=string_array(*core,"core","exclude",excluded);!result)return fail(std::move(result.error()));
for(std::string& name:excluded)
{
if(name!="window"&&name!="keyboard")return fail("zodiac.toml: error: unknown [core] exclude entry \""+one_line(name)+"\"; valid: window, keyboard");
add_unique(project.core_exclude,std::move(name));
}
const auto diagnostics=section(root,"diagnostics");
if(!diagnostics)return fail(std::move(diagnostics.error()));
if(auto result=output_mode(*diagnostics,"console",project.diagnostics.console);!result)return fail(std::move(result.error()));
if(auto result=output_mode(*diagnostics,"dialog",project.diagnostics.dialog);!result)return fail(std::move(result.error()));
if(*diagnostics)
{
if(const toml::node* file_node=(*diagnostics)->get("file"))
{
const toml::value<std::string>* file_value=file_node->as_string();
if(!file_value)return fail_line("[diagnostics] file must be a string");
auto file=inside_root(project.root,file_value->get());
if(!file)return fail("zodiac.toml: error: [diagnostics] file \""+one_line(file_value->get())+"\" must be inside the project");
project.diagnostics.file=std::move(*file);
}
}
const auto build=section(root,"build");
if(!build)return fail(std::move(build.error()));
if(auto result=string_array(*build,"build","assets",project.build.assets);!result)return fail(std::move(result.error()));
if(*build)
{
if(const toml::node* strip_node=(*build)->get("strip_debug_info"))
{
const toml::value<bool>* strip_value=strip_node->as_boolean();
if(!strip_value)return fail_line("[build] strip_debug_info must be a boolean");
project.build.strip_debug_info=strip_value->get();
}
const toml::node* sign_node=(*build)->get("sign");
if(sign_node)
{
const toml::table* sign=sign_node->as_table();
if(!sign)return fail_line("[build.sign] must be a table");
if(auto result=string_array(sign,"build.sign","command",project.build.sign_command);!result)return fail(std::move(result.error()));
const bool has_placeholder=std::ranges::any_of(project.build.sign_command,[](const std::string& argument){return argument.find("{file}")!=std::string::npos;});
if(!project.build.sign_command.empty()&&!has_placeholder)return fail_line("[build.sign] command must contain {file}");
}
}
const auto workers=section(root,"workers");
if(!workers)return fail(std::move(workers.error()));
std::vector<std::string> worker_names;
if(auto result=string_array(*workers,"workers","scripts",worker_names);!result)return fail(std::move(result.error()));
for(const std::string& worker_name:worker_names)
{
auto worker=inside_root(project.root,worker_name);
if(!worker)return fail("zodiac.toml: error: worker script \""+one_line(worker_name)+"\" must be inside the project");
const bool worker_is_file=fs::is_regular_file(*worker,ec);
if(ec||!worker_is_file)return fail("zodiac.toml: error: worker script \""+one_line(worker_name)+"\" does not exist");
if(worker->extension()!=fs::path(".zs"))return fail("zodiac.toml: error: worker script \""+one_line(worker_name)+"\" must be a .zs file");
if(std::ranges::find(project.workers,*worker)==project.workers.end())project.workers.push_back(std::move(*worker));
}
auto main_script=inside_root(project.root,main_value->get());
if(!main_script)return fail("zodiac.toml: error: main script \""+one_line(main_value->get())+"\" must be inside the project");
const bool is_file=fs::is_regular_file(*main_script,ec);
if(ec||!is_file)return fail("zodiac.toml: error: main script \""+one_line(main_value->get())+"\" does not exist");
project.main_script=std::move(*main_script);
return project;
}

std::expected<std::vector<fs::path>,std::string> expand_assets(const Project& project)
{
std::vector<fs::path> files;
if(project.build.assets.empty())return files;
const fs::path release_dir=project.root/"release";
const fs::path manifest=project.root/"zodiac.toml";
std::error_code ec;
fs::recursive_directory_iterator it(project.root,fs::directory_options::none,ec);
if(ec)return fail("zodiac.toml: error: cannot read directory \""+one_line(path_to_utf8(project.root))+"\": "+one_line(error_text(ec)));
for(const fs::recursive_directory_iterator end;it!=end;it.increment(ec))
{
const fs::path& path=it->path();
if(path==release_dir)
{
it.disable_recursion_pending();
continue;
}
if(path==manifest||!it->is_regular_file(ec))
{
ec.clear();
continue;
}
const std::u8string relative=path.lexically_relative(project.root).generic_u8string();
const std::string_view text(reinterpret_cast<const char*>(relative.data()),relative.size());
if(std::ranges::any_of(project.build.assets,[text](const std::string& pattern){return glob_match(pattern,text);}))files.push_back(path);
}
// A failed increment turns the iterator into end and leaves the reason in ec.
if(ec)return fail("zodiac.toml: error: cannot read directory \""+one_line(path_to_utf8(project.root))+"\": "+one_line(error_text(ec)));
std::ranges::sort(files);
files.erase(std::ranges::unique(files).begin(),files.end());
return files;
}
}
