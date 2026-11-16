// Process-level facts and the program entry point. Implemented per platform in src/platform/<os>/process.cpp.
//
// The platform layer owns the real entry point (wmain on Windows, main elsewhere): it converts the arguments to UTF-8 once
// and calls zodiac::cli::run, which the command line front end defines.
#pragma once
#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace zodiac::platform {
// The absolute path of the running executable, resolved by the operating system (never taken from argv[0]). Empty when unknown.
std::filesystem::path executable_path();

struct ProcessResult {
int exit_code;
// Standard output and standard error merged in arrival order, UTF-8, with line breaks normalized to \n.
std::string output;
};
// Runs a child process to completion and captures its output. argv[0] is the absolute path of the program; the other
// arguments are UTF-8 and reach the child verbatim (no shell, no globbing, no re-quoting surprises). Implemented in
// src/platform/<os>/run_process.cpp so that it can be linked into the core library and tests.
// Returns an error message when the program cannot be started (or argv is empty); a started program that fails is a result
// with its non-zero exit_code.
std::expected<ProcessResult,std::string> run_process(std::span<const std::string> argv);
}

namespace zodiac::cli {
// args excludes the program name. Returns the process exit code.
int run(std::span<const std::string> args);
}
