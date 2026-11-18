// fill_entropy on POSIX systems (see platform/entropy.hpp): getrandom from the kernel's pool (retried on EINTR and short
// reads), falling back to /dev/urandom where the call is missing (ENOSYS) or not permitted.
#include <platform/entropy.hpp>
#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

namespace zodiac::platform {
namespace {
bool read_urandom(unsigned char* bytes,std::size_t size) noexcept
{
const int file=::open("/dev/urandom",O_RDONLY|O_CLOEXEC);
if(file<0)return false;
while(size>0){
const ssize_t got=::read(file,bytes,size);
if(got<0&&errno==EINTR)continue;
if(got<=0){
::close(file);
return false;
}
bytes+=got;
size-=static_cast<std::size_t>(got);
}
::close(file);
return true;
}
}

bool fill_entropy(void* buffer,std::size_t size) noexcept
{
auto* bytes=static_cast<unsigned char*>(buffer);
while(size>0){
const ssize_t got=::getrandom(bytes,size,0);
if(got<0&&errno==EINTR)continue;
if(got<0)return (errno==ENOSYS||errno==EPERM)&&read_urandom(bytes,size);
bytes+=got;
size-=static_cast<std::size_t>(got);
}
return true;
}
}
