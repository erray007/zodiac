#include <doctest/doctest.h>
#include <platform/process.hpp>
#include <string>
#include <vector>

TEST_CASE("run_process passes arguments verbatim and captures output")
{
const std::vector<std::string> argv{ZODIAC_TEST_ECHO_ARGS,"a b","q\"uote","back\\slash\\","şehir",""};
auto result=zodiac::platform::run_process(argv);
REQUIRE(result);
CHECK(result->exit_code==5);
CHECK(result->output.find("a b\n")!=std::string::npos);
CHECK(result->output.find("q\"uote\n")!=std::string::npos);
CHECK(result->output.find("back\\slash\\\n")!=std::string::npos);
CHECK(result->output.find("şehir\n")!=std::string::npos);
CHECK(result->output.find("err")!=std::string::npos);
CHECK(result->output.find('\r')==std::string::npos);
}

#ifdef ZODIAC_TEST_ATTACH_PROBE
TEST_CASE("a GUI-subsystem program keeps redirected streams after attach_parent_console")
{
const std::vector<std::string> argv{ZODIAC_TEST_ATTACH_PROBE};
auto result=zodiac::platform::run_process(argv);
REQUIRE(result);
CHECK(result->exit_code==3);
CHECK(result->output.find("stdio out\n")!=std::string::npos);
CHECK(result->output.find("line out\n")!=std::string::npos);
CHECK(result->output.find("stdio err\n")!=std::string::npos);
CHECK(result->output.find("line err\n")!=std::string::npos);
}
#endif

TEST_CASE("run_process reports a missing program")
{
#ifdef _WIN32
const std::vector<std::string> argv{"C:/does/not/exist.exe"};
#else
const std::vector<std::string> argv{"/does/not/exist"};
#endif
CHECK_FALSE(zodiac::platform::run_process(argv));
}

TEST_CASE("run_process rejects an empty argument list")
{
CHECK_FALSE(zodiac::platform::run_process({}));
}
