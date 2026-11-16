// zodiac::load_project reads and validates <dir>/zodiac.toml: the main script, the modules a game uses, core parts to exclude and diagnostics output.
// toml++ is only included by project_file.cpp, so it never leaks into other translation units. Every error is one plain-text line starting with "zodiac.toml".
#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace zodiac {
enum class OutputMode{automatic,always,never};
struct DiagnosticsConfig{
OutputMode console=OutputMode::automatic;
// Absolute (project root / configured file), or empty when no log file is configured.
std::filesystem::path file;
OutputMode dialog=OutputMode::automatic;
};
struct Project{
// Absolute, normalized project directory.
std::filesystem::path root;
// Absolute, normalized, inside root, and an existing regular file.
std::filesystem::path main_script;
// Names matching [a-z0-9_]+, without duplicates, in file order. "core" is dropped.
std::vector<std::string> static_modules;
std::vector<std::string> dynamic_modules;
// Entries of [core] exclude: "window" or "keyboard", without duplicates.
std::vector<std::string> core_exclude;
DiagnosticsConfig diagnostics;
};
std::expected<Project,std::string> load_project(const std::filesystem::path& dir);
}
