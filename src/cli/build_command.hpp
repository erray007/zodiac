// zodiac::cli::build_project is "zodiac build": it turns a project into a release folder holding <name>.exe, its dynamic module DLLs and its assets.
//
// Steps (release-and-kit spec 8.2), each failure reported as one line through Diagnostics:
// 1. With config, the project is load_project_file(config), and a target (a .zs script) becomes its main script through set_main_script (a missing
//    script: "error: script "<target>" does not exist"). Without config, target is a project folder or a script file; a .zs target must exist
//    ("error: script "<target>" does not exist"), and a script file belongs to the project find_project_root finds (whose own main stays the main
//    script). A .zs script with no zodiac.toml in its folder or above builds alone, as zodiac run runs it (load_project_for_script: root its folder,
//    name its stem, every installed module of module_dirs), after the warning "warning: no zodiac.toml; every installed module was linked in
//    (create a project file to choose modules)". Each of those modules is linked statically when its link modes allow it and step 2 finds its
//    static library, otherwise dynamically when its link modes allow that (a module allowing neither is checked as a static one in step 2).
//    A folder (or another file) with no zodiac.toml: "error: zodiac.toml not found; create a project with zodiac new" (exit 2). Project file errors exit 2.
// 2. The modules load as in zodiac run (module_dirs, then <root>/plugins). Before any registers, every static module is checked:
//    a module whose link modes lack ZODIAC_LINK_STATIC is refused ("error: module "<name>" (<license>) cannot be linked statically; list it under dynamic");
//    a license outside Zlib MIT BSD-2-Clause BSD-3-Clause Apache-2.0 BSL-1.0 ISC Unlicense CC0-1.0 draws
//    "warning: module "<name>" is linked statically under license <license>; check its terms"; the library is the kit's modules/static/zodiac_mod_<name>.a,
//    else <root>/plugins/static/zodiac_mod_<name>.a, whose zodiac_mod_<name>.stamp must equal the kit's toolchain.txt
//    ("error: plugin "<name>" was not built with this kit's toolchain; list it under dynamic"). Every static module is checked before the build stops (exit 2).
// 3. The scripts compile exactly as in zodiac run (errors exit 1), in the project root as working directory, and the bytecode is saved
//    (without debug info when [build] strip_debug_info is set).
// 4. The payload (kit_version ZODIAC_VERSION; requested: static then dynamic names as the runtime loads them; modules: registration order with versions)
//    is written as <output>/<name>.zodpay.obj and linked by link_release to <output>/<name>.exe.tmp.
// 5. With [build.sign] command, the command runs on <name>.exe.tmp with {file} replaced by that temporary path; the program is an absolute path,
//    a path relative to the project root, or a name searched on PATH. Every dynamic module (a registered module not listed as static, so also an
//    undeclared dependency) is then copied from the folder it was loaded from to <output>/<dll>.tmp and signed the same way, followed by its
//    third-party DLLs (spec item 7): each name pe_imported_dlls (core/pe_imports.hpp) reads from the module DLL, and recursively from each DLL
//    found this way, that names a file in the module DLL's folder (compared as the file system does; a name without a dot gets ".dll") is
//    copied and signed under that file's own name, breadth first, each file once per build (a name already shipped, by any module, is skipped).
//    A name not found there is skipped when it is a system DLL (it starts with "api-ms-win-" or "ext-ms-", ASCII case-insensitively, or
//    SearchPathW finds it in GetSystemDirectoryW); otherwise "error: module "<name>" needs "<dll>", which is not next to "<module dll path>""
//    (exit 2), and a DLL pe_imported_dlls refuses stops the build with its error (exit 2). A failed signing:
//    "error: signing "<final file>" failed" (the .exe or .dll path the file is built for) followed by the tool's output, each line indented by
//    two spaces (exit 2).
// 6. <name>.exe.tmp is renamed over <output>/<name>.exe, then each <dll>.tmp over its DLL. A rename that fails because another process holds the
//    old file open (a sharing or lock violation): "error: cannot replace "<file>": it is in use (is the game still running?)"; any other failure
//    (a read-only file, a folder of that name): "error: cannot replace "<file>": <system reason>" (exit 2). A build that fails up to and including
//    the executable's replace (a link, copy or signing failure included) leaves the old executable and DLLs untouched, and no temporary file
//    (.tmp, .obj, .rsp) is left behind, whatever happened.
// 7. The expanded [build] assets are copied keeping their project-relative paths, except files inside the output folder (compared
//    case-insensitively, as Windows does). A failure from the DLL renames on leaves the new executable in place, with the files replaced
//    before the failure (each copy replaces its file whole) and the rest as an earlier build left them. Stale files of earlier builds in the
//    output folder are never removed.
// Exit codes: 0 success, 1 script error, 2 project, module, link or file error. On hosts other than Windows the only result is
// "error: release builds are not supported on this host yet" (exit 2).
// Result (spec 8.2a): every message goes to the console as it happens, and the last line on standard output is the summary ResultReport::summary
// (verb build) gives. Without no_window, when platform::has_interactive_desktop() holds, the result window (platform/result_window.hpp) then
// opens, titled "Zodiac build - <game name>" (when no project loaded, the target's own name, or the config file's folder without a target),
// labeled "Build output", holding ResultReport::text, with "Open output folder" for the executable's folder after a success, and, when a lone
// script was built (step 1, success or failure), "Create project file": create_project_file (cli/build_report.hpp) for the script's folder with
// the game name, the script's file name and the static and dynamic module lists step 1 chose. build_project returns once the window is closed.
// Without a desktop it ends as a --no-window build does.
// Threads: main thread only; changes the working directory.
#pragma once
#include <core/diagnostics.hpp>
#include <filesystem>
#include <vector>

namespace zodiac::cli {
struct BuildOptions{
// Empty: the current folder, or with config no script override.
std::filesystem::path target;
// --config: the project file, absolute; empty when not given. The caller refuses a project folder target beside it.
std::filesystem::path config;
// Empty: <project root>/release.
std::filesystem::path output;
// The module search folders, absolute, in search order (zodiac run's: --module-dir folders, then the kit's modules folder).
std::vector<std::filesystem::path> module_dirs;
// --no-window: report on the console only, never open the result window.
bool no_window=false;
};
int build_project(const BuildOptions& options,Diagnostics& diagnostics);
}
