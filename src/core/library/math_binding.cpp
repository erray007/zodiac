// The script maths library (contract in library.hpp). Every function is a noexcept native asCALL_CDECL function; the two
// that can fail raise their script exception themselves, so none needs the guarded<> wrapper.
// floor, ceil, trunc and round are computed inline (truncation through int64 below 2^52, where every double with a
// fraction lies), so they cost no call into the C library on targets without SSE4.1 rounding; sqrt is one instruction; the
// others are the C library's own functions, registered by address.
#include <core/library/library.hpp>
#include <core/library/round.hpp>
#include <core/types/script_guard.hpp>
#include <angelscript.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <math.h>
#include <numbers>
#include <stdexcept>
#include <string>

namespace zodiac {
namespace {
constexpr double kTwo52=0x1p52;
constexpr char kOverflow[]="integer overflow";
constexpr char kClampOrder[]="clamp: low is greater than high";

// The failures raise their exception out of line and return x, reached by a tail jump: with the call inline, the hot
// path of abs and clamp set up a stack frame (and clamp saved a vector register) on every call. The empty asm hides that
// x comes back, or clang keeps x alive across a call to them instead of jumping.
template<class T>
[[gnu::noinline,gnu::cold]] T overflow_error(T x) noexcept
{
raise_script_exception(kOverflow);
asm volatile("":"+m"(x));
return x;
}
template<class T>
[[gnu::noinline,gnu::cold]] T clamp_order_error(T x) noexcept
{
raise_script_exception(kClampOrder);
asm volatile("":"+m"(x));
return x;
}
template<class T>
T abs_integer(T x) noexcept
{
if(x==std::numeric_limits<T>::min())[[unlikely]]return overflow_error(x);
return x<0?-x:x;
}
float abs_float(float x) noexcept
{
return std::fabs(x);
}
double abs_double(double x) noexcept
{
return std::fabs(x);
}
// std::min, std::max and std::clamp semantics: the first argument wins ties, a NaN bound never throws.
template<class T>
T min_of(T a,T b) noexcept
{
return b<a?b:a;
}
template<class T>
T max_of(T a,T b) noexcept
{
return a<b?b:a;
}
template<class T>
T clamp_of(T x,T low,T high) noexcept
{
if(high<low)[[unlikely]]return clamp_order_error(x);
return x<low?low:high<x?high:x;
}
// x truncated toward zero when |x| < 2^52; at or past it (and for NaN and the infinities) x is already its own floor,
// ceiling and truncation. The results take x's sign, which keeps -0.0 and gives ceil(-0.5) its -0.0.
double floor_of(double x) noexcept
{
if(!(std::fabs(x)<kTwo52))return x;
const double t=static_cast<double>(static_cast<std::int64_t>(x));
return std::copysign(t-(t>x?1.0:0.0),x);
}
double ceil_of(double x) noexcept
{
if(!(std::fabs(x)<kTwo52))return x;
const double t=static_cast<double>(static_cast<std::int64_t>(x));
return std::copysign(t+(t<x?1.0:0.0),x);
}
double trunc_of(double x) noexcept
{
if(!(std::fabs(x)<kTwo52))return x;
return std::copysign(static_cast<double>(static_cast<std::int64_t>(x)),x);
}
// sqrt without the errno a negative argument sets, which scripts cannot see: one instruction where the target has it.
double sqrt_of(double x) noexcept
{
#if defined(__has_builtin)
#if __has_builtin(__builtin_elementwise_sqrt)
return __builtin_elementwise_sqrt(x);
#else
return std::sqrt(x);
#endif
#else
return std::sqrt(x);
#endif
}
// The C library's own double functions, registered by address so a call goes straight to them.
using unary=double(*)(double);
using binary=double(*)(double,double);

// The constants scripts read; the engine only ever reads them through the const declarations.
constinit double pi=std::numbers::pi;
constinit double tau=2*std::numbers::pi;
constinit double e=std::numbers::e;

void check(int result,const char* declaration)
{
if(result<0)throw std::runtime_error(std::string("cannot register \"")+declaration+"\" (angelscript error "+std::to_string(result)+")");
}
void function(asIScriptEngine* engine,const char* declaration,const asSFuncPtr& pointer)
{
check(engine->RegisterGlobalFunction(declaration,pointer,asCALL_CDECL),declaration);
}
void constant(asIScriptEngine* engine,const char* declaration,double* value)
{
check(engine->RegisterGlobalProperty(declaration,value),declaration);
}
}

void register_math(asIScriptEngine* engine)
{
function(engine,"int abs(int)",asFUNCTION(abs_integer<std::int32_t>));
function(engine,"int64 abs(int64)",asFUNCTION(abs_integer<std::int64_t>));
function(engine,"float abs(float)",asFUNCTION(abs_float));
function(engine,"double abs(double)",asFUNCTION(abs_double));
function(engine,"int min(int,int)",asFUNCTION(min_of<std::int32_t>));
function(engine,"int64 min(int64,int64)",asFUNCTION(min_of<std::int64_t>));
function(engine,"float min(float,float)",asFUNCTION(min_of<float>));
function(engine,"double min(double,double)",asFUNCTION(min_of<double>));
function(engine,"int max(int,int)",asFUNCTION(max_of<std::int32_t>));
function(engine,"int64 max(int64,int64)",asFUNCTION(max_of<std::int64_t>));
function(engine,"float max(float,float)",asFUNCTION(max_of<float>));
function(engine,"double max(double,double)",asFUNCTION(max_of<double>));
function(engine,"int clamp(int,int,int)",asFUNCTION(clamp_of<std::int32_t>));
function(engine,"int64 clamp(int64,int64,int64)",asFUNCTION(clamp_of<std::int64_t>));
function(engine,"float clamp(float,float,float)",asFUNCTION(clamp_of<float>));
function(engine,"double clamp(double,double,double)",asFUNCTION(clamp_of<double>));
function(engine,"double floor(double)",asFUNCTION(floor_of));
function(engine,"double ceil(double)",asFUNCTION(ceil_of));
function(engine,"double round(double)",asFUNCTION(round_half_away));
function(engine,"double round(double,int)",asFUNCTION(round_digits));
function(engine,"double trunc(double)",asFUNCTION(trunc_of));
function(engine,"double sqrt(double)",asFUNCTION(sqrt_of));
function(engine,"double pow(double,double)",asFUNCTION(static_cast<binary>(::pow)));
function(engine,"double exp(double)",asFUNCTION(static_cast<unary>(::exp)));
function(engine,"double log(double)",asFUNCTION(static_cast<unary>(::log)));
function(engine,"double log10(double)",asFUNCTION(static_cast<unary>(::log10)));
function(engine,"double sin(double)",asFUNCTION(static_cast<unary>(::sin)));
function(engine,"double cos(double)",asFUNCTION(static_cast<unary>(::cos)));
function(engine,"double tan(double)",asFUNCTION(static_cast<unary>(::tan)));
function(engine,"double asin(double)",asFUNCTION(static_cast<unary>(::asin)));
function(engine,"double acos(double)",asFUNCTION(static_cast<unary>(::acos)));
function(engine,"double atan(double)",asFUNCTION(static_cast<unary>(::atan)));
function(engine,"double atan2(double,double)",asFUNCTION(static_cast<binary>(::atan2)));
function(engine,"double hypot(double,double)",asFUNCTION(static_cast<binary>(::hypot)));
function(engine,"double fmod(double,double)",asFUNCTION(static_cast<binary>(::fmod)));
constant(engine,"const double PI",&pi);
constant(engine,"const double TAU",&tau);
constant(engine,"const double E",&e);
}
}
