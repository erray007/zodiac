#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <core/text.hpp>
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
for(const std::string& path:{std::string("../x.zs"),std::string("sub/../../x.zs"),std::string("/x.zs"),std::string("."),outside})
{
project.toml("[game]\nmain = \""+path+"\"\n");
CHECK(error_of(project)=="zodiac.toml: error: main script \""+path+"\" must be inside the project");
}
#ifdef _WIN32
for(const std::string& path:{std::string("C:/x.zs"),std::string("C:x.zs")})
{
project.toml("[game]\nmain = \""+path+"\"\n");
CHECK(error_of(project)=="zodiac.toml: error: main script \""+path+"\" must be inside the project");
}
#endif
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

TEST_CASE("build section loads assets, strip_debug_info and sign command")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[build]\nassets = [\"sounds/**/*.ogg\"]\nstrip_debug_info = true\n[build.sign]\ncommand = [\"signtool\", \"sign\", \"{file}\"]\n");
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->build.assets==std::vector<std::string>{"sounds/**/*.ogg"});
CHECK(result->build.strip_debug_info);
CHECK(result->build.sign_command==std::vector<std::string>{"signtool","sign","{file}"});
}

TEST_CASE("build section defaults")
{
TempProject project;
project.main_script();
project.toml(minimal);
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->build.assets.empty());
CHECK_FALSE(result->build.strip_debug_info);
CHECK(result->build.sign_command.empty());
CHECK(result->workers.empty());
}

TEST_CASE("sign command must contain {file}")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[build.sign]\ncommand = [\"signtool\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: [build.sign] command must contain {file}");
}

TEST_CASE("build section type errors")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[build]\nassets = \"x\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [build] assets must be an array of strings");
project.toml("[game]\nmain = \"main.zs\"\n[build]\nstrip_debug_info = \"yes\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [build] strip_debug_info must be a boolean");
project.toml("[game]\nmain = \"main.zs\"\n[build.sign]\ncommand = 3\n");
CHECK(error_of(project)=="zodiac.toml: error: [build.sign] command must be an array of strings");
project.toml("build = 1\n[game]\nmain = \"main.zs\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [build] must be a table");
project.toml("[game]\nmain = \"main.zs\"\n[build]\nsign = 1\n");
CHECK(error_of(project)=="zodiac.toml: error: [build.sign] must be a table");
}

TEST_CASE("worker scripts")
{
TempProject project;
project.main_script();
project.write("bots.zs","");
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [\"bots.zs\", \"./bots.zs\"]\n");
const auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
REQUIRE(result->workers.size()==1);
CHECK(result->workers[0]==result->root/"bots.zs");
CHECK(result->workers[0].is_absolute());
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [\"missing.zs\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: worker script \"missing.zs\" does not exist");
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [\"../x.zs\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: worker script \"../x.zs\" must be inside the project");
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [5]\n");
CHECK(error_of(project)=="zodiac.toml: error: [workers] scripts must be an array of strings");
// The extension is compared ignoring case, as Windows compares file names.
project.write("Bots2.ZS","");
project.write("notes.txt","");
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [\"Bots2.ZS\"]\n");
const auto upper=zodiac::load_project(project.dir());
REQUIRE(upper.has_value());
CHECK(upper->workers==std::vector<fs::path>{upper->root/"Bots2.ZS"});
project.toml("[game]\nmain = \"main.zs\"\n[workers]\nscripts = [\"notes.txt\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: worker script \"notes.txt\" must be a .zs file");
}

TEST_CASE("project name comes from [game] name or the folder")
{
TempProject project{fs::path(u8"Kanka oyunu")};
project.main_script();
project.toml(minimal);
auto result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->name=="Kanka oyunu");
project.toml("[game]\nname = \"My Game\"\nmain = \"main.zs\"\n");
result=zodiac::load_project(project.dir());
REQUIRE(result.has_value());
CHECK(result->name=="My Game");
project.toml("[game]\nname = 4\nmain = \"main.zs\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [game] name must be a string");
}

TEST_CASE("expand_assets selects matching files under the root")
{
TempProject project;
project.main_script();
project.write("sounds/a.ogg","");
project.write("sounds/guns/b.ogg","");
project.write("sounds/c.wav","");
project.write("release/sounds/d.ogg","");
project.toml("[game]\nmain = \"main.zs\"\n[build]\nassets = [\"sounds/**/*.ogg\", \"sounds/a.ogg\"]\n");
const auto loaded=zodiac::load_project(project.dir());
REQUIRE(loaded.has_value());
const auto files=zodiac::expand_assets(*loaded);
REQUIRE(files.has_value());
CHECK(*files==std::vector<fs::path>{loaded->root/"sounds"/"a.ogg",loaded->root/"sounds"/"guns"/"b.ogg"});
}

TEST_CASE("expand_assets never returns zodiac.toml or release files and allows no matches")
{
TempProject project;
project.main_script();
project.write("release/x.txt","");
project.toml("[game]\nmain = \"main.zs\"\n[build]\nassets = [\"**\", \"nothing/*.xyz\"]\n");
const auto loaded=zodiac::load_project(project.dir());
REQUIRE(loaded.has_value());
const auto files=zodiac::expand_assets(*loaded);
REQUIRE(files.has_value());
CHECK(*files==std::vector<fs::path>{loaded->root/"main.zs"});
project.toml("[game]\nmain = \"main.zs\"\n[build]\nassets = [\"nothing/*.xyz\"]\n");
const auto none=zodiac::load_project(project.dir());
REQUIRE(none.has_value());
const auto empty=zodiac::expand_assets(*none);
REQUIRE(empty.has_value());
CHECK(empty->empty());
}

TEST_CASE("expand_assets reports an unreadable root")
{
TempProject project;
zodiac::Project missing;
missing.root=project.dir()/"does_not_exist";
missing.build.assets={"**"};
const auto result=zodiac::expand_assets(missing);
REQUIRE_FALSE(result.has_value());
CHECK(result.error().starts_with("zodiac.toml: error: cannot read directory \""+zodiac::path_to_utf8(missing.root)+"\": "));
CHECK(result.error().find('\n')==std::string::npos);
zodiac::Project not_a_directory;
project.main_script();
not_a_directory.root=project.dir()/"main.zs";
not_a_directory.build.assets={"**"};
CHECK_FALSE(zodiac::expand_assets(not_a_directory).has_value());
}

TEST_CASE("echoed user text stays on one line")
{
TempProject project;
project.main_script();
project.toml("[game]\nmain = \"main.zs\"\n[modules]\nstatic = [\"a\\nb\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: module name \"a\\nb\" is invalid");
project.toml("[game]\nmain = \"x\\ty\\u0001.zs\"\n");
CHECK(error_of(project)=="zodiac.toml: error: main script \"x\\ty\\x01.zs\" does not exist");
project.toml("[game]\nmain = \"main.zs\"\n[core]\nexclude = [\"p\\rq\"]\n");
CHECK(error_of(project)=="zodiac.toml: error: unknown [core] exclude entry \"p\\rq\"; valid: window, keyboard");
project.toml("[game]\nmain = \"main.zs\"\n[diagnostics]\nfile = \"../a\\nb\"\n");
CHECK(error_of(project)=="zodiac.toml: error: [diagnostics] file \"../a\\nb\" must be inside the project");
}

TEST_CASE("find_project_root returns the folder of the script when it holds zodiac.toml")
{
TempProject project(fs::path(u8"Kanka şövalye"));
project.main_script();
project.toml(minimal);
const auto root=zodiac::find_project_root(project.dir()/"main.zs");
REQUIRE(root.has_value());
CHECK(*root==fs::absolute(project.dir()).lexically_normal());
}

TEST_CASE("find_project_root walks up to the nearest zodiac.toml")
{
TempProject project;
project.toml(minimal);
project.write("levels/deep/er/level.zs","");
const auto root=zodiac::find_project_root(project.dir()/"levels"/"deep"/"er"/"level.zs");
REQUIRE(root.has_value());
CHECK(*root==fs::absolute(project.dir()).lexically_normal());
// A nested project is nearer than the outer one.
project.write("levels/deep/zodiac.toml",minimal);
const auto nested=zodiac::find_project_root(project.dir()/"levels"/"deep"/"er"/"level.zs");
REQUIRE(nested.has_value());
CHECK(*nested==fs::absolute(project.dir()/"levels"/"deep").lexically_normal());
}

TEST_CASE("find_project_root resolves a relative script path and ignores a folder named zodiac.toml")
{
TempProject project;
project.toml(minimal);
project.write("sub/zodiac.toml/keep.txt","");
project.write("sub/game.zs","");
const fs::path previous=fs::current_path();
fs::current_path(project.dir());
const auto root=zodiac::find_project_root(fs::path("sub")/"game.zs");
fs::current_path(previous);
REQUIRE(root.has_value());
CHECK(root->is_absolute());
CHECK(*root==fs::absolute(project.dir()).lexically_normal());
}

TEST_CASE("find_project_root finds nothing when no folder up to the filesystem root holds zodiac.toml")
{
TempProject project;
project.write("alone/game.zs","");
const fs::path base=fs::absolute(project.dir()).lexically_normal().parent_path();
const auto root=zodiac::find_project_root(project.dir()/"alone"/"game.zs");
// Nothing inside the temporary tree qualifies; a zodiac.toml above the system temp folder is outside this test's control.
if(root){
const fs::path inner=root->lexically_relative(base);
CHECK((inner.empty()||*inner.begin()==fs::path("..")));
}
}

TEST_CASE("load_project_for_script inside a project loads it and makes the file the main script")
{
TempProject project;
project.main_script();
project.write("includes/menu.zs","void menu(){}\n");
project.toml("[game]\nmain = \"main.zs\"\n[modules]\nstatic = [\"example\"]\n");
const std::vector<std::string> installed{"other"};
const auto result=zodiac::load_project_for_script(project.dir()/"includes"/"menu.zs",installed);
REQUIRE(result.has_value());
const fs::path root=fs::absolute(project.dir()).lexically_normal();
CHECK(result->root==root);
CHECK(result->main_script==root/"includes"/"menu.zs");
CHECK(result->static_modules==std::vector<std::string>{"example"});
}

TEST_CASE("load_project_for_script reports a broken project file")
{
TempProject project;
project.toml("[game\n");
project.write("menu.zs","");
const std::vector<std::string> installed;
const auto result=zodiac::load_project_for_script(project.dir()/"menu.zs",installed);
REQUIRE_FALSE(result.has_value());
CHECK(result.error().starts_with("zodiac.toml"));
}

TEST_CASE("load_project_for_script without a project uses the file folder and the installed modules sorted")
{
TempProject project(fs::path("lone"));
project.write("tek dosya.zs","");
const std::vector<std::string> installed{"zeta","example","beta"};
const auto result=zodiac::load_project_for_script(project.dir()/"tek dosya.zs",installed);
REQUIRE(result.has_value());
// A zodiac.toml above the system temp folder is outside this test's control.
if(zodiac::find_project_root(project.dir()/"tek dosya.zs"))return;
const fs::path root=fs::absolute(project.dir()).lexically_normal();
CHECK(result->root==root);
CHECK(result->main_script==root/"tek dosya.zs");
CHECK(result->name=="tek dosya");
CHECK(result->static_modules==std::vector<std::string>{"beta","example","zeta"});
CHECK(result->dynamic_modules.empty());
CHECK(result->core_exclude.empty());
CHECK(result->workers.empty());
}

