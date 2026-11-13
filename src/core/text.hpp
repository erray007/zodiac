// Text helpers shared by user-facing messages.
#pragma once
#include <string>
#include <string_view>

namespace zodiac {
// Returns text with every byte below 0x20 and 0x7F escaped so it can be echoed inside a one-line message: \n, \r and \t as those two characters, the rest as \xNN (lowercase hex).
// Backslashes and bytes of UTF-8 multibyte sequences (0x80 and above) are left unchanged.
std::string one_line(std::string_view text);
}
