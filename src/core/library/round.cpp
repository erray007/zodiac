// Half-away-from-zero and decimal rounding (contract in round.hpp).
// Why the fast path of round_digits is exact: let S be the shortest round-trip decimal of x, y=fl(x*p) and p=10^digits
// (exact for 0<=digits<=22). The exact answer rounds S*p; |y-S*p| <= |y-x*p|+p*|x-S| <= 2^-53*|x*p|+p*2^-53*|x|, so
// at most 2^-52*|y| (to first order). When the fraction of |y| is farther than twice that (and than 2^-20) from 0.5, y and
// S*p round to the same integer r, and fl(r/p) is the double nearest r/10^digits, which is what reading the rounded decimal
// back gives. For digits<0 the same holds for y=fl(x/q), q=10^-digits, and fl(r*q). |y| < 2^52 keeps r and the integer part
// exact. Past 2^32 the 2^-20 bound alone is not enough (round(87364748190.165, 2) would give ...0.16), hence the relative one.
#include <core/library/round.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <locale.h>
#include <stdlib.h>
#include <system_error>

namespace zodiac {
namespace {
// The powers of ten a double holds exactly.
constexpr double kPow10[23]={1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,1e20,1e21,1e22};
constexpr double kTwo52=0x1p52;
constexpr std::uint64_t kTwo53=std::uint64_t(1)<<53;

// The double nearest the decimal [first, last) ("<digits>e<exponent>", NUL-terminated at last, at least 10^-16), infinity
// past the largest double. from_chars where the library has it for double (libc++ 20 and later), as string.cpp reads
// numbers; strtod in the "C" locale otherwise, which also overflows to infinity.
double read_decimal(const char* first,const char* last) noexcept
{
#if defined(_LIBCPP_VERSION)&&_LIBCPP_VERSION>=200000
double value=0;
const std::from_chars_result read=std::from_chars(first,last,value);
if(read.ec==std::errc::result_out_of_range)return HUGE_VAL;
return value;
#else
(void)last;
#if defined(_WIN32)
static const _locale_t c_locale=_create_locale(LC_ALL,"C");
return _strtod_l(first,nullptr,c_locale);
#else
static const locale_t c_locale=newlocale(LC_ALL_MASK,"C",static_cast<locale_t>(0));
return strtod_l(first,nullptr,c_locale);
#endif
#endif
}

// The integer nearest a, ties to even, for 0 <= a < 2^52: a+2^52 lies in [2^52, 2^53), where the doubles are the integers,
// so the addition rounds a in the current (default, to-nearest-even) mode and the subtraction is exact. Two additions, a
// shorter dependency chain than a round trip through int64. Needs IEEE arithmetic as compiled (no -ffast-math).
inline double nearest_even(double a) noexcept
{
return (a+kTwo52)-kTwo52;
}
// Sets rounded to y (x*p or x/q) rounded half away from zero, with x's sign, and returns true when that provably agrees
// with the decimal path; returns false otherwise (|y| >= 2^52, NaN or infinite, or too close to a half). Away from a half,
// ties to even and half away from zero give the same integer.
inline bool fast_round(double y,double x,double& rounded) noexcept
{
const double a=std::fabs(y);
if(!(a<kTwo52))return false;
const double r=nearest_even(a);
// 0.5-|a-r| is how far a's fraction lies from 0.5.
if(!(0.5-std::fabs(a-r)>std::max(0x1p-20,a*0x1p-51)))return false;
rounded=std::copysign(r,x);
return true;
}
}

double round_half_away(double x) noexcept
{
const double a=std::fabs(x);
if(!(a<kTwo52))return x;
const double r=nearest_even(a);
// a-r is 0.5 exactly when a tie went down to even; half away from zero takes the integer above.
return std::copysign(r+(a-r==0.5?1.0:0.0),x);
}

double round_digits(double x,int digits) noexcept
{
double rounded;
if(static_cast<unsigned>(digits)<=16u){
const double p=kPow10[digits];
if(fast_round(x*p,x,rounded))return rounded/p;
}
else if(digits<0){
if(digits>=-22){
const double q=kPow10[-digits];
if(fast_round(x/q,x,rounded))return rounded*q;
}
}
else return x;
return round_digits_exact(x,digits);
}

// Kept out of round_digits: inlined, its registers and stack frame were saved and restored on every fast-path call too
// (8 % slower than multiply-round-divide in bench_types); out of line, round_digits needs no frame and jumps here.
[[gnu::noinline]] double round_digits_exact(double x,int digits) noexcept
{
if(digits>=17||!std::isfinite(x))return x;
// Every finite double's decimal exponent is at least -324, so rounding at more than 400 places to the left gives 0 as -400 does.
if(digits<-400)digits=-400;
// The shortest round-trip form, std::to_chars(double): fixed notation when it is not longer than scientific (and then
// the exact digits of an integer past 2^53, such as 68399401231769848), scientific otherwise; at most 24 characters.
char text[32];
const char* const end=std::to_chars(text,text+sizeof text,x).ptr;
const char* at=text;
const bool negative=*at=='-';
at+=negative;
// As S = 0.significand * 10^power: the significant digits without leading or trailing zeros.
char significand[32];
int count=0;
int power=0;
bool fraction=false;
for(;at<end&&*at!='e';++at){
if(*at=='.')fraction=true;
else if(count>0||*at!='0'){
significand[count++]=*at;
power+=!fraction;
}
else power-=fraction;
}
if(at<end){
++at;
const bool negative_exponent=*at=='-';
int exponent=0;
for(++at;at<end;++at)exponent=exponent*10+(*at-'0');
power+=negative_exponent?-exponent:exponent;
}
while(count>0&&significand[count-1]=='0')--count;
// Keep the digits down to 10^-digits.
const int kept=power+digits;
if(kept>=count)return x;
if(kept<0)return std::copysign(0.0,x);
// Half away from zero on an exact decimal: the first dropped digit decides, a 5 with nothing after it included.
const bool up=significand[kept]>='5';
double result;
char decimal[48];
char* out;
if(kept<=19){
std::uint64_t integer=0;
for(int i=0;i<kept;++i)integer=integer*10+static_cast<std::uint64_t>(significand[i]-'0');
integer+=up;
if(integer==0)return std::copysign(0.0,x);
// The result is integer*10^-digits. One correctly rounded operation on exact operands gives the double nearest it,
// as reading the decimal back does; other cases read it back.
if(integer<=kTwo53&&digits>=-22){
result=digits>=0?static_cast<double>(integer)/kPow10[digits]:static_cast<double>(integer)*kPow10[-digits];
return negative?-result:result;
}
out=std::to_chars(decimal,decimal+sizeof decimal,integer).ptr;
}
else{
// More than 19 digits kept (only the exact digits of an integer past 10^20 have that many): carry in the characters,
// into a leading 0.
decimal[0]='0';
std::memcpy(decimal+1,significand,static_cast<std::size_t>(kept));
if(up){
int i=kept;
for(;decimal[i]=='9';--i)decimal[i]='0';
++decimal[i];
}
out=decimal+kept+1;
}
*out++='e';
out=std::to_chars(out,decimal+sizeof decimal-1,-digits).ptr;
*out='\0';
result=read_decimal(decimal,out);
return negative?-result:result;
}
}
