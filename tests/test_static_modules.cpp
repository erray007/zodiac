// Links zodiac_mod_example_static with -u zodiac_static_module_example and walks the .zodmod section (Windows only: COFF grouped sections).
#include <doctest/doctest.h>
#include <core/static_modules.hpp>
#include <string_view>

TEST_CASE("the forced static module record is found between the section markers")
{
const auto modules=zodiac::linked_static_modules();
REQUIRE(modules.size()==1);
REQUIRE(modules[0]!=nullptr);
CHECK(std::string_view(modules[0]->name)=="example");
CHECK(modules[0]->abi_version==ZODIAC_ABI_VERSION);
CHECK(modules[0]->link_modes==(ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC));
}

TEST_CASE("repeated calls return the same records")
{
const auto first=zodiac::linked_static_modules();
const auto second=zodiac::linked_static_modules();
CHECK(first.data()==second.data());
CHECK(first.size()==second.size());
}
