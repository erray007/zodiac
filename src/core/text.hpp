// Text helpers shared by user-facing messages.
#pragma once
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
// "*" matches any run of characters inside one segment, "?" exactly one character of a segment, and a "**" segment any number of whole segments (including none).
// Uses no regular expressions and never allocates.
bool glob_match(std::string_view pattern,std::string_view path) noexcept;
// The system's description of code without trailing line breaks, spaces or full stop, for use after "...: " in a message.
std::string error_text(const std::error_code& code);
}
