// zodiac::release::start is the start-up path of a release executable: the game payload linked into it becomes a running game.
//
// start() parses payload_bytes (see core/payload.hpp) and sets up diagnostics from it: a console sink unless the payload's console mode is never,
// and a file sink when it names a log file (relative to the executable's folder). The game folder (project root) is executable.parent_path():
// the main script section is resolved against it, dynamic modules are loaded from it and nowhere else, and it becomes the working directory.
// Before any module registers, every module the payload lists must be present (static: linked into the executable; dynamic: its shared library
// in the game folder) with the version and in the order stored in the payload. Then the bytecode is loaded and main() runs.
// Returns the exit code under the same rules as zodiac run: 0-255 from main() (anything else is 1 with a warning), 1 for a script error,
// 2 when the payload is damaged or the modules do not match: "error: module "<name>" is missing",
// "error: module "<name>" is version <found>, but the game was built with <expected>",
// "error: the modules were registered in a different order than when the game was built".
// Never throws and never writes to the console itself; every message goes through Diagnostics as one line. Call it once, on the main thread.
//
// start_linked() takes the linked payload section as written by write_payload_object (core/coff_object.hpp): [begin, end) holds a u64
// little-endian size followed by the payload. The stored size is never trusted: unless it equals end - begin - 8 it reports
// "error: the game data is damaged (size mismatch)" on the console and returns 2 without reading the payload; otherwise it calls start().
// The Windows entry point (src/release/win_main.cpp) calls it with the symbols zodiac_payload and zodiac_payload_end, which only the object
// zodiac build writes defines, and the running executable's path.
#pragma once
#include <filesystem>
#include <string_view>

namespace zodiac::release {
int start(std::string_view payload_bytes,const std::filesystem::path& executable);
int start_linked(const unsigned char* begin,const unsigned char* end,const std::filesystem::path& executable);
}
