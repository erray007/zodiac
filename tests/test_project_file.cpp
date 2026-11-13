#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <doctest/doctest.h>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace fs=std::filesystem;
// A fresh directory under the system temp dir, removed on destruction. The project lives in a sub directory with the given name.
class TempProject{
public:
explicit TempProject(const fs::path& name=fs::path(u8"project"))
{
static std::atomic<unsigned> counter{0};
base_=fs::temp_directory_path()/("zodiac_project_test_"+std::to_string(counter.fetch_add(1))+"_"+std::to_string(reinterpret_cast<std::uintptr_t>(this)));
dir_=base_/name;
fs::create_directories(dir_);
}
~TempProject()
{
std::error_code ec;
fs::remove_all(base_,ec);
}
TempProject(const TempProject&)=delete;
TempProject& operator=(const TempProject&)=delete;
const fs::path& dir() const{return dir_;}
void write(const fs::path& relative,std::string_view text) const
{
const fs::path target=dir_/relative;
fs::create_directories(target.parent_path());
std::ofstream out(target,std::ios::binary);
out.write(text.data(),static_cast<std::streamsize>(text.size()));
}
void toml(std::string_view text) const{write("zodiac.toml",text);}
void main_script() const{write("main.zs","void main(){}\n");}
private:
fs::path base_;
fs::path dir_;
};
std::string error_of(const TempProject& project)
{
const auto result=zodiac::load_project(project.dir());
REQUIRE_FALSE(result.has_value());
return result.error();
}
constexpr std::string_view minimal="[game]\nmain = \"main.zs\"\n";
}

TEST_CASE("minimal valid project")
{
TempProject project;
project.main_script();
project.toml(minimal);
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->root==fs::absolute(project.dir()).lexically_normal());
CHECK(result->main_script==result->root/"main.zs");
CHECK(result->main_script.is_absolute());
CHECK(result->static_modules.empty());
CHECK(result->dynamic_modules.empty());
CHECK(result->core_exclude.empty());
}

TEST_CASE("full project with modules, core exclude and trailing separator")
{
TempProject project;
project.write("scripts/start.zs","");
project.toml("[game]\nname = \"My Game\"\nmain = \"scripts/../scripts/start.zs\"\n[modules]\nstatic = [\"core\", \"example\", \"example\", \"audio_2\"]\ndynamic = [\"extra\"]\n[core]\nexclude = [\"window\", \"keyboard\", \"window\"]\n[unknown]\nkey = 1\n");
const auto result=zodiac::load_project(project.dir()/"");
REQUIRE(result.has_value());
CHECK(result->main_script==result->root/"scripts"/"start.zs");
CHECK(result->root.filename()==project.dir().filename());
CHECK(result->static_modules==std::vector<std::string>{"example","audio_2"});
CHECK(result->dynamic_modules==std::vector<std::string>{"extra"});
CHECK(result->core_exclude==std::vector<std::string>{"window","keyboard"});
}

TEST_CASE("diagnostics section defaults when missing")
{
TempProject project;
project.main_script();
project.toml(minimal);
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->diagnostics.console==zodiac::OutputMode::automatic);
CHECK(result->diagnostics.dialog==zodiac::OutputMode::automatic);
CHECK(result->diagnostics.file.empty());
}

TEST_CASE("diagnostics section parsed")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nconsole = \"always\"\nfile = \"logs/zodiac.log\"\ndialog = \"never\"\n");
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->diagnostics.console==zodiac::OutputMode::always);
CHECK(result->diagnostics.dialog==zodiac::OutputMode::never);
CHECK(result->diagnostics.file==result->root/"logs"/"zodiac.log");
}

TEST_CASE("diagnostics auto value")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nconsole = \"auto\"\ndialog = \"auto\"\n");
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->diagnostics.console==zodiac::OutputMode::automatic);
CHECK(result->diagnostics.dialog==zodiac::OutputMode::automatic);
}

TEST_CASE("project in a non-ASCII directory")
{
TempProject project{fs::path(u8"Kanka şövalye oyunu")};
project.write(fs::path(u8"oyun/ana şey.zs"),"");
project.toml("[game]\nmain = \"oyun/ana şey.zs\"\n[diagnostics]\nfile = \"günlük/zodiac.log\"\n");
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(fs::is_regular_file(result->main_script));
CHECK(result->root.filename()==fs::path(u8"Kanka şövalye oyunu"));
CHECK(result->diagnostics.file==result->root/fs::path(u8"günlük")/"zodiac.log");
}

TEST_CASE("missing zodiac.toml")
{
TempProject project{fs::path(u8"Kanka şövalye oyunu")};
CHECK(error_of(project)=="zodiac.toml: error: no zodiac.toml in \""+zodiac::path_to_utf8(project.dir())+"\"");
}

TEST_CASE("toml syntax error reports line and column")
{
TempProject project;
project.toml("[game]\nmain = \n");
const std::string message=error_of(project);
CHECK(message.starts_with("zodiac.toml:2:"));
CHECK(message.find(": error: ")!=std::string::npos);
CHECK(message.find('\n')==std::string::npos);
}

TEST_CASE("main is required")
{
TempProject project;
project.toml("[modules]\nstatic = []\n");
CHECK(error_of(project)=="zodiac.toml: error: [game] main is required");
project.toml("[game]\nname = \"x\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [game] main is required");
}

TEST_CASE("type errors name the key and the expected type")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = 5\n");
CHECK(error_of(project)=="zodiac.toml: error: [game] main must be a string");
project.toml("[game]\nmain = \"main.zs\"\n[modules]\nstatic = \"x\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [modules] static must be an array of strings");
project.toml("[game]\nmain = \"main.zs\"\n[modules]\ndynamic = [1]\n");
CHECK(error_of(project)=="zodiac.toml: error: [modules] dynamic must be an array of strings");
project.toml("[game]\nmain = \"main.zs\"\n[core]\nexclude = \"window\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [core] exclude must be an array of strings");
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nfile = 3\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] file must be a string");
project.toml("game = 1\n");
CHECK(error_of(project)=="zodiac.toml: error: [game] must be a table");
}

TEST_CASE("module listed as both static and dynamic")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[modules]\nstatic = [\"example\"]\ndynamic = [\"example\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: module \"example\" is listed as both static and dynamic");
}

TEST_CASE("invalid module names")
{
TempProject project;
project.main_script();
for(const std::string_view name:{"Example","my-mod","","a b","../x"})
{
project.toml("[game]\nmain = \"main.zs\"\n[modules]\nstatic = [\""+std::string(name)+"\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: module name \""+std::string(name)+"\" is invalid");
}
}

TEST_CASE("unknown core exclude entry")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[core]\nexclude = [\"window\", \"audio\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: unknown [core] exclude entry \"audio\"; valid: window, keyboard");
}

TEST_CASE("invalid diagnostics values")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nconsole = \"loud\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] console must be \"auto\", \"always\" or \"never\"");
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\ndialog = true\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] dialog must be \"auto\", \"always\" or \"never\"");
}

TEST_CASE("main script must exist")
{
TempProject project;
project.toml(minimal);
CHECK(error_of(project)=="zodiac.toml: error: main script \"main.zs\" does not exist");
fs::create_directories(project.dir()/"main.zs");
CHECK(error_of(project)=="zodiac.toml: error: main script \"main.zs\" does not exist");
}

TEST_CASE("main script must stay inside the project")
{
TempProject project;
project.main_script();
const std::u8string outside_u8=(project.dir().parent_path()/"x.zs").generic_u8string();
const std::string outside(reinterpret_cast<const char*>(outside_u8.data()),outside_u8.size());
for(const std::string& path:{std::string("../x.zs"),std::string("sub/../../x.zs"),std::string("/x.zs"),std::string("C:/x.zs"),std::string("."),outside})
{
project.toml("[game]\nmain = \""+path+"\"\n");
CHECK(error_of(project)=="zodiac.toml: error: main script \""+path+"\" must be inside the project");
}
}

TEST_CASE("diagnostics file must stay inside the project")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nfile = \"../zodiac.log\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] file \"../zodiac.log\" must be inside the project");
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nfile = \"/zodiac.log\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] file \"/zodiac.log\" must be inside the project");
}
