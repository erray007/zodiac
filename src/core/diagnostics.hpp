// zodiac::Diagnostics sends every user-facing message (module errors, compile errors, runtime errors, plugin logs) to all its sinks.
//
// Each report() becomes exactly one plain-text line per sink: control bytes are escaped with one_line, and the sinks add the line terminator.
// Line formats used by the runtime: compile "<rel_path>:<line>:<col>: error|warning: <message>" (compiler information messages are not reported), runtime "<rel_path>:<line>: runtime error: <message>"
// followed by one "  at <function> (<rel_path>:<line>)" line per frame, and "<message>" for module and plugin messages.
// Threads: report() may be called from any thread; sinks are written under one lock, so a sink never sees concurrent writes.
// Core never touches stdout/stderr itself: ConsoleSink hands each line to the platform layer, so other platforms can route it elsewhere.
#pragma once
#include <zodiac/plugin.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace zodiac {
class DiagnosticSink{
public:
virtual ~DiagnosticSink()=default;
// line is one UTF-8 line without terminator and without control bytes.
virtual void write(zodiac_log_level level,std::string_view line) noexcept=0;
};
// Writes to the process's standard error; a console shows the text as Unicode.
class ConsoleSink final:public DiagnosticSink{
public:
void write(zodiac_log_level level,std::string_view line) noexcept override;
};
// Appends UTF-8 lines (no BOM, "\n" terminated) to a file, creating its parent directories, and flushes every line so a crash loses nothing.
class FileSink final:public DiagnosticSink{
public:
explicit FileSink(const std::filesystem::path& path);
// False when the file could not be opened; error() then says why, and write() does nothing.
bool is_open() const noexcept{return error_.empty();}
const std::string& error() const noexcept{return error_;}
void write(zodiac_log_level level,std::string_view line) noexcept override;
private:
std::ofstream out_;
std::string error_;
};
class Diagnostics{
public:
Diagnostics()=default;
Diagnostics(const Diagnostics&)=delete;
Diagnostics& operator=(const Diagnostics&)=delete;
void add_sink(std::unique_ptr<DiagnosticSink> sink);
// Adds a FileSink for path. When the file cannot be opened, reports "error: cannot open log file "<path>": <reason>" to the sinks added so far
// and returns false; add the console sink first so that message is seen.
bool add_file_sink(const std::filesystem::path& path);
void report(zodiac_log_level level,std::string_view line) noexcept;
private:
std::mutex mutex_;
std::vector<std::unique_ptr<DiagnosticSink>> sinks_;
};
}
