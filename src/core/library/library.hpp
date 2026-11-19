// The script library of spec 8.5 to 8.7 (core-types design) beyond the types: maths, random numbers and time.
// Contract:
// - register_math registers abs, min, max and clamp for int, int64, float and double; floor, ceil, round(x),
//   round(x, digits), trunc, sqrt, pow, exp, log, log10, sin, cos, tan, asin, acos, atan, atan2, hypot and fmod on double;
//   and the constants PI, TAU and E. Every function is native asCALL_CDECL. Only integer abs (on the smallest value:
//   "integer overflow") and clamp (low greater than high: "clamp: low is greater than high") raise script exceptions.
//   round is half away from zero; round(x, digits) rounds the shortest round-trip decimal form of x (round.hpp).
// - register_random registers int random(int min, int max) (inclusive, bounds swapped when min > max), double random_float()
//   (in [0, 1)), double random_float(double min, double max), bool random_bool(), all on the calling thread's generator
//   (random.hpp), and random_generator with void seed(uint64) and the same four as methods. random_generator is a counted
//   reference type (controller ruling: AngelScript allows &inout only on such types) with value semantics: random_generator g
//   is seeded from entropy, random_generator g(n) by seed(n), and copies (the copy factory, opAssign) copy the state; a
//   failed allocation raises "out of memory". It adds array<T>::random_choice() and random_choice(random_generator
//   &inout) (both const T&, raising "random_choice on an empty array"), so it must come after register_array and before
//   anything makes an array instance (register_string makes array<string>): the engine refuses new template methods then. Native
//   asCALL_CDECL and asCALL_CDECL_OBJFIRST functions; generator: xoshiro256** (random.hpp).
// - register_time registers ticks, ticks_us, timer and date_time (Task 3).
// - Each throws std::runtime_error naming the declaration when the engine refuses one, as register_exception_helpers does.
#pragma once

class asIScriptEngine;

namespace zodiac {
void register_math(asIScriptEngine* engine);
void register_random(asIScriptEngine* engine);
void register_time(asIScriptEngine* engine);
}
