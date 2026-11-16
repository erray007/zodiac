// Reads the names of the DLLs a Windows x64 PE32+ image imports, so zodiac build can ship the third-party DLLs a dynamic module needs.
// Plain byte parsing, the same on every host; nothing is loaded or run.
#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac {
// Why pe_imported_dlls_in refused an image.
enum class PeError{
// No PE file, or a damaged one.
invalid,
// A readable PE file for another machine: a PE32 (32-bit) optional header, or a Machine field other than x64 (0x8664).
not_x64,
};
// The DLL names image (the bytes of a whole x64 PE32+ file) imports: the import directory's names first, then the delay-import directory's, each in
// table order, a name that equals an earlier one ASCII case-insensitively left out. A missing or empty directory adds nothing.
// PeError::not_x64 once the DOS header, the PE signature, the file header and the optional header's magic are inside image and either the magic
// is 0x10B (PE32) or the Machine field is not 0x8664; nothing else is checked then.
// PeError::invalid otherwise when image is no valid PE32+ file: every header, the section table, each section's raw data, every structure an
// RVA leads to and every name (NUL-terminated inside the section it starts in, 1 to 255 printable ASCII characters) must lie inside image, and
// each descriptor array must end (an import descriptor with Name or FirstThunk 0, a delay descriptor with a zero name) inside its section,
// after at most 65536 entries.
// An RVA maps through the section whose [VirtualAddress, VirtualAddress+min(SizeOfRawData,VirtualSize or SizeOfRawData when 0)) holds it;
// those ranges must not overlap or pass 4 GiB.
// A delay descriptor's name is read as an RVA whatever its attributes say: a PE32+ image has no room for 32-bit virtual addresses, and
// linkers that leave the RVA bit clear still write RVAs. The data directories' sizes are not checked.
// Never throws but std::bad_alloc.
std::expected<std::vector<std::string>,PeError> pe_imported_dlls_in(std::string_view image);
// pe_imported_dlls_in on the whole of file. Errors, as one line (paths as UTF-8): `error: cannot read "<file>"` when the file cannot be read,
// `error: "<file>" is not a 64-bit (x64) Windows DLL` for PeError::not_x64, and `error: "<file>" is not a valid Windows DLL` for
// PeError::invalid (also for a file over 4 GiB, which is not read).
std::expected<std::vector<std::string>,std::string> pe_imported_dlls(const std::filesystem::path& file);
}
