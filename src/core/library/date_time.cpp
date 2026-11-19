// DateTime::format (contract in date_time.hpp). The parts are computed once, the text is written into a buffer on the stack
// (heap memory only for formats longer than kStackFormat bytes) and becomes the String in one construction, which keeps
// results of up to 23 bytes, the default format's 19 among them, free of allocation.
// Bound on the output: a literal byte is one byte, an escape two bytes in and one out, and a token k bytes in and at most k
// out, except {yyyy} (6 in, at most 10 out: '-' and 9 digits, since |year| < 300 000 000); so it never exceeds twice fmt.
#include <core/library/date_time.hpp>
#include <cstddef>
#include <cstring>
#include <memory>

namespace zodiac {
namespace {
constexpr std::size_t kStackFormat=128;

char* write_two(char* out,int value) noexcept
{
out[0]=static_cast<char>('0'+value/10);
out[1]=static_cast<char>('0'+value%10);
return out+2;
}
// value without padding (0 to 99, the unpadded tokens' range).
char* write_short(char* out,int value) noexcept
{
if(value>=10)return write_two(out,value);
*out=static_cast<char>('0'+value);
return out+1;
}
// The year with at least 4 digits and a '-' when negative.
char* write_year(char* out,int year) noexcept
{
if(year<0)*out++='-';
unsigned magnitude=year<0?0u-static_cast<unsigned>(year):static_cast<unsigned>(year);
char digits[10];
int count=0;
do{
digits[count++]=static_cast<char>('0'+magnitude%10);
magnitude/=10;
}while(magnitude!=0);
for(int pad=count;pad<4;++pad)*out++='0';
while(count>0)*out++=digits[--count];
return out;
}

struct Parts {
date_time_detail::CivilDate date;
int hour;
int minute;
int second;
int millisecond;
};

// Writes the token between the braces at token (length bytes, the braces excluded) and returns the end, or nullptr when it
// is not a token.
char* write_token(char* out,const char* token,std::size_t length,const Parts& parts) noexcept
{
switch(length){
case 1:
switch(*token){
case 'M':return write_short(out,parts.date.month);
case 'd':return write_short(out,parts.date.day);
case 'H':return write_short(out,parts.hour);
default:return nullptr;
}
case 2:
if(token[0]!=token[1])return nullptr;
switch(*token){
case 'y':return write_two(out,static_cast<int>(date_time_detail::floor_mod(parts.date.year,100)));
case 'M':return write_two(out,parts.date.month);
case 'd':return write_two(out,parts.date.day);
case 'H':return write_two(out,parts.hour);
case 'm':return write_two(out,parts.minute);
case 's':return write_two(out,parts.second);
default:return nullptr;
}
case 3:
if(std::memcmp(token,"fff",3)!=0)return nullptr;
out[0]=static_cast<char>('0'+parts.millisecond/100);
return write_two(out+1,parts.millisecond%100);
case 4:
if(std::memcmp(token,"yyyy",4)!=0)return nullptr;
return write_year(out,parts.date.year);
default:
return nullptr;
}
}

// Writes the formatted text at out (room for 2 * fmt.size() bytes) and returns its end.
char* write_format(char* out,std::string_view fmt,const Parts& parts) noexcept
{
const char* in=fmt.data();
const char* const end=in+fmt.size();
while(in<end){
const char c=*in;
if(c=='{'){
if(in+1<end&&in[1]=='{'){
*out++='{';
in+=2;
continue;
}
// The longest token is 4 bytes, so a closing brace further away never closes one.
const std::size_t room=static_cast<std::size_t>(end-in-1);
const void* close=std::memchr(in+1,'}',room<5?room:5);
if(close){
const char* closing=static_cast<const char*>(close);
if(char* written=write_token(out,in+1,static_cast<std::size_t>(closing-in-1),parts)){
out=written;
in=closing+1;
continue;
}
}
}else if(c=='}'&&in+1<end&&in[1]=='}'){
*out++='}';
in+=2;
continue;
}
*out++=c;
++in;
}
return out;
}
}

String DateTime::format(std::string_view fmt) const
{
const int time=time_of_day_ms();
const Parts parts{date(),time/3600000,time/60000%60,time/1000%60,time%1000};
if(fmt.size()<=kStackFormat){
char buffer[2*kStackFormat];
return String(std::string_view(buffer,static_cast<std::size_t>(write_format(buffer,fmt,parts)-buffer)));
}
const std::unique_ptr<char[]> buffer(new char[2*fmt.size()]);
return String(std::string_view(buffer.get(),static_cast<std::size_t>(write_format(buffer.get(),fmt,parts)-buffer.get())));
}
}
