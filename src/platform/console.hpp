// Process-level console and error-dialog setup. Implemented per platform in src/platform/<os>/console.cpp.
#pragma once

namespace zodiac::platform {
// Stops the operating system from showing modal error dialogs (for example when a library fails to load). Call before loading anything.
void disable_error_dialogs() noexcept;
// Makes the console accept and print UTF-8.
void setup_utf8_console() noexcept;
}
