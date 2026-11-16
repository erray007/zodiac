// Test fixture (Windows only, GUI subsystem like a release executable): calls attach_parent_console and setup_utf8_console as wWinMain does,
// then writes through both output paths, stdio (as modules do) and write_stdout_line/write_stderr_line (as ConsoleSink does), and exits with 3.
// test_process runs it with redirected (piped) streams and expects every line: attaching must keep streams the parent redirected.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/console.hpp>
#include <windows.h>
#include <cstdio>

int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
zodiac::platform::attach_parent_console();
zodiac::platform::setup_utf8_console();
std::fputs("stdio out\n",stdout);
std::fflush(stdout);
zodiac::platform::write_stdout_line("line out");
std::fputs("stdio err\n",stderr);
std::fflush(stderr);
zodiac::platform::write_stderr_line("line err");
return 3;
}
