// Tests the random numbers (Xoshiro256 and register_random, spec 8.5 of the core-types design and 8.2 of the library-and-packs
// design): xoshiro256** golden values after SplitMix64 seeding (from an independent reference implementation), Lemire's
// unbiased range with its swapped and full-width bounds, the unit interval, a chi-square uniformity check, the per-thread
// generator, and the script functions, random_generator and array<T>::random_choice with its exception.
#include "script_fixture.hpp"
#include <core/library/random.hpp>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

using zodiac_test::ScriptFixture;

namespace {
// Runs "bool main(){<body>}" and returns its result.
bool holds(ScriptFixture& fixture,std::string_view body)
{
const std::string source="bool main(){"+std::string(body)+"}";
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

TEST_CASE("Xoshiro256 is 32 bytes of trivially copyable state")
{
static_assert(sizeof(zodiac::Xoshiro256)==32);
static_assert(std::is_trivially_copyable_v<zodiac::Xoshiro256>);
}

TEST_CASE("seed(42) gives the reference xoshiro256** outputs")
{
zodiac::Xoshiro256 generator;
generator.seed(42);
CHECK(generator.next()==0x15780b2e0c2ec716u);
CHECK(generator.next()==0x6104d9866d113a7eu);
CHECK(generator.next()==0xae17533239e499a1u);
CHECK(generator.next()==0xecb8ad4703b360a1u);
}

TEST_CASE("range and unit after seed(42)")
{
zodiac::Xoshiro256 generator;
generator.seed(42);
const std::int64_t expected[]={9,38,69,93,100};
for(const std::int64_t value:expected)CHECK(generator.range(1,100)==value);
generator.seed(42);
CHECK(generator.unit()==0.08386297105988216);
CHECK(generator.unit()==0.3789802506626686);
CHECK(generator.unit()==0.6800434110281394);
generator.seed(42);
CHECK_FALSE(generator.coin());
CHECK_FALSE(generator.coin());
CHECK(generator.coin());
CHECK(generator.coin());
}

TEST_CASE("range swaps reversed bounds and handles the full int64 range")
{
zodiac::Xoshiro256 generator;
generator.seed(7);
bool seen[6]={};
for(int i=0;i<1000;++i){
const std::int64_t value=generator.range(5,1);
REQUIRE(value>=1);
REQUIRE(value<=5);
seen[value]=true;
}
for(int value=1;value<=5;++value)CHECK(seen[value]);
for(int i=0;i<1000;++i)CHECK(generator.range(3,3)==3);
constexpr std::int64_t low=std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t high=std::numeric_limits<std::int64_t>::max();
bool negative=false,positive=false;
for(int i=0;i<1000;++i){
const std::int64_t value=generator.range(low,high);
negative|=value<0;
positive|=value>0;
}
CHECK(negative);
CHECK(positive);
for(int i=0;i<1000;++i){
const std::int64_t value=generator.range(high-1,high);
CHECK((value==high-1||value==high));
}
for(int i=0;i<1000;++i){
const double value=generator.unit();
REQUIRE(value>=0.0);
REQUIRE(value<1.0);
}
}

TEST_CASE("range(0, 9) is uniform by a chi-square test over 100 000 draws")
{
zodiac::Xoshiro256 generator;
generator.seed(0x5EED);
constexpr int draws=100000;
long counts[10]={};
for(int i=0;i<draws;++i)++counts[generator.range(0,9)];
constexpr double expected=draws/10.0;
double chi_square=0;
for(const long count:counts){
const double difference=static_cast<double>(count)-expected;
chi_square+=difference*difference/expected;
}
INFO("chi-square: ",chi_square);
// The 99.9 % point of the chi-square distribution with 9 degrees of freedom.
CHECK(chi_square<27.877);
}

TEST_CASE("seed_from_entropy gives different generators")
{
zodiac::Xoshiro256 first,second;
first.seed_from_entropy();
second.seed_from_entropy();
CHECK(first.next()!=second.next());
}

TEST_CASE("each thread has its own seeded generator")
{
zodiac::Xoshiro256* here=&zodiac::thread_generator();
const std::uint64_t here_value=here->next();
zodiac::Xoshiro256* there=nullptr;
std::uint64_t there_values[2]={};
std::thread thread([&]{
there=&zodiac::thread_generator();
there_values[0]=there->next();
there_values[1]=zodiac::thread_generator().next();
});
thread.join();
CHECK(here==&zodiac::thread_generator());
CHECK(there!=here);
// An unseeded (all-zero) xoshiro256** state only ever gives 0.
CHECK((here_value|here->next())!=0);
CHECK((there_values[0]|there_values[1])!=0);
CHECK(there_values[0]!=here_value);
}

TEST_CASE("script random functions")
{
ScriptFixture fixture;
CHECK(holds(fixture,"for(int i=0;i<1000;++i)if(random(3,3)!=3)return false;return true;"));
CHECK(holds(fixture,"for(int i=0;i<1000;++i){int v=random(5,1);if(v<1||v>5)return false;}return true;"));
CHECK(holds(fixture,"for(int i=0;i<1000;++i){int v=random(-2147483647-1,2147483647);if(v<-2147483647-1||v>2147483647)return false;}return true;"));
CHECK(holds(fixture,"for(int i=0;i<1000;++i){double v=random_float();if(v<0||v>=1)return false;}return true;"));
CHECK(holds(fixture,"for(int i=0;i<1000;++i){double v=random_float(2.0,5.0);if(v<2||v>5)return false;}return true;"));
CHECK(holds(fixture,"int t=0;for(int i=0;i<1000;++i)if(random_bool())++t;return t>0&&t<1000;"));
}

TEST_CASE("script random_generator gives reproducible sequences")
{
ScriptFixture fixture;
CHECK(holds(fixture,"random_generator a;random_generator b;a.seed(42);b.seed(42);"
"for(int i=0;i<100;++i){if(a.random(1,100)!=b.random(1,100))return false;if(a.random_float()!=b.random_float())return false;"
"if(a.random_float(-1.0,1.0)!=b.random_float(-1.0,1.0))return false;if(a.random_bool()!=b.random_bool())return false;}return true;"));
CHECK(holds(fixture,"random_generator g;g.seed(42);return g.random(1,100)==9&&g.random(1,100)==38&&g.random(1,100)==69&&g.random(1,100)==93&&g.random(1,100)==100;"));
// The exact unit() value built in the script: AngelScript's own literal parser is not correctly rounded.
CHECK(holds(fixture,"random_generator g;g.seed(42);return g.random_float()==double(0x15780b2e0c2ec716>>11)/9007199254740992.0;"));
CHECK(holds(fixture,"random_generator g;g.seed(42);return !g.random_bool()&&!g.random_bool()&&g.random_bool()&&g.random_bool();"));
CHECK(holds(fixture,"random_generator g;g.seed(42);random_generator c=g;return g.random(1,1000000)==c.random(1,1000000);"));
// random_generator(n) is seed(n); the copy factory, opAssign and by-value parameters copy the state, handles share it.
CHECK(holds(fixture,"random_generator g(42);return g.random(1,100)==9&&g.random(1,100)==38;"));
CHECK(holds(fixture,"random_generator g(42);random_generator c;c=g;g.random(1,100);return c.random(1,100)==9&&g.random(1,100)==38;"));
CHECK(holds(fixture,"random_generator g(42);random_generator c(g);c.random(1,100);return g.random(1,100)==9;"));
CHECK(holds(fixture,"random_generator g(42);random_generator@ h=g;h.random(1,100);return g.random(1,100)==38;"));
CHECK(holds(fixture,"random_generator a;random_generator b;return a.random(-2147483647-1,2147483647)!=b.random(-2147483647-1,2147483647)||a.random(-2147483647-1,2147483647)!=b.random(-2147483647-1,2147483647);"));
}

TEST_CASE("script array random_choice")
{
ScriptFixture fixture;
CHECK(holds(fixture,"array<int> a={1,2,3};for(int i=0;i<100;++i){int v=a.random_choice();if(v<1||v>3)return false;}return true;"));
CHECK(holds(fixture,"array<int> a={1,2,3};random_generator g;for(int i=0;i<100;++i){int v=a.random_choice(g);if(v<1||v>3)return false;}return true;"));
// random_choice(g) advances g itself: generators seeded alike agree, and a copy taken before the draws falls behind.
CHECK(holds(fixture,"array<int> a;for(int i=0;i<1000;++i)a.insert_last(i);random_generator g(5);random_generator h(5);random_generator c=g;"
"for(int i=0;i<100;++i)if(a.random_choice(g)!=a.random_choice(h))return false;"
"int same=0;for(int i=0;i<100;++i)if(a.random_choice(g)==a.random_choice(c))++same;return same<50;"));
CHECK(holds(fixture,"array<int> a;for(int i=0;i<1000;++i)a.insert_last(i);random_generator g(5);random_generator c(5);"
"for(int i=0;i<10;++i)a.random_choice(g);for(int i=0;i<10;++i)c.random(0,999);return g.random(0,999999)==c.random(0,999999);"));
CHECK(holds(fixture,"array<string> a={\"x\",\"y\"};string v=a.random_choice();return v==\"x\"||v==\"y\";"));
CHECK(holds(fixture,"array<array<int>@> a={array<int>={4}};return a.random_choice()[0]==4;"));
CHECK(error_of(fixture,"array<int> e;e.random_choice()")=="random_choice on an empty array");
CHECK(error_of(fixture,"array<int> e;random_generator g;e.random_choice(g)")=="random_choice on an empty array");
}
