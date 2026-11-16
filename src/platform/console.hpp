// Process-level console and error-dialog setup. Implemented per platform in src/platform/<os>/console.cpp.
#pragma once
#include <string_view>

namespace zodiac::platform {
// Stops the operating system from showing modal error dialogs (for example when a library fails to load). Call before loading anything.
void disable_error_dialogs() noexcept;
// For a GUI-subsystem program (the release executable): attaches to the console of the process that started it, when there is one,
// and points every standard stream at it whose C runtime stream (stdin, stdout, stderr) has no usable handle: the stream is reopened on the console
// and its handle also becomes the Win32 standard handle, so stdio (as modules use it) and write_stdout_line/write_stderr_line both reach the console.
// A stream the parent redirected (file or pipe) is kept, and published as the Win32 standard handle if that one is unusable.
// Does nothing when there is no parent console, and on non-Windows platforms. Call before setup_utf8_console.
void attach_parent_console() noexcept;
// Makes the console accept and print UTF-8.
void setup_utf8_console() noexcept;
// Writes line (UTF-8, without terminator) plus a newline to the process's standard error in one write. A console gets the text as Unicode,
// a redirected stream (file or pipe) gets the UTF-8 bytes unchanged. Failures are ignored. Callers serialize concurrent calls.
void write_stderr_line(std::string_view line) noexcept;
// Same as write_stderr_line, for the process's standard output (normal program output such as help or listings).
void write_stdout_line(std::string_view line) noexcept;
}
