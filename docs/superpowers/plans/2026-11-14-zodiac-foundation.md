# Zodiac Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A contributor clones the repo, runs `python bootstrap.py` and `cmake --workflow --preset windows`, and gets a `zodiac` CLI that runs an AngelScript project in dev mode, loading only the modules its `zodiac.toml` declares, through the stable C plugin ABI.

**Architecture:** `zodiac.exe` links a static `zodiac_core` library (AngelScript engine, host ABI implementation, module registry, project file parser, script runtime, diagnostics). Modules and third-party plugins are shared libraries that export one C entry point returning a `zodiac_module_info`; they see only the C function table in `include/zodiac/plugin.h`. A header-only C++ layer (`plugin.hpp`) compiles down to that C table.

**Tech Stack:** C++23, C11 (ABI header), CMake ≥ 3.28 + Ninja, clang (llvm-mingw UCRT on Windows, system clang ≥ 17 on Linux), AngelScript 2.3x + add-ons (zlib), toml++ (MIT), doctest (MIT), Python ≥ 3.9 stdlib only for bootstrap.

**Spec:** `docs/superpowers/specs/2026-11-14-bgdk-design.md`

**Plan series** (each its own plan, written when the previous one lands):
1. Foundation (this plan)
2. Script API proposal: a document for owner approval, including the game-loop model (spec §5). No module code before it is approved.
3. Release builds: static module registration via linker section, bundled `lld`, bytecode/asset embedding, CI size gate (< 3 MB hello world)
4. Core platform: window, keyboard, timers, file I/O
5. `audio` + `screen_reader`
6. Windows host → Linux target
7. `network`, `http`, `crypto`, `compression`, `db`, script/asset protection
8. `map` (port of `map_system`) and `ffi`
9. `bgt_compat` + Blood Bath port
10. Linux host kit

## Global Constraints

- All code, comments, identifiers, docs and commit messages in English.
- C, C++ and CMake sources use **no indentation** (owner rule; see `D:\nvgt\plugin\map_system\map_system.cpp` for the style). Python and YAML keep the indentation their syntax requires.
- C++23; the ABI header `include/zodiac/plugin.h` must compile as C11 and as C++23.
- Only C types cross the plugin boundary: no AngelScript classes, no `std::` types, no exceptions (spec §8.2).
- Plugin ABI is append-only; a module declares the minimum host ABI version it needs and the host refuses newer ones (spec §8.2).
- Windows 10+ x86_64 and Linux x86_64 only; no 32-bit (spec §5).
- clang only. Windows target is llvm-mingw UCRT; executables and modules link libc++ statically (`-static`), which is safe because only C crosses module boundaries.
- Dependencies: zlib/MIT/BSD/Apache only, pinned by version and SHA-256 via `FetchContent`; no GPL, no vcpkg (spec §5, §8.8).
- Kit license: zlib (`LICENSE` at repo root).
- Every user-facing message is a single plain-text line usable with a screen reader; no ASCII art, no color-only information (spec G8).
- Code must be optimized: no needless copies or allocations on hot paths (owner rule).
- Platform-specific code only in `src/platform/<os>/` (spec §8.9 rule 1). Core is a library with no `main`, no `exit()`, and no direct stdout/stderr writes: all output goes through `zodiac::Diagnostics` sinks (spec §8.9 rules 2–3, §8.10).
- No x86-only code (spec §8.9 rule 6).
- Plugin ABI thread rules from v1: registration only on the main thread inside `register_module`; registered functions may be called from any script thread (spec §8.9).

## Review Focus

1. **Non-ASCII and spaced paths** (e.g. `D:\Oyunlar\Kanka şövalye\`, like the owner's `Blood bath source`): scripts, includes, project files and modules must load. Covered in Task 7 and Task 9.
2. **A module DLL that is missing, corrupt, lacks the entry symbol, or needs a newer ABI:** clear one-line error naming the module, exit code 2, never a crash and never a Windows modal error dialog (dialogs are hard to handle with a screen reader). Covered in Task 5.
3. **Script compile errors:** every error as `path:line:col: error: message` with the path relative to the project, exit code 1. Covered in Task 7.
4. **Uncaught script exceptions and plugin-side C++ exceptions:** reported as `path:line: runtime error: message` plus call stack, exit code 1, process does not crash. Covered in Task 4 and Task 7.
5. **Bad project files:** unknown module, dependency cycle, module listed as both static and dynamic, unknown `[core] exclude` entry: one-line error naming the offending entry, exit code 2. Covered in Task 6.

Exit codes used throughout: `0` success, `1` script error (compile or runtime), `2` usage, project or module error.

## File Structure

```
.gitignore                     ignores .tools/, build/, Blood bath source/
LICENSE                        zlib
README.md                      what Zodiac is, quick start
CONTRIBUTING.md                setup, build, test, PR rules, code style
bootstrap.py                   downloads pinned tools into .tools/
tools/toolchain.json           pinned tool URLs + SHA-256 per host
tools/test_bootstrap.py        unittest for bootstrap.py
CMakeLists.txt                 root project
CMakePresets.json              windows-debug/-release, linux-debug/-release, workflows
cmake/toolchain-llvm-mingw.cmake
cmake/Dependencies.cmake       FetchContent: angelscript, tomlplusplus, doctest
cmake/ZodiacModule.cmake       zodiac_add_module()
include/zodiac/plugin.h        C ABI (the public contract)
include/zodiac/plugin.hpp      header-only C++ convenience layer
src/core/host.hpp/.cpp         zodiac_host implementation over AngelScript
src/core/dynamic_library.hpp       platform-neutral interface
src/platform/windows/dynamic_library.cpp   LoadLibraryExW
src/platform/posix/dynamic_library.cpp     dlopen
src/platform/windows/console.cpp   UTF-8 console setup, SetErrorMode
src/platform/posix/console.cpp
src/core/module_registry.hpp/.cpp   discovery, ABI check, dependency order
src/core/project_file.hpp/.cpp      zodiac.toml parsing + validation
src/core/script_runtime.hpp/.cpp    build, run main(), exceptions
src/core/diagnostics.hpp/.cpp       message formatting, console + file sinks
src/cli/main.cpp               zodiac run / zodiac modules / zodiac new
modules/example/example.cpp    test-only module exercising the ABI (not shipped)
tests/                         doctest unit + end-to-end tests, fixtures/
.github/workflows/ci.yml
```

Kit layout produced by the build (dev mode looks modules up here): `build/<preset>/bin/zodiac(.exe)`, `build/<preset>/modules/zodiac_mod_<name>.(dll|so)`. Third-party plugins are also searched in `<project>/plugins/`.

---

### Task 1: Repository and one-command bootstrap

**Files:**
- Create: `.gitignore`, `LICENSE`, `README.md`, `bootstrap.py`, `tools/toolchain.json`
- Test: `tools/test_bootstrap.py`

**Interfaces:**
- Produces: tools extracted to `.tools/cmake/`, `.tools/ninja/`, `.tools/llvm-mingw/` (Windows only), each with a `.stamp` file containing the pinned version. Later tasks reference `${sourceDir}/.tools/...` from presets.
- `bootstrap.py` functions: `load_manifest(path: Path) -> dict`, `download(url: str, dest: Path, sha256: str) -> None` (raises `ChecksumError`, deletes the partial file), `safe_extract(archive: Path, dest: Path, strip_prefix: str) -> None` (raises `UnsafeArchiveError` on absolute paths or `..` members), `ensure_tool(name: str, spec: dict, root: Path) -> bool` (returns `False` when the stamp already matches and nothing was done).

- [ ] **Step 1:** `git init` in `D:\bgdk`, create `.gitignore` with `.tools/`, `build/`, `Blood bath source/`, `*.log`. Add `LICENSE` with the zlib license text, copyright "Zodiac contributors". Check `git status` shows no file from `Blood bath source/`.
- [ ] **Step 2: Write failing tests** in `tools/test_bootstrap.py` (unittest, stdlib only):
  - `test_download_rejects_bad_checksum`: serve a file via `file://` URL, wrong sha → `ChecksumError`, destination does not exist afterwards.
  - `test_safe_extract_rejects_parent_paths`: zip with member `../evil.txt` → `UnsafeArchiveError`, nothing written outside `dest`.
  - `test_safe_extract_strips_prefix`: zip with `cmake-3.x/bin/cmake.exe` and `strip_prefix="cmake-3.x"` → `dest/bin/cmake.exe` exists.
  - `test_ensure_tool_is_idempotent`: second call returns `False` and does not download (patch `download`).
- [ ] **Step 3:** Run `python -m unittest tools/test_bootstrap.py -v`. Expected: FAIL (module/functions missing).
- [ ] **Step 4:** Implement `bootstrap.py`. `tools/toolchain.json` pins, per host (`windows-x86_64`, `linux-x86_64`): CMake (latest 3.x or 4.x release ≥ 3.28), Ninja (latest), llvm-mingw latest release `ucrt-x86_64` (Windows host only), each `{version, url, sha256, strip_prefix}`. On Linux, bootstrap checks `clang --version` ≥ 17 and prints the distro install hint if missing. On success prints exactly one next-step line: `Setup complete. Next: .tools/cmake/bin/cmake --workflow --preset windows` (or `linux`).
- [ ] **Step 5:** Run the unit tests. Expected: PASS. Then run `python bootstrap.py` for real; expected: the next-step line, and a second run finishes without downloading.
- [ ] **Step 6:** Commit: `git add .gitignore LICENSE README.md bootstrap.py tools/` and `git commit -m "build: add one-command bootstrap for pinned toolchain"`.

### Task 2: CMake skeleton, presets and dependencies

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `cmake/toolchain-llvm-mingw.cmake`, `cmake/Dependencies.cmake`, `tests/CMakeLists.txt`, `tests/test_angelscript_smoke.cpp`

**Interfaces:**
- Produces CMake targets: `angelscript` (static, with add-ons `scriptstdstring`, `scriptarray`, `scriptdictionary`, `scriptbuilder`, `scripthelper` compiled in), `tomlplusplus::tomlplusplus`, `doctest::doctest`. Function `zodiac_add_test(name SOURCES ...)` registering a doctest executable with CTest.
- Presets: configure `windows-debug`, `windows-release`, `linux-debug`, `linux-release` (binary dir `build/<preset>`, generator Ninja from `.tools/ninja`); matching build and test presets; workflow presets `windows` and `linux` (debug: configure → build → test).

- [ ] **Step 1: Write failing test** `tests/test_angelscript_smoke.cpp`: `TEST_CASE("angelscript runs a script")` creates an engine, registers std string, builds module from `int f(){string s="ş"; return int(s.length());}`, executes `f`, `CHECK(result == 2)` (UTF-8 bytes).
- [ ] **Step 2:** Run `.tools/cmake/bin/cmake --workflow --preset windows`. Expected: FAIL (no project yet).
- [ ] **Step 3:** Write the CMake files. Toolchain file points `CMAKE_C_COMPILER`/`CMAKE_CXX_COMPILER` at `${CMAKE_SOURCE_DIR}/.tools/llvm-mingw/bin/x86_64-w64-mingw32-clang(++)`, `CMAKE_RC_COMPILER` at its `windres`, adds `-static` to exe and shared linker flags. `Dependencies.cmake` uses `FetchContent_Declare(... URL ... URL_HASH SHA256=...)` for AngelScript (latest stable 2.3x SDK zip), toml++ (latest 3.x), doctest (latest 2.4.x). Warnings: `-Wall -Wextra` on our targets only. AngelScript is built with thread support (no `AS_NO_THREADS`); the runtime calls `asPrepareMultithread()` before creating the engine.
- [ ] **Step 4:** Run the workflow again. Expected: configure, build and `100% tests passed`.
- [ ] **Step 5:** Commit: `git commit -m "build: add CMake presets, llvm-mingw toolchain and pinned dependencies"`.

### Task 3: Plugin ABI header

**Files:**
- Create: `include/zodiac/plugin.h`, `tests/test_plugin_header_c.c`, `tests/test_plugin_header.cpp`

**Interfaces:**
- Produces (exact; every later task and every plugin uses these):

```c
#define ZODIAC_ABI_VERSION 1u
#define ZODIAC_OK 0
#define ZODIAC_E_INVALID_ARGUMENT -1
#define ZODIAC_E_DECLARATION -2
#define ZODIAC_E_NOT_PRIMITIVE -3
#define ZODIAC_E_ALREADY_REGISTERED -4
#define ZODIAC_E_UNKNOWN_TYPE -5
typedef struct zodiac_host zodiac_host;
typedef struct zodiac_call zodiac_call;
typedef struct zodiac_array zodiac_array;
typedef void (*zodiac_generic_fn)(zodiac_call* call);
typedef enum zodiac_log_level{ZODIAC_LOG_INFO=0,ZODIAC_LOG_WARNING=1,ZODIAC_LOG_ERROR=2}zodiac_log_level;
typedef struct zodiac_module_info{
uint32_t abi_version;          /* minimum host ABI this module needs */
const char* name;              /* "example"; [a-z0-9_]+ */
const char* version;           /* semver */
const char* license;           /* SPDX id, e.g. "Zlib" */
const char* const* dependencies;
uint32_t dependency_count;
int (*register_module)(zodiac_host* host);   /* ZODIAC_OK or negative code */
void (*shutdown_module)(void);               /* may be NULL */
}zodiac_module_info;
struct zodiac_host{
uint32_t abi_version;
uint32_t struct_size;          /* sizeof(zodiac_host) of the host; check before using appended members */
int (*register_function)(zodiac_host*,const char* declaration,zodiac_generic_fn fn,void* user_data);
int (*register_native_function)(zodiac_host*,const char* declaration,void (*fn)(void)); /* primitive params/return only; C calling convention */
int (*register_ref_type)(zodiac_host*,const char* name,void (*add_ref)(void*),void (*release)(void*));
int (*register_method)(zodiac_host*,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data);
int (*register_factory)(zodiac_host*,const char* type_name,const char* declaration,zodiac_generic_fn fn,void* user_data);
void* (*call_user_data)(zodiac_call*);
void* (*call_object)(zodiac_call*);
int32_t (*arg_int32)(zodiac_call*,uint32_t index);
int64_t (*arg_int64)(zodiac_call*,uint32_t index);
float (*arg_float)(zodiac_call*,uint32_t index);
double (*arg_double)(zodiac_call*,uint32_t index);
int (*arg_bool)(zodiac_call*,uint32_t index);
const char* (*arg_string)(zodiac_call*,uint32_t index,size_t* length); /* UTF-8, valid until the call returns */
void* (*arg_object)(zodiac_call*,uint32_t index);
zodiac_array* (*arg_array)(zodiac_call*,uint32_t index);
void (*ret_int32)(zodiac_call*,int32_t);
void (*ret_int64)(zodiac_call*,int64_t);
void (*ret_float)(zodiac_call*,float);
void (*ret_double)(zodiac_call*,double);
void (*ret_bool)(zodiac_call*,int);
void (*ret_string)(zodiac_call*,const char* data,size_t length);
void (*ret_object)(zodiac_call*,void* object); /* host takes one reference */
zodiac_array* (*ret_array)(zodiac_call*,const char* element_type,uint32_t reserve); /* "string", "int", "double" */
uint32_t (*array_length)(zodiac_array*);
const char* (*array_get_string)(zodiac_array*,uint32_t index,size_t* length);
int32_t (*array_get_int32)(zodiac_array*,uint32_t index);
double (*array_get_double)(zodiac_array*,uint32_t index);
void (*array_push_string)(zodiac_array*,const char* data,size_t length);
void (*array_push_int32)(zodiac_array*,int32_t);
void (*array_push_double)(zodiac_array*,double);
void (*set_exception)(zodiac_call*,const char* message);
void (*log)(zodiac_host*,zodiac_log_level level,const char* message);
};
#define ZODIAC_MODULE_ENTRY_NAME "zodiac_module_get_info"
/* ZODIAC_EXPORT expands to __declspec(dllexport) on Windows, visibility("default") elsewhere */
/* ZODIAC_DEFINE_MODULE(info_variable) defines the exported entry returning &info_variable (shared builds) */
```

- [ ] **Step 1: Write failing tests.** `test_plugin_header_c.c` (compiled as C11) includes the header and declares a `zodiac_module_info` with designated initializers; `test_plugin_header.cpp` `TEST_CASE("abi layout is stable")`: `static_assert(offsetof(zodiac_host, abi_version)==0)`, `static_assert(offsetof(zodiac_host, struct_size)==4)`, `CHECK(ZODIAC_ABI_VERSION==1)`, and `CHECK(std::string(ZODIAC_MODULE_ENTRY_NAME)=="zodiac_module_get_info")`.
- [ ] **Step 2:** Build. Expected: FAIL (header missing).
- [ ] **Step 3:** Write `include/zodiac/plugin.h` with exactly the members above in that order, include guards, `extern "C"` guards, `<stdint.h>`/`<stddef.h>` only. A header comment states the ABI rules: append-only, C types only, and the thread rules from Global Constraints.
- [ ] **Step 4:** Build and test. Expected: PASS.
- [ ] **Step 5:** Commit: `git commit -m "feat(abi): add stable C plugin ABI v1"`.

### Task 4: Host implementation and C++ convenience layer

**Files:**
- Create: `src/core/host.hpp`, `src/core/host.cpp`, `include/zodiac/plugin.hpp`, `src/core/CMakeLists.txt`
- Test: `tests/test_host.cpp`, `tests/test_plugin_hpp.cpp`

**Interfaces:**
- Consumes: `plugin.h` (Task 3), `angelscript` target (Task 2).
- Produces: `class zodiac::Host { public: explicit Host(asIScriptEngine* engine); zodiac_host* abi(); const std::string& current_module() const; void set_current_module(std::string_view name); };` Registration errors are logged as `module <name>: cannot register "<declaration>": <reason>`.
- Produces in `plugin.hpp`: `namespace zodiac::plugin { template<auto Fn> int function(zodiac_host* host, const char* declaration); }` which deduces `Fn`'s C++ signature (supported: `int32_t`, `int64_t`, `float`, `double`, `bool`, `std::string_view` in, `std::string` out, `std::vector<std::string>` out, `void`), generates a `zodiac_generic_fn` wrapper and registers it. Any C++ exception thrown by `Fn` is caught in the wrapper and turned into `set_exception(call, e.what())`.

- [ ] **Step 1: Write failing tests** in `tests/test_host.cpp` (each builds a tiny script against a fresh engine + `Host`):
  - `"generic function receives string and returns string"`: register `string greet(const string &in)` returning `"merhaba " + arg`; script `greet("kanka")` → `"merhaba kanka"`.
  - `"native fast path rejects non-primitive declarations"`: `register_native_function(host, "void f(string)", ...)` → `ZODIAC_E_NOT_PRIMITIVE`; `"int add(int, int)"` → `ZODIAC_OK` and script `add(2,3)==5`.
  - `"returned string array"`: `ret_array(call,"string",2)` + two pushes → script sees `array<string>` of length 2.
  - `"set_exception becomes a script exception"`: script catches nothing → execution result is `asEXECUTION_EXCEPTION` and the exception string equals the message.
  - `"script try/catch catches a plugin exception"`: `try { fail(); } catch { return getExceptionInfo(); }` returns the message passed to `set_exception`, and execution finishes normally.
  - `"ref type with factory and method"`: counter type with `add_ref`/`release`; after script ends and context is released, `release` brought the count to 0.
  - `"duplicate registration"` → `ZODIAC_E_ALREADY_REGISTERED`.
- [ ] **Step 2: Write failing test** `tests/test_plugin_hpp.cpp`: `"function<> wraps a throwing C++ function"`: `int boom(int)` throws `std::runtime_error("patladı")` → script exception message `"patladı"`; `"function<> wraps vector<string> return"`.
- [ ] **Step 3:** Run tests. Expected: FAIL.
- [ ] **Step 4:** Implement `Host` with `asCALL_GENERIC` thunks; a `zodiac_call` is a thin cast of `asIScriptGeneric*` plus the user data pointer (no allocation per call). `register_native_function` tokenizes the declaration and accepts only `void bool int8 int16 int int32 int64 uint8 uint16 uint uint32 uint64 float double` before registering with `asCALL_CDECL`. Implement `plugin.hpp` with templates (no virtual dispatch, no heap allocation for primitive arguments).
- [ ] **Step 5:** Run tests. Expected: PASS.
- [ ] **Step 6:** Commit: `git commit -m "feat(core): implement host ABI over AngelScript with C++ helper layer"`.

### Task 5: Dynamic library loading and module registry

**Files:**
- Create: `src/core/dynamic_library.hpp`, `src/platform/windows/dynamic_library.cpp`, `src/platform/posix/dynamic_library.cpp`, `src/platform/windows/console.cpp`, `src/platform/posix/console.cpp`, `src/core/module_registry.hpp/.cpp`, `cmake/ZodiacModule.cmake`, `modules/example/example.cpp`, `modules/CMakeLists.txt`
- Test: `tests/test_module_registry.cpp`, fixtures built by CMake: `zodiac_mod_example`, `zodiac_mod_needs_abi_99` (declares `abi_version = 99`), `zodiac_mod_no_entry` (shared library without the entry symbol), `tests/fixtures/corrupt/zodiac_mod_corrupt.dll|.so` (a text file)

**Interfaces:**
- Consumes: `Host` (Task 4).
- Produces:
  - `class zodiac::DynamicLibrary { public: static std::expected<DynamicLibrary, std::string> open(const std::filesystem::path&); void* symbol(const char*) const; };` (move-only; Windows uses `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`).
  - `struct zodiac::ModuleSearchPaths { std::vector<std::filesystem::path> dirs; };` default: `<exe_dir>/../modules`, then `<project>/plugins`.
  - `class zodiac::ModuleRegistry { public: std::expected<void, std::string> load(std::span<const std::string> names, const ModuleSearchPaths&); std::expected<void, std::string> register_all(Host&); void shutdown_all(); std::span<const zodiac_module_info* const> ordered() const; };` `load` resolves dependencies transitively and orders them so dependencies register first.
  - `zodiac_add_module(<name> SOURCES ... [DEPENDS ...])` creates `zodiac_mod_<name>` (SHARED, output to `${CMAKE_BINARY_DIR}/modules`) and `zodiac_mod_<name>_static` (STATIC, compile definition `ZODIAC_MODULE_STATIC`). Plan 3 consumes the static variant.
  - Example module `example` (license `Zlib`) registers `string example_echo(const string &in)`, `int example_add(int, int)` (native fast path) and `void example_print(const string &in)` (writes the UTF-8 line to stdout).
- Error messages (exact prefixes): `module "<name>" not found in: <dir1>; <dir2>`, `module "<name>" could not be loaded: <os reason>`, `module "<name>" has no zodiac_module_get_info entry`, `module "<name>" needs ABI <n> but this Zodiac provides <m>`, `module "<name>" reports name "<other>"`, `module dependency cycle: a -> b -> a`.

- [ ] **Step 1: Write failing tests** in `tests/test_module_registry.cpp`, one per error message above (assert the exact text), plus `"example loads and registers"` (script calls `example_add(2,3)` → 5) and `"dependencies register before dependents"` (fixture `zodiac_mod_dep_child` depends on `example`, records order).
- [ ] **Step 2:** Run tests. Expected: FAIL.
- [ ] **Step 3:** Implement. The CLI process calls `SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)` before any load (Task 9), and the test runner does the same in its `main`, so no Windows dialog appears. Paths are `std::filesystem::path` end to end; never narrow to ANSI.
- [ ] **Step 4:** Run tests. Expected: PASS.
- [ ] **Step 5:** Commit: `git commit -m "feat(core): load modules dynamically with ABI and dependency checks"`.

### Task 6: Project file

**Files:**
- Create: `src/core/project_file.hpp/.cpp`
- Test: `tests/test_project_file.cpp`

**Interfaces:**
- Produces: `enum class zodiac::OutputMode { automatic, always, never };` `struct zodiac::DiagnosticsConfig { OutputMode console = OutputMode::automatic; std::filesystem::path file; OutputMode dialog = OutputMode::automatic; };` `struct zodiac::Project { std::filesystem::path root; std::filesystem::path main_script; std::vector<std::string> static_modules; std::vector<std::string> dynamic_modules; std::vector<std::string> core_exclude; DiagnosticsConfig diagnostics; };` and `std::expected<Project, std::string> zodiac::load_project(const std::filesystem::path& dir);` reading `<dir>/zodiac.toml`.
- Format:

```toml
[game]
name = "My Game"
main = "main.zs"
[modules]
static = ["example"]
dynamic = []
[core]
exclude = []
[diagnostics]
console = "auto"
file = "logs/zodiac.log"
dialog = "never"
```

- `[diagnostics]` is optional; values `"auto"`, `"always"`, `"never"`; `file` is relative to the project root. Error: `zodiac.toml: error: [diagnostics] console must be "auto", "always" or "never"` (same for `dialog`).

- Valid `[core] exclude` entries for now: `"window"`, `"keyboard"` (they become meaningful in plan 4). `"core"` in a module list is accepted and ignored (spec examples list it).
- Error messages: `zodiac.toml:<line>:<col>: error: <toml parser message>`, `zodiac.toml: error: [game] main is required`, `zodiac.toml: error: module "<name>" is listed as both static and dynamic`, `zodiac.toml: error: unknown [core] exclude entry "<name>"; valid: window, keyboard`, `zodiac.toml: error: main script "<path>" does not exist`.

- [ ] **Step 1: Write failing tests**: one per error message (exact text), `"diagnostics section defaults when missing"`, `"diagnostics section parsed"`, `"minimal valid project"`, `"project in a non-ASCII directory"` (temp dir named `Kanka şövalye oyunu`, main script loads).
- [ ] **Step 2:** Run tests. Expected: FAIL.
- [ ] **Step 3:** Implement with toml++ (`TOML_EXCEPTIONS=0`, using `toml::parse_result`).
- [ ] **Step 4:** Run tests. Expected: PASS.
- [ ] **Step 5:** Commit: `git commit -m "feat(core): parse and validate zodiac.toml"`.

### Task 7: Script runtime and diagnostics

**Files:**
- Create: `src/core/script_runtime.hpp/.cpp`, `src/core/diagnostics.hpp/.cpp`
- Test: `tests/test_script_runtime.cpp`, fixtures `tests/fixtures/scripts/`

**Interfaces:**
- Consumes: `Host`, `ModuleRegistry`, `Project`.
- Produces: `class zodiac::ScriptRuntime { public: ScriptRuntime(); Host& host(); int build(const Project&); int run(); };` `build` returns `0` or `1`; `run` executes `void main()` or `int main()` and returns its value, `0` for `void main()`, `1` on exception. Core registers only AngelScript's std add-ons (`string` with utils, `array`, `dictionary`); the core script API comes with plan 2.
- `class zodiac::DiagnosticSink { public: virtual ~DiagnosticSink() = default; virtual void write(zodiac_log_level level, std::string_view line) = 0; };` with `ConsoleSink` (stderr; platform part in `src/platform/<os>/console.cpp`) and `FileSink(const std::filesystem::path&)` (appends UTF-8, creates parent directories, flushes each line so nothing is lost on a crash). `class zodiac::Diagnostics { public: void add_sink(std::unique_ptr<DiagnosticSink>); void report(zodiac_log_level, std::string_view line); };` The dialog sink arrives with the window in plan 4; the attach-to-parent-console logic for GUI-subsystem games arrives with the release stub in plan 3.
- `zodiac::Diagnostics` writes one line per message to every sink: compile `<rel_path>:<line>:<col>: error|warning: <message>`; runtime `<rel_path>:<line>: runtime error: <message>` followed by one line per frame `  at <function> (<rel_path>:<line>)`. Paths relative to `Project::root`, forward slashes.
- `#include "file"` resolved relative to the including file (AngelScript `CScriptBuilder` with include callback reading via `std::filesystem::path`, UTF-8 BOM stripped).

- [ ] **Step 1: Write failing tests**:
  - `"compile error format"`: fixture with a typo on line 3 col 5 → stderr contains `main.zs:3:5: error:` and `build` returns 1.
  - `"error in included file is reported with its own path"`: `includes/util.zs:2:`.
  - `"include from a non-ASCII directory"`: `şehir/harita.zs`.
  - `"uncaught exception reports location and stack"`: null handle access in `helper()` called by `main()` → `main.zs:<line>: runtime error: Null pointer access` and an `at helper` line; `run` returns 1.
  - `"int main return value is the exit code"`: `int main(){return 7;}` → 7.
  - `"missing main"` → `main.zs: error: no main() function` and `build` returns 1.
  - `"file sink receives the same lines as the console"`: compile error with both sinks → identical lines in both, file is valid UTF-8.
  - `"errors before the script runs reach the file sink"`: missing module error goes to the log file.
- [ ] **Step 2:** Run tests. Expected: FAIL.
- [ ] **Step 3:** Implement. Strings are passed as `std::string_view` internally; diagnostics build each line once and write it with a single call.
- [ ] **Step 4:** Run tests. Expected: PASS.
- [ ] **Step 5:** Commit: `git commit -m "feat(core): build and run scripts with screen-reader-friendly diagnostics"`.

### Task 8: CLI

**Files:**
- Create: `src/cli/main.cpp`, `src/cli/CMakeLists.txt`, `src/cli/templates/` (embedded `zodiac.toml` and `main.zs` for `zodiac new`)
- Test: `tests/e2e/test_cli.py` (unittest, runs the built `zodiac` binary; registered with CTest)

**Interfaces:**
- Consumes: everything above.
- Commands:
  - `zodiac run [project_dir]` (default `.`): load project → load modules (`static` ∪ `dynamic`, both as shared libraries in dev mode) → register → build → run. Exit codes per Global Constraints.
  - `zodiac modules`: one line per available module: `<name> <version> <license> (<path>)`.
  - `zodiac new <dir>`: creates `zodiac.toml` and `main.zs` (script extension `.zs`), refuses a non-empty directory with `error: directory "<dir>" is not empty`.
  - Diagnostics are configured first, before the project's modules load: console sink always, file sink from `[diagnostics] file` or `--log-file <path>` (the flag overrides the project).
  - `zodiac --version`, `zodiac help`. Unknown command: `error: unknown command "<x>"; run zodiac help`, exit 2.
- Windows: `wmain`, arguments converted to UTF-8 once; `SetConsoleOutputCP(CP_UTF8)`; `SetErrorMode` as in Task 5.

- [ ] **Step 1: Write failing tests** in `tests/e2e/test_cli.py`: `test_new_then_run_prints_echo` (project using `example`, script prints `example_echo("merhaba kanka")` via `example_print`, stdout `merhaba kanka`, exit 0); `test_run_in_unicode_dir`; `test_missing_module_exit_2`; `test_compile_error_exit_1`; `test_modules_lists_example`; `test_unknown_command_exit_2`; `test_new_refuses_non_empty_dir`; `test_log_file_flag_captures_module_error` (`--log-file` with a missing module: same line on stderr and in the file, exit 2).
- [ ] **Step 2:** Run `ctest --preset windows-debug -R cli`. Expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run the full workflow. Expected: all tests pass.
- [ ] **Step 5:** Commit: `git commit -m "feat(cli): add zodiac run, modules and new"`.

### Task 9: CI and contributor docs

**Files:**
- Create: `.github/workflows/ci.yml`, `CONTRIBUTING.md`
- Modify: `README.md`

**Interfaces:**
- CI jobs `windows` (`windows-latest`) and `linux` (`ubuntu-latest`, apt `clang`, `ninja-build` not needed since bootstrap provides Ninja): checkout → `python bootstrap.py` → `.tools/cmake/bin/cmake --workflow --preset <os>` → upload test logs on failure. Cache `.tools/` keyed by hash of `tools/toolchain.json`, and FetchContent downloads keyed by `cmake/Dependencies.cmake`.
- `CONTRIBUTING.md` (plain headings and lists, no images, no tables that only make sense visually): setup in two commands, building a single module target, running one test, code style (English, no indentation in C/C++/CMake, no C++ types across the ABI), PR rules (CI green, tests for every behavior change, one topic per PR, review by a maintainer), how to write a plugin with `plugin.hpp`.

- [ ] **Step 1:** Write `ci.yml` and push to a branch on GitHub (repository creation is the owner's call; ask before creating it).
- [ ] **Step 2:** Verify: both jobs green on the branch. Expected: `windows` and `linux` pass.
- [ ] **Step 3:** Write `CONTRIBUTING.md`, update `README.md` quick start (exact two commands + `zodiac new mygame` + `zodiac run mygame`).
- [ ] **Step 4:** Fresh-clone check: clone into a new folder with a space and a non-ASCII character in its path, run the two commands. Expected: all tests pass.
- [ ] **Step 5:** Commit: `git commit -m "ci: build and test on Windows and Linux; add contributor guide"`.
