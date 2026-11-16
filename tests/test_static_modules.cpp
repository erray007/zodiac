// Links zodiac_mod_example_static and zodiac_mod_near_static with -u zodiac_static_module_<name> and walks the .zodmod
// section (Windows only: COFF grouped sections).
#include <doctest/doctest.h>
#include <core/static_modules.hpp>
#include <algorithm>
#include <string_view>

// The record of the "near" fixture under the name zodiac build forces in. Linking fails when the module's name went
// through windows.h's empty near macro (the record would be zodiac_static_module_).
extern "C" const zodiac_module_info* const zodiac_static_module_near;

namespace {
const zodiac_module_info* find(std::string_view name)
{
const auto modules=zodiac::linked_static_modules();
const auto found=std::ranges::find_if(modules,[name](const zodiac_module_info* info){return info!=nullptr&&std::string_view(info->name)==name;});
return found==modules.end()?nullptr:*found;
}
}

TEST_CASE("the forced static module records are found between the section markers")
{
const auto modules=zodiac::linked_static_modules();
REQUIRE(modules.size()==2);
CHECK(std::ranges::none_of(modules,[](const zodiac_module_info* info){return info==nullptr;}));
const zodiac_module_info* const example=find("example");
REQUIRE(example!=nullptr);
CHECK(example->abi_version==ZODIAC_ABI_VERSION);
CHECK(example->link_modes==(ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC));
}

TEST_CASE("a module named like a windows.h macro keeps its record name")
{
const zodiac_module_info* const near_module=find("near");
REQUIRE(near_module!=nullptr);
CHECK(near_module==zodiac_static_module_near);
}

TEST_CASE("repeated calls return the same records")
{
const auto first=zodiac::linked_static_modules();
const auto second=zodiac::linked_static_modules();
CHECK(first.data()==second.data());
CHECK(first.size()==second.size());
}
