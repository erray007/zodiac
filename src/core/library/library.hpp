// The script library of spec 8.5 to 8.7 (core-types design) beyond the types: maths, random numbers and time.
// Contract:
// - register_math registers abs, min, max and clamp for int, int64, float and double; floor, ceil, round(x),
//   round(x, digits), trunc, sqrt, pow, exp, log, log10, sin, cos, tan, asin, acos, atan, atan2, hypot and fmod on double;
//   and the constants PI, TAU and E. Every function is native asCALL_CDECL. Only integer abs (on the smallest value:
//   "integer overflow") and clamp (low greater than high: "clamp: low is greater than high") raise script exceptions.
//   round is half away from zero; round(x, digits) rounds the shortest round-trip decimal form of x (round.hpp).
// - register_random registers random, random_float, random_bool, random_choice and random_generator (Task 2).
// - register_time registers ticks, ticks_us, timer and date_time (Task 3).
// - Each throws std::runtime_error naming the declaration when the engine refuses one, as register_exception_helpers does.
#pragma once

class asIScriptEngine;

namespace zodiac {
void register_math(asIScriptEngine* engine);
void register_random(asIScriptEngine* engine);
void register_time(asIScriptEngine* engine);
}
