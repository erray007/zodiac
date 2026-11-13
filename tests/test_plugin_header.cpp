#include <doctest/doctest.h>
#include <zodiac/plugin.h>
#include <cstddef>
#include <string>

extern "C" const zodiac_module_info* zodiac_test_get_c_module_info(void);
extern "C" const zodiac_module_info* zodiac_module_get_info(void);

TEST_CASE("abi layout is stable")
{
static_assert(offsetof(zodiac_host, abi_version) == 0);
static_assert(offsetof(zodiac_host, struct_size) == 4);
CHECK(ZODIAC_ABI_VERSION == 1);
CHECK(std::string(ZODIAC_MODULE_ENTRY_NAME) == "zodiac_module_get_info");
}

TEST_CASE("module info defined in C is exported through the entry point")
{
const zodiac_module_info* info = zodiac_test_get_c_module_info();
REQUIRE(info != nullptr);
CHECK(std::string(info->name) == "plugin_header_c");
CHECK(info->abi_version == ZODIAC_ABI_VERSION);
CHECK(zodiac_module_get_info() == info);
}
