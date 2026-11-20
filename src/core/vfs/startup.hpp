// The start-up mounts every game gets, shared by zodiac run, zodiac build's compile and the release stub (and Task 10's mount_release).
//
// mount_game_folders(vfs, root, game_name, host_paths, dev_mode): sets host paths and dev mode, mounts root read-write at "/" and
// <user_data_folder()>/<game_name> read-write at "data:/". The data folder is created by the first write into data:/ (a file or a
// folder), never at start-up, so a game that saves nothing leaves nothing behind; until then data:/ reads as an empty folder.
// When there is no per-user data folder, or the game name cannot be a folder name (valid_name), data:/ is not mounted and the log
// gets one warning: `warning: data:/ is not available: <reason>`.
// mount_project(vfs, project, dev_mode) calls it with the project root, [files] host_paths and the game name.
#pragma once
#include <core/vfs/vfs.hpp>
#include <filesystem>
#include <string_view>

namespace zodiac {
struct Project;
}

namespace zodiac::vfs {
void mount_game_folders(Vfs& vfs,const std::filesystem::path& root,std::string_view game_name,bool host_paths,bool dev_mode);
void mount_project(Vfs& vfs,const Project& project,bool dev_mode);
}
