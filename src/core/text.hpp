// Text helpers shared by user-facing messages.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace zodiac {
// Returns text with every byte below 0x20 and 0x7F escaped so it can be echoed inside a one-line message: \n, \r and \t as those two characters, the rest as \xNN (lowercase hex).
// Backslashes and bytes of UTF-8 multibyte sequences (0x80 and above) are left unchanged.
std::string one_line(std::string_view text);
// True when one_line(text) would return text unchanged, so callers can skip the copy.
bool is_one_line(std::string_view text) noexcept;
// Matches a project-relative path against a glob pattern, both with '/' separators and compared case-sensitively byte by byte.
// "*" matches any run of characters inside one segment, "?" exactly one character of a segment (a whole UTF-8 sequence, not one byte), and a "**" segment any number of whole segments (including none).
// Uses no regular expressions and never allocates.
bool glob_match(std::string_view pattern,std::string_view path) noexcept;
// True when path's extension (as std::filesystem::path::extension gives it) equals extension (".zs"), compared ASCII
// case-insensitively: Windows file names ignore case, so "MAIN.ZS" is a script too. extension must be ASCII.
bool has_extension(const std::filesystem::path& path,std::string_view extension);
// True when a and b are spelled alike as the file system compares names: on Windows their native text compares equal ordinally ignoring case
// (CompareStringOrdinal, the file system's own upper-case table), elsewhere byte for byte. Pure text: nothing is touched on disk, and
// separators or "." / ".." segments are not normalized, so callers pass paths built the same way (both absolute and normalized, or two names).
bool same_path_text(const std::filesystem::path& a,const std::filesystem::path& b) noexcept;
// The system's description of code as UTF-8 without trailing line breaks, spaces or full stop, for use after "...: " in a message
// ("error N" when the system has none). On Windows a std::system_category code (a Win32 error, as std::filesystem and
// GetLastError report them) is described by FormatMessageW in the user's language and converted to UTF-8, never through
// the ANSI code page; other categories use code.message().
std::string error_text(const std::error_code& code);
}
