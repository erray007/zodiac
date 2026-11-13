// Converts a filesystem path to UTF-8 for messages. Never goes through the ANSI code page, so non-ASCII paths survive on every platform.
#pragma once
#include <filesystem>
#include <string>

namespace zodiac {
inline std::string path_to_utf8(const std::filesystem::path& path)
{
const std::u8string text=path.u8string();
return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
}
