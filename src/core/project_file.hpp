// zodiac::load_project reads and validates <dir>/zodiac.toml: the game name and main script, the modules a game uses, core parts to exclude, diagnostics output, build settings (assets, debug info, signing) and worker scripts.
// toml++ is only included by project_file.cpp, so it never leaks into other translation units. Every error is one plain-text line starting with "zodiac.toml".
#pragma once
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zodiac {
enum class OutputMode{automatic,always,never};
struct DiagnosticsConfig{
OutputMode console=OutputMode::automatic;
// Absolute (project root / configured file), or empty when no log file is configured.
std::filesystem::path file;
OutputMode dialog=OutputMode::automatic;
bool operator==(const DiagnosticsConfig&) const=default;
};
struct BuildConfig{
// Glob patterns relative to the project root with '/' separators (see glob_match), in file order.
std::vector<std::string> assets;
bool strip_debug_info=false;
// Program and arguments run on the built executable; empty when signing is not configured. When not empty, at least one element contains {file}.
std::vector<std::string> sign_command;
};
struct Project{
// Absolute, normalized project directory.
std::filesystem::path root;
// [game] name, or the name of the root folder when absent.
std::string name;
// Absolute, normalized, inside root, and an existing regular file.
std::filesystem::path main_script;
// Names matching [a-z0-9_]+, without duplicates, in file order. "core" is dropped.
std::vector<std::string> static_modules;
std::vector<std::string> dynamic_modules;
// Entries of [core] exclude: "window" or "keyboard", without duplicates.
std::vector<std::string> core_exclude;
DiagnosticsConfig diagnostics;
BuildConfig build;
// [workers] scripts: absolute, normalized, inside root, existing .zs regular files, without duplicates, in file order.
std::vector<std::filesystem::path> workers;
};
std::expected<Project,std::string> load_project(const std::filesystem::path& dir);
// The project a script file belongs to: the nearest folder holding a regular file zodiac.toml, starting at the file's own folder and walking up to
// the filesystem root. The result is absolute and normalized (no trailing separator); nullopt when no folder qualifies. Only checks for the file, never reads it.
std::optional<std::filesystem::path> find_project_root(const std::filesystem::path& script_file);
// Every regular file under project.root matching any of project.build.assets, as absolute paths, sorted and without duplicates.
// zodiac.toml in the root and everything under <root>/release are never returned. A pattern matching nothing is not an error; a missing or unreadable directory is: the error is one line, `zodiac.toml: error: cannot read directory "<path>": <reason>`.
std::expected<std::vector<std::filesystem::path>,std::string> expand_assets(const Project& project);
}
