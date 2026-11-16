// Tests Zodiac's script array<T> (zodiac::ScriptArray registered by register_array, spec 8.2): every method with int,
// double, string and script class elements, initializer lists, the range rules with their exact error texts, that none of
// the add-on's camelCase names is registered, the native fast paths of sort/sort_descending/find/contains/opEquals against
// the script callback path on 1 000 random arrays per element type, callback sorts that fail or change the array,
// join() on array<string>, and garbage collection of handle cycles through arrays (Review Focus 4).
#include "script_fixture.hpp"
#include <core/types/array.hpp>
#include <core/types/string.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using zodiac_test::ScriptFixture;

namespace {
// Script helpers every test source starts with: show() writes the elements separated by commas.
constexpr std::string_view prelude=
"class Item{int v;Item(){v=0;}Item(int x){v=x;}int opCmp(const Item&in o) const{return v-o.v;}bool opEquals(const Item&in o) const{return v==o.v;}}\n"
"class Bare{}\n"
"string show(const array<int>&in a){string s;for(int i=0;i<a.length();++i){if(i>0)s+=\",\";s+=a[i];}return s;}\n"
"string show(const array<double>&in a){string s;for(int i=0;i<a.length();++i){if(i>0)s+=\",\";s+=a[i];}return s;}\n"
"string show(const array<string>&in a){string s;for(int i=0;i<a.length();++i){if(i>0)s+=\",\";s+=a[i];}return s;}\n"
"string show(const array<Item>&in a){string s;for(int i=0;i<a.length();++i){if(i>0)s+=\",\";s+=a[i].v;}return s;}\n"
"string show(const array<Item@>&in a){string s;for(int i=0;i<a.length();++i){if(i>0)s+=\",\";if(a[i] is null)s+=\"null\";else s+=a[i].v;}return s;}\n";
// Runs "string main(){<body>}" after the prelude and returns the bytes of the result.
std::string value_of(ScriptFixture& fixture,std::string_view body)
{
const std::string source=std::string(prelude)+"string main(){"+std::string(body)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"string main()")==asEXECUTION_FINISHED);
return fixture.returned_string();
}
// Runs "void main(){<statements>}" after the prelude, which must raise a script exception, and returns its text.
std::string error_of(ScriptFixture& fixture,std::string_view statements)
{
const std::string source=std::string(prelude)+"void main(){"+std::string(statements)+"}";
INFO(source);
REQUIRE(fixture.run(source.c_str(),"void main()")==asEXECUTION_EXCEPTION);
return fixture.exception_string();
}
// True when "void main(){<statements>}" after the prelude compiles.
bool compiles(ScriptFixture& fixture,std::string_view statements)
{
const std::string source=std::string(prelude)+"void main(){"+std::string(statements)+"}";
asIScriptModule* module=fixture.engine->GetModule("test",asGM_ALWAYS_CREATE);
REQUIRE(module!=nullptr);
REQUIRE(module->AddScriptSection("test",source.c_str())>=0);
fixture.messages.clear();
return module->Build()>=0;
}
std::string replaced(std::string text,std::string_view from,std::string_view to)
{
for(std::size_t at=text.find(from);at!=std::string::npos;at=text.find(from,at+to.size()))text.replace(at,from.size(),to);
return text;
}
// One element type for the random comparison: its name, an expression making a random value with many duplicates, and the
// script less/greater comparisons the callback path sorts with.
struct RandomCase{
std::string_view type;
std::string_view make;
std::string_view less;
std::string_view greater;
};
// Compares the native paths with the callback path and plain loops on 1 000 random arrays (fixed seed) of TYPE.
constexpr std::string_view random_source=
"uint seed=20261116;\n"
"uint next(){seed=seed*1103515245+12345;return (seed>>16)&0x7FFF;}\n"
"array<string> words={\"\",\"a\",\"ab\",\"b\",\"\xC5\x9F" "ehir\",\"\xC5\x9E" "ehir\",\"\xC3\xA7" "ay\",\"Zeytin\",\"a long string that lives on the heap!\",\"a long string that lives on the heap?\"};\n"
"TYPE make(){return MAKE;}\n"
"bool same(const array<TYPE>&in a,const array<TYPE>&in b){if(a.length()!=b.length())return false;for(int i=0;i<a.length();++i)if(!(a[i]==b[i]))return false;return true;}\n"
"int linear_find(const array<TYPE>&in a,const TYPE&in v,int start){for(int i=start;i<a.length();++i)if(a[i]==v)return i;return -1;}\n"
"string main(){\n"
"for(int round=0;round<1000;++round){\n"
"array<TYPE> a;int n=int(next()%40);for(int i=0;i<n;++i)a.insert_last(make());\n"
"array<TYPE> b=a;array<TYPE> c=a;\n"
"a.sort();b.sort(function(x,y){return LESS;});\n"
"if(!same(a,b))return \"sort \"+round;\n"
"a.sort_descending();b.sort(function(x,y){return GREATER;});\n"
"if(!same(a,b))return \"sort_descending \"+round;\n"
"TYPE v=make();int start=int(next()%uint(n+1));\n"
"if(c.find(v)!=linear_find(c,v,0))return \"find \"+round;\n"
"if(c.find(v,start)!=linear_find(c,v,start))return \"find from \"+round;\n"
"if(c.contains(v)!=(linear_find(c,v,0)>=0))return \"contains \"+round;\n"
"array<TYPE> d=c;if(n>0&&next()%2==0)d[int(next()%uint(n))]=make();if(next()%5==0)d.insert_last(make());\n"
"if((c==d)!=same(c,d)||!(c==c))return \"equals \"+round;\n"
"}\n"
"return \"ok\";\n"
"}\n";
int g_destroyed=0;
void node_destroyed()
{
++g_destroyed;
}
}

TEST_CASE("array is Zodiac's array with style-C names only")
{
ScriptFixture fixture;
asITypeInfo* type=fixture.engine->GetTypeInfoByName("array");
REQUIRE(type!=nullptr);
CHECK((type->GetFlags()&(asOBJ_REF|asOBJ_GC|asOBJ_TEMPLATE))==(asOBJ_REF|asOBJ_GC|asOBJ_TEMPLATE));
std::set<std::string> names;
for(asUINT index=0;index<type->GetMethodCount();++index){
const std::string name=type->GetMethodByIndex(index)->GetName();
if(!name.starts_with("op"))names.insert(name);
}
const std::set<std::string> expected{"length","empty","resize","reserve","clear","insert_at","insert_last","remove_at","remove_last","remove_range","find","find_by_ref","contains","sort","sort_descending","reverse","slice","join"};
CHECK(names==expected);
CHECK(compiles(fixture,"array<int> a;a.insert_last(1);int[] b={1};"));
CHECK_FALSE(compiles(fixture,"array<int> a;a.insertLast(1);"));
CHECK_FALSE(compiles(fixture,"array<int> a;a.sortAsc();"));
CHECK_FALSE(compiles(fixture,"array<int> a;a.isEmpty();"));
CHECK_FALSE(compiles(fixture,"array<int> a;a.removeAt(0);"));
}

TEST_CASE("int elements")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<int> a={5,3,8};a.insert_last(1);a.insert_at(0,9);a.insert_at(5,7);return show(a)+\"|\"+a.length()+a[0]+a.empty();")=="9,5,3,8,1,7|69false");
CHECK(value_of(fixture,"array<int> a={1,2,3,4,5,6};a.remove_at(0);a.remove_last();a.remove_range(1,2);return show(a);")=="2,5");
CHECK(value_of(fixture,"array<int> a={1,2,3,4};a.remove_range(1);array<int> b={1,2,3};b.remove_range(1,100);array<int> c={1,2};c.remove_range(2,5);c.remove_range(0,0);return show(a)+\"|\"+show(b)+\"|\"+show(c);")=="1|1|1,2");
CHECK(value_of(fixture,"array<int> a={1,2};a.resize(4);string s=show(a);a.resize(1);a.reserve(100);return s+\"|\"+show(a)+\"|\"+a.length();")=="1,2,0,0|1|1");
CHECK(value_of(fixture,"array<int> a={1,2};a.clear();string s=a.empty()+\"\"+a.length();a.insert_last(3);return s+show(a);")=="true03");
CHECK(value_of(fixture,"array<int> a={4,2,4,1};return a.find(4)+\",\"+a.find(4,1)+\",\"+a.find(9)+\",\"+a.find(4,4)+\",\"+a.contains(1)+\",\"+a.contains(7);")=="0,2,-1,-1,true,false");
CHECK(value_of(fixture,"array<int> a={3,-1,2,2};a.sort();string s=show(a);a.sort_descending();s+=\"|\"+show(a);a.sort(function(x,y){return x<y;});s+=\"|\"+show(a);a.reverse();return s+\"|\"+show(a);")=="-1,2,2,3|3,2,2,-1|-1,2,2,3|3,2,2,-1");
CHECK(value_of(fixture,"array<int> a={1,2,3,4};return show(a.slice(1,2))+\"|\"+show(a.slice(2))+\"|\"+show(a.slice(1,100))+\"|\"+show(a.slice(4))+\"|\"+show(a.slice(0,0));")=="2,3|3,4|2,3,4||");
CHECK(value_of(fixture,"array<int> a={1,2};array<int> b;b=a;b[0]=5;array<int> c={1,2};return (a==c)+\",\"+(a==b)+\",\"+(a!=b)+\",\"+show(a)+\",\"+show(b);")=="true,false,true,1,2,5,2");
CHECK(value_of(fixture,"array<int> a={1,4};array<int> b={2,3};a.insert_at(1,b);a.insert_at(2,a);return show(a);")=="1,2,1,2,3,4,3,4");
CHECK(value_of(fixture,"array<int> a={1,2,3};int s=0;int k=0;foreach(int v,int i:a){s+=v;k+=i;}return s+\",\"+k;")=="6,3");
CHECK(value_of(fixture,"array<int> a(3);array<int> b(2,7);int[] c={8};array<int> e;return show(a)+\"|\"+show(b)+\"|\"+show(c)+\"|\"+e.length();")=="0,0,0|7,7|8|0");
CHECK(value_of(fixture,"array<int> a;for(int i=0;i<1000;++i)a.insert_last(i);a.insert_last(a[0]);int s=0;for(int i=0;i<a.length();++i)s+=a[i];return \"\"+a.length()+\",\"+s;")=="1001,499500");
CHECK(value_of(fixture,"array<int8> a={-3,5,-128,127};a.sort();array<uint64> b={18446744073709551615,0,5};b.sort();return a[0]+\",\"+a[3]+\",\"+b[0]+\",\"+b[2];")=="-128,127,0,18446744073709551615");
CHECK(value_of(fixture,"array<bool> a={true,false,true,false};a.sort();string s=a[0]+\",\"+a[3];a.sort_descending();return s+\",\"+a[0]+\",\"+a.find(false);")=="false,true,true,2");
}

TEST_CASE("enum elements compare by their value")
{
ScriptFixture fixture;
REQUIRE(fixture.run("enum Color{red=3,green=1,blue=2} string main(){array<Color> a={red,green,blue};a.sort();return a[0]+\",\"+a[2]+\",\"+a.find(blue)+\",\"+(a==array<Color>={green,blue,red});}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="1,3,1,true");
}

TEST_CASE("double elements")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<double> a={2.5,-1,0.125};a.insert_last(4);a.insert_at(1,1.5);return show(a);")=="2.5,1.5,-1,0.125,4");
CHECK(value_of(fixture,"array<double> a={2.5,-1,0.125,2.5};a.sort();string s=show(a);a.sort_descending();return s+\"|\"+show(a)+\"|\"+a.find(2.5)+a.find(3)+a.contains(-1);")=="-1,0.125,2.5,2.5|2.5,2.5,0.125,-1|0-1true");
CHECK(value_of(fixture,"array<double> a={1,2,3};a.resize(5);a.remove_at(0);a.remove_range(2,1);return show(a)+\"|\"+show(a.slice(1));")=="2,3,0|3,0");
CHECK(value_of(fixture,"array<double> a={0.5,1};array<double> b={0.5,1};array<double> c={0.5};return (a==b)+\",\"+(a==c)+\",\"+a.length();")=="true,false,2");
CHECK(value_of(fixture,"array<float> a={2.5f,-0.5f};a.sort();return a[0]+\",\"+a[1]+\",\"+a.find(2.5f);")=="-0.5,2.5,1");
}

TEST_CASE("string elements")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<string> a={\"\xC5\x9F" "ehir\",\"araba\",\"\xC3\xA7" "ay\",\"Zeytin\"};a.sort();string s=show(a);a.sort_descending();return s+\"|\"+show(a);")=="Zeytin,araba,\xC3\xA7" "ay,\xC5\x9F" "ehir|\xC5\x9F" "ehir,\xC3\xA7" "ay,araba,Zeytin");
CHECK(value_of(fixture,"array<string> a={\"a\",\"b\"};a.insert_last(\"a string long enough to live on the heap\");a.insert_at(0,\"\xC4\xB0\");a.remove_at(1);return show(a)+\"|\"+a.find(\"b\")+a.find(\"z\")+a.contains(\"a string long enough to live on the heap\");")=="\xC4\xB0,b,a string long enough to live on the heap|1-1true");
CHECK(value_of(fixture,"array<string> a={\"x\",\"y\"};array<string> b=a;b[0]+=\"!\";a.resize(3);return show(a)+\"|\"+show(b)+\"|\"+(a[2]==\"\")+(a==b);")=="x,y,|x!,y|truefalse");
CHECK(value_of(fixture,"array<string> a={\"a\",\"b\",\"c\"};array<string> b={\"a\",\"b\",\"c\"};return (a==b)+\",\"+show(a.slice(1,1))+\",\"+a.join(\"-\")+\",\"+join(a,\"+\");")=="true,b,a-b-c,a+b+c");
CHECK(value_of(fixture,"array<string> a=\"a,b\".split(\",\");a.insert_last(\"c\");a.sort_descending();return a.join(\"\")+a.length();")=="cba3");
CHECK(value_of(fixture,"array<string> a;array<string> b={\"\xC5\x9F\"};return a.join(\",\")+\"|\"+b.join(\",\");")=="|\xC5\x9F");
CHECK(value_of(fixture,"array<string> a={\"b\",\"a\"};a.sort(function(x,y){return x.length()>y.length()||(x.length()==y.length()&&x<y);});return show(a);")=="a,b");
}

TEST_CASE("join is only usable on array<string>")
{
ScriptFixture fixture;
CHECK(compiles(fixture,"array<string> a;a.join(\",\");"));
CHECK_FALSE(compiles(fixture,"array<int> a;a.join(\",\");"));
CHECK(error_of(fixture,"array<int> a={1};a.join(0);")=="join needs an array of strings");
}

TEST_CASE("script class elements")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<Item> a(2);a.insert_last(Item(5));a.insert_at(0,Item(3));return show(a)+\"|\"+a.find(Item(5))+a.find(Item(7))+a.contains(Item(3));")=="3,0,0,5|3-1true");
CHECK(value_of(fixture,"array<Item> a={Item(4),Item(1),Item(3)};a.sort();string s=show(a);a.sort_descending();s+=\"|\"+show(a);a.sort(function(x,y){return x.v<y.v;});return s+\"|\"+show(a);")=="1,3,4|4,3,1|1,3,4");
CHECK(value_of(fixture,"Item x(3);array<Item> a={x};x.v=9;array<Item@> h={x};x.v=10;return show(a)+\"|\"+show(h);")=="3|10");
CHECK(value_of(fixture,"array<Item> a={Item(1),Item(2)};array<Item> b;b=a;b[0].v=7;array<Item> c={Item(1),Item(2)};return show(a)+\"|\"+show(b)+\"|\"+(a==c)+(a==b)+\"|\"+show(a.slice(1));")=="1,2|7,2|truefalse|2");
CHECK(value_of(fixture,"Item x(2);Item y(2);array<Item@> h={x,null,y};return \"\"+h.find_by_ref(y)+h.find_by_ref(x)+h.find(y)+h.find(null)+h.find_by_ref(y,3);")=="2001-1");
CHECK(value_of(fixture,"array<Item@> h={Item(2),null,Item(1)};h.sort();string s=show(h);h.sort_descending();return s+\"|\"+show(h);")=="null,1,2|2,1,null");
CHECK(value_of(fixture,"array<Item@> h={Item(2),Item(1)};h.reverse();h.remove_last();h.resize(3);return show(h);")=="1,null,null");
CHECK(error_of(fixture,"array<Bare> b(1);b.sort();")=="type \"Bare\" has no opCmp method");
CHECK(error_of(fixture,"array<Bare> b(1);b.find(Bare());")=="type \"Bare\" has no opEquals or opCmp method");
}

TEST_CASE("initializer lists")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<array<int>> n={{1},{2,3},{}};return n.length()+\":\"+show(n[1])+\":\"+n[2].length();")=="3:2,3:0");
CHECK(value_of(fixture,"array<string> s={};array<string> t={\"\xC5\x9F\",\"\"};return s.length()+\",\"+t.length()+\",\"+t[0];")=="0,2,\xC5\x9F");
CHECK(value_of(fixture,"array<Item@> h={Item(1),null};array<Item> v={Item(2)};return show(h)+\"|\"+show(v);")=="1,null|2");
}

TEST_CASE("range errors use the exact texts")
{
ScriptFixture fixture;
CHECK(error_of(fixture,"array<int> a={1};a[1];")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a[-1];")=="index out of range");
CHECK(error_of(fixture,"array<string> a;a[0];")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.insert_at(2,0);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.insert_at(-1,0);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};array<int> b={2};a.insert_at(3,b);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.remove_at(1);")=="index out of range");
CHECK(error_of(fixture,"array<int> a;a.remove_last();")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.remove_range(2,1);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.remove_range(-1,1);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.remove_range(0,-2);")=="negative count");
CHECK(error_of(fixture,"array<int> a={1};a.slice(2);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.slice(0,-2);")=="negative count");
CHECK(error_of(fixture,"array<int> a={1};a.find(1,2);")=="index out of range");
CHECK(error_of(fixture,"array<int> a={1};a.find(1,-1);")=="index out of range");
CHECK(error_of(fixture,"array<Item@> a;Item x;a.find_by_ref(x,1);")=="index out of range");
CHECK(error_of(fixture,"array<int> a;a.resize(-1);")=="negative count");
CHECK(error_of(fixture,"array<int> a;a.reserve(-1);")=="negative count");
CHECK(error_of(fixture,"array<int> a(-1);")=="negative count");
CHECK(error_of(fixture,"array<string> a(-1,\"x\");")=="negative count");
}

TEST_CASE("native fast paths match the callback path on random arrays")
{
const RandomCase cases[]={
{"bool","next()%2==0","!x&&y","x&&!y"},
{"int8","int8(int(next()%21)-10)","x<y","x>y"},
{"int16","int16(int(next()%2001)-1000)","x<y","x>y"},
{"int","int(next()%201)-100","x<y","x>y"},
{"int64","int64(next())*int64(next()%7)-int64(50000)","x<y","x>y"},
{"uint8","uint8(next()%30)","x<y","x>y"},
{"uint16","uint16(next()%3000)","x<y","x>y"},
{"uint","next()%100","x<y","x>y"},
{"uint64","uint64(next()%50)*uint64(4294967296)+uint64(next()%3)","x<y","x>y"},
{"float","float(int(next()%41)-20)/4.0f","x<y","x>y"},
{"double","double(int(next()%41)-20)/8.0","x<y","x>y"},
{"string","words[next()%uint(words.length())]","x<y","x>y"},
};
for(const RandomCase& item:cases){
INFO(item.type);
ScriptFixture fixture;
std::string source=replaced(std::string(random_source),"TYPE",item.type);
source=replaced(source,"MAKE",item.make);
source=replaced(source,"LESS",item.less);
source=replaced(source,"GREATER",item.greater);
REQUIRE(fixture.run(source.c_str(),"string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="ok");
}
}

TEST_CASE("callback sorts leave the array unchanged when the compare fails or changes it")
{
ScriptFixture fixture;
CHECK(value_of(fixture,"array<int> a={3,1,2};string e;try{a.sort(function(x,y){throw(\"boom\");return x<y;});}catch{e=exception_info();}return show(a)+\"|\"+e;")=="3,1,2|boom");
REQUIRE(fixture.run("class Bad{int v;Bad(int x){v=x;}int opCmp(const Bad&in o) const{throw(\"no order\");return 0;}} string main(){array<Bad@> a={Bad(2),Bad(1)};string e;try{a.sort();}catch{e=exception_info();}return a[0].v+e;}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="2no order");
REQUIRE(fixture.run("array<string> g={\"c\",\"a\",\"b\"}; string main(){string e;try{g.sort(function(x,y){g.remove_last();return x<y;});}catch{e=exception_info();}return g.length()+e;}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="2the array changed while it was sorted");
REQUIRE(fixture.run("array<int> g={3,1,2}; string main(){string e;try{g.sort(function(x,y){g.insert_last(0);return x<y;});}catch{e=exception_info();}return g[0]+e;}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="3the array changed while it was sorted");
REQUIRE(fixture.run("class Node{int v;Node(int x){v=x;}} array<Node@> g={Node(3),Node(1),Node(2)}; string main(){g.sort(function(x,y){@g[0]=Node(9);return x.v<y.v;});string s;for(int i=0;i<g.length();++i)s+=g[i].v;return s;}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="123");
}

TEST_CASE("ScriptArray from C++: NaN sorts last, element type and string detection")
{
ScriptFixture fixture;
asITypeInfo* doubles=fixture.engine->GetTypeInfoByDecl("array<double>");
REQUIRE(doubles!=nullptr);
zodiac::ScriptArray* values=zodiac::ScriptArray::create(doubles);
REQUIRE(values!=nullptr);
const double nan=std::numeric_limits<double>::quiet_NaN();
for(double value:{2.0,nan,-1.0,nan,0.5})values->insert_last(&value);
auto element=[values](int index){return *static_cast<const double*>(values->at(index));};
values->sort_ascending();
CHECK(element(0)==-1.0);
CHECK(element(1)==0.5);
CHECK(element(2)==2.0);
CHECK(std::isnan(element(3)));
CHECK(std::isnan(element(4)));
values->sort_descending();
CHECK(element(0)==2.0);
CHECK(element(2)==-1.0);
CHECK(std::isnan(element(4)));
CHECK(values->length()==5);
CHECK(values->element_type_id()==asTYPEID_DOUBLE);
CHECK_FALSE(values->holds_strings());
CHECK(values->at(5)==nullptr);
values->release();
zodiac::ScriptArray* words=zodiac::ScriptArray::create(fixture.engine->GetTypeInfoByDecl("array<string>"),2);
REQUIRE(words!=nullptr);
CHECK(words->holds_strings());
*static_cast<zodiac::String*>(words->at(1))="kanka";
CHECK(static_cast<const zodiac::String*>(words->at(1))->bytes()=="kanka");
CHECK(static_cast<const zodiac::String*>(words->at(0))->empty());
words->release();
}

TEST_CASE("handle cycles through arrays are collected and the engine shuts down cleanly")
{
constexpr const char* source=
"class Node{array<Node@> links;~Node(){node_destroyed();}}\n"
"class Owner{array<Owner@>@ list;~Owner(){node_destroyed();}}\n"
"int main(){\n"
"int made=0;\n"
"for(int i=0;i<50;++i){Node a;Node b;a.links.insert_last(b);b.links.insert_last(a);a.links.insert_last(a);made+=2;}\n"
"array<Owner@> list;for(int i=0;i<50;++i){Owner o;@o.list=@list;list.insert_last(o);++made;}\n"
"return made;\n"
"}\n";
g_destroyed=0;
{
ScriptFixture fixture;
REQUIRE(fixture.engine->RegisterGlobalFunction("void node_destroyed()",asFUNCTION(node_destroyed),asCALL_CDECL)>=0);
REQUIRE(fixture.run(source,"int main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDWord()==150);
fixture.release_context();
CHECK(g_destroyed==0);
REQUIRE(fixture.engine->GarbageCollect(asGC_FULL_CYCLE)>=0);
CHECK(g_destroyed==150);
asUINT live=1;
fixture.engine->GetGCStatistics(&live);
CHECK(live==0);
}
CHECK(g_destroyed==150);
g_destroyed=0;
{
ScriptFixture fixture;
REQUIRE(fixture.engine->RegisterGlobalFunction("void node_destroyed()",asFUNCTION(node_destroyed),asCALL_CDECL)>=0);
REQUIRE(fixture.run(source,"int main()")==asEXECUTION_FINISHED);
fixture.release_context();
}
// Left to the engine's shutdown, every node of every cycle is still destroyed.
CHECK(g_destroyed==150);
}
