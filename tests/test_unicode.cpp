// Tests zodiac::unicode: simple and Turkic case mapping, White_Space, and NFC against the committed NormalizationTest-16.0.0 vectors.
#include <core/types/unicode.hpp>
#include <doctest/doctest.h>
#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
std::u32string parse_code_points(std::string_view field)
{
std::u32string text;
char32_t value=0;
bool digits=false;
for(const char c:field){
if(c==' '){
if(digits)text.push_back(value);
value=0;
digits=false;
continue;
}
const unsigned digit=c>='A'?static_cast<unsigned>(c-'A'+10):static_cast<unsigned>(c-'0');
value=value*16+digit;
digits=true;
}
if(digits)text.push_back(value);
return text;
}
std::vector<std::string_view> split_fields(std::string_view line)
{
std::vector<std::string_view> fields;
std::size_t start=0;
for(;;){
const std::size_t end=line.find(';',start);
fields.push_back(line.substr(start,end==std::string_view::npos?std::string_view::npos:end-start));
if(end==std::string_view::npos)return fields;
start=end+1;
}
}
std::string hex(std::u32string_view text)
{
std::string out;
constexpr char digits[]="0123456789ABCDEF";
for(const char32_t c:text){
if(!out.empty())out.push_back(' ');
bool started=false;
for(int shift=28;shift>=0;shift-=4){
const unsigned digit=(c>>shift)&0xF;
if(digit!=0||started||shift==0){
out.push_back(digits[digit]);
started=true;
}
}
}
return out;
}
}

TEST_CASE("to_lower and to_upper use the locale-independent simple mapping")
{
CHECK(zodiac::unicode::to_lower(U'A')==U'a');
CHECK(zodiac::unicode::to_lower(U'a')==U'a');
CHECK(zodiac::unicode::to_lower(U'1')==U'1');
CHECK(zodiac::unicode::to_upper(U'z')==U'Z');
CHECK(zodiac::unicode::to_upper(U'ş')==U'Ş');
CHECK(zodiac::unicode::to_lower(U'Ş')==U'ş');
CHECK(zodiac::unicode::to_lower(U'I')==U'i');
CHECK(zodiac::unicode::to_upper(U'i')==U'I');
CHECK(zodiac::unicode::to_lower(U'İ')==U'i');
CHECK(zodiac::unicode::to_upper(U'ı')==U'I');
CHECK(zodiac::unicode::to_upper(U'ğ')==U'Ğ');
CHECK(zodiac::unicode::to_lower(U'Ç')==U'ç');
CHECK(zodiac::unicode::to_lower(U'Ö')==U'ö');
CHECK(zodiac::unicode::to_upper(U'ü')==U'Ü');
CHECK(zodiac::unicode::to_lower(U'Ω')==U'ω');
CHECK(zodiac::unicode::to_lower(U'Ж')==U'ж');
CHECK(zodiac::unicode::to_lower(U'\U0001E900')==U'\U0001E922');
CHECK(zodiac::unicode::to_upper(U'ꭰ')==U'Ꭰ');
CHECK(zodiac::unicode::to_upper(U'ß')==U'ß');
CHECK(zodiac::unicode::to_lower(U'ẞ')==U'ß');
CHECK(zodiac::unicode::to_lower(U'中')==U'中');
CHECK(zodiac::unicode::to_lower(char32_t{0x110000})==char32_t{0x110000});
CHECK(zodiac::unicode::to_upper(char32_t{0xFFFFFFFF})==char32_t{0xFFFFFFFF});
CHECK(zodiac::unicode::to_lower(char32_t{0xD800})==char32_t{0xD800});
}

TEST_CASE("Turkic case mapping applies the dotted and dotless I rules")
{
CHECK(zodiac::unicode::to_lower_turkic(U'I')==U'ı');
CHECK(zodiac::unicode::to_lower_turkic(U'İ')==U'i');
CHECK(zodiac::unicode::to_upper_turkic(U'i')==U'İ');
CHECK(zodiac::unicode::to_upper_turkic(U'ı')==U'I');
CHECK(zodiac::unicode::to_lower_turkic(U'Ş')==U'ş');
CHECK(zodiac::unicode::to_upper_turkic(U'ç')==U'Ç');
CHECK(zodiac::unicode::to_lower_turkic(U'A')==U'a');
CHECK(zodiac::unicode::to_upper_turkic(U'a')==U'A');
}

TEST_CASE("Turkish text round-trips through the Turkic mappings")
{
const std::u32string upper=U"İSTANBUL'DA IŞIK ĞÜÇÖ";
const std::u32string lower=U"istanbul'da ışık ğüçö";
std::u32string lowered;
for(const char32_t c:upper)lowered.push_back(zodiac::unicode::to_lower_turkic(c));
CHECK(lowered==lower);
std::u32string uppered;
for(const char32_t c:lower)uppered.push_back(zodiac::unicode::to_upper_turkic(c));
CHECK(uppered==upper);
std::u32string invariant;
for(const char32_t c:std::u32string_view(U"IŞIK"))invariant.push_back(zodiac::unicode::to_lower(c));
CHECK(invariant==U"işik");
}

TEST_CASE("is_whitespace follows the White_Space property")
{
CHECK(zodiac::unicode::is_whitespace(U' '));
for(const char32_t c:std::u32string_view(U"\t\n\v\f\r\u0085         　"))CHECK(zodiac::unicode::is_whitespace(c));
for(const char32_t c:std::u32string_view(U"a0_​᠎﻿\u0008\u001F、ş"))CHECK_FALSE(zodiac::unicode::is_whitespace(c));
CHECK_FALSE(zodiac::unicode::is_whitespace(char32_t{0x110000}));
CHECK_FALSE(zodiac::unicode::is_whitespace(char32_t{0x20|0x1000000}));
std::size_t count=0;
for(char32_t c=0;c<=0x10FFFF;++c)if(zodiac::unicode::is_whitespace(c))++count;
CHECK(count==25);
}

TEST_CASE("combining_class reports Canonical_Combining_Class")
{
CHECK(zodiac::unicode::combining_class(U'A')==0);
CHECK(zodiac::unicode::combining_class(U'ş')==0);
CHECK(zodiac::unicode::combining_class(char32_t{0x0300})==230);
CHECK(zodiac::unicode::combining_class(char32_t{0x0307})==230);
CHECK(zodiac::unicode::combining_class(char32_t{0x0323})==220);
CHECK(zodiac::unicode::combining_class(char32_t{0x0327})==202);
CHECK(zodiac::unicode::combining_class(char32_t{0x05B0})==10);
CHECK(zodiac::unicode::combining_class(char32_t{0x1D165})==216);
CHECK(zodiac::unicode::combining_class(U'中')==0);
CHECK(zodiac::unicode::combining_class(char32_t{0x110000})==0);
}

TEST_CASE("nfc keeps Turkish text, composes decomposed text and Hangul")
{
CHECK(zodiac::unicode::nfc(U"ş")==U"ş");
CHECK(zodiac::unicode::nfc(U"ş")==U"ş");
CHECK(zodiac::unicode::nfc(U"İ")==U"İ");
CHECK(zodiac::unicode::nfc(U"İ")==U"İ");
CHECK(zodiac::unicode::nfc(U"")==U"");
const std::u32string already=U"Merhaba dünya, şehir ve ığdır İzmir — 中文 한국어";
CHECK(zodiac::unicode::nfc(already)==already);
CHECK(zodiac::unicode::quick_check_nfc(already));
CHECK(zodiac::unicode::nfc(U"한글")==U"한글");
CHECK(zodiac::unicode::nfc(U"한ᆯ")==U"한ᆯ");
CHECK(zodiac::unicode::nfc(U"한")==U"한");
CHECK(zodiac::unicode::nfc(U"ạ́b")==U"ạ́b");
CHECK(zodiac::unicode::nfc(U"Å")==U"Å");
CHECK(zodiac::unicode::nfc(U"क़")==U"क़");
CHECK(zodiac::unicode::nfc(U"̈́")==U"̈́");
CHECK_FALSE(zodiac::unicode::quick_check_nfc(U"é"));
}

TEST_CASE("nfc passes values above U+10FFFF through as boundaries")
{
const std::u32string text{U'e',char32_t{0x110000},U'́',U'e',U'́',char32_t{0xFFFFFFFF},U'̣',U'̂'};
const std::u32string expected{U'e',char32_t{0x110000},U'́',U'é',char32_t{0xFFFFFFFF},U'̣',U'̂'};
CHECK(zodiac::unicode::nfc(text)==expected);
}

TEST_CASE("nfc matches NormalizationTest-16.0.0")
{
std::ifstream file(ZODIAC_TEST_UNICODE_VECTORS,std::ios::binary);
REQUIRE(file.is_open());
std::unordered_set<char32_t> part1;
std::size_t rows=0;
std::size_t failures=0;
int part=-1;
// Each vector row is also checked behind NFC prefixes, so nfc() takes its prefix-copy and restart path: nfc(P+source)==P+wanted.
// Both prefixes end in U+4E2D, which composes with nothing. The second adds two U+0334 (ccc 1, the lowest class, NFC_QC=Yes):
// no mark of a row reorders before them and none composes across them, but a row failing the quick check makes nfc() back up over
// both marks to U+4E2D, copying only the "x" before it.
const std::u32string_view prefixes[]={U"x中",U"x中̴̴"};
for(const std::u32string_view prefix:prefixes)REQUIRE(zodiac::unicode::quick_check_nfc(prefix));
const auto check=[&](const std::u32string& source,const std::u32string& wanted){
const std::u32string got=zodiac::unicode::nfc(source);
if(got==wanted)return;
if(++failures<=10)MESSAGE(doctest::String(("nfc("+hex(source)+") is "+hex(got)+", expected "+hex(wanted)).c_str()));
};
const auto expect=[&](std::u32string_view source,const std::u32string& wanted,bool with_prefixes=true){
check(std::u32string(source),wanted);
if(!with_prefixes)return;
for(const std::u32string_view prefix:prefixes)check(std::u32string(prefix)+std::u32string(source),std::u32string(prefix)+wanted);
};
std::string line;
while(std::getline(file,line)){
if(!line.empty()&&line.back()=='\r')line.pop_back();
if(line.empty()||line[0]=='#')continue;
const std::string_view view(line);
if(view.starts_with("@part ")){
part=std::stoi(line.substr(6));
continue;
}
if(view.starts_with("@hangul ")){
const std::vector<std::string_view> range=split_fields(view.substr(8));
REQUIRE(range.size()==2);
const char32_t first=parse_code_points(range[0]).at(0);
const char32_t last=parse_code_points(range[1]).at(0);
for(char32_t s=first;s<=last;++s){
const char32_t index=s-0xAC00;
std::u32string decomposed{static_cast<char32_t>(0x1100+index/588),static_cast<char32_t>(0x1161+index%588/28)};
if(index%28!=0)decomposed.push_back(0x11A7+index%28);
const std::u32string syllable(1,s);
expect(syllable,syllable);
expect(decomposed,syllable);
part1.insert(s);
++rows;
}
continue;
}
const std::vector<std::string_view> fields=split_fields(view);
REQUIRE(fields.size()==3);
const std::u32string source=parse_code_points(fields[0]);
const std::u32string composed=fields[1].empty()?source:parse_code_points(fields[1]);
const std::u32string decomposed=fields[2].empty()?composed:parse_code_points(fields[2]);
expect(source,composed);
expect(composed,composed);
expect(decomposed,composed);
if(part==1){
REQUIRE(source.size()==1);
part1.insert(source[0]);
}
++rows;
}
CHECK(rows==19965);
for(char32_t c=0;c<=0x10FFFF;++c){
if((c>=0xD800&&c<=0xDFFF)||part1.contains(c))continue;
const std::u32string single(1,c);
expect(single,single,false);
}
CHECK(failures==0);
}
