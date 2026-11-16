// zodiac::String (string.hpp): UTF-8 scanning with word-at-a-time ASCII fast paths, the heap block and its sparse character
// index, the hash, searching on character boundaries, case mapping, NFC runs, and number formatting and parsing.
#include <core/types/string.hpp>
#include <core/types/unicode.hpp>
#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <locale.h>
#include <new>
#include <stdlib.h>
#include <string>
#include <system_error>

namespace zodiac {
namespace {
using byte=unsigned char;
using string_detail::meta_ascii;
using string_detail::meta_length;
using string_detail::meta_unknown;
constexpr std::uint64_t high_bits=0x8080808080808080u;
constexpr std::uint64_t low_bits=0x0101010101010101u;
constexpr std::uint32_t max_size=static_cast<std::uint32_t>(String::max_byte_length);
constexpr std::uint32_t step=static_cast<std::uint32_t>(String::index_step);
constexpr std::size_t npos=std::string_view::npos;

[[noreturn]] void too_long()
{
throw std::bad_alloc();
}
inline const byte* ubytes(const char* p) noexcept
{
return reinterpret_cast<const byte*>(p);
}
inline std::uint64_t load_word(const byte* p) noexcept
{
std::uint64_t word;
std::memcpy(&word,p,8);
return word;
}
inline void store_word(char* p,std::uint64_t word) noexcept
{
std::memcpy(p,&word,8);
}
// Bytes in front of the first byte with its high bit set, for a word loaded from memory whose high bits are not all clear.
inline unsigned ascii_prefix(std::uint64_t word) noexcept
{
const std::uint64_t high=word&high_bits;
if constexpr(std::endian::native==std::endian::little){
return static_cast<unsigned>(std::countr_zero(high))>>3;
}else{
return static_cast<unsigned>(std::countl_zero(high))>>3;
}
}
inline bool has_byte(std::uint64_t word,byte value) noexcept
{
const std::uint64_t x=word^(low_bits*value);
return ((x-low_bits)&~x&high_bits)!=0;
}
// Case mapping of eight ASCII bytes at once (every byte below 0x80, so the additions never carry between bytes).
inline std::uint64_t ascii_lower(std::uint64_t word) noexcept
{
const std::uint64_t at_least_a=word+low_bits*(0x80-'A');
const std::uint64_t above_z=word+low_bits*(0x80-'Z'-1);
return word|(((at_least_a^above_z)&high_bits)>>2);
}
inline std::uint64_t ascii_upper(std::uint64_t word) noexcept
{
const std::uint64_t at_least_a=word+low_bits*(0x80-'a');
const std::uint64_t above_z=word+low_bits*(0x80-'z'-1);
return word^(((at_least_a^above_z)&high_bits)>>2);
}
inline bool is_continuation(byte b) noexcept
{
return (b&0xC0)==0x80;
}
// Bytes of the character at p: the length of the well-formed sequence starting there (Unicode Table 3-7), or 1.
inline std::uint32_t char_size(const byte* p,const byte* end) noexcept
{
const byte lead=*p;
if(lead<0xC2)return 1;
const std::ptrdiff_t left=end-p;
if(lead<0xE0)return left>=2&&is_continuation(p[1])?2:1;
if(lead<0xF0){
if(left<3)return 1;
const byte low=lead==0xE0?0xA0:0x80;
const byte high=lead==0xED?0x9F:0xBF;
return p[1]>=low&&p[1]<=high&&is_continuation(p[2])?3:1;
}
if(lead<0xF5){
if(left<4)return 1;
const byte low=lead==0xF0?0x90:0x80;
const byte high=lead==0xF4?0x8F:0xBF;
return p[1]>=low&&p[1]<=high&&is_continuation(p[2])&&is_continuation(p[3])?4:1;
}
return 1;
}
// The code point of the character of size bytes at p (size from char_size); U+FFFD for an invalid byte.
inline char32_t decode(const byte* p,std::uint32_t size) noexcept
{
switch(size){
case 1:
return p[0]<0x80?static_cast<char32_t>(p[0]):char32_t{0xFFFD};
case 2:
return static_cast<char32_t>(((p[0]&0x1Fu)<<6)|(p[1]&0x3Fu));
case 3:
return static_cast<char32_t>(((p[0]&0x0Fu)<<12)|((p[1]&0x3Fu)<<6)|(p[2]&0x3Fu));
default:
return static_cast<char32_t>(((p[0]&0x07u)<<18)|((p[1]&0x3Fu)<<12)|((p[2]&0x3Fu)<<6)|(p[3]&0x3Fu));
}
}
// Writes the UTF-8 encoding of c (U+FFFD for surrogates and values above U+10FFFF) and returns its size.
inline std::uint32_t encode(char32_t c,char* out) noexcept
{
if(c<0x80){
out[0]=static_cast<char>(c);
return 1;
}
if(c<0x800){
out[0]=static_cast<char>(0xC0|(c>>6));
out[1]=static_cast<char>(0x80|(c&0x3F));
return 2;
}
if(c>0x10FFFF||(c>=0xD800&&c<0xE000))c=0xFFFD;
if(c<0x10000){
out[0]=static_cast<char>(0xE0|(c>>12));
out[1]=static_cast<char>(0x80|((c>>6)&0x3F));
out[2]=static_cast<char>(0x80|(c&0x3F));
return 3;
}
out[0]=static_cast<char>(0xF0|(c>>18));
out[1]=static_cast<char>(0x80|((c>>12)&0x3F));
out[2]=static_cast<char>(0x80|((c>>6)&0x3F));
out[3]=static_cast<char>(0x80|(c&0x3F));
return 4;
}
// Characters in [p, end), and whether every byte is ASCII.
std::uint32_t count_chars(const byte* p,const byte* end,bool& ascii) noexcept
{
std::uint32_t count=0;
bool all_ascii=true;
while(p<end){
if(end-p>=8){
const std::uint64_t word=load_word(p);
if((word&high_bits)==0){
p+=8;
count+=8;
continue;
}
const unsigned prefix=ascii_prefix(word);
p+=prefix;
count+=prefix;
}else if(*p<0x80){
++p;
++count;
continue;
}
all_ascii=false;
p+=char_size(p,end);
++count;
}
ascii=all_ascii;
return count;
}
// The position n characters after p (or end, if fewer remain).
const byte* skip_chars(const byte* p,const byte* end,std::uint32_t n) noexcept
{
while(n!=0&&p<end){
if(n>=8&&end-p>=8){
const std::uint64_t word=load_word(p);
if((word&high_bits)==0){
p+=8;
n-=8;
continue;
}
const unsigned prefix=ascii_prefix(word);
if(prefix!=0){
p+=prefix;
n-=prefix;
continue;
}
}
p+=*p<0x80?1:char_size(p,end);
--n;
}
return p;
}
// True when p (in [begin, end]) is not inside a well-formed sequence. Only a sequence starting at the nearest preceding
// non-continuation byte can cover p, so at most three bytes are examined.
inline bool is_boundary(const byte* begin,const byte* end,const byte* p) noexcept
{
if(p==begin||p==end||!is_continuation(*p))return true;
for(std::ptrdiff_t k=1;k<=3&&p-k>=begin;++k){
const byte* start=p-k;
if(!is_continuation(*start))return !(*start>=0xC2&&static_cast<std::ptrdiff_t>(char_size(start,end))>k);
}
return true;
}
// The start of the character ending at the boundary e (begin < e, begin a boundary).
inline const byte* previous_char(const byte* begin,const byte* e) noexcept
{
if(!is_continuation(e[-1]))return e-1;
for(std::ptrdiff_t k=2;k<=4&&e-k>=begin;++k){
const byte* start=e-k;
if(!is_continuation(*start))return static_cast<std::ptrdiff_t>(char_size(start,e))==k?start:e-1;
}
return e-1;
}
// Byte offset of the first match at or after from that covers whole characters.
std::size_t find_aligned(std::string_view text,std::string_view needle,std::size_t from) noexcept
{
const byte* begin=ubytes(text.data());
const byte* end=begin+text.size();
for(;;){
const std::size_t pos=text.find(needle,from);
if(pos==npos||(is_boundary(begin,end,begin+pos)&&is_boundary(begin,end,begin+pos+needle.size())))return pos;
from=pos+1;
}
}
// Byte offset of the last match starting at or before from that covers whole characters.
std::size_t rfind_aligned(std::string_view text,std::string_view needle,std::size_t from) noexcept
{
const byte* begin=ubytes(text.data());
const byte* end=begin+text.size();
for(;;){
const std::size_t pos=text.rfind(needle,from);
if(pos==npos||(is_boundary(begin,end,begin+pos)&&is_boundary(begin,end,begin+pos+needle.size())))return pos;
if(pos==0)return npos;
from=pos-1;
}
}
inline std::uint32_t joined_meta(std::uint32_t a,std::uint32_t b,std::string_view b_bytes) noexcept
{
// Appending cannot change earlier characters unless the new bytes start with a continuation byte that completes a
// sequence the old end left open; otherwise counts add up.
if(a==meta_unknown||b==meta_unknown)return meta_unknown;
if(!b_bytes.empty()&&is_continuation(static_cast<byte>(b_bytes[0])))return meta_unknown;
return ((a&meta_length)+(b&meta_length))|(a&b&meta_ascii);
}
std::uint32_t format_double(double value,char* out) noexcept
{
if(std::isnan(value)){
std::memcpy(out,"nan",3);
return 3;
}
if(std::isinf(value)){
if(value<0){
std::memcpy(out,"-inf",4);
return 4;
}
std::memcpy(out,"inf",3);
return 3;
}
if(value==0.0){
out[0]='0';
return 1;
}
return static_cast<std::uint32_t>(std::to_chars(out,out+32,value).ptr-out);
}

// The hash: wyhash's structure (multiply-fold mixing of 8-byte little-endian reads) with a fixed seed.
constexpr std::uint64_t hash_seed=0x2D358DCCAA6C78A5u;
constexpr std::uint64_t secret0=0xA0761D6478BD642Fu;
constexpr std::uint64_t secret1=0xE7037ED1A0B428DBu;
constexpr std::uint64_t secret2=0x8EBC6AF09C88C6DBu;
constexpr std::uint64_t secret3=0x589965CC75374CC3u;
inline void multiply(std::uint64_t& a,std::uint64_t& b) noexcept
{
const unsigned __int128 product=static_cast<unsigned __int128>(a)*b;
a=static_cast<std::uint64_t>(product);
b=static_cast<std::uint64_t>(product>>64);
}
inline std::uint64_t mix(std::uint64_t a,std::uint64_t b) noexcept
{
multiply(a,b);
return a^b;
}
inline std::uint64_t read64(const byte* p) noexcept
{
std::uint64_t value;
std::memcpy(&value,p,8);
if constexpr(std::endian::native==std::endian::big)value=std::byteswap(value);
return value;
}
inline std::uint64_t read32(const byte* p) noexcept
{
std::uint32_t value;
std::memcpy(&value,p,4);
if constexpr(std::endian::native==std::endian::big)value=std::byteswap(value);
return value;
}

// The heap block: this header, then capacity bytes and the NUL. index is the sparse character index or null:
// index[0] entries built, index[1] entries allocated, index[2+k] the byte offset of character k*step.
struct HeapHeader {
std::uint32_t* index;
};
char* allocate_block(std::uint32_t capacity)
{
void* block=::operator new(sizeof(HeapHeader)+std::size_t{capacity}+1);
auto* header=::new(block) HeapHeader{nullptr};
return reinterpret_cast<char*>(header+1);
}
inline HeapHeader* header_of(char* ptr) noexcept
{
return reinterpret_cast<HeapHeader*>(ptr)-1;
}

// Number parsing. Clinger's fast path is exact when the significand fits in 53 bits and the power of ten is exact.
constexpr double exact_powers[]={1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,1e20,1e21,1e22};
inline bool is_digit(char c) noexcept
{
return static_cast<unsigned char>(c-'0')<10;
}
inline bool equals_ignoring_case(std::string_view text,std::string_view lower) noexcept
{
if(text.size()!=lower.size())return false;
for(std::size_t i=0;i<text.size();++i){
if((text[i]|0x20)!=lower[i])return false;
}
return true;
}
// Correctly rounded conversion of text that already matched the grammar; text is NUL-terminated.
// from_chars where the library has it for double (libc++ 20 and later); strtod in the "C" locale otherwise and for
// results out of range (overflow to infinity, underflow and subnormals as strtod rounds them).
double convert_slow(std::string_view text) noexcept
{
#if defined(_LIBCPP_VERSION)&&_LIBCPP_VERSION>=200000
const char* first=text.data()+(text[0]=='+'?1:0);
double value=0;
const std::from_chars_result result=std::from_chars(first,text.data()+text.size(),value);
if(result.ec==std::errc{}&&result.ptr==text.data()+text.size())return value;
#endif
#if defined(_WIN32)
static const _locale_t c_locale=_create_locale(LC_ALL,"C");
return _strtod_l(text.data(),nullptr,c_locale);
#else
static const locale_t c_locale=newlocale(LC_ALL_MASK,"C",static_cast<locale_t>(0));
return strtod_l(text.data(),nullptr,c_locale);
#endif
}
// Parses the whole of text (to_number's grammar); text is NUL-terminated.
bool parse_number(std::string_view text,double& value) noexcept
{
const char* p=text.data();
const char* end=p+text.size();
bool negative=false;
if(p<end&&(*p=='+'||*p=='-')){
negative=*p=='-';
++p;
}
if(p==end)return false;
if(!is_digit(*p)&&*p!='.'){
const std::string_view word(p,static_cast<std::size_t>(end-p));
if(equals_ignoring_case(word,"inf")||equals_ignoring_case(word,"infinity")){
value=negative?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();
return true;
}
if(equals_ignoring_case(word,"nan")){
value=std::numeric_limits<double>::quiet_NaN();
return true;
}
return false;
}
std::uint64_t significand=0;
int digits=0;
long long exponent=0;
bool any_digit=false;
bool inexact=false;
for(;p<end&&is_digit(*p);++p){
any_digit=true;
const auto digit=static_cast<unsigned>(*p-'0');
if(significand==0&&digit==0)continue;
if(digits<19){
significand=significand*10+digit;
++digits;
}else{
++exponent;
inexact=inexact||digit!=0;
}
}
if(p<end&&*p=='.'){
for(++p;p<end&&is_digit(*p);++p){
any_digit=true;
const auto digit=static_cast<unsigned>(*p-'0');
if(significand==0&&digit==0){
--exponent;
continue;
}
if(digits<19){
significand=significand*10+digit;
++digits;
--exponent;
}else{
inexact=inexact||digit!=0;
}
}
}
if(!any_digit)return false;
if(p<end&&(*p=='e'||*p=='E')){
++p;
bool negative_exponent=false;
if(p<end&&(*p=='+'||*p=='-')){
negative_exponent=*p=='-';
++p;
}
if(p==end||!is_digit(*p))return false;
long long written=0;
for(;p<end&&is_digit(*p);++p){
if(written<1000000)written=written*10+(*p-'0');
}
exponent+=negative_exponent?-written:written;
}
if(p!=end)return false;
if(significand==0){
value=negative?-0.0:0.0;
return true;
}
if(!inexact&&significand<=(std::uint64_t{1}<<53)&&exponent>=-22&&exponent<=22){
double result=static_cast<double>(significand);
result=exponent<0?result/exact_powers[-exponent]:result*exact_powers[exponent];
value=negative?-result:result;
return true;
}
value=convert_slow(text);
return true;
}
}

// Writes a new string front to back with amortized growth; used by operations whose result size is not known up front.
class StringWriter {
public:
explicit StringWriter(std::size_t capacity)
{
base_=out_.raw_reserve(std::min<std::size_t>(capacity,max_size));
cursor_=base_;
limit_=base_+out_.capacity_bytes();
}
// Room for at least size more bytes at the returned cursor; advance() commits what was written.
char* reserve(std::size_t size)
{
if(static_cast<std::size_t>(limit_-cursor_)<size)grow(size);
return cursor_;
}
void advance(std::size_t size) noexcept
{
cursor_+=size;
}
void put(const char* bytes,std::size_t size)
{
if(size==0)return;
std::memcpy(reserve(size),bytes,size);
cursor_+=size;
}
void put_byte(char value)
{
*reserve(1)=value;
++cursor_;
}
String finish(std::uint32_t meta)
{
out_.raw_set_size(static_cast<std::uint32_t>(cursor_-base_),meta);
return std::move(out_);
}
private:
void grow(std::size_t size)
{
const auto used=static_cast<std::size_t>(cursor_-base_);
if(size>max_size-used)too_long();
const std::size_t doubled=std::min<std::size_t>(std::size_t{out_.capacity_bytes()}*2,max_size);
out_.raw_set_size(static_cast<std::uint32_t>(used),meta_unknown);
base_=out_.raw_reserve(std::max(used+size,doubled));
cursor_=base_+used;
limit_=base_+out_.capacity_bytes();
}
String out_;
char* base_;
char* cursor_;
char* limit_;
};

namespace {
template<bool Upper,bool Turkic>
String map_case(std::string_view text)
{
const byte* p=ubytes(text.data());
const byte* end=p+text.size();
StringWriter out(text.size()+text.size()/4+8);
std::uint32_t count=0;
bool ascii=true;
constexpr byte trigger=Upper?'i':'I';
while(p<end){
if(end-p>=8){
const std::uint64_t word=load_word(p);
if((word&high_bits)==0&&(!Turkic||!has_byte(word,trigger))){
store_word(out.reserve(8),Upper?ascii_upper(word):ascii_lower(word));
out.advance(8);
p+=8;
count+=8;
continue;
}
}
const byte b=*p;
++count;
if(b<0x80){
if(Turkic&&b==trigger){
char* w=out.reserve(2);
ascii=false;
if constexpr(Upper){
w[0]=static_cast<char>(0xC4);
w[1]=static_cast<char>(0xB0);
out.advance(2);
++p;
}else if(end-p>=3&&p[1]==0xCC&&p[2]==0x87){
// SpecialCasing After_I: I followed by U+0307 COMBINING DOT ABOVE is i.
w[0]='i';
out.advance(1);
p+=3;
}else{
w[0]=static_cast<char>(0xC4);
w[1]=static_cast<char>(0xB1);
out.advance(2);
++p;
}
continue;
}
char mapped=static_cast<char>(b);
if constexpr(Upper){
if(static_cast<byte>(b-'a')<26)mapped=static_cast<char>(b-32);
}else{
if(static_cast<byte>(b-'A')<26)mapped=static_cast<char>(b+32);
}
out.put_byte(mapped);
++p;
continue;
}
const std::uint32_t n=char_size(p,end);
if(n==1){
out.put_byte(static_cast<char>(b));
ascii=false;
++p;
continue;
}
const char32_t c=decode(p,n);
char32_t mapped;
if constexpr(Turkic){
mapped=Upper?unicode::to_upper_turkic(c):unicode::to_lower_turkic(c);
}else{
mapped=Upper?unicode::to_upper(c):unicode::to_lower(c);
}
if(mapped==c){
out.put(reinterpret_cast<const char*>(p),n);
ascii=false;
}else{
out.advance(encode(mapped,out.reserve(4)));
ascii=ascii&&mapped<0x80;
}
p+=n;
}
// Every character maps to exactly one complete character (After_I drops one), so the count is exact.
return out.finish(count|(ascii?meta_ascii:0));
}
// "tr" or "az" in any case, alone or followed by '-' or '_' and more.
bool is_turkic_locale(std::string_view locale) noexcept
{
if(locale.size()<2||(locale.size()>2&&locale[2]!='-'&&locale[2]!='_'))return false;
const char a=static_cast<char>(locale[0]|0x20);
const char b=static_cast<char>(locale[1]|0x20);
return (a=='t'&&b=='r')||(a=='a'&&b=='z');
}
}

namespace string_detail {
std::uint64_t hash_bytes(const char* data,std::size_t size) noexcept
{
const byte* p=ubytes(data);
std::uint64_t seed=hash_seed^mix(hash_seed^secret0,secret1);
std::uint64_t a;
std::uint64_t b;
if(size<=16){
if(size>=4){
const std::size_t shift=(size>>3)<<2;
a=(read32(p)<<32)|read32(p+shift);
b=(read32(p+size-4)<<32)|read32(p+size-4-shift);
}else if(size>0){
a=(std::uint64_t{p[0]}<<16)|(std::uint64_t{p[size>>1]}<<8)|p[size-1];
b=0;
}else{
a=0;
b=0;
}
}else{
std::size_t i=size;
if(i>48){
std::uint64_t see1=seed;
std::uint64_t see2=seed;
do{
seed=mix(read64(p)^secret1,read64(p+8)^seed);
see1=mix(read64(p+16)^secret2,read64(p+24)^see1);
see2=mix(read64(p+32)^secret3,read64(p+40)^see2);
p+=48;
i-=48;
}while(i>48);
seed^=see1^see2;
}
while(i>16){
seed=mix(read64(p)^secret1,read64(p+8)^seed);
i-=16;
p+=16;
}
a=read64(p+i-16);
b=read64(p+i-8);
}
a^=secret1;
b^=seed;
multiply(a,b);
return mix(a^secret0^size,b^secret1);
}
}

String::String(const String& other)
{
if(!other.is_heap()){
rep_=other.rep_;
return;
}
const std::uint32_t size=other.size_bytes();
if(size<=static_cast<std::uint32_t>(inline_capacity)){
std::memcpy(rep_.storage.buffer,other.rep_.storage.heap.ptr,std::size_t{size}+1);
rep_.size=size;
rep_.meta=other.rep_.meta;
return;
}
char* ptr=allocate_block(size);
std::memcpy(ptr,other.rep_.storage.heap.ptr,std::size_t{size}+1);
rep_.storage.heap={ptr,size,other.rep_.storage.heap.hash_known,other.rep_.storage.heap.hash};
rep_.size=size|heap_bit;
rep_.meta=other.rep_.meta;
}

String& String::operator=(const String& other)
{
if(this==&other)return *this;
const std::uint32_t size=other.size_bytes();
if(!is_heap()){
if(!other.is_heap()){
rep_=other.rep_;
return *this;
}
if(size<=static_cast<std::uint32_t>(inline_capacity)){
std::memcpy(rep_.storage.buffer,other.rep_.storage.heap.ptr,std::size_t{size}+1);
rep_.size=size;
rep_.meta=other.rep_.meta;
return *this;
}
}else if(rep_.storage.heap.capacity>=size){
free_index();
std::memcpy(rep_.storage.heap.ptr,other.data(),std::size_t{size}+1);
rep_.size=size|heap_bit;
rep_.meta=other.rep_.meta;
rep_.storage.heap.hash_known=other.is_heap()?other.rep_.storage.heap.hash_known:0;
rep_.storage.heap.hash=other.is_heap()?other.rep_.storage.heap.hash:0;
return *this;
}
String copy(other);
return *this=std::move(copy);
}

String& String::operator=(std::string_view bytes)
{
if(bytes.size()>max_size)too_long();
const auto size=static_cast<std::uint32_t>(bytes.size());
if(size>capacity_bytes()){
// Copy first: bytes may view the storage about to be released.
String fresh(bytes);
return *this=std::move(fresh);
}
char* d=mutable_data();
if(size!=0)std::memmove(d,bytes.data(),size);
raw_set_size(size,meta_unknown);
return *this;
}

void String::init(const char* bytes,std::size_t size,std::uint32_t meta)
{
if(size>max_size)too_long();
const auto n=static_cast<std::uint32_t>(size);
if(n<=static_cast<std::uint32_t>(inline_capacity)){
if(n!=0)std::memcpy(rep_.storage.buffer,bytes,n);
rep_.storage.buffer[n]='\0';
rep_.size=n;
}else{
char* ptr=allocate_block(n);
std::memcpy(ptr,bytes,n);
ptr[n]='\0';
rep_.storage.heap={ptr,n,0,0};
rep_.size=n|heap_bit;
}
rep_.meta=n==0?meta_ascii:meta;
}

void String::release() noexcept
{
HeapHeader* header=header_of(rep_.storage.heap.ptr);
if(header->index!=nullptr)::operator delete(header->index);
::operator delete(static_cast<void*>(header));
}

void String::free_index() noexcept
{
HeapHeader* header=header_of(rep_.storage.heap.ptr);
if(header->index!=nullptr){
::operator delete(header->index);
header->index=nullptr;
}
}

int String::scan() const noexcept
{
const byte* p=ubytes(data());
bool ascii=true;
const std::uint32_t count=count_chars(p,p+size_bytes(),ascii);
rep_.meta=count|(ascii?meta_ascii:0);
return static_cast<int>(count);
}

std::uint64_t String::cache_hash() const noexcept
{
const std::uint64_t value=string_detail::hash_bytes(rep_.storage.heap.ptr,size_bytes());
rep_.storage.heap.hash=value;
rep_.storage.heap.hash_known=1;
return value;
}

void String::reallocate(std::uint32_t capacity)
{
const std::uint32_t size=size_bytes();
char* ptr=allocate_block(capacity);
std::memcpy(ptr,data(),size);
ptr[size]='\0';
if(is_heap()){
HeapHeader* old_header=header_of(rep_.storage.heap.ptr);
header_of(ptr)->index=old_header->index;
::operator delete(static_cast<void*>(old_header));
rep_.storage.heap.ptr=ptr;
rep_.storage.heap.capacity=capacity;
}else{
rep_.storage.heap={ptr,capacity,0,0};
rep_.size|=heap_bit;
}
}

char* String::raw_reserve(std::size_t capacity)
{
if(capacity>max_size)too_long();
if(capacity>capacity_bytes())reallocate(static_cast<std::uint32_t>(capacity));
return mutable_data();
}

void String::raw_set_size(std::uint32_t size,std::uint32_t meta) noexcept
{
mutable_data()[size]='\0';
rep_.size=size|(rep_.size&heap_bit);
rep_.meta=size==0?meta_ascii:meta;
if(is_heap()){
rep_.storage.heap.hash_known=0;
free_index();
}
}

void String::reserve(int bytes)
{
if(bytes>0)raw_reserve(static_cast<std::size_t>(bytes));
}

void String::clear() noexcept
{
raw_set_size(0,meta_ascii);
}

String& String::append_raw(const char* bytes,std::size_t size,std::uint32_t meta)
{
if(size==0)return *this;
const std::uint32_t old_size=size_bytes();
if(size>max_size-old_size)too_long();
const auto total=old_size+static_cast<std::uint32_t>(size);
const std::uint32_t new_meta=joined_meta(rep_.meta,meta,std::string_view(bytes,size));
if(total>capacity_bytes()){
const auto base=reinterpret_cast<std::uintptr_t>(data());
const auto source=reinterpret_cast<std::uintptr_t>(bytes);
const bool own=source>=base&&source<base+old_size;
reallocate(std::max(total,std::min(capacity_bytes()*2,max_size)));
if(own)bytes=data()+(source-base);
}
char* d=mutable_data();
std::memcpy(d+old_size,bytes,size);
d[total]='\0';
rep_.size=total|(rep_.size&heap_bit);
rep_.meta=new_meta;
if(is_heap()){
rep_.storage.heap.hash_known=0;
// Earlier characters (and so the index entries) change only when the new bytes start with a continuation byte that
// completes a sequence the old end left open; otherwise the index stays and index() extends it.
if(is_continuation(static_cast<byte>(bytes[0])))free_index();
}
return *this;
}

String& String::append(const String& other)
{
return append_raw(other.data(),other.size_bytes(),other.rep_.meta);
}

String& String::append(std::string_view bytes)
{
std::uint32_t meta=meta_unknown;
// Short appends to a string whose metadata is known keep it known (a cheap count instead of a later full rescan).
if(rep_.meta!=meta_unknown&&!bytes.empty()&&bytes.size()<=64){
const byte* p=ubytes(bytes.data());
bool ascii=true;
const std::uint32_t count=count_chars(p,p+bytes.size(),ascii);
meta=count|(ascii?meta_ascii:0);
}
return append_raw(bytes.data(),bytes.size(),meta);
}

String& String::append(char32_t code_point)
{
char buffer[4];
const std::uint32_t size=encode(code_point,buffer);
return append_raw(buffer,size,size==1?1|meta_ascii:1);
}

String& String::append(bool value)
{
return value?append_raw("true",4,4|meta_ascii):append_raw("false",5,5|meta_ascii);
}

String& String::append(double value)
{
char buffer[32];
const std::uint32_t size=format_double(value,buffer);
return append_raw(buffer,size,size|meta_ascii);
}

String& String::append_signed(long long value)
{
char buffer[24];
const auto size=static_cast<std::uint32_t>(std::to_chars(buffer,buffer+sizeof(buffer),value).ptr-buffer);
return append_raw(buffer,size,size|meta_ascii);
}

String& String::append_unsigned(unsigned long long value)
{
char buffer[24];
const auto size=static_cast<std::uint32_t>(std::to_chars(buffer,buffer+sizeof(buffer),value).ptr-buffer);
return append_raw(buffer,size,size|meta_ascii);
}

String String::concat(std::string_view a,std::uint32_t a_meta,std::string_view b,std::uint32_t b_meta)
{
if(a.size()+b.size()>max_size)too_long();
String out=with_size(a.size()+b.size());
char* d=out.mutable_data();
if(!a.empty())std::memcpy(d,a.data(),a.size());
if(!b.empty())std::memcpy(d+a.size(),b.data(),b.size());
if(!out.empty())out.rep_.meta=a.empty()?b_meta:joined_meta(a_meta,b_meta,b);
return out;
}

String String::with_size(std::size_t size)
{
String out;
out.raw_reserve(size);
out.raw_set_size(static_cast<std::uint32_t>(size),meta_unknown);
return out;
}

String String::from_code_point(char32_t code_point)
{
String text;
text.append(code_point);
return text;
}

String String::from_bool(bool value)
{
return value?String("true",4,4|meta_ascii):String("false",5,5|meta_ascii);
}

String String::from_number(double value)
{
char buffer[32];
const std::uint32_t size=format_double(value,buffer);
return String(buffer,size,size|meta_ascii);
}

const std::uint32_t* String::index() const
{
char* ptr=rep_.storage.heap.ptr;
HeapHeader* header=header_of(ptr);
const std::uint32_t needed=((rep_.meta&meta_length)-1)/step+1;
std::uint32_t* block=header->index;
if(block==nullptr||block[1]<needed){
const std::uint32_t capacity=block==nullptr?needed:std::max(needed,block[1]*2);
auto* grown=static_cast<std::uint32_t*>(::operator new(sizeof(std::uint32_t)*(std::size_t{capacity}+2)));
if(block==nullptr){
grown[0]=1;
grown[2]=0;
}else{
std::memcpy(grown,block,sizeof(std::uint32_t)*(std::size_t{block[0]}+2));
::operator delete(block);
}
grown[1]=capacity;
header->index=block=grown;
}
std::uint32_t built=block[0];
if(built<needed){
const byte* begin=ubytes(ptr);
const byte* end=begin+size_bytes();
const byte* p=begin+block[1+built];
for(;built<needed;++built){
p=skip_chars(p,end,step);
block[2+built]=static_cast<std::uint32_t>(p-begin);
}
block[0]=built;
}
return block+2;
}

std::uint32_t String::char_to_byte(std::uint32_t position) const
{
const std::uint32_t size=size_bytes();
const std::uint32_t length=rep_.meta&meta_length;
if(length==size)return position;
if(position==length)return size;
const byte* begin=ubytes(data());
const byte* end=begin+size;
if(position<step||!is_heap())return static_cast<std::uint32_t>(skip_chars(begin,end,position)-begin);
const std::uint32_t k=position/step;
const std::uint32_t offset=index()[k];
return static_cast<std::uint32_t>(skip_chars(begin+offset,end,position-k*step)-begin);
}

std::uint32_t String::byte_to_char(std::uint32_t offset) const
{
const std::uint32_t size=size_bytes();
const std::uint32_t length=rep_.meta&meta_length;
if(length==size)return offset;
if(offset==size)return length;
const byte* begin=ubytes(data());
bool ascii=true;
if(offset<=4*step||!is_heap())return count_chars(begin,begin+offset,ascii);
const std::uint32_t* offsets=index();
const std::uint32_t entries=(length-1)/step+1;
const auto k=static_cast<std::uint32_t>(std::upper_bound(offsets,offsets+entries,offset)-offsets)-1;
return k*step+count_chars(begin+offsets[k],begin+offset,ascii);
}

String String::bytes_between(std::uint32_t begin,std::uint32_t end,std::uint32_t meta) const
{
if(begin==0&&end==size_bytes())return *this;
return String(data()+begin,end-begin,meta);
}

std::expected<String,StringError> String::char_at(int index) const
{
const int length=this->length();
if(index<0||index>=length)return std::unexpected(StringError::index_out_of_range);
const std::uint32_t offset=char_to_byte(static_cast<std::uint32_t>(index));
const byte* begin=ubytes(data());
const byte* p=begin+offset;
const std::uint32_t size=static_cast<std::uint32_t>(length)==size_bytes()?1:char_size(p,begin+size_bytes());
return String(data()+offset,size,*p<0x80?1|meta_ascii:1);
}

std::expected<char32_t,StringError> String::code_at(int index) const
{
const int length=this->length();
if(index<0||index>=length)return std::unexpected(StringError::index_out_of_range);
const byte* begin=ubytes(data());
const byte* p=begin+char_to_byte(static_cast<std::uint32_t>(index));
const std::uint32_t size=static_cast<std::uint32_t>(length)==size_bytes()?1:char_size(p,begin+size_bytes());
return decode(p,size);
}

std::expected<std::uint8_t,StringError> String::byte_at(int index) const
{
if(index<0||index>=byte_length())return std::unexpected(StringError::index_out_of_range);
return static_cast<std::uint8_t>(data()[index]);
}

std::expected<int,StringError> String::find(std::string_view needle,int start) const
{
const int length=this->length();
if(start<0||start>length)return std::unexpected(StringError::index_out_of_range);
if(needle.empty())return start;
const std::size_t pos=find_aligned(bytes(),needle,char_to_byte(static_cast<std::uint32_t>(start)));
if(pos==npos)return -1;
return static_cast<int>(byte_to_char(static_cast<std::uint32_t>(pos)));
}

std::expected<int,StringError> String::rfind(std::string_view needle,int start) const
{
const int length=this->length();
if(start==-1)start=length;
else if(start<0||start>length)return std::unexpected(StringError::index_out_of_range);
if(needle.empty())return start;
const std::size_t pos=rfind_aligned(bytes(),needle,char_to_byte(static_cast<std::uint32_t>(start)));
if(pos==npos)return -1;
return static_cast<int>(byte_to_char(static_cast<std::uint32_t>(pos)));
}

bool String::contains(std::string_view needle) const noexcept
{
return needle.empty()||find_aligned(bytes(),needle,0)!=npos;
}

bool String::starts_with(std::string_view prefix) const noexcept
{
const std::string_view text=bytes();
if(!text.starts_with(prefix))return false;
const byte* begin=ubytes(text.data());
return is_boundary(begin,begin+text.size(),begin+prefix.size());
}

bool String::ends_with(std::string_view suffix) const noexcept
{
const std::string_view text=bytes();
if(!text.ends_with(suffix))return false;
const byte* begin=ubytes(text.data());
return is_boundary(begin,begin+text.size(),begin+text.size()-suffix.size());
}

int String::count(std::string_view needle) const noexcept
{
if(needle.empty())return 0;
const std::string_view text=bytes();
int matches=0;
for(std::size_t pos=find_aligned(text,needle,0);pos!=npos;pos=find_aligned(text,needle,pos+needle.size()))++matches;
return matches;
}

std::expected<String,StringError> String::substr(int start,int count) const
{
const int length=this->length();
if(start<0||start>length)return std::unexpected(StringError::index_out_of_range);
if(count<-1)return std::unexpected(StringError::negative_count);
const auto available=static_cast<std::uint32_t>(length-start);
const std::uint32_t n=count==-1||static_cast<std::uint32_t>(count)>available?available:static_cast<std::uint32_t>(count);
const std::uint32_t begin=char_to_byte(static_cast<std::uint32_t>(start));
std::uint32_t end;
if(n==available){
end=size_bytes();
}else if(static_cast<std::uint32_t>(length)==size_bytes()){
end=begin+n;
}else if(n<step){
const byte* base=ubytes(data());
end=static_cast<std::uint32_t>(skip_chars(base+begin,base+size_bytes(),n)-base);
}else{
end=char_to_byte(static_cast<std::uint32_t>(start)+n);
}
return bytes_between(begin,end,ascii_meta(n));
}

std::expected<String,StringError> String::left(int count) const
{
if(count<0)return std::unexpected(StringError::negative_count);
return substr(0,count);
}

std::expected<String,StringError> String::right(int count) const
{
if(count<0)return std::unexpected(StringError::negative_count);
const int length=this->length();
const int n=std::min(count,length);
return substr(length-n,n);
}

std::vector<String> String::split(std::string_view delimiter,bool keep_empty) const
{
std::vector<String> parts;
const std::string_view text=bytes();
if(delimiter.empty()){
parts.reserve(static_cast<std::size_t>(length()));
const byte* begin=ubytes(text.data());
const byte* end=begin+text.size();
for(const byte* p=begin;p<end;){
const std::uint32_t size=*p<0x80?1:char_size(p,end);
parts.push_back(String(reinterpret_cast<const char*>(p),size,*p<0x80?1|meta_ascii:1));
p+=size;
}
return parts;
}
const bool ascii=rep_.meta!=meta_unknown&&(rep_.meta&meta_ascii)!=0;
std::size_t from=0;
for(;;){
const std::size_t pos=find_aligned(text,delimiter,from);
const std::size_t end=pos==npos?text.size():pos;
if(keep_empty||end>from){
const auto size=static_cast<std::uint32_t>(end-from);
parts.push_back(String(text.data()+from,size,ascii?size|meta_ascii:meta_unknown));
}
if(pos==npos)break;
from=pos+delimiter.size();
}
return parts;
}

String String::join(std::span<const String> parts,std::string_view separator)
{
if(parts.empty())return String();
std::size_t total=separator.size()*(parts.size()-1);
for(const String& part:parts)total+=part.size_bytes();
if(total>max_size)too_long();
String out=with_size(total);
char* d=out.mutable_data();
bool first=true;
for(const String& part:parts){
if(!first&&!separator.empty()){
std::memcpy(d,separator.data(),separator.size());
d+=separator.size();
}
first=false;
std::memcpy(d,part.data(),part.size_bytes());
d+=part.size_bytes();
}
return out;
}

String String::replace(std::string_view old_text,std::string_view new_text,bool all) const
{
if(old_text.empty())return *this;
const std::string_view text=bytes();
std::size_t pos=find_aligned(text,old_text,0);
if(pos==npos)return *this;
const std::size_t growth=new_text.size()>old_text.size()?(new_text.size()-old_text.size())*(all?4:1):0;
StringWriter out(text.size()+growth);
std::size_t from=0;
do{
out.put(text.data()+from,pos-from);
out.put(new_text.data(),new_text.size());
from=pos+old_text.size();
if(!all)break;
pos=find_aligned(text,old_text,from);
}while(pos!=npos);
out.put(text.data()+from,text.size()-from);
return out.finish(meta_unknown);
}

String String::trim_left() const
{
const byte* begin=ubytes(data());
const byte* end=begin+size_bytes();
const byte* p=begin;
while(p<end){
if(*p<0x80){
if(!unicode::is_whitespace(*p))break;
++p;
continue;
}
const std::uint32_t size=char_size(p,end);
if(size==1||!unicode::is_whitespace(decode(p,size)))break;
p+=size;
}
return bytes_between(static_cast<std::uint32_t>(p-begin),size_bytes(),meta_unknown);
}

String String::trim_right() const
{
const byte* begin=ubytes(data());
const byte* e=begin+size_bytes();
while(e>begin){
if(e[-1]<0x80){
if(!unicode::is_whitespace(e[-1]))break;
--e;
continue;
}
const byte* start=previous_char(begin,e);
const auto size=static_cast<std::uint32_t>(e-start);
if(size==1||!unicode::is_whitespace(decode(start,size)))break;
e=start;
}
return bytes_between(0,static_cast<std::uint32_t>(e-begin),meta_unknown);
}

String String::trim() const
{
const byte* begin=ubytes(data());
const byte* end=begin+size_bytes();
const byte* p=begin;
while(p<end){
if(*p<0x80){
if(!unicode::is_whitespace(*p))break;
++p;
continue;
}
const std::uint32_t size=char_size(p,end);
if(size==1||!unicode::is_whitespace(decode(p,size)))break;
p+=size;
}
const byte* e=end;
while(e>p){
if(e[-1]<0x80){
if(!unicode::is_whitespace(e[-1]))break;
--e;
continue;
}
const byte* start=previous_char(p,e);
const auto size=static_cast<std::uint32_t>(e-start);
if(size==1||!unicode::is_whitespace(decode(start,size)))break;
e=start;
}
return bytes_between(static_cast<std::uint32_t>(p-begin),static_cast<std::uint32_t>(e-begin),meta_unknown);
}

String String::lower() const
{
return map_case<false,false>(bytes());
}

String String::upper() const
{
return map_case<true,false>(bytes());
}

String String::lower(std::string_view locale) const
{
return is_turkic_locale(locale)?map_case<false,true>(bytes()):map_case<false,false>(bytes());
}

String String::upper(std::string_view locale) const
{
return is_turkic_locale(locale)?map_case<true,true>(bytes()):map_case<true,false>(bytes());
}

std::expected<String,StringError> String::repeat(int count) const
{
if(count<0)return std::unexpected(StringError::negative_count);
const std::uint32_t size=size_bytes();
if(count==0||size==0)return String();
if(static_cast<std::uint64_t>(size)*static_cast<std::uint64_t>(count)>max_size)too_long();
const std::uint32_t total=size*static_cast<std::uint32_t>(count);
String out=with_size(total);
char* d=out.mutable_data();
std::memcpy(d,data(),size);
for(std::uint32_t copied=size;copied<total;){
const std::uint32_t chunk=std::min(copied,total-copied);
std::memcpy(d+copied,d,chunk);
copied+=chunk;
}
const std::uint32_t meta=rep_.meta;
if(meta!=meta_unknown&&!is_continuation(static_cast<byte>(d[0])))out.rep_.meta=((meta&meta_length)*static_cast<std::uint32_t>(count))|(meta&meta_ascii);
return out;
}

String String::reverse() const
{
const std::uint32_t size=size_bytes();
if(size<=1)return *this;
String out=with_size(size);
char* d=out.mutable_data();
const byte* begin=ubytes(data());
const byte* end=begin+size;
std::uint32_t count=0;
bool ascii=true;
bool valid=true;
for(const byte* p=begin;p<end;){
const auto at=static_cast<std::uint32_t>(p-begin);
if(end-p>=8){
const std::uint64_t word=load_word(p);
if((word&high_bits)==0){
store_word(d+size-at-8,std::byteswap(word));
p+=8;
count+=8;
continue;
}
}
std::uint32_t n=1;
if(*p>=0x80){
ascii=false;
n=char_size(p,end);
valid=valid&&n!=1;
}
std::memcpy(d+size-at-n,p,n);
p+=n;
++count;
}
// Reversing can join invalid bytes into a valid sequence; when every character was valid the count is unchanged.
if(valid)out.rep_.meta=count|(ascii?meta_ascii:0);
return out;
}

std::expected<String,StringError> String::insert(int index,std::string_view text) const
{
const int length=this->length();
if(index<0||index>length)return std::unexpected(StringError::index_out_of_range);
const std::uint32_t at=char_to_byte(static_cast<std::uint32_t>(index));
const std::uint32_t size=size_bytes();
if(text.size()>max_size-size)too_long();
String out=with_size(size+text.size());
char* d=out.mutable_data();
const char* s=data();
std::memcpy(d,s,at);
if(!text.empty())std::memcpy(d+at,text.data(),text.size());
std::memcpy(d+at+text.size(),s+at,size-at);
return out;
}

std::expected<String,StringError> String::erase(int start,int count) const
{
const int length=this->length();
if(start<0||start>length)return std::unexpected(StringError::index_out_of_range);
if(count<-1)return std::unexpected(StringError::negative_count);
const auto available=static_cast<std::uint32_t>(length-start);
const std::uint32_t n=count==-1||static_cast<std::uint32_t>(count)>available?available:static_cast<std::uint32_t>(count);
if(n==0)return *this;
const std::uint32_t size=size_bytes();
const std::uint32_t begin=char_to_byte(static_cast<std::uint32_t>(start));
const std::uint32_t end=n==available?size:char_to_byte(static_cast<std::uint32_t>(start)+n);
String out=with_size(size-(end-begin));
char* d=out.mutable_data();
std::memcpy(d,data(),begin);
std::memcpy(d+begin,data()+end,size-end);
if(!out.empty())out.rep_.meta=ascii_meta(static_cast<std::uint32_t>(length)-n);
return out;
}

String String::normalize() const
{
const std::string_view text=bytes();
// Every code point below U+0300 has NFC_Quick_Check=Yes and combining class 0, and those are exactly the characters whose
// bytes are all below 0xCC, so such text (all ASCII and Turkish among it) is already NFC.
byte top=0;
for(const char c:text)top=std::max(top,static_cast<byte>(c));
if(top<0xCC)return *this;
const byte* p=ubytes(text.data());
const byte* end=p+text.size();
StringWriter out(text.size()+16);
std::u32string run;
const byte* run_begin=p;
const auto flush=[&](const byte* run_end){
if(run_begin==run_end)return;
if(unicode::quick_check_nfc(run)){
out.put(reinterpret_cast<const char*>(run_begin),static_cast<std::size_t>(run_end-run_begin));
}else{
for(const char32_t c:unicode::nfc(run))out.advance(encode(c,out.reserve(4)));
}
run.clear();
};
while(p<end){
const std::uint32_t size=*p<0x80?1:char_size(p,end);
if(size==1&&*p>=0x80){
flush(p);
out.put_byte(static_cast<char>(*p));
++p;
run_begin=p;
continue;
}
run.push_back(decode(p,size));
p+=size;
}
flush(end);
return out.finish(meta_unknown);
}

std::expected<double,StringError> String::to_number() const
{
double value=0;
if(!parse_number(bytes(),value))return std::unexpected(StringError::not_a_number);
return value;
}

std::expected<std::int64_t,StringError> String::to_int() const
{
const std::string_view text=bytes();
std::size_t i=0;
const bool negative=!text.empty()&&text[0]=='-';
if(!text.empty()&&(text[0]=='-'||text[0]=='+'))i=1;
if(i<text.size()){
std::uint64_t magnitude=0;
bool overflow=false;
std::size_t k=i;
for(;k<text.size()&&is_digit(text[k]);++k){
const auto digit=static_cast<std::uint64_t>(text[k]-'0');
if(magnitude>(std::numeric_limits<std::uint64_t>::max()-digit)/10)overflow=true;
else magnitude=magnitude*10+digit;
}
if(k==text.size()){
constexpr std::uint64_t max_positive=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
if(negative){
if(overflow||magnitude>max_positive+1)return std::numeric_limits<std::int64_t>::min();
return magnitude==max_positive+1?std::numeric_limits<std::int64_t>::min():-static_cast<std::int64_t>(magnitude);
}
if(overflow||magnitude>max_positive)return std::numeric_limits<std::int64_t>::max();
return static_cast<std::int64_t>(magnitude);
}
}
double value=0;
if(!parse_number(text,value)||std::isnan(value))return std::unexpected(StringError::not_a_number);
constexpr double limit=9223372036854775808.0;
if(value>=limit)return std::numeric_limits<std::int64_t>::max();
if(value<=-limit)return std::numeric_limits<std::int64_t>::min();
return static_cast<std::int64_t>(value);
}

bool String::is_number() const noexcept
{
double value=0;
return parse_number(bytes(),value);
}

bool String::is_digits() const noexcept
{
const std::string_view text=bytes();
if(text.empty())return false;
// No early exit, so the loop vectorizes.
unsigned other=0;
for(const char c:text)other|=static_cast<unsigned char>(c-'0')>=10?1u:0u;
return other==0;
}
}
