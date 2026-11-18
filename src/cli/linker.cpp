// zodiac::cli::link_release (see cli/linker.hpp): expands the kit's captured link line into a response file and runs the kit's ld.lld on it.
#include <cli/linker.hpp>
#include <core/path_utf8.hpp>
#include <core/text.hpp>
#include <platform/process.hpp>
#include <zodiac/plugin.h>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace zodiac::cli {
namespace {
std::unexpected<std::string> fail(Diagnostics& diagnostics,std::string message)
{
diagnostics.report(ZODIAC_LOG_ERROR,message);
return std::unexpected(std::move(message));
}
}

#ifdef _WIN32
namespace {
namespace fs=std::filesystem;
std::string in_quotes(std::string_view text)
{
std::string result;
result.reserve(text.size()+2);
result.push_back('"');
result.append(text);
result.push_back('"');
return result;
}
// '/' separators, so no argument holds a backslash and the GNU and Windows response-file rules read every argument the same way.
std::string generic_utf8(const fs::path& path)
{
const std::u8string text=path.generic_u8string();
return std::string(reinterpret_cast<const char*>(text.data()),text.size());
}
// Appends argument quoted so that the Windows command line rules (CommandLineToArgvW, LLVM's TokenizeWindowsCommandLine) give it back unchanged:
// backslashes are literal except in front of a quote, where they are doubled, and the quote itself is escaped. The bytes are UTF-8 and stay as they are.
void append_quoted(std::string& out,std::string_view argument)
{
out.push_back('"');
std::size_t backslashes=0;
for(const char character:argument){
if(character=='\\'){
++backslashes;
continue;
}
if(character=='"'){
out.append(backslashes*2+1,'\\');
out.push_back('"');
} else {
out.append(backslashes,'\\');
out.push_back(character);
}
backslashes=0;
}
out.append(backslashes*2,'\\');
out.append("\"\n");
}
void replace_all(std::string& text,std::string_view from,std::string_view to)
{
for(std::size_t at=text.find(from);at!=std::string::npos;at=text.find(from,at+to.size()))text.replace(at,from.size(),to);
}
enum class LinkLineError{unreadable,damaged};
// The lines of link.txt; damaged when a line is empty, {inputs} or {output} is not there exactly once, or {cache} is in more than one line.
std::expected<std::vector<std::string>,LinkLineError> read_link_line(const fs::path& file)
{
std::ifstream in(file,std::ios::binary);
if(!in)return std::unexpected(LinkLineError::unreadable);
const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
if(in.bad())return std::unexpected(LinkLineError::unreadable);
std::vector<std::string> lines;
std::size_t inputs=0;
std::size_t outputs=0;
std::size_t caches=0;
std::size_t start=0;
while(start<text.size()){
std::size_t end=text.find('\n',start);
if(end==std::string::npos)end=text.size();
std::string line=text.substr(start,end-start);
start=end+1;
if(!line.empty()&&line.back()=='\r')line.pop_back();
if(line.empty())return std::unexpected(LinkLineError::damaged);
if(line=="{inputs}")++inputs;
if(line=="{output}")++outputs;
if(line.find("{cache}")!=std::string::npos)++caches;
lines.push_back(std::move(line));
}
if(inputs!=1||outputs!=1||caches>1)return std::unexpected(LinkLineError::damaged);
return lines;
}
// Reports each non-empty line of text, prefixed by prefix (continuation lines of a failure are indented by two spaces).
void report_lines(Diagnostics& diagnostics,zodiac_log_level level,std::string_view prefix,std::string_view text)
{
std::string line;
while(!text.empty()){
const std::size_t end=text.find('\n');
const std::string_view part=text.substr(0,end);
if(!part.empty()){
line.assign(prefix).append(part);
diagnostics.report(level,line);
}
if(end==std::string_view::npos)break;
text.remove_prefix(end+1);
}
}
// Removes the response file when the link is over, whatever happened.
class ResponseFile{
public:
ResponseFile(fs::path path,Diagnostics& diagnostics):path_(std::move(path)),diagnostics_(diagnostics){}
~ResponseFile()
{
std::error_code ec;
fs::remove(path_,ec);
if(ec)diagnostics_.report(ZODIAC_LOG_WARNING,"warning: cannot remove "+in_quotes(path_to_utf8(path_))+": "+error_text(ec));
}
ResponseFile(const ResponseFile&)=delete;
ResponseFile& operator=(const ResponseFile&)=delete;
const fs::path& path() const noexcept{return path_;}
private:
fs::path path_;
Diagnostics& diagnostics_;
};
}

std::expected<void,std::string> link_release(const KitLayout& kit,const LinkRequest& request,Diagnostics& diagnostics)
{
const fs::path link_file=kit.release_lib/"link.txt";
auto lines=read_link_line(link_file);
if(!lines){
const std::string shown=in_quotes(path_to_utf8(link_file));
return fail(diagnostics,lines.error()==LinkLineError::damaged?"error: the release link line "+shown+" is damaged":"error: cannot read the release link line "+shown);
}
const std::string release_lib=generic_utf8(kit.release_lib);
std::string arguments;
for(std::string& line:*lines){
if(line=="{inputs}"){
for(const std::string& symbol:request.forced_symbols){
append_quoted(arguments,"-u");
append_quoted(arguments,symbol);
}
for(const fs::path& input:request.inputs)append_quoted(arguments,generic_utf8(input));
continue;
}
if(line=="{output}"){
append_quoted(arguments,generic_utf8(request.output));
continue;
}
// The cache line is expanded on its own, so a cache path holding the text "{release_lib}" stays as it is.
if(line.find("{cache}")!=std::string::npos){
if(request.thinlto_cache.empty())continue;
replace_all(line,"{cache}",generic_utf8(request.thinlto_cache));
append_quoted(arguments,line);
continue;
}
replace_all(line,"{release_lib}",release_lib);
append_quoted(arguments,line);
}
fs::path rsp_path=request.output;
rsp_path+=".rsp";
{
std::ofstream out(rsp_path,std::ios::binary|std::ios::trunc);
out.write(arguments.data(),static_cast<std::streamsize>(arguments.size()));
out.close();
if(!out){
std::error_code ec;
fs::remove(rsp_path,ec);
return fail(diagnostics,"error: cannot write "+in_quotes(path_to_utf8(rsp_path)));
}
}
const ResponseFile rsp(std::move(rsp_path),diagnostics);
const std::vector<std::string> argv{path_to_utf8(kit.bin/"ld.lld.exe"),"@"+generic_utf8(rsp.path())};
const auto ran=platform::run_process(argv);
if(!ran)return fail(diagnostics,"error: cannot run the linker: "+ran.error());
if(ran->exit_code!=0){
auto failed=fail(diagnostics,"error: linking failed");
report_lines(diagnostics,ZODIAC_LOG_ERROR,"  ",ran->output);
return failed;
}
report_lines(diagnostics,ZODIAC_LOG_WARNING,{},ran->output);
return {};
}
#else
std::expected<void,std::string> link_release(const KitLayout&,const LinkRequest&,Diagnostics& diagnostics)
{
// Release builds are Windows-only in this plan: the kit's link line, CRT subset and linker come from llvm-mingw.
return fail(diagnostics,"error: release builds are not supported on this host yet");
}
#endif
}
