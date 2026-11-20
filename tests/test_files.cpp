// Tests the script file object and the file helpers (core/vfs/files.hpp; spec 8.7 of the core-types design) through scripts run
// against ScriptFixture's Vfs: every mode, text and binary reads (line ends, the BOM, a line end cut by a count), read_line, the
// misuse and failure texts, the helpers and last_file_error, copies and moves across "/" and "data:/", file_modified against
// date_time::now(), reads at the 64 KiB buffer's edges, and a sparse file of more than 4 GiB read at its end (Review Focus 3).
#include "script_fixture.hpp"
#include <core/vfs/vfs.hpp>
#include <platform/clock.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using zodiac_test::ScriptFixture;

namespace {
namespace fs=std::filesystem;
// Runs "bool main(){<body>}" and returns its result.
bool holds(ScriptFixture& fixture,std::string_view body)
{
const std::string source="bool main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"bool main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnByte()!=0;
}
// Runs "string main(){<body>}" and returns the text.
std::string text_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source="string main(){"+std::string(body)+"}";
INFO(source);
const int result=fixture.run(source.c_str(),"string main()");
INFO(fixture.exception_string());
REQUIRE(result==asEXECUTION_FINISHED);
return fixture.returned_string();
}
// Runs "int64 main(){<body>}" and returns the value.
std::int64_t int64_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source="int64 main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"int64 main()")==asEXECUTION_FINISHED);
return static_cast<std::int64_t>(fixture.context->GetReturnQWord());
}
// Runs "void main(){<body>}", which must raise a script exception, and returns its text.
std::string error_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source="void main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"void main()")==asEXECUTION_EXCEPTION);
return fixture.exception_string();
}
// Writes bytes to <root>/name from C++, making the folders on the way.
void put(const fs::path& file,std::string_view bytes)
{
fs::create_directories(file.parent_path());
std::ofstream out(file,std::ios::binary|std::ios::trunc);
out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
REQUIRE(out.good());
}
std::string contents(const fs::path& file)
{
std::ifstream in(file,std::ios::binary);
REQUIRE(in.good());
return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
// size bytes that differ at every offset of a 64 KiB block and from block to block.
std::string pattern(std::size_t size)
{
std::string bytes(size,'\0');
for(std::size_t i=0;i<size;++i)bytes[i]=static_cast<char>((i*131+i/65536*7+1)&0xFF);
return bytes;
}
// A sparse file of offset + text.size() bytes holding text at offset and nothing (a hole) before it. Fails rather than write the
// hole out when the file system cannot keep it sparse.
void make_sparse(const fs::path& file,std::uint64_t offset,std::string_view text)
{
fs::create_directories(file.parent_path());
#ifdef _WIN32
const HANDLE handle=CreateFileW(file.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
REQUIRE(handle!=INVALID_HANDLE_VALUE);
DWORD returned=0;
const bool sparse=DeviceIoControl(handle,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr)!=0;
LARGE_INTEGER end;
end.QuadPart=static_cast<LONGLONG>(offset);
const bool sized=sparse&&SetFilePointerEx(handle,end,nullptr,FILE_BEGIN)&&SetEndOfFile(handle);
DWORD written=0;
const bool wrote=sized&&WriteFile(handle,text.data(),static_cast<DWORD>(text.size()),&written,nullptr)&&written==text.size();
CloseHandle(handle);
REQUIRE(sparse);
REQUIRE(sized);
REQUIRE(wrote);
DWORD high=0;
const DWORD low=GetCompressedFileSizeW(file.c_str(),&high);
REQUIRE((low!=INVALID_FILE_SIZE||GetLastError()==NO_ERROR));
const std::uint64_t allocated=(static_cast<std::uint64_t>(high)<<32)|low;
#else
const int descriptor=::open(file.c_str(),O_RDWR|O_CREAT|O_TRUNC,0600);
REQUIRE(descriptor>=0);
const bool sized=ftruncate(descriptor,static_cast<off_t>(offset))==0;
const bool wrote=sized&&pwrite(descriptor,text.data(),text.size(),static_cast<off_t>(offset))==static_cast<ssize_t>(text.size());
struct stat info{};
const bool stated=fstat(descriptor,&info)==0;
::close(descriptor);
REQUIRE(sized);
REQUIRE(wrote);
REQUIRE(stated);
const std::uint64_t allocated=static_cast<std::uint64_t>(info.st_blocks)*512;
#endif
// The hole takes no space: at most a few blocks hold the text.
REQUIRE(allocated<(std::uint64_t{1}<<20));
}
// Removes a file at scope exit, also when a check fails.
class RemoveAtExit{
public:
explicit RemoveAtExit(fs::path file):file_(std::move(file)){}
~RemoveAtExit()
{
std::error_code error;
fs::remove(file_,error);
}
RemoveAtExit(const RemoveAtExit&)=delete;
RemoveAtExit& operator=(const RemoveAtExit&)=delete;
private:
fs::path file_;
};
}

TEST_CASE("every mode round-trips")
{
ScriptFixture fixture;
for(const char* write_mode:{"w","wb"}){
CAPTURE(write_mode);
CHECK(text_of(fixture,std::string("file f; if(!f.open(\"m.txt\", \"")+write_mode+"\"))return f.error; f.write(\"one\"); f.close(); f.open(\"m.txt\", \"r\"); return f.read();")=="one");
}
for(const char* append_mode:{"a","ab"}){
CAPTURE(append_mode);
CHECK(text_of(fixture,std::string("write_file(\"a.txt\", \"one\"); file f; f.open(\"a.txt\", \"")+append_mode+"\"); f.seek(0); f.write(\"two\"); f.write(\"three\"); f.close(); return read_file(\"a.txt\");")=="onetwothree");
}
for(const char* update_mode:{"r+","r+b","rb+"}){
CAPTURE(update_mode);
CHECK(text_of(fixture,std::string("write_file(\"u.txt\", \"abcdef\"); file f; f.open(\"u.txt\", \"")+update_mode+"\"); f.seek(2); f.write(\"XY\"); string rest=f.read(); f.seek(0); string all=f.read(); f.close(); return rest+\"|\"+all+\"|\"+read_file(\"u.txt\");")=="ef|abXYef|abXYef");
}
for(const char* read_mode:{"r","rb"}){
CAPTURE(read_mode);
CHECK(text_of(fixture,std::string("write_file(\"r.txt\", \"read me\"); file f; f.open(\"r.txt\", \"")+read_mode+"\"); return f.read(4)+\"|\"+f.read();")=="read| me");
}
// "w" empties an existing file and reads back what it wrote; the size and position follow the writes.
CHECK(text_of(fixture,"write_file(\"w.txt\", \"old text\"); file f; f.open(\"w.txt\", \"w\"); string s=f.size+\",\"+f.position; f.write(\"new\"); s+=\",\"+f.size+\",\"+f.position+\",\"+f.eof; f.seek(0); return s+\",\"+f.read();")=="0,0,3,3,true,new");
CHECK(contents(fixture.root()/"w.txt")=="new");
}

TEST_CASE("text mode reads line ends as \\n and skips a BOM; binary mode keeps the bytes")
{
ScriptFixture fixture;
put(fixture.root()/"crlf.txt","a\r\nb\rc\r\n\r\n");
CHECK(text_of(fixture,"file f; f.open(\"crlf.txt\", \"r\"); return f.read();")=="a\nb\rc\n\n");
CHECK(text_of(fixture,"file f; f.open(\"crlf.txt\", \"rb\"); return f.read();")=="a\r\nb\rc\r\n\r\n");
put(fixture.root()/"bom.txt","\xEF\xBB\xBFhi\r\n");
CHECK(text_of(fixture,"file f; f.open(\"bom.txt\", \"r\"); return f.read();")=="hi\n");
CHECK(text_of(fixture,"file f; f.open(\"bom.txt\", \"r\"); return f.read(2);")=="hi");
CHECK(text_of(fixture,"file f; f.open(\"bom.txt\", \"r\"); string line=f.read_line(); return line+\",\"+f.position;")=="hi,7");
CHECK(text_of(fixture,"file f; f.open(\"bom.txt\", \"r\"); f.read(); f.seek(0); return f.read();")=="hi\n");
CHECK(text_of(fixture,"file f; f.open(\"bom.txt\", \"rb\"); return f.read();")=="\xEF\xBB\xBFhi\r\n");
// Only at position 0, and only a whole BOM.
put(fixture.root()/"late.txt","x\xEF\xBB\xBF");
CHECK(text_of(fixture,"file f; f.open(\"late.txt\", \"r\"); return f.read();")=="x\xEF\xBB\xBF");
put(fixture.root()/"half.txt","\xEF\xBB");
CHECK(text_of(fixture,"file f; f.open(\"half.txt\", \"r\"); return f.read();")=="\xEF\xBB");
// Writing never converts.
CHECK(holds(fixture,"file f; f.open(\"out.txt\", \"w\"); return f.write(\"a\\r\\nb\\n\");"));
CHECK(contents(fixture.root()/"out.txt")=="a\r\nb\n");
}

TEST_CASE("a counted text read takes the \\n of a \\r\\n it would cut")
{
ScriptFixture fixture;
put(fixture.root()/"cut.txt","ab\r\ncd\r\r\n");
CHECK(text_of(fixture,"file f; f.open(\"cut.txt\", \"r\"); string a=f.read(3); string p=\"\"+f.position; string b=f.read(1); return a+\"|\"+p+\"|\"+b;")=="ab\n|4|c");
CHECK(text_of(fixture,"file f; f.open(\"cut.txt\", \"r\"); f.seek(4); string a=f.read(4); return a+\"|\"+f.position+\"|\"+f.eof;")=="cd\r\n|9|true");
CHECK(text_of(fixture,"file f; f.open(\"cut.txt\", \"rb\"); return f.read(3)+\"|\"+f.position;")=="ab\r|3");
// Across the 64 KiB buffer: the "\r" is the block's last byte.
std::string bytes(65535,'a');
bytes.append("\r\nz");
put(fixture.root()/"edge.txt",bytes);
CHECK(text_of(fixture,"file f; f.open(\"edge.txt\", \"r\"); string a=f.read(65536); return a.byte_length()+\",\"+a.substr(65534)+\",\"+f.position+\",\"+f.read();")=="65536,a\n,65537,z");
CHECK(text_of(fixture,"file f; f.open(\"edge.txt\", \"r\"); string a=f.read_line(); return a.byte_length()+\",\"+f.position+\",\"+f.read_line();")=="65535,65537,z");
}

TEST_CASE("read_line returns each line without its end, then \"\" and eof")
{
ScriptFixture fixture;
CHECK(text_of(fixture,"write_file(\"l.txt\", \"x\\ny\"); file f; f.open(\"l.txt\", \"r\"); string a=f.read_line(); string b=f.read_line(); string c=f.read_line(); return a+\",\"+b+\",[\"+c+\"],\"+f.eof;")=="x,y,[],true");
CHECK(text_of(fixture,"write_file(\"l.txt\", \"one\\r\\ntwo\\r\\n\\r\\nthree\\r\"); file f; f.open(\"l.txt\", \"rb\"); string s; while(!f.eof)s+=\"[\"+f.read_line()+\"]\"; return s;")=="[one][two][][three\r]");
// A line longer than the buffer, with a "\r\n" cut by the block edge.
std::string bytes(70000,'q');
bytes[65535]='\r';
bytes.insert(65536,"\n");
bytes.append("\nlast");
put(fixture.root()/"long.txt",bytes);
CHECK(text_of(fixture,"file f; f.open(\"long.txt\", \"r\"); string a=f.read_line(); string b=f.read_line(); string c=f.read_line(); return a.byte_length()+\",\"+b.byte_length()+\",\"+c+\",\"+f.eof;")=="65535,4464,last,true");
}

TEST_CASE("the file object's misuse and failures have exact texts")
{
ScriptFixture fixture;
CHECK(error_of(fixture,"file f; f.read();")=="file is not open");
CHECK(error_of(fixture,"file f; f.read(3);")=="file is not open");
CHECK(error_of(fixture,"file f; f.read_line();")=="file is not open");
CHECK(error_of(fixture,"file f; f.write(\"x\");")=="file is not open");
CHECK(error_of(fixture,"file f; f.seek(0);")=="file is not open");
CHECK(error_of(fixture,"write_file(\"r.txt\", \"x\"); file f; f.open(\"r.txt\", \"r\"); f.close(); f.read();")=="file is not open");
CHECK(error_of(fixture,"write_file(\"r.txt\", \"x\"); file f; f.open(\"r.txt\", \"rb\"); f.write(\"y\");")=="file is not open for writing");
CHECK(error_of(fixture,"file f; f.open(\"r.txt\", \"rw\");")=="\"rw\" is not a file mode");
CHECK(error_of(fixture,"file f; f.open(\"r.txt\", \"w+\");")=="\"w+\" is not a file mode");
CHECK(error_of(fixture,"file f; f.open(\"r.txt\", \"\");")=="\"\" is not a file mode");
CHECK(text_of(fixture,"file f; bool opened=f.open(\"nope.txt\", \"r\"); return opened+\",\"+f.is_open+\",\"+f.error;")=="false,false,cannot read \"nope.txt\": file not found");
CHECK(text_of(fixture,"file f; f.open(\"nope.txt\", \"r+\"); return f.error;")=="cannot read \"nope.txt\": file not found");
CHECK(text_of(fixture,"file f; f.open(\"con\", \"w\"); return f.error;")=="\"con\" is not a valid file name");
CHECK(text_of(fixture,"file f; f.open(\"../x\", \"r\"); return f.error;")=="\"../x\" leaves its mount");
CHECK(text_of(fixture,"file f; f.open(\"\", \"r\"); return f.error;")=="cannot read \"\": is a directory");
// A closed file: no size, no position, at its end; seeking outside the file changes nothing.
CHECK(text_of(fixture,"file f; return f.size+\",\"+f.position+\",\"+f.eof+\",\"+f.is_open;")=="0,0,true,false");
CHECK(text_of(fixture,"write_file(\"s.txt\", \"abc\"); file f; f.open(\"s.txt\", \"r\"); f.seek(1); bool a=f.seek(-1); bool b=f.seek(4); bool c=f.seek(3); return a+\",\"+b+\",\"+c+\",\"+f.position;")=="false,false,true,3");
CHECK(text_of(fixture,"write_file(\"s.txt\", \"abc\"); file f; f.open(\"s.txt\", \"r\"); return \"[\"+f.read(0)+f.read(-5)+\"]\"+f.read(100)+\"[\"+f.read()+f.read(1)+f.read_line()+\"]\";")=="[]abc[]");
// Opening again closes the first file.
CHECK(text_of(fixture,"write_file(\"1.txt\", \"one\"); write_file(\"2.txt\", \"two\"); file f; f.open(\"1.txt\", \"r\"); f.read(1); f.open(\"2.txt\", \"r\"); return f.read()+\",\"+f.position;")=="two,3");
}

TEST_CASE("read_file returns the bytes or throws")
{
ScriptFixture fixture;
CHECK(error_of(fixture,"read_file(\"missing.txt\");")=="cannot read \"missing.txt\": file not found");
CHECK(error_of(fixture,"read_file(\"sounds/\");")=="cannot read \"sounds/\": is a directory");
CHECK(error_of(fixture,"read_file(\"nul.txt\");")=="\"nul.txt\" is not a valid file name");
put(fixture.root()/"b.bin",std::string("\r\n\0\xFF",4));
CHECK(text_of(fixture,"return read_file(\"b.bin\");")==std::string("\r\n\0\xFF",4));
put(fixture.root()/"empty.txt","");
CHECK(text_of(fixture,"return \"[\"+read_file(\"empty.txt\")+\"]\";")=="[]");
// BGT-style backslashes and any case.
put(fixture.root()/"Sounds"/"Shot.ogg","bang");
CHECK(text_of(fixture,"return read_file(\"sounds\\\\shot.OGG\");")=="bang");
}

TEST_CASE("the bool helpers set last_file_error on failure and clear it on success")
{
ScriptFixture fixture;
CHECK(int64_of(fixture,"return file_size(\"missing\");")==-1);
CHECK(text_of(fixture,"bool written=write_file(\"con\", \"x\"); return written+\",\"+last_file_error();")=="false,\"con\" is not a valid file name");
CHECK(text_of(fixture,"write_file(\"con\", \"x\"); bool written=write_file(\"ok.txt\", \"x\"); return written+\",[\"+last_file_error()+\"]\";")=="true,[]");
CHECK(text_of(fixture,"bool deleted=delete_file(\"gone.txt\"); return deleted+\",\"+last_file_error();")=="false,cannot read \"gone.txt\": file not found");
// Queries never touch it.
CHECK(text_of(fixture,"delete_file(\"gone.txt\"); file_exists(\"ok.txt\"); directory_exists(\"x\"); file_size(\"ok.txt\"); return last_file_error();")=="cannot read \"gone.txt\": file not found");
CHECK(text_of(fixture,"return file_exists(\"ok.txt\")+\",\"+file_exists(\"OK.TXT\")+\",\"+directory_exists(\"ok.txt\")+\",\"+file_exists(\"\")+\",\"+directory_exists(\"\")+\",\"+file_exists(\"con\");")=="true,true,false,false,true,false");
CHECK(int64_of(fixture,"return file_size(\"ok.txt\");")==1);
CHECK(int64_of(fixture,"create_directory(\"d\"); return file_size(\"d\");")==-1);
CHECK(text_of(fixture,"bool a=append_file(\"ap.txt\", \"one\"); bool b=append_file(\"ap.txt\", \"two\"); return a+\",\"+b+\",\"+read_file(\"ap.txt\");")=="true,true,onetwo");
CHECK(text_of(fixture,"write_file(\"ap.txt\", \"x\"); return read_file(\"ap.txt\");")=="x");
CHECK(text_of(fixture,"bool deleted=delete_file(\"ap.txt\"); return deleted+\",\"+file_exists(\"ap.txt\")+\",[\"+last_file_error()+\"]\";")=="true,false,[]");
CHECK(text_of(fixture,"bool written=write_file(\"no/such/folder.txt\", \"x\"); return written+\",\"+last_file_error();")=="false,cannot read \"no/such/folder.txt\": file not found");
}

TEST_CASE("folders are made, listed and removed")
{
ScriptFixture fixture;
CHECK(holds(fixture,"return create_directory(\"a/b/c\")&&directory_exists(\"A/B/c\")&&create_directory(\"a/b/c\");"));
CHECK(text_of(fixture,"bool made=create_directory(\"x/y\", false); return made+\",\"+last_file_error();")=="false,cannot read \"x/y\": file not found");
CHECK(holds(fixture,"return create_directory(\"x\", false)&&create_directory(\"x/y\", false);"));
CHECK(text_of(fixture,"write_file(\"a/b/f.txt\", \"x\"); bool removed=delete_directory(\"a\"); return removed+\",\"+last_file_error();")=="false,cannot read \"a\": directory not empty");
CHECK(holds(fixture,"return delete_directory(\"a\", true)&&!directory_exists(\"a\")&&delete_directory(\"x/y\")&&last_file_error()==\"\";"));
CHECK(text_of(fixture,"return join(list_directories(\"\"), \"|\");")=="x");
}

TEST_CASE("list_files and list_directories give sorted names")
{
ScriptFixture fixture;
// Made before the Vfs first lists the folder: a change made by another process may take FolderSource's racy window to be seen.
put(fixture.root()/"a.txt","1");
put(fixture.root()/"B.txt","2");
put(fixture.root()/"c.log","3");
put(fixture.root()/"sub"/"inner.txt","4");
fs::create_directories(fixture.root()/"Zeta");
CHECK(text_of(fixture,"return join(list_files(\"\", \"*.TXT\"), \"|\");")=="a.txt|B.txt");
CHECK(text_of(fixture,"return join(list_files(\"/\"), \"|\");")=="a.txt|B.txt|c.log");
CHECK(text_of(fixture,"return join(list_files(\"\", \"?.*\"), \"|\");")=="a.txt|B.txt|c.log");
CHECK(text_of(fixture,"return join(list_directories(\"\"), \"|\");")=="sub|Zeta");
CHECK(text_of(fixture,"create_directory(\"x\"); return join(list_directories(\"/\"), \"|\");")=="sub|x|Zeta");
CHECK(text_of(fixture,"return join(list_files(\"SUB\"), \"|\");")=="inner.txt");
CHECK(text_of(fixture,"array<string>@ names=list_files(\"nowhere\"); return names.length()+\",\"+last_file_error();")=="0,cannot read \"nowhere\": file not found");
CHECK(text_of(fixture,"list_files(\"nowhere\"); list_files(\"\"); return \"[\"+last_file_error()+\"]\";")=="[]");
}

TEST_CASE("copy_file and move_file overwrite, within and across / and data:/")
{
ScriptFixture fixture;
CHECK(holds(fixture,"return write_file(\"src.txt\", \"payload\")&&copy_file(\"src.txt\", \"data:/saved.txt\")&&read_file(\"data:/saved.txt\")==\"payload\"&&read_file(\"src.txt\")==\"payload\";"));
CHECK(contents(fixture.data()/"saved.txt")=="payload");
// Overwriting a longer target cuts it to length.
CHECK(holds(fixture,"return write_file(\"long.txt\", \"a much longer text\")&&copy_file(\"src.txt\", \"long.txt\")&&read_file(\"long.txt\")==\"payload\";"));
// Onto itself, under another spelling: kept whole.
CHECK(holds(fixture,"return copy_file(\"src.txt\", \"SRC.TXT\")&&read_file(\"src.txt\")==\"payload\";"));
CHECK(holds(fixture,"return move_file(\"src.txt\", \"Src.txt\")&&read_file(\"src.txt\")==\"payload\";"));
// Across sources: a copy, then a delete.
CHECK(holds(fixture,"return move_file(\"data:/saved.txt\", \"back.txt\")&&!file_exists(\"data:/saved.txt\")&&read_file(\"back.txt\")==\"payload\";"));
CHECK_FALSE(fs::exists(fixture.data()/"saved.txt"));
CHECK(holds(fixture,"return move_file(\"back.txt\", \"data:/again.txt\")&&!file_exists(\"back.txt\")&&read_file(\"data:/again.txt\")==\"payload\";"));
// Within one source: a rename, over an existing file, into another folder, and seen at once by listings.
CHECK(holds(fixture,"return write_file(\"old.txt\", \"new bytes\")&&create_directory(\"dir\")&&write_file(\"dir/target.txt\", \"stale\")&&join(list_files(\"dir\"), \"|\")==\"target.txt\";"));
CHECK(holds(fixture,"return move_file(\"old.txt\", \"DIR/Target.txt\")&&!file_exists(\"old.txt\")&&read_file(\"dir/target.txt\")==\"new bytes\"&&join(list_files(\"dir\"), \"|\")==\"target.txt\";"));
CHECK(contents(fixture.root()/"dir"/"target.txt")=="new bytes");
CHECK(holds(fixture,"return move_file(\"dir/target.txt\", \"dir/renamed.txt\")&&join(list_files(\"dir\"), \"|\")==\"renamed.txt\";"));
// Failures name the path that failed.
CHECK(text_of(fixture,"bool copied=copy_file(\"none.txt\", \"x.txt\"); return copied+\",\"+last_file_error();")=="false,cannot read \"none.txt\": file not found");
CHECK(text_of(fixture,"bool moved=move_file(\"none.txt\", \"x.txt\"); return moved+\",\"+last_file_error();")=="false,cannot read \"none.txt\": file not found");
CHECK(text_of(fixture,"bool copied=copy_file(\"data:/again.txt\", \"aux.txt\"); return copied+\",\"+last_file_error();")=="false,\"aux.txt\" is not a valid file name");
CHECK(text_of(fixture,"bool moved=move_file(\"dir\", \"x.txt\"); return moved+\",\"+last_file_error();")=="false,cannot read \"dir\": is a directory");
CHECK(text_of(fixture,"bool moved=move_file(\"data:/again.txt\", \"dir\"); return moved+\",\"+last_file_error();")=="false,cannot read \"dir\": is a directory");
CHECK(holds(fixture,"return file_exists(\"data:/again.txt\");"));
}

TEST_CASE("Vfs::rename renames within one source only, and never out of a read-only mount")
{
ScriptFixture fixture;
put(fixture.root()/"a.txt","a");
put(fixture.folder/"ro"/"x.txt","x");
REQUIRE(fixture.vfs.mount_folder("ro",fixture.folder/"ro",false));
const auto within=fixture.vfs.rename("a.txt","b.txt");
REQUIRE(within.has_value());
CHECK(*within);
CHECK(contents(fixture.root()/"b.txt")=="a");
CHECK_FALSE(fs::exists(fixture.root()/"a.txt"));
const auto across=fixture.vfs.rename("b.txt","data:/b.txt");
REQUIRE(across.has_value());
CHECK_FALSE(*across);
CHECK(fs::exists(fixture.root()/"b.txt"));
const auto read_only=fixture.vfs.rename("ro/x.txt","y.txt");
REQUIRE_FALSE(read_only.has_value());
CHECK(read_only.error().code==zodiac::vfs::FileError::read_only_pack);
CHECK(text_of(fixture,"bool moved=move_file(\"ro/x.txt\", \"y.txt\"); return moved+\",\"+file_exists(\"ro/x.txt\")+\",\"+file_exists(\"y.txt\")+\",\"+last_file_error();")=="false,true,false,\"ro/x.txt\" is in a read-only pack; save files in data:/ instead");
CHECK(text_of(fixture,"bool copied=copy_file(\"ro/x.txt\", \"y.txt\"); return copied+\",\"+read_file(\"y.txt\");")=="true,x");
}

TEST_CASE("file_modified is local civil time, on date_time::now()'s clock")
{
ScriptFixture fixture;
REQUIRE(holds(fixture,"return write_file(\"m.txt\", \"x\");"));
const auto info=fixture.vfs.stat("m.txt");
REQUIRE(info.has_value());
// date_time() is 1970-01-01 00:00, so the difference is the civil milliseconds themselves.
CHECK(int64_of(fixture,"return file_modified(\"m.txt\")-date_time();")==zodiac::platform::utc_to_local_civil_ms(info->modified_utc_ms));
// Written just before: within seconds of now() (not of utc_now(), unless the zone is UTC).
const std::int64_t behind=int64_of(fixture,"return date_time::now()-file_modified(\"m.txt\");");
CHECK(behind>-5000);
CHECK(behind<60000);
CHECK(int64_of(fixture,"create_directory(\"folder\"); return date_time::now()-file_modified(\"folder\");")<60000);
CHECK(error_of(fixture,"file_modified(\"missing.txt\");")=="cannot read \"missing.txt\": file not found");
CHECK(error_of(fixture,"file_modified(\"prn\");")=="\"prn\" is not a valid file name");
}

TEST_CASE("reads at the buffer's edges give the file's bytes")
{
ScriptFixture fixture;
for(const std::size_t size:{std::size_t{0},std::size_t{1},std::size_t{65535},std::size_t{65536},std::size_t{65537},std::size_t{3*65536+7}}){
CAPTURE(size);
const std::string bytes=pattern(size);
put(fixture.root()/"edge.bin",bytes);
CHECK(text_of(fixture,"return read_file(\"edge.bin\");")==bytes);
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); return f.read();")==bytes);
// In small pieces, through the buffer.
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); string s; while(!f.eof)s+=f.read(1000); return s;")==bytes);
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); string s; while(!f.eof)s+=f.read(65536); return s;")==bytes);
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); return f.read(7)+f.read(70000)+f.read();")==bytes);
}
const std::string bytes=pattern(3*65536+7);
put(fixture.root()/"edge.bin",bytes);
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); f.seek(65535); return f.read(3);")==bytes.substr(65535,3));
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); f.seek(65536); string a=f.read(1); f.seek(65535); return a+f.read(2);")==bytes.substr(65536,1)+bytes.substr(65535,2));
// One read over three blocks, after a small one filled the buffer.
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); f.read(10); f.seek(65530); return f.read(2*65536+20);")==bytes.substr(65530,2*65536+20));
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"rb\"); f.seek(3*65536); return f.read(100)+\"|\"+f.eof;")==bytes.substr(3*65536)+"|true");
// Reads after writes see the written bytes, in the buffer and beyond it.
CHECK(text_of(fixture,"file f; f.open(\"edge.bin\", \"r+b\"); f.read(5); f.seek(2); f.write(\"XYZ\"); f.seek(0); string head=f.read(6); f.seek(f.size); f.write(\"tail\"); f.seek(f.size-6); return head+\"|\"+f.read();")==bytes.substr(0,2)+"XYZ"+bytes.substr(5,1)+"|"+bytes.substr(bytes.size()-2)+"tail");
CHECK(text_of(fixture,"file f; f.open(\"grow.bin\", \"wb\"); f.write(\"ab\"); f.seek(0); string a=f.read(1); f.seek(2); f.write(\"cd\"); f.seek(1); return a+f.read();")=="abcd");
}

TEST_CASE("a sparse file over 4 GiB is read at its end")
{
ScriptFixture fixture;
const fs::path file=fixture.root()/"big.bin";
const RemoveAtExit cleanup(file);
constexpr std::uint64_t offset=5368709120;
make_sparse(file,offset,"END");
CHECK(int64_of(fixture,"return file_size(\"big.bin\");")==5368709123);
CHECK(text_of(fixture,"file f; f.open(\"big.bin\", \"rb\"); string s=f.size+\",\"; f.seek(5368709120); s+=f.read(3); return s+\",\"+f.position+\",\"+f.eof;")=="5368709123,END,5368709123,true");
CHECK(text_of(fixture,"file f; f.open(\"big.bin\", \"rb\"); f.seek(5368709119); return f.read(10);")==std::string("\0END",4));
}
