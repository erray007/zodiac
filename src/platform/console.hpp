// Process-level console and error-dialog setup. Implemented per platform in src/platform/<os>/console.cpp.
#pragma once
#include <string_view>

namespace zodiac::platform {
// Stops the operating system from showing modal error dialogs (for example when a library fails to load). Call before loading anything.
void disable_error_dialogs() noexcept;
// Makes the console accept and print UTF-8.
void setup_utf8_console() noexcept;
// Writes line (UTF-8, without terminator) plus a newline to the process's standard error in one write. A console gets the text as Unicode,
// a redirected stream (file or pipe) gets the UTF-8 bytes unchanged. Failures are ignored. Callers serialize concurrent calls.
void write_stderr_line(std::string_view line) noexcept;
// Same as write_stderr_line, for the process's standard output (normal program output such as help or listings).
void write_stdout_line(std::string_view line) noexcept;
}
