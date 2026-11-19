// The script library of spec 8.5 to 8.7 (core-types design) beyond the types: maths, random numbers and time.
// Contract:
// - register_math registers abs, min, max and clamp for int, int64, float and double; floor, ceil, round(x),
//   round(x, digits), trunc, sqrt, pow, exp, log, log10, sin, cos, tan, asin, acos, atan, atan2, hypot and fmod on double;
//   and the constants PI, TAU and E. Every function is native asCALL_CDECL. Only integer abs (on the smallest value:
//   "integer overflow") and clamp (low greater than high: "clamp: low is greater than high") raise script exceptions.
//   round is half away from zero; round(x, digits) rounds the shortest round-trip decimal form of x (round.hpp).
// - register_random registers int random(int min, int max) (inclusive, bounds swapped when min > max), double random_float()
//   (in [0, 1)), double random_float(double min, double max) (in [min, max), bounds swapped when min > max; equal bounds give
//   min, a NaN bound gives NaN, an infinite bound with unequal bounds gives NaN; no overflow for any finite bounds, a rounding
//   to max gives the double just below it; Xoshiro256::between), bool random_bool(), all on the calling thread's generator
//   (random.hpp), and random_generator with void seed(uint64) and the same four as methods. random_generator is a counted
//   reference type (controller ruling: AngelScript allows &inout only on such types) with value semantics: random_generator g
//   is seeded from entropy, random_generator g(n) by seed(n), and copies (the copy factory, opAssign) copy the state; a
//   failed allocation raises "out of memory". It adds array<T>::random_choice() and random_choice(random_generator
//   &inout) (both const T&, raising "random_choice on an empty array"), so it must come after register_array and before
//   anything makes an array instance (register_string makes array<string>): the engine refuses new template methods then. Native
//   asCALL_CDECL and asCALL_CDECL_OBJFIRST functions; generator: xoshiro256** (random.hpp).
// - register_time registers int64 ticks() and int64 ticks_us() (milliseconds and microseconds since the process started, on
//   platform::monotonic_us, so they never decrease); timer, a POD value type that starts running, with the property getters
//   int64 elapsed (ms, rounded down), double elapsed_seconds and bool running, and restart() (0 and running), pause() and
//   resume() (both idempotent) and set(int64 ms) (sets elapsed, keeps the running state; saturates at +-2^62 us); and
//   date_time, a POD value type holding a DateTime (date_time.hpp): date_time() is 1970-01-01 00:00:00.000,
//   date_time(year, month, day, hour = 0, minute = 0, second = 0, millisecond = 0) raises "invalid date" for parts
//   DateTime::from_parts refuses, date_time::now() (local) and date_time::utc_now() in namespace date_time, the int
//   methods year() month() day() hour() minute() second() millisecond() weekday() (0 = Monday) day_of_year(),
//   to_string(format = "{yyyy}-{MM}-{dd} {HH}:{mm}:{ss}") (DateTime::format), + - += -= with int64 milliseconds,
//   date_time - date_time as int64 milliseconds (results outside int64 raise "integer overflow"), opEquals and opCmp.
//   to_string uses string, so it comes after register_string.
// - Each throws std::runtime_error naming the declaration when the engine refuses one, as register_exception_helpers does.
#pragma once

class asIScriptEngine;

namespace zodiac {
void register_math(asIScriptEngine* engine);
void register_random(asIScriptEngine* engine);
void register_time(asIScriptEngine* engine);
}
