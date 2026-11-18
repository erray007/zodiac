// Registers zodiac::String as the script `string` (spec 8.1) and the exception helpers (spec 8.8) with an AngelScript engine.
// Contract:
// - register_string(engine): `string` is a value type of zodiac::String's size with asOBJ_VALUE|asOBJ_APP_CLASS_CDAK (its
//   asGetTypeTraits), bound with native calling conventions. Zodiac's array (register_array) must be registered first:
//   split() returns and join() takes array<string>, and join(separator) is added to array<T> as string join(const T &in)
//   const, which throws "join needs an array of strings" for any other element type (a string separator does not compile
//   for them).
//   Script API (style C; indices and counts are characters): length, byte_length, empty, s[i] (read-only, one-character
//   string), code_at and byte_at (declared const uint &code_at(int) const and const uint8 &byte_at(int) const, so the
//   engine calls them through its fast asBC_Thiscall1 path; the references stay valid and unchanged, scripts see the
//   values), find, rfind, contains, starts_with, ends_with, count, substr, left, right,
//   split(delimiter, keep_empty=true), replace(old, new, all=true), trim, trim_left, trim_right, lower, upper,
//   lower(locale), upper(locale), repeat, reverse, insert, erase(start, count=-1), normalize, to_number, to_int (int64),
//   is_number, is_digits, hash (uint64); explicit constructors string(v) and assignment from every integer type, float,
//   double and bool; + and += with strings, numbers and bools on either side; == != < <= > >= by bytes;
//   global join(array<string>, separator) and format(fmt, ...).
// - Numbers are written by zodiac::String::from_number for their exact type (float as float, integers exactly), bools as
//   "true"/"false".
// - format(fmt, ...) replaces each "{}" with the next argument (strings, numbers, bools, enums as their integer value,
//   null handles as "null", any other object as its type's name); a "{}" without an argument left stays as is; "{{" and
//   "}}" write "{" and "}"; any other brace is literal. Any number of arguments.
// - Errors raise script exceptions with exact texts: "index out of range", "negative count", "\"<text>\" is not a number"
//   (text through one_line, cut to 64 characters followed by "..." when longer), "out of memory" for std::bad_alloc; no
//   C++ exception ever reaches AngelScript.
// - String literals come from one process-wide factory that keeps each distinct constant once, reference counted under a
//   mutex. Constants are immutable and shared by every engine, module and context; their metadata is computed when they
//   are created, so concurrent reads never race to fill it.
// - register_exception_helpers(engine): void throw(const string &in), and string exception_info() with its alias
//   getExceptionInfo() bound to the same function (the active context's exception text, "" when none).
// - Both throw std::runtime_error naming the declaration when the engine refuses a registration (a programming error).
// - is_script_string_type(type) is true exactly for the `string` type register_string registered in type's engine (the
//   engine's string factory is Zodiac's and its type is type), never for another type that happens to be named string.
#pragma once

class asIScriptEngine;
class asITypeInfo;

namespace zodiac {
void register_string(asIScriptEngine* engine);
void register_exception_helpers(asIScriptEngine* engine);
bool is_script_string_type(const asITypeInfo* type) noexcept;
}
