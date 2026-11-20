#include <core/vfs/startup.hpp>
#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <core/text.hpp>
#include <core/vfs/folder_source.hpp>
#include <core/vfs/path.hpp>
#include <platform/file.hpp>
#include <memory>
#include <string>

namespace zodiac::vfs {
void mount_game_folders(Vfs& vfs,const std::filesystem::path& root,std::string_view game_name,bool host_paths,bool dev_mode)
{
vfs.set_host_paths(host_paths);
vfs.set_dev_mode(dev_mode);
vfs.mount("/",std::make_shared<FolderSource>(root,false),true,nullptr);
const std::filesystem::path data=platform::user_data_folder();
std::string reason;
if(data.empty())reason="no per-user data folder was found";
else if(!valid_name(game_name))reason="the game name \""+one_line(game_name)+"\" cannot be used as a folder name";
if(!reason.empty()){
if(const auto& log=vfs.log())log(ZODIAC_LOG_WARNING,"warning: data:/ is not available: "+reason);
return;
}
vfs.mount("data:/",std::make_shared<FolderSource>(data/path_from_utf8(game_name),false,true),true,nullptr);
}

void mount_project(Vfs& vfs,const Project& project,bool dev_mode)
{
mount_game_folders(vfs,project.root,project.name,project.files.host_paths,dev_mode);
}
}
