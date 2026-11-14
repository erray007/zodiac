#include <core/diagnostics.hpp>
#include <core/module_registry.hpp>
#include <core/path_utf8.hpp>
#include <core/project_file.hpp>
#include <core/script_runtime.hpp>
#include <doctest/doctest.h>
#include <platform/console.hpp>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
namespace fs=std::filesystem;
// Runs before doctest starts so a failing module load can never raise a Windows modal dialog.
const int g_dialogs_disabled=(zodiac::platform::disable_error_dialogs(),0);
fs::path from_utf8(std::string_view text)
{
return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
fs::path fixture(std::string_view name){return from_utf8(ZODIAC_TEST_SCRIPT_DIR)/from_utf8(name);}
zodiac::ModuleSearchPaths search(){return zodiac::ModuleSearchPaths{{from_utf8(ZODIAC_TEST_MODULE_DIR)}};}
zodiac::Project project_of(std::string_view name)
{
auto project=zodiac::load_project(fixture(name));
INFO(project.error_or(""));
REQUIRE(project.has_value());
return std::move(*project);
}
// Keeps every line it receives, with its level, in a list the test owns.
struct Line{
zodiac_log_level level;
std::string text;
};
class MemorySink final:public zodiac::DiagnosticSink{
public:
explicit MemorySink(std::vector<Line>& lines):lines_(lines){}
void write(zodiac_log_level level,std::string_view line) noexcept override{lines_.push_back({level,std::string(line)});}
private:
std::vector<Line>& lines_;
};
std::vector<std::string> texts(const std::vector<Line>& lines)
{
std::vector<std::string> out;
for(const Line& line:lines)out.push_back(line.text);
return out;
}
bool has_line(const std::vector<Line>& lines,std::string_view text)
{
return std::ranges::any_of(lines,[&](const Line& line){return line.text==text;});
}
bool has_line_starting(const std::vector<Line>& lines,std::string_view prefix)
{
return std::ranges::any_of(lines,[&](const Line& line){return line.text.starts_with(prefix);});
}
std::string join(const std::vector<Line>& lines)
{
std::string text;
for(const Line& line:lines)text.append(line.text).push_back('\n');
return text;
}
// A fresh directory under the system temp dir, removed on destruction.
class TempDir{
public:
TempDir()
{
static std::atomic<unsigned> counter{0};
dir_=fs::temp_directory_path()/("zodiac_runtime_test_"+std::to_string(counter.fetch_add(1))+"_"+std::to_string(reinterpret_cast<std::uintptr_t>(this)));
fs::create_directories(dir_);
}
~TempDir()
{
std::error_code ec;
fs::remove_all(dir_,ec);
}
TempDir(const TempDir&)=delete;
TempDir& operator=(const TempDir&)=delete;
const fs::path& dir() const{return dir_;}
private:
fs::path dir_;
};
std::string read_file(const fs::path& path)
{
std::ifstream in(path,std::ios::binary);
return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
std::vector<std::string> split_lines(std::string_view text)
{
std::vector<std::string> lines;
while(!text.empty()){
const std::size_t end=text.find('\n');
lines.emplace_back(text.substr(0,end));
if(end==std::string_view::npos)break;
text.remove_prefix(end+1);
}
return lines;
}
bool valid_utf8(std::string_view text)
{
std::size_t i=0;
while(i<text.size()){
const unsigned char lead=static_cast<unsigned char>(text[i]);
std::size_t extra=0;
if(lead<0x80)extra=0;
else if((lead&0xE0)==0xC0&&lead>=0xC2)extra=1;
else if((lead&0xF0)==0xE0)extra=2;
else if((lead&0xF8)==0xF0&&lead<=0xF4)extra=3;
else return false;
if(i+extra>=text.size()&&extra)return false;
for(std::size_t k=1;k<=extra;++k)if((static_cast<unsigned char>(text[i+k])&0xC0)!=0x80)return false;
i+=extra+1;
}
return true;
}
// Points file descriptor 2 at a file for the lifetime of the object, so the real ConsoleSink output can be inspected.
class StderrCapture{
public:
explicit StderrCapture(const fs::path& file):file_(file)
{
std::fflush(stderr);
std::FILE* target=std::fopen(file.string().c_str(),"wb");
REQUIRE(target!=nullptr);
saved_=dup(2);
REQUIRE(saved_>=0);
REQUIRE(dup2(fileno(target),2)>=0);
std::fclose(target);
}
~StderrCapture(){restore();}
StderrCapture(const StderrCapture&)=delete;
StderrCapture& operator=(const StderrCapture&)=delete;
std::string text()
{
restore();
return read_file(file_);
}
private:
void restore()
{
if(saved_<0)return;
std::fflush(stderr);
dup2(saved_,2);
close(saved_);
saved_=-1;
}
fs::path file_;
int saved_=-1;
};
struct Outcome{
std::vector<Line> lines;
int build=-1;
int run=-1;
};
Outcome execute(std::string_view name,bool run=true)
{
Outcome outcome;
const zodiac::Project project=project_of(name);
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<MemorySink>(outcome.lines));
zodiac::ScriptRuntime runtime(diagnostics,search());
outcome.build=runtime.build(project);
if(run&&outcome.build==0)outcome.run=runtime.run();
return outcome;
}
}

TEST_CASE("compile error format")
{
TempDir temp;
const fs::path captured=temp.dir()/"stderr.txt";
const zodiac::Project project=project_of("compile_error");
std::vector<Line> lines;
int build=-1;
std::string console;
{
StderrCapture capture(captured);
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<zodiac::ConsoleSink>());
diagnostics.add_sink(std::make_unique<MemorySink>(lines));
zodiac::ScriptRuntime runtime(diagnostics,search());
build=runtime.build(project);
console=capture.text();
}
INFO(console);
CHECK(build==1);
CHECK(console.find("main.zs:3:5: error:")!=std::string::npos);
CHECK(has_line_starting(lines,"main.zs:3:5: error: "));
CHECK(split_lines(console)==texts(lines));
CHECK(texts(lines)==std::vector<std::string>{"main.zs:1:1: info: Compiling void main()","main.zs:3:5: error: No matching symbol 'zz'"});
for(const Line& line:lines)CHECK(line.text.find('\n')==std::string::npos);
const auto error=std::ranges::find_if(lines,[](const Line& line){return line.text.starts_with("main.zs:3:5: error: ");});
REQUIRE(error!=lines.end());
CHECK(error->level==ZODIAC_LOG_ERROR);
}

TEST_CASE("error in included file is reported with its own path")
{
const Outcome outcome=execute("include_error");
INFO(join(outcome.lines));
CHECK(outcome.build==1);
CHECK(has_line_starting(outcome.lines,"includes/util.zs:2:"));
}

TEST_CASE("missing included file names the including script")
{
const Outcome outcome=execute("missing_include");
CHECK(outcome.build==1);
REQUIRE(outcome.lines.size()==1);
CHECK(outcome.lines[0].level==ZODIAC_LOG_ERROR);
CHECK(outcome.lines[0].text.starts_with("main.zs: error: cannot read included file \"gone/nope.zs\": "));
}

TEST_CASE("include from a non-ASCII directory")
{
const Outcome outcome=execute("non_ascii_include");
INFO(join(outcome.lines));
CHECK(outcome.build==0);
// harita.zs and yardım.zs start with a UTF-8 BOM, and yardım.zs is included relative to şehir/harita.zs.
CHECK(outcome.run==42);
}

TEST_CASE("uncaught exception reports location and stack")
{
const Outcome outcome=execute("runtime_error");
INFO(join(outcome.lines));
CHECK(outcome.build==0);
CHECK(outcome.run==1);
CHECK(has_line(outcome.lines,"main.zs:8: runtime error: Null pointer access"));
CHECK(has_line(outcome.lines,"  at helper (main.zs:8)"));
CHECK(has_line(outcome.lines,"  at main (main.zs:12)"));
const auto first=std::ranges::find_if(outcome.lines,[](const Line& line){return line.text.find("runtime error")!=std::string::npos;});
REQUIRE(first!=outcome.lines.end());
CHECK(first->level==ZODIAC_LOG_ERROR);
CHECK(std::next(first)->text=="  at helper (main.zs:8)");
}

TEST_CASE("int main return value is the exit code")
{
const Outcome outcome=execute("int_main");
INFO(join(outcome.lines));
CHECK(outcome.build==0);
CHECK(outcome.run==7);
}

TEST_CASE("void main returns 0")
{
const Outcome outcome=execute("void_main");
INFO(join(outcome.lines));
CHECK(outcome.build==0);
CHECK(outcome.run==0);
CHECK(outcome.lines.empty());
}

TEST_CASE("missing main")
{
const Outcome outcome=execute("missing_main");
CHECK(outcome.build==1);
CHECK(texts(outcome.lines)==std::vector<std::string>{"main.zs: error: no main() function"});
}

TEST_CASE("main with an unsupported signature")
{
const Outcome outcome=execute("wrong_main");
CHECK(outcome.build==1);
CHECK(texts(outcome.lines)==std::vector<std::string>{"main.zs:1:1: error: main() must be declared as \"void main()\" or \"int main()\""});
}

TEST_CASE("project modules are loaded and scripts can catch exceptions")
{
const Outcome outcome=execute("uses_module");
INFO(join(outcome.lines));
CHECK(outcome.build==0);
CHECK(outcome.run==7);
}

TEST_CASE("missing module fails the build with code 2")
{
const Outcome outcome=execute("missing_module");
CHECK(outcome.build==2);
REQUIRE(outcome.lines.size()==1);
CHECK(outcome.lines[0].level==ZODIAC_LOG_ERROR);
CHECK(outcome.lines[0].text.starts_with("module \"nope\" not found in: "));
CHECK(outcome.lines[0].text.ends_with("; "+zodiac::path_to_utf8(project_of("missing_module").root/"plugins")));
}

TEST_CASE("run before a successful build")
{
std::vector<Line> lines;
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<MemorySink>(lines));
zodiac::ScriptRuntime runtime(diagnostics,search());
CHECK(runtime.run()==1);
CHECK(texts(lines)==std::vector<std::string>{"error: no script has been built"});
}

TEST_CASE("file sink receives the same lines as the console")
{
TempDir temp;
const fs::path log=temp.dir()/"logs"/"deep"/"zodiac.log";
const zodiac::Project project=project_of("compile_error");
std::string console;
{
StderrCapture capture(temp.dir()/"stderr.txt");
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<zodiac::ConsoleSink>());
CHECK(diagnostics.add_file_sink(log));
zodiac::ScriptRuntime runtime(diagnostics,search());
CHECK(runtime.build(project)==1);
console=capture.text();
}
const std::string file=read_file(log);
INFO(console);
CHECK_FALSE(file.empty());
CHECK(file==console);
CHECK(valid_utf8(file));
CHECK_FALSE(file.starts_with("\xEF\xBB\xBF"));
CHECK(file.ends_with('\n'));
CHECK(file.find('\r')==std::string::npos);
}

TEST_CASE("errors before the script runs reach the file sink")
{
TempDir temp;
const fs::path log=temp.dir()/"zodiac.log";
const zodiac::Project project=project_of("missing_module");
{
zodiac::Diagnostics diagnostics;
CHECK(diagnostics.add_file_sink(log));
zodiac::ScriptRuntime runtime(diagnostics,search());
CHECK(runtime.build(project)==2);
}
const std::vector<std::string> lines=split_lines(read_file(log));
REQUIRE(lines.size()==1);
CHECK(lines[0].starts_with("module \"nope\" not found in: "));
}

TEST_CASE("file sink appends UTF-8 lines to a non-ASCII path")
{
TempDir temp;
const fs::path log=temp.dir()/fs::path(u8"günlük")/fs::path(u8"oyun ş.log");
{
zodiac::Diagnostics diagnostics;
CHECK(diagnostics.add_file_sink(log));
diagnostics.report(ZODIAC_LOG_INFO,"first \xC5\x9F");
}
{
zodiac::Diagnostics diagnostics;
CHECK(diagnostics.add_file_sink(log));
diagnostics.report(ZODIAC_LOG_WARNING,"second");
}
CHECK(read_file(log)=="first \xC5\x9F\nsecond\n");
}

TEST_CASE("unopenable log file is reported to the other sinks")
{
TempDir temp;
const fs::path blocker=temp.dir()/"blocker";
std::ofstream(blocker)<<"not a directory";
const fs::path log=blocker/"zodiac.log";
std::vector<Line> lines;
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<MemorySink>(lines));
CHECK_FALSE(diagnostics.add_file_sink(log));
REQUIRE(lines.size()==1);
CHECK(lines[0].level==ZODIAC_LOG_ERROR);
CHECK(lines[0].text.starts_with("error: cannot open log file \""+zodiac::path_to_utf8(log)+"\": "));
CHECK(lines[0].text.size()>("error: cannot open log file \""+zodiac::path_to_utf8(log)+"\": ").size());
diagnostics.report(ZODIAC_LOG_INFO,"still works");
CHECK(lines.size()==2);
}

TEST_CASE("report writes exactly one escaped line per sink")
{
std::vector<Line> first;
std::vector<Line> second;
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<MemorySink>(first));
diagnostics.add_sink(std::make_unique<MemorySink>(second));
diagnostics.report(ZODIAC_LOG_WARNING,"line one\nline two\ttab");
REQUIRE(first.size()==1);
REQUIRE(second.size()==1);
CHECK(first[0].text=="line one\\nline two\\ttab");
CHECK(first[0].level==ZODIAC_LOG_WARNING);
CHECK(second[0].text==first[0].text);
}

TEST_CASE("plugin log messages reach the diagnostics")
{
std::vector<Line> lines;
zodiac::Diagnostics diagnostics;
diagnostics.add_sink(std::make_unique<MemorySink>(lines));
zodiac::ScriptRuntime runtime(diagnostics,search());
zodiac_host* host=runtime.host().abi();
host->log(host,ZODIAC_LOG_WARNING,"from a plugin");
CHECK(texts(lines)==std::vector<std::string>{"from a plugin"});
CHECK(lines[0].level==ZODIAC_LOG_WARNING);
}
