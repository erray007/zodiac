// Process-level facts and the program entry point. Implemented per platform in src/platform/<os>/process.cpp.
//
// The platform layer owns the real entry point (wmain on Windows, main elsewhere): it converts the arguments to UTF-8 once
// and calls zodiac::cli::run, which the command line front end defines.
#pragma once
#include <filesystem>
#include <span>
#include <string>

namespace zodiac::platform {
// The absolute path of the running executable, resolved by the operating system (never taken from argv[0]). Empty when unknown.
std::filesystem::path executable_path();
}

namespace zodiac::cli {
// args excludes the program name. Returns the process exit code.
int run(std::span<const std::string> args);
}
