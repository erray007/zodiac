#include "script_fixture.hpp"
#include <core/dynamic_library.hpp>
#include <core/module_registry.hpp>
#include <core/path_utf8.hpp>
#include <platform/console.hpp>
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using zodiac_test::ScriptFixture;

namespace {
// Runs before doctest starts so a failing load can never raise a Windows modal dialog.
const int g_dialogs_disabled=(zodiac::platform::disable_error_dialogs(),0);
std::filesystem::path from_utf8(std::string_view text)
{
return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
std::filesystem::path module_dir(){return from_utf8(ZODIAC_TEST_MODULE_DIR);}
std::filesystem::path fixture_dir(){return from_utf8(ZODIAC_TEST_FIXTURE_DIR);}
zodiac::ModuleSearchPaths search()
{
return zodiac::ModuleSearchPaths{{fixture_dir(),module_dir()}};
}
std::string expected_search_list()
{
return zodiac::path_to_utf8(fixture_dir())+"; "+zodiac::path_to_utf8(module_dir());
}
std::string load_error(const std::vector<std::string>& names)
{
zodiac::ModuleRegistry registry;
const auto result=registry.load(names,search());
REQUIRE_FALSE(result.has_value());
return result.error();
}
std::vector<std::string> module_names(const zodiac::ModuleRegistry& registry)
{
std::vector<std::string> names;
for(const zodiac_module_info* info:registry.ordered())names.emplace_back(info->name);
return names;
}
std::vector<std::string> log_messages(const ScriptFixture& fixture)
{
std::vector<std::string> messages;
for(const zodiac_test::LogEntry& entry:fixture.logs)messages.push_back(entry.message);
return messages;
}
std::string reason(const std::expected<void,std::string>& result)
{
return result?std::string():result.error();
}
bool contains(const std::vector<std::string>& list,std::string_view text)
{
return std::find(list.begin(),list.end(),text)!=list.end();
}
}

TEST_CASE("DynamicLibrary reports a one-line reason for a missing file")
{
const auto result=zodiac::DynamicLibrary::open(fixture_dir()/"no_such_file_at_all");
REQUIRE_FALSE(result.has_value());
CHECK_FALSE(result.error().empty());
CHECK(result.error().find('\n')==std::string::npos);
CHECK(result.error().find('\r')==std::string::npos);
}

TEST_CASE("invalid module names are rejected before any file lookup")
{
CHECK(load_error({"Bad-Name"})=="module name \"Bad-Name\" is invalid");
CHECK(load_error({"../evil"})=="module name \"../evil\" is invalid");
CHECK(load_error({""})=="module name \"\" is invalid");
}

TEST_CASE("missing module names every searched directory")
{
CHECK(load_error({"nope"})=="module \"nope\" not found in: "+expected_search_list());
}

TEST_CASE("corrupt file could not be loaded")
{
const std::string error=load_error({"corrupt"});
const std::string prefix="module \"corrupt\" could not be loaded: ";
REQUIRE(error.size()>prefix.size());
CHECK(error.compare(0,prefix.size(),prefix)==0);
CHECK(error.find('\n')==std::string::npos);
}

TEST_CASE("library without the entry symbol")
{
CHECK(load_error({"no_entry"})=="module \"no_entry\" has no zodiac_module_get_info entry");
}

TEST_CASE("module needing a newer ABI is refused")
{
CHECK(load_error({"needs_abi_99"})=="module \"needs_abi_99\" needs ABI 99 but this Zodiac provides 1");
}

TEST_CASE("module reporting another name is refused")
{
CHECK(load_error({"wrong_name"})=="module \"wrong_name\" reports name \"other\"");
}

TEST_CASE("dependency cycle is reported")
{
CHECK(load_error({"cycle_a"})=="module dependency cycle: cycle_a -> cycle_b -> cycle_a");
}

TEST_CASE("a failed load leaves nothing behind")
{
zodiac::ModuleRegistry registry;
CHECK_FALSE(registry.load(std::vector<std::string>{"example","nope"},search()).has_value());
CHECK(registry.ordered().empty());
}

TEST_CASE("example loads and registers")
{
zodiac::ModuleRegistry registry;
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"example"},search()).has_value());
REQUIRE(registry.ordered().size()==1);
CHECK(std::string_view(registry.ordered()[0]->license)=="Zlib");
const auto registered=registry.register_all(fixture.host);
INFO(reason(registered));
REQUIRE(registered.has_value());
CHECK(fixture.host.current_module()=="test");
REQUIRE(fixture.run("int main(){return example_add(2,3);}","int main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDWord()==5);
fixture.release_context();
REQUIRE(fixture.run("string main(){return example_echo(\"h\\xC3\\xA9llo\");}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="h\xC3\xA9llo");
fixture.release_context();
CHECK(fixture.run("void main(){example_print(\"zodiac example\");}","void main()")==asEXECUTION_FINISHED);
}

TEST_CASE("dependencies register before dependents and shut down in reverse")
{
zodiac::ModuleRegistry registry;
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"dep_grandchild","dep_child","example"},search()).has_value());
CHECK(module_names(registry)==std::vector<std::string>{"example","dep_child","dep_grandchild"});
REQUIRE(registry.register_all(fixture.host).has_value());
CHECK(log_messages(fixture)==std::vector<std::string>{"dep_child register","dep_grandchild register"});
registry.shutdown_all();
CHECK(log_messages(fixture)==std::vector<std::string>{"dep_child register","dep_grandchild register","dep_grandchild shutdown","dep_child shutdown"});
CHECK(registry.ordered().size()==3);
}

TEST_CASE("a module's own logged error is forwarded but does not fail it")
{
zodiac::ModuleRegistry registry;
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"logs_error"},search()).has_value());
const auto result=registry.register_all(fixture.host);
INFO(reason(result));
REQUIRE(result.has_value());
CHECK(contains(log_messages(fixture),"fixture refuses to register"));
CHECK(fixture.host.current_module()=="test");
registry.shutdown_all();
}

TEST_CASE("a registration the host rejected fails the module with the host's message")
{
zodiac::ModuleRegistry registry;
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"bad_decl"},search()).has_value());
const auto result=registry.register_all(fixture.host);
REQUIRE_FALSE(result.has_value());
const std::string prefix="module \"bad_decl\" failed to register: module bad_decl: cannot register \"this is not a declaration\"";
CHECK(result.error().compare(0,prefix.size(),prefix)==0);
CHECK(contains(log_messages(fixture),result.error().substr(std::string("module \"bad_decl\" failed to register: ").size())));
CHECK(fixture.host.current_module()=="test");
registry.shutdown_all();
}

TEST_CASE("teardown: shutdown_all while the host is alive, then engine and host, then the registry")
{
std::vector<std::string> seen;
{
zodiac::ModuleRegistry registry;
{
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"dep_child"},search()).has_value());
REQUIRE(registry.register_all(fixture.host).has_value());
registry.shutdown_all();
seen=log_messages(fixture);
}
}
CHECK(contains(seen,"dep_child shutdown"));
}

TEST_CASE("destroying the registry without shutdown_all never calls shutdown_module")
{
ScriptFixture fixture;
{
zodiac::ModuleRegistry registry;
REQUIRE(registry.load(std::vector<std::string>{"dep_child"},search()).has_value());
REQUIRE(registry.register_all(fixture.host).has_value());
}
CHECK_FALSE(contains(log_messages(fixture),"dep_child shutdown"));
}

TEST_CASE("a failing return code names the module and the code, and the module is not shut down")
{
zodiac::ModuleRegistry registry;
ScriptFixture fixture;
REQUIRE(registry.load(std::vector<std::string>{"example","fails_code"},search()).has_value());
const auto result=registry.register_all(fixture.host);
REQUIRE_FALSE(result.has_value());
CHECK(result.error()=="module \"fails_code\" failed to register: error code -2");
registry.shutdown_all();
CHECK_FALSE(contains(log_messages(fixture),"fails_code shutdown"));
}

TEST_CASE("modules load from a directory with non-ASCII characters")
{
struct Cleanup{
std::filesystem::path path;
~Cleanup(){std::error_code error;std::filesystem::remove_all(path,error);}
};
const Cleanup cleanup{std::filesystem::temp_directory_path()/std::filesystem::path(u8"zodiac-mödül-ğşı")};
std::filesystem::create_directories(cleanup.path);
const std::filesystem::path file=std::filesystem::path("zodiac_mod_example" ZODIAC_TEST_MODULE_SUFFIX);
std::error_code error;
std::filesystem::copy_file(module_dir()/file,cleanup.path/file,std::filesystem::copy_options::overwrite_existing,error);
REQUIRE_FALSE(error);
zodiac::ModuleRegistry registry;
const auto result=registry.load(std::vector<std::string>{"example"},zodiac::ModuleSearchPaths{{cleanup.path}});
INFO(reason(result));
REQUIRE(result.has_value());
CHECK(module_names(registry)==std::vector<std::string>{"example"});
}

TEST_CASE("default search paths are the module and plugin directories")
{
const auto paths=zodiac::ModuleSearchPaths::defaults("/app/bin","/game");
REQUIRE(paths.dirs.size()==2);
CHECK(paths.dirs[0]==std::filesystem::path("/app/modules"));
CHECK(paths.dirs[1]==std::filesystem::path("/game/plugins"));
}
