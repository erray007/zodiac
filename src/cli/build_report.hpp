// zodiac::cli::ResultReport is the diagnostics sink that keeps the tally and transcript of a zodiac build or zodiac run, for the summary line and
// the result window; create_project_file is the result window's "Create project file" action.
//
// write() records every line (any level) in arrival order and counts an error for each ZODIAC_LOG_ERROR line that does not start with two spaces
// (continuation lines, such as call-stack frames or a signing tool's output, do). The transcript keeps the newest lines whose sizes (plus one
// byte each for its line break) fit max_text_bytes, and always the newest line whole; older lines are dropped from it, still counted.
// summary() is the summary line. Build: "Build succeeded: <exe>" (native path, UTF-8) or "Build failed: <N> errors" ("1 error" singular).
// Run: always "Run failed: <N> errors" ("1 error"), whatever succeeded and exe say, since zodiac run shows a summary only for a failed run.
// text() is the result window's text: the summary line, then "(<M> earlier lines left out; the console shows every line)" when lines were
// dropped, then every kept line, joined with "\n" and with no trailing line break.
// Threads: write() may be called from any thread (Diagnostics also serializes its sinks); errors(), summary() and text() may run concurrently with it.
#pragma once
#include <core/diagnostics.hpp>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <span>
#include <string>
#include <string_view>

namespace zodiac::cli {
enum class ResultVerb{build,run};

class ResultReport final:public DiagnosticSink{
public:
// 8 MiB: far more than any build prints, and a bound on what a long zodiac run full of module log lines holds in memory.
static constexpr std::size_t default_max_text_bytes=std::size_t{8}<<20;
explicit ResultReport(ResultVerb verb,std::size_t max_text_bytes=default_max_text_bytes) noexcept:verb_(verb),max_text_bytes_(max_text_bytes){}
void write(zodiac_log_level level,std::string_view line) noexcept override;
std::size_t errors() const noexcept;
std::string summary(bool succeeded,const std::filesystem::path& exe) const;
std::string text(bool succeeded,const std::filesystem::path& exe) const;
private:
ResultVerb verb_;
std::size_t max_text_bytes_;
mutable std::mutex mutex_;
std::deque<std::string> lines_;
std::size_t text_size_=0;
std::size_t dropped_=0;
std::size_t errors_=0;
};

// Writes folder/zodiac.toml holding project_file_text(name,main_relative,static_modules,dynamic_modules) (core/project_file.hpp) and returns the
// one line the result window appends: "Created <path>", or "error: "<path>" already exists" when any file or folder of that name is there (also
// one that appeared after the build: the file is opened for exclusive creation, so nothing is ever replaced), or "error: cannot create "<path>":
// <reason>" / "error: cannot write "<path>"" (a partly written file is removed again). Paths are native, UTF-8. Never throws except std::bad_alloc.
std::string create_project_file(const std::filesystem::path& folder,std::string_view name,std::string_view main_relative,std::span<const std::string> static_modules,std::span<const std::string> dynamic_modules);
// The last element of the target's absolute, normalized path ("game" for "." in a folder game, "main.zs" for a script; "." when target is
// empty): what a result window title names when no project loaded.
std::string target_name(const std::filesystem::path& target);
}
