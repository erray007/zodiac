// A valid shared library that does not export zodiac_module_get_info.
#include <zodiac/plugin.h>

ZODIAC_LINKAGE_C ZODIAC_EXPORT int zodiac_fixture_unrelated_export(void)
{
return 1;
}
