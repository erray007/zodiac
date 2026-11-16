// zodiac::cli::ResultReport and create_project_file (see cli/build_report.hpp): a build's or run's error tally and transcript for the summary and
// the result window, and the window's "Create project file" action.
#include <cli/build_report.hpp>
#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <core/text.hpp>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <system_error>

namespace zodiac::cli {
namespace {
void append_count(std::string& line,std::size_t count)
{
char digits[24];
const auto end=std::to_chars(digits,digits+sizeof digits,count).ptr;
line.append(digits,end);
}

std::string quoted(const std::filesystem::path& path)
{
std::string result="\"";
result.append(path_to_utf8(path)).push_back('"');
return result;
}
}

void ResultReport::write(zodiac_log_level level,std::string_view line) noexcept
{
const std::lock_guard lock(mutex_);
if(level==ZODIAC_LOG_ERROR&&!line.starts_with("  "))++errors_;
try{
lines_.emplace_back(line);
text_size_+=line.size()+1;
}
catch(...){
// Out of memory: the line is lost from the transcript but still counted, and the console sink has it.
return;
}
// The newest line stays even when it alone is over the budget.
while(text_size_>max_text_bytes_&&lines_.size()>1){
text_size_-=lines_.front().size()+1;
lines_.pop_front();
++dropped_;
}
}

std::size_t ResultReport::errors() const noexcept
{
const std::lock_guard lock(mutex_);
return errors_;
}

std::string ResultReport::summary(bool succeeded,const std::filesystem::path& exe) const
{
const bool build=verb_==ResultVerb::build;
if(build&&succeeded)return "Build succeeded: "+path_to_utf8(exe);
const std::size_t count=errors();
std::string line=build?"Build failed: ":"Run failed: ";
append_count(line,count);
line.append(count==1?" error":" errors");
return line;
}

std::string ResultReport::text(bool succeeded,const std::filesystem::path& exe) const
{
std::string result=summary(succeeded,exe);
const std::lock_guard lock(mutex_);
constexpr std::string_view left_out=" earlier lines left out; the console shows every line)";
result.reserve(result.size()+text_size_+(dropped_?left_out.size()+24:0));
if(dropped_){
result.append("\n(");
append_count(result,dropped_);
result.append(left_out);
}
for(const std::string& line:lines_){
result.push_back('\n');
result.append(line);
}
return result;
}

std::string create_project_file(const std::filesystem::path& folder,std::string_view name,std::string_view main_relative)
{
const std::filesystem::path file=folder/"zodiac.toml";
const std::string text=project_file_text(name,main_relative,{},{});
errno=0;
// noreplace: the open itself fails when the name exists, so a file created meanwhile (by an editor, another build) is never replaced.
std::ofstream out(file,std::ios::out|std::ios::binary|std::ios::noreplace);
if(!out.is_open()){
const int reason=errno;
std::error_code ec;
if(std::filesystem::exists(file,ec)||reason==EEXIST)return "error: "+quoted(file)+" already exists";
std::string line="error: cannot create "+quoted(file);
if(reason)line.append(": ").append(one_line(error_text(std::error_code(reason,std::generic_category()))));
return line;
}
out.write(text.data(),static_cast<std::streamsize>(text.size()));
out.close();
if(!out.fail())return "Created "+path_to_utf8(file);
// The file is this call's own (it did not exist before), so a partial one goes again and a later press can retry.
std::error_code ec;
std::filesystem::remove(file,ec);
return "error: cannot write "+quoted(file);
}

std::string target_name(const std::filesystem::path& target)
{
std::error_code ec;
std::filesystem::path path=std::filesystem::absolute(target.empty()?std::filesystem::path("."):target,ec);
if(ec)path=target;
path=path.lexically_normal();
if(!path.has_filename()&&path.has_relative_path())path=path.parent_path();
return path_to_utf8(path.filename());
}
}
