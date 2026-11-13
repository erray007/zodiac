#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <platform/console.hpp>
#include <windows.h>

namespace zodiac::platform {
void disable_error_dialogs() noexcept
{
SetErrorMode(GetErrorMode()|SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
}
void setup_utf8_console() noexcept
{
SetConsoleOutputCP(CP_UTF8);
SetConsoleCP(CP_UTF8);
}
}
