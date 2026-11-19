// The script random numbers (contract in library.hpp): the global functions on the calling thread's generator
// (thread_generator), the random_generator value type (a Xoshiro256 in place, copied as its 32 bytes), and
// array<T>::random_choice. Every function is a noexcept native function; only random_choice can fail, and raises its script
// exception itself out of line, so none needs the guarded<> wrapper.
// random_float(min, max) is min + unit() * (max - min), as std::uniform_real_distribution computes it: rounding can give max.
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
return min+thread_generator().unit()*(max-min);
}
bool random_coin() noexcept
{
return thread_generator().coin();
}

void construct_generator(Xoshiro256* memory) noexcept
{
new(memory) Xoshiro256;
memory->seed_from_entropy();
}
void generator_seed(Xoshiro256& generator,std::uint64_t value) noexcept
{
generator.seed(value);
}
std::int32_t generator_int(Xoshiro256& generator,std::int32_t min,std::int32_t max) noexcept
{
return generator.range32(min,max);
}
double generator_unit(Xoshiro256& generator) noexcept
{
return generator.unit();
}
double generator_between(Xoshiro256& generator,double min,double max) noexcept
{
return min+generator.unit()*(max-min);
}
bool generator_coin(Xoshiro256& generator) noexcept
{
return generator.coin();
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
// asOBJ_APP_CLASS_ALLINTS is only valid with asOBJ_APP_CLASS (RegisterObjectType refuses it alone).
check(engine->RegisterObjectType("random_generator",sizeof(Xoshiro256),asOBJ_VALUE|asOBJ_POD|asOBJ_APP_CLASS|asOBJ_APP_CLASS_ALLINTS),"random_generator");
check(engine->RegisterObjectBehaviour("random_generator",asBEHAVE_CONSTRUCT,"void f()",asFUNCTION(construct_generator),asCALL_CDECL_OBJFIRST),"random_generator()");
method(engine,"random_generator","void seed(uint64 value)",asFUNCTION(generator_seed));
method(engine,"random_generator","int random(int min, int max)",asFUNCTION(generator_int));
method(engine,"random_generator","double random_float()",asFUNCTION(generator_unit));
method(engine,"random_generator","double random_float(double min, double max)",asFUNCTION(generator_between));
method(engine,"random_generator","bool random_bool()",asFUNCTION(generator_coin));
method(engine,"array<T>","const T &random_choice() const",asFUNCTION(random_choice));
}
}
