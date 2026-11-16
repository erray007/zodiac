// Locates the folders of the Zodiac kit relative to the running executable, so the same code works in the build tree
// (build/<preset>/bin/zodiac.exe) and in the installed kit (<kit>/bin/zodiac.exe).
#pragma once
#include <filesystem>

namespace zodiac {
struct KitLayout {
std::filesystem::path root;
std::filesystem::path bin;
std::filesystem::path modules;
std::filesystem::path static_modules;
std::filesystem::path release_lib;
};
// root is two levels above the executable; the other folders are fixed names below it:
// bin, modules, modules/static and lib/release. Pure path arithmetic, nothing is touched on disk.
KitLayout kit_layout(const std::filesystem::path& executable);
}
