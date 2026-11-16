// pe_imported_dlls on images this test generates (every host) and, on Windows, on the vendor_dep fixture module the build produces.
#include <core/path_utf8.hpp>
#include <core/pe_imports.hpp>
#include <doctest/doctest.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
namespace fs=std::filesystem;
using Names=std::vector<std::string>;
using zodiac::pe_imported_dlls_in;

constexpr std::size_t kLfanew=0x40;
constexpr std::size_t kFileHeader=kLfanew+4;
constexpr std::size_t kOptional=kFileHeader+20;
constexpr std::size_t kOptionalSize=240;
constexpr std::size_t kDirectories=kOptional+112;
constexpr std::size_t kSectionTable=kOptional+kOptionalSize;
constexpr std::size_t kSectionOffset=0x200;
constexpr uint32_t kSectionRva=0x1000;
constexpr std::size_t kImportDescriptorSize=20;
constexpr std::size_t kDelayDescriptorSize=32;

void put(std::string& image,std::size_t at,uint64_t value,std::size_t size)
{
for(std::size_t i=0;i<size;++i)image[at+i]=static_cast<char>((value>>(8*i))&0xFFu);
}

void append(std::string& data,uint64_t value,std::size_t size)
{
data.append(size,'\0');
put(data,data.size()-size,value,size);
}

enum class Unterminated{none,imports,delay};

struct PeSpec{
Names imports;
Names delay;
// The table that lacks its terminating descriptor; it is laid out last, so its walk runs into the end of the section.
Unterminated unterminated=Unterminated::none;
};

// A minimal PE32+ file: DOS header, PE signature, file header, a 240-byte optional header with 16 data directories and one section at
// file offset 0x200 / RVA 0x1000 whose raw data (exactly as long as its content, ending the file) holds the import descriptors, the delay
// descriptors, then the names, so the last name's NUL is the file's last byte; an unterminated table comes after the names instead, ending
// the file. An empty list leaves its directory at RVA 0. Descriptors point at their names after the layout is known.
std::string make_pe(const PeSpec& spec)
{
std::string data;
std::size_t import_at=0;
std::size_t delay_at=0;
const auto add_imports=[&]{
if(spec.imports.empty())return;
import_at=data.size();
for(std::size_t i=0;i<spec.imports.size();++i){
append(data,0,12);
append(data,0,4);
append(data,kSectionRva,4);
}
if(spec.unterminated!=Unterminated::imports)append(data,0,kImportDescriptorSize);
};
const auto add_delay=[&]{
if(spec.delay.empty())return;
delay_at=data.size();
for(std::size_t i=0;i<spec.delay.size();++i){
append(data,1,4);
append(data,0,4);
append(data,kSectionRva,4);
append(data,kSectionRva,4);
append(data,kSectionRva,4);
append(data,0,12);
}
if(spec.unterminated!=Unterminated::delay)append(data,0,kDelayDescriptorSize);
};
if(spec.unterminated!=Unterminated::imports)add_imports();
if(spec.unterminated!=Unterminated::delay)add_delay();
const auto add_names=[&data](const Names& names){
std::vector<uint32_t> rvas;
for(const std::string& name:names){
rvas.push_back(static_cast<uint32_t>(kSectionRva+data.size()));
data+=name;
data.push_back('\0');
}
return rvas;
};
const std::vector<uint32_t> import_names=add_names(spec.imports);
const std::vector<uint32_t> delay_names=add_names(spec.delay);
if(spec.unterminated!=Unterminated::none){
while(data.size()%8)data.push_back('\0');
if(spec.unterminated==Unterminated::imports)add_imports();
else add_delay();
}
for(std::size_t i=0;i<import_names.size();++i)put(data,import_at+i*kImportDescriptorSize+12,import_names[i],4);
for(std::size_t i=0;i<delay_names.size();++i)put(data,delay_at+i*kDelayDescriptorSize+4,delay_names[i],4);
const uint32_t import_rva=spec.imports.empty()?0:static_cast<uint32_t>(kSectionRva+import_at);
const uint32_t delay_rva=spec.delay.empty()?0:static_cast<uint32_t>(kSectionRva+delay_at);
std::string image(kSectionOffset,'\0');
image[0]='M';
image[1]='Z';
put(image,0x3C,kLfanew,4);
image.replace(kLfanew,4,std::string_view("PE\0\0",4));
put(image,kFileHeader,0x8664,2);
put(image,kFileHeader+2,1,2);
put(image,kFileHeader+16,kOptionalSize,2);
put(image,kFileHeader+18,0x2022,2);
put(image,kOptional,0x20B,2);
put(image,kOptional+24,0x180000000ull,8);
put(image,kOptional+108,16,4);
put(image,kDirectories+1*8,import_rva,4);
put(image,kDirectories+1*8+4,import_names.size()*kImportDescriptorSize,4);
put(image,kDirectories+13*8,delay_rva,4);
put(image,kDirectories+13*8+4,delay_names.size()*kDelayDescriptorSize,4);
image.replace(kSectionTable,6,".idata");
put(image,kSectionTable+8,data.size(),4);
put(image,kSectionTable+12,kSectionRva,4);
put(image,kSectionTable+16,data.size(),4);
put(image,kSectionTable+20,kSectionOffset,4);
put(image,kSectionTable+36,0x40000040u,4);
image+=data;
return image;
}

const PeSpec kVendorSpec{{"vendor_lib.dll","KERNEL32.dll"},{"VENDOR_LIB.DLL","late.dll","kernel32.DLL"}};
const Names kVendorNames{"vendor_lib.dll","KERNEL32.dll","late.dll"};

uint32_t get32(const std::string& image,std::size_t at)
{
uint32_t value=0;
for(std::size_t i=0;i<4;++i)value|=static_cast<uint32_t>(static_cast<unsigned char>(image[at+i]))<<(8*i);
return value;
}

// Every name the contract allows: 1 to 255 printable ASCII characters.
bool names_are_plain(const Names& names)
{
for(const std::string& name:names){
if(name.empty()||name.size()>255)return false;
for(const char c:name)if(c<0x20||c>0x7E)return false;
}
return true;
}

// Parses cut(length) for length = 0..last (every length up to dense, then every step-th, then last) and checks that the results fail below
// some length and equal full from it on. Returns that length (last+1 when even cut(last) fails).
template<typename Cut>
std::size_t check_cuts(const Names& full,std::size_t last,std::size_t dense,std::size_t step,Cut cut)
{
std::optional<std::size_t> threshold;
for(std::size_t length=0;;length=length<dense?length+1:std::min(last,length+step)){
const std::optional<Names> result=pe_imported_dlls_in(cut(length));
if(!threshold&&result)threshold=length;
if(threshold)CHECK_MESSAGE(result==full,"cut at ",length," bytes");
if(length==last)break;
}
return threshold.value_or(last+1);
}

// Every prefix of image: returns the shortest that parses (to the full result).
std::size_t check_prefixes(const std::string& image,std::size_t dense,std::size_t step)
{
const std::optional<Names> full=pe_imported_dlls_in(image);
REQUIRE(full);
return check_cuts(*full,image.size(),dense,step,[&image](std::size_t length){return std::string_view(image).substr(0,length);});
}

// The section headers of a well-formed image: the file offset of each header.
std::vector<std::size_t> section_headers(const std::string& image)
{
const std::size_t file_header=get32(image,0x3C)+4;
const std::size_t count=static_cast<unsigned char>(image[file_header+2])|static_cast<std::size_t>(static_cast<unsigned char>(image[file_header+3]))<<8;
const std::size_t optional_size=static_cast<unsigned char>(image[file_header+16])|static_cast<std::size_t>(static_cast<unsigned char>(image[file_header+17]))<<8;
std::vector<std::size_t> headers;
for(std::size_t i=0;i<count;++i)headers.push_back(file_header+20+optional_size+i*40);
return headers;
}

// The import data cut short inside the file: for the section holding rva, every raw size from 0 to its own, so the walk itself meets the end
// (the up-front check that raw data lies inside the file passes). Fails below some size, gives the full result from it on; returns that size.
std::size_t check_section_cuts(std::string image,uint32_t rva)
{
const std::optional<Names> full=pe_imported_dlls_in(image);
REQUIRE(full);
for(const std::size_t header:section_headers(image)){
const uint32_t start=get32(image,header+12);
const uint32_t raw_size=get32(image,header+16);
if(rva<start||rva-start>=raw_size)continue;
const std::size_t threshold=check_cuts(*full,raw_size,raw_size,1,[&image,header](std::size_t length)->std::string_view{
put(image,header+16,length,4);
return image;
});
put(image,header+16,raw_size,4);
return threshold;
}
FAIL("no section holds RVA ",rva);
return 0;
}

class TempDir{
public:
TempDir()
{
static std::atomic<unsigned> counter{0};
dir_=fs::temp_directory_path()/("zodiac_pe_test_"+std::to_string(counter.fetch_add(1))+"_"+std::to_string(reinterpret_cast<std::uintptr_t>(this)));
fs::create_directories(dir_);
}
~TempDir()
{
std::error_code ec;
fs::remove_all(dir_,ec);
}
TempDir(const TempDir&)=delete;
TempDir& operator=(const TempDir&)=delete;
fs::path write(const fs::path& name,std::string_view bytes) const
{
const fs::path path=dir_/name;
std::ofstream out(path,std::ios::binary);
out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
return path;
}
private:
fs::path dir_;
};
}

namespace zodiac {
TEST_CASE("import and delay-import names in table order, duplicates left out case-insensitively")
{
CHECK(pe_imported_dlls_in(make_pe(kVendorSpec))==kVendorNames);
CHECK(pe_imported_dlls_in(make_pe({{"a.dll"},{}}))==Names{"a.dll"});
CHECK(pe_imported_dlls_in(make_pe({{},{"b.dll"}}))==Names{"b.dll"});
CHECK(pe_imported_dlls_in(make_pe({}))==Names{});
}

TEST_CASE("every truncation of a generated image fails, the whole image does not")
{
const std::string image=make_pe(kVendorSpec);
// The section's raw data ends the file, so every byte is needed.
CHECK(check_prefixes(image,image.size(),1)==image.size());
// Cut inside the file instead: the walk meets the section's end in the descriptors, the names, and the last name's NUL.
const uint32_t section_size=get32(image,kSectionTable+16);
CHECK(check_section_cuts(image,get32(image,kDirectories+1*8))==section_size);
CHECK(check_section_cuts(image,get32(image,kDirectories+13*8))==section_size);
// The same with every import in the import directory, then every import in the delay-import directory: its own array and names only.
for(const PeSpec& spec:{PeSpec{{"vendor_lib.dll","KERNEL32.dll"},{}},PeSpec{{},{"vendor_lib.dll","KERNEL32.dll"}}}){
const std::string single=make_pe(spec);
const uint32_t rva=get32(single,kDirectories+(spec.imports.empty()?13:1)*8);
CHECK(check_section_cuts(single,rva)==get32(single,kSectionTable+16));
}
}

TEST_CASE("damaged headers are refused")
{
const std::string valid=make_pe(kVendorSpec);
const auto with=[&valid](std::size_t at,uint64_t value,std::size_t size){
std::string image=valid;
put(image,at,value,size);
return pe_imported_dlls_in(image);
};
CHECK_FALSE(with(0,'Z',1));
CHECK_FALSE(with(0x3C,valid.size(),4));
CHECK_FALSE(with(0x3C,valid.size()-20,4));
CHECK_FALSE(with(0x3C,0xFFFFFFF0u,4));
CHECK_FALSE(with(kLfanew+2,'X',1));
CHECK_FALSE(with(kOptional,0x10B,2));
CHECK_FALSE(with(kFileHeader+16,111,2));
CHECK_FALSE(with(kFileHeader+16,0xFFFF,2));
CHECK_FALSE(with(kFileHeader+2,0,2));
CHECK_FALSE(with(kFileHeader+2,0xFFFF,2));
// Raw data past the end of the file, or a pointer that wraps around.
CHECK_FALSE(with(kSectionTable+16,valid.size(),4));
CHECK_FALSE(with(kSectionTable+20,0xFFFFFFFFu,4));
// The data directory count decides which directories exist: 1 has neither, 2 only the import directory.
CHECK(with(kOptional+108,1,4)==Names{});
CHECK(with(kOptional+108,2,4)==Names{"vendor_lib.dll","KERNEL32.dll"});
CHECK(with(kOptional+108,0xFFFFFFFFu,4)==kVendorNames);
// Directories the optional header is too short to hold, while the count says they exist.
{
std::string image=valid;
put(image,kFileHeader+16,112+8,2);
CHECK_FALSE(pe_imported_dlls_in(image));
}
}

TEST_CASE("RVAs outside the section's raw data are refused")
{
const std::string valid=make_pe(kVendorSpec);
const std::size_t import_dir=kDirectories+1*8;
const std::size_t delay_dir=kDirectories+13*8;
const uint32_t section_size=get32(valid,kSectionTable+16);
const auto with=[&valid](std::size_t at,uint64_t value){
std::string image=valid;
put(image,at,value,4);
return pe_imported_dlls_in(image);
};
CHECK_FALSE(with(import_dir,kSectionRva-1));
CHECK_FALSE(with(import_dir,kSectionRva+section_size));
CHECK_FALSE(with(import_dir,0xFFFFFFFFu));
CHECK_FALSE(with(delay_dir,kSectionRva+section_size));
// A descriptor that starts inside the section but does not fit in it.
CHECK_FALSE(with(import_dir,kSectionRva+section_size-kImportDescriptorSize+1));
CHECK_FALSE(with(delay_dir,kSectionRva+section_size-kDelayDescriptorSize+1));
// A virtual size shorter than the raw data bounds the section.
CHECK_FALSE(with(kSectionTable+8,section_size-1));
CHECK(with(kSectionTable+8,0)==kVendorNames);
// A name RVA of the first import descriptor outside the section.
const uint32_t first_import=get32(valid,import_dir)-kSectionRva+kSectionOffset;
CHECK_FALSE(with(first_import+12,kSectionRva+section_size));
CHECK_FALSE(with(first_import+12,0x7FFFFFFFu));
}

TEST_CASE("names must be NUL-terminated, plain and short")
{
CHECK_FALSE(pe_imported_dlls_in(make_pe({{""},{}})));
CHECK_FALSE(pe_imported_dlls_in(make_pe({{"tab\t.dll"},{}})));
CHECK_FALSE(pe_imported_dlls_in(make_pe({{"\xC3\xBC.dll"},{}})));
CHECK_FALSE(pe_imported_dlls_in(make_pe({{std::string(256,'a')},{}})));
CHECK(pe_imported_dlls_in(make_pe({{std::string(255,'a')},{}}))==Names{std::string(255,'a')});
CHECK(pe_imported_dlls_in(make_pe({{"with space.dll"},{}}))==Names{"with space.dll"});
// The last name of the section without its NUL: the section's raw data, and the file, end right after it.
for(const PeSpec& spec:{PeSpec{{"a.dll"},{}},PeSpec{{},{"b.dll"}}}){
std::string image=make_pe(spec);
REQUIRE(image.back()=='\0');
image.back()='x';
CHECK_FALSE(pe_imported_dlls_in(image));
// Inside a larger file the section still bounds the name.
image.append(16,'\0');
CHECK_FALSE(pe_imported_dlls_in(image));
}
}

TEST_CASE("descriptor arrays must end inside the section, after at most 65536 entries")
{
CHECK_FALSE(pe_imported_dlls_in(make_pe({{"a.dll","b.dll"},{"c.dll"},Unterminated::imports})));
CHECK_FALSE(pe_imported_dlls_in(make_pe({{"a.dll"},{"c.dll","d.dll"},Unterminated::delay})));
// An import descriptor with FirstThunk 0 ends the array, as the loader's walk does.
{
std::string image=make_pe({{"a.dll","b.dll"},{}});
const uint32_t descriptor=get32(image,kDirectories+1*8)-kSectionRva+kSectionOffset;
put(image,descriptor+kImportDescriptorSize+16,0,4);
CHECK(pe_imported_dlls_in(image)==Names{"a.dll"});
}
CHECK(pe_imported_dlls_in(make_pe({Names(65536,"same.dll"),{}}))==Names{"same.dll"});
CHECK_FALSE(pe_imported_dlls_in(make_pe({Names(65537,"same.dll"),{}})));
CHECK(pe_imported_dlls_in(make_pe({{},Names(65536,"same.dll")}))==Names{"same.dll"});
CHECK_FALSE(pe_imported_dlls_in(make_pe({{},Names(65537,"same.dll")})));
}

TEST_CASE("random bytes and random damage never crash")
{
std::mt19937 random(20261116u);
std::uniform_int_distribution<int> byte(0,255);
for(int round=0;round<2000;++round){
std::string bytes(std::uniform_int_distribution<std::size_t>(0,4096)(random),'\0');
for(char& c:bytes)c=static_cast<char>(byte(random));
// Half of them get a DOS header and a PE signature, so the parse goes past the first checks.
if(round%2&&bytes.size()>=kOptional+2){
bytes[0]='M';
bytes[1]='Z';
put(bytes,0x3C,kLfanew,4);
bytes.replace(kLfanew,4,std::string_view("PE\0\0",4));
}
CHECK_FALSE(pe_imported_dlls_in(bytes));
}
const std::string valid=make_pe(kVendorSpec);
std::uniform_int_distribution<std::size_t> position(0,valid.size()-1);
for(int round=0;round<20000;++round){
std::string image=valid;
const int changes=1+round%4;
for(int i=0;i<changes;++i)image[position(random)]=static_cast<char>(byte(random));
const std::optional<Names> result=pe_imported_dlls_in(image);
if(result)CHECK(names_are_plain(*result));
}
}

TEST_CASE("pe_imported_dlls reads a file and names it in its errors")
{
const TempDir dir;
const fs::path good=dir.write(u8"modül.dll",make_pe(kVendorSpec));
CHECK(pe_imported_dlls(good)==kVendorNames);
const fs::path text=dir.write("notes.dll","not a DLL\n");
const auto refused=pe_imported_dlls(text);
REQUIRE_FALSE(refused);
CHECK(refused.error()=="error: \""+path_to_utf8(text)+"\" is not a valid Windows DLL");
const fs::path missing=text.parent_path()/"missing.dll";
const auto unread=pe_imported_dlls(missing);
REQUIRE_FALSE(unread);
CHECK(unread.error()=="error: cannot read \""+path_to_utf8(missing)+"\"");
}

#ifdef ZODIAC_TEST_VENDOR_DEP
TEST_CASE("the vendor_dep fixture module imports KERNEL32.dll and delay-imports vendor_lib.dll")
{
const auto names=pe_imported_dlls(fs::path(ZODIAC_TEST_VENDOR_DEP));
REQUIRE(names);
REQUIRE_FALSE(names->empty());
CHECK(names->front()=="KERNEL32.dll");
CHECK(names->back()=="vendor_lib.dll");
CHECK(names_are_plain(*names));
std::ifstream in(fs::path(ZODIAC_TEST_VENDOR_DEP),std::ios::binary);
const std::string image((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
const bool read=!in.bad();
REQUIRE(read);
// Every prefix through the headers, then every 61st: all fail up to the end of the last section's raw data, all parse after it.
const std::size_t headers_end=section_headers(image).back()+40;
CHECK(check_prefixes(image,headers_end+1,61)>headers_end);
// Each directory's section cut at every length up to its raw size: the walk fails until the section holds all the import data.
const std::size_t file_header=get32(image,0x3C)+4;
const std::size_t directories=file_header+20+112;
for(const std::size_t index:{std::size_t{1},std::size_t{13}}){
const uint32_t rva=get32(image,directories+index*8);
REQUIRE(rva!=0);
CHECK(check_section_cuts(image,rva)>0);
}
}
#endif
}
