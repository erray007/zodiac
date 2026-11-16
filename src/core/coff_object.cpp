#include <core/coff_object.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace zodiac {
namespace {
constexpr uint16_t kMachineAmd64=0x8664;
constexpr std::size_t kFileHeaderSize=20;
constexpr std::size_t kSectionHeaderSize=40;
constexpr std::size_t kSymbolSize=18;
constexpr std::size_t kLengthFieldSize=8;
// IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_16BYTES | IMAGE_SCN_MEM_READ
constexpr uint32_t kSectionCharacteristics=0x00000040u|0x00500000u|0x40000000u;
constexpr int16_t kSectionNumber=1;
constexpr uint8_t kStorageClassExternal=2;
constexpr std::string_view kSectionName=".zodpay";
// Both names are longer than 8 bytes, so they live in the string table (each with its terminating NUL).
constexpr std::string_view kBeginName="zodiac_payload";
constexpr std::string_view kEndName="zodiac_payload_end";
constexpr uint32_t kSymbolCount=2;
constexpr uint32_t kStringTableSize=4+kBeginName.size()+1+kEndName.size()+1;
void put(std::string& out,uint64_t value,std::size_t size)
{
for(std::size_t i=0;i<size;++i)out.push_back(static_cast<char>((value>>(8*i))&0xFFu));
}
// An external symbol in section 1: a long name is 4 zero bytes then its offset in the string table (which starts with its own u32 size).
void put_symbol(std::string& out,uint32_t name_offset,uint64_t value)
{
put(out,0,4);
put(out,name_offset,4);
put(out,value,4);             // Value
put(out,static_cast<uint16_t>(kSectionNumber),2);
put(out,0,2);                 // Type
put(out,kStorageClassExternal,1);
put(out,0,1);                 // NumberOfAuxSymbols
}
}

std::string write_payload_object(std::string_view payload)
{
constexpr std::size_t data_offset=kFileHeaderSize+kSectionHeaderSize;
// Every file offset is a u32, the last one being the end of the string table.
if(payload.size()>std::numeric_limits<uint32_t>::max()-(data_offset+kLengthFieldSize+kSymbolCount*kSymbolSize+kStringTableSize))throw std::length_error("game payload is too large for a COFF object");
const std::size_t data_size=kLengthFieldSize+payload.size();
const std::size_t symbols_offset=data_offset+data_size;
std::string out;
out.reserve(symbols_offset+kSymbolCount*kSymbolSize+kStringTableSize);
// File header.
put(out,kMachineAmd64,2);
put(out,1,2);                 // NumberOfSections
put(out,0,4);                 // TimeDateStamp: 0 keeps builds reproducible
put(out,symbols_offset,4);    // PointerToSymbolTable
put(out,kSymbolCount,4);      // NumberOfSymbols
put(out,0,2);                 // SizeOfOptionalHeader
put(out,0,2);                 // Characteristics
// Section header.
out.append(kSectionName);
out.append(8-kSectionName.size(),'\0');
put(out,0,4);                 // VirtualSize
put(out,0,4);                 // VirtualAddress
put(out,data_size,4);         // SizeOfRawData
put(out,data_offset,4);       // PointerToRawData
put(out,0,4);                 // PointerToRelocations
put(out,0,4);                 // PointerToLinenumbers
put(out,0,2);                 // NumberOfRelocations
put(out,0,2);                 // NumberOfLinenumbers
put(out,kSectionCharacteristics,4);
// Section data.
put(out,payload.size(),8);
out.append(payload);
// Symbols: the start of the section, and its end (the value may equal the section size), so the stub can bound the stored length.
put_symbol(out,4,0);
put_symbol(out,static_cast<uint32_t>(4+kBeginName.size()+1),data_size);
// String table.
put(out,kStringTableSize,4);
out.append(kBeginName);
out.push_back('\0');
out.append(kEndName);
out.push_back('\0');
return out;
}
}
