#include <core/vfs/vfs.hpp>
#include <core/text.hpp>
#include <core/vfs/folder_source.hpp>
#include <core/vfs/host_source.hpp>
#include <algorithm>

namespace zodiac::vfs {
namespace {
bool fall_through(FileError error) noexcept
{
return error==FileError::not_found||error==FileError::not_a_directory;
}
std::size_t segment_count(std::string_view relative) noexcept
{
return relative.empty()?0:static_cast<std::size_t>(std::ranges::count(relative,'/'))+1;
}
// The end of the first count segments of relative (count at least 1), or npos when it has fewer.
std::size_t prefix_end(std::string_view relative,std::size_t count) noexcept
{
std::size_t at=0;
for(;;){
const std::size_t cut=relative.find('/',at);
if(--count==0)return cut==std::string_view::npos?relative.size():cut;
if(cut==std::string_view::npos)return std::string_view::npos;
at=cut+1;
}
}
std::string quote_path(std::string_view path)
{
std::string text;
text.reserve(path.size()+2);
text.push_back('"');
text.append(one_line(path)).push_back('"');
return text;
}
std::unexpected<Failure> failed(FileError code)
{
return std::unexpected(Failure{code,{}});
}
}

std::string_view reason_text(FileError error) noexcept
{
switch(error){
case FileError::not_found:
return "file not found";
case FileError::access_denied:
return "access denied";
case FileError::damaged:
return "damaged data";
case FileError::is_directory:
return "is a directory";
case FileError::not_a_directory:
return "not a directory";
case FileError::read_only_pack:
return "in a read-only pack";
case FileError::bad_path:
return "invalid path";
case FileError::ambiguous:
return "ambiguous name";
default:
return "input/output error";
}
}

std::string read_failure_text(std::string_view path,const Failure& failure)
{
if(!failure.message.empty())return failure.message;
const std::string_view reason=reason_text(failure.code);
std::string text;
text.reserve(path.size()+reason.size()+16);
text.append("cannot read ").append(quote_path(path)).append(": ").append(reason);
return text;
}

bool Source::real_spelling(std::string_view,std::string&)
{
return false;
}

std::optional<std::pair<std::string,std::string>> Source::ambiguous_names(std::string_view)
{
return std::nullopt;
}

Vfs::Vfs():table_(std::make_shared<const Table>()),host_(std::make_shared<HostSource>())
{
}

Vfs::~Vfs()=default;

std::shared_ptr<const Vfs::Table> Vfs::snapshot() const
{
const std::shared_lock lock(table_mutex_);
return table_;
}

template<class Visit>
bool Vfs::visit_covering(const Table& table,const VfsPath& p,Visit&& visit)
{
for(auto it=table.mounts.rbegin();it!=table.mounts.rend();++it){
const Mount& mount=*it;
if(mount.mount!=p.mount)continue;
std::string_view sub=p.relative;
if(mount.at_segments){
const std::size_t end=prefix_end(p.relative,mount.at_segments);
if(end==std::string_view::npos)continue;
const std::string_view prefix=std::string_view(p.relative).substr(0,end);
if(prefix!=mount.at&&fold_name(prefix)!=mount.at_fold)continue;
sub=end==p.relative.size()?std::string_view():std::string_view(p.relative).substr(end+1);
}
if(visit(Target{&mount,sub}))return true;
}
return false;
}

bool Vfs::leads_to_mount(const Table& table,const VfsPath& p)
{
const std::size_t count=segment_count(p.relative);
const std::string fold=count?fold_name(p.relative):std::string();
return std::ranges::any_of(table.mounts,[&](const Mount& mount){
if(mount.mount!=p.mount||mount.at_segments<=count)return false;
if(!count)return true;
const std::string_view prefix=std::string_view(mount.at_fold).substr(0,prefix_end(mount.at_fold,count));
return prefix==fold;
});
}

bool Vfs::mount(std::string_view at,std::shared_ptr<Source> source,bool writable,const void* owner)
{
if(!source)return false;
auto parsed=parse_path(at,false);
if(!parsed)return false;
Mount entry;
entry.mount=std::move(parsed->mount);
entry.at=std::move(parsed->relative);
entry.at_fold=fold_name(entry.at);
entry.at_segments=segment_count(entry.at);
entry.source=std::move(source);
entry.writable=writable;
entry.owner=owner;
const std::lock_guard writer(writer_mutex_);
const std::shared_ptr<const Table> current=snapshot();
if(owner){
const bool taken=std::ranges::any_of(current->mounts,[&entry](const Mount& mount){return mount.owner==entry.owner&&mount.mount==entry.mount&&mount.at_fold==entry.at_fold;});
if(taken)return false;
}
auto next=std::make_shared<Table>();
next->mounts.reserve(current->mounts.size()+1);
next->mounts=current->mounts;
next->mounts.push_back(std::move(entry));
std::shared_ptr<const Table> replaced=std::move(next);
{
const std::unique_lock lock(table_mutex_);
table_.swap(replaced);
}
// The old table (now in replaced) is released here, outside the lock.
return true;
}

std::size_t Vfs::unmount(const void* owner)
{
if(!owner)return 0;
const std::lock_guard writer(writer_mutex_);
const std::shared_ptr<const Table> current=snapshot();
const std::size_t removed=static_cast<std::size_t>(std::ranges::count_if(current->mounts,[owner](const Mount& mount){return mount.owner==owner;}));
if(!removed)return 0;
auto next=std::make_shared<Table>();
next->mounts.reserve(current->mounts.size()-removed);
for(const Mount& mount:current->mounts)if(mount.owner!=owner)next->mounts.push_back(mount);
std::shared_ptr<const Table> replaced=std::move(next);
{
const std::unique_lock lock(table_mutex_);
table_.swap(replaced);
}
return removed;
}

bool Vfs::mount_folder(std::string_view at,const std::filesystem::path& host,bool writable,const void* owner)
{
return mount(at,std::make_shared<FolderSource>(host,!writable),writable,owner);
}

std::expected<VfsPath,Failure> Vfs::parse_failure(std::string_view path) const
{
auto parsed=parse_path(path,host_paths());
if(!parsed)return std::unexpected(Failure{FileError::bad_path,std::move(parsed.error())});
return std::move(*parsed);
}

Failure Vfs::ambiguous_failure(std::string_view path,const Target& target) const
{
Failure failure{FileError::ambiguous,{}};
const auto names=target.mount->source->ambiguous_names(target.sub);
if(!names)return failure;
std::string prefix=target.mount->mount=="data:"?"data:/":"";
if(!target.mount->at.empty())prefix.append(target.mount->at).push_back('/');
failure.message=quote_path(path);
failure.message.append(" matches both ").append(quote_path(prefix+names->first)).append(" and ").append(quote_path(prefix+names->second)).append("; rename one of them");
return failure;
}

void Vfs::warn(std::string_view line) const
{
if(log_)log_(ZODIAC_LOG_WARNING,line);
}

void Vfs::check_spelling(std::string_view path,const VfsPath& p,const Target& target)
{
if(!dev_mode()||target.sub.empty())return;
std::string key;
key.reserve(p.mount.size()+p.relative.size());
key.append(p.mount).append(p.relative);
{
const std::lock_guard lock(warned_mutex_);
if(!spelled_.insert(std::move(key)).second)return;
}
std::string real;
if(!target.mount->source->real_spelling(target.sub,real))return;
std::string shown=target.mount->mount=="data:"?"data:/":"";
if(!target.mount->at.empty())shown.append(target.mount->at).push_back('/');
shown.append(real);
std::string line="warning: ";
line.append(quote_path(path)).append(" was found as ").append(quote_path(shown)).append("; use the real name, other systems may not find it");
warn(line);
}

void Vfs::check_release_pack(std::string_view path,const VfsPath& p)
{
if(!dev_mode()||!release_pack_check_||!p.mount.empty()||!release_pack_check_(p.relative))return;
{
const std::lock_guard lock(warned_mutex_);
if(!pack_warned_.insert(p.relative).second)return;
}
std::string line="warning: ";
line.append(quote_path(path)).append(" is written in a folder here, but the release puts it in a read-only pack, where writing fails; save files in data:/ instead");
warn(line);
}

std::expected<EntryInfo,Failure> Vfs::stat(std::string_view path)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->mount=="host:"){
auto info=host_->stat(p->relative);
if(!info)return failed(info.error());
if(p->folder&&info->kind!=EntryKind::directory)return failed(FileError::not_a_directory);
return std::move(*info);
}
const std::shared_ptr<const Table> table=snapshot();
std::expected<EntryInfo,Failure> result=failed(FileError::not_found);
Target found;
const bool stopped=visit_covering(*table,*p,[&](const Target& target){
auto info=target.mount->source->stat(target.sub);
if(info){
result=std::move(*info);
found=target;
return true;
}
if(fall_through(info.error()))return false;
result=std::unexpected(info.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{info.error(),{}});
return true;
});
if(stopped){
if(result){
if(p->folder&&result->kind!=EntryKind::directory)return failed(FileError::not_a_directory);
check_spelling(path,*p,found);
}
return result;
}
if(leads_to_mount(*table,*p)){
EntryInfo info;
info.kind=EntryKind::directory;
const std::size_t slash=p->relative.rfind('/');
info.name=slash==std::string::npos?p->relative:p->relative.substr(slash+1);
return info;
}
return result;
}

std::expected<std::shared_ptr<Reader>,Failure> Vfs::open_read(std::string_view path)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->folder)return failed(FileError::is_directory);
if(p->mount=="host:"){
auto reader=host_->open_read(p->relative);
if(!reader)return failed(reader.error());
return std::move(*reader);
}
const std::shared_ptr<const Table> table=snapshot();
std::expected<std::shared_ptr<Reader>,Failure> result=failed(FileError::not_found);
Target found;
const bool stopped=visit_covering(*table,*p,[&](const Target& target){
auto reader=target.mount->source->open_read(target.sub);
if(reader){
result=std::move(*reader);
found=target;
return true;
}
if(fall_through(reader.error()))return false;
result=std::unexpected(reader.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{reader.error(),{}});
return true;
});
if(stopped){
if(result)check_spelling(path,*p,found);
return result;
}
if(leads_to_mount(*table,*p))return failed(FileError::is_directory);
return result;
}

std::expected<std::unique_ptr<WriteTarget>,Failure> Vfs::open_write(std::string_view path,WriteMode mode)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->folder)return failed(FileError::is_directory);
if(p->mount=="host:"){
auto target=host_->open_write(p->relative,mode);
if(!target)return failed(target.error());
return std::move(*target);
}
const std::shared_ptr<const Table> table=snapshot();
std::expected<std::unique_ptr<WriteTarget>,Failure> result=failed(FileError::not_found);
bool covered=false;
Target found;
const bool stopped=visit_covering(*table,*p,[&](const Target& target){
covered=true;
if(!target.mount->writable)return false;
auto writer=target.mount->source->open_write(target.sub,mode);
if(writer){
result=std::move(*writer);
found=target;
}
else result=std::unexpected(writer.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{writer.error(),{}});
return true;
});
if(!stopped){
if(leads_to_mount(*table,*p))return failed(FileError::is_directory);
if(covered)return std::unexpected(Failure{FileError::read_only_pack,quote_path(path)+" is in a read-only pack; save files in data:/ instead"});
return result;
}
if(result){
check_release_pack(path,*p);
check_spelling(path,*p,found);
}
return result;
}

std::expected<void,Failure> Vfs::remove(std::string_view path)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->folder)return failed(FileError::is_directory);
if(p->mount=="host:"){
if(auto removed=host_->remove(p->relative);!removed)return failed(removed.error());
return {};
}
const std::shared_ptr<const Table> table=snapshot();
std::expected<void,Failure> result=failed(FileError::not_found);
const bool stopped=visit_covering(*table,*p,[&](const Target& target){
Source& source=*target.mount->source;
if(!target.mount->writable){
const auto info=source.stat(target.sub);
if(info&&info->kind!=EntryKind::deleted){
result=std::unexpected(Failure{FileError::read_only_pack,quote_path(path)+" is in a read-only pack; save files in data:/ instead"});
return true;
}
if(info||fall_through(info.error()))return false;
result=std::unexpected(info.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{info.error(),{}});
return true;
}
// The spelling is checked before the entry is gone (dev mode only).
if(dev_mode()&&source.stat(target.sub))check_spelling(path,*p,target);
auto removed=source.remove(target.sub);
if(removed){
result={};
return true;
}
if(fall_through(removed.error()))return false;
result=std::unexpected(removed.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{removed.error(),{}});
return true;
});
if(!stopped&&leads_to_mount(*table,*p))return failed(FileError::is_directory);
return result;
}

std::expected<std::vector<std::string>,Failure> Vfs::list(std::string_view path,ListKind kind,std::string_view pattern)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
std::vector<EntryInfo> entries;
bool found_any=false;
if(p->mount=="host:"){
if(auto listed=host_->list(p->relative,entries);!listed)return failed(listed.error());
found_any=true;
}
else{
const std::shared_ptr<const Table> table=snapshot();
std::optional<Failure> failure;
visit_covering(*table,*p,[&](const Target& target){
auto listed=target.mount->source->list(target.sub,entries);
if(listed){
if(!found_any)check_spelling(path,*p,target);
found_any=true;
return false;
}
if(fall_through(listed.error()))return false;
failure=listed.error()==FileError::ambiguous?ambiguous_failure(path,target):Failure{listed.error(),{}};
return true;
});
if(failure)return std::unexpected(std::move(*failure));
// The folders that lead to deeper mount points.
const std::size_t count=segment_count(p->relative);
const std::string fold=count?fold_name(p->relative):std::string();
for(const Mount& mount:table->mounts){
if(mount.mount!=p->mount||mount.at_segments<=count)continue;
if(count&&std::string_view(mount.at_fold).substr(0,prefix_end(mount.at_fold,count))!=fold)continue;
const std::size_t start=count?prefix_end(mount.at,count)+1:0;
const std::size_t end=prefix_end(mount.at,count+1);
EntryInfo& info=entries.emplace_back();
info.kind=EntryKind::directory;
info.name=mount.at.substr(start,end-start);
found_any=true;
}
}
if(!found_any)return failed(FileError::not_found);
const EntryKind wanted=kind==ListKind::files?EntryKind::file:EntryKind::directory;
const bool every=pattern=="*";
const std::string pattern_fold=every?std::string():fold_name(pattern);
std::vector<std::pair<std::string,std::string>> names;
names.reserve(entries.size());
for(EntryInfo& entry:entries){
if(entry.kind!=wanted)continue;
std::string fold=fold_name(entry.name);
if(!every&&!glob_match(pattern_fold,fold))continue;
names.emplace_back(std::move(fold),std::move(entry.name));
}
std::ranges::sort(names);
const auto [first,last]=std::ranges::unique(names,{},[](const auto& name)->const std::string&{return name.second;});
names.erase(first,last);
std::vector<std::string> result;
result.reserve(names.size());
for(auto& name:names)result.push_back(std::move(name.second));
return result;
}

std::expected<void,Failure> Vfs::create_directory(std::string_view path,bool recursive)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->mount=="host:"){
const auto made=recursive?host_->make_directories(p->relative):host_->make_directory(p->relative);
if(!made)return failed(made.error());
return {};
}
const std::shared_ptr<const Table> table=snapshot();
std::expected<void,Failure> result=failed(FileError::not_found);
bool covered=false;
const bool stopped=visit_covering(*table,*p,[&](const Target& target){
covered=true;
if(!target.mount->writable)return false;
Source& source=*target.mount->source;
const auto fail=[&](FileError error){
result=std::unexpected(error==FileError::ambiguous?ambiguous_failure(path,target):Failure{error,{}});
return true;
};
if(recursive){
for(std::size_t at=target.sub.find('/');at!=std::string_view::npos;at=target.sub.find('/',at+1)){
if(auto made=source.make_directory(target.sub.substr(0,at));!made)return fail(made.error());
}
}
if(auto made=source.make_directory(target.sub);!made)return fail(made.error());
result={};
return true;
});
if(stopped)return result;
if(leads_to_mount(*table,*p))return {};
if(covered)return std::unexpected(Failure{FileError::read_only_pack,quote_path(path)+" is in a read-only pack; save files in data:/ instead"});
return result;
}

std::expected<void,Failure> Vfs::delete_directory(std::string_view path,bool recursive)
{
const auto p=parse_failure(path);
if(!p)return std::unexpected(p.error());
if(p->mount=="host:"){
if(auto removed=host_->remove_directory(p->relative,recursive);!removed)return failed(removed.error());
return {};
}
const std::shared_ptr<const Table> table=snapshot();
if(leads_to_mount(*table,*p))return failed(FileError::access_denied);
std::expected<void,Failure> result=failed(FileError::not_found);
visit_covering(*table,*p,[&](const Target& target){
if(target.sub.empty()){
result=failed(FileError::access_denied);
return true;
}
Source& source=*target.mount->source;
const auto fail=[&](FileError error){
result=std::unexpected(error==FileError::ambiguous?ambiguous_failure(path,target):Failure{error,{}});
return true;
};
const auto info=source.stat(target.sub);
if(!info)return fall_through(info.error())?false:fail(info.error());
if(info->kind!=EntryKind::directory)return info->kind==EntryKind::deleted?false:fail(FileError::not_a_directory);
if(!target.mount->writable){
result=std::unexpected(Failure{FileError::read_only_pack,quote_path(path)+" is in a read-only pack; save files in data:/ instead"});
return true;
}
check_spelling(path,*p,target);
if(auto removed=source.remove_directory(target.sub,recursive);!removed)return fail(removed.error());
result={};
return true;
});
return result;
}
}
