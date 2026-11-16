#include <cli/build_report.hpp>
#include <doctest/doctest.h>
#include <filesystem>
#include <string>

TEST_CASE("BuildReport counts each error once and lists every line after the summary")
{
zodiac::cli::BuildReport report;
report.write(ZODIAC_LOG_ERROR,"main.zs:3:1: error: no matching function");
report.write(ZODIAC_LOG_ERROR,"  at main (main.zs:3)");
report.write(ZODIAC_LOG_ERROR,"main.zs:7:5: error: unknown name");
CHECK(report.errors()==2);
CHECK(report.text(false,{})=="Build failed: 2 errors\nmain.zs:3:1: error: no matching function\n  at main (main.zs:3)\nmain.zs:7:5: error: unknown name");
}

TEST_CASE("BuildReport names the executable of a successful build")
{
zodiac::cli::BuildReport report;
const std::filesystem::path exe=std::filesystem::path("C:/x/game.exe");
CHECK(report.text(true,exe)=="Build succeeded: C:/x/game.exe");
CHECK(report.summary(true,exe)=="Build succeeded: C:/x/game.exe");
}

TEST_CASE("BuildReport says 1 error, keeps warnings and does not count them")
{
zodiac::cli::BuildReport report;
report.write(ZODIAC_LOG_WARNING,"warning: module \"gpl\" is linked statically under license GPL-3.0-only; check its terms");
report.write(ZODIAC_LOG_ERROR,"error: cannot replace \"game.exe\": it is in use (is the game still running?)");
CHECK(report.summary(false,{})=="Build failed: 1 error");
CHECK(report.text(false,{})=="Build failed: 1 error\nwarning: module \"gpl\" is linked statically under license GPL-3.0-only; check its terms\nerror: cannot replace \"game.exe\": it is in use (is the game still running?)");
}

TEST_CASE("BuildReport with no errors and no lines")
{
const zodiac::cli::BuildReport report;
CHECK(report.text(false,{})=="Build failed: 0 errors");
}
