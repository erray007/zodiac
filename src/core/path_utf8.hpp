// Converts a filesystem path to UTF-8 for messages. Never goes through the ANSI code page, so non-ASCII paths survive on every platform.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace zodiac {
inline std::string path_to_utf8(const std::filesystem::path& path)
{
const std::u8string text=path.u8string();
return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
// The inverse of path_to_utf8: builds a path from UTF-8 text (command line arguments) without the ANSI code page.
inline std::filesystem::path path_from_utf8(std::string_view text)
{
return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
}
