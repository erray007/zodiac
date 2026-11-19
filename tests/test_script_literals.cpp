// Tests that decimal literals in scripts compile to the nearest double or float (round half to even, as std::from_chars
// rounds), which AngelScript's own scanner did not do (cmake/patches/angelscript-exact-literals.patch): literals at the
// edges of the double range, a float literal written with or without the f suffix, the decimals whose nearest double lies
// exactly halfway between two floats, the other kinds of number literal, and 100 000 random doubles and floats each
// written in its shortest round-trip form and read back bit for bit.
#include "script_fixture.hpp"
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using zodiac_test::ScriptFixture;

namespace {
// True when a and b are the same double (or float), sign of zero included.
bool same(double a,double b)
{
return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
}
bool same(float a,float b)
{
return std::bit_cast<std::uint32_t>(a)==std::bit_cast<std::uint32_t>(b);
}
// Runs "bool main(){return <expression>;}" and returns its result.
bool holds(ScriptFixture& fixture,std::string_view expression)
{
const std::string source="bool main(){return "+std::string(expression)+";}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"bool main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnByte()!=0;
}
// The double that "double main(){<body>}" returns.
double double_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source="double main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"double main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnDouble();
}
// The float that "float main(){<body>}" returns; same_float is a script function that takes and returns a float.
float float_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source="float same_float(float x){return x;} float main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"float main()")==asEXECUTION_FINISHED);
return fixture.context->GetReturnFloat();
}
// The shortest round-trip form of x as a script double literal: std::to_chars writes integers such as 1e+20 as
// "100000000000000000000", which a script reads as an integer literal, so those get ".0".
template<class Real>
std::string literal_of(Real x)
{
char text[64];
std::string written(text,std::to_chars(text,text+sizeof text,x).ptr);
if(written.find_first_of(".e")==std::string::npos)written+=".0";
return written;
}
// The decimal digits of 5^n, so that "<digits>e-<n>" is 2^-n written out exactly.
std::string power_of_five(int n)
{
// Little-endian limbs of nine decimal digits each.
std::vector<std::uint32_t> limbs{1};
for(int i=0;i<n;++i){
std::uint32_t carry=0;
for(std::uint32_t& limb:limbs){
const std::uint64_t product=std::uint64_t(limb)*5+carry;
limb=static_cast<std::uint32_t>(product%1000000000u);
carry=static_cast<std::uint32_t>(product/1000000000u);
}
if(carry)limbs.push_back(carry);
}
std::string digits=std::to_string(limbs.back());
for(std::size_t i=limbs.size()-1;i-->0;){
const std::string limb=std::to_string(limbs[i]);
digits.append(9-limb.size(),'0').append(limb);
}
return digits;
}
// Literal values collected from scripts, in call order.
std::vector<double> collected_doubles;
std::vector<float> collected_floats;
void collect_double(double x)
{
collected_doubles.push_back(x);
}
void collect_float(float x)
{
collected_floats.push_back(x);
}
}

TEST_CASE("decimal literals compile to the nearest double")
{
ScriptFixture fixture;
CHECK(holds(fixture,"0.04==4.0/100.0"));
CHECK(holds(fixture,"0.1+0.2==0.30000000000000004"));
CHECK(holds(fixture,"0.1+0.2!=0.3"));
CHECK(same(double_of(fixture,"return 1.7976931348623157e308;"),std::numeric_limits<double>::max()));
CHECK(same(double_of(fixture,"return 5e-324;"),std::numeric_limits<double>::denorm_min()));
CHECK(same(double_of(fixture,"return 2.2250738585072014e-308;"),std::numeric_limits<double>::min()));
CHECK(same(double_of(fixture,"return 0.04;"),0.04));
CHECK(same(double_of(fixture,"return 0.1;"),0.1));
CHECK(same(double_of(fixture,"return 123456.789e-3;"),123.456789));
// The exact binary value of 0.1, and the decimals halfway between two doubles (ties to even) and just past one.
CHECK(same(double_of(fixture,"return 0.1000000000000000055511151231257827021181583404541015625;"),0.1));
CHECK(same(double_of(fixture,"return 9007199254740993.0;"),9007199254740992.0));
CHECK(same(double_of(fixture,"return 9007199254740995.0;"),9007199254740996.0));
CHECK(same(double_of(fixture,"return 9007199254740993.00000000000000000000000000001;"),9007199254740994.0));
// The largest double's upper half-ulp: below it the largest double, at it infinity (ties to even).
CHECK(same(double_of(fixture,"return 1.797693134862315807937289714053e308;"),std::numeric_limits<double>::max()));
CHECK(same(double_of(fixture,"return 1.797693134862315807937289714054e308;"),std::numeric_limits<double>::infinity()));
CHECK(same(double_of(fixture,"return 1e400;"),std::numeric_limits<double>::infinity()));
// Half the smallest subnormal (2^-1075) rounds to zero (ties to even), anything past it to the smallest subnormal.
const std::string half_subnormal=power_of_five(1075);
CHECK(same(double_of(fixture,"return "+half_subnormal+"e-1075;"),0.0));
CHECK(same(double_of(fixture,"return "+half_subnormal+"1e-1076;"),std::numeric_limits<double>::denorm_min()));
CHECK(same(double_of(fixture,"return 1e-400;"),0.0));
CHECK(same(double_of(fixture,"return -0.0;"),-0.0));
CHECK(same(double_of(fixture,"return -2.5;"),-2.5));
}

TEST_CASE("float literals compile to the nearest float")
{
ScriptFixture fixture;
CHECK(same(float_of(fixture,"return 0.1f;"),0.1f));
CHECK(same(float_of(fixture,"float x=0.1; return x;"),0.1f));
CHECK(same(float_of(fixture,"return 3.4028235e38f;"),std::numeric_limits<float>::max()));
CHECK(same(float_of(fixture,"return 1e-45f;"),std::numeric_limits<float>::denorm_min()));
CHECK(same(float_of(fixture,"return 1.17549435e-38F;"),std::numeric_limits<float>::min()));
CHECK(holds(fixture,"0.1f!=0.1"));
CHECK(holds(fixture,"double(0.1f)==0.100000001490116119384765625"));
// Decimals whose nearest double lies exactly halfway between two floats, so that rounding to a double first and then to a
// float rounds twice and lands on the wrong float: just past 1+2^-24 (between 1 and 1+2^-23), just past half the smallest
// subnormal float and just below the float overflow threshold 2^128-2^103. Written with the f suffix, as a double literal
// converted to a float (initialization, assignment, explicit conversion, a float argument, negated) and exactly at the
// halfway point (ties to even, the same either way).
const float above_one=std::bit_cast<float>(std::uint32_t(0x3F800001));
CHECK(same(float_of(fixture,"return 1.000000059604644775390625001f;"),above_one));
CHECK(same(float_of(fixture,"float x=1.000000059604644775390625001; return x;"),above_one));
CHECK(same(float_of(fixture,"float x; x=1.000000059604644775390625001; return x;"),above_one));
CHECK(same(float_of(fixture,"return float(1.000000059604644775390625001);"),above_one));
CHECK(same(float_of(fixture,"return same_float(1.000000059604644775390625001);"),above_one));
CHECK(same(float_of(fixture,"return 1.000000059604644775390625001;"),above_one));
CHECK(same(float_of(fixture,"float x=-1.000000059604644775390625001; return x;"),-above_one));
CHECK(same(float_of(fixture,"return 1.000000059604644775390625f;"),1.0f));
CHECK(same(float_of(fixture,"float x=1.000000059604644775390625; return x;"),1.0f));
CHECK(same(float_of(fixture,"return 1.0000000596046447753906249999f;"),1.0f));
CHECK(same(float_of(fixture,"float x=1.0000000596046447753906249999; return x;"),1.0f));
const std::string half_float_subnormal=power_of_five(150);
CHECK(same(float_of(fixture,"return "+half_float_subnormal+"1e-151f;"),std::numeric_limits<float>::denorm_min()));
CHECK(same(float_of(fixture,"float x="+half_float_subnormal+"1e-151; return x;"),std::numeric_limits<float>::denorm_min()));
CHECK(same(float_of(fixture,"return "+half_float_subnormal+"e-150f;"),0.0f));
CHECK(same(float_of(fixture,"float x="+half_float_subnormal+"e-150; return x;"),0.0f));
CHECK(same(float_of(fixture,"return 340282356779733661637539395458142568447.0f;"),std::numeric_limits<float>::max()));
CHECK(same(float_of(fixture,"float x=340282356779733661637539395458142568447.0; return x;"),std::numeric_limits<float>::max()));
CHECK(same(float_of(fixture,"return 340282356779733661637539395458142568448.0f;"),std::numeric_limits<float>::infinity()));
CHECK(same(float_of(fixture,"return 1e39f;"),std::numeric_limits<float>::infinity()));
CHECK(same(float_of(fixture,"return 1e-50f;"),0.0f));
// A double expression keeps double semantics: the sum is rounded to a double, then to a float. When the left operand is
// a literal that carries the float nearest it, folding the expression must drop that float, or the result would be
// 1+2^-23 instead of 1.
CHECK(same(float_of(fixture,"float x=1.0+0.000000059604644775390625001; return x;"),1.0f));
CHECK(same(float_of(fixture,"float x=1.000000059604644775390625001+0.0; return x;"),1.0f));
CHECK(same(float_of(fixture,"float x=1.000000059604644775390625001*1.0; return x;"),1.0f));
CHECK(same(float_of(fixture,"float x=-1.000000059604644775390625001*1.0; return x;"),-1.0f));
}

TEST_CASE("integer, based, separated and exponent literals keep working")
{
ScriptFixture fixture;
CHECK(holds(fixture,"1'000'000==1000000"));
CHECK(holds(fixture,"0x1F==31&&0X1f==31&&0b101==5&&0o17==15&&0d99==99&&0xFF'FF==65535"));
CHECK(holds(fixture,"9223372036854775807==int64(0x7FFFFFFFFFFFFFFF)"));
CHECK(holds(fixture,"1'000.5==1000.5"));
CHECK(holds(fixture,"0.000'001==1e-6"));
CHECK(holds(fixture,"1e1'0==10000000000.0"));
CHECK(holds(fixture,"2.5E+3==2500.0&&2.5e3==2500.0&&25e-1==2.5"));
CHECK(holds(fixture,"1e-5==0.00001"));
CHECK(holds(fixture,".5==0.5&&.5f==0.5f"));
CHECK(holds(fixture,"1.==1.0"));
CHECK(holds(fixture,"1.5e3f==1500.0f"));
CHECK(holds(fixture,"1'234.567'8e1'0==12345678000000.0"));
CHECK(holds(fixture,"0.0==0.0&&0e0==0.0&&0.0e99999999999999999999==0.0"));
// A literal longer than the scanner's buffer on the stack, with separators.
CHECK(same(double_of(fixture,"return 0.1000'0000'0000'0000'0555'1115'1231'2578'2702'1181'5834'0454'1015'6250'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0000'0001;"),0.1));
CHECK(same(double_of(fixture,"return 1e2'000;"),std::numeric_limits<double>::infinity()));
}

TEST_CASE("100 000 random doubles and floats round-trip through their shortest literal")
{
ScriptFixture fixture;
REQUIRE(fixture.engine->RegisterGlobalFunction("void collect_double(double)",asFUNCTION(collect_double),asCALL_CDECL)>=0);
REQUIRE(fixture.engine->RegisterGlobalFunction("void collect_float(float)",asFUNCTION(collect_float),asCALL_CDECL)>=0);
constexpr int kValues=100000;
// Scripts of 3000 calls: one script of 30 000 compiles more slowly per call.
constexpr int kBatch=1000;
std::mt19937_64 random(0x5A0D1AC5u);
// Half any finite bit pattern (every exponent, subnormals included), half the short decimals scripts usually hold.
const auto next_double=[&random](int i){
for(;;){
double x;
if(i&1)x=static_cast<double>(random()%2000000001)/std::pow(10.0,static_cast<double>(random()%10));
else x=std::bit_cast<double>(random()&0x7FFFFFFFFFFFFFFFu);
if(std::isfinite(x))return x;
}
};
const auto next_float=[&random](int i){
for(;;){
float x;
if(i&1)x=static_cast<float>(random()%20000001)/std::pow(10.0f,static_cast<float>(random()%8));
else x=std::bit_cast<float>(static_cast<std::uint32_t>(random())&0x7FFFFFFFu);
if(std::isfinite(x))return x;
}
};
std::vector<double> doubles;
std::vector<float> floats;
doubles.reserve(kValues);
floats.reserve(kValues);
for(int i=0;i<kValues;++i){
doubles.push_back(next_double(i));
floats.push_back(next_float(i));
}
collected_doubles.clear();
collected_floats.clear();
collected_doubles.reserve(kValues);
collected_floats.reserve(2*kValues);
// Each float goes in twice: as an f literal and as a double literal converted to the float argument.
std::string source;
for(int first=0;first<kValues;first+=kBatch){
source.assign("void main(){");
for(int i=first;i<first+kBatch;++i){
const std::string d=literal_of(doubles[static_cast<std::size_t>(i)]);
const std::string f=literal_of(floats[static_cast<std::size_t>(i)]);
source.append("collect_double(").append(d).append(");collect_float(").append(f).append("f);collect_float(").append(f).append(");");
}
source.append("}");
REQUIRE(fixture.run(source.c_str(),"void main()")==asEXECUTION_FINISHED);
}
REQUIRE(collected_doubles.size()==doubles.size());
REQUIRE(collected_floats.size()==2*floats.size());
int double_failures=0;
int float_failures=0;
for(std::size_t i=0;i<doubles.size();++i){
if(!same(collected_doubles[i],doubles[i])&&++double_failures<=10)FAIL_CHECK("double literal "<<literal_of(doubles[i])<<" compiled to "<<literal_of(collected_doubles[i]));
if(!same(collected_floats[2*i],floats[i])&&++float_failures<=10)FAIL_CHECK("float literal "<<literal_of(floats[i])<<"f compiled to "<<literal_of(collected_floats[2*i]));
if(!same(collected_floats[2*i+1],floats[i])&&++float_failures<=10)FAIL_CHECK("double literal "<<literal_of(floats[i])<<" converted to float gave "<<literal_of(collected_floats[2*i+1]));
}
CHECK(double_failures==0);
CHECK(float_failures==0);
}
