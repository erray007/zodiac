// The script time library (contract in library.hpp): ticks and ticks_us, timer and date_time. Every function is a noexcept
// native function except date_time's to_string, which can run out of memory and is wrapped in guarded<>; the ones that can
// fail otherwise raise their script exception themselves, out of line.
// - ticks counts from process_start_us, read by a static initializer (this file is linked whenever register_time is), so
//   it starts near 0 when the game starts; it divides unsigned, as the difference is never negative.
// - timer holds one int64: while running the monotonic instant its elapsed time counts from (now - elapsed), while paused
//   the elapsed time itself, both in microseconds; so elapsed is one clock read and a subtraction, and pause, resume and
//   set convert between the two. set saturates at +-2^62 us (about 146 000 years) so no later subtraction can overflow.
// - Both types are POD value types (the engine copies them bytewise and places them on its script stack, possibly at an
//   address that is only 4 mod 8; their int64 fields are read and written with plain moves, which x86-64 and ARM64
//   perform at any address, as String's comment in string.hpp explains).
#include <core/library/date_time.hpp>
#include <core/library/library.hpp>
#include <core/types/script_guard.hpp>
#include <core/types/string.hpp>
#include <platform/clock.hpp>
#include <angelscript.h>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace zodiac {
namespace {
constexpr char kOverflow[]="integer overflow";
constexpr char kInvalidDate[]="invalid date";

const std::int64_t process_start_us=platform::monotonic_us();

std::int64_t ticks() noexcept
{
return static_cast<std::int64_t>(static_cast<std::uint64_t>(platform::monotonic_us()-process_start_us)/1000);
}
std::int64_t ticks_us() noexcept
{
return platform::monotonic_us()-process_start_us;
}

struct Timer {
std::int64_t value_us;
bool running;
};
static_assert(std::is_trivially_copyable_v<Timer>&&std::is_aggregate_v<Timer>);
constexpr std::int64_t kTimerLimitUs=std::int64_t{1}<<62;
constexpr std::int64_t kTimerLimitMs=kTimerLimitUs/1000;

std::int64_t elapsed_us(const Timer& timer) noexcept
{
return timer.running?platform::monotonic_us()-timer.value_us:timer.value_us;
}
void timer_construct(Timer* timer) noexcept
{
new(timer) Timer{platform::monotonic_us(),true};
}
std::int64_t timer_elapsed(const Timer& timer) noexcept
{
return date_time_detail::floor_div(elapsed_us(timer),1000);
}
double timer_elapsed_seconds(const Timer& timer) noexcept
{
return static_cast<double>(elapsed_us(timer))/1e6;
}
bool timer_running(const Timer& timer) noexcept
{
return timer.running;
}
void timer_restart(Timer& timer) noexcept
{
timer.value_us=platform::monotonic_us();
timer.running=true;
}
void timer_pause(Timer& timer) noexcept
{
if(!timer.running)return;
timer.value_us=platform::monotonic_us()-timer.value_us;
timer.running=false;
}
void timer_resume(Timer& timer) noexcept
{
if(timer.running)return;
timer.value_us=platform::monotonic_us()-timer.value_us;
timer.running=true;
}
void timer_set(Timer& timer,std::int64_t ms) noexcept
{
const std::int64_t us=(ms>kTimerLimitMs?kTimerLimitMs:ms<-kTimerLimitMs?-kTimerLimitMs:ms)*1000;
timer.value_us=timer.running?platform::monotonic_us()-us:us;
}

static_assert(std::is_trivially_copyable_v<DateTime>&&std::is_aggregate_v<DateTime>&&sizeof(DateTime)==8);

// The failures raise their exception out of line and are reached by a tail jump, so the arithmetic's hot path sets up no
// stack frame for them (as in math_binding.cpp).
[[gnu::noinline,gnu::cold]] DateTime date_overflow(DateTime value) noexcept
{
raise_script_exception(kOverflow);
return value;
}
[[gnu::noinline,gnu::cold]] std::int64_t difference_overflow() noexcept
{
raise_script_exception(kOverflow);
return 0;
}
[[gnu::noinline,gnu::cold]] void invalid_date(DateTime* date) noexcept
{
raise_script_exception(kInvalidDate);
new(date) DateTime{0};
}

void date_construct(DateTime* date) noexcept
{
new(date) DateTime{0};
}
void date_construct_parts(int year,int month,int day,int hour,int minute,int second,int millisecond,DateTime* date) noexcept
{
const std::optional<DateTime> parts=DateTime::from_parts(year,month,day,hour,minute,second,millisecond);
if(!parts)[[unlikely]]return invalid_date(date);
new(date) DateTime(*parts);
}
DateTime date_now() noexcept
{
return DateTime{platform::utc_to_local_civil_ms(platform::utc_now_ms())};
}
DateTime date_utc_now() noexcept
{
return DateTime{platform::utc_now_ms()};
}
int date_year(const DateTime& date) noexcept
{
return date.year();
}
int date_month(const DateTime& date) noexcept
{
return date.month();
}
int date_day(const DateTime& date) noexcept
{
return date.day();
}
int date_hour(const DateTime& date) noexcept
{
return date.hour();
}
int date_minute(const DateTime& date) noexcept
{
return date.minute();
}
int date_second(const DateTime& date) noexcept
{
return date.second();
}
int date_millisecond(const DateTime& date) noexcept
{
return date.millisecond();
}
int date_weekday(const DateTime& date) noexcept
{
return date.weekday();
}
int date_day_of_year(const DateTime& date) noexcept
{
return date.day_of_year();
}
String date_to_string(const DateTime& date,const String& format)
{
return date.format(format.bytes());
}
DateTime date_add(const DateTime& date,std::int64_t ms) noexcept
{
DateTime result;
if(__builtin_add_overflow(date.civil_ms,ms,&result.civil_ms))[[unlikely]]return date_overflow(date);
return result;
}
DateTime date_subtract(const DateTime& date,std::int64_t ms) noexcept
{
DateTime result;
if(__builtin_sub_overflow(date.civil_ms,ms,&result.civil_ms))[[unlikely]]return date_overflow(date);
return result;
}
DateTime& date_add_assign(DateTime& date,std::int64_t ms) noexcept
{
std::int64_t result;
if(__builtin_add_overflow(date.civil_ms,ms,&result))[[unlikely]]{
date_overflow(date);
return date;
}
date.civil_ms=result;
return date;
}
DateTime& date_subtract_assign(DateTime& date,std::int64_t ms) noexcept
{
std::int64_t result;
if(__builtin_sub_overflow(date.civil_ms,ms,&result))[[unlikely]]{
date_overflow(date);
return date;
}
date.civil_ms=result;
return date;
}
std::int64_t date_difference(const DateTime& date,const DateTime& other) noexcept
{
std::int64_t result;
if(__builtin_sub_overflow(date.civil_ms,other.civil_ms,&result))[[unlikely]]return difference_overflow();
return result;
}
bool date_equals(const DateTime& date,const DateTime& other) noexcept
{
return date.civil_ms==other.civil_ms;
}
int date_compare(const DateTime& date,const DateTime& other) noexcept
{
return (date.civil_ms>other.civil_ms)-(date.civil_ms<other.civil_ms);
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

void register_time(asIScriptEngine* engine)
{
function(engine,"int64 ticks()",asFUNCTION(ticks));
function(engine,"int64 ticks_us()",asFUNCTION(ticks_us));

check(engine->RegisterObjectType("timer",sizeof(Timer),asOBJ_VALUE|asOBJ_POD|asOBJ_APP_CLASS_ALLINTS|asGetTypeTraits<Timer>()),"timer");
check(engine->RegisterObjectBehaviour("timer",asBEHAVE_CONSTRUCT,"void f()",asFUNCTION(timer_construct),asCALL_CDECL_OBJLAST),"timer()");
method(engine,"timer","int64 get_elapsed() const property",asFUNCTION(timer_elapsed));
method(engine,"timer","double get_elapsed_seconds() const property",asFUNCTION(timer_elapsed_seconds));
method(engine,"timer","bool get_running() const property",asFUNCTION(timer_running));
method(engine,"timer","void restart()",asFUNCTION(timer_restart));
method(engine,"timer","void pause()",asFUNCTION(timer_pause));
method(engine,"timer","void resume()",asFUNCTION(timer_resume));
method(engine,"timer","void set(int64 ms)",asFUNCTION(timer_set));

check(engine->RegisterObjectType("date_time",sizeof(DateTime),asOBJ_VALUE|asOBJ_POD|asOBJ_APP_CLASS_ALLINTS|asGetTypeTraits<DateTime>()),"date_time");
check(engine->RegisterObjectBehaviour("date_time",asBEHAVE_CONSTRUCT,"void f()",asFUNCTION(date_construct),asCALL_CDECL_OBJLAST),"date_time()");
check(engine->RegisterObjectBehaviour("date_time",asBEHAVE_CONSTRUCT,"void f(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int millisecond = 0)",asFUNCTION(date_construct_parts),asCALL_CDECL_OBJLAST),"date_time(int year, int month, int day, ...)");
method(engine,"date_time","int year() const",asFUNCTION(date_year));
method(engine,"date_time","int month() const",asFUNCTION(date_month));
method(engine,"date_time","int day() const",asFUNCTION(date_day));
method(engine,"date_time","int hour() const",asFUNCTION(date_hour));
method(engine,"date_time","int minute() const",asFUNCTION(date_minute));
method(engine,"date_time","int second() const",asFUNCTION(date_second));
method(engine,"date_time","int millisecond() const",asFUNCTION(date_millisecond));
method(engine,"date_time","int weekday() const",asFUNCTION(date_weekday));
method(engine,"date_time","int day_of_year() const",asFUNCTION(date_day_of_year));
method(engine,"date_time","string to_string(const string &in format = \"{yyyy}-{MM}-{dd} {HH}:{mm}:{ss}\") const",asFUNCTION(guarded<&date_to_string>::call));
method(engine,"date_time","date_time opAdd(int64 ms) const",asFUNCTION(date_add));
method(engine,"date_time","date_time opSub(int64 ms) const",asFUNCTION(date_subtract));
method(engine,"date_time","date_time &opAddAssign(int64 ms)",asFUNCTION(date_add_assign));
method(engine,"date_time","date_time &opSubAssign(int64 ms)",asFUNCTION(date_subtract_assign));
method(engine,"date_time","int64 opSub(const date_time &in other) const",asFUNCTION(date_difference));
method(engine,"date_time","bool opEquals(const date_time &in other) const",asFUNCTION(date_equals));
method(engine,"date_time","int opCmp(const date_time &in other) const",asFUNCTION(date_compare));
check(engine->SetDefaultNamespace("date_time"),"namespace date_time");
function(engine,"date_time now()",asFUNCTION(date_now));
function(engine,"date_time utc_now()",asFUNCTION(date_utc_now));
check(engine->SetDefaultNamespace(""),"namespace date_time");
}
}
