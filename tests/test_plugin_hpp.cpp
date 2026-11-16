#include "script_fixture.hpp"
#include <zodiac/plugin.hpp>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using zodiac_test::ScriptFixture;

namespace {
int boom(int)
{
throw std::runtime_error("patlad\xC4\xB1");
}
std::vector<std::string> split_words(std::string_view text)
{
std::vector<std::string> words;
size_t start=0;
while(start<=text.size()){
const size_t end=text.find(' ',start);
const size_t stop=end==std::string_view::npos?text.size():end;
words.emplace_back(text.substr(start,stop-start));
start=stop+1;
}
return words;
}
std::string shout(std::string_view text)
{
std::string result(text);
result.push_back('!');
return result;
}
double combine(int32_t a,int64_t b,float c,double d,bool e)
{
return e?double(a)+double(b)+double(c)+d:0.0;
}
bool g_touched=false;
void touch()
{
g_touched=true;
}
bool is_even(int64_t value)
{
return value%2==0;
}
float half(float value)
{
return value/2.0f;
}
int64_t twice(int64_t value)
{
return value*2;
}
}

TEST_CASE("function<> wraps a throwing C++ function")
{
ScriptFixture fixture;
REQUIRE(zodiac::plugin::function<&boom>(fixture.abi(),"int boom(int)")==ZODIAC_OK);
REQUIRE(fixture.run("int main(){return boom(1);}","int main()")==asEXECUTION_EXCEPTION);
CHECK(fixture.exception_string()=="patlad\xC4\xB1");
}

TEST_CASE("function<> wraps vector<string> return")
{
ScriptFixture fixture;
REQUIRE(zodiac::plugin::function<&split_words>(fixture.abi(),"array<string>@ split_words(const string &in)")==ZODIAC_OK);
REQUIRE(fixture.run("string main(){array<string>@ w=split_words(\"kral kanka selam\");return w.length()+\"|\"+w[0]+\"|\"+w[1]+\"|\"+w[2];}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="3|kral|kanka|selam");
}

TEST_CASE("function<> converts string_view in and string out")
{
ScriptFixture fixture;
REQUIRE(zodiac::plugin::function<&shout>(fixture.abi(),"string shout(const string &in)")==ZODIAC_OK);
REQUIRE(fixture.run("string main(){return shout(\"hey\");}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="hey!");
}

TEST_CASE("function<> passes every primitive kind")
{
ScriptFixture fixture;
REQUIRE(zodiac::plugin::function<&combine>(fixture.abi(),"double combine(int, int64, float, double, bool)")==ZODIAC_OK);
REQUIRE(zodiac::plugin::function<&touch>(fixture.abi(),"void touch()")==ZODIAC_OK);
REQUIRE(zodiac::plugin::function<&is_even>(fixture.abi(),"bool is_even(int64)")==ZODIAC_OK);
REQUIRE(zodiac::plugin::function<&half>(fixture.abi(),"float half(float)")==ZODIAC_OK);
REQUIRE(zodiac::plugin::function<&twice>(fixture.abi(),"int64 twice(int64)")==ZODIAC_OK);
g_touched=false;
REQUIRE(fixture.run("double main(){touch();if(!is_even(4)||is_even(3))return -1;return combine(1,2,0.5f,0.25,true)+half(3.0f)+double(twice(5000000000));}","double main()")==asEXECUTION_FINISHED);
CHECK(g_touched);
CHECK(fixture.context->GetReturnDouble()==doctest::Approx(3.75+1.5+10000000000.0));
}

TEST_CASE("function<> reports registration errors through the return code")
{
ScriptFixture fixture;
REQUIRE(zodiac::plugin::function<&touch>(fixture.abi(),"void touch()")==ZODIAC_OK);
CHECK(zodiac::plugin::function<&touch>(fixture.abi(),"void touch()")==ZODIAC_E_ALREADY_REGISTERED);
}
