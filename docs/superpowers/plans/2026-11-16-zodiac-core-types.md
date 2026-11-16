# Zodiac Core Types (Plan 4a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace AngelScript's sample `string`, `array` and `dictionary` add-ons with Zodiac's own types — a character-counting UTF-8 `string`, an `array<T>` forked from `scriptarray` with native fast paths, and a hash `dictionary` — with a benchmark gate proving they are not slower.

**Architecture:** New types live in `src/core/types/`. Each type has a pure C++ core (testable without AngelScript) and a binding file that registers it with the engine. `ScriptRuntime` registers the new types instead of the add-ons; the plugin ABI host functions in `src/core/host.cpp` are re-implemented on them. The sample add-ons survive only in a separate library linked by the benchmark target.

**Tech Stack:** C++23, clang (llvm-mingw 20261104 / clang 18 + libc++), AngelScript 2.39.0, doctest, Python (Unicode table generator), CMake + Ninja.

**Spec:** `docs/superpowers/specs/2026-11-16-zodiac-core-types-design.md` (sections 8.1–8.4 and 8.8; 8.5–8.7 are plan 4b).

## Global Constraints

- C++23, clang only. Match existing style exactly: no indentation in C++ and CMake files, English identifiers and comments, a header comment per file describing its contract. Python 4-space indentation.
- Every user-facing message one line through Diagnostics; script errors are catchable AngelScript exceptions with the exact texts below.
- Exact error texts: `index out of range`; `"<text>" is not a number` (`<text>` passed through one_line and truncated to 64 characters with `...`); negative count: `negative count`.
- Character = Unicode code point; invalid UTF-8 bytes count as one character each; no operation ever splits a UTF-8 sequence; lengths, indices and counts are `int`, "not found" is `-1`; starts outside `[0, length()]` throw, counts past the end clamp, negative counts other than the `-1` default throw (spec 8.1).
- Plugin ABI v1 unchanged: `arg_string` returns a NUL-terminated UTF-8 pointer valid until the call returns; `ret_string`, `ret_array("string"|"int"|"double")`, `array_*` keep their behaviour (existing tests must pass unchanged).
- Unicode data: version 16.0.0 of the Unicode Character Database, files pinned by SHA-256 in the generator; generated tables are committed; the generator is re-runnable and deterministic.
- `array` source keeps AngelScript's zlib notice at the top; `THIRD_PARTY_LICENSES` mentions that Zodiac's array is derived from AngelScript's `scriptarray` add-on.
- Benchmark gate: release preset, new vs add-on interleaved in one process, ≥15 rounds, median ratio ≤ 1.05, one re-measure before failing (spec 8.4).
- Linux CI must stay green; everything here is portable.
- Never use shell heredocs or shell redirection to create or edit files (owner's rule).

## Review Focus

1. **Mutation invalidates cached metadata**: `+=`, `insert`, `erase`, `[]` on a copy, assignment from bytes — after each, `length()`, ASCII flag, hash and the sparse index must match a fresh scan. Property test in Task 2.
2. **Invalid UTF-8 at the ends**: a lone lead byte at the end, an overlong sequence, a surrogate code point, a 5-byte lead — counted per spec, never read past the end. Property and fuzz tests in Task 2.
3. **Plugin ABI with the new string**: a plugin returning a string containing NUL bytes and non-ASCII text, and reading one, round-trips exactly. Test in Task 3.
4. **Garbage collector with handle cycles** through arrays and dictionaries (`array<Node@>` where nodes hold the array; dictionary holding itself): collected, no leak, no crash at engine shutdown. Tests in Tasks 4 and 5.
5. **Turkish case on mixed text**: `"İstanbul'da IŞIK".lower("tr")` → `"istanbul'da ışık"`, `.upper("tr")` of `"istanbul ışık"` → `"İSTANBUL IŞIK"`, invariant `lower()` of `"I"` → `"i"`. Test in Task 1/3.

---

### Task 1: Unicode tables

**Files:**
- Create: `tools/gen_unicode.py`, `tools/test_gen_unicode.py`, `src/core/types/unicode.hpp`, `src/core/types/unicode.cpp`, `src/core/types/unicode_tables.cpp` (generated, committed)
- Modify: `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Test: `tests/test_unicode.cpp`

**Interfaces:**
- Produces in `unicode.hpp` (namespace `zodiac::unicode`):
  - `char32_t to_lower(char32_t) noexcept;` `char32_t to_upper(char32_t) noexcept;` — simple (1:1) case mapping, locale-independent.
  - `char32_t to_lower_turkic(char32_t) noexcept;` `char32_t to_upper_turkic(char32_t) noexcept;` — Turkish/Azerbaijani rules for I/İ/ı/i, otherwise the simple mapping.
  - `std::u32string nfc(std::u32string_view) ;` — canonical composition (NFD decomposition, canonical ordering, composition with exclusions).
  - `bool is_whitespace(char32_t) noexcept;` (White_Space property) used by `trim`.
- `gen_unicode.py` downloads `UnicodeData.txt`, `CompositionExclusions.txt`, `PropList.txt` (16.0.0, SHA-256 pinned in the script), writes compact two-level lookup tables to `unicode_tables.cpp`; `python -I tools/gen_unicode.py --check` exits 1 if the committed file differs from a fresh generation (used in CI's bootstrap test step).

- [ ] **Step 1: Write the failing tests** — `to_lower('A')=='a'`, `to_upper(U'ş')==U'Ş'`, `to_lower(U'I')==U'i'`, `to_lower_turkic(U'I')==U'ı'`, `to_lower_turkic(U'İ')==U'i'`, `to_upper_turkic(U'i')==U'İ'`, `to_upper_turkic(U'ı')==U'I'`; `nfc(U"ş")==U"ş"`, `nfc(U"İ")==U"İ"`, already-NFC text unchanged, a Hangul syllable sequence composes; `is_whitespace(U' ')` true. Python test: `--check` passes on the committed file; a corrupted pinned hash makes the generator refuse.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement** the generator and the lookup functions (no allocation in the per-character functions).
- [ ] **Step 4: Run tests to verify they pass** (`ctest --preset windows-debug -R test_unicode`, `python -I -m unittest discover -s tools -p "test_*.py"`); full suite once.
- [ ] **Step 5: Commit** `git commit -m "Add pinned Unicode case and composition tables"`

### Task 2: `zodiac::String` core

**Files:**
- Create: `src/core/types/string.hpp`, `src/core/types/string.cpp`
- Test: `tests/test_string.cpp`

**Interfaces:**
- Produces `class zodiac::String` (value semantics, noexcept moves) with:
  - storage: SSO with at least 22 inline bytes; always NUL-terminated; `const char* data() const noexcept`, `std::string_view bytes() const noexcept`, `int byte_length() const noexcept`.
  - cached metadata computed lazily and invalidated on every mutation: character count, ASCII flag, 64-bit hash (stable across platforms; any fast hash, fixed seed), sparse character→byte index (granularity chosen by the builder).
  - `int length() const;` `bool empty() const noexcept;`
  - every operation of spec 8.1 as a C++ member returning by value (`substr`, `left`, `right`, `find`, `rfind`, `contains`, `starts_with`, `ends_with`, `count`, `split` → `std::vector<String>`, `replace`, `trim*`, `lower`, `upper`, `lower(std::string_view locale)`, `upper(locale)`, `repeat`, `reverse`, `insert`, `erase`, `normalize`, `code_at`, `byte_at`, `char_at` → String, `hash`, `to_number` → `std::expected<double,...>`, `to_int`, `is_number`, `is_digits`), `append`/`operator+=` for String, UTF-8 view, `char32_t`, numbers and bool; comparisons by bytes.
  - range rules from the Global Constraints; errors as `std::expected` or a small `StringError` enum the binding maps to exception texts (no C++ exceptions across the binding).
- Number formatting for `string(double)`: shortest round-trip representation (`std::to_chars`), integers without `.0` (BGT-like), `inf`/`-inf`/`nan`.

- [ ] **Step 1: Write the failing tests** — the spec 9 examples (`"şehir"`: length 5, byte_length 7, `char_at(1)=="e"`), SSO boundary (21/22/23 bytes) without allocation for short strings (count allocations with a test allocator hook or by checking `data()` lies inside the object), every operation's range rules (clamp, throw-equivalent errors, `-1` not found), Review Focus 1 (random mutation sequences compared against a reference `std::u32string` model, 10 000 iterations, fixed seed), Review Focus 2 (all byte strings up to length 4, and 100 000 random byte strings: `length()` equals the reference count, concatenating all `char_at(i)` reproduces the bytes), Review Focus 5 (on the C++ API).
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run tests to verify they pass**; full suite once.
- [ ] **Step 5: Commit** `git commit -m "Add Zodiac's UTF-8 string core with cached character metadata"`

### Task 3: Register `string` and replace the string add-ons

**Files:**
- Create: `src/core/types/string_binding.hpp`, `src/core/types/string_binding.cpp`, `src/core/types/exception_binding.cpp`
- Modify: `src/core/script_runtime.cpp`, `src/core/host.cpp`, `src/core/CMakeLists.txt`, `cmake/Dependencies.cmake` (string add-ons no longer compiled into `angelscript`; see Task 6 for the bench library)
- Test: `tests/test_string_binding.cpp`, existing `tests/test_host.cpp`, `tests/test_plugin_hpp.cpp`, `tests/test_script_runtime.cpp`, all e2e

**Interfaces:**
- `void zodiac::register_string(asIScriptEngine*);` registers `string` as a value type (`asOBJ_VALUE|asOBJ_APP_CLASS_CDAK` with the right size/alignment flags), the string factory (literals cached as constants), constructors from every numeric type and bool, every method of spec 8.1 with style-C names, operators `+ += == != < <= > >= []` (index read-only), `+`/`+=` with numbers and bools both ways (`"a"+5`, `5+"a"`), `format(const string &in, ?&in, ...)` with `{}` placeholders (up to 16 arguments; extra `{}` without argument stay literal; `{{`/`}}` escape), `array<string>@ split(...)` and `string join(const array<string>&in, const string &in)` (registered after the array type exists — Task 4 adds the array registration; until then the existing add-on array is used, see the ruling in the dispatch).
- `void zodiac::register_exception_helpers(asIScriptEngine*);` — `void throw(const string &in)`, `string exception_info()`, and the alias `string getExceptionInfo()` bound to the same function (spec 8.8), replacing `RegisterExceptionRoutines`.
- `host.cpp`: the "string" type check, `arg_string`, `ret_string`, generic thunk string results, and string arrays use `zodiac::String`.

- [ ] **Step 1: Write the failing tests** — script-level tests through `ScriptRuntime` (or the existing script fixture helper) for each method group, the range errors as exact exception messages, `"a"+5+"b"`, `format("{} ve {}", 1, "iki")=="1 ve iki"`, Review Focus 3 (plugin string round trip with NUL and Turkish), Review Focus 5 (script level), `getExceptionInfo()` and `exception_info()` both return the message.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**; remove `RegisterStdString`, `RegisterStdStringUtils`, `RegisterExceptionRoutines` from `ScriptRuntime`.
- [ ] **Step 4: Run tests to verify they pass**, full suite once (all existing fixtures and e2e unchanged); push; CI green.
- [ ] **Step 5: Commit** `git commit -m "Register Zodiac's string and exception helpers instead of the add-ons"`

### Task 4: `array<T>` fork with native fast paths

**Files:**
- Create: `src/core/types/array.hpp`, `src/core/types/array.cpp` (forked from `scriptarray`, notice kept), `src/core/types/array_fast_paths.cpp`
- Modify: `src/core/script_runtime.cpp`, `src/core/host.cpp`, `src/core/types/string_binding.cpp` (split/join on the new array), `THIRD_PARTY_LICENSES`
- Test: `tests/test_array.cpp`

**Interfaces:**
- `class zodiac::ScriptArray` (renamed fork) and `void zodiac::register_array(asIScriptEngine*);` with the style-C API of spec 8.2 only (the add-on's camelCase names are not registered), initializer lists, bounds errors with the exact texts, GC behaviour of the add-on unchanged.
- Fast paths: when the subtype is `bool`, `int8`…`uint64`, `float`, `double` or `string`, `sort()`, `sort_descending()`, `find`, `contains`, `opEquals` use direct C++ comparisons (std::sort on the element memory / String comparison) without engine callbacks; `sort(compare_function)` keeps the callback path.
- `host.cpp` `ret_array`/`array_*` operate on `zodiac::ScriptArray`.

- [ ] **Step 1: Write the failing tests** — every method with int, double, string and a script class element type; initializer list; range errors; fast-path results equal the callback path on 1 000 random arrays (fixed seed); Review Focus 4 (handle cycle through an array, collected, engine shuts down cleanly — count live objects with a script class destructor counter).
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**; stop registering the add-on array.
- [ ] **Step 4: Run tests to verify they pass**, full suite once; push; CI green.
- [ ] **Step 5: Commit** `git commit -m "Fork the array add-on into Zodiac's array with native fast paths"`

### Task 5: Hash `dictionary`

**Files:**
- Create: `src/core/types/dictionary.hpp`, `src/core/types/dictionary.cpp`
- Modify: `src/core/script_runtime.cpp`
- Test: `tests/test_dictionary.cpp`

**Interfaces:**
- `void zodiac::register_dictionary(asIScriptEngine*);` registering `dictionary` and `dictionary_value` (the proxy) with spec 8.3's API; insertion-ordered open-addressing table keyed by `zodiac::String` using its cached hash; values stored like the add-on (`int64`/`double` inline, objects by reference with type id), GC-registered (`asOBJ_GC`, enum references, release all handles).
- `serialize()` format: first line `zodiac-dictionary 1`, then one entry per line with a type tag and escaped key and value; nested dictionaries and arrays of primitives/strings/dictionaries supported; any other value throws `cannot serialize "<key>": unsupported type <type>`; `deserialize` returns false (and leaves the dictionary unchanged) on any malformed input.
- Dictionary literal syntax `dictionary d = {{"a", 1}, {"b", "x"}};` (list factory like the add-on).

- [ ] **Step 1: Write the failing tests** — set/get/exists/remove/keys order after removals and re-inserts, `d["k"]` conversions, 100 000 keys insert+lookup, serialize→deserialize round trip incl. Turkish keys and values with newlines/quotes, malformed input leaves the dictionary untouched, unsupported type message, Review Focus 4 (dictionary holding itself and a cycle through a script class).
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**; stop registering the add-on dictionary.
- [ ] **Step 4: Run tests to verify they pass**, full suite once; push; CI green.
- [ ] **Step 5: Commit** `git commit -m "Add Zodiac's insertion-ordered hash dictionary"`

### Task 6: Benchmark gate

**Files:**
- Create: `tests/bench/CMakeLists.txt`, `tests/bench/bench_types.cpp`, `tools/check_bench.py`
- Modify: `cmake/Dependencies.cmake` (library `angelscript_sample_addons` with `scriptstdstring*`, `scriptarray`, `scriptdictionary`, linked only by the bench), `tests/CMakeLists.txt`, `.github/workflows/ci.yml`
- Test: `tools/test_check_bench.py`

**Interfaces:**
- `bench_types` executable: two engines in one process (one with Zodiac's types, one with the sample add-ons), the operation list of spec 8.4 each as an AngelScript snippet pair (same semantics, add-on side written the way a script would have to), interleaved ≥15 rounds, prints one JSON line per operation `{"op":..., "new_ns":..., "addon_ns":..., "ratio":...}` (medians).
- `tools/check_bench.py <json>`: exit 1 listing every op with ratio > 1.05; CI (windows job on the release preset, and linux job) runs bench, on failure re-runs bench once and checks again.
- No game links `angelscript_sample_addons`; a test asserts the kit's `lib/release/libangelscript.a` contains no `RegisterScriptArray`/`RegisterStdString` symbols.

- [ ] **Step 1: Write the failing tests** — `test_check_bench.py`: ratios above 1.05 fail with the op names, all below pass, malformed JSON fails.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Implement**; run the bench locally on `windows-release` and record the table in the report.
- [ ] **Step 4: Verify**: local bench passes the gate; push; CI green including the bench steps. If any operation cannot meet the gate after optimization, STOP and report (the controller records a ruling and tells the owner — spec 8.4).
- [ ] **Step 5: Commit** `git commit -m "Gate Zodiac's types against the sample add-ons with a benchmark"`
