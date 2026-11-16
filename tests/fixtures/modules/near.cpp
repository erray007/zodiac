// A module named "near", which windows.h (minwindef.h) defines as an empty macro. Its static record must still be
// zodiac_static_module_near: zodiac_add_module passes the finished name as ZODIAC_MODULE_RECORD (see zodiac/plugin.h).
#ifdef _WIN32
#include <windows.h>
#ifndef near
#error windows.h no longer defines near; pick another macro name for this fixture
#endif
#endif
#include <zodiac/plugin.h>

namespace {
int register_module(zodiac_host*)
{
return ZODIAC_OK;
}
}

extern "C" const zodiac_module_info zodiac_module_info_near={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"near","1.0.0","Zlib",nullptr,0,&register_module,nullptr,ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC};
ZODIAC_DEFINE_MODULE(zodiac_module_info_near)
