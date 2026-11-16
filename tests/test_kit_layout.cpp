#include <core/kit_layout.hpp>
#include <doctest/doctest.h>
#include <filesystem>

TEST_CASE("kit_layout derives every folder from the executable")
{
const auto kit=zodiac::kit_layout("/kit root/bin/zodiac.exe");
CHECK(kit.root==std::filesystem::path("/kit root"));
CHECK(kit.bin==std::filesystem::path("/kit root/bin"));
CHECK(kit.modules==std::filesystem::path("/kit root/modules"));
CHECK(kit.release_lib==std::filesystem::path("/kit root/lib/release"));
CHECK(kit.static_modules==std::filesystem::path("/kit root/modules/static"));
}
