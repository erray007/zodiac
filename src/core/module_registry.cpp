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

ModuleSearchPaths ModuleSearchPaths::defaults(const std::filesystem::path& exe_dir,const std::filesystem::path& project_dir)
{
return ModuleSearchPaths{{(exe_dir/".."/"modules").lexically_normal(),project_dir/"plugins"}};
}

std::expected<void,std::string> ModuleRegistry::resolve(std::string_view name,const ModuleSearchPaths& search,std::vector<std::string>& stack)
{
if(!valid_module_name(name))return failure("module name "+in_quotes(name)+" is invalid");
const auto loaded=std::find_if(entries_.begin(),entries_.end(),[&](const Entry& entry){return entry.name==name;});
if(loaded!=entries_.end())return {};
const auto cycle=std::find(stack.begin(),stack.end(),name);
if(cycle!=stack.end()){
std::string message="module dependency cycle: ";
for(auto it=cycle;it!=stack.end();++it)message.append(*it).append(" -> ");
message.append(name);
return failure(std::move(message));
}
std::string file="zodiac_mod_";
file.append(name).append(ZODIAC_MODULE_SUFFIX);
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
auto library=DynamicLibrary::open(found);
if(!library)return failure("module "+in_quotes(name)+" could not be loaded: "+library.error());
using EntryFunction=const zodiac_module_info*(*)(void);
void* symbol=library->symbol(ZODIAC_MODULE_ENTRY_NAME);
if(!symbol)return failure("module "+in_quotes(name)+" has no "+ZODIAC_MODULE_ENTRY_NAME+" entry");
const zodiac_module_info* info=reinterpret_cast<EntryFunction>(symbol)();
if(!info)return failure("module "+in_quotes(name)+" returned no module info");
if(info->abi_version>ZODIAC_ABI_VERSION)return failure("module "+in_quotes(name)+" needs ABI "+std::to_string(info->abi_version)+" but this Zodiac provides "+std::to_string(ZODIAC_ABI_VERSION));
const std::string_view reported=info->name?std::string_view(info->name):std::string_view();
if(reported!=name)return failure("module "+in_quotes(name)+" reports name "+in_quotes(reported));
stack.emplace_back(name);
for(uint32_t i=0;i<info->dependency_count;++i){
const char* dependency=info->dependencies?info->dependencies[i]:nullptr;
if(!dependency)return failure("module "+in_quotes(name)+" lists an empty dependency");
if(auto resolved=resolve(dependency,search,stack);!resolved)return resolved;
}
stack.pop_back();
entries_.push_back(Entry{std::string(name),std::move(*library),info,false});
infos_.push_back(info);
return {};
}

std::expected<void,std::string> ModuleRegistry::load(std::span<const std::string> names,const ModuleSearchPaths& search)
{
const size_t before=entries_.size();
std::vector<std::string> stack;
for(const std::string& name:names){
auto resolved=resolve(name,search,stack);
if(resolved)continue;
while(entries_.size()>before)entries_.pop_back();
infos_.resize(before);
return resolved;
}
return {};
}

std::expected<void,std::string> ModuleRegistry::register_all(Host& host)
{
HostScope scope(host);
std::string first_error;
host.set_log_handler([&scope,&first_error](zodiac_log_level level,std::string_view message){
if(level==ZODIAC_LOG_ERROR&&first_error.empty())first_error.assign(message);
if(scope.handler)scope.handler(level,message);
});
for(Entry& entry:entries_){
if(entry.registered)continue;
first_error.clear();
host.set_current_module(entry.name);
const int code=entry.info->register_module?entry.info->register_module(host.abi()):ZODIAC_OK;
if(code!=ZODIAC_OK||!first_error.empty()){
return failure("module "+in_quotes(entry.name)+" failed to register: "+(first_error.empty()?"error code "+std::to_string(code):first_error));
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
while(!entries_.empty())entries_.pop_back();
infos_.clear();
}
}
