#include <core/payload.hpp>
#include <doctest/doctest.h>
#include <cstddef>
#include <string>
#include <string_view>

namespace {
zodiac::GamePayload sample()
{
zodiac::GamePayload payload;
payload.kit_version="0.3.0";
payload.game_name="Kara \xC5\x9F\x65hir \xE2\x98\x85";
payload.requested={"example","dep_child"};
payload.modules={{"example","1.2.3",zodiac::LinkMode::static_link},{"dep_child","0.1.0",zodiac::LinkMode::dynamic_link}};
payload.core_exclude={"window"};
payload.diagnostics.console=zodiac::OutputMode::always;
payload.diagnostics.file="logs/game.log";
payload.diagnostics.dialog=zodiac::OutputMode::never;
payload.scripts={{"main.zs",true,std::string("\x00\x01\xFF bytecode \x00",14)}};
return payload;
}
}

TEST_CASE("crc32 matches the IEEE check values")
{
CHECK(zodiac::crc32("")==0x00000000u);
CHECK(zodiac::crc32("a")==0xE8B7BE43u);
CHECK(zodiac::crc32("123456789")==0xCBF43926u);
CHECK(zodiac::crc32("The quick brown fox jumps over the lazy dog")==0x414FA339u);
}

TEST_CASE("payload round trip keeps every field")
{
const zodiac::GamePayload input=sample();
const std::string bytes=zodiac::serialize_payload(input);
CHECK(bytes.starts_with("ZDPAYLD1"));
const auto parsed=zodiac::parse_payload(bytes);
INFO(parsed.error_or(""));
REQUIRE(parsed.has_value());
CHECK(*parsed==input);
CHECK(parsed->format_version==1);
}

TEST_CASE("payload with no modules and no scripts round trips")
{
zodiac::GamePayload input;
input.game_name="g";
const auto parsed=zodiac::parse_payload(zodiac::serialize_payload(input));
REQUIRE(parsed.has_value());
CHECK(*parsed==input);
}

TEST_CASE("every flipped byte is reported as damage")
{
const std::string bytes=zodiac::serialize_payload(sample());
for(std::size_t i=0;i<bytes.size();++i){
for(const unsigned char mask:{0x01u,0x80u,0xFFu}){
std::string damaged=bytes;
damaged[i]=static_cast<char>(static_cast<unsigned char>(damaged[i])^mask);
const auto parsed=zodiac::parse_payload(damaged);
INFO("byte ",i," mask ",static_cast<unsigned>(mask));
REQUIRE_FALSE(parsed.has_value());
CHECK(parsed.error().starts_with("error: the game data is damaged"));
CHECK(parsed.error().find('\n')==std::string::npos);
}
}
}

TEST_CASE("every truncation is reported as damage")
{
const std::string bytes=zodiac::serialize_payload(sample());
for(std::size_t size=0;size<bytes.size();++size){
// A separate buffer of exactly size bytes, so a read past the end is caught by sanitizers and debug iterators.
const std::string truncated=bytes.substr(0,size);
const auto parsed=zodiac::parse_payload(truncated);
INFO("size ",size);
REQUIRE_FALSE(parsed.has_value());
CHECK(parsed.error().starts_with("error: the game data is damaged"));
}
}

TEST_CASE("checksum mismatch is named")
{
std::string bytes=zodiac::serialize_payload(sample());
bytes.back()=static_cast<char>(bytes.back()^0x10);
const auto parsed=zodiac::parse_payload(bytes);
REQUIRE_FALSE(parsed.has_value());
CHECK(parsed.error()=="error: the game data is damaged (checksum mismatch)");
}

TEST_CASE("appended bytes are reported as damage")
{
const std::string bytes=zodiac::serialize_payload(sample())+"x";
const auto parsed=zodiac::parse_payload(bytes);
REQUIRE_FALSE(parsed.has_value());
CHECK(parsed.error().starts_with("error: the game data is damaged"));
}
