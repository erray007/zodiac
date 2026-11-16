// zodiac::cli::BuildReport (see cli/build_report.hpp): counts a build's errors and keeps its lines for the summary and the result window.
#include <cli/build_report.hpp>
#include <core/path_utf8.hpp>
#include <charconv>

namespace zodiac::cli {
void BuildReport::write(zodiac_log_level level,std::string_view line) noexcept
{
const std::lock_guard lock(mutex_);
if(level==ZODIAC_LOG_ERROR&&!line.starts_with("  "))++errors_;
try{
lines_.emplace_back(line);
text_size_+=line.size()+1;
}
catch(...){
// Out of memory: the line is lost from the transcript but still counted, and the console sink has it.
}
}

std::size_t BuildReport::errors() const noexcept
{
const std::lock_guard lock(mutex_);
return errors_;
}

std::string BuildReport::summary(bool succeeded,const std::filesystem::path& exe) const
{
if(succeeded)return "Build succeeded: "+path_to_utf8(exe);
const std::size_t count=errors();
char digits[24];
const auto end=std::to_chars(digits,digits+sizeof digits,count).ptr;
std::string line="Build failed: ";
line.append(digits,end).append(count==1?" error":" errors");
return line;
}

std::string BuildReport::text(bool succeeded,const std::filesystem::path& exe) const
{
std::string result=summary(succeeded,exe);
const std::lock_guard lock(mutex_);
result.reserve(result.size()+text_size_);
for(const std::string& line:lines_){
result.push_back('\n');
result.append(line);
}
return result;
}
}
