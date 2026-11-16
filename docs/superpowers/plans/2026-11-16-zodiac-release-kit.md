# Zodiac Release Builds and Installable Kit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `zodiac build` turns a project into a single Windows executable containing only its declared modules, using only the installed Zodiac kit, and the kit ships as a setup wizard and a portable zip.

**Architecture:** `zodiac build` compiles the scripts against the kit's shared modules, saves bytecode into a versioned payload, writes that payload as a COFF object, and links it with the release stub, `zodiac_core` (release configuration), AngelScript and the static module libraries using the kit's `ld.lld` and a link line captured from llvm-mingw when the kit was built. Static modules register through records in a dedicated linker section, forced in with `-u`. The release stub and `zodiac run` share one start-up path in `ScriptRuntime`.

**Tech Stack:** C++23, clang (llvm-mingw 20261104 UCRT), CMake 4.5 + Ninja, AngelScript 2.39.0, doctest, Python e2e tests, Inno Setup 6, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-16-zodiac-release-and-kit-design.md` (this plan's spec), built on `docs/superpowers/specs/2026-11-14-bgdk-design.md` (foundation) and `docs/superpowers/specs/2026-11-16-zodiac-script-api-design.md` (Script API).

## Global Constraints

- C++23, clang only, llvm-mingw 20261104 UCRT on Windows (pinned in `tools/toolchain.json`). Match the existing code style exactly: no indentation in C++ and CMake files, English identifiers and comments, a header comment per file describing its contract.
- Every user-facing message goes through `zodiac::Diagnostics` as one line (`one_line` escaping); core never writes stdout/stderr directly.
- Plugin ABI v1 is append-only: new `zodiac_module_info` members are read only when `struct_size` covers them; `ZODIAC_ABI_VERSION` stays 1.
- A release needs nothing on the player's machine beyond the game folder: no VC++ redistributable, no runtime DLLs other than the developer's `dynamic` modules.
- Dynamic modules in a release are loaded from the executable's folder only (never the current directory or PATH).
- Release startup registers exactly the modules stored in the payload, in the stored order, with the stored versions; any mismatch is a startup error (exit 2), never undefined behaviour.
- Identity: script behaviour (results, exit codes, exception messages, file and line) is identical between `zodiac run` and a release build. Debug info is kept unless `[build] strip_debug_info = true`.
- Paths: project root is the folder containing `zodiac.toml` (or the given `.zs` file's folder when there is none); in a release, the executable's folder. `#include` is relative to the including file; assets, workers and `plugins/` are relative to the project root. The working directory is set to the project root (dev) / executable folder (release) at start-up.
- `zodiac build` never leaves a half-written executable at the output path (temporary name, rename at the end).
- Exit codes: 0 success, 1 script (compile/runtime) error, 2 usage/project/module/link error.
- Release builds are Windows-host, Windows-target only in this plan. On Linux, `zodiac build` reports `error: release builds are not supported on this host yet` and exits 2; Linux CI must stay green, Windows-only tests are registered only `if(WIN32)`.
- A core-only "hello world" release is at most 3 MB (3145728 bytes).
- The engine shows no window in games. The build result window (Task 9) belongs to the kit tool only.
- Not in this plan (owned by plan 4): `verbose`, the dialog sink and the automatic release `<game>.log`, dev-only checks, workers at runtime. Until then the release stub honours the project's `console` and `file` diagnostics settings, and dev and release cores are built from identical sources under two target names.
- Never use shell heredocs to create or edit files (owner's rule); use the editor tools.

## Review Focus

1. **Paths with spaces and non-ASCII characters** in the kit folder, the project folder and the output folder (e.g. `C:\Program Files\Zodiac`, `Kanka şövalye oyunu`): the linker must receive them correctly (response file, UTF-8/UTF-16 handled). Test in Task 7.
2. **A module changed after the build** (dynamic DLL next to the exe replaced by another version, or missing): the stub must stop with a clear error, exit 2, never crash. Test in Task 5.
3. **Rebuilding while the game is still running** (output exe locked): build fails with a clear message, exit 2, the old exe and nothing else is left at the output path. Test in Task 7.
4. **A truncated or tampered executable** (payload bytes changed): CRC failure reported, exit 2, no crash. Test in Task 4 (payload) and Task 5 (stub).
5. **Double-clicking a `.zs` file in a subfolder** (`includes/menu.zs` with `zodiac.toml` in the parent): the project is found, the clicked file runs as main, includes resolve relative to it. Test in Task 8.

---

### Task 1: Process runner and kit layout lookup

**Files:**
- Modify: `src/platform/process.hpp`, `src/platform/windows/process.cpp`, `src/platform/posix/process.cpp`
- Create: `src/core/kit_layout.hpp`, `src/core/kit_layout.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/test_process.cpp`, `tests/test_kit_layout.cpp`, `tests/fixtures/tools/echo_args.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct zodiac::platform::ProcessResult{int exit_code;std::string output;};` (output = stdout and stderr merged, UTF-8)
  - `std::expected<ProcessResult,std::string> zodiac::platform::run_process(std::span<const std::string> argv);` — argv[0] is an absolute path; arguments are UTF-8 and passed verbatim (Windows: `CreateProcessW` with correct `CommandLineToArgvW` quoting; POSIX: `posix_spawn`). No shell.
  - `struct zodiac::KitLayout{std::filesystem::path root,bin,modules,static_modules,release_lib;};` and `KitLayout zodiac::kit_layout(const std::filesystem::path& executable);` — `root = executable.parent_path().parent_path()`, `bin = root/"bin"`, `modules = root/"modules"`, `static_modules = root/"modules"/"static"`, `release_lib = root/"lib"/"release"`. Works for the build tree (`build/<preset>/bin/zodiac.exe`) and the installed kit alike.

- [ ] **Step 1: Write the failing tests**

`tests/fixtures/tools/echo_args.cpp` is a tiny executable that prints each argument on its own line to stdout and `err` to stderr, and exits with the number of arguments.

```cpp
TEST_CASE("run_process passes arguments verbatim and captures output"){
const std::vector<std::string> argv{ZODIAC_TEST_ECHO_ARGS,"a b","q\"uote","back\\slash\\","şehir",""};
auto result=zodiac::platform::run_process(argv);
REQUIRE(result);
CHECK(result->exit_code==5);
CHECK(result->output.find("a b\n")!=std::string::npos);
CHECK(result->output.find("q\"uote\n")!=std::string::npos);
CHECK(result->output.find("back\\slash\\\n")!=std::string::npos);
CHECK(result->output.find("şehir\n")!=std::string::npos);
CHECK(result->output.find("err")!=std::string::npos);
}
TEST_CASE("run_process reports a missing program"){
const std::vector<std::string> argv{"C:/does/not/exist.exe"};
CHECK_FALSE(zodiac::platform::run_process(argv));
}
TEST_CASE("kit_layout derives every folder from the executable"){
const auto kit=zodiac::kit_layout("/kit root/bin/zodiac.exe");
CHECK(kit.root==std::filesystem::path("/kit root"));
CHECK(kit.release_lib==std::filesystem::path("/kit root/lib/release"));
CHECK(kit.static_modules==std::filesystem::path("/kit root/modules/static"));
}
```

(On POSIX the missing-program path is `/does/not/exist`; the CRLF of Windows output is normalized to `\n` by `run_process`.)

- [ ] **Step 2: Run to verify they fail**

Run: `.tools/cmake/bin/cmake --build --preset windows-debug` then `.tools/cmake/bin/ctest --preset windows-debug -R "test_process|test_kit_layout"`
Expected: build fails (functions not declared).

- [ ] **Step 3: Implement `run_process` and `kit_layout`**

Windows: build the command line with the MSVC quoting rules (backslashes doubled only before a quote, empty argument as `""`), capture through one inherited pipe for stdout and stderr, `CREATE_NO_WINDOW`, read until EOF, wait, `GetExitCodeProcess`. POSIX: `posix_spawn` with a pipe on fds 1 and 2.

- [ ] **Step 4: Run tests to verify they pass**

Same commands. Expected: PASS on Windows; on Linux (CI) the POSIX variant passes too.

- [ ] **Step 5: Commit**

```
git add src/platform src/core/kit_layout.* src/core/CMakeLists.txt tests
git commit -m "Add process runner and kit layout lookup"
```

### Task 2: Project file additions and asset globs

**Files:**
- Modify: `src/core/project_file.hpp`, `src/core/project_file.cpp`, `src/core/text.hpp`, `src/core/text.cpp`
- Test: `tests/test_project_file.cpp`, `tests/test_text.cpp`

**Interfaces:**
- Produces in `zodiac::Project`:
  - `std::string name;` (`[game] name`, already written by `zodiac new`; defaults to the root folder's name)
  - `struct BuildConfig{std::vector<std::string> assets;bool strip_debug_info=false;std::vector<std::string> sign_command;};` as `Project::build` — `assets` are glob patterns relative to the root (`/` separators); `sign_command` must contain `{file}` in at least one element when non-empty.
  - `std::vector<std::filesystem::path> workers;` (`[workers] scripts`, absolute, inside root, existing `.zs` files, no duplicates)
- Produces in `text.hpp`: `bool glob_match(std::string_view pattern,std::string_view path) noexcept;` — `*` matches within one path segment, `?` one character of a segment, `**` any number of segments (including none), `/` separators, case-sensitive byte comparison.
- Produces: `std::expected<std::vector<std::filesystem::path>,std::string> zodiac::expand_assets(const Project& project);` in `project_file.hpp` — every regular file under root matching any pattern, sorted, without duplicates, never `zodiac.toml`, never inside `release/`.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("glob_match"){
CHECK(zodiac::glob_match("sounds/*.ogg","sounds/shot.ogg"));
CHECK_FALSE(zodiac::glob_match("sounds/*.ogg","sounds/guns/shot.ogg"));
CHECK(zodiac::glob_match("sounds/**/*.ogg","sounds/guns/rifle/shot.ogg"));
CHECK(zodiac::glob_match("sounds/**/*.ogg","sounds/shot.ogg"));
CHECK(zodiac::glob_match("maps/level?.map","maps/level1.map"));
CHECK_FALSE(zodiac::glob_match("maps/level?.map","maps/level10.map"));
CHECK(zodiac::glob_match("**","a/b/c.txt"));
}
```

Project file tests (one `TEST_CASE` each, using the existing temp-project helper in `test_project_file.cpp`):
- `[build] assets = ["sounds/**/*.ogg"]`, `strip_debug_info = true` and `[build.sign] command = ["signtool","sign","{file}"]` load into `Project::build` unchanged.
- `[build.sign] command = ["signtool"]` fails with `zodiac.toml: error: [build.sign] command must contain {file}`.
- `[workers] scripts = ["bots.zs"]` with the file present loads one absolute path; a missing file fails with `zodiac.toml: error: worker script "bots.zs" does not exist`; `"../x.zs"` fails with `... must be inside the project`.
- `expand_assets` on a tree with `sounds/a.ogg`, `sounds/guns/b.ogg`, `sounds/c.wav`, `release/sounds/d.ogg` and pattern `sounds/**/*.ogg` returns exactly `sounds/a.ogg`, `sounds/guns/b.ogg`.
- A pattern matching nothing is not an error (empty list).

- [ ] **Step 2: Run to verify they fail**

Run: `.tools/cmake/bin/ctest --preset windows-debug -R "test_project_file|test_text"` after building. Expected: compile failure.

- [ ] **Step 3: Implement** the new keys in `load_project` with the same error style as the existing keys, `glob_match` as a segment-wise matcher (no regex, no allocation), and `expand_assets` with one recursive directory walk.

- [ ] **Step 4: Run tests to verify they pass** (same command; Linux CI as well).

- [ ] **Step 5: Commit**

```
git add src/core tests
git commit -m "Read build, signing and worker settings from zodiac.toml"
```

### Task 3: Link modes in the module info and static module registration

**Files:**
- Modify: `include/zodiac/plugin.h`, `include/zodiac/plugin.hpp` (doc only if it mentions the info layout), `cmake/ZodiacModule.cmake`, `modules/example/example.cpp`, `tests/fixtures/modules/chain.cpp`, `tests/CMakeLists.txt`
- Modify: `src/core/module_registry.hpp`, `src/core/module_registry.cpp`
- Create: `src/core/static_modules.hpp`, `src/core/static_modules.cpp`
- Test: `tests/test_plugin_header.cpp`, `tests/test_plugin_header_c.c`, `tests/test_module_registry.cpp`, `tests/test_static_modules.cpp`

**Interfaces:**
- Produces in `plugin.h` (appended after `shutdown_module`):
  - `uint32_t link_modes;` with `#define ZODIAC_LINK_STATIC 1u` and `#define ZODIAC_LINK_DYNAMIC 2u`; 0 or absent (v1-sized info) means both are allowed.
  - Static builds: `ZODIAC_DEFINE_MODULE(info_variable)` emits `const zodiac_module_info* const zodiac_static_module_<ZODIAC_MODULE_ID>` with C linkage, `used`, in section `.zodmod$m`, pointing at `info_variable`. `ZODIAC_MODULE_ID` is required when `ZODIAC_MODULE_STATIC` is defined (`#error` otherwise); `zodiac_add_module` passes `ZODIAC_MODULE_ID=<name>` to the static target.
- Produces in `module_registry.hpp`:
  - `inline constexpr std::size_t module_info_link_modes_end=offsetof(zodiac_module_info,link_modes)+sizeof(uint32_t);`
  - `uint32_t module_link_modes(const zodiac_module_info& info) noexcept;` — returns `ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC` when the member is absent or 0.
  - `ModuleRegistry::load(std::span<const std::string> names,const ModuleSearchPaths& search,std::span<const zodiac_module_info* const> linked={})` — a name found in `linked` (by `info->name`) is used without loading a library; otherwise the shared-library search applies as today. Validation (ABI, struct_size, name) is identical for both.
  - `std::span<const std::string> ModuleRegistry::ordered_names() const noexcept;` (registration order, for the payload)
- Produces in `static_modules.hpp`: `std::span<const zodiac_module_info* const> zodiac::linked_static_modules() noexcept;` — the non-null records between the `.zodmod$a` and `.zodmod$z` markers defined in `static_modules.cpp`. Built only into the release core (Task 5); declared everywhere.

- [ ] **Step 1: Write the failing tests**

- `test_plugin_header.cpp`: `static_assert(offsetof(zodiac_module_info,link_modes)==kHeader+7*kPointer);` and update the final size assert to the new `sizeof` (72 on 64-bit, padded). Mirror in `test_plugin_header_c.c`.
- `test_module_registry.cpp`:
  - `module_link_modes` of a 64-byte info returns both bits; of an info with `link_modes=ZODIAC_LINK_DYNAMIC` returns only dynamic.
  - The `larger_info` fixture moves to `STRUCT_SIZE 80 EXTRA_BYTES 8` (still "newer than this header") and still loads.
  - "a linked static module is used instead of a library": `load({"example"}, {empty search}, {&info})` with an in-test `zodiac_module_info` named `example` succeeds and `ordered_names()` is `{"example"}`.
  - "linked info is validated like a library": a linked info with `abi_version=0` is refused with the same message as a library.
- `test_static_modules.cpp`: a test executable linking `zodiac_mod_example_static` with `-Wl,-u,zodiac_static_module_example` and the core's `static_modules.cpp`; `linked_static_modules()` has exactly one entry whose `name` is `"example"`. Windows only.

- [ ] **Step 2: Run to verify they fail**

Build and run `ctest --preset windows-debug -R "test_plugin_header|test_module_registry|test_static_modules"`. Expected: compile failures.

- [ ] **Step 3: Implement** the appended member, the static `ZODIAC_DEFINE_MODULE` variant (section attribute plus `extern "C"` so `-u` can name it; also valid in C), the markers (`.zodmod$a` begin pointer, `.zodmod$z` end pointer, both `used`), the registry lookup and `ordered_names()`. Set `link_modes=ZODIAC_LINK_STATIC|ZODIAC_LINK_DYNAMIC` in `example.cpp`.

- [ ] **Step 4: Run tests to verify they pass**, plus the whole suite (`ctest --preset windows-debug`). Linux CI builds the new headers; `test_static_modules` is Windows-only.

- [ ] **Step 5: Commit**

```
git add include cmake modules src/core tests
git commit -m "Add module link modes and static module registration records"
```

### Task 4: Bytecode, game payload and payload object

**Files:**
- Create: `src/core/payload.hpp`, `src/core/payload.cpp`, `src/core/coff_object.hpp`, `src/core/coff_object.cpp`
- Modify: `src/core/script_runtime.hpp`, `src/core/script_runtime.cpp`, `src/core/CMakeLists.txt`
- Test: `tests/test_payload.cpp`, `tests/test_coff_object.cpp`, `tests/test_script_runtime.cpp`

**Interfaces:**
- Produces in `payload.hpp`:

```cpp
enum class LinkMode:uint8_t{static_link=1,dynamic_link=2};
struct PayloadModule{std::string name;std::string version;LinkMode link;};
struct PayloadScript{std::string section;bool is_main;std::string bytecode;};
struct GamePayload{
uint32_t format_version=1;
std::string kit_version;
std::string game_name;
std::vector<std::string> requested;      // names passed to ModuleRegistry::load, in that order
std::vector<PayloadModule> modules;      // registration order
std::vector<std::string> core_exclude;
DiagnosticsConfig diagnostics;           // file stored relative to the root
std::vector<PayloadScript> scripts;
};
std::string serialize_payload(const GamePayload& payload);
std::expected<GamePayload,std::string> parse_payload(std::string_view bytes);
```

  Format: magic `ZDPAYLD1` (8 bytes), little-endian `u32` fields, strings and byte blobs as `u32` length plus bytes, counts as `u32`, a final `u32` CRC-32 (IEEE) over everything before it. `parse_payload` checks magic, format version 1, every length against the remaining bytes, and the CRC; each error is one line starting with `error: the game data is damaged` (e.g. `error: the game data is damaged (checksum mismatch)`).
- Produces in `coff_object.hpp`: `std::string write_payload_object(std::string_view payload);` — an x86-64 COFF object (machine `0x8664`) with one section `.zodpay` (initialized data, read-only, 16-byte aligned) holding a `u64` little-endian length followed by the payload, and one external symbol `zodiac_payload` at offset 0 of that section.
- Produces in `ScriptRuntime`:
  - `std::expected<std::string,std::string> save_bytecode(bool strip_debug_info) const;` (after a successful `build`)
  - `int load_bytecode(const Project& project,std::string_view bytecode,std::span<const zodiac_module_info* const> linked);` — loads modules exactly as `build` does (passing `linked` to the registry), then `LoadByteCode` into the same module name and finds `main` the same way. Returns 0, 1 (bytecode does not match) or 2 (module error).
  - `std::span<const std::string> module_order() const noexcept;` forwarding `registry_.ordered_names()`.

- [ ] **Step 1: Write the failing tests**

- `test_payload.cpp`: round trip of a payload with two modules, one script and Unicode game name equals the input; flipping any single byte of the serialized form makes `parse_payload` fail with a message starting `error: the game data is damaged`; truncating to every length 0..size-1 fails without crashing (loop over all lengths).
- `test_coff_object.cpp`: the object starts with machine `0x8664`, has 1 section named `.zodpay`, section data is `u64(size)+payload`, the symbol table contains `zodiac_payload` with section number 1 and value 0 (parse the bytes in the test).
- `test_script_runtime.cpp`:
  - "bytecode round trip runs the same": build `fixtures/scripts/int_main`, save, load into a fresh runtime with the same project, `run()` returns the same value.
  - "runtime errors keep file and line from bytecode": `runtime_error` fixture through bytecode reports the same diagnostic lines as from source.
  - "stripped bytecode reports line unknown": with `strip_debug_info=true` the runtime-error line is `<section>: runtime error: ... (line unknown)`. Decision: when the row is 0 and the module came from stripped bytecode, append ` (line unknown)`.
  - "bytecode built against another module set is refused": save with `example` loaded, load with no modules → returns 1 with `error: the game data does not match the modules it was built with`.

- [ ] **Step 2: Run to verify they fail** (`ctest -R "test_payload|test_coff_object|test_script_runtime"`).

- [ ] **Step 3: Implement** an `asIBinaryStream` over `std::string` (write appends, read checks bounds and fails the load instead of reading past the end), the payload codec with a table-driven CRC-32, and the COFF writer (file header, one section header, raw data, symbol table with the long-name string table entry for `zodiac_payload`).

- [ ] **Step 4: Run tests to verify they pass** (Windows and Linux; nothing here is Windows-only — the COFF writer is plain byte output).

- [ ] **Step 5: Commit**

```
git add src/core tests
git commit -m "Save bytecode into a checked game payload and a COFF object"
```

### Task 5: Release core and release stub

**Files:**
- Modify: `src/core/CMakeLists.txt` (second target `zodiac_core_release` from the same sources plus `static_modules.cpp`, compile definition `ZODIAC_RELEASE=1`)
- Create: `src/release/CMakeLists.txt`, `src/release/stub.cpp`, `src/release/stub.hpp`
- Modify: `src/platform/console.hpp`, `src/platform/windows/console.cpp`, `src/platform/posix/console.cpp`, `CMakeLists.txt`
- Test: `tests/test_release_stub.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `GamePayload`, `parse_payload`, `ScriptRuntime::load_bytecode`, `linked_static_modules()`, `platform::executable_path()`.
- Produces:
  - `void zodiac::platform::attach_parent_console() noexcept;` — Windows: `AttachConsole(ATTACH_PARENT_PROCESS)` and rebinding the standard handles when it succeeds; POSIX: no-op.
  - `int zodiac::release::start(std::string_view payload_bytes,const std::filesystem::path& executable);` in `stub.hpp` — parses the payload, sets up diagnostics (console sink; file sink when the payload has a file, relative to the executable folder), builds a `Project` with `root=executable.parent_path()`, `main_script=root/<main section>`, `static_modules`/`dynamic_modules` from the payload's `requested` names and link modes, sets the working directory to root, calls `load_bytecode(project,main_bytecode,linked_static_modules())` with `ModuleSearchPaths{{root}}`, then checks that `module_order()` and each module's version equal the payload's list, and runs. Returns the exit code under the same rules as `zodiac run` (0–255 clamp with the same warning).
  - `src/release/stub.cpp` also defines `wWinMain` (Windows, GUI subsystem) that calls `disable_error_dialogs`, `attach_parent_console`, `setup_utf8_console`, reads the `zodiac_payload` symbol (`u64` length + bytes) and calls `start`. The library target `zodiac_release_stub` contains it; `zodiac_payload` stays unresolved until `zodiac build` links the payload object.
  - Mismatch messages (exit 2): `error: module "<name>" is missing` (from the registry), `error: module "<name>" is version <found>, but the game was built with <expected>`, `error: the modules were registered in a different order than when the game was built`.

- [ ] **Step 1: Write the failing tests**

`test_release_stub.cpp` calls `release::start` directly (no linking step needed), with a payload made in the test from `ScriptRuntime::build`+`save_bytecode` and a temporary folder as the "executable folder":
- "start runs the payload's main": `int_main` fixture → returns its value.
- "start refuses a damaged payload": one flipped byte → returns 2, the diagnostics line starts with `error: the game data is damaged`.
- "start refuses a missing dynamic module": payload lists `example` as dynamic, folder has no DLL → 2, `module "example"` in the message.
- "start refuses a different module version": payload says `example` version `9.9.9` → 2 with the version message.
- "start sets the working directory to the executable folder": a fixture module `cwd_probe` (new, `tests/fixtures/modules/cwd_probe.cpp`, registers `string cwd()`) copied into the folder and used dynamically; script returns 0 when `cwd()` equals the folder.

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Implement** `zodiac_core_release`, `attach_parent_console`, `release::start` and `wWinMain`. `zodiac run` also sets the working directory to the project root before `runtime.run()` (identity rule); add an e2e case for it using `cwd_probe`.

- [ ] **Step 4: Run tests to verify they pass** (whole suite; stub tests run on Linux too, since `start` is portable — only `wWinMain` is Windows-only).

- [ ] **Step 5: Commit**

```
git add src tests CMakeLists.txt
git commit -m "Add the release core and the release stub start-up path"
```

### Task 6: Release link line capture and the kit's release library folder

**Files:**
- Create: `cmake/CaptureReleaseLink.cmake`, `cmake/ZodiacKitStage.cmake`, `src/cli/linker.hpp`, `src/cli/linker.cpp`
- Modify: `CMakeLists.txt`, `src/cli/CMakeLists.txt`, `cmake/ZodiacModule.cmake`
- Test: `tests/test_linker.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Produces in the build tree (and later the kit, Task 10), Windows only:
  - `lib/release/` with `libzodiac_release_stub.a`, `libzodiac_core_release.a`, `libangelscript.a`, `crt/` (every CRT object and library the captured link line needs, copied from llvm-mingw), `link.txt`, `toolchain.txt` (`llvm-mingw 20261104 ucrt x86_64`).
  - `bin/ld.lld.exe` copied from llvm-mingw.
  - `modules/static/zodiac_mod_<name>.a` for every module built with a static variant (renamed from `libzodiac_mod_<name>_static.a`), plus `zodiac_mod_<name>.stamp` holding the `toolchain.txt` text.
- `link.txt` format: one argument per line, as captured from `x86_64-w64-mingw32-clang++ -### -static -mwindows -municode -Wl,--gc-sections -s` linking a placeholder object, with absolute llvm-mingw paths rewritten to `{release_lib}/crt/<file>`, the output path replaced by `{output}`, and the placeholder object replaced by `{inputs}`. `CaptureReleaseLink.cmake` fails the kit build if the captured line has no `ld.lld` invocation or references a file it cannot copy.
- Produces in `linker.hpp`:

```cpp
struct LinkRequest{
std::filesystem::path output;
std::vector<std::filesystem::path> inputs;     // payload object, stub, static modules, core, angelscript, in this order
std::vector<std::string> forced_symbols;       // zodiac_static_module_<name>
};
std::expected<void,std::string> link_release(const KitLayout& kit,const LinkRequest& request,Diagnostics& diagnostics);
```

  It expands `link.txt`, adds `-u <symbol>` per forced symbol, writes all arguments to a UTF-8 response file (each argument quoted for lld's response-file syntax) next to the output, runs `bin/ld.lld.exe @<rsp>` through `run_process`, deletes the response file, and on failure returns `error: linking failed` followed by the linker's output lines (each reported separately through `diagnostics`).

- [ ] **Step 1: Write the failing test**

`test_linker.cpp` (Windows only): build a payload object for the `int_main` fixture with Task 4/5 APIs, call `link_release` into a temporary folder whose path contains a space and `ş`, run the resulting exe with `run_process`, expect its exit code to equal the fixture's value. A second case links with a nonexistent input and expects `error: linking failed` plus at least one line of linker output.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** the capture script (run at configure time through `execute_process`, regenerated when the toolchain version changes), the staging target `zodiac_kit_stage` that every `zodiac` build depends on, and `link_release`.

- [ ] **Step 4: Run tests to verify they pass.**

- [ ] **Step 5: Commit**

```
git add cmake CMakeLists.txt src/cli tests
git commit -m "Capture the release link line and stage the kit's release libraries"
```

### Task 7: `zodiac build`

**Files:**
- Create: `src/cli/build_command.hpp`, `src/cli/build_command.cpp`
- Modify: `src/cli/main.cpp`, `src/cli/CMakeLists.txt`
- Test: `tests/e2e/test_build.py`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 1–6.
- Produces: `int zodiac::cli::build_project(const BuildOptions& options,Diagnostics& diagnostics);` with `struct BuildOptions{std::filesystem::path target;std::filesystem::path output;std::vector<std::filesystem::path> module_dirs;bool no_window=false;};` (`target` is a project folder or a `.zs` file; `.zs` resolves to its project by the upward search of Task 8, and a missing `zodiac.toml` is `error: zodiac.toml not found; create a project with zodiac new`, exit 2).
- CLI: `zodiac build [project_dir|file.zs] [--output <dir>] [--no-window] [--module-dir <dir>] [--log-file <path>]`; help text gains the line `zodiac build [project_dir|file.zs]: build a release of the game (default output: <project>/release)`.
- Steps exactly as spec 8.2: load project; for each `static` module, refuse when `module_link_modes` lacks `ZODIAC_LINK_STATIC` (`error: module "<name>" (<license>) cannot be linked statically; list it under dynamic`, exit 2) and warn when its license is not one of `Zlib MIT BSD-2-Clause BSD-3-Clause Apache-2.0 BSL-1.0 ISC Unlicense CC0-1.0` (`warning: module "<name>" is linked statically under license <license>; check its terms`); third-party static modules come from `<root>/plugins/static/` and need a `.stamp` equal to `toolchain.txt`, otherwise `error: plugin "<name>" was not built with this kit's toolchain; list it under dynamic`; compile (errors exit 1); save bytecode honouring `strip_debug_info`; serialize the payload (`kit_version=ZODIAC_VERSION`); write `<output>/<name>.zodpay.obj` as a temporary input; link to `<output>/<name>.exe.tmp`; rename over `<output>/<name>.exe` (failure: `error: cannot replace "<path>": it is in use (is the game still running?)`, exit 2, temporary file removed, old exe untouched); copy each `dynamic` module DLL (kit `modules/` or `<root>/plugins/`); copy expanded assets keeping relative paths; run the sign command for the exe and every copied DLL when configured (non-zero: `error: signing "<file>" failed`, exit 2); delete the temporary object.
- With `no_window=false` the result is shown through Task 9's window; until Task 9 lands, `build` behaves as `--no-window` (Task 9 replaces that branch).
- Linux host: `error: release builds are not supported on this host yet`, exit 2.

- [ ] **Step 1: Write the failing tests** (`tests/e2e/test_build.py`, Windows only via `unittest.skipUnless(os.name == "nt")`; the CTest entry passes `ZODIAC_EXE`, `ZODIAC_MODULE_DIR`):

- `test_build_hello_runs`: `zodiac new`, `build --no-window`, run `release/<name>.exe` → exit 0, no output; the build's stdout ends with `Build succeeded: <path>`.
- `test_build_static_example_module_prints`: project with `static = ["example"]` prints `merhaba kanka` from the release exe. Undeclared modules stay out: the core-only hello exe from `test_build_hello_runs` does not contain the bytes `example_echo` (the example module's declaration string), while this exe does.
- `test_build_dynamic_module_ships_dll`: `dynamic = ["example"]` → `release/zodiac_mod_example.dll` exists and the exe runs; deleting the DLL makes the exe exit 2 with `module "example"`.
- `test_identity_with_run`: for each folder in `tests/fixtures/scripts` that `run` accepts, exit code and stderr lines equal between `zodiac run` and the built exe (script paths in messages are identical by construction).
- `test_build_paths_with_space_and_unicode`: project at `<tmp>/Kanka şövalye oyunu`, output `<tmp>/çıktı klasörü` → exe runs.
- `test_build_locked_output`: open the existing exe with exclusive access from the test (`msvcrt.locking` is not enough; open via `ctypes` `CreateFileW` with share mode 0), rebuild → exit 2, message contains `is in use`, no `.tmp` left, old file bytes unchanged.
- `test_build_compile_error_exit_1`: compile error → exit 1, no exe.
- `test_build_assets_copied`: `[build] assets = ["sounds/**/*.ogg"]` copies `sounds/a.ogg` and `sounds/x/b.ogg` only.
- `test_build_sign_command_runs`: `[build.sign] command` pointing at the `echo_args` fixture tool with `{file}` → build succeeds and the tool was invoked (fixture writes a marker file); a tool exiting non-zero fails the build with `signing`.
- `test_build_without_project`: a lone `.zs` → exit 2 with `zodiac.toml not found`.
- `test_hello_size_gate`: core-only hello exe size ≤ 3145728 bytes.

- [ ] **Step 2: Run to verify they fail** (`ctest --preset windows-debug -R build_e2e`).

- [ ] **Step 3: Implement** `build_project` and wire the command into `main.cpp`.

- [ ] **Step 4: Run tests to verify they pass**, then the whole suite on Windows; push and confirm Linux CI green.

- [ ] **Step 5: Commit**

```
git add src/cli tests
git commit -m "Add zodiac build"
```

### Task 8: `zodiac run` with a `.zs` file

**Files:**
- Modify: `src/cli/main.cpp`, `src/core/project_file.hpp`, `src/core/project_file.cpp`
- Test: `tests/test_project_file.cpp`, `tests/e2e/test_cli.py`

**Interfaces:**
- Produces in `project_file.hpp`:
  - `std::optional<std::filesystem::path> find_project_root(const std::filesystem::path& script_file);` — the nearest folder, starting at the file's folder and walking up to the filesystem root, that contains `zodiac.toml`.
  - `std::expected<Project,std::string> load_project_for_script(const std::filesystem::path& script_file,std::span<const std::string> installed_modules);` — with a root: `load_project(root)` and `main_script` replaced by the given file; without: `Project{root=file folder,main_script=file,name=file stem,static_modules=installed_modules (sorted by name)}`.
- `zodiac run <file.zs>` uses it; `installed_modules` are the module names found in the search directories (the same scan as `zodiac modules`, first directory wins). `zodiac build <file.zs>` uses only `find_project_root` (Task 7).

- [ ] **Step 1: Write the failing tests**

- Unit: `find_project_root` finds the parent of `includes/menu.zs`; returns empty in a tree without `zodiac.toml`.
- e2e `test_run_script_in_subfolder_uses_project`: project with `static = ["example"]`, `includes/menu.zs` calling `example_print("menu")` and including `../shared.zs` relative to itself → `zodiac run includes/menu.zs --module-dir <dir>` prints `menu`.
- e2e `test_run_lone_script_loads_installed_modules`: a `.zs` in an empty folder calling `example_print("tek dosya")` runs with exit 0.
- e2e `test_run_lone_script_relative_include`: lone script including a sibling file works.

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Implement**; `run` keeps accepting a directory (unchanged behaviour); a path ending in `.zs` that is a regular file takes the new branch.

- [ ] **Step 4: Run tests to verify they pass** (Windows and Linux).

- [ ] **Step 5: Commit**

```
git add src tests
git commit -m "Run a single .zs file, with or without a project file"
```

### Task 9: Build result window and `zodiacw.exe`

**Files:**
- Create: `src/platform/result_window.hpp`, `src/platform/windows/result_window.cpp`, `src/platform/posix/result_window.cpp`, `src/cli/build_report.hpp`, `src/cli/build_report.cpp`
- Modify: `src/cli/build_command.cpp`, `src/cli/CMakeLists.txt`, `src/platform/windows/process.cpp`
- Test: `tests/test_build_report.cpp`, `tests/e2e/test_build.py`

**Interfaces:**
- Produces in `result_window.hpp`:
  - `bool zodiac::platform::has_interactive_desktop() noexcept;` — Windows: the process window station has `WSF_VISIBLE`; POSIX: false (until a Linux host plan adds it).
  - `void zodiac::platform::show_result_window(std::string_view title,std::string_view text,const std::filesystem::path* output_folder);` — blocks until closed.
- Window layout (exact copy from spec 8.2a): title `Zodiac build - <game name>`; a static label `Build output` followed by a read-only multi-line edit (`ES_MULTILINE|ES_READONLY|WS_VSCROLL|WS_TABSTOP`), focused on open with the caret at position 0; buttons `Copy all`, `Open output folder` (created only when `output_folder` is non-null), `Close` (default button, also Esc and the window's close box). Dialog-style keyboard handling (`IsDialogMessageW`) so Tab and Shift+Tab move between controls. Text converted to UTF-16 with `\r\n` line ends. Copy puts the whole text on the clipboard as `CF_UNICODETEXT`; Open runs `ShellExecuteW(nullptr,L"open",folder,...)`.
- Produces in `build_report.hpp`: a `DiagnosticSink` `BuildReport` that records every line and counts `ZODIAC_LOG_ERROR` lines that do not start with two spaces, and `std::string BuildReport::text(bool succeeded,const std::filesystem::path& exe) const;` whose first line is `Build succeeded: <exe>` or `Build failed: <N> errors` (`1 error` singular), followed by every recorded line.
- `build_project` with `no_window=false`: adds a `BuildReport` sink; when finished and `has_interactive_desktop()`, shows the window; otherwise prints the summary line to the console exactly as with `--no-window`.
- `zodiacw.exe`: the `zodiac` sources linked with `-mwindows` and a `wWinMain` entry (selected by `ZODIAC_GUI_ENTRY`) in `src/platform/windows/process.cpp`; same `cli::run`. Output to `bin/`.

- [ ] **Step 1: Write the failing tests**

- `test_build_report.cpp`: two compile errors with a call-stack line → `Build failed: 2 errors` then the three lines in order; success → `Build succeeded: C:/x/game.exe`; one error → `Build failed: 1 error`.
- e2e `test_build_without_flag_and_no_desktop_prints_summary`: whether a CI runner has a visible window station is not guaranteed, so the test sets `ZODIAC_FORCE_NO_DESKTOP=1`, which `has_interactive_desktop()` honours (a documented test hook), runs `zodiac build` without `--no-window`, and checks that it returns and prints the summary line on stdout.
- Manual check (recorded in the task report, owner verifies with NVDA after the kit exists): the window opens on right-click build, the edit is read line by line, Copy all and Open output folder work, Esc closes.

- [ ] **Step 2: Run to verify they fail.**

- [ ] **Step 3: Implement.**

- [ ] **Step 4: Run tests to verify they pass.**

- [ ] **Step 5: Commit**

```
git add src tests
git commit -m "Show build results in an accessible window and add zodiacw.exe"
```

### Task 10: Kit install layout and portable zip

**Files:**
- Modify: `CMakeLists.txt`, `src/cli/CMakeLists.txt`, `cmake/ZodiacKitStage.cmake`, `modules/CMakeLists.txt`
- Create: `THIRD_PARTY_LICENSES`, `cmake/ZodiacPackage.cmake`, `packaging/README-zip.txt`
- Test: `tests/e2e/test_kit.py`, `tests/CMakeLists.txt`

**Interfaces:**
- `cmake --install build/windows-release --prefix dist/Zodiac` produces exactly spec 8.1's layout: `bin/zodiac.exe`, `bin/zodiacw.exe`, `bin/ld.lld.exe`, `lib/release/...` (Task 6 contents), `modules/*.dll` (shipped modules only, never test fixtures), `modules/static/*.a` and `*.stamp`, `include/zodiac/plugin.h`, `include/zodiac/plugin.hpp`, `docs/` (copy of `docs/manual` when present; empty folder otherwise), `examples/` (one `hello` example project), `LICENSE`, `THIRD_PARTY_LICENSES`.
- `THIRD_PARTY_LICENSES` contains the full license texts of AngelScript (zlib), toml++ (MIT; only if linked into shipped binaries — it is, in `zodiac.exe`), LLVM lld/libc++/libunwind/compiler-rt (Apache-2.0 WITH LLVM-exception), mingw-w64 runtime (ZPL-2.1 and public domain notice), and doctest is not shipped (test only, not listed).
- Target `zodiac_zip` (from `ZodiacPackage.cmake`): `dist/zodiac-<version>-windows-x86_64.zip` from `dist/Zodiac` via `cmake -E tar cf ... --format=zip`, with `README.txt` from `packaging/README-zip.txt` (how to add `bin` to PATH, how to run `zodiac new`).

- [ ] **Step 1: Write the failing test** `tests/e2e/test_kit.py` (Windows only; env `ZODIAC_KIT_DIR` set by a CTest fixture that installs to `build/<preset>/kit-test/Zodiac`): kit layout files exist; no fixture module DLLs present; copying the kit to `<tmp>/Zodiac Kit ş/` and using its `bin/zodiac.exe` (with `PATH` reduced to the Windows system folders) for `new` + `build --no-window` + running the exe succeeds — proving the kit works from any folder without the dev toolchain.

- [ ] **Step 2: Run to verify it fails.**

- [ ] **Step 3: Implement** install rules, the license file and the zip target.

- [ ] **Step 4: Run tests to verify they pass.**

- [ ] **Step 5: Commit**

```
git add CMakeLists.txt cmake src modules packaging THIRD_PARTY_LICENSES tests
git commit -m "Install the kit layout and package the portable zip"
```

### Task 11: Setup wizard and CI packaging

**Files:**
- Create: `packaging/zodiac.iss`, `tools/build_installer.py`
- Modify: `tools/toolchain.json`, `bootstrap.py`, `tools/test_bootstrap.py`, `cmake/ZodiacPackage.cmake`, `.github/workflows/ci.yml`, `README.md`

**Interfaces:**
- `tools/toolchain.json` gains `innosetup` for `windows-x86_64`: the latest Inno Setup 6.x release at implementation time, its official download URL and SHA-256 (computed from the downloaded file and recorded); `bootstrap.py` installs it into `.tools/innosetup` with `/VERYSILENT /SUPPRESSMSGBOXES /CURRENTUSER /NORESTART /DIR=<abs>`, idempotent like the other tools.
- Target `zodiac_installer`: runs `.tools/innosetup/ISCC.exe` on `packaging/zodiac.iss` with `/DZodiacVersion=<version> /DKitDir=<dist/Zodiac>` → `dist/zodiac-setup-<version>.exe`.
- `zodiac.iss` per spec 8.7:
  - `PrivilegesRequiredOverridesAllowed=dialog` (all users / only me), `DefaultDirName={autopf}\Zodiac`, standard wizard pages only, `WizardStyle=modern`, no images carrying information.
  - Tasks (checkboxes, all checked): `addtopath` (HKLM or HKCU `Environment\Path` by install mode, added once, removed on uninstall), `associate` (`.zs` → ProgID `Zodiac.Script`, open command `"{app}\bin\zodiac.exe" run "%1"`), `buildmenu` (verb `zodiacbuild`, text `Build with Zodiac`, command `"{app}\bin\zodiacw.exe" build "%1"`), `shortcuts` (Start menu: Zodiac manual → `{app}\docs`, Zodiac examples → `{app}\examples`).
  - `ChangesEnvironment=yes`, `ChangesAssociations=yes`, `UsePreviousTasks=yes` (upgrade keeps options), `AppId` fixed GUID so upgrades install in place.
  - Uninstall removes files, PATH entry, ProgID, verb, shortcuts — nothing else.
- Signing: `zodiac.iss` uses `SignTool=zodiacsign` only when `/DSign=1` is passed; CI passes it only when the `ZODIAC_SIGNING_CERT` secret is non-empty (checked in a step that sets an env flag, since secrets cannot be used in `if:` directly).
- CI (`.github/workflows/ci.yml`):
  - `kit-windows` (needs `windows`): bootstrap, configure+build `windows-release`, install to `dist/Zodiac`, build `zodiac_zip` and `zodiac_installer`, upload both as artifact `zodiac-kit`.
  - `kit-clean-machine` (needs `kit-windows`, no checkout of `.tools`, no Python setup): download the artifact; (1) unzip to `C:\Users\runneradmin\Zodiac Kit ş`, run `zodiac.exe new hello`, `zodiac.exe build hello --no-window`, run `hello\release\hello.exe`, check exit 0; (2) run the setup with `/VERYSILENT /CURRENTUSER /TASKS="addtopath,associate,buildmenu"`, check `zodiac --version` in a new shell, `HKCU\Software\Classes\.zs` default value `Zodiac.Script`, then run the uninstaller silently and check the folder, PATH entry and ProgID are gone. Steps use PowerShell only.
  - `release` (on tags `v*`, needs `kit-clean-machine`, `permissions: contents: write`): create a GitHub release with the zip, the setup and a `SHA256SUMS` file.
- `README.md`: install section (setup or zip), `zodiac build`, double-click and right-click behaviour, SmartScreen note for the unsigned setup ("More info", then "Run anyway").

- [ ] **Step 1: Write the failing test**: `tools/test_bootstrap.py` case that the `innosetup` entry is present with version, URL and a 64-hex SHA-256, and that the installer command line uses `/CURRENTUSER` and `/DIR`.

- [ ] **Step 2: Run to verify it fails** (`python -I -m unittest discover -s tools -p test_bootstrap.py`).

- [ ] **Step 3: Implement** the pin, bootstrap step, `.iss`, CMake target, CI jobs and README section.

- [ ] **Step 4: Verify**: bootstrap tests pass; locally `cmake --build build/windows-release --target zodiac_installer` produces the setup; push the branch and confirm `kit-windows` and `kit-clean-machine` pass in CI.

- [ ] **Step 5: Commit**

```
git add packaging tools bootstrap.py cmake .github README.md
git commit -m "Build the setup wizard and test the kit on a clean CI machine"
```

- [ ] **Step 6: Owner check (not automatable):** the owner installs the setup from the CI artifact with NVDA, right-click-builds a `.zs` file, reads the result window, double-clicks a `.zs` file, and uninstalls. Findings go back into the plan's ledger.
