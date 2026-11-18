// Exception boundary for native AngelScript bindings of Zodiac's types: no C++ exception ever unwinds into the engine.
// Contract:
// - raise_script_exception(text) sets a script exception on the active context; without one it does nothing. Never throws.
// - raise_current_exception() must be called inside a catch block; it raises the exception being handled as a script
//   exception: std::bad_alloc as "out of memory", another std::exception as its what() made one-line, anything else as
//   "unknown error". Never throws.
// - guarded<&f>::call has exactly f's signature and is noexcept. It calls f; when f throws it raises the exception as
//   above and returns a value-initialized result, or for an lvalue-reference result its first argument (the object of
//   the method-style functions, which bind with asCALL_CDECL_OBJFIRST). For a noexcept f, call is f itself.
#pragma once
#include <angelscript.h>
#include <core/text.hpp>
#include <exception>
#include <new>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace zodiac {
inline void raise_script_exception(const char* text) noexcept
{
try{
if(asIScriptContext* context=asGetActiveContext())context->SetException(text);
}
catch(...){
}
}
inline void raise_current_exception() noexcept
{
try{
throw;
}
catch(const std::bad_alloc&){
raise_script_exception("out of memory");
}
catch(const std::exception& error){
try{
raise_script_exception(one_line(error.what()).c_str());
}
catch(...){
raise_script_exception("out of memory");
}
}
catch(...){
raise_script_exception("unknown error");
}
}

template<auto Function>
struct guarded;
template<class R,class... A,R (*Function)(A...)>
struct guarded<Function> {
static R call(A... args) noexcept
{
try{
return Function(std::forward<A>(args)...);
}
catch(...){
raise_current_exception();
}
if constexpr(std::is_lvalue_reference_v<R>){
return std::get<0>(std::forward_as_tuple(args...));
}else if constexpr(!std::is_void_v<R>){
return R();
}
}
};
template<class R,class... A,R (*Function)(A...) noexcept>
struct guarded<Function> {
static constexpr R (*call)(A...) noexcept=Function;
};
}
