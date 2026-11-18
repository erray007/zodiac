// fill_entropy on Windows (see platform/entropy.hpp): RtlGenRandom (advapi32's SystemFunction036), which every game links
// already (advapi32 is in the captured release link line), and which needs no provider handle.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/entropy.hpp>
#include <windows.h>
#include <ntsecapi.h>

namespace zodiac::platform {
bool fill_entropy(void* buffer,std::size_t size) noexcept
{
auto* bytes=static_cast<unsigned char*>(buffer);
while(size>0){
const ULONG chunk=static_cast<ULONG>(size>0x40000000u?0x40000000u:size);
if(!RtlGenRandom(bytes,chunk))return false;
bytes+=chunk;
size-=chunk;
}
return true;
}
}
