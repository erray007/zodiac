#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/process.hpp>
#include <windows.h>
#include <string>
#include <utility>

namespace zodiac::platform {
std::filesystem::path executable_path()
{
std::wstring buffer(MAX_PATH,L'\0');
for(;;){
const DWORD length=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));
if(length==0)return {};
if(length<buffer.size()){
buffer.resize(length);
return std::filesystem::path(std::move(buffer));
}
if(buffer.size()>=32768)return {};
buffer.resize(buffer.size()*2);
}
}
}
