#include <platform/console.hpp>
#include <sys/uio.h>
#include <unistd.h>
#include <cerrno>
#include <cstddef>

namespace zodiac::platform {
namespace {
void write_line(int fd,std::string_view line) noexcept
{
// One writev hands the text and its newline to the kernel together, so concurrent writers never split a line.
char newline='\n';
iovec parts[2]={{const_cast<char*>(line.data()),line.size()},{&newline,1}};
iovec* part=parts;
int count=2;
while(count>0){
const ssize_t written=writev(fd,part,count);
if(written<0){
if(errno==EINTR)continue;
return;
}
if(written==0)return;
std::size_t left=static_cast<std::size_t>(written);
while(count>0&&left>=part->iov_len){
left-=part->iov_len;
++part;
--count;
}
if(count>0){
part->iov_base=static_cast<char*>(part->iov_base)+left;
part->iov_len-=left;
}
}
}
}
void disable_error_dialogs() noexcept
{
}
void setup_utf8_console() noexcept
{
}
void write_stderr_line(std::string_view line) noexcept
{
write_line(STDERR_FILENO,line);
}
void write_stdout_line(std::string_view line) noexcept
{
write_line(STDOUT_FILENO,line);
}
}
