// Tests zodiac::String: the spec 9 examples, small-string storage, every operation's range rules, Turkish case mapping
// (Review Focus 5), cached metadata after mutation against a std::u32string model (Review Focus 1), and invalid UTF-8
// handling over exhaustive short byte strings and random ones (Review Focus 2).
#include <core/types/string.hpp>
#include <core/types/unicode.hpp>
#include <doctest/doctest.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using zodiac::String;
using zodiac::StringError;

namespace doctest {
template<>
struct StringMaker<zodiac::String> {
static doctest::String convert(const zodiac::String& value)
{
return doctest::String(value.data(),static_cast<unsigned>(value.byte_length()));
}
};
}

namespace {
// An independent UTF-8 encoder for the model (no validation: the model only holds scalar values).
std::string utf8(std::u32string_view text)
{
std::string out;
for(const char32_t c:text){
if(c<0x80){
out.push_back(static_cast<char>(c));
}else if(c<0x800){
out.push_back(static_cast<char>(0xC0|(c>>6)));
out.push_back(static_cast<char>(0x80|(c&0x3F)));
}else if(c<0x10000){
out.push_back(static_cast<char>(0xE0|(c>>12)));
out.push_back(static_cast<char>(0x80|((c>>6)&0x3F)));
out.push_back(static_cast<char>(0x80|(c&0x3F)));
}else{
out.push_back(static_cast<char>(0xF0|(c>>18)));
out.push_back(static_cast<char>(0x80|((c>>12)&0x3F)));
out.push_back(static_cast<char>(0x80|((c>>6)&0x3F)));
out.push_back(static_cast<char>(0x80|(c&0x3F)));
}
}
return out;
}
// The characters of bytes per Unicode Table 3-7, written independently of string.cpp: a well-formed sequence is one
// character, every other byte is a character of its own.
std::vector<std::string_view> reference_characters(std::string_view bytes)
{
std::vector<std::string_view> characters;
std::size_t i=0;
const auto in=[&](std::size_t k,unsigned low,unsigned high){
if(i+k>=bytes.size())return false;
const unsigned b=static_cast<unsigned char>(bytes[i+k]);
return b>=low&&b<=high;
};
while(i<bytes.size()){
const unsigned b=static_cast<unsigned char>(bytes[i]);
std::size_t n=1;
if(b>=0xC2&&b<=0xDF){
if(in(1,0x80,0xBF))n=2;
}else if(b==0xE0){
if(in(1,0xA0,0xBF)&&in(2,0x80,0xBF))n=3;
}else if((b>=0xE1&&b<=0xEC)||b==0xEE||b==0xEF){
if(in(1,0x80,0xBF)&&in(2,0x80,0xBF))n=3;
}else if(b==0xED){
if(in(1,0x80,0x9F)&&in(2,0x80,0xBF))n=3;
}else if(b==0xF0){
if(in(1,0x90,0xBF)&&in(2,0x80,0xBF)&&in(3,0x80,0xBF))n=4;
}else if(b>=0xF1&&b<=0xF3){
if(in(1,0x80,0xBF)&&in(2,0x80,0xBF)&&in(3,0x80,0xBF))n=4;
}else if(b==0xF4){
if(in(1,0x80,0x8F)&&in(2,0x80,0xBF)&&in(3,0x80,0xBF))n=4;
}
characters.push_back(bytes.substr(i,n));
i+=n;
}
return characters;
}
bool stored_inline(const String& s)
{
const auto* object=reinterpret_cast<const char*>(&s);
return s.data()>=object&&s.data()<object+sizeof(String);
}
// Checks s against the reference decoder; returns an empty string when it agrees, otherwise what differs.
std::string check_characters(const String& s)
{
const std::vector<std::string_view> expected=reference_characters(s.bytes());
if(s.length()!=static_cast<int>(expected.size()))return "length "+std::to_string(s.length())+" expected "+std::to_string(expected.size());
std::string joined;
for(int i=0;i<s.length();++i){
const auto c=s.char_at(i);
if(!c)return "char_at("+std::to_string(i)+") failed";
if(c->bytes()!=expected[static_cast<std::size_t>(i)])return "char_at("+std::to_string(i)+") differs";
joined.append(c->bytes());
}
if(joined!=s.bytes())return "characters do not reproduce the bytes";
if(s.data()[s.byte_length()]!='\0')return "not NUL-terminated";
return {};
}
// The exhaustive length-3 test takes about a minute in a debug build; it runs when ZODIAC_LONG_TESTS=1 (set in CI's Linux job).
bool long_tests_enabled()
{
const char* value=std::getenv("ZODIAC_LONG_TESTS");
return value!=nullptr&&std::string_view(value)=="1";
}
std::string hex_bytes(std::string_view bytes)
{
std::string out;
constexpr char digits[]="0123456789ABCDEF";
for(const char c:bytes){
const auto b=static_cast<unsigned char>(c);
out.push_back(digits[b>>4]);
out.push_back(digits[b&15]);
out.push_back(' ');
}
return out;
}
}

TEST_CASE("the spec 9 examples")
{
const String s="şehir";
CHECK(s.length()==5);
// Spec 9 says 7, but precomposed "şehir" is C5 9F 65 68 69 72: six bytes (seven would be the NFD form, six characters).
CHECK(s.byte_length()==6);
CHECK(s.char_at(1)=="e");
CHECK(String("İSTANBUL").lower("tr")=="istanbul");
CHECK_FALSE(s.empty());
CHECK(String().empty());
CHECK(String().length()==0);
CHECK(String().data()[0]=='\0');
}

TEST_CASE("short strings live inside the object")
{
CHECK(String::inline_capacity>=22);
CHECK(sizeof(String)==32);
for(const int size:{0,1,21,22,23}){
const String s(std::string(static_cast<std::size_t>(size),'x'));
CAPTURE(size);
CHECK(stored_inline(s));
CHECK(s.byte_length()==size);
CHECK(s.data()[size]=='\0');
const String copy=s;
CHECK(stored_inline(copy));
CHECK(copy==s);
}
const String long_text(std::string(24,'x'));
CHECK_FALSE(stored_inline(long_text));
CHECK(long_text.data()[24]=='\0');
String grown(std::string(23,'y'));
CHECK(stored_inline(grown));
grown+="z";
CHECK_FALSE(stored_inline(grown));
CHECK(grown.byte_length()==24);
CHECK(grown.bytes()==std::string(23,'y')+"z");
}

TEST_CASE("copies and moves keep value semantics")
{
String a(std::string(40,'a'));
String b=a;
CHECK(b==a);
CHECK(b.data()!=a.data());
b+="!";
CHECK(a.byte_length()==40);
CHECK(b.byte_length()==41);
String c=std::move(b);
CHECK(c.byte_length()==41);
CHECK(b.empty());
CHECK(b.data()[0]=='\0');
b=c;
CHECK(b==c);
a=std::move(c);
CHECK(a.byte_length()==41);
const String& same=a;
a=same;
CHECK(a.byte_length()==41);
String small="ş";
small=a;
CHECK(small==a);
a="kısa";
CHECK(a=="kısa");
CHECK(a.length()==4);
static_assert(std::is_nothrow_move_constructible_v<String>);
static_assert(std::is_nothrow_move_assignable_v<String>);
}

TEST_CASE("append and operator+= take strings, views, code points, numbers and bools")
{
String s;
s+="a";
s+=String("ş");
s+=std::string_view("b");
s+=U'ğ';
s+=char32_t{0x1F600};
s+=42;
s+=-7LL;
s+=std::uint64_t{18446744073709551615u};
s+=1.5;
s+=true;
s+=false;
CHECK(s=="aşbğ😀42-7184467440737095516151.5truefalse");
CHECK(s.length()==41);
String t="x";
t.append(String("y")).append(U'z');
CHECK(t=="xyz");
String u=String("ab")+"cd";
CHECK(u=="abcd");
CHECK("<"+u+">"=="<abcd>");
CHECK(String("a")+String("b")=="ab");
CHECK(std::string_view("p")+String("q")=="pq");
String invalid;
invalid+=char32_t{0xD800};
invalid+=char32_t{0x110000};
CHECK(invalid=="��");
String self(std::string(30,'s'));
self+=self;
CHECK(self.bytes()==std::string(60,'s'));
String small_self="ab";
small_self+=small_self;
CHECK(small_self=="abab");
String view_of_self(std::string(50,'v'));
view_of_self+=view_of_self.bytes().substr(10,30);
CHECK(view_of_self.bytes()==std::string(80,'v'));
}

TEST_CASE("number formatting is the shortest round trip without .0")
{
CHECK(String::from_number(1.0)=="1");
CHECK(String::from_number(-2.0)=="-2");
CHECK(String::from_number(0.0)=="0");
CHECK(String::from_number(-0.0)=="0");
CHECK(String::from_number(0.1)=="0.1");
CHECK(String::from_number(1.5)=="1.5");
CHECK(String::from_number(123456789.0)=="123456789");
CHECK(String::from_number(0.1+0.2)=="0.30000000000000004");
CHECK(String::from_number(100000.0)=="100000");
CHECK(String::from_number(1e6)=="1000000");
CHECK(String::from_number(1.2e8)=="120000000");
CHECK(String::from_number(-1.2e8)=="-120000000");
CHECK(String::from_number(0.0001)=="0.0001");
CHECK(String::from_number(1e20)=="100000000000000000000");
// Integers print their exact digits (std::to_chars picks the closest of equally short texts), unlike JavaScript's zero padding.
CHECK(String::from_number(123456789012345680000.0)=="123456789012345683968");
CHECK(String::from_number(1e21)=="1e+21");
CHECK(String::from_number(1.5e300)=="1.5e+300");
CHECK(String::from_number(1e-7)=="0.0000001");
CHECK(String::from_number(-1.25e-7)=="-0.000000125");
CHECK(String::from_number(9.9e-8)=="9.9e-8");
CHECK(String::from_number(5e-324)=="5e-324");
CHECK(String::from_number(-2.2250738585072014e-308)=="-2.2250738585072014e-308");
CHECK(String::from_number(1.7976931348623157e308)=="1.7976931348623157e+308");
CHECK(String::from_number(0.1f)=="0.1");
CHECK(String::from_number(16777216.0f)=="16777216");
CHECK(String::from_number(1e-8f)=="1e-8");
CHECK(String::from_number(3.4028235e38f)=="3.4028235e+38");
String with_float;
with_float+=0.1f;
CHECK(with_float=="0.1");
CHECK(String::from_number(std::numeric_limits<double>::infinity())=="inf");
CHECK(String::from_number(-std::numeric_limits<double>::infinity())=="-inf");
CHECK(String::from_number(std::numeric_limits<double>::quiet_NaN())=="nan");
CHECK(String::from_number(-std::numeric_limits<double>::quiet_NaN())=="nan");
CHECK(String::from_number(42)=="42");
CHECK(String::from_number(std::numeric_limits<std::int64_t>::min())=="-9223372036854775808");
CHECK(String::from_number(std::uint64_t{18446744073709551615u})=="18446744073709551615");
CHECK(String::from_number(2.5f)=="2.5");
CHECK(String::from_bool(true)=="true");
CHECK(String::from_bool(false)=="false");
CHECK(String::from_code_point(U'ş')=="ş");
CHECK(String::from_code_point(char32_t{0xDFFF})=="�");
}

TEST_CASE("to_number parses decimal numbers exactly and rejects everything else")
{
CHECK(String("3.5").to_number()==3.5);
CHECK(String("-0.25").to_number()==-0.25);
CHECK(String("+2").to_number()==2.0);
CHECK(String("1e3").to_number()==1000.0);
CHECK(String("1E-2").to_number()==0.01);
CHECK(String(".5").to_number()==0.5);
CHECK(String("5.").to_number()==5.0);
CHECK(String("0.1").to_number()==0.1);
CHECK(String("2.2250738585072014e-308").to_number()==2.2250738585072014e-308);
CHECK(String("12345678901234567890").to_number()==12345678901234567890.0);
CHECK(String("9007199254740993").to_number()==9007199254740992.0);
CHECK(String("0.000000000000000000000000000001").to_number()==1e-30);
CHECK(String("1e400").to_number()==std::numeric_limits<double>::infinity());
CHECK(String("-1e400").to_number()==-std::numeric_limits<double>::infinity());
CHECK(String("1e-400").to_number()==0.0);
for(const char* text:{"","abc"," 1","1 ","1e","1e+",".","-","+.e1","0x10","1,5","1.2.3","--1","١","in","nanx","inf","-inf","Infinity","nan","NaN","-nan"}){
CAPTURE(text);
CHECK(String(text).to_number()==std::unexpected(StringError::not_a_number));
CHECK_FALSE(String(text).is_number());
}
CHECK(String("1.5e-3").is_number());
CHECK(String("-0").is_number());
std::mt19937_64 random(20261116);
int mismatches=0;
for(int i=0;i<20000;++i){
double value=std::bit_cast<double>(random());
if(!std::isfinite(value))continue;
const String text=String::from_number(value);
const auto parsed=text.to_number();
if(!parsed||(*parsed!=value&&!(value==0.0&&*parsed==0.0)))++mismatches;
}
CHECK(mismatches==0);
for(int i=0;i<20000;++i){
const double value=static_cast<double>(random()%2000001)/1000.0-1000.0;
const auto parsed=String::from_number(value).to_number();
if(!parsed||*parsed!=value)++mismatches;
}
CHECK(mismatches==0);
for(int i=0;i<20000;++i){
const float value=std::bit_cast<float>(static_cast<std::uint32_t>(random()));
if(!std::isfinite(value))continue;
const auto parsed=String::from_number(value).to_number();
if(!parsed||static_cast<float>(*parsed)!=value)++mismatches;
}
CHECK(mismatches==0);
}

TEST_CASE("to_int, is_number and is_digits")
{
CHECK(String("42").to_int()==42);
CHECK(String("-42").to_int()==-42);
CHECK(String("+7").to_int()==7);
CHECK(String("9223372036854775807").to_int()==std::numeric_limits<std::int64_t>::max());
CHECK(String("-9223372036854775808").to_int()==std::numeric_limits<std::int64_t>::min());
CHECK(String("99999999999999999999").to_int()==std::numeric_limits<std::int64_t>::max());
CHECK(String("-1e300").to_int()==std::numeric_limits<std::int64_t>::min());
CHECK(String("3.9").to_int()==3);
CHECK(String("-3.9").to_int()==-3);
CHECK(String("1e3").to_int()==1000);
CHECK(String("1e400").to_int()==std::numeric_limits<std::int64_t>::max());
CHECK(String("inf").to_int()==std::unexpected(StringError::not_a_number));
CHECK(String("nan").to_int()==std::unexpected(StringError::not_a_number));
CHECK(String("x").to_int()==std::unexpected(StringError::not_a_number));
CHECK(String("").to_int()==std::unexpected(StringError::not_a_number));
CHECK(String("0123").is_digits());
CHECK(String("7").is_digits());
CHECK_FALSE(String("").is_digits());
CHECK_FALSE(String("-1").is_digits());
CHECK_FALSE(String("12a").is_digits());
CHECK_FALSE(String("1.0").is_digits());
CHECK_FALSE(String("١٢").is_digits());
CHECK(String(std::string(100,'5')).is_digits());
}

TEST_CASE("character access and its range rules")
{
const String s="şehir";
CHECK(s.char_at(0)=="ş");
CHECK(s.char_at(4)=="r");
CHECK(s.char_at(5)==std::unexpected(StringError::index_out_of_range));
CHECK(s.char_at(-1)==std::unexpected(StringError::index_out_of_range));
CHECK(s.code_at(0)==U'ş');
CHECK(s.code_at(1)==U'e');
CHECK(s.code_at(5)==std::unexpected(StringError::index_out_of_range));
CHECK(s.code_at(-1)==std::unexpected(StringError::index_out_of_range));
CHECK(s.byte_at(0)==0xC5);
CHECK(s.byte_at(1)==0x9F);
CHECK(s.byte_at(5)==std::uint8_t{'r'});
CHECK(s.byte_at(6)==std::unexpected(StringError::index_out_of_range));
CHECK(s.byte_at(-1)==std::unexpected(StringError::index_out_of_range));
const String invalid="a\xFF" "b";
CHECK(invalid.length()==3);
CHECK(invalid.code_at(1)==char32_t{0xFFFD});
CHECK(invalid.char_at(1)=="\xFF");
CHECK(String("😀").code_at(0)==char32_t{0x1F600});
CHECK_FALSE(s.is_ascii());
CHECK(String("plain").is_ascii());
CHECK_FALSE(invalid.is_ascii());
}

TEST_CASE("indexed access on long non-ASCII text uses the sparse index")
{
std::u32string model;
for(int i=0;i<1000;++i)model.push_back(i%3==0?U'ş':i%3==1?char32_t(U'a'+i%26):char32_t{0x4E00+static_cast<char32_t>(i)});
const String s(utf8(model));
CHECK(s.length()==1000);
for(int i=999;i>=0;i-=7){
CAPTURE(i);
CHECK(s.code_at(i)==model[static_cast<std::size_t>(i)]);
}
for(int i=0;i<1000;i+=13)CHECK(s.char_at(i)==utf8(model.substr(static_cast<std::size_t>(i),1)));
CHECK(s.substr(500,10)==utf8(model.substr(500,10)));
CHECK(s.find(utf8(model.substr(900,5)))==900);
CHECK(s.rfind(utf8(model.substr(30,4)))==30);
}

TEST_CASE("find, rfind, contains, starts_with, ends_with and count")
{
const String s="şehir şehir";
CHECK(s.find("hir")==2);
CHECK(s.find("hir",3)==8);
CHECK(s.find("hir",9)==-1);
CHECK(s.find("x")==-1);
CHECK(s.find("ş")==0);
CHECK(s.find("ş",1)==6);
CHECK(s.find("",4)==4);
CHECK(s.find("",11)==11);
CHECK(s.find("h",11)==-1);
CHECK(s.find("h",12)==std::unexpected(StringError::index_out_of_range));
CHECK(s.find("h",-1)==std::unexpected(StringError::index_out_of_range));
CHECK(s.rfind("hir")==8);
CHECK(s.rfind("hir",8)==8);
CHECK(s.rfind("hir",7)==2);
CHECK(s.rfind("hir",1)==-1);
CHECK(s.rfind("ş",5)==0);
CHECK(s.rfind("")==11);
CHECK(s.rfind("",3)==3);
CHECK(s.rfind("h",11)==8);
CHECK(s.rfind("h",12)==std::unexpected(StringError::index_out_of_range));
CHECK(s.rfind("h",-2)==std::unexpected(StringError::index_out_of_range));
CHECK(s.contains("ir ş"));
CHECK(s.contains(""));
CHECK_FALSE(s.contains("x"));
CHECK(s.starts_with("şe"));
CHECK(s.starts_with(""));
CHECK_FALSE(s.starts_with("e"));
CHECK(s.ends_with("hir"));
CHECK(s.ends_with(""));
CHECK_FALSE(s.ends_with("şe"));
CHECK(s.count("hir")==2);
CHECK(s.count("ş")==2);
CHECK(s.count("")==0);
CHECK(String("ababab").count("ab")==3);
CHECK(String("aaaa").count("aa")==2);
CHECK(String("").find("")==0);
CHECK(String("").rfind("")==0);
CHECK(String("").find("a")==-1);
}

TEST_CASE("find agrees with std::string_view::find around the 16-byte search blocks")
{
// Texts over a small alphabet put matches, near misses (first and last byte equal) and block edges everywhere: every
// start and needle length is checked against the standard library on one-byte-per-character text, and through count
// and contains, which search the same way.
std::mt19937 random(20261117);
for(int round=0;round<200;++round){
const std::size_t size=static_cast<std::size_t>(random()%80);
std::string text;
for(std::size_t i=0;i<size;++i)text.push_back(static_cast<char>('a'+random()%3));
const String s(text);
for(std::size_t n=1;n<=std::min<std::size_t>(size+1,20);++n){
const std::size_t at=size>=n?static_cast<std::size_t>(random()%(size-n+1)):0;
const std::string needle=size>=n&&random()%2?text.substr(at,n):std::string(n,static_cast<char>('a'+random()%3));
for(std::size_t start=0;start<=size;++start){
const std::size_t expected=std::string_view(text).find(needle,start);
CAPTURE(text);
CAPTURE(needle);
CAPTURE(start);
CHECK(s.find(needle,static_cast<int>(start))==(expected==std::string_view::npos?-1:static_cast<int>(expected)));
}
int matches=0;
for(std::size_t pos=std::string_view(text).find(needle);pos!=std::string_view::npos;pos=std::string_view(text).find(needle,pos+n))++matches;
CHECK(s.count(needle)==matches);
CHECK(s.contains(needle)==(matches>0));
}
}
}

TEST_CASE("search on one-byte-per-character text with invalid bytes agrees with the general path")
{
// Invalid bytes count as one character each, so such text takes find_simple (byte offsets are indices). The same text
// after "ş" (a two-byte character) takes the general path; its answers must be the same, one character later.
const std::string texts[]={"\xC3(","a\xFF" "b\xFF","\xFF\xFF\xFF","(\xC3(\xC3","\xE2\x82(\xE2\x82"};
const std::string needles[]={"(","\xC3","\xC3(","\xFF","b\xFF","\xFF\xFF","a","\xE2\x82","x",""};
for(const std::string& text:texts){
const String simple(text);
const String general("\xC5\x9F"+text);
REQUIRE(simple.length()==simple.byte_length());
REQUIRE(general.length()==simple.length()+1);
for(const std::string& needle:needles){
CAPTURE(text);
CAPTURE(needle);
for(int start=0;start<=simple.length();++start){
const std::size_t bytes=std::string_view(text).find(needle,static_cast<std::size_t>(start));
const int expected=bytes==std::string_view::npos?-1:static_cast<int>(bytes);
CHECK(simple.find(needle,start)==expected);
CHECK(general.find(needle,start+1)==(expected<0?-1:expected+1));
const std::size_t last=std::string_view(text).rfind(needle,static_cast<std::size_t>(start));
const int expected_last=last==std::string_view::npos?-1:static_cast<int>(last);
CHECK(simple.rfind(needle,start)==expected_last);
}
CHECK(simple.contains(needle)==(std::string_view(text).find(needle)!=std::string_view::npos));
CHECK(general.contains(needle)==simple.contains(needle));
CHECK(general.count(needle)==simple.count(needle));
}
}
}

TEST_CASE("one-byte find agrees with std::string_view::find at every length, start and position")
{
// A one-byte needle takes its own 16-byte scan with an overlapping, shifted last block: the byte is placed at every
// position of texts up to 70 bytes and searched from every start, with and without a second copy after it.
for(std::size_t size=0;size<=70;++size){
for(std::size_t at=0;at<=size;++at){
std::string text(size,'a');
if(at<size)text[at]='z';
if(at+5<size)text[at+5]='z';
const String s(text);
for(std::size_t start=0;start<=size;++start){
const std::size_t expected=std::string_view(text).find('z',start);
CAPTURE(size);
CAPTURE(at);
CAPTURE(start);
CHECK(s.find("z",static_cast<int>(start))==(expected==std::string_view::npos?-1:static_cast<int>(expected)));
}
}
}
}

TEST_CASE("copies, appends and concatenations keep every byte at every length")
{
// The inline copy handles each size class (1, 2-3, 4-7, 8-15, 16-32, 33-64 bytes) with overlapping loads and stores, and
// memcpy above; every length up to 130 is checked byte for byte, inline and on the heap, at odd offsets as well.
std::string source;
for(int i=0;i<140;++i)source.push_back(static_cast<char>('!'+(i*7)%90));
for(std::size_t n=0;n<=130;++n){
CAPTURE(n);
const std::string_view piece=std::string_view(source).substr(3,n);
const String text(piece);
CHECK(text.bytes()==piece);
const String copy(text);
CHECK(copy.bytes()==piece);
String assigned("x");
assigned=text;
CHECK(assigned.bytes()==piece);
String appended("ab");
appended.append(piece);
CHECK(appended.bytes()==std::string("ab")+std::string(piece));
const String joined=text+String(piece);
CHECK(joined.bytes()==std::string(piece)+std::string(piece));
CHECK(joined.c_str()[joined.byte_length()]=='\0');
}
}

TEST_CASE("heap blocks are recycled per thread, across threads and size classes")
{
// Sizes around every 16-byte block class and past the cached range; blocks are dropped, reused, grown in place (the
// rounded capacity), freed on another thread than the one that made them, and the threads end with full caches.
std::vector<String> made;
for(int size=20;size<=300;++size)made.emplace_back(std::string(static_cast<std::size_t>(size),static_cast<char>('a'+size%26)));
for(int round=0;round<3;++round){
std::vector<String> copies(made.begin(),made.end());
for(std::size_t i=0;i<copies.size();++i){
CHECK(copies[i]==made[i]);
copies[i].append("!");
CHECK(copies[i].byte_length()==made[i].byte_length()+1);
CHECK(copies[i].bytes().substr(0,static_cast<std::size_t>(made[i].byte_length()))==made[i].bytes());
CHECK(copies[i].capacity()>=copies[i].byte_length());
}
}
std::vector<String> handed;
std::thread maker([&handed]{
for(int i=0;i<2000;++i)handed.emplace_back(std::string(static_cast<std::size_t>(24+i%200),'x'));
});
maker.join();
std::thread freer([&handed]{
for(String& s:handed)CHECK(s.byte_length()>=24);
handed.clear();
std::vector<String> local;
for(int i=0;i<2000;++i)local.emplace_back(std::string(static_cast<std::size_t>(24+i%200),'y'));
for(int i=0;i<2000;++i)CHECK(local[static_cast<std::size_t>(i)].bytes()==std::string(static_cast<std::size_t>(24+i%200),'y'));
});
freer.join();
String grown;
for(int i=0;i<500;++i)grown.append("0123456789");
CHECK(grown.byte_length()==5000);
CHECK(grown.count("0123456789")==500);
}

TEST_CASE("code_at on one-byte-per-character text, before and after the count is known")
{
const String ascii="a\x7F" "b";
CHECK(ascii.known_ascii_code_at(0)==-1);
CHECK(ascii.code_at(1)==char32_t{0x7F});
CHECK(ascii.known_ascii_code_at(0)=='a');
CHECK(ascii.known_ascii_code_at(1)==0x7F);
CHECK(ascii.known_ascii_code_at(3)==-1);
CHECK(ascii.known_ascii_code_at(-1)==-1);
// One byte per character but an invalid byte: never the fast path, and code_at gives U+FFFD for it.
const String text="a\x7F\xFF" "b";
CHECK(text.code_at(2)==char32_t{0xFFFD});
CHECK(text.length()==4);
CHECK(text.known_ascii_code_at(0)==-1);
CHECK(text.known_ascii_code_at(2)==-1);
CHECK(text.code_at(0)==U'a');
CHECK(text.code_at(3)==U'b');
CHECK(text.code_at(4)==std::unexpected(StringError::index_out_of_range));
CHECK(text.code_at(-1)==std::unexpected(StringError::index_out_of_range));
const String turkish="ışık";
CHECK(turkish.code_at(0)==U'ı');
CHECK(turkish.known_ascii_code_at(2)==-1);
}

TEST_CASE("search never matches part of a UTF-8 sequence")
{
const String c="ç";
CHECK(c.find("\xA7")==-1);
CHECK(c.find("\xC3")==-1);
CHECK(c.rfind("\xA7")==-1);
CHECK_FALSE(c.contains("\xA7"));
CHECK_FALSE(c.starts_with("\xC3"));
CHECK_FALSE(c.ends_with("\xA7"));
CHECK(c.count("\xA7")==0);
const String stray="ç\xA7";
CHECK(stray.length()==2);
CHECK(stray.find("\xA7")==1);
CHECK(stray.rfind("\xA7")==1);
CHECK(stray.count("\xA7")==1);
CHECK(stray.ends_with("\xA7"));
CHECK(String("\xE2\x82").ends_with("\x82"));
CHECK(String("\xE2\x82\xAC").find("\x82")==-1);
CHECK(c.replace("\xA7","x")=="ç");
CHECK(stray.replace("\xA7","x")=="çx");
const std::vector<String> parts=stray.split("\xA7");
REQUIRE(parts.size()==2);
CHECK(parts[0]=="ç");
CHECK(parts[1]=="");
}

TEST_CASE("substr, left and right clamp counts and reject bad starts")
{
const String s="şehir";
CHECK(s.substr(1,3)=="ehi");
CHECK(s.substr(1)=="ehir");
CHECK(s.substr(1,100)=="ehir");
CHECK(s.substr(5)=="");
CHECK(s.substr(0,0)=="");
CHECK(s.substr(6)==std::unexpected(StringError::index_out_of_range));
CHECK(s.substr(-1)==std::unexpected(StringError::index_out_of_range));
CHECK(s.substr(1,-2)==std::unexpected(StringError::negative_count));
CHECK(s.left(2)=="şe");
CHECK(s.left(0)=="");
CHECK(s.left(100)=="şehir");
CHECK(s.left(-1)==std::unexpected(StringError::negative_count));
CHECK(s.right(2)=="ir");
CHECK(s.right(0)=="");
CHECK(s.right(9)=="şehir");
CHECK(s.right(-1)==std::unexpected(StringError::negative_count));
CHECK(s.substr(1,2)->length()==2);
}

TEST_CASE("insert and erase")
{
const String s="şehir";
CHECK(s.insert(1,"X")=="şXehir");
CHECK(s.insert(0,"ğ")=="ğşehir");
CHECK(s.insert(5,"!")=="şehir!");
CHECK(s.insert(6,"!")==std::unexpected(StringError::index_out_of_range));
CHECK(s.insert(-1,"!")==std::unexpected(StringError::index_out_of_range));
CHECK(s.erase(1,2)=="şir");
CHECK(s.erase(1)=="ş");
CHECK(s.erase(0,100)=="");
CHECK(s.erase(5,1)=="şehir");
CHECK(s.erase(2,0)=="şehir");
CHECK(s.erase(6,0)==std::unexpected(StringError::index_out_of_range));
CHECK(s.erase(-1,1)==std::unexpected(StringError::index_out_of_range));
CHECK(s.erase(1,-2)==std::unexpected(StringError::negative_count));
}

TEST_CASE("split and join")
{
const auto check_parts=[](const std::vector<String>& parts,std::vector<std::string_view> expected){
REQUIRE(parts.size()==expected.size());
for(std::size_t i=0;i<parts.size();++i)CHECK(parts[i]==expected[i]);
};
check_parts(String("a,b,,c").split(","),{"a","b","","c"});
check_parts(String("a,b,,c").split(",",false),{"a","b","c"});
check_parts(String(",a,").split(","),{"","a",""});
check_parts(String(",a,").split(",",false),{"a"});
check_parts(String("").split(","),{""});
check_parts(String("").split(",",false),{});
check_parts(String("aşbşc").split("ş"),{"a","b","c"});
check_parts(String("a::b").split("::"),{"a","b"});
check_parts(String("şa😀").split(""),{"ş","a","😀"});
check_parts(String("").split(""),{});
const std::vector<String> parts{String("x"),String("ş"),String("z")};
CHECK(String::join(parts,", ")=="x, ş, z");
CHECK(String::join({},"-")=="");
CHECK(String::join(std::vector<String>{String("only")},"-")=="only");
}

TEST_CASE("replace")
{
CHECK(String("a-b-c").replace("-","+")=="a+b+c");
CHECK(String("a-b-c").replace("-","+",false)=="a+b-c");
CHECK(String("a-b-c").replace("-","")=="abc");
CHECK(String("a-b-c").replace("-","<->")=="a<->b<->c");
CHECK(String("abc").replace("","x")=="abc");
CHECK(String("abc").replace("x","y")=="abc");
CHECK(String("şş").replace("ş","s")=="ss");
CHECK(String("aaaa").replace("aa","b")=="bb");
String long_text;
for(int i=0;i<100;++i)long_text+="ab";
CHECK(long_text.replace("b","ğğ").byte_length()==500);
}

TEST_CASE("trim uses the White_Space property")
{
CHECK(String("  \t hi \n").trim()=="hi");
CHECK(String("  hi ").trim_left()=="hi ");
CHECK(String("  hi ").trim_right()=="  hi");
CHECK(String(" x　").trim()=="x");
CHECK(String("​x").trim()=="​x");
CHECK(String("   ").trim()=="");
CHECK(String("").trim()=="");
CHECK(String("\xFF ").trim()=="\xFF");
CHECK(String(" \x85").trim()=="\x85");
CHECK(String("x\u0085").trim()=="x");
}

TEST_CASE("lower and upper, invariant and Turkish (Review Focus 5)")
{
CHECK(String("HeLLo, World 123").lower()=="hello, world 123");
CHECK(String("HeLLo, World 123").upper()=="HELLO, WORLD 123");
CHECK(String("I").lower()=="i");
CHECK(String("ŞEHİR").lower()=="şehir");
CHECK(String("ışık").upper()=="IŞIK");
CHECK(String("İstanbul'da IŞIK").lower("tr")=="istanbul'da ışık");
CHECK(String("istanbul ışık").upper("tr")=="İSTANBUL IŞIK");
CHECK(String("İstanbul'da IŞIK").lower("az")=="istanbul'da ışık");
CHECK(String("istanbul ışık").upper("az")=="İSTANBUL IŞIK");
CHECK(String("İstanbul").lower("tr")=="istanbul");
CHECK(String("İ").lower("az")=="i");
CHECK(String("İ").lower()=="i̇");
CHECK(String("Í").lower("tr")=="ı́");
CHECK(String("ISPARTA").lower("TR")=="ısparta");
CHECK(String("ISPARTA").lower("tr-TR")=="ısparta");
CHECK(String("ISPARTA").lower("tr_TR")=="ısparta");
CHECK(String("ISPARTA").lower("en")=="isparta");
CHECK(String("ISPARTA").lower("trx")=="isparta");
CHECK(String("ISPARTA").lower("")=="isparta");
CHECK(String("\xFF" "A\xC3").lower()=="\xFF" "a\xC3");
CHECK(String("\xFF" "A\xC3").lower("tr")=="\xFF" "a\xC3");
std::string many_i(100,'i');
const String upper_tr=String(many_i).upper("tr");
CHECK(upper_tr.length()==100);
CHECK(upper_tr.byte_length()==200);
CHECK(upper_tr.lower("tr")==many_i);
std::string many_dotless;
for(int i=0;i<60;++i)many_dotless+="ı";
CHECK(String(many_dotless).upper()==std::string(60,'I'));
std::string expanding;
for(int i=0;i<50;++i)expanding+="Ⱥ";
const String expanded=String(expanding).lower();
CHECK(expanded.length()==50);
CHECK(expanded.byte_length()==150);
std::u32string mixed;
for(char32_t c=0;c<0x3000;c+=7)if(c<0xD800)mixed.push_back(c);
std::u32string lowered;
std::u32string uppered;
for(const char32_t c:mixed){
lowered.push_back(zodiac::unicode::to_lower(c));
uppered.push_back(zodiac::unicode::to_upper(c));
}
CHECK(String(utf8(mixed)).lower()==utf8(lowered));
CHECK(String(utf8(mixed)).upper()==utf8(uppered));
}

TEST_CASE("After_I skips marks of combining classes other than 0 and 230")
{
CHECK(String("Ị̇").lower("tr")=="ị");
CHECK(String("Ị̇").lower("tr").length()==2);
CHECK(String("Į̣̇x").lower("tr")=="į̣x");
CHECK(String("Í̇").lower("tr")=="ı́̇");
CHECK(String("Ị").lower("tr")=="ı̣");
CHECK(String("Ị̇").lower()=="ị̇");
CHECK(String("İ").lower("tr")=="i");
CHECK(String("İ").lower("tr").is_ascii());
}

TEST_CASE("case mapping results carry exact metadata")
{
const char* inputs[]={"","İ","İstanbul","I","i","ı","İ","IŞIK","istanbul ışık","İstanbul'da IŞIK","ABCDEFGHIJKLMNOP","\xFF\x41","K","KK","Ⱥ","Ị̇","ii","plain ascii text, long enough for words","\xC3\xA7\xA7"};
const auto check=[](const String& result){
const String fresh(result.bytes());
CAPTURE(hex_bytes(result.bytes()));
CHECK(result.length()==fresh.length());
CHECK(result.is_ascii()==fresh.is_ascii());
};
for(const char* input:inputs){
const String s(input);
check(s.lower());
check(s.upper());
check(s.lower("tr"));
check(s.upper("tr"));
check(s.lower("az"));
check(s.upper("az"));
}
}

TEST_CASE("results that fit stay inline")
{
for(int size=13;size<=23;++size){
CAPTURE(size);
const String s(std::string(static_cast<std::size_t>(size),'A'));
CHECK(stored_inline(s.lower()));
CHECK(stored_inline(s.upper()));
CHECK(stored_inline(s.lower("tr")));
CHECK(stored_inline(s.replace("A","b")));
CHECK(stored_inline(s.replace("A","",false)));
CHECK(stored_inline(s.normalize()));
}
CHECK(stored_inline(String("abcdefghijklm é").normalize()));
CHECK(String("abcdefghijklm é").normalize()=="abcdefghijklm é");
CHECK(stored_inline(String("iiiiiiiiiii").upper("tr")));
CHECK(String("iiiiiiiiiii").upper("tr").byte_length()==22);
CHECK_FALSE(stored_inline(String("iiiiiiiiiiii").upper("tr")));
CHECK(String("iiiiiiiiiiii").upper("tr").length()==12);
}

TEST_CASE("parts keep a known character count")
{
const String s="  şehir şehir  ";
CHECK(s.length()==15);
const String trimmed=s.trim();
CHECK(trimmed.length()==11);
CHECK(trimmed.length()==String(trimmed.bytes()).length());
CHECK(s.substr(2,5)->length()==5);
CHECK(s.erase(0,2)->length()==13);
CHECK((s+"ğ").length()==16);
CHECK(("ğ"+s).length()==16);
const String joined=String("\xC3")+std::string_view("\xA7");
CHECK(joined.length()==1);
const String erased=String("\xC3" "x\xA7").erase(1,1).value();
CHECK(erased=="ç");
CHECK(erased.length()==1);
const String simple="\xFF" "a,b";
CHECK(simple.length()==4);
const std::vector<String> parts=simple.split(",");
REQUIRE(parts.size()==2);
CHECK(parts[0].length()==2);
CHECK_FALSE(parts[0].is_ascii());
CHECK(parts[1].is_ascii());
CHECK(String("abc").substr(1)->is_ascii());
const String mixed="\xFF" "abc";
CHECK(mixed.length()==4);
const String tail=mixed.substr(1).value();
CHECK(tail.is_ascii());
CHECK(tail.length()==3);
}

TEST_CASE("concurrent const access to one string is safe")
{
std::u32string model;
for(int i=0;i<3000;++i)model.push_back(i%4==0?U'ş':i%4==1?U'中':char32_t(U'a'+i%26));
const std::string bytes=utf8(model);
const String reference(bytes);
std::vector<int> positions;
for(int i=0;i<3000;i+=97)positions.push_back(i);
std::vector<std::u32string> expected_chars;
for(const int i:positions)expected_chars.push_back(model.substr(static_cast<std::size_t>(i),1));
const std::string needle=utf8(model.substr(2500,6));
const int expected_find=reference.find(needle).value();
const std::uint64_t expected_hash=reference.hash();
for(int round=0;round<20;++round){
const String shared(bytes);
std::atomic<bool> go{false};
std::atomic<int> failures{0};
std::vector<std::thread> threads;
for(int t=0;t<8;++t){
threads.emplace_back([&,t]{
while(!go.load(std::memory_order_acquire)){}
for(int k=0;k<4;++k){
const std::size_t first=static_cast<std::size_t>(t+k)%positions.size();
for(std::size_t j=0;j<positions.size();++j){
const std::size_t n=(first+j)%positions.size();
if(shared.char_at(positions[n])!=utf8(expected_chars[n]))++failures;
}
if(shared.length()!=3000)++failures;
if(shared.hash()!=expected_hash)++failures;
if(shared.find(needle)!=expected_find)++failures;
if(shared.is_ascii())++failures;
const String copy=shared;
if(copy.hash()!=expected_hash||copy.length()!=3000)++failures;
}
});
}
go.store(true,std::memory_order_release);
for(std::thread& thread:threads)thread.join();
CHECK(failures.load()==0);
}
}

TEST_CASE("repeat, reverse and normalize")
{
CHECK(String("ab").repeat(3)=="ababab");
CHECK(String("ab").repeat(0)=="");
CHECK(String("").repeat(5)=="");
CHECK(String("ab").repeat(-1)==std::unexpected(StringError::negative_count));
CHECK(String("ş").repeat(30)->length()==30);
CHECK(String("şehir").reverse()=="riheş");
CHECK(String("😀a").reverse()=="a😀");
CHECK(String("").reverse()=="");
CHECK(String(std::string(40,'a')+"b").reverse()==String("b"+std::string(40,'a')));
CHECK(String("ş").normalize()=="ş");
CHECK(String("İ").normalize()=="İ");
CHECK(String("plain").normalize()=="plain");
CHECK(String("şehir").normalize()=="şehir");
CHECK(String("é\xFF").normalize()=="é\xFF");
CHECK(String("e\xFF́").normalize()=="e\xFF́");
CHECK(String("각").normalize()=="각");
}

TEST_CASE("comparisons are by bytes, which is code point order")
{
CHECK(String("a")<String("b"));
CHECK(String("z")<String("ş"));
CHECK(String("ab")>String("a"));
CHECK(String("ab")==std::string_view("ab"));
CHECK(String("ab")!="abc");
CHECK(String("")<String("a"));
CHECK((String("abc")<=>String("abd"))==std::strong_ordering::less);
CHECK((String("abc")<=>"abc")==std::strong_ordering::equal);
CHECK(String(std::string_view("a\0b",3))!=String("a"));
CHECK(String(std::string_view("a\0b",3)).byte_length()==3);
}

TEST_CASE("hash is equal for equal bytes, whatever the storage")
{
const String a(std::string(30,'h'));
String b(std::string(20,'h'));
b+=std::string(10,'h');
CHECK(a.hash()==b.hash());
CHECK(String("ş").hash()==String::from_code_point(U'ş').hash());
CHECK(String("a").hash()!=String("b").hash());
CHECK(String("").hash()!=String(std::string_view("\0",1)).hash());
const std::uint64_t before=b.hash();
b+="x";
CHECK(b.hash()!=before);
CHECK(b.hash()==String(b.bytes()).hash());
}

TEST_CASE("table_hash is equal for equal bytes, whatever the storage")
{
const String a(std::string(30,'h'));
String b(std::string(20,'h'));
b+=std::string(10,'h');
const String shared=a;
CHECK(a.table_hash()==b.table_hash());
CHECK(shared.table_hash()==a.table_hash());
CHECK(String("ş").table_hash()==String::from_code_point(U'ş').table_hash());
CHECK(String("a").table_hash()!=String("b").table_hash());
CHECK(String("").table_hash()!=String(std::string_view("\0",1)).table_hash());
// Stable within the process: a second call (the cached value for heap text) and a fresh copy agree.
const std::uint64_t first=a.table_hash();
CHECK(a.table_hash()==first);
CHECK(String(a.bytes()).table_hash()==first);
CHECK(String("abc").table_hash()==String("abc").table_hash());
const std::uint64_t before=b.table_hash();
b+="x";
CHECK(b.table_hash()!=before);
CHECK(b.table_hash()==String(b.bytes()).table_hash());
String inline_text("short");
const std::uint64_t inline_before=inline_text.table_hash();
inline_text+="er";
CHECK(inline_text.table_hash()!=inline_before);
CHECK(inline_text.table_hash()==String("shorter").table_hash());
// Keyed per process, so it differs from the stable script hash.
CHECK(String("abc").table_hash()!=String("abc").hash());
CHECK(a.table_hash()!=a.hash());
CHECK(std::hash<String>{}(a)==static_cast<std::size_t>(a.table_hash()));
CHECK(std::hash<String>{}(String("abc"))==static_cast<std::size_t>(String("abc").table_hash()));
}

TEST_CASE("script hash stays uncached and stable on heap strings")
{
// Over 48 bytes: heap storage and the hash's three-lane path. hash() must not read the cached table hash.
const std::string bytes="İstanbul'da IŞIK ve ışık, uzun bir cümle ile 48 baytı geçen metin";
constexpr std::uint64_t golden=512401696878949126u;
const String text(bytes);
REQUIRE(text.byte_length()>String::inline_capacity);
CHECK(text.hash()==golden);
CHECK(text.hash()==zodiac::string_detail::hash_bytes(bytes.data(),bytes.size()));
const std::uint64_t keyed=text.table_hash();
CHECK(text.table_hash()==keyed);
CHECK(text.hash()==golden);
CHECK(text.hash()!=keyed);
const String copy=text;
CHECK(copy.table_hash()==keyed);
CHECK(copy.hash()==golden);
}

TEST_CASE("hash values are fixed across platforms and runs")
{
// Golden values (one per hashing path: empty, 1-3, 4-16, 17-48 and over 48 bytes); CI checks them on Windows and Linux.
CHECK(String("").hash()==9182185294049140429u);
CHECK(String("a").hash()==10092782711508069508u);
CHECK(String("abc").hash()==5175409476211137323u);
CHECK(String("şehir").hash()==7540264406729466443u);
CHECK(String("0123456789abcdef").hash()==1371159094663155661u);
CHECK(String("0123456789abcdefg").hash()==15622335290923033891u);
CHECK(String("İstanbul'da IŞIK ve ışık, uzun bir cümle ile 48 baytı geçen metin").hash()==512401696878949126u);
}

TEST_CASE("cached metadata matches a fresh scan after random mutations (Review Focus 1)")
{
const char32_t alphabet[]={U'a',U'Z',U'0',U' ',U'\t',U'ş',U'ğ',U'İ',U'ı',U'é',char32_t{0x0307},U'中',U'語',char32_t{0x1F600},char32_t{0x10FFFF},char32_t{0x7F},char32_t{0x80},char32_t{0x7FF},char32_t{0x800}};
std::mt19937_64 random(4242);
const auto pick=[&](std::uint64_t n){return static_cast<int>(random()%n);};
const auto piece=[&](int max_length){
std::u32string text;
const int n=pick(static_cast<std::uint64_t>(max_length)+1);
const bool ascii=pick(3)==0;
for(int i=0;i<n;++i)text.push_back(ascii?char32_t(U'a'+pick(26)):alphabet[pick(std::size(alphabet))]);
return text;
};
String s;
std::u32string model;
int failures=0;
const auto verify=[&](const String& value,const std::u32string& expected,const char* what){
const std::string bytes=utf8(expected);
bool ascii=true;
for(const char32_t c:expected)ascii=ascii&&c<0x80;
const String fresh(bytes);
bool ok=value.bytes()==bytes&&value.length()==static_cast<int>(expected.size())&&value.length()==fresh.length();
ok=ok&&value.is_ascii()==ascii&&fresh.is_ascii()==ascii&&value.hash()==fresh.hash();
ok=ok&&value.data()[value.byte_length()]=='\0';
for(int k=0;ok&&k<4&&!expected.empty();++k){
const int i=pick(expected.size());
ok=value.code_at(i)==expected[static_cast<std::size_t>(i)]&&value.char_at(i)==utf8(expected.substr(static_cast<std::size_t>(i),1));
}
if(!expected.empty()){
const int last=static_cast<int>(expected.size())-1;
ok=ok&&value.code_at(last)==expected.back();
}
if(!ok&&++failures<=5){
INFO(what);
INFO(hex_bytes(value.bytes()));
CHECK(false);
}
};
for(int iteration=0;iteration<10000;++iteration){
const char* what="";
switch(pick(10)){
case 0:{
const std::u32string p=piece(40);
s+=String(utf8(p));
model+=p;
what="+= String";
break;
}
case 1:{
const std::u32string p=piece(40);
const std::string bytes=utf8(p);
s+=std::string_view(bytes);
model+=p;
what="+= view";
break;
}
case 2:{
const char32_t c=alphabet[pick(std::size(alphabet))];
s+=c;
model.push_back(c);
what="+= code point";
break;
}
case 3:{
const int at=pick(model.size()+1);
const std::u32string p=piece(10);
s=s.insert(at,utf8(p)).value();
model.insert(static_cast<std::size_t>(at),p);
what="insert";
break;
}
case 4:{
const int start=pick(model.size()+1);
const int count=pick(20);
s=s.erase(start,count).value();
model.erase(static_cast<std::size_t>(start),static_cast<std::size_t>(count));
what="erase";
break;
}
case 5:{
const std::u32string p=piece(60);
const std::string bytes=utf8(p);
s=std::string_view(bytes);
model=p;
what="assign bytes";
break;
}
case 6:{
String copy=s;
const std::u32string copy_model=model;
if(!copy_model.empty())(void)copy.char_at(pick(copy_model.size()));
const std::u32string p=piece(30);
s+=String(utf8(p));
model+=p;
verify(copy,copy_model,"copy after the original changed");
copy+="!";
verify(s,model,"original after the copy changed");
what="[] on a copy";
break;
}
case 7:{
const int start=pick(model.size()+1);
const int count=pick(50);
s=s.substr(start,count).value();
model=model.substr(static_cast<std::size_t>(start),static_cast<std::size_t>(count));
what="substr";
break;
}
case 8:{
s+=42;
model+=U"42";
s+=true;
model+=U"true";
what="+= number and bool";
break;
}
default:{
String other=s;
const std::u32string p=piece(20);
other=String(utf8(p));
s=std::move(other);
model=p;
what="move assign";
break;
}
}
if(model.size()>700){
s=s.substr(300).value();
model.erase(0,300);
}
if(pick(4)!=0)verify(s,model,what);
}
verify(s,model,"final");
CHECK(failures==0);
}

TEST_CASE("appending bytes that complete an open sequence recounts")
{
String a="x\xC3";
CHECK(a.length()==2);
a+=String("\xA7");
CHECK(a.length()==2);
CHECK(a=="xç");
String b(std::string(40,'b')+"\xE2\x82");
CHECK(b.length()==42);
b+=std::string_view("\xAC");
CHECK(b.length()==41);
CHECK(b.code_at(40)==char32_t{0x20AC});
CHECK(String("\xC3")+String("\xA7")=="ç");
CHECK((String("\xC3")+String("\xA7")).length()==1);
String c="\xC3";
CHECK(c.length()==1);
const auto repeated=String("\xA7\xC3").repeat(3);
CHECK(repeated->length()==4);
CHECK(String("\xA7\xC3").reverse().length()==1);
String long_text;
for(int i=0;i<200;++i)long_text+="ş";
CHECK(long_text.char_at(199)=="ş");
long_text+="\xC5";
CHECK(long_text.length()==201);
long_text+="\x9F";
CHECK(long_text.length()==201);
CHECK(long_text.char_at(200)=="ş");
// An index entry on a stray continuation byte (character 64) that the append turns into the middle of a character.
String merged;
for(int i=0;i<63;++i)merged+="ş";
merged+=std::string_view("\xE2\x82");
CHECK(merged.length()==65);
CHECK(merged.char_at(64)=="\x82");
merged+=std::string_view("\xAC");
merged+=std::string(40,'a');
CHECK(merged.length()==104);
CHECK(merged.code_at(63)==char32_t{0x20AC});
CHECK(merged.char_at(100)=="a");
CHECK(merged.find("a")==64);
}

TEST_CASE("invalid UTF-8 at the ends is counted per byte and never read past the end (Review Focus 2)")
{
CHECK(String("\xC5").length()==1);
CHECK(String("a\xC5").length()==2);
CHECK(String(std::string_view("\xE2\x82\xAC",2)).length()==2);
CHECK(String(std::string_view("\xF0\x9F\x98\x80",3)).length()==3);
CHECK(String("\xC0\x80").length()==2);
CHECK(String("\xE0\x80\x80").length()==3);
CHECK(String("\xED\xA0\x80").length()==3);
CHECK(String("\xF4\x90\x80\x80").length()==4);
CHECK(String("\xF8\x88\x80\x80\x80").length()==5);
CHECK(String("\xFF\xFE").length()==2);
CHECK(String("\xED\x9F\xBF").length()==1);
CHECK(String("\xF4\x8F\xBF\xBF").length()==1);
for(const char* text:{"\xC5","a\xC5","\xC0\x80","\xE0\x80\x80","\xED\xA0\x80","\xF8\x88\x80\x80\x80","\xE2\x82"}){
CAPTURE(hex_bytes(text));
CHECK(check_characters(String(text))=="");
}
// Exhaustive: every byte string of length 1 and 2, and every string of length 3 and 4 over two bytes from each UTF-8 byte
// class of Table 3-7 (the decoder's behaviour depends only on the class), which covers every decoding path at both ends.
std::string first_failure;
int failures=0;
const auto run=[&](std::string_view bytes){
const String s(bytes);
const std::string result=check_characters(s);
if(!result.empty()&&failures++==0)first_failure=hex_bytes(bytes)+": "+result;
};
std::string bytes;
for(int a=0;a<256;++a){
bytes.assign(1,static_cast<char>(a));
run(bytes);
for(int b=0;b<256;++b){
bytes.assign({static_cast<char>(a),static_cast<char>(b)});
run(bytes);
}
}
const unsigned char classes[]={0x00,0x7F,0x80,0x8F,0x90,0x9F,0xA0,0xBF,0xC0,0xC1,0xC2,0xDF,0xE0,0xE1,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF3,0xF4,0xF5,0xFF};
for(const unsigned char a:classes)for(const unsigned char b:classes)for(const unsigned char c:classes){
bytes.assign({static_cast<char>(a),static_cast<char>(b),static_cast<char>(c)});
run(bytes);
for(const unsigned char d:classes){
bytes.push_back(static_cast<char>(d));
run(bytes);
bytes.pop_back();
}
}
CHECK(first_failure=="");
CHECK(failures==0);
// Random byte strings biased towards UTF-8 structure, long enough to reach the heap and the sparse index.
std::mt19937_64 random(7);
const unsigned char interesting[]={0x80,0xBF,0xC2,0xC5,0xDF,0xE0,0xE2,0xED,0xEF,0xF0,0xF4,0xF5,0xFF,0x9F,0x82,0xA0};
for(int i=0;i<100000;++i){
const std::size_t n=random()%(i%10==0?200:40);
bytes.clear();
for(std::size_t k=0;k<n;++k){
const std::uint64_t r=random();
bytes.push_back(static_cast<char>(r%3==0?static_cast<unsigned char>(r>>8):r%3==1?interesting[(r>>8)%std::size(interesting)]:static_cast<unsigned char>('a'+(r>>8)%26)));
}
run(bytes);
}
CHECK(first_failure=="");
CHECK(failures==0);
}

TEST_CASE("every byte string of length 3 (ZODIAC_LONG_TESTS=1)"*doctest::skip(!long_tests_enabled()))
{
std::string first_failure;
int failures=0;
std::string bytes(3,'\0');
for(int a=0;a<256;++a)for(int b=0;b<256;++b)for(int c=0;c<256;++c){
bytes[0]=static_cast<char>(a);
bytes[1]=static_cast<char>(b);
bytes[2]=static_cast<char>(c);
const std::string result=check_characters(String(bytes));
if(!result.empty()&&failures++==0)first_failure=hex_bytes(bytes)+": "+result;
}
CHECK(first_failure=="");
CHECK(failures==0);
}
