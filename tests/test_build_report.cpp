#include <cli/build_report.hpp>
#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <doctest/doctest.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace fs=std::filesystem;
using zodiac::cli::ResultReport;
using zodiac::cli::ResultVerb;

// A fresh folder (with a space and non-ASCII letters in its name) removed again at the end of the test.
class TempFolder{
public:
TempFolder()
{
static std::atomic<int> counter{0};
path_=fs::temp_directory_path()/zodiac::path_from_utf8("zodiac report test ş "+std::to_string(counter.fetch_add(1))+"_"+std::to_string(reinterpret_cast<std::uintptr_t>(this)));
fs::remove_all(path_);
fs::create_directories(path_);
}
~TempFolder()
{
std::error_code ec;
fs::remove_all(path_,ec);
}
TempFolder(const TempFolder&)=delete;
TempFolder& operator=(const TempFolder&)=delete;
const fs::path& path() const noexcept{return path_;}
private:
fs::path path_;
};

std::string read_file(const fs::path& path)
{
std::ifstream in(path,std::ios::binary);
return std::string((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
}
}

TEST_CASE("ResultReport counts each error once and lists every line after the summary")
{
ResultReport report(ResultVerb::build);
report.write(ZODIAC_LOG_ERROR,"main.zs:3:1: error: no matching function");
report.write(ZODIAC_LOG_ERROR,"  at main (main.zs:3)");
report.write(ZODIAC_LOG_ERROR,"main.zs:7:5: error: unknown name");
CHECK(report.errors()==2);
CHECK(report.text(false,{})=="Build failed: 2 errors\nmain.zs:3:1: error: no matching function\n  at main (main.zs:3)\nmain.zs:7:5: error: unknown name");
}

TEST_CASE("ResultReport names the executable of a successful build")
{
const ResultReport report(ResultVerb::build);
const fs::path exe=fs::path("C:/x/game.exe");
CHECK(report.text(true,exe)=="Build succeeded: C:/x/game.exe");
CHECK(report.summary(true,exe)=="Build succeeded: C:/x/game.exe");
}

TEST_CASE("ResultReport says 1 error, keeps warnings and does not count them")
{
ResultReport report(ResultVerb::build);
report.write(ZODIAC_LOG_WARNING,"warning: module \"gpl\" is linked statically under license GPL-3.0-only; check its terms");
report.write(ZODIAC_LOG_ERROR,"error: cannot replace \"game.exe\": it is in use (is the game still running?)");
CHECK(report.summary(false,{})=="Build failed: 1 error");
CHECK(report.text(false,{})=="Build failed: 1 error\nwarning: module \"gpl\" is linked statically under license GPL-3.0-only; check its terms\nerror: cannot replace \"game.exe\": it is in use (is the game still running?)");
}

TEST_CASE("ResultReport with no errors and no lines")
{
const ResultReport report(ResultVerb::build);
CHECK(report.text(false,{})=="Build failed: 0 errors");
}

TEST_CASE("ResultReport of a run says Run failed, singular and plural, and never names an executable")
{
ResultReport report(ResultVerb::run);
report.write(ZODIAC_LOG_INFO,"lgpl_dynamic register");
report.write(ZODIAC_LOG_ERROR,"main.zs:4: runtime error: division by zero");
report.write(ZODIAC_LOG_ERROR,"  at main (main.zs:4)");
CHECK(report.errors()==1);
CHECK(report.summary(false,{})=="Run failed: 1 error");
CHECK(report.summary(true,fs::path("C:/x/game.exe"))=="Run failed: 1 error");
CHECK(report.text(false,{})=="Run failed: 1 error\nlgpl_dynamic register\nmain.zs:4: runtime error: division by zero\n  at main (main.zs:4)");
report.write(ZODIAC_LOG_ERROR,"error: module \"x\" failed to shut down");
CHECK(report.summary(false,{})=="Run failed: 2 errors");
}

TEST_CASE("ResultReport keeps the newest lines within its text budget and says how many it left out")
{
// Each line takes its size plus one byte for its line break: three 9-byte lines fit a 30-byte budget, the fourth pushes the first out.
ResultReport report(ResultVerb::run,30);
report.write(ZODIAC_LOG_ERROR,"error: 01");
report.write(ZODIAC_LOG_INFO,"info:  02");
report.write(ZODIAC_LOG_INFO,"info:  03");
CHECK(report.text(false,{})=="Run failed: 1 error\nerror: 01\ninfo:  02\ninfo:  03");
report.write(ZODIAC_LOG_ERROR,"error: 04");
report.write(ZODIAC_LOG_INFO,"info:  05");
// Dropped lines still count.
CHECK(report.errors()==2);
CHECK(report.text(false,{})=="Run failed: 2 errors\n(2 earlier lines left out; the console shows every line)\ninfo:  03\nerror: 04\ninfo:  05");
// A single line longer than the budget is still kept whole.
report.write(ZODIAC_LOG_ERROR,"error: a line longer than the whole budget of thirty bytes");
CHECK(report.text(false,{})=="Run failed: 3 errors\n(5 earlier lines left out; the console shows every line)\nerror: a line longer than the whole budget of thirty bytes");
}

TEST_CASE("create_project_file writes the generated project file with no modules and never replaces one")
{
const TempFolder folder;
const fs::path file=folder.path()/"zodiac.toml";
const std::string expected=zodiac::project_file_text("oyun","oyun.zs",{},{});
CHECK(zodiac::cli::create_project_file(folder.path(),"oyun","oyun.zs")=="Created "+zodiac::path_to_utf8(file));
CHECK(read_file(file)==expected);
CHECK(expected.find("static = []\ndynamic = []\n")!=std::string::npos);
// The written file is a project the script can be built from.
{
std::ofstream script(folder.path()/"oyun.zs",std::ios::binary);
script<<"void main()\n{\n}\n";
}
const auto project=zodiac::load_project(folder.path());
REQUIRE(project);
CHECK(project->name=="oyun");
CHECK(project->main_script==fs::absolute(folder.path()).lexically_normal()/"oyun.zs");
CHECK(project->static_modules.empty());
CHECK(project->dynamic_modules.empty());
// A second press, or a file that appeared meanwhile, is refused and left as it is.
{
std::ofstream out(file,std::ios::binary|std::ios::trunc);
out<<"# mine\n";
}
CHECK(zodiac::cli::create_project_file(folder.path(),"oyun","oyun.zs")=="error: \""+zodiac::path_to_utf8(file)+"\" already exists");
CHECK(read_file(file)=="# mine\n");
}

TEST_CASE("create_project_file reports a folder it cannot write to on one line")
{
const TempFolder folder;
const fs::path missing=folder.path()/"gone";
const std::string line=zodiac::cli::create_project_file(missing,"oyun","oyun.zs");
CHECK(line.starts_with("error: cannot create \""+zodiac::path_to_utf8(missing/"zodiac.toml")+"\""));
CHECK(line.find('\n')==std::string::npos);
CHECK_FALSE(fs::exists(missing));
}
