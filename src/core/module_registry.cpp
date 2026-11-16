#include <core/module_registry.hpp>
#include <core/host.hpp>
#include <core/path_utf8.hpp>
#include <algorithm>
#include <system_error>
#include <utility>

#ifndef ZODIAC_MODULE_SUFFIX
#error ZODIAC_MODULE_SUFFIX must be defined by the build (the shared library extension of the target platform)
#endif

namespace zodiac {
namespace {
bool valid_module_name(std::string_view name) noexcept
{
return !name.empty()&&std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_';});
}
std::unexpected<std::string> failure(std::string message)
{
return std::unexpected<std::string>(std::move(message));
}
std::string in_quotes(std::string_view name)
{
std::string text;
text.reserve(name.size()+2);
text.push_back('"');
text.append(name);
text.push_back('"');
return text;
}
// Restores the host's log handler and current module however register_all ends.
struct HostScope{
Host& host;
Host::LogHandler handler;
std::string module;
explicit HostScope(Host& target):host(target),handler(target.log_handler()),module(target.current_module()){}
HostScope(const HostScope&)=delete;
HostScope& operator=(const HostScope&)=delete;
~HostScope()
{
host.set_log_handler(std::move(handler));
host.set_current_module(module);
}
};
}

std::string module_file_name(std::string_view name)
{
constexpr std::string_view prefix="zodiac_mod_";
constexpr std::string_view suffix=ZODIAC_MODULE_SUFFIX;
std::string file;
file.reserve(prefix.size()+name.size()+suffix.size());
file.append(prefix).append(name).append(suffix);
return file;
}

std::vector<InstalledModule> find_installed_modules(std::span<const std::filesystem::path> dirs)
{
namespace fs=std::filesystem;
constexpr std::string_view prefix="zodiac_mod_";
constexpr std::string_view suffix=ZODIAC_MODULE_SUFFIX;
const fs::path extension(suffix);
std::vector<InstalledModule> found;
for(const fs::path& dir:dirs){
std::error_code error;
fs::directory_iterator it(dir,error);
if(error)continue;
for(const fs::directory_iterator end;it!=end;it.increment(error)){
if(error)break;
const fs::path& file=it->path();
if(file.extension()!=extension)continue;
std::string name=path_to_utf8(file.filename());
if(!name.starts_with(prefix))continue;
std::error_code status;
if(!it->is_regular_file(status))continue;
name.erase(name.size()-suffix.size());
name.erase(0,prefix.size());
if(std::any_of(found.begin(),found.end(),[&name](const InstalledModule& module){return module.name==name;}))continue;
found.push_back({file,std::move(name)});
}
}
return found;
}

ModuleSearchPaths ModuleSearchPaths::defaults(const std::filesystem::path& exe_dir,const std::filesystem::path& project_dir)
{
return ModuleSearchPaths{{(exe_dir/".."/"modules").lexically_normal(),project_dir/"plugins"}};
}

std::expected<void,std::string> ModuleRegistry::resolve(std::string_view name,const ModuleSearchPaths& search,std::span<const zodiac_module_info* const> linked,std::vector<std::string>& stack)
{
if(!valid_module_name(name))return failure("module name "+in_quotes(name)+" is invalid");
if(std::find(names_.begin(),names_.end(),name)!=names_.end())return {};
const auto cycle=std::find(stack.begin(),stack.end(),name);
if(cycle!=stack.end()){
std::string message="module dependency cycle: ";
for(auto it=cycle;it!=stack.end();++it)message.append(*it).append(" -> ");
message.append(name);
return failure(std::move(message));
}
DynamicLibrary library;
const zodiac_module_info* info=nullptr;
const auto match=std::find_if(linked.begin(),linked.end(),[name](const zodiac_module_info* candidate){return candidate&&candidate->name&&name==candidate->name;});
if(match!=linked.end()){
info=*match;
}else{
const std::string file=module_file_name(name);
std::filesystem::path found;
for(const std::filesystem::path& dir:search.dirs){
std::error_code error;
std::filesystem::path candidate=dir/file;
if(std::filesystem::is_regular_file(candidate,error)){
found=std::move(candidate);
break;
}
}
if(found.empty()){
std::string message="module "+in_quotes(name)+" not found in: ";
for(size_t i=0;i<search.dirs.size();++i){
if(i)message.append("; ");
message.append(path_to_utf8(search.dirs[i]));
}
return failure(std::move(message));
}
auto opened=DynamicLibrary::open(found);
if(!opened)return failure("module "+in_quotes(name)+" could not be loaded: "+opened.error());
library=std::move(*opened);
using EntryFunction=const zodiac_module_info*(*)(void);
void* symbol=library.symbol(ZODIAC_MODULE_ENTRY_NAME);
if(!symbol)return failure("module "+in_quotes(name)+" has no "+ZODIAC_MODULE_ENTRY_NAME+" entry");
info=reinterpret_cast<EntryFunction>(symbol)();
if(!info)return failure("module "+in_quotes(name)+" returned no module info");
}
if(info->abi_version==0)return failure("module "+in_quotes(name)+" reports ABI version 0");
if(info->abi_version>ZODIAC_ABI_VERSION)return failure("module "+in_quotes(name)+" needs ABI "+std::to_string(info->abi_version)+" but this Zodiac provides "+std::to_string(ZODIAC_ABI_VERSION));
if(!module_info_provides(*info,module_info_v1_size))return failure("module "+in_quotes(name)+" has an invalid info structure");
const std::string_view reported=info->name?std::string_view(info->name):std::string_view();
if(reported!=name)return failure("module "+in_quotes(name)+" reports name "+in_quotes(reported));
stack.emplace_back(name);
for(uint32_t i=0;i<info->dependency_count;++i){
const char* dependency=info->dependencies?info->dependencies[i]:nullptr;
if(!dependency)return failure("module "+in_quotes(name)+" lists an empty dependency");
if(auto resolved=resolve(dependency,search,linked,stack);!resolved)return resolved;
}
stack.pop_back();
entries_.push_back(Entry{std::move(library),info,false});
infos_.push_back(info);
names_.emplace_back(name);
return {};
}

std::expected<void,std::string> ModuleRegistry::load(std::span<const std::string> names,const ModuleSearchPaths& search,std::span<const zodiac_module_info* const> linked)
{
const size_t before=entries_.size();
std::vector<std::string> stack;
for(const std::string& name:names){
auto resolved=resolve(name,search,linked,stack);
if(resolved)continue;
while(entries_.size()>before)entries_.pop_back();
infos_.resize(before);
names_.resize(before);
return resolved;
}
return {};
}

std::expected<void,std::string> ModuleRegistry::register_all(Host& host)
{
HostScope scope(host);
std::string first_error;
std::size_t baseline=0;
host.set_log_handler([&scope,&first_error,&baseline,&host](zodiac_log_level level,std::string_view message){
if(level==ZODIAC_LOG_ERROR&&first_error.empty()&&host.registration_failures()>baseline)first_error.assign(message);
if(scope.handler)scope.handler(level,message);
});
for(std::size_t i=0;i<entries_.size();++i){
Entry& entry=entries_[i];
if(entry.registered)continue;
const std::string& name=names_[i];
first_error.clear();
baseline=host.registration_failures();
host.set_current_module(name);
const int code=entry.info->register_module?entry.info->register_module(host.abi()):ZODIAC_OK;
if(code!=ZODIAC_OK||host.registration_failures()>baseline){
return failure("module "+in_quotes(name)+" failed to register: "+(first_error.empty()?"error code "+std::to_string(code):first_error));
}
entry.registered=true;
}
return {};
}

void ModuleRegistry::shutdown_all() noexcept
{
for(auto it=entries_.rbegin();it!=entries_.rend();++it){
if(it->registered&&it->info->shutdown_module)it->info->shutdown_module();
}
for(Entry& entry:entries_)entry.registered=false;
}
}
