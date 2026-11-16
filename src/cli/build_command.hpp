// zodiac::cli::build_project is "zodiac build": it turns a project into a release folder holding <name>.exe, its dynamic module DLLs and its assets.
//
// Steps (release-and-kit spec 8.2), each failure reported as one line through Diagnostics:
// 1. target is a project folder or a script file; a script file belongs to the project find_project_root finds. No zodiac.toml:
//    "error: zodiac.toml not found; create a project with zodiac new" (exit 2). The project is loaded as zodiac run loads it (errors exit 2).
// 2. The modules load as in zodiac run (module_dirs, then <root>/plugins). Before any registers, every static module is checked:
//    a module whose link modes lack ZODIAC_LINK_STATIC is refused ("error: module "<name>" (<license>) cannot be linked statically; list it under dynamic");
//    a license outside Zlib MIT BSD-2-Clause BSD-3-Clause Apache-2.0 BSL-1.0 ISC Unlicense CC0-1.0 draws
//    "warning: module "<name>" is linked statically under license <license>; check its terms"; the library is the kit's modules/static/zodiac_mod_<name>.a,
//    else <root>/plugins/static/zodiac_mod_<name>.a, whose zodiac_mod_<name>.stamp must equal the kit's toolchain.txt
//    ("error: plugin "<name>" was not built with this kit's toolchain; list it under dynamic"). Every static module is checked before the build stops (exit 2).
// 3. The scripts compile exactly as in zodiac run (errors exit 1), in the project root as working directory, and the bytecode is saved
//    (without debug info when [build] strip_debug_info is set).
// 4. The payload (kit_version ZODIAC_VERSION; requested: static then dynamic names as the runtime loads them; modules: registration order with versions)
//    is written as <output>/<name>.zodpay.obj and linked by link_release to <output>/<name>.exe.tmp, which is renamed over <output>/<name>.exe.
//    A rename that fails because the old executable is open: "error: cannot replace "<exe>": it is in use (is the game still running?)" (exit 2).
//    The old executable is never touched by a failed build, and no temporary file (.tmp, .obj, .rsp) is left behind, whatever happened.
// 5. Every dynamic module (a registered module not listed as static, so also an undeclared dependency) is copied next to the executable from the
//    folder it was loaded from; the expanded [build] assets are copied keeping their project-relative paths.
// 6. With [build.sign] command, the command runs once per executable and copied DLL with {file} replaced by the file's path; the program is an absolute
//    path, a path relative to the project root, or a name searched on PATH. Failure: "error: signing "<file>" failed" followed by the tool's
//    output, each line indented by two spaces (exit 2).
// Exit codes: 0 success, 1 script error, 2 project, module, link or file error. On hosts other than Windows the only result is
// "error: release builds are not supported on this host yet" (exit 2).
// The last line on standard output is the summary BuildReport::summary gives. Threads: main thread only; changes the working directory.
#pragma once
#include <core/diagnostics.hpp>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac::cli {
struct BuildOptions{
std::filesystem::path target;
// Empty: <project root>/release.
std::filesystem::path output;
// The module search folders, absolute, in search order (zodiac run's: --module-dir folders, then the kit's modules folder).
std::vector<std::filesystem::path> module_dirs;
// Until the result window exists (plan task 9), a build without --no-window behaves as with it.
bool no_window=false;
};
// The diagnostics sink that keeps a build's tally: an error is an ERROR-level line that does not start with two spaces (continuation lines,
// such as a signing tool's output, do). summary() is the build's summary line: "Build succeeded: <exe>" (native path, UTF-8) or
// "Build failed: <N> errors" ("1 error"). Threads: write() may be called from any thread.
class BuildReport final:public DiagnosticSink{
public:
void write(zodiac_log_level level,std::string_view line) noexcept override;
std::size_t errors() const noexcept{return errors_.load(std::memory_order_relaxed);}
std::string summary(bool succeeded,const std::filesystem::path& exe) const;
private:
std::atomic<std::size_t> errors_{0};
};
int build_project(const BuildOptions& options,Diagnostics& diagnostics);
}
