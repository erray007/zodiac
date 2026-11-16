// Fixture module that imports a third-party DLL (vendor_lib.dll) through its delay-import table, so it registers even when that DLL is
// missing and zodiac build itself must notice it. Registers int vendor_value(int value), which calls vendor_lib_value (value*7) and so
// loads vendor_lib.dll on the first call.
#include <zodiac/plugin.h>
#include <zodiac/plugin.hpp>

extern "C" int vendor_lib_value(int value);

namespace {
int vendor_value(int value)
{
return vendor_lib_value(value);
}
int register_module(zodiac_host* host)
{
return zodiac::plugin::function<&vendor_value>(host,"int vendor_value(int value)");
}
const zodiac_module_info g_info={ZODIAC_ABI_VERSION,sizeof(zodiac_module_info),"vendor_dep","0.0.1","Zlib",nullptr,0,&register_module,nullptr,ZODIAC_LINK_DYNAMIC};
}

ZODIAC_DEFINE_MODULE(g_info)
