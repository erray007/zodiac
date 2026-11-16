#include <core/diagnostics.hpp>
#include <core/path_utf8.hpp>
#include <core/text.hpp>
#include <platform/console.hpp>
#include <cerrno>
#include <ios>
#include <system_error>
#include <utility>

namespace zodiac {
void ConsoleSink::write(zodiac_log_level,std::string_view line) noexcept
{
platform::write_stderr_line(line);
}

FileSink::FileSink(const std::filesystem::path& path)
{
std::error_code error;
const std::filesystem::path parent=path.parent_path();
if(!parent.empty())std::filesystem::create_directories(parent,error);
if(error){
error_=error_text(error);
return;
}
errno=0;
out_.open(path,std::ios::binary|std::ios::app);
if(!out_.is_open()){
const int code=errno;
error_=code?error_text(std::error_code(code,std::generic_category())):std::string("the file cannot be opened for writing");
}
}
void FileSink::write(zodiac_log_level,std::string_view line) noexcept
{
if(!error_.empty())return;
try{
// The stream buffer joins the text and its terminator, so flush() hands the whole line to the system in one write.
out_.write(line.data(),static_cast<std::streamsize>(line.size()));
out_.put('\n');
out_.flush();
}
catch(...){
}
}

void Diagnostics::add_sink(std::unique_ptr<DiagnosticSink> sink)
{
if(!sink)return;
const std::lock_guard lock(mutex_);
sinks_.push_back(std::move(sink));
}
bool Diagnostics::add_file_sink(const std::filesystem::path& path)
{
auto sink=std::make_unique<FileSink>(path);
if(sink->is_open()){
add_sink(std::move(sink));
return true;
}
const std::string shown=path_to_utf8(path);
std::string message;
message.reserve(shown.size()+sink->error().size()+32);
message.append("error: cannot open log file \"").append(shown).append("\": ").append(sink->error());
report(ZODIAC_LOG_ERROR,message);
return false;
}
void Diagnostics::report(zodiac_log_level level,std::string_view line) noexcept
{
try{
// Escaping copies the text, so it only happens when the line really contains control bytes.
std::string escaped;
if(!is_one_line(line)){
escaped=one_line(line);
line=escaped;
}
const std::lock_guard lock(mutex_);
for(const std::unique_ptr<DiagnosticSink>& sink:sinks_)sink->write(level,line);
}
catch(...){
}
}
}
