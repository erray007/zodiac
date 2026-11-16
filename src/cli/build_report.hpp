// zodiac::cli::ResultReport is the diagnostics sink that keeps the tally and transcript of a zodiac build or zodiac run, for the summary line and
// the result window; create_project_file is the result window's "Create project file" action, offers_project_file says when a run or build window
// has it and create_project_action builds it for both.
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
#include <platform/result_window.hpp>
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

// Reported at ZODIAC_LOG_WARNING level (so ResultReport counts no error for it) by zodiac run and zodiac build right after the scripts of a lone
// script, run or built without a project file and so with the core only, failed to compile (exit 1): a call into a module is the likely cause.
inline constexpr std::string_view lone_script_compile_note="note: no zodiac.toml; only the core is available, so module functions are not declared (create a project file to add modules)";
// Writes folder/zodiac.toml holding project_file_text(name,main_relative,{},{}) (core/project_file.hpp: empty static and dynamic lists, since a
// lone script ran with the core only) and returns the one line the result window appends: "Created <path>", or "error: "<path>" already exists"
// when any file or folder of that name is there (also one that appeared after the build: the file is opened for exclusive creation, so nothing
// is ever replaced), or "error: cannot create "<path>": <reason>" / "error: cannot write "<path>"" (a partly written file is removed again).
// Paths are native, UTF-8. Never throws except std::bad_alloc.
std::string create_project_file(const std::filesystem::path& folder,std::string_view name,std::string_view main_relative);
// Whether lone_script_compile_note is reported, and with it the result window offers "Create project file": exactly when a script ran or was built
// without a project file (lone) and its scripts failed to compile (compile_failed). Any other failure (a missing main(), a runtime error, a project
// file's run) is neither.
constexpr bool offers_project_file(bool lone,bool compile_failed) noexcept
{
return lone&&compile_failed;
}
// The result window's "Create &project file" action: run() calls create_project_file(root,name,main_script's file name) and returns its line. It holds
// copies of its arguments, so it may outlive them; its label is a literal.
platform::ResultAction create_project_action(const std::filesystem::path& root,std::string_view name,const std::filesystem::path& main_script);
// The last element of the target's absolute, normalized path ("game" for "." in a folder game, "main.zs" for a script; "." when target is
// empty): what a result window title names when no project loaded.
std::string target_name(const std::filesystem::path& target);
}
