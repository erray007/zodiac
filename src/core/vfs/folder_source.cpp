#include <core/vfs/folder_source.hpp>
#include <core/path_utf8.hpp>
#include <core/vfs/path.hpp>
#include <platform/clock.hpp>
#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <limits>

namespace zodiac::vfs {
namespace fs=std::filesystem;
namespace detail {
namespace {
std::int64_t reader_failure(std::int64_t negated_errno) noexcept
{
const std::error_code error(static_cast<int>(-negated_errno),std::generic_category());
return -static_cast<std::int64_t>(platform::error_kind(error)==platform::ErrorKind::access_denied?FileError::access_denied:FileError::io);
}
// A folder's file opened for reading. The size is the one at open, so eof checks cost no system call.
class FileReader final:public Reader{
public:
explicit FileReader(platform::File file) noexcept:file_(std::move(file)),size_(file_.size()){}
std::int64_t read_at(std::uint64_t offset,std::span<std::byte> buffer) override
{
const std::int64_t got=file_.read_at(offset,buffer);
return got<0?reader_failure(got):got;
}
std::uint64_t size() const override{return size_;}
private:
platform::File file_;
std::uint64_t size_;
};
// A folder's file opened for writing. Every byte goes straight to the file, so commit has nothing left to do; the size is tracked
// from the writes, since one file object owns the target.
class FileWriter final:public WriteTarget{
public:
explicit FileWriter(platform::File file) noexcept:file_(std::move(file)),size_(file_.size()){}
std::int64_t read_at(std::uint64_t offset,std::span<std::byte> buffer) override
{
const std::int64_t got=file_.read_at(offset,buffer);
return got<0?reader_failure(got):got;
}
bool write_at(std::uint64_t offset,std::span<const std::byte> bytes) override
{
if(offset>std::numeric_limits<std::uint64_t>::max()-bytes.size()||!file_.write_at(offset,bytes))return false;
size_=std::max(size_,offset+bytes.size());
return true;
}
std::uint64_t size() const override{return size_;}
bool truncate(std::uint64_t size) override
{
if(!file_.truncate(size))return false;
size_=size;
return true;
}
std::expected<void,FileError> commit() override{return {};}
private:
platform::File file_;
std::uint64_t size_;
};
}

FileError file_error(const std::error_code& error) noexcept
{
switch(platform::error_kind(error)){
case platform::ErrorKind::not_found:
return FileError::not_found;
case platform::ErrorKind::not_a_directory:
return FileError::not_a_directory;
case platform::ErrorKind::is_directory:
return FileError::is_directory;
case platform::ErrorKind::access_denied:
return FileError::access_denied;
default:
return FileError::io;
}
}

EntryKind entry_kind(platform::NodeKind kind) noexcept
{
switch(kind){
case platform::NodeKind::file:
return EntryKind::file;
case platform::NodeKind::directory:
return EntryKind::directory;
default:
return EntryKind::none;
}
}

std::int64_t to_ms(std::int64_t ns) noexcept
{
const std::int64_t ms=ns/1000000;
return ns%1000000<0?ms-1:ms;
}

platform::FileAccess access_of(WriteMode mode) noexcept
{
switch(mode){
case WriteMode::truncate:
return platform::FileAccess::create_truncate;
case WriteMode::append:
return platform::FileAccess::create_or_open;
default:
return platform::FileAccess::read_write;
}
}

std::shared_ptr<Reader> make_file_reader(platform::File file)
{
return std::make_shared<FileReader>(std::move(file));
}

std::unique_ptr<WriteTarget> make_file_writer(platform::File file)
{
return std::make_unique<FileWriter>(std::move(file));
}

namespace {
// Removes a folder known not to be a link, with everything in it; the listing tells which children are links.
std::error_code remove_folder_tree(const fs::path& path)
{
std::vector<platform::DirectoryEntry> entries;
if(std::error_code error=platform::list_directory(path,entries))return error;
for(const platform::DirectoryEntry& entry:entries){
const fs::path child=path/path_from_utf8(entry.name);
const std::error_code error=!entry.link&&entry.kind==platform::NodeKind::directory?remove_folder_tree(child):platform::remove_file(child);
if(error&&platform::error_kind(error)!=platform::ErrorKind::not_found)return error;
}
return platform::remove_directory(path);
}
}

std::error_code remove_tree(const fs::path& path)
{
// A link to a folder goes, never what it leads to (as std::filesystem::remove_all).
if(platform::is_link(path))return platform::remove_file(path);
return remove_folder_tree(path);
}

std::error_code make_directories(const fs::path& path)
{
const auto node=platform::stat_path(path);
if(node)return node->kind==platform::NodeKind::directory?std::error_code():std::error_code(ENOTDIR,std::generic_category());
if(platform::error_kind(node.error())!=platform::ErrorKind::not_found)return node.error();
const fs::path parent=path.parent_path();
if(!parent.empty()&&parent!=path)if(std::error_code error=make_directories(parent))return error;
std::error_code error=platform::make_directory(path);
// Made meanwhile by someone else.
if(error&&platform::error_kind(error)==platform::ErrorKind::exists){
const auto made=platform::stat_path(path);
if(made&&made->kind==platform::NodeKind::directory)error.clear();
}
return error;
}
}

namespace {
// The parent folder of a '/'-joined real path ("" for a name in the root).
std::string_view parent_of(std::string_view real) noexcept
{
const std::size_t slash=real.rfind('/');
return slash==std::string_view::npos?std::string_view():real.substr(0,slash);
}
std::string_view leaf_of(std::string_view real) noexcept
{
const std::size_t slash=real.rfind('/');
return slash==std::string_view::npos?real:real.substr(slash+1);
}
EntryInfo entry_of(std::string_view relative,const platform::NodeInfo& node)
{
EntryInfo info;
info.kind=detail::entry_kind(node.kind);
info.size=node.size;
info.modified_utc_ms=detail::to_ms(node.modified_utc_ns);
info.name.assign(leaf_of(relative));
return info;
}
bool fall_through(FileError error) noexcept
{
return error==FileError::not_found||error==FileError::not_a_directory;
}
}

FolderSource::FolderSource(fs::path root,bool read_only,bool create_root)
:root_(std::move(root)),read_only_(read_only),create_root_(create_root)
{
}

fs::path FolderSource::host_path(std::string_view real) const
{
return real.empty()?root_:root_/path_from_utf8(real);
}

FolderSource::Match FolderSource::match(const Names& names,std::string_view segment)
{
const std::string fold=fold_name(segment);
const auto range=std::ranges::equal_range(names.names,std::string_view(fold),{},[](const Name& name){return std::string_view(name.fold);});
Match result;
if(range.empty())return result;
for(const Name& name:range){
if(name.real==segment){
result.name=&name;
return result;
}
}
if(range.size()==1)result.name=&range.front();
else result.ambiguous=true;
return result;
}

std::expected<std::shared_ptr<const FolderSource::Names>,FileError> FolderSource::folder_names(const std::string& real_folder)
{
const std::string key=fold_name(real_folder);
std::shared_ptr<const Names> cached;
{
const std::lock_guard lock(cache_mutex_);
if(const auto it=cache_.find(key);it!=cache_.end())cached=it->second;
}
const fs::path folder=host_path(real_folder);
// The folder's time is read before its names, so a change during the scan leaves a time that differs from the next one read.
const auto node=platform::stat_path(folder);
if(!node){
const FileError error=detail::file_error(node.error());
if(error==FileError::not_found&&real_folder.empty()&&create_root_){
static const std::shared_ptr<const Names> empty=std::make_shared<const Names>();
return empty;
}
return std::unexpected(error);
}
if(node->kind!=platform::NodeKind::directory)return std::unexpected(FileError::not_a_directory);
if(cached&&!cached->racy&&cached->modified_ns==node->modified_utc_ns&&cached->real==real_folder)return cached;
const std::int64_t scanned_ns=platform::utc_now_ms()*1000000;
std::vector<platform::DirectoryEntry> entries;
if(const std::error_code error=platform::list_directory(folder,entries))return std::unexpected(detail::file_error(error));
auto names=std::make_shared<Names>();
names->real=real_folder;
names->modified_ns=node->modified_utc_ns;
names->racy=node->modified_utc_ns>scanned_ns-racy_window_ns;
names->names.reserve(entries.size());
for(platform::DirectoryEntry& entry:entries){
if(entry.kind!=platform::NodeKind::file&&entry.kind!=platform::NodeKind::directory)continue;
if(!valid_name(entry.name))continue;
Name& name=names->names.emplace_back();
name.fold=fold_name(entry.name);
name.real=std::move(entry.name);
name.kind=entry.kind;
}
std::ranges::sort(names->names,[](const Name& a,const Name& b){return a.fold!=b.fold?a.fold<b.fold:a.real<b.real;});
std::shared_ptr<const Names> result=std::move(names);
{
const std::lock_guard lock(cache_mutex_);
cache_.insert_or_assign(key,result);
}
return result;
}

void FolderSource::invalidate(std::string_view real_folder)
{
const std::string key=fold_name(real_folder);
const std::lock_guard lock(cache_mutex_);
cache_.erase(key);
}

void FolderSource::invalidate_tree(std::string_view real_folder)
{
const std::string key=fold_name(real_folder);
const std::lock_guard lock(cache_mutex_);
std::erase_if(cache_,[&key](const auto& entry){
const std::string& folder=entry.first;
return folder.starts_with(key)&&(folder.size()==key.size()||folder[key.size()]=='/');
});
}

std::expected<FolderSource::Resolved,FileError> FolderSource::resolve(std::string_view relative,bool leaf_may_miss)
{
Resolved result;
if(relative.empty()){
const auto node=platform::stat_path(root_);
if(node){
result.node=*node;
return result;
}
const FileError error=detail::file_error(node.error());
if(error!=FileError::not_found||!create_root_)return std::unexpected(error);
// A data folder that no write has made yet reads as an empty folder.
result.node.kind=platform::NodeKind::directory;
return result;
}
// The exact name first: one system call when the spelling is right (always, on a file system that ignores case).
const auto exact=platform::stat_path(host_path(relative));
if(exact){
result.real.assign(relative);
result.node=*exact;
return result;
}
if(const FileError error=detail::file_error(exact.error());!fall_through(error))return std::unexpected(error);
result.real.reserve(relative.size());
for(std::size_t at=0;;){
const std::size_t cut=relative.find('/',at);
const bool last=cut==std::string_view::npos;
const std::string_view segment=relative.substr(at,last?std::string_view::npos:cut-at);
const auto names=folder_names(result.real);
if(!names)return std::unexpected(names.error());
const Match found=match(**names,segment);
if(found.ambiguous)return std::unexpected(FileError::ambiguous);
if(!result.real.empty())result.real.push_back('/');
if(!found.name){
if(!last||!leaf_may_miss)return std::unexpected(FileError::not_found);
result.real.append(segment);
result.node=platform::NodeInfo{};
return result;
}
result.real.append(found.name->real);
if(last)break;
if(found.name->kind!=platform::NodeKind::directory)return std::unexpected(FileError::not_a_directory);
at=cut+1;
}
const auto node=platform::stat_path(host_path(result.real));
if(!node){
const FileError error=detail::file_error(node.error());
// Removed since the folder was scanned.
if(error==FileError::not_found&&leaf_may_miss){
result.node=platform::NodeInfo{};
return result;
}
return std::unexpected(error);
}
result.node=*node;
return result;
}

std::expected<void,FileError> FolderSource::ensure_root()
{
if(!create_root_)return {};
const auto node=platform::stat_path(root_);
if(node)return {};
if(const std::error_code error=detail::make_directories(root_))return std::unexpected(detail::file_error(error));
invalidate({});
return {};
}

std::expected<EntryInfo,FileError> FolderSource::stat(std::string_view relative)
{
const auto found=resolve(relative,false);
if(!found)return std::unexpected(found.error());
if(found->node.kind!=platform::NodeKind::file&&found->node.kind!=platform::NodeKind::directory)return std::unexpected(FileError::not_found);
// The name as asked: an exact hit on a file system that ignores case never learns the stored spelling, and every platform answers alike.
return entry_of(relative,found->node);
}

std::expected<std::shared_ptr<Reader>,FileError> FolderSource::open_read(std::string_view relative)
{
if(relative.empty())return std::unexpected(FileError::is_directory);
// The exact name first, straight to an open: a right spelling costs one system call and the size query.
auto file=platform::File::open(host_path(relative),platform::FileAccess::read);
if(file)return detail::make_file_reader(std::move(*file));
if(const FileError error=detail::file_error(file.error());!fall_through(error))return std::unexpected(error);
const auto found=resolve(relative,false);
if(!found)return std::unexpected(found.error());
if(found->node.kind==platform::NodeKind::directory)return std::unexpected(FileError::is_directory);
if(found->node.kind!=platform::NodeKind::file)return std::unexpected(FileError::not_found);
file=platform::File::open(host_path(found->real),platform::FileAccess::read);
if(!file)return std::unexpected(detail::file_error(file.error()));
return detail::make_file_reader(std::move(*file));
}

std::expected<std::unique_ptr<WriteTarget>,FileError> FolderSource::open_write(std::string_view relative,WriteMode mode)
{
if(read_only_)return std::unexpected(FileError::access_denied);
if(relative.empty())return std::unexpected(FileError::is_directory);
if(mode!=WriteMode::update)if(auto made=ensure_root();!made)return std::unexpected(made.error());
const auto found=resolve(relative,mode!=WriteMode::update);
if(!found)return std::unexpected(found.error());
if(found->node.kind==platform::NodeKind::directory)return std::unexpected(FileError::is_directory);
const bool created=found->node.kind==platform::NodeKind::none;
auto file=platform::File::open(host_path(found->real),detail::access_of(mode));
if(!file)return std::unexpected(detail::file_error(file.error()));
if(created)invalidate(parent_of(found->real));
return detail::make_file_writer(std::move(*file));
}

std::expected<void,FileError> FolderSource::list(std::string_view relative,std::vector<EntryInfo>& out)
{
const auto found=resolve(relative,false);
if(!found)return std::unexpected(found.error());
if(found->node.kind!=platform::NodeKind::directory)return std::unexpected(FileError::not_a_directory);
const auto names=folder_names(found->real);
if(!names)return std::unexpected(names.error());
out.reserve(out.size()+(*names)->names.size());
for(const Name& name:(*names)->names){
EntryInfo& info=out.emplace_back();
info.kind=detail::entry_kind(name.kind);
info.name=name.real;
}
return {};
}

std::expected<void,FileError> FolderSource::remove(std::string_view relative)
{
if(read_only_)return std::unexpected(FileError::access_denied);
if(relative.empty())return std::unexpected(FileError::is_directory);
const auto found=resolve(relative,false);
if(!found)return std::unexpected(found.error());
if(found->node.kind==platform::NodeKind::directory)return std::unexpected(FileError::is_directory);
const std::error_code error=platform::remove_file(host_path(found->real));
invalidate(parent_of(found->real));
if(error)return std::unexpected(detail::file_error(error));
return {};
}

std::expected<void,FileError> FolderSource::make_directory(std::string_view relative)
{
if(read_only_)return std::unexpected(FileError::access_denied);
if(auto made=ensure_root();!made)return std::unexpected(made.error());
const auto found=resolve(relative,true);
if(!found)return std::unexpected(found.error());
if(found->node.kind==platform::NodeKind::directory)return {};
if(found->node.kind!=platform::NodeKind::none)return std::unexpected(FileError::not_a_directory);
std::error_code error=platform::make_directory(host_path(found->real));
invalidate(parent_of(found->real));
if(error&&platform::error_kind(error)==platform::ErrorKind::exists){
const auto made=platform::stat_path(host_path(found->real));
if(made&&made->kind==platform::NodeKind::directory)return {};
return std::unexpected(FileError::not_a_directory);
}
if(error)return std::unexpected(detail::file_error(error));
return {};
}

std::expected<void,FileError> FolderSource::remove_directory(std::string_view relative,bool recursive)
{
if(read_only_||relative.empty())return std::unexpected(FileError::access_denied);
const auto found=resolve(relative,false);
if(!found)return std::unexpected(found.error());
if(found->node.kind!=platform::NodeKind::directory)return std::unexpected(FileError::not_a_directory);
const fs::path folder=host_path(found->real);
const std::error_code error=recursive?detail::remove_tree(folder):platform::remove_directory(folder);
invalidate(parent_of(found->real));
invalidate_tree(found->real);
if(error)return std::unexpected(detail::file_error(error));
return {};
}

bool FolderSource::real_spelling(std::string_view relative,std::string& real)
{
if(relative.empty())return false;
std::string spelled;
spelled.reserve(relative.size());
for(std::size_t at=0;;){
const std::size_t cut=relative.find('/',at);
const bool last=cut==std::string_view::npos;
const std::string_view segment=relative.substr(at,last?std::string_view::npos:cut-at);
const auto names=folder_names(spelled);
if(!names)return false;
const Match found=match(**names,segment);
if(!found.name)return false;
if(!spelled.empty())spelled.push_back('/');
spelled.append(found.name->real);
if(last)break;
at=cut+1;
}
if(spelled==relative)return false;
real=std::move(spelled);
return true;
}

std::optional<std::pair<std::string,std::string>> FolderSource::ambiguous_names(std::string_view relative)
{
std::string spelled;
for(std::size_t at=0;;){
const std::size_t cut=relative.find('/',at);
const bool last=cut==std::string_view::npos;
const std::string_view segment=relative.substr(at,last?std::string_view::npos:cut-at);
const auto names=folder_names(spelled);
if(!names)return std::nullopt;
const Match found=match(**names,segment);
const std::string prefix=spelled.empty()?std::string():spelled+"/";
if(found.ambiguous){
// The fold's names are sorted by bytes inside their range, so the first two are the smallest.
const std::string fold=fold_name(segment);
const auto range=std::ranges::equal_range((*names)->names,std::string_view(fold),{},[](const Name& name){return std::string_view(name.fold);});
return std::pair(prefix+range[0].real,prefix+range[1].real);
}
if(!found.name||last)return std::nullopt;
spelled=prefix+found.name->real;
at=cut+1;
}
}
}
