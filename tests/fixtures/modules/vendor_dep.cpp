// Fixture modules that import a third-party DLL (vendor_lib.dll), built twice:
// - vendor_dep imports it through its delay-import table, so it registers even when that DLL is missing; registers int vendor_value(int value).
// - vendor_direct (VENDOR_DIRECT defined) imports it normally, as a module using BASS or FMOD does, so it cannot even load without it;
//   registers int vendor_direct_value(int value).
// Both functions return vendor_lib_value(value) (value*7).
#include <zodiac/plugin.h>
#include <zodiac/plugin.hpp>

#ifdef VENDOR_DIRECT
#define VENDOR_DEP_NAME "vendor_direct"
#define VENDOR_DEP_DECLARATION "int vendor_direct_value(int value)"
#else
#define VENDOR_DEP_NAME "vendor_dep"
#define VENDOR_DEP_DECLARATION "int vendor_value(int value)"
#endif

extern "C" int vendor_lib_value(int value);

namespace {
int vendor_value(int value)
{
return vendor_lib_value(value);
}
int register_module(zodiac_host* host)
{
return zodiac::plugin::function<&vendor_value>(host,VENDOR_DEP_DECLARATION);
}
const zodiac_module_info g_info={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),VENDOR_DEP_NAME,"0.0.1","Zlib",nullptr,0,&register_module,nullptr,ZODIAC_LINK_DYNAMIC};
}

ZODIAC_DEFINE_MODULE(g_info)
