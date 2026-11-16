// zodiac::cli::link_release links a release executable with the kit's own linker (bin/ld.lld.exe), the kit's CRT subset and the link line
// captured from llvm-mingw when the kit was built (lib/release/link.txt, see cmake/CaptureReleaseLink.cmake), so no compiler is needed.
//
// link.txt holds one ld.lld argument per line. link_release expands it: {release_lib} becomes kit.release_lib, a line {output} becomes
// request.output, and the line {inputs} becomes "-u <symbol>" for every forced symbol followed by every input in request order. Each line must
// appear exactly once ({inputs}, {output}). All arguments go into a UTF-8 response file <output>.rsp next to the output, one per line, every path
// with '/' separators and every argument quoted with the Windows command line rules, which lld's MinGW driver uses for response files on a Windows host
// (and which agree with the GNU rules lld uses to find -m in them, because no argument then contains a backslash). The linker runs as
// "bin/ld.lld.exe @<output>.rsp" through platform::run_process, and the response file is removed afterwards whatever happened.
//
// Every message goes through diagnostics as one line, and link_release reports each failure itself; the returned error is the first line it reported
// (callers must not report it again):
// - "error: cannot read the release link line "<link.txt>"" / "error: the release link line "<link.txt>" is damaged" (an expected line missing or repeated);
// - "error: cannot write "<rsp>"", "error: cannot run the linker: <reason>";
// - "error: linking failed" (ld.lld exited non-zero) followed by one line per non-empty line of the linker's output, indented by two spaces
//   (continuation lines, so a build counts the failure as one error).
// A successful link reports each line of linker output as a warning. On hosts other than Windows the only result is
// "error: release builds are not supported on this host yet".
// Threads: call from one thread at a time per output path.
#pragma once
#include <core/diagnostics.hpp>
#include <core/kit_layout.hpp>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace zodiac::cli {
struct LinkRequest{
std::filesystem::path output;
std::vector<std::filesystem::path> inputs;     // payload object, stub, static modules, core, angelscript, in this order
std::vector<std::string> forced_symbols;       // zodiac_static_module_<name>
};
std::expected<void,std::string> link_release(const KitLayout& kit,const LinkRequest& request,Diagnostics& diagnostics);
}
