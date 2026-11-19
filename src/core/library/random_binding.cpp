// The script random numbers (contract in library.hpp): the global functions on the calling thread's generator
// (thread_generator), random_generator (a counted Xoshiro256 on the heap, allocated once per construction; every draw is
// allocation-free), and array<T>::random_choice. Every function is a noexcept native function; only random_choice and the
// factories can fail, and raise their script exception themselves, so none needs the guarded<> wrapper.
#include <core/library/library.hpp>
#include <core/library/random.hpp>
#include <core/types/array.hpp>
#include <core/types/script_guard.hpp>
#include <angelscript.h>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>

namespace zodiac {
namespace {
constexpr char kEmptyChoice[]="random_choice on an empty array";

std::int32_t random_int(std::int32_t min,std::int32_t max) noexcept
{
return thread_generator().range32(min,max);
}
double random_unit() noexcept
{
return thread_generator().unit();
}
double random_between(double min,double max) noexcept
{
return thread_generator().between(min,max);
}
bool random_coin() noexcept
{
return thread_generator().coin();
}

// The script's random_generator: a reference type (so random_choice can take it &inout) with value semantics. The
// generator is the first member, and the count only ever changes on the script's thread.
struct ScriptGenerator {
Xoshiro256 generator;
int references=1;
};
constexpr char kOutOfMemory[]="out of memory";

// The factories' one allocation; nullptr after raising "out of memory".
ScriptGenerator* allocate_generator() noexcept
{
auto* object=new(std::nothrow) ScriptGenerator;
if(!object)[[unlikely]]raise_script_exception(kOutOfMemory);
return object;
}
ScriptGenerator* make_generator() noexcept
{
ScriptGenerator* object=allocate_generator();
if(object)object->generator.seed_from_entropy();
return object;
}
ScriptGenerator* make_seeded_generator(std::uint64_t value) noexcept
{
ScriptGenerator* object=allocate_generator();
if(object)object->generator.seed(value);
return object;
}
ScriptGenerator* copy_generator(const ScriptGenerator& other) noexcept
{
ScriptGenerator* object=allocate_generator();
if(object)object->generator=other.generator;
return object;
}
void generator_add_ref(ScriptGenerator& object) noexcept
{
++object.references;
}
void generator_release(ScriptGenerator& object) noexcept
{
if(--object.references==0)delete &object;
}
ScriptGenerator& generator_assign(ScriptGenerator& object,const ScriptGenerator& other) noexcept
{
object.generator=other.generator;
return object;
}
void generator_seed(ScriptGenerator& object,std::uint64_t value) noexcept
{
object.generator.seed(value);
}
std::int32_t generator_int(ScriptGenerator& object,std::int32_t min,std::int32_t max) noexcept
{
return object.generator.range32(min,max);
}
double generator_unit(ScriptGenerator& object) noexcept
{
return object.generator.unit();
}
double generator_between(ScriptGenerator& object,double min,double max) noexcept
{
return object.generator.between(min,max);
}
bool generator_coin(ScriptGenerator& object) noexcept
{
return object.generator.coin();
}

// Raised out of line and reached by a tail jump, so the hot path of random_choice sets up no stack frame for it.
[[gnu::noinline,gnu::cold]] const void* empty_choice() noexcept
{
raise_script_exception(kEmptyChoice);
return nullptr;
}
// The element at a uniform index; at() returns the object for object elements and the slot for handles and primitives,
// which is what a const T& return hands the engine.
const void* choice_of(const ScriptArray& array,Xoshiro256& generator) noexcept
{
const int count=array.length();
if(count==0)[[unlikely]]return empty_choice();
return array.at(static_cast<int>(generator.below(static_cast<std::uint32_t>(count))));
}
const void* random_choice(const ScriptArray& array) noexcept
{
return choice_of(array,thread_generator());
}
const void* random_choice_with(const ScriptArray& array,ScriptGenerator& object) noexcept
{
return choice_of(array,object.generator);
}

void check(int result,const char* declaration)
{
if(result<0)throw std::runtime_error(std::string("cannot register \"")+declaration+"\" (angelscript error "+std::to_string(result)+")");
}
void function(asIScriptEngine* engine,const char* declaration,const asSFuncPtr& pointer)
{
check(engine->RegisterGlobalFunction(declaration,pointer,asCALL_CDECL),declaration);
}
void method(asIScriptEngine* engine,const char* type,const char* declaration,const asSFuncPtr& pointer)
{
check(engine->RegisterObjectMethod(type,declaration,pointer,asCALL_CDECL_OBJFIRST),declaration);
}
}

void register_random(asIScriptEngine* engine)
{
function(engine,"int random(int min, int max)",asFUNCTION(random_int));
function(engine,"double random_float()",asFUNCTION(random_unit));
function(engine,"double random_float(double min, double max)",asFUNCTION(random_between));
function(engine,"bool random_bool()",asFUNCTION(random_coin));
// A reference type, since AngelScript allows &inout only on counted reference types; it holds no handles, so it is not
// garbage collected. Value semantics: the copy factory and opAssign copy the state.
check(engine->RegisterObjectType("random_generator",0,asOBJ_REF),"random_generator");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_FACTORY,"random_generator@ f()",asFUNCTION(make_generator),asCALL_CDECL),"random_generator()");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_FACTORY,"random_generator@ f(uint64 seed)",asFUNCTION(make_seeded_generator),asCALL_CDECL),"random_generator(uint64 seed)");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_FACTORY,"random_generator@ f(const random_generator &in other)",asFUNCTION(copy_generator),asCALL_CDECL),"random_generator(const random_generator &in)");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_ADDREF,"void f()",asFUNCTION(generator_add_ref),asCALL_CDECL_OBJFIRST),"random_generator addref");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_RELEASE,"void f()",asFUNCTION(generator_release),asCALL_CDECL_OBJFIRST),"random_generator release");
method(engine,"random_generator","random_generator &opAssign(const random_generator &in other)",asFUNCTION(generator_assign));
method(engine,"random_generator","void seed(uint64 value)",asFUNCTION(generator_seed));
method(engine,"random_generator","int random(int min, int max)",asFUNCTION(generator_int));
method(engine,"random_generator","double random_float()",asFUNCTION(generator_unit));
method(engine,"random_generator","double random_float(double min, double max)",asFUNCTION(generator_between));
method(engine,"random_generator","bool random_bool()",asFUNCTION(generator_coin));
method(engine,"array<T>","const T &random_choice() const",asFUNCTION(random_choice));
method(engine,"array<T>","const T &random_choice(random_generator &inout generator) const",asFUNCTION(random_choice_with));
}
}
