// Lookups over the generated two-level tables (unicode_tables.hpp) and the NFC algorithm of UAX #15.
#include <core/types/unicode.hpp>
#include <core/types/unicode_tables.hpp>
#include <cstddef>
#include <cstdint>
#include <iterator>

namespace zodiac::unicode {
namespace {
namespace t=tables;
static_assert(detail::ascii_whitespace_mask==t::whitespace_ascii_mask,"the inline ASCII White_Space mask no longer matches PropList.txt");
constexpr char32_t max_code_point=0x10FFFF;
constexpr char32_t hangul_s_base=0xAC00;
constexpr char32_t hangul_l_base=0x1100;
constexpr char32_t hangul_v_base=0x1161;
constexpr char32_t hangul_t_base=0x11A7;
constexpr char32_t hangul_l_count=19;
constexpr char32_t hangul_v_count=21;
constexpr char32_t hangul_t_count=28;
constexpr char32_t hangul_n_count=hangul_v_count*hangul_t_count;
constexpr char32_t hangul_s_count=hangul_l_count*hangul_n_count;
constexpr char32_t code_point_mask=(char32_t{1}<<t::ccc_shift)-1;
constexpr std::uint8_t qc_not_yes=t::qc_no|t::qc_maybe;
static_assert(max_code_point<=code_point_mask,"code points must fit below the packed combining class");
// The value of a two-level table for c, which must be below the table's limit.
template<unsigned Shift,typename Stage1,std::size_t N1,typename Stage2,std::size_t N2>
inline Stage2 two_level(const Stage1 (&stage1)[N1],const Stage2 (&stage2)[N2],char32_t c) noexcept
{
return stage2[(static_cast<std::size_t>(stage1[c>>Shift])<<Shift)|(c&((char32_t{1}<<Shift)-1))];
}
inline const t::CaseDelta& case_delta(char32_t c) noexcept
{
return t::case_deltas[c<t::case_limit?two_level<t::case_shift>(t::case_stage1,t::case_stage2,c):0];
}
inline t::NormProperties properties(char32_t c) noexcept
{
return t::norm_properties[c<t::property_limit?two_level<t::property_shift>(t::property_stage1,t::property_stage2,c):0];
}
inline t::NormMapping mapping(char32_t c) noexcept
{
return t::norm_mappings[c<t::mapping_limit?two_level<t::mapping_shift>(t::mapping_stage1,t::mapping_stage2,c):0];
}
// True when normalization may restart at c: ccc 0 and NFC_QC=Yes, so nothing before c reorders or composes with it.
// gen_unicode.py checks that the decomposition of such a character starts with a starter that does not combine backward either.
inline bool is_restart_point(char32_t c) noexcept
{
if(c<t::quick_check_limit)return true;
const t::NormProperties p=properties(c);
return p.ccc==0&&(p.flags&qc_not_yes)==0;
}
// The index of the first character where the NFC quick check does not answer Yes, or text.size() when text is NFC.
std::size_t quick_check_failure(std::u32string_view text) noexcept
{
unsigned last_ccc=0;
for(std::size_t i=0;i<text.size();++i){
const char32_t c=text[i];
if(c<t::quick_check_limit){
last_ccc=0;
continue;
}
const t::NormProperties p=properties(c);
if((p.flags&qc_not_yes)!=0||(p.ccc!=0&&p.ccc<last_ccc))return i;
last_ccc=p.ccc;
}
return text.size();
}
// Appends packed (a code point with its combining class from ccc_shift up) to out and moves it before the marks of a higher
// class that precede it: canonical ordering, stable, never past a starter or before begin.
inline void append_ordered(std::u32string& out,std::size_t begin,char32_t packed)
{
out.push_back(packed);
const char32_t ccc=packed>>t::ccc_shift;
if(ccc==0)return;
std::size_t i=out.size()-1;
while(i>begin&&(out[i-1]>>t::ccc_shift)>ccc){
out[i]=out[i-1];
--i;
}
out[i]=packed;
}
// Appends the canonical decomposition of segment (code points only) to out in canonical order, entries packed as for append_ordered.
void decompose(std::u32string_view segment,std::u32string& out)
{
const std::size_t begin=out.size();
for(const char32_t c:segment){
if(c-hangul_s_base<hangul_s_count){
const char32_t index=c-hangul_s_base;
out.push_back(hangul_l_base+index/hangul_n_count);
out.push_back(hangul_v_base+index%hangul_n_count/hangul_t_count);
if(index%hangul_t_count!=0)out.push_back(hangul_t_base+index%hangul_t_count);
continue;
}
const std::uint16_t decomposition=mapping(c).decomposition;
if(decomposition!=0){
const char32_t* part=t::decomposition_pool+(decomposition>>t::decomposition_length_bits);
const char32_t* const end=part+(decomposition&((1u<<t::decomposition_length_bits)-1));
for(;part!=end;++part)append_ordered(out,begin,*part);
continue;
}
append_ordered(out,begin,c<t::quick_check_limit?c:c|char32_t{properties(c).ccc}<<t::ccc_shift);
}
}
// The primary composite of starter followed by c, or 0 when they do not compose.
char32_t compose_pair(char32_t starter,char32_t c) noexcept
{
if(starter-hangul_l_base<hangul_l_count&&c-hangul_v_base<hangul_v_count)return hangul_s_base+((starter-hangul_l_base)*hangul_v_count+(c-hangul_v_base))*hangul_t_count;
if(starter-hangul_s_base<hangul_s_count)return (starter-hangul_s_base)%hangul_t_count==0&&c-(hangul_t_base+1)<hangul_t_count-1?starter+(c-hangul_t_base):0;
const std::uint16_t composition=mapping(starter).composition;
const char32_t* second=t::composition_seconds+(composition>>t::composition_count_bits);
const char32_t* const end=second+(composition&((1u<<t::composition_count_bits)-1));
for(;second!=end&&*second<=c;++second){
if(*second==c)return t::composition_results[second-t::composition_seconds];
}
return 0;
}
// Canonical composition of out[begin..] in place. The entries come packed from decompose() and leave as plain code points.
// A character composes with the last starter when it directly follows it or every character in between has a lower class.
void compose(std::u32string& out,std::size_t begin)
{
constexpr std::size_t none=static_cast<std::size_t>(-1);
std::size_t starter=none;
unsigned last_ccc=0;
std::size_t write=begin;
for(std::size_t read=begin;read<out.size();++read){
const char32_t c=out[read]&code_point_mask;
const unsigned ccc=out[read]>>t::ccc_shift;
if(starter!=none&&(write==starter+1||last_ccc<ccc)){
const char32_t composite=compose_pair(out[starter],c);
if(composite!=0){
out[starter]=composite;
continue;
}
}
if(ccc==0)starter=write;
else last_ccc=ccc;
out[write++]=c;
}
out.resize(write);
}
// Appends the NFC of text to out. Values above U+10FFFF are copied as they are and split text into separately normalized runs.
void normalize(std::u32string_view text,std::u32string& out)
{
std::size_t i=0;
while(i<text.size()){
std::size_t end=i;
while(end<text.size()&&text[end]<=max_code_point)++end;
if(end>i){
const std::size_t begin=out.size();
decompose(text.substr(i,end-i),out);
compose(out,begin);
}
if(end<text.size())out.push_back(text[end++]);
i=end;
}
}
}

namespace detail {
char32_t to_lower_table(char32_t c) noexcept
{
return c+static_cast<char32_t>(case_delta(c).lower);
}
char32_t to_upper_table(char32_t c) noexcept
{
return c+static_cast<char32_t>(case_delta(c).upper);
}
bool is_whitespace_table(char32_t c) noexcept
{
// One comparison for everything above the last range (U+3000): most CJK, Hangul and supplementary text.
if(c>t::whitespace_ranges[std::size(t::whitespace_ranges)-1].last)return false;
for(const t::CodePointRange& range:t::whitespace_ranges){
if(c<range.first)return false;
if(c<=range.last)return true;
}
return false;
}
}

bool quick_check_nfc(std::u32string_view text) noexcept
{
return quick_check_failure(text)==text.size();
}

std::u32string nfc(std::u32string_view text)
{
const std::size_t failure=quick_check_failure(text);
if(failure==text.size())return std::u32string(text);
std::size_t start=failure;
while(start>0&&!is_restart_point(text[start]))--start;
std::u32string out;
// Decomposing usually less than doubles the renormalized part, and composing shrinks it again.
out.reserve(start+2*(text.size()-start));
out.append(text.substr(0,start));
normalize(text.substr(start),out);
return out;
}
}
