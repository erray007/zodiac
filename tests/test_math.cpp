// Tests the script maths library (register_math, spec 8.5 of the core-types design and 8.1 of the library-and-packs design):
// half-away-from-zero rounding, decimal rounding of the shortest round-trip form, the fast path agreeing bit for bit with the
// exact decimal path over 1 000 000 values, the exact path agreeing with an independent decimal-string rounding, and the
// script functions, constants and their two exceptions.
#include "script_fixture.hpp"
#include <core/library/round.hpp>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <random>
#include <string>
#include <string_view>

using zodiac_test::ScriptFixture;

namespace {
// True when a and b are the same double, sign of zero and NaN payload included.
bool same(double a,double b)
{
return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
}
// The shortest round-trip form of x, for failure messages.
std::string shortest(double x)
{
char text[32];
return std::string(text,std::to_chars(text,text+sizeof text,x).ptr);
}
// One property-test case: the i-th value and number of digits drawn from random, cycling through five kinds of input.
void sample(std::mt19937_64& random,int i,double& x,int& digits)
{
const auto below=[&random](std::uint64_t bound){return random()%bound;};
switch(i%5){
case 0:
// Any bit pattern: subnormals, NaN, infinities and every exponent.
x=std::bit_cast<double>(random());
digits=static_cast<int>(below(61))-30;
break;
case 1:{
// Short decimals as scripts write them, at and around the rounding position.
const int places=static_cast<int>(below(10));
x=static_cast<double>(static_cast<std::int64_t>(below(2000000001))-1000000000)/std::pow(10.0,places);
digits=places-static_cast<int>(below(4));
break;
}
case 2:{
// Decimal halves at the rounding position, with up to nine more decimals around them, over magnitudes up to 10^15:
// the values whose product with 10^digits lands near 0.5 and past 2^32.
const int places=static_cast<int>(below(8));
const int integer_digits=static_cast<int>(below(static_cast<std::uint64_t>(15-places)))+1;
const double whole=static_cast<double>(below(static_cast<std::uint64_t>(std::pow(10.0,integer_digits))));
const double offset=(static_cast<double>(below(19999))-9999)/std::pow(10.0,static_cast<int>(below(5))+5);
x=(whole+0.5+offset)/std::pow(10.0,places);
if(random()&1)x=-x;
digits=places;
break;
}
case 3:{
// The doubles next to an exact half, where binary representation error decides the side.
const int places=static_cast<int>(below(6));
const int steps=static_cast<int>(below(5))-2;
x=(static_cast<double>(below(1000000000))+0.5)/std::pow(10.0,places);
for(int s=0;s<steps;++s)x=std::nextafter(x,std::numeric_limits<double>::infinity());
for(int s=0;s>steps;--s)x=std::nextafter(x,-std::numeric_limits<double>::infinity());
digits=places;
break;
}
default:
// Products with 10^digits between 2^32 and 2^53, where the fast path's bounds matter most.
digits=static_cast<int>(below(17));
x=std::ldexp(static_cast<double>(random()>>11),static_cast<int>(below(22))-21)/std::pow(10.0,digits);
break;
}
}
// An independent reference for round_digits_exact: the shortest round-trip form (std::to_chars(double)) written out as a
// plain digit string with the decimal point moved by its exponent, rounded half away from zero as a string (with
// carries), read back by strtod.
double reference_round(double x,int digits)
{
if(digits>=17||!std::isfinite(x))return x;
char text[64];
const std::string form(text,std::to_chars(text,text+sizeof text,x).ptr);
const bool negative=form[0]=='-';
const std::size_t e=form.find('e');
const std::string mantissa=form.substr(negative?1:0,e==std::string::npos?std::string::npos:e-(negative?1:0));
std::string all;
long point=-1;
for(const char c:mantissa){
if(c=='.')point=static_cast<long>(all.size());
else all.push_back(c);
}
if(point<0)point=static_cast<long>(all.size());
if(e!=std::string::npos)point+=std::stol(form.substr(e+1));
// Pad with zeros so the point lies within the digits.
if(point<0){
all.insert(0,static_cast<std::size_t>(-point),'0');
point=0;
}
if(point>static_cast<long>(all.size()))all.append(static_cast<std::size_t>(point)-all.size(),'0');
const long kept=point+digits;
if(kept>=static_cast<long>(all.size()))return x;
if(kept<0)return negative?-0.0:0.0;
std::string rounded=all.substr(0,static_cast<std::size_t>(kept));
if(all[static_cast<std::size_t>(kept)]>='5'){
long i=kept-1;
for(;i>=0&&rounded[static_cast<std::size_t>(i)]=='9';--i)rounded[static_cast<std::size_t>(i)]='0';
if(i<0)rounded.insert(rounded.begin(),'1');
else ++rounded[static_cast<std::size_t>(i)];
}
if(rounded.empty())rounded="0";
const std::string decimal=(negative?"-":"")+rounded+"e"+std::to_string(-digits);
return std::strtod(decimal.c_str(),nullptr);
}
// Runs "bool main(){return <expression>;}" and returns its result.
bool holds(ScriptFixture& fixture,std::string_view expression)
{
const std::string source="bool main(){return "+std::string(expression)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"bool main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnByte()!=0;
}
// Runs "void main(){<statement>;}", which must raise a script exception, and returns its text.
std::string error_of(ScriptFixture& fixture,std::string_view statement)
{
const std::string source="void main(){"+std::string(statement)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"void main()")==asEXECUTION_EXCEPTION);
return fixture.exception_string();
}
}

TEST_CASE("round_half_away rounds halves away from zero")
{
using zodiac::round_half_away;
CHECK(round_half_away(0.5)==1);
CHECK(round_half_away(-2.5)==-3);
CHECK(round_half_away(2.4999999999999996)==2);
CHECK(round_half_away(1.5)==2);
CHECK(round_half_away(-0.4)==0);
CHECK(std::signbit(round_half_away(-0.4)));
CHECK(same(round_half_away(-0.0),-0.0));
CHECK(round_half_away(4503599627370495.5)==4503599627370496.0);
CHECK(round_half_away(9007199254740993.0)==9007199254740993.0);
CHECK(round_half_away(1e300)==1e300);
CHECK(std::isnan(round_half_away(std::numeric_limits<double>::quiet_NaN())));
CHECK(round_half_away(-std::numeric_limits<double>::infinity())==-std::numeric_limits<double>::infinity());
}

TEST_CASE("round_digits rounds the shortest decimal form")
{
using zodiac::round_digits;
CHECK(round_digits(2.675,2)==2.68);
CHECK(round_digits(1.005,2)==1.01);
CHECK(round_digits(-0.125,2)==-0.13);
CHECK(round_digits(1234.5,-2)==1200);
CHECK(round_digits(0.1+0.2,15)==0.3);
CHECK(round_digits(1e300,2)==1e300);
CHECK(round_digits(1250,-2)==1300);
CHECK(round_digits(-1250,-2)==-1300);
CHECK(round_digits(49999,-5)==0);
CHECK(round_digits(50000,-5)==100000);
CHECK(round_digits(9.995,2)==10);
CHECK(round_digits(2.5,0)==3);
CHECK(same(round_digits(-0.001,2),-0.0));
CHECK(same(round_digits(-0.0,3),-0.0));
// Past 2^32 the multiply's error exceeds 2^-20, so the fast path must hand these to the exact path.
CHECK(round_digits(87364748190.165,2)==87364748190.17);
CHECK(round_digits(4459252572.26545,4)==4459252572.2655);
CHECK(round_digits(576940805.205455,5)==576940805.20546);
// Integers past 2^53 have their exact digits as shortest form (to_chars writes them in fixed notation), not a shorter
// scientific significand ending in 5 (6.839940123176985e16) that would round up.
CHECK(round_digits(68399401231769848.0,-2)==68399401231769800.0);
CHECK(round_digits(123456789012345683968.0,-1)==123456789012345683970.0);
CHECK(round_digits(123456789012345683968.0,-20)==100000000000000000000.0);
// 22 digits kept past the 19 an integer holds, with a carry through the 9 ahead of the dropped 5.
CHECK(round_digits(1000000000000005373952.0,-2)==1000000000000005374000.0);
// Rounding up past the largest double overflows to infinity.
CHECK(round_digits(1.7976931348623157e308,-307)==std::numeric_limits<double>::infinity());
CHECK(round_digits(-1.7976931348623157e308,-300)==-1.79769313e308);
const double nan=std::numeric_limits<double>::quiet_NaN();
const double inf=std::numeric_limits<double>::infinity();
CHECK(same(round_digits(nan,2),nan));
CHECK(round_digits(inf,2)==inf);
CHECK(round_digits(-inf,-3)==-inf);
CHECK(same(round_digits(0.12345678901234567,17),0.12345678901234567));
CHECK(same(round_digits(1.2345678901234567e-10,40),1.2345678901234567e-10));
CHECK(round_digits(123.456,-2147483647-1)==0);
CHECK(round_digits(123.456,2147483647)==123.456);
CHECK(zodiac::round_digits_exact(2.675,2)==2.68);
CHECK(zodiac::round_digits_exact(1.005,2)==1.01);
CHECK(zodiac::round_digits_exact(0.1+0.2,15)==0.3);
}

TEST_CASE("round_digits agrees with the exact decimal path on 1 000 000 values")
{
std::mt19937_64 random(0x5A0D1AC2026ull);
long mismatches=0;
for(int i=0;i<1000000;++i){
double x;
int digits;
sample(random,i,x,digits);
const double fast=zodiac::round_digits(x,digits);
const double exact=zodiac::round_digits_exact(x,digits);
if(!same(fast,exact)&&++mismatches<=10){
INFO("x=",shortest(x)," digits=",digits," round_digits=",shortest(fast)," round_digits_exact=",shortest(exact));
CHECK(same(fast,exact));
}
}
INFO("mismatches: ",mismatches);
CHECK(mismatches==0);
}

TEST_CASE("round_digits_exact agrees with decimal string rounding on 200 000 values")
{
std::mt19937_64 random(0x0DEC1A1ull);
long mismatches=0;
for(int i=0;i<200000;++i){
double x;
int digits;
sample(random,i,x,digits);
const double exact=zodiac::round_digits_exact(x,digits);
const double expected=reference_round(x,digits);
if(!same(exact,expected)&&++mismatches<=10){
INFO("x=",shortest(x)," digits=",digits," round_digits_exact=",shortest(exact)," reference=",shortest(expected));
CHECK(same(exact,expected));
}
}
INFO("mismatches: ",mismatches);
CHECK(mismatches==0);
}

TEST_CASE("script maths functions and constants")
{
ScriptFixture fixture;
CHECK(holds(fixture,"abs(-3)==3"));
CHECK(holds(fixture,"clamp(5,1,3)==3"));
CHECK(holds(fixture,"min(2.5,1.0)==1.0"));
CHECK(holds(fixture,"floor(-1.5)==-2"));
CHECK(holds(fixture,"trunc(-1.5)==-1"));
CHECK(holds(fixture,"abs(PI-3.141592653589793)<1e-15"));
CHECK(holds(fixture,"abs(TAU-2*PI)<1e-15"));
CHECK(holds(fixture,"E==exp(1.0)"));
CHECK(holds(fixture,"hypot(3,4)==5"));
CHECK(holds(fixture,"fmod(7,3)==1"));
CHECK(holds(fixture,"max(2,7)==7&&min(2,7)==2"));
CHECK(holds(fixture,"abs(int64(-5000000000))==5000000000&&max(int64(1),int64(2))==2&&clamp(int64(9),int64(0),int64(4))==4"));
CHECK(holds(fixture,"abs(-1.5f)==1.5f&&min(1.5f,2.5f)==1.5f&&max(1.5f,2.5f)==2.5f&&clamp(3.0f,0.0f,1.0f)==1.0f"));
CHECK(holds(fixture,"abs(-2.25)==2.25&&max(2.5,1.0)==2.5&&clamp(0.5,0.0,1.0)==0.5&&clamp(-1.0,0.0,1.0)==0"));
CHECK(holds(fixture,"ceil(-1.5)==-1&&ceil(1.2)==2&&floor(1.7)==1&&trunc(1.7)==1&&ceil(-0.5)==0&&floor(-0.5)==-1"));
CHECK(holds(fixture,"round(2.5)==3&&round(-2.5)==-3&&round(2.4999999999999996)==2"));
CHECK(holds(fixture,"round(2.675,2)==2.68&&round(1234.5,-2)==1200"));
CHECK(holds(fixture,"sqrt(16.0)==4&&pow(2.0,10.0)==1024&&log(E)==1&&log10(1000.0)==3"));
CHECK(holds(fixture,"sin(0.0)==0&&cos(0.0)==1&&tan(0.0)==0&&asin(1.0)==PI/2&&acos(1.0)==0&&atan(0.0)==0&&atan2(1.0,1.0)==PI/4"));
CHECK(error_of(fixture,"abs(int(-2147483648))")=="integer overflow");
CHECK(error_of(fixture,"abs(int64(-9223372036854775807)-1)")=="integer overflow");
CHECK(error_of(fixture,"clamp(1.0,3.0,2.0)")=="clamp: low is greater than high");
CHECK(error_of(fixture,"clamp(1,3,2)")=="clamp: low is greater than high");
CHECK(error_of(fixture,"clamp(int64(1),int64(3),int64(2))")=="clamp: low is greater than high");
CHECK(error_of(fixture,"clamp(1.0f,3.0f,2.0f)")=="clamp: low is greater than high");
}
