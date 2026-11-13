#include "script_fixture.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

using zodiac_test::ScriptFixture;

namespace {
const zodiac_host* g_abi=nullptr;
void greet(zodiac_call* call)
{
size_t length=0;
const char* data=g_abi->arg_string(call,0,&length);
std::string result="merhaba ";
result.append(data,length);
g_abi->ret_string(call,result.data(),result.size());
}
bool g_string_not_copied=false;
void inspect_string(zodiac_call* call)
{
size_t length=0;
const char* data=g_abi->arg_string(call,0,&length);
const std::string* original=static_cast<const std::string*>(reinterpret_cast<asIScriptGeneric*>(call)->GetArgObject(0));
g_string_not_copied=original!=nullptr&&data==original->data()&&length==original->size();
}
int native_add(int a,int b)
{
return a+b;
}
void two_words(zodiac_call* call)
{
zodiac_array* words=g_abi->ret_array(call,"string",2);
REQUIRE(words!=nullptr);
g_abi->array_push_string(words,"kanka",5);
g_abi->array_push_string(words,"kral",4);
}
void three_numbers(zodiac_call* call)
{
zodiac_array* numbers=g_abi->ret_array(call,"int",0);
REQUIRE(numbers!=nullptr);
for(int32_t value=1;value<=3;++value)g_abi->array_push_int32(numbers,value*10);
}
zodiac_array* g_bad_array=reinterpret_cast<zodiac_array*>(1);
void bad_element_type(zodiac_call* call)
{
g_bad_array=g_abi->ret_array(call,"float",1);
}
void sum_doubles(zodiac_call* call)
{
zodiac_array* values=g_abi->arg_array(call,0);
double total=0;
const uint32_t length=g_abi->array_length(values);
for(uint32_t index=0;index<length;++index)total+=g_abi->array_get_double(values,index);
g_abi->ret_double(call,total);
}
void join_strings(zodiac_call* call)
{
zodiac_array* values=g_abi->arg_array(call,0);
std::string joined;
const uint32_t length=g_abi->array_length(values);
for(uint32_t index=0;index<length;++index){
size_t size=0;
const char* data=g_abi->array_get_string(values,index,&size);
joined.append(data,size);
}
g_abi->ret_string(call,joined.data(),joined.size());
}
void mix_numbers(zodiac_call* call)
{
const int32_t small=g_abi->arg_int32(call,0);
const int64_t big=g_abi->arg_int64(call,1);
const float single=g_abi->arg_float(call,2);
const double precise=g_abi->arg_double(call,3);
const int flag=g_abi->arg_bool(call,4);
g_abi->ret_double(call,flag?double(small)+double(big)+double(single)+precise:-1.0);
}
void widen_int8(zodiac_call* call)
{
g_abi->ret_int64(call,int64_t(g_abi->arg_int32(call,0))*2);
}
void fail(zodiac_call* call)
{
g_abi->set_exception(call,"plugin failed");
}
int g_user_value=41;
void read_user_data(zodiac_call* call)
{
g_abi->ret_int32(call,*static_cast<int*>(g_abi->call_user_data(call))+1);
}
struct Counter{
int references=1;
int value=0;
};
int g_live_counters=0;
void counter_add_ref(void* object)
{
++static_cast<Counter*>(object)->references;
}
void counter_release(void* object)
{
Counter* counter=static_cast<Counter*>(object);
if(--counter->references==0){
delete counter;
--g_live_counters;
}
}
void counter_factory(zodiac_call* call)
{
++g_live_counters;
g_abi->ret_object(call,new Counter());
}
void counter_add(zodiac_call* call)
{
Counter* counter=static_cast<Counter*>(g_abi->call_object(call));
counter->value+=g_abi->arg_int32(call,0);
g_abi->ret_int32(call,counter->value);
}
void noop(zodiac_call*)
{
}
}

TEST_CASE("host table carries version and size")
{
ScriptFixture fixture;
g_abi=fixture.abi();
CHECK(g_abi->abi_version==ZODIAC_ABI_VERSION);
CHECK(g_abi->struct_size==sizeof(zodiac_host));
CHECK(fixture.host.current_module()=="test");
}

TEST_CASE("generic function receives string and returns string")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"string greet(const string &in)",greet,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("string main(){return greet(\"kanka\");}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="merhaba kanka");
}

TEST_CASE("arg_string points into the script string without copying")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"void inspect(const string &in)",inspect_string,nullptr)==ZODIAC_OK);
g_string_not_copied=false;
REQUIRE(fixture.run("void main(){string s=\"a string long enough to live on the heap\";inspect(s);}","void main()")==asEXECUTION_FINISHED);
CHECK(g_string_not_copied);
}

TEST_CASE("native fast path rejects non-primitive declarations")
{
ScriptFixture fixture;
g_abi=fixture.abi();
auto fn=reinterpret_cast<void(*)(void)>(&native_add);
CHECK(g_abi->register_native_function(fixture.abi(),"void f(string)",fn)==ZODIAC_E_NOT_PRIMITIVE);
CHECK(g_abi->register_native_function(fixture.abi(),"int f(int &in)",fn)==ZODIAC_E_NOT_PRIMITIVE);
CHECK(g_abi->register_native_function(fixture.abi(),"string f(int)",fn)==ZODIAC_E_NOT_PRIMITIVE);
CHECK(g_abi->register_native_function(fixture.abi(),"int f(int",fn)==ZODIAC_E_DECLARATION);
CHECK(g_abi->register_native_function(fixture.abi(),"int add(int, int)",nullptr)==ZODIAC_E_INVALID_ARGUMENT);
REQUIRE(g_abi->register_native_function(fixture.abi(),"int add(int, int)",fn)==ZODIAC_OK);
REQUIRE(fixture.run("bool main(){return add(2,3)==5;}","bool main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnByte()==1);
CHECK(fixture.logs.size()==5);
CHECK(fixture.logs.front().message=="module test: cannot register \"void f(string)\": only primitive types are allowed on the native fast path");
}

TEST_CASE("primitive arguments and return values convert to the declared types")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"double mix(int, int64, float, double, bool)",mix_numbers,nullptr)==ZODIAC_OK);
REQUIRE(g_abi->register_function(fixture.abi(),"int64 widen(int8)",widen_int8,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("double main(){return mix(1,10000000000,0.5f,0.25,true)+double(widen(-4));}","double main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDouble()==doctest::Approx(10000000001.75-8.0));
}

TEST_CASE("returned string array")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"array<string>@ words()",two_words,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("string main(){array<string>@ w=words();return w.length()+\":\"+w[0]+\",\"+w[1];}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="2:kanka,kral");
}

TEST_CASE("returned int array grows past its reservation")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"array<int>@ numbers()",three_numbers,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("int main(){array<int>@ n=numbers();return int(n.length())*1000+n[0]+n[1]+n[2];}","int main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDWord()==3060);
}

TEST_CASE("ret_array with an unknown element type returns null and logs")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"array<float>@ bad()",bad_element_type,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("bool main(){return bad() is null;}","bool main()")==asEXECUTION_FINISHED);
CHECK(g_bad_array==nullptr);
CHECK(fixture.context->GetReturnByte()==1);
REQUIRE(fixture.logs.size()==1);
CHECK(fixture.logs.front().level==ZODIAC_LOG_ERROR);
}

TEST_CASE("array arguments are readable")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"double sum(const array<double> &in)",sum_doubles,nullptr)==ZODIAC_OK);
REQUIRE(g_abi->register_function(fixture.abi(),"string join(array<string>@)",join_strings,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("string main(){array<double> d={1.5,2.5};array<string> s={\"a\",\"b\",\"c\"};return join(s)+sum(d);}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="abc4");
}

TEST_CASE("set_exception becomes a script exception")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"void fail()",fail,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("void main(){fail();}","void main()")==asEXECUTION_EXCEPTION);
CHECK(fixture.exception_string()=="plugin failed");
}

TEST_CASE("script try/catch catches a plugin exception")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"void fail()",fail,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("string main(){try{fail();}catch{return getExceptionInfo();}return \"not caught\";}","string main()")==asEXECUTION_FINISHED);
CHECK(fixture.returned_string()=="plugin failed");
}

TEST_CASE("user data reaches the generic function")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"int user_value()",read_user_data,&g_user_value)==ZODIAC_OK);
REQUIRE(fixture.run("int main(){return user_value();}","int main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDWord()==42);
}

TEST_CASE("ref type with factory and method")
{
g_live_counters=0;
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_ref_type(fixture.abi(),"counter",counter_add_ref,counter_release)==ZODIAC_OK);
REQUIRE(g_abi->register_factory(fixture.abi(),"counter","counter@ f()",counter_factory,nullptr)==ZODIAC_OK);
REQUIRE(g_abi->register_method(fixture.abi(),"counter","int add(int)",counter_add,nullptr)==ZODIAC_OK);
CHECK(g_abi->register_method(fixture.abi(),"missing","int add(int)",counter_add,nullptr)==ZODIAC_E_UNKNOWN_TYPE);
REQUIRE(fixture.run("int main(){counter c;counter@ other=c;c.add(2);return other.add(3);}","int main()")==asEXECUTION_FINISHED);
CHECK(fixture.context->GetReturnDWord()==5);
fixture.release_context();
CHECK(g_live_counters==0);
}
CHECK(g_live_counters==0);
}

TEST_CASE("duplicate registration")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"int dup()",noop,nullptr)==ZODIAC_OK);
CHECK(g_abi->register_function(fixture.abi(),"int dup()",noop,nullptr)==ZODIAC_E_ALREADY_REGISTERED);
REQUIRE(g_abi->register_ref_type(fixture.abi(),"thing",counter_add_ref,counter_release)==ZODIAC_OK);
CHECK(g_abi->register_ref_type(fixture.abi(),"thing",counter_add_ref,counter_release)==ZODIAC_E_ALREADY_REGISTERED);
REQUIRE(g_abi->register_factory(fixture.abi(),"thing","thing@ f()",counter_factory,nullptr)==ZODIAC_OK);
CHECK(g_abi->register_factory(fixture.abi(),"thing","thing@ f()",counter_factory,nullptr)==ZODIAC_E_ALREADY_REGISTERED);
REQUIRE(g_abi->register_method(fixture.abi(),"thing","int add(int)",counter_add,nullptr)==ZODIAC_OK);
CHECK(g_abi->register_method(fixture.abi(),"thing","int add(int)",counter_add,nullptr)==ZODIAC_E_ALREADY_REGISTERED);
auto fn=reinterpret_cast<void(*)(void)>(&native_add);
CHECK(g_abi->register_native_function(fixture.abi(),"int dup()",fn)==ZODIAC_E_ALREADY_REGISTERED);
REQUIRE(fixture.logs.size()==5);
CHECK(fixture.logs.front().level==ZODIAC_LOG_ERROR);
CHECK(fixture.logs.front().message=="module test: cannot register \"int dup()\": already registered");
// Rejected duplicates must not poison the engine configuration.
CHECK(fixture.run("void main(){}","void main()")==asEXECUTION_FINISHED);
}

TEST_CASE("call-time accessors work from concurrent script contexts")
{
ScriptFixture fixture;
g_abi=fixture.abi();
REQUIRE(g_abi->register_function(fixture.abi(),"string greet(const string &in)",greet,nullptr)==ZODIAC_OK);
REQUIRE(g_abi->register_function(fixture.abi(),"double mix(int, int64, float, double, bool)",mix_numbers,nullptr)==ZODIAC_OK);
REQUIRE(fixture.run("bool main(){for(int i=0;i<200;++i){if(greet(\"kanka\")!=\"merhaba kanka\"||mix(1,2,0.5f,0.25,true)!=3.75)return false;}return true;}","bool main()")==asEXECUTION_FINISHED);
asIScriptFunction* entry=fixture.engine->GetModule("test")->GetFunctionByDecl("bool main()");
std::atomic<int> successes=0;
std::vector<std::thread> threads;
for(int worker=0;worker<4;++worker){
threads.emplace_back([&]{
asIScriptContext* context=fixture.engine->CreateContext();
bool ok=context!=nullptr;
for(int round=0;ok&&round<25;++round)ok=context->Prepare(entry)>=0&&context->Execute()==asEXECUTION_FINISHED&&context->GetReturnByte()==1;
if(context)context->Release();
asThreadCleanup();
if(ok)++successes;
});
}
for(std::thread& thread:threads)thread.join();
CHECK(successes==4);
CHECK(fixture.logs.empty());
}

TEST_CASE("plugin log calls reach the log handler")
{
ScriptFixture fixture;
g_abi=fixture.abi();
g_abi->log(fixture.abi(),ZODIAC_LOG_WARNING,"careful");
REQUIRE(fixture.logs.size()==1);
CHECK(fixture.logs.front().level==ZODIAC_LOG_WARNING);
CHECK(fixture.logs.front().message=="careful");
}

TEST_CASE("invalid arguments are rejected")
{
ScriptFixture fixture;
g_abi=fixture.abi();
CHECK(g_abi->register_function(fixture.abi(),nullptr,noop,nullptr)==ZODIAC_E_INVALID_ARGUMENT);
CHECK(g_abi->register_function(fixture.abi(),"void f()",nullptr,nullptr)==ZODIAC_E_INVALID_ARGUMENT);
CHECK(g_abi->register_function(nullptr,"void f()",noop,nullptr)==ZODIAC_E_INVALID_ARGUMENT);
CHECK(g_abi->register_ref_type(fixture.abi(),"half",counter_add_ref,nullptr)==ZODIAC_E_INVALID_ARGUMENT);
CHECK(g_abi->register_factory(fixture.abi(),"missing","missing@ f()",noop,nullptr)==ZODIAC_E_UNKNOWN_TYPE);
}
