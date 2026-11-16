// zodiac::cli::BuildReport is the diagnostics sink that keeps a zodiac build's tally and transcript, for the summary line and the result window.
//
// write() records every line (any level) in arrival order and counts an error for each ZODIAC_LOG_ERROR line that does not start with two spaces
// (continuation lines, such as call-stack frames or a signing tool's output, do).
// summary() is the build's summary line: "Build succeeded: <exe>" (native path, UTF-8) or "Build failed: <N> errors" ("1 error" singular).
// text() is the result window's text: the summary line, then every recorded line, joined with "\n" and with no trailing line break.
// Threads: write() may be called from any thread (Diagnostics also serializes its sinks); errors(), summary() and text() may run concurrently with it.
#pragma once
#include <core/diagnostics.hpp>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac::cli {
class BuildReport final:public DiagnosticSink{
public:
void write(zodiac_log_level level,std::string_view line) noexcept override;
std::size_t errors() const noexcept;
std::string summary(bool succeeded,const std::filesystem::path& exe) const;
std::string text(bool succeeded,const std::filesystem::path& exe) const;
private:
mutable std::mutex mutex_;
std::vector<std::string> lines_;
std::size_t text_size_=0;
std::size_t errors_=0;
};
}
