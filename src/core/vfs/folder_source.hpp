// zodiac::vfs::FolderSource: a host folder as a VFS source, with case-insensitive lookup on every platform.
//
// Lookup: the exact name first (one stat or open of the whole path). On a miss the path is walked segment by segment through a
// per-folder cache of fold_name -> real names: the exact name wins, one case-insensitive match is taken, several are ambiguous
// (FileError::ambiguous; ambiguous_names tells which). The cache is guarded by a mutex and keyed by the folder's fold; it is dropped
// by this source's own writes in the folder (create, remove, make or remove a folder) and rescanned when the folder's modification
// time differs from the scanned one. A scan made within racy_window_ns of the folder's modification time is trusted for nothing
// (the next miss rescans), since a change inside one tick of the file system's clock (FAT's 2 s, a coarse kernel tick) would keep
// the time unchanged. Names that are not valid_name (they could never be asked for) are left out of the cache and of listings.
// Writes reuse an existing case-insensitive match (writing "SAVE.TXT" over "save.txt" changes save.txt, on Linux as on Windows),
// and create a new file with the name as given.
//
// With create_root (data:/), a missing root folder reads as an empty folder, and the first write (a file or a folder) creates it
// with its parents. A read-only source refuses every change with access_denied.
#pragma once
#include <core/vfs/vfs.hpp>
#include <platform/file.hpp>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zodiac::vfs {
namespace detail {
// The FileError for a failed system call (not_found, not_a_directory, is_directory, access_denied; io for anything else).
FileError file_error(const std::error_code& error) noexcept;
EntryKind entry_kind(platform::NodeKind kind) noexcept;
// Nanoseconds to milliseconds, rounded down.
std::int64_t to_ms(std::int64_t ns) noexcept;
platform::FileAccess access_of(WriteMode mode) noexcept;
std::shared_ptr<Reader> make_file_reader(platform::File file);
std::unique_ptr<WriteTarget> make_file_writer(platform::File file);
// Removes path and, for a folder, everything in it; links are removed, never followed.
std::error_code remove_tree(const std::filesystem::path& path);
// Creates path and every missing folder above it; success when it is already a folder.
std::error_code make_directories(const std::filesystem::path& path);
}
class FolderSource final:public Source{
public:
static constexpr std::int64_t racy_window_ns=3'000'000'000;
// root is an absolute folder; it is not checked here.
FolderSource(std::filesystem::path root,bool read_only,bool create_root=false);
std::expected<EntryInfo,FileError> stat(std::string_view relative) override;
std::expected<std::shared_ptr<Reader>,FileError> open_read(std::string_view relative) override;
std::expected<std::unique_ptr<WriteTarget>,FileError> open_write(std::string_view relative,WriteMode mode) override;
std::expected<void,FileError> list(std::string_view relative,std::vector<EntryInfo>& out) override;
std::expected<void,FileError> remove(std::string_view relative) override;
std::expected<void,FileError> make_directory(std::string_view relative) override;
std::expected<void,FileError> remove_directory(std::string_view relative,bool recursive) override;
bool read_only() const noexcept override{return read_only_;}
bool real_spelling(std::string_view relative,std::string& real) override;
std::optional<std::pair<std::string,std::string>> ambiguous_names(std::string_view relative) override;
const std::filesystem::path& root() const noexcept{return root_;}
private:
struct Name{
std::string fold;
std::string real;
platform::NodeKind kind=platform::NodeKind::none;
};
// One scanned folder: its real path below the root, the modification time it was scanned at, and its names sorted by fold, then real.
struct Names{
std::string real;
std::int64_t modified_ns=0;
bool racy=false;
std::vector<Name> names;
};
struct Resolved{
std::string real;
platform::NodeInfo node;
};
// What one segment matched in a folder: found (name set), missing, or ambiguous.
struct Match{
const Name* name=nullptr;
bool ambiguous=false;
};
std::filesystem::path host_path(std::string_view real) const;
// The real path and the node of relative. With leaf_may_miss, a missing last segment (below an existing folder) gives the folder's
// real path joined with the segment as given and node kind none.
std::expected<Resolved,FileError> resolve(std::string_view relative,bool leaf_may_miss);
std::expected<std::shared_ptr<const Names>,FileError> folder_names(const std::string& real_folder);
static Match match(const Names& names,std::string_view segment);
void invalidate(std::string_view real_folder);
void invalidate_tree(std::string_view real_folder);
// For create_root: creates the root folder when it is missing.
std::expected<void,FileError> ensure_root();
std::filesystem::path root_;
bool read_only_;
bool create_root_;
std::mutex cache_mutex_;
std::unordered_map<std::string,std::shared_ptr<const Names>> cache_;
};
}
