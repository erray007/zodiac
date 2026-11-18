// Tests zodiac::String when the heap runs out in the middle of an append (final review item 1): this executable replaces the
// global operator new so a test can make one chosen allocation of the current thread throw std::bad_alloc. An append whose
// bytes are already committed when growing the sparse index fails must still leave an index that covers the whole text
// (both append paths: the bytes fit the block, and the block is reallocated first), and an append whose new block cannot be
// allocated changes nothing. Every character is checked against a std::u32string model afterwards.
#include <core/types/string.hpp>
#include <doctest/doctest.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <new>
#include <string>
#include <string_view>

namespace {
// The allocation (counted from 1) of this thread that throws, 0 when none is armed; failed counts the throws.
thread_local int fail_at=0;
thread_local int failed=0;
}

void* operator new(std::size_t size)
{
if(fail_at>0&&--fail_at==0){
++failed;
throw std::bad_alloc();
}
if(void* block=std::malloc(size==0?1:size))return block;
throw std::bad_alloc();
}

namespace {
using zodiac::String;

// Characters of 1, 2, 3 and 4 bytes, so byte offsets are not a multiple of the character index.
constexpr char32_t pattern[]={U'a',U'ş',U'€',U'\U0001F600',U'İ',U'z'};

void append_utf8(std::string& out,char32_t code)
{
if(code<0x80){
out.push_back(static_cast<char>(code));
}else if(code<0x800){
out.push_back(static_cast<char>(0xC0|(code>>6)));
out.push_back(static_cast<char>(0x80|(code&0x3F)));
}else if(code<0x10000){
out.push_back(static_cast<char>(0xE0|(code>>12)));
out.push_back(static_cast<char>(0x80|((code>>6)&0x3F)));
out.push_back(static_cast<char>(0x80|(code&0x3F)));
}else{
out.push_back(static_cast<char>(0xF0|(code>>18)));
out.push_back(static_cast<char>(0x80|((code>>12)&0x3F)));
out.push_back(static_cast<char>(0x80|((code>>6)&0x3F)));
out.push_back(static_cast<char>(0x80|(code&0x3F)));
}
}
// count characters of the pattern starting at its first'th character, as UTF-8 and as code points.
void make_text(std::size_t first,std::size_t count,std::string& bytes,std::u32string& codes)
{
for(std::size_t at=0;at<count;++at){
const char32_t code=pattern[(first+at)%std::size(pattern)];
append_utf8(bytes,code);
codes.push_back(code);
}
}
// Every character of text read through the index (code_at, char_at, substr and find of the last character) against model.
void check_every_character(const String& text,const std::u32string& model)
{
REQUIRE(text.length()==static_cast<int>(model.size()));
for(std::size_t at=0;at<model.size();++at){
INFO("character ",at);
const auto index=static_cast<int>(at);
const auto code=text.code_at(index);
REQUIRE(code.has_value());
CHECK(*code==model[at]);
std::string expected;
append_utf8(expected,model[at]);
const auto one=text.char_at(index);
REQUIRE(one.has_value());
CHECK(one->bytes()==expected);
const auto rest=text.substr(index);
REQUIRE(rest.has_value());
CHECK(rest->length()==static_cast<int>(model.size()-at));
}
std::string last;
append_utf8(last,model.back());
const auto hit=text.rfind(last);
REQUIRE(hit.has_value());
CHECK(*hit==static_cast<int>(model.size()-1));
}
// Appends bytes to text with the allocation-th allocation of the append failing; true when the append threw std::bad_alloc.
// No test macro runs while the failure is armed, so only the append's own allocations are counted.
bool append_failing(String& text,std::string_view bytes,int allocation)
{
failed=0;
fail_at=allocation;
bool threw=false;
try{
text.append(bytes);
}
catch(const std::bad_alloc&){
threw=true;
}
fail_at=0;
return threw;
}
// A heap string of count characters whose sparse index is built (a read past index_step characters builds it).
String indexed_text(std::size_t count,int reserve,std::string& bytes,std::u32string& model)
{
make_text(0,count,bytes,model);
String text;
text.reserve(reserve);
text.append(std::string_view(bytes));
REQUIRE(text.length()==static_cast<int>(count));
REQUIRE(text.code_at(static_cast<int>(count-1)).has_value());
return text;
}
}

TEST_CASE("an append whose sparse index cannot grow keeps an index covering the whole text (bytes fit the block)")
{
std::string bytes;
std::u32string model;
// 100 characters have an index of 4 entries; 70 more need 6, so the extension has to grow the index block, and that
// allocation is the only one the append makes (the reserved block holds all the bytes).
String text=indexed_text(100,4096,bytes,model);
std::string more;
make_text(100,70,more,model);
const String tail(more);
REQUIRE(tail.length()==70);
CHECK_FALSE(append_failing(text,tail.bytes(),1));
CHECK(failed==1);
CHECK(text.bytes()==bytes+more);
check_every_character(text,model);
// The index dropped by the failure is rebuilt, and later appends extend it as usual.
std::string again;
make_text(170,90,again,model);
text.append(std::string_view(again));
CHECK(text.bytes()==bytes+more+again);
check_every_character(text,model);
}

TEST_CASE("an append whose sparse index cannot grow keeps an index covering the whole text (block reallocated)")
{
std::string bytes;
std::u32string model;
// No spare room: the append reallocates the block (the first allocation, more than the block cache's 256 bytes) and then
// grows the index (the second), which fails.
String text=indexed_text(100,0,bytes,model);
std::string more;
make_text(100,70,more,model);
REQUIRE(static_cast<std::size_t>(text.capacity())<bytes.size()+more.size());
CHECK_FALSE(append_failing(text,more,2));
CHECK(failed==1);
CHECK(text.bytes()==bytes+more);
check_every_character(text,model);
}

TEST_CASE("an append whose new block cannot be allocated throws and changes nothing")
{
std::string bytes;
std::u32string model;
String text=indexed_text(100,0,bytes,model);
std::string more;
std::u32string unused;
make_text(100,70,more,unused);
CHECK(append_failing(text,more,1));
CHECK(failed==1);
CHECK(text.bytes()==bytes);
check_every_character(text,model);
}
