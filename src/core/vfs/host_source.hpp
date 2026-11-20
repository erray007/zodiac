// zodiac::vfs::HostSource: the host's own file system for "host:" paths (only parsed when [files] host_paths = true). Its relative
// paths are absolute host paths in generic UTF-8 form (VfsPath::relative of a host path), opened exactly as given: no case cache,
// no case-insensitive fallback (the host's file system decides), writable, never mounted (the Vfs routes host paths to it).
#pragma once
#include <core/vfs/vfs.hpp>
#include <expected>
#include <memory>
#include <string_view>
#include <vector>

namespace zodiac::vfs {
class HostSource final:public Source{
public:
std::expected<EntryInfo,FileError> stat(std::string_view relative) override;
std::expected<std::shared_ptr<Reader>,FileError> open_read(std::string_view relative) override;
std::expected<std::unique_ptr<WriteTarget>,FileError> open_write(std::string_view relative,WriteMode mode) override;
std::expected<void,FileError> list(std::string_view relative,std::vector<EntryInfo>& out) override;
std::expected<void,FileError> remove(std::string_view relative) override;
std::expected<void,FileError> make_directory(std::string_view relative) override;
std::expected<void,FileError> remove_directory(std::string_view relative,bool recursive) override;
// platform::replace_file; false (copy instead) between volumes.
std::expected<bool,FileError> rename(std::string_view from,std::string_view to) override;
bool read_only() const noexcept override{return false;}
// make_directory with every missing folder above it.
std::expected<void,FileError> make_directories(std::string_view relative);
};
}
