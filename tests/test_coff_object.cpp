#include <core/coff_object.hpp>
#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace {
uint64_t read_le(std::string_view bytes,std::size_t offset,std::size_t size)
{
REQUIRE(offset+size<=bytes.size());
uint64_t value=0;
for(std::size_t i=size;i-->0;)value=(value<<8)|static_cast<unsigned char>(bytes[offset+i]);
return value;
}
struct Symbol{
std::string name;
uint32_t value=0;
int16_t section=0;
uint8_t storage_class=0;
};
struct Parsed{
std::string section_name;
uint32_t characteristics=0;
std::string data;
std::vector<Symbol> symbols;
uint16_t section_count=0;
};
// Reads the file header, the first section header, its raw data and every symbol (long names through the string table).
Parsed parse(std::string_view object)
{
Parsed out;
REQUIRE(read_le(object,0,2)==0x8664);
out.section_count=static_cast<uint16_t>(read_le(object,2,2));
const auto symbols=static_cast<std::size_t>(read_le(object,8,4));
const auto symbol_count=static_cast<std::size_t>(read_le(object,12,4));
REQUIRE(read_le(object,16,2)==0);
const std::size_t header=20;
const std::string_view name=object.substr(header,8);
out.section_name=std::string(name.substr(0,name.find('\0')));
const auto raw_size=static_cast<std::size_t>(read_le(object,header+16,4));
const auto raw_pointer=static_cast<std::size_t>(read_le(object,header+20,4));
out.characteristics=static_cast<uint32_t>(read_le(object,header+36,4));
REQUIRE(raw_pointer+raw_size<=object.size());
out.data=std::string(object.substr(raw_pointer,raw_size));
const std::size_t strings=symbols+18*symbol_count;
const auto strings_size=static_cast<std::size_t>(read_le(object,strings,4));
REQUIRE(strings+strings_size==object.size());
for(std::size_t i=0;i<symbol_count;++i){
const std::size_t at=symbols+18*i;
REQUIRE(read_le(object,at,4)==0);
const auto name_offset=static_cast<std::size_t>(read_le(object,at+4,4));
REQUIRE(name_offset>=4);
REQUIRE(name_offset<strings_size);
REQUIRE(read_le(object,at+17,1)==0);
const std::string_view text=object.substr(strings+name_offset,strings_size-name_offset);
Symbol symbol;
symbol.name=std::string(text.substr(0,text.find('\0')));
symbol.value=static_cast<uint32_t>(read_le(object,at+8,4));
symbol.section=static_cast<int16_t>(read_le(object,at+12,2));
symbol.storage_class=static_cast<uint8_t>(read_le(object,at+16,1));
out.symbols.push_back(std::move(symbol));
}
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
REQUIRE(parsed.symbols.size()==2);
CHECK(parsed.symbols[0].name=="zodiac_payload");
CHECK(parsed.symbols[0].section==1);
CHECK(parsed.symbols[0].value==0);
CHECK(parsed.symbols[0].storage_class==2);
// The end symbol sits just past the data, so end - begin - 8 is the payload size the stub expects.
CHECK(parsed.symbols[1].name=="zodiac_payload_end");
CHECK(parsed.symbols[1].section==1);
CHECK(parsed.symbols[1].value==8+payload.size());
CHECK(parsed.symbols[1].storage_class==2);
}

TEST_CASE("empty payload object still has the length field")
{
const Parsed parsed=parse(zodiac::write_payload_object({}));
CHECK(parsed.data==std::string(8,'\0'));
REQUIRE(parsed.symbols.size()==2);
CHECK(parsed.symbols[0].name=="zodiac_payload");
CHECK(parsed.symbols[1].name=="zodiac_payload_end");
CHECK(parsed.symbols[1].value==8);
}
