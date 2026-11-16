#include <core/coff_object.hpp>
#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace {
uint64_t read_le(std::string_view bytes,std::size_t offset,std::size_t size)
{
REQUIRE(offset+size<=bytes.size());
uint64_t value=0;
for(std::size_t i=size;i-->0;)value=(value<<8)|static_cast<unsigned char>(bytes[offset+i]);
return value;
}
struct Parsed{
std::string section_name;
uint32_t characteristics=0;
std::string data;
std::string symbol_name;
uint32_t symbol_value=0;
int16_t symbol_section=0;
uint8_t storage_class=0;
uint32_t symbol_count=0;
uint16_t section_count=0;
};
// Reads the file header, the first section header, its raw data and the first symbol (with its string table name).
Parsed parse(std::string_view object)
{
Parsed out;
REQUIRE(read_le(object,0,2)==0x8664);
out.section_count=static_cast<uint16_t>(read_le(object,2,2));
const auto symbols=static_cast<std::size_t>(read_le(object,8,4));
out.symbol_count=static_cast<uint32_t>(read_le(object,12,4));
REQUIRE(read_le(object,16,2)==0);
const std::size_t header=20;
const std::string_view name=object.substr(header,8);
out.section_name=std::string(name.substr(0,name.find('\0')));
const auto raw_size=static_cast<std::size_t>(read_le(object,header+16,4));
const auto raw_pointer=static_cast<std::size_t>(read_le(object,header+20,4));
out.characteristics=static_cast<uint32_t>(read_le(object,header+36,4));
REQUIRE(raw_pointer+raw_size<=object.size());
out.data=std::string(object.substr(raw_pointer,raw_size));
REQUIRE(out.symbol_count>=1);
const std::size_t strings=symbols+18*out.symbol_count;
REQUIRE(read_le(object,symbols,4)==0);
const auto name_offset=static_cast<std::size_t>(read_le(object,symbols+4,4));
const auto strings_size=static_cast<std::size_t>(read_le(object,strings,4));
REQUIRE(strings+strings_size==object.size());
REQUIRE(name_offset>=4);
REQUIRE(name_offset<strings_size);
const std::string_view text=object.substr(strings+name_offset,strings_size-name_offset);
out.symbol_name=std::string(text.substr(0,text.find('\0')));
out.symbol_value=static_cast<uint32_t>(read_le(object,symbols+8,4));
out.symbol_section=static_cast<int16_t>(read_le(object,symbols+12,2));
out.storage_class=static_cast<uint8_t>(read_le(object,symbols+16,1));
return out;
}
}

TEST_CASE("payload object holds the length and the payload in .zodpay")
{
const std::string payload("ZDPAYLD1\x00\x01\x02 any bytes",20);
const std::string object=zodiac::write_payload_object(payload);
const Parsed parsed=parse(object);
CHECK(parsed.section_count==1);
CHECK(parsed.section_name==".zodpay");
// Initialized data, 16-byte aligned, read-only (no write or execute bit).
CHECK((parsed.characteristics&0x00000040u)!=0);
CHECK((parsed.characteristics&0x00F00000u)==0x00500000u);
CHECK((parsed.characteristics&0x40000000u)!=0);
CHECK((parsed.characteristics&0xA0000000u)==0);
REQUIRE(parsed.data.size()==8+payload.size());
CHECK(read_le(parsed.data,0,8)==payload.size());
CHECK(parsed.data.substr(8)==payload);
CHECK(parsed.symbol_count==1);
CHECK(parsed.symbol_name=="zodiac_payload");
CHECK(parsed.symbol_section==1);
CHECK(parsed.symbol_value==0);
CHECK(parsed.storage_class==2);
}

TEST_CASE("empty payload object still has the length field")
{
const Parsed parsed=parse(zodiac::write_payload_object({}));
CHECK(parsed.data==std::string(8,'\0'));
CHECK(parsed.symbol_name=="zodiac_payload");
}
