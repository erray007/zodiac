// zodiac::pe_imported_dlls (see core/pe_imports.hpp): a bounds-checked walk of a PE32+ file's import and delay-import directories.
#include <core/pe_imports.hpp>
#include <core/path_utf8.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <unordered_set>
#include <utility>

namespace zodiac {
namespace {
constexpr std::size_t kDosHeaderSize=0x40;
constexpr std::size_t kLfanewOffset=0x3C;
constexpr std::size_t kSignatureSize=4;
constexpr std::size_t kFileHeaderSize=20;
constexpr std::size_t kSectionCountOffset=2;
constexpr std::size_t kOptionalSizeOffset=16;
constexpr uint16_t kMachineAmd64=0x8664;
constexpr std::size_t kMagicSize=2;
constexpr uint16_t kPe32Magic=0x10B;
constexpr uint16_t kPe32PlusMagic=0x20B;
constexpr std::size_t kDirectoryCountOffset=108;
constexpr std::size_t kDirectoriesOffset=112;
constexpr std::size_t kDirectorySize=8;
constexpr uint32_t kMaxDirectories=16;
constexpr std::size_t kImportDirectory=1;
constexpr std::size_t kDelayImportDirectory=13;
constexpr std::size_t kSectionHeaderSize=40;
constexpr std::size_t kImportDescriptorSize=20;
constexpr std::size_t kImportNameOffset=12;
constexpr std::size_t kImportFirstThunkOffset=16;
constexpr std::size_t kDelayDescriptorSize=32;
constexpr std::size_t kDelayNameOffset=4;
constexpr std::size_t kMaxDescriptors=65536;
constexpr std::size_t kMaxNameLength=255;

// Little-endian reads; the caller has checked that the bytes exist.
uint16_t read16(std::string_view bytes,std::size_t at) noexcept
{
return static_cast<uint16_t>(static_cast<unsigned char>(bytes[at])|static_cast<unsigned char>(bytes[at+1])<<8);
}

uint32_t read32(std::string_view bytes,std::size_t at) noexcept
{
return static_cast<uint32_t>(read16(bytes,at))|static_cast<uint32_t>(read16(bytes,at+2))<<16;
}

// True when [at, at+size) lies inside a buffer of length total, without overflow.
bool fits(uint64_t at,uint64_t size,std::size_t total) noexcept
{
return at<=total&&size<=total-at;
}

// A section's mapped part that is backed by raw data: RVAs [rva, rva+size) are the file bytes [offset, offset+size).
struct Section{
uint32_t rva;
uint32_t size;
uint32_t offset;
};

// The checked headers of a PE32+ file, with its sections sorted and disjoint so an RVA maps through one of them.
class Image{
public:
static std::expected<Image,PeError> parse(std::string_view bytes)
{
const std::unexpected<PeError> invalid(PeError::invalid);
if(bytes.size()<kDosHeaderSize||bytes[0]!='M'||bytes[1]!='Z')return invalid;
const uint32_t pe=read32(bytes,kLfanewOffset);
if(!fits(pe,kSignatureSize+kFileHeaderSize,bytes.size())||bytes.substr(pe,kSignatureSize)!=std::string_view("PE\0\0",kSignatureSize))return invalid;
const std::size_t file_header=pe+kSignatureSize;
const uint16_t machine=read16(bytes,file_header);
const uint16_t section_count=read16(bytes,file_header+kSectionCountOffset);
const uint16_t optional_size=read16(bytes,file_header+kOptionalSizeOffset);
const std::size_t optional=file_header+kFileHeaderSize;
if(optional_size<kMagicSize||!fits(optional,kMagicSize,bytes.size()))return invalid;
const uint16_t magic=read16(bytes,optional);
if(magic==kPe32Magic||(magic==kPe32PlusMagic&&machine!=kMachineAmd64))return std::unexpected(PeError::not_x64);
if(magic!=kPe32PlusMagic)return invalid;
if(optional_size<kDirectoriesOffset||!fits(optional,optional_size,bytes.size()))return invalid;
Image image(bytes);
const uint32_t directory_count=std::min(read32(bytes,optional+kDirectoryCountOffset),kMaxDirectories);
for(const std::size_t index:{kImportDirectory,kDelayImportDirectory}){
if(index>=directory_count)continue;
const std::size_t entry=kDirectoriesOffset+index*kDirectorySize;
// A directory the count promises must lie inside the optional header.
if(entry+kDirectorySize>optional_size)return invalid;
(index==kImportDirectory?image.import_rva_:image.delay_rva_)=read32(bytes,optional+entry);
}
const std::size_t table=optional+optional_size;
if(!fits(table,uint64_t{section_count}*kSectionHeaderSize,bytes.size()))return invalid;
image.sections_.reserve(section_count);
for(std::size_t i=0;i<section_count;++i){
const std::size_t header=table+i*kSectionHeaderSize;
const uint32_t virtual_size=read32(bytes,header+8);
const uint32_t rva=read32(bytes,header+12);
const uint32_t raw_size=read32(bytes,header+16);
const uint32_t offset=read32(bytes,header+20);
if(raw_size!=0&&!fits(offset,raw_size,bytes.size()))return invalid;
// Bytes past the virtual size are not mapped; a virtual size of 0 means the raw size.
const uint32_t size=virtual_size==0?raw_size:std::min(raw_size,virtual_size);
if(uint64_t{rva}+size>std::numeric_limits<uint32_t>::max())return invalid;
if(size!=0)image.sections_.push_back({rva,size,offset});
}
std::ranges::sort(image.sections_,{},&Section::rva);
for(std::size_t i=1;i<image.sections_.size();++i){
const Section& previous=image.sections_[i-1];
if(uint64_t{previous.rva}+previous.size>image.sections_[i].rva)return invalid;
}
return image;
}
// The file bytes from rva to the end of the section holding it; nullopt when no section holds it.
std::optional<std::string_view> at(uint32_t rva) const noexcept
{
const auto next=std::ranges::upper_bound(sections_,rva,{},&Section::rva);
if(next==sections_.begin())return std::nullopt;
const Section& section=*std::prev(next);
const uint32_t inside=rva-section.rva;
if(inside>=section.size)return std::nullopt;
return bytes_.substr(std::size_t{section.offset}+inside,section.size-inside);
}
// The NUL-terminated name at rva: 1 to kMaxNameLength printable ASCII characters inside one section, or nullopt.
std::optional<std::string_view> name(uint32_t rva) const noexcept
{
const std::optional<std::string_view> text=at(rva);
if(!text)return std::nullopt;
const std::string_view window=text->substr(0,kMaxNameLength+1);
const std::size_t end=window.find('\0');
if(end==0||end==std::string_view::npos)return std::nullopt;
const std::string_view result=window.substr(0,end);
if(!std::ranges::all_of(result,[](char c){return c>=0x20&&c<=0x7E;}))return std::nullopt;
return result;
}
uint32_t import_rva() const noexcept{return import_rva_;}
uint32_t delay_rva() const noexcept{return delay_rva_;}
private:
explicit Image(std::string_view bytes):bytes_(bytes){}
std::string_view bytes_;
std::vector<Section> sections_;
uint32_t import_rva_=0;
uint32_t delay_rva_=0;
};

// Collects names once each, compared ASCII case-insensitively as Windows compares DLL names.
class NameList{
public:
void add(std::string_view name)
{
std::string key(name);
for(char& c:key)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
if(seen_.insert(std::move(key)).second)names_.emplace_back(name);
}
std::vector<std::string> take() &&{return std::move(names_);}
private:
std::vector<std::string> names_;
std::unordered_set<std::string> seen_;
};

// Walks the descriptor array at rva (a directory RVA of 0 is no array) until ends(descriptor), adding the name each descriptor's
// name_offset field points at. False when the array, a name or the terminator lies outside its section or there are too many entries.
template<typename Ends>
bool walk(const Image& image,uint32_t rva,std::size_t descriptor_size,std::size_t name_offset,Ends ends,NameList& names)
{
if(rva==0)return true;
const std::optional<std::string_view> table=image.at(rva);
if(!table)return false;
for(std::size_t i=0;;++i){
if(table->size()/descriptor_size<=i)return false;
const std::string_view descriptor=table->substr(i*descriptor_size,descriptor_size);
if(ends(descriptor))return true;
if(i==kMaxDescriptors)return false;
const std::optional<std::string_view> name=image.name(read32(descriptor,name_offset));
if(!name)return false;
names.add(*name);
}
}
}

std::expected<std::vector<std::string>,PeError> pe_imported_dlls_in(std::string_view bytes)
{
const std::expected<Image,PeError> image=Image::parse(bytes);
if(!image)return std::unexpected(image.error());
NameList names;
// The loader stops at the first import descriptor without a name or an import address table.
const auto import_ends=[](std::string_view descriptor){return read32(descriptor,kImportNameOffset)==0||read32(descriptor,kImportFirstThunkOffset)==0;};
const auto delay_ends=[](std::string_view descriptor){return read32(descriptor,kDelayNameOffset)==0;};
if(!walk(*image,image->import_rva(),kImportDescriptorSize,kImportNameOffset,import_ends,names))return std::unexpected(PeError::invalid);
if(!walk(*image,image->delay_rva(),kDelayDescriptorSize,kDelayNameOffset,delay_ends,names))return std::unexpected(PeError::invalid);
return std::move(names).take();
}

std::expected<std::vector<std::string>,std::string> pe_imported_dlls(const std::filesystem::path& file)
{
const std::string quoted="\""+path_to_utf8(file)+"\"";
std::ifstream in(file,std::ios::binary|std::ios::ate);
const std::streamoff size=in?static_cast<std::streamoff>(in.tellg()):-1;
if(size<0)return std::unexpected("error: cannot read "+quoted);
// Every offset in a PE file is 32 bits wide, so a larger file is no PE file; it is not read.
if(static_cast<uint64_t>(size)>std::numeric_limits<uint32_t>::max())return std::unexpected("error: "+quoted+" is not a valid Windows DLL");
std::string bytes(static_cast<std::size_t>(size),'\0');
in.seekg(0);
in.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
if(!in)return std::unexpected("error: cannot read "+quoted);
std::expected<std::vector<std::string>,PeError> names=pe_imported_dlls_in(bytes);
if(!names)return std::unexpected("error: "+quoted+(names.error()==PeError::not_x64?" is not a 64-bit (x64) Windows DLL":" is not a valid Windows DLL"));
return std::move(*names);
}
}
