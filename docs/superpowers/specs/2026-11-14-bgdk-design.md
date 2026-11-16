# Zodiac Game Development Kit for the Blind (Design)

Date: 2026-11-14
Status: Approved by owner (2026-11-14)
Name: **Zodiac Game Development Kit for the Blind**. Short name and CLI command: **`zodiac`**.

## 1. Intent

BGT (Blastbay Game Toolkit) was the standard tool for building audio games for blind and visually impaired players. It was simple and pleasant to use, but it stopped evolving and fell behind modern technology. Its spiritual successor NVGT is open source and keeps BGT's AngelScript language, but it statically compiles a large set of dependencies into every game whether the game uses them or not (for example curl, miniaudio, ENet and Steam Audio / Phonon). Even a small offline game ships with networking and HRTF code it never calls, and binaries grow accordingly.

Zodiac is a community-owned, open-source game development kit for audio games that keeps the BGT feel (write AngelScript, press build, get a game) while following one rule:

**You never pay for what you don't use.** A game contains only the modules it actually uses.

The project lives on GitHub. Community members contribute via pull requests; maintainers review and merge code that meets the quality bar.

## 2. Goals

- **G1. Minimal core.** The core contains only what every audio game needs: the AngelScript engine and runtime, the platform layer (window, keyboard input, timing), file I/O, strings with full Unicode, math and random. Window and keyboard input are **on by default** because developers coming from BGT expect them, but they are built as separate internal parts that a project can switch off in its project file (e.g. for a headless Linux game server).
- **G2. Everything else is an optional module.** Networking, HTTP, Steam Audio, screen reader output, crypto, compression, database and BGT compatibility are separate modules. A game that does not declare a module contains none of its code.
- **G3. Hybrid linking.**
  - *Development mode:* modules are loaded as shared libraries (DLL / .so) so a script runs instantly with no link step.
  - *Release build:* modules the game uses are statically linked into a single executable by default.
  - *Per-module override:* any module can be marked `dynamic` for the release build; it is then shipped as a DLL/.so next to the executable instead of being linked in. Mixed setups are allowed (e.g. core and audio static, phonon dynamic).
- **G4. No compiler required for game developers.** A game developer installs the kit and builds releases without installing Visual Studio, GCC or any other toolchain. The kit ships prebuilt module libraries and a bundled linker (LLVM `lld`).
- **G2a. Public plugin system.** Third-party native plugins (C/C++) use exactly the same contract as built-in modules, so a plugin can be statically linked into a release executable or shipped as a DLL/.so, like any module. The plugin interface is a stable, versioned C ABI: a plugin built once keeps working with later Zodiac versions without recompiling. This is the main improvement over NVGT plugins, which bind directly to the engine's AngelScript build and break when it changes.
- **G2b. Optional `ffi` module** that lets scripts call functions in arbitrary native libraries without writing a plugin, like BGT's `library` object (e.g. `lib.call("int add(int, int)", 1, 2)`). Documented as unsafe: a wrong signature can crash the game.
- **G5. Modern, clean scripting API** in AngelScript, designed fresh rather than copying BGT's historical quirks.
- **G6. Optional `bgt_compat` module** that maps BGT's function and class names onto the modern API so existing BGT games can be ported with minimal changes. Games that don't use it don't carry it.
- **G7. Platforms: Windows and Linux**, with a platform-independent core so macOS and mobile can be added later without rewriting the architecture. The build system is target-aware from day one (every build names a target platform; prebuilt libraries are organized per target). Delivery order:
  1. Windows host builds Windows games (first release).
  2. Windows host builds Linux games, mainly game servers (immediately after; most developers work on Windows while servers run on Linux).
  3. The kit itself runs on a Linux host (later).
- **G8. Fully accessible tooling.** Every developer-facing tool (CLI, error messages, documentation) is usable with a screen reader. The developers are largely blind themselves.
- **G9. Contributor-friendly repository.** Clear module boundaries, CI on Windows and Linux, tests, a contribution guide and PR rules so outside contributors can work on one module without understanding the whole kit.

## 3. Non-goals (v1)

- macOS, Android and iOS (architecture must allow them later).
- Running the kit on a Linux host in the first release (see G7 delivery order).
- Migrating data created by BGT games (encrypted saves, accounts).
- NVGT game compatibility. The owner will bring NVGT games as later porting targets; an `nvgt_compat` module can follow the same pattern as `bgt_compat`.
- Graphics or 3D rendering beyond the minimal window needed for keyboard focus.
- A visual editor or GUI IDE.
- Running unmodified BGT scripts with 100% fidelity (`bgt_compat` aims for "minimal changes", not perfect emulation).
- Replacing AngelScript with another language.

## 4. Anti-goals (failure even if it technically works)

- **Bloat creeping back.** If a "hello world" release build grows because some convenience dependency slipped into core, the project has failed its main promise.
- **Requiring a native toolchain from game developers** for normal builds.
- **Inaccessible tooling or docs** (anything that needs sight to use: image-only docs, GUI-only configuration, unlabeled controls).
- **Silent behavior differences between dev and release builds.** A script that works in dev mode must behave identically in a release build.

## 5. Constraints

- Minimum platforms: Windows 10 or later, x86_64 only. Linux x86_64 with a conservative glibc baseline. No 32-bit builds.
- The scripting API (including the game-loop model, e.g. BGT-style blocking `wait()` vs. events) is written up as a proposal and approved by the owner before modules are implemented, because every module's registration code and docs depend on it.
- Engine written in C++ (C++23). Build system: CMake + Ninja with presets, clang only (see 8.8). AngelScript is embedded as the scripting language (zlib licensed, actively maintained).
- Each module must be buildable as both a static library and a shared library from the same source.
- Third-party library licenses must be respected per module. LGPL dependencies must support dynamic linking; the module manifest records the license so the build tool can warn about license-relevant static linking.
- Windows target toolchain must produce libraries that can be linked by the bundled `lld` without any Microsoft toolchain on the developer's machine. This implies a MinGW-w64 based target (e.g. llvm-mingw, UCRT) rather than MSVC static libraries, because the MSVC static runtime cannot be redistributed with the kit.
- Linux release builds link dynamically against glibc with a conservative baseline version so games run on common distributions. System audio and speech libraries (PulseAudio/PipeWire/ALSA, speech-dispatcher) are loaded at runtime with `dlopen` so they are never hard dependencies.

## 6. Approaches considered

| Approach | Summary | Trade-offs |
|---|---|---|
| 1. Shared libraries only | Each module is a DLL/.so; the build tool copies only the used ones next to the game. | Simple, fast, no linker needed. Game is never a single file; DLLs are easy to swap or tamper with. |
| 2. Static linking only | Modules ship as `.lib`/`.a`; bundled `lld` links only used modules into one executable. | Smallest possible single-file games. Slower iteration during development; no way to ship a module separately when licensing or updates require it. |
| **3. Hybrid (chosen)** | Dev mode loads modules as shared libraries; release builds link used modules statically by default, with per-module `dynamic` override. | Best of both. Each module must be built in two forms, which is more build-system work, automated once in CI. |

**Chosen: Approach 3**, with per-module static/dynamic choice in release builds. Reasons: it is the clearest differentiator from NVGT (tiny single-file games) while keeping fast iteration, and dynamic shipping remains available for LGPL libraries, hot-fixing one module without redistributing the whole game, and future modding/plugin support.

## 7. Decisions made with the project owner

- Community-owned, open source on GitHub, PR-driven.
- Engine in C++, games in AngelScript.
- Modular core; heavy dependencies (curl, Phonon, ENet, etc.) are optional.
- Windows + Linux first.
- Modern API plus optional `bgt_compat` module.
- Hybrid linking with per-module static/dynamic choice.
- Name: Zodiac Game Development Kit for the Blind (`zodiac`).
- Closed-source and commercial games must be allowed. Kit license: **zlib** (same as AngelScript). Dependencies: no GPL; LGPL only as `dynamic` modules.
- Window and keyboard stay in core and are on by default; a project setting can exclude them.
- Both a stable public plugin system (G2a) and an `ffi` module (G2b).
- First porting target is Blood Bath; its `GameEngine.dll` is replaced by porting the existing `map_system` NVGT plugin as the `map` module.
- Target order: Windows→Windows first, Windows→Linux (servers) right after, Linux host later.
- Strong, opt-in protection for scripts and assets (see 8.6). The owner wants it as hard to break as realistically possible.

## 8. Architecture

### 8.1 Components

- **Runtime core (`zodiac_core`)**: AngelScript engine setup, module registry, script loading (source in dev mode, bytecode in release), platform layer, file I/O, Unicode strings, timers, math, random.
- **Modules (`zodiac_mod_<name>`)**: each module exposes one C entry point that registers its types and functions with the AngelScript engine, plus a manifest.
- **CLI tool (`zodiac`)**: `zodiac run <script>` (dev mode), `zodiac build` (release), `zodiac new` (project scaffold), `zodiac modules` (list available modules and their licenses).
- **Release stub**: a prebuilt minimal runtime object that, after linking, loads embedded bytecode and starts the game.

### 8.2 Module contract

Each module provides:

- A manifest (name, version, dependencies on other modules, license, allowed link modes, supported platforms).
- A registration entry point with a stable C ABI, e.g. `zodiac_module_register(zodiac_host*)`, called before script compilation.
- An optional shutdown entry point.
- Its AngelScript API documentation in an accessible text format.

The same contract is published for third-party plugins (G2a). ABI rules:

- Only C types cross the boundary. No AngelScript classes (`asIScriptEngine` etc.), no `std::string`, `std::vector` or other C++ library types, no exceptions.
- `zodiac_host` is a versioned table of C function pointers (registration of types, functions, methods and properties; access to strings, arrays and dictionaries through opaque handles; logging; errors). New functions are only appended; a plugin declares the minimum host ABI version it needs and the host refuses to load plugins that need a newer one.
- A header-only C++ convenience layer on top of the C ABI keeps writing plugins as easy as NVGT plugins (registering a C++ function with a script declaration in one line), while compiling down to the stable C interface.
- Plugins are listed in the project file exactly like modules, with the same `static` / `dynamic` choice.

### 8.2a `ffi` module

Calls into arbitrary native libraries from scripts using a runtime call library (e.g. dyncall or libffi, both permissively licensed). Signatures are parsed from a C-like declaration string as in BGT. Supports the common C types (integers, floats, doubles, pointers, `char*` strings) and the platform's default C calling convention, plus `stdcall` on Windows. Only 64-bit libraries on 64-bit games; 32-bit DLLs cannot be loaded.

In a static release build, module registration must not depend on generated C++ code (no compiler is available). Proposed mechanism: each module's static library contains a registration record placed in a dedicated linker section; the core walks that section at startup. Linking a module's library is therefore enough to register it. In dev mode the same record is found by loading the shared library and looking up the entry point.

### 8.3 Project file

Each game has a project file listing the modules it uses and their release link mode, for example:

```
[modules]
static = ["core", "audio", "screen_reader"]
dynamic = ["phonon"]
```

Default-on core parts can be switched off, for example for a headless server:

```
[core]
exclude = ["window", "keyboard"]
```

Modules are declared explicitly (not auto-detected from script source) because AngelScript needs all types registered before compilation, and explicit declaration makes builds deterministic and reviewable. Module dependencies are resolved automatically (declaring `phonon` pulls in `audio`).

### 8.4 Build flow (release)

1. Resolve declared modules and their dependencies.
2. Compile the scripts with the same module set registered, and save AngelScript bytecode.
3. Link the release stub, core and every `static` module library with bundled `lld` into one executable.
4. Embed bytecode and game assets in the executable (or alongside it in a pack file).
5. Copy `dynamic` modules next to the executable.

Dev mode (`zodiac run`) uses the exact same module set and registration order, guaranteeing identical behavior (see anti-goals).

### 8.5 Initial module set (v1)

| Module | Purpose | Notes |
|---|---|---|
| core | Engine, platform layer, file, string, timer, math | Always present; window and keyboard parts on by default, excludable |
| audio | Sound playback, panning, pitch, volume, sound pools, streaming | Backend: miniaudio, behind an internal backend interface so other backends can be added |
| screen_reader | Speech and braille output via the active screen reader | Windows: NVDA, JAWS, SAPI fallback. Linux: speech-dispatcher |
| phonon | HRTF and 3D audio via Steam Audio | Extends audio; large, so typically `dynamic` |
| network | Real-time game networking via ENet | |
| http | HTTP/HTTPS requests via curl | |
| crypto | Hashing and encryption | |
| compression | Data compression | |
| db | Embedded SQLite database | |
| map | Tiles, walls, stairs, zones, safe/restricted zones, climate, effects, pathfinding, sound occlusion | Ported from the existing NVGT plugin `D:\nvgt\plugin\map_system\map_system.cpp` (already Windows/Linux compatible) onto the Zodiac plugin ABI |
| ffi | Call functions in arbitrary native libraries from scripts | See 8.2a |
| bgt_compat | BGT names mapped onto the modern API | Depends on whichever modules the mapped names need |

### 8.6 Script and asset protection (opt-in)

Goal: make extracting a release game's scripts and assets as hard as realistically possible, while staying honest that a game running on the player's machine must decrypt its data in memory, so no scheme is mathematically unbreakable. Documentation must say "very hard", never "impossible".

Layers, all enabled by one project setting:

- **Authenticated encryption** of bytecode and packed assets (AES-256-GCM or XChaCha20-Poly1305).
- **Unique random key per game build**, never a kit-wide key, so breaking one game reveals nothing about others.
- **Key obfuscation**: the key is split and reconstructed at runtime, never stored as a contiguous constant.
- **Bytecode stripping**: no source text, no debug info, no symbol names beyond what AngelScript strictly needs.
- **Integrity checks**: tampered executables or packs refuse to run.
- **In-memory hygiene**: decrypted data lives only as long as needed and is wiped after use.

Constraints: the decryption code must stay small and lives in the release stub only when protection is enabled, so unprotected games carry none of it. No aggressive anti-debugging or packing tricks that trigger antivirus false positives; screen-reader users are hit especially hard by antivirus blocking their games.

### 8.7 `bgt_compat` v1 target: Blood Bath

The first porting target, and the definition of "done" for `bgt_compat` v1, is the online game **Blood Bath** (source in `Blood bath source/Blood bath/`): a BGT client (`bloodbath.bgt` + `includes/`) and a BGT server (`server/bbserver_b.bgt` + `server/includes/`). Both must run on Zodiac; the server should run on Linux (delivery step 2 in G7).

BGT surface used by the game (from a scan of all `.bgt` files, most used first): `dictionary`, `key_pressed`/`key_down`, `file` and `directory_*` / `file_*` functions, `string_split`, `timer`, `alert`, `sound`, `wait`, `exit`, `serialize`/`deserialize`, `string_encrypt`/`string_decrypt`, `sound_pool` (custom, in `includes/sp.bgt`), `screen_reader_*`, `run`, `clipboard_*`, `tts_voice`, `dynamic_menu`, `input_box`, `pack` with `set_sound_storage` and `set_sound_decryption_key`, `library` (native DLL calls), `network` / `network_event`, `url_get`, `show_game_window`, `find_files`, `string_hash`, `instance`.

Native DLL dependencies (all 32-bit Windows, which a 64-bit Windows/Linux kit cannot load):

- `GameEngine.dll`: map functions used by the game (`get_tile_at`, `get_zone_at`, `spawn_platform`, `spawn_zone`, `clear_map`) plus `ftp_connect`.
- `terlark.dll` (server): message dialogs and FTP upload.
- `nvdaControllerClient32.dll`: replaced by the `screen_reader` module.

These are replaced, not loaded: there is no source for `GameEngine.dll`, and its map functions are covered by the `map` module (ported from `map_system`). The port maps Blood Bath's calls (`get_tile_at`, `spawn_zone`, ...) onto `map` functions. The few `terlark.dll` uses (message dialogs, FTP upload) are reimplemented or dropped. The `ffi` module exists for other games that need direct DLL calls, but it cannot load these 32-bit DLLs.

Existing assets: the original `.ogg` files are present, so `sounds.dat` is repacked in Zodiac's own format rather than decoding BGT's encrypted pack.

Performance rule: every `bgt_compat` name is registered natively against the same C++ implementation as its modern counterpart (e.g. BGT's global `string_split(s, delim, full)` and the modern method `s.split(delim)` bind to one native function). Compat functions are never implemented as AngelScript wrappers, so ported code runs exactly as fast as new code.

Existing player data does not need to be migrated. Blood Bath is a medium-difficulty first target, not a live service to carry over, so `string_encrypt`/`string_decrypt` do not have to be byte-compatible with BGT's output; they only have to behave consistently within Zodiac.

### 8.8 Building Zodiac itself (contributor experience)

Building the engine must be as easy as running one command, so outside contributors can open PRs without fighting the toolchain. Decided with the owner: **CMake + Ninja + clang**, replacing the SCons + MSVC setup the owner uses for NVGT.

- **One-command setup:** `python bootstrap.py` downloads pinned versions of CMake, Ninja and (on Windows) llvm-mingw into `.tools/` inside the repository, verifying SHA-256 checksums. Nothing is installed system-wide; Visual Studio is not needed.
- **One-command build and test:** `cmake --workflow --preset windows` (or `linux`) configures, builds and runs the tests.
- **Single official compiler:** clang everywhere (llvm-mingw UCRT on Windows, clang on Linux). Contributors, CI and the `lld` shipped in the kit use the same pinned version.
- **Dependencies from source, pinned by version and hash** via CMake `FetchContent`; no vcpkg. Libraries that are only distributed as binaries (Steam Audio) are downloaded as their official prebuilt releases.
- **Build only what you work on:** every module is its own CMake target (`cmake --build --preset windows-debug --target zodiac_mod_audio`).
- Editors that understand CMake presets (Visual Studio, VS Code, CLion) open the project directly.

### 8.9 Portability rules (macOS, Android, iOS later)

macOS and mobile are not in v1, but these rules apply from the first line of code so adding them later is additive:

1. Platform-specific code lives only in `src/platform/<os>/`; no `#ifdef _WIN32` scattered through core or modules.
2. The engine is an embeddable library with a start / run-frame / stop lifecycle; `zodiac.exe` and the release stub are thin front ends. Android has no `main()`, and its front end will drive this lifecycle.
3. Core never writes to stdout/stderr directly; all output goes through diagnostic sinks (8.10), so Android can route it to logcat.
4. Game loop (decided in the Script API proposal): BGT-style `wait()` stays available, but the engine must not block the platform's main thread. Scripts run as coroutines that `wait()` suspends.
5. File access goes through a virtual file system (Android assets live inside the APK), introduced with core file I/O.
6. No x86-only code or assembly; arm64 (Apple Silicon, Android) must remain buildable.
7. Static module registration (release builds) must be designed for COFF, ELF and Mach-O. iOS forbids loading code at runtime, so it will be static-only.

Concurrency:

- `try` / `catch` is available from the start (AngelScript ≥ 2.34). Exceptions raised by plugins through `set_exception` are catchable by scripts.
- Coroutines come with the Script API (plan 2); they are the basis of the non-blocking `wait()`.
- Threads: AngelScript is built with thread support from the start, and the plugin ABI documents thread rules from v1 (registration only on the main thread inside `register_module`; registered functions may be called from any script thread and must be thread-safe or document otherwise). Script-level `thread`, `mutex`, `atomic` and ready-made async operations (e.g. background download with a completion callback) are designed in the Script API proposal and implemented after the core platform.

### 8.10 Diagnostics output

Every error and log message, including those raised before a script runs (module load failures, ABI mismatches, bytecode errors), goes through the same diagnostic sinks. The sinks are set up before anything else is loaded. NVGT shows these errors only in a window, which cannot be read from a console and is awkward with a screen reader; Zodiac never does that by default.

Sinks, combinable:

- **console:** `auto` (default) writes to the console when the game was started from a terminal or command prompt (on Windows a GUI-subsystem game attaches to the parent console), `always` (opens a console if none exists), `never`.
- **file:** optional log file path; when set, every message is appended there as UTF-8 lines, in addition to the console.
- **dialog:** `auto` (default) shows one accessible message box only when there is no console and no log file, so errors never vanish silently; `always`; `never`.

Configured in the project file and overridable at launch (e.g. `--log-file <path>`, `--console`), so players can produce a log for the developer without a rebuild:

```
[diagnostics]
console = "auto"
file = "logs/zodiac.log"
dialog = "never"
```

The dev CLI (`zodiac run`) always writes to its console; the file sink applies there as well.

## 9. Success criteria

- A "hello world" release build (core + audio + screen_reader, playing one sound and speaking text) is a single executable **at most 5 MB** (raised to 5 MB by the owner, 2026-11-17), with no networking, HTTP or Phonon code inside (verifiable by symbol inspection). CI enforces the size limit and fails the PR when it is exceeded.
- A clean Windows machine and a clean Linux machine can install the kit and produce a release build with no other toolchain installed.
- Blood Bath (client on Windows, server on Linux) is ported using `bgt_compat` with minimal, documented changes, and a full multiplayer session works end to end.
- CI builds and tests every module on Windows and Linux on every PR.
- A blind developer can complete the getting-started guide using only a screen reader.

## 10. Left to the builder

- Exact CLI flags, project file format details and file naming.
- Choice of window/input library for the platform layer (e.g. SDL3 with unused subsystems disabled vs. a thin custom Win32/X11/Wayland layer), as long as the core stays small.
- Bytecode and asset embedding method per platform (appended payload vs. PE resources / ELF section), including its interaction with Windows code signing.
- Concrete libraries for crypto and compression.
- Exact layout of the `zodiac_host` function table and the C++ convenience layer, within the rules in 8.2.
- Test framework and CI layout.
- Order in which modules are built after core and audio.
