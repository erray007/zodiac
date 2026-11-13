#include <zodiac/plugin.h>

static const zodiac_module_info zodiac_test_module_info = {
.abi_version = ZODIAC_ABI_VERSION,
.name = "plugin_header_c",
.version = "0.1.0",
.license = "Zlib",
.dependencies = NULL,
.dependency_count = 0,
.register_module = NULL,
.shutdown_module = NULL,
};

ZODIAC_DEFINE_MODULE(zodiac_test_module_info)

const zodiac_module_info* zodiac_test_get_c_module_info(void)
{
return &zodiac_test_module_info;
}
