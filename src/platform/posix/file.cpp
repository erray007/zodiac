// Files and folders on POSIX systems (see platform/file.hpp): open with O_CLOEXEC (and O_NONBLOCK, so a FIFO can never block an open;
// it changes nothing for regular files), pread/pwrite retried on EINTR and short counts, readdir listings that trust d_type and stat
// only links and unknown entries, and rename for replace_file.
#include <platform/file.hpp>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace zodiac::platform {
namespace {
namespace fs=std::filesystem;
std::error_code errno_code(int value) noexcept
{
return std::error_code(value,std::generic_category());
}
std::int64_t unix_ns(const struct stat& info) noexcept
{
#if defined(__APPLE__)
const struct timespec& time=info.st_mtimespec;
#else
const struct timespec& time=info.st_mtim;
#endif
constexpr std::int64_t limit=INT64_MAX/1000000000;
const auto seconds=static_cast<std::int64_t>(time.tv_sec);
if(seconds>=limit)return INT64_MAX;
if(seconds<= -limit)return INT64_MIN;
return seconds*1000000000+static_cast<std::int64_t>(time.tv_nsec);
}
NodeKind kind_of(mode_t mode) noexcept
{
return S_ISREG(mode)?NodeKind::file:S_ISDIR(mode)?NodeKind::directory:NodeKind::other;
}
// Closes a directory stream on every exit.
struct DirectoryStream{
DIR* dir;
~DirectoryStream(){if(dir)::closedir(dir);}
};
}

std::expected<File,std::error_code> File::open(const fs::path& path,FileAccess access)
{
int flags=O_CLOEXEC|O_NONBLOCK;
switch(access){
case FileAccess::read:
flags|=O_RDONLY;
break;
case FileAccess::read_write:
flags|=O_RDWR;
break;
case FileAccess::create_truncate:
flags|=O_RDWR|O_CREAT|O_TRUNC;
break;
case FileAccess::create_or_open:
flags|=O_RDWR|O_CREAT;
break;
}
int fd;
do{
fd=::open(path.c_str(),flags,0666);
}while(fd<0&&errno==EINTR);
if(fd<0)return std::unexpected(errno_code(errno));
// A folder opens read-only on POSIX; a device, FIFO or socket is no file either.
struct stat info;
const bool stated=::fstat(fd,&info)==0;
if(!stated||!S_ISREG(info.st_mode)){
const int error=!stated?errno:S_ISDIR(info.st_mode)?EISDIR:EINVAL;
::close(fd);
return std::unexpected(errno_code(error));
}
return File(fd);
}

std::int64_t File::read_at(std::uint64_t offset,std::span<std::byte> buffer) const noexcept
{
std::size_t done=0;
while(done<buffer.size()){
const std::size_t chunk=std::min<std::size_t>(buffer.size()-done,0x40000000u);
const ssize_t got=::pread(static_cast<int>(handle_),buffer.data()+done,chunk,static_cast<off_t>(offset+done));
if(got<0){
if(errno==EINTR)continue;
if(done>0)break;
return -static_cast<std::int64_t>(errno);
}
if(got==0)break;
done+=static_cast<std::size_t>(got);
}
return static_cast<std::int64_t>(done);
}

bool File::write_at(std::uint64_t offset,std::span<const std::byte> bytes) noexcept
{
std::size_t done=0;
while(done<bytes.size()){
const std::size_t chunk=std::min<std::size_t>(bytes.size()-done,0x40000000u);
const ssize_t put=::pwrite(static_cast<int>(handle_),bytes.data()+done,chunk,static_cast<off_t>(offset+done));
if(put<0&&errno==EINTR)continue;
if(put<=0)return false;
done+=static_cast<std::size_t>(put);
}
return true;
}

std::uint64_t File::size() const noexcept
{
struct stat info;
return ::fstat(static_cast<int>(handle_),&info)==0?static_cast<std::uint64_t>(info.st_size):0;
}

bool File::truncate(std::uint64_t size) noexcept
{
if(size>static_cast<std::uint64_t>(INT64_MAX))return false;
int result;
do{
result=::ftruncate(static_cast<int>(handle_),static_cast<off_t>(size));
}while(result!=0&&errno==EINTR);
return result==0;
}

bool File::sync() noexcept
{
return ::fsync(static_cast<int>(handle_))==0;
}

void File::close() noexcept
{
if(handle_==invalid_handle)return;
::close(static_cast<int>(handle_));
handle_=invalid_handle;
}

std::expected<NodeInfo,std::error_code> stat_path(const fs::path& path)
{
struct stat info;
if(::stat(path.c_str(),&info)!=0)return std::unexpected(errno_code(errno));
NodeInfo node;
node.kind=kind_of(info.st_mode);
node.size=node.kind==NodeKind::file?static_cast<std::uint64_t>(info.st_size):0;
node.modified_utc_ns=unix_ns(info);
return node;
}

bool is_link(const fs::path& path)
{
struct stat info;
return ::lstat(path.c_str(),&info)==0&&S_ISLNK(info.st_mode);
}

std::error_code list_directory(const fs::path& path,std::vector<DirectoryEntry>& out)
{
const DirectoryStream stream{::opendir(path.c_str())};
if(!stream.dir)return errno_code(errno);
const int dir_fd=::dirfd(stream.dir);
for(;;){
errno=0;
const dirent* entry=::readdir(stream.dir);
if(!entry){
if(errno!=0)return errno_code(errno);
return {};
}
const char* name=entry->d_name;
if(name[0]=='.'&&(name[1]=='\0'||(name[1]=='.'&&name[2]=='\0')))continue;
DirectoryEntry item;
item.name.assign(name);
switch(entry->d_type){
case DT_REG:
item.kind=NodeKind::file;
break;
case DT_DIR:
item.kind=NodeKind::directory;
break;
case DT_LNK:
case DT_UNKNOWN:{
struct stat info;
if(entry->d_type==DT_UNKNOWN){
if(::fstatat(dir_fd,name,&info,AT_SYMLINK_NOFOLLOW)!=0)continue;
item.link=S_ISLNK(info.st_mode);
if(!item.link){
item.kind=kind_of(info.st_mode);
break;
}
}
else item.link=true;
// A link that leads nowhere keeps kind none: it is still an entry, which removing its folder must remove.
if(::fstatat(dir_fd,name,&info,0)==0)item.kind=kind_of(info.st_mode);
break;
}
default:
item.kind=NodeKind::other;
break;
}
out.push_back(std::move(item));
}
}

std::error_code make_directory(const fs::path& path)
{
return ::mkdir(path.c_str(),0777)==0?std::error_code():errno_code(errno);
}

std::error_code remove_file(const fs::path& path)
{
return ::unlink(path.c_str())==0?std::error_code():errno_code(errno);
}

std::error_code remove_directory(const fs::path& path)
{
if(::rmdir(path.c_str())==0)return {};
// POSIX allows EEXIST for a folder that is not empty.
const int error=errno;
return errno_code(error==EEXIST?ENOTEMPTY:error);
}

ErrorKind error_kind(const std::error_code& code) noexcept
{
const std::error_condition condition=code.default_error_condition();
if(condition.category()!=std::generic_category())return ErrorKind::other;
switch(condition.value()){
case ENOENT:
return ErrorKind::not_found;
case ENOTDIR:
return ErrorKind::not_a_directory;
case EISDIR:
return ErrorKind::is_directory;
case EACCES:
case EPERM:
return ErrorKind::access_denied;
case EEXIST:
return ErrorKind::exists;
case ENOTEMPTY:
return ErrorKind::not_empty;
case EXDEV:
return ErrorKind::cross_device;
default:
return ErrorKind::other;
}
}

bool replace_file(const fs::path& from,const fs::path& to,std::error_code& error)
{
error.clear();
if(std::rename(from.c_str(),to.c_str())==0)return true;
error=errno_code(errno);
return false;
}

fs::path user_data_folder()
{
// The XDG Base Directory rules: a relative $XDG_DATA_HOME is invalid and ignored.
if(const char* data=std::getenv("XDG_DATA_HOME");data&&data[0]=='/')return fs::path(data).lexically_normal();
if(const char* home=std::getenv("HOME");home&&home[0]!='\0')return (fs::path(home)/".local"/"share").lexically_normal();
return {};
}
}
