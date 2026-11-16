# Zodiac Script API — Design

Status: approved by the owner (2026-11-16). Plan 2 of the series in `docs/superpowers/specs/2026-11-14-bgdk-design.md` (the "foundation spec"). That spec requires the scripting API, including the game-loop model, to be approved before any module is implemented, because every module's registration code and documentation depend on it.

## 1. Intent

Fix the shape of the language every Zodiac game is written in: how names look, how the game loop runs, how work runs in parallel without the crashes NVGT users hit, how errors are reported, and how developers learn all of it. The owner's experience drives the decisions:

- BGT-style blocking loops (`while(true){ wait(5); ... }`) are how blind developers write games today. Blood Bath (the first porting target) uses only that pattern: every menu, dialog and game screen is its own `wait(5)` loop, and nested modal loops keep calling `netloop()` so the world stays alive. That style must keep working, without freezing the window.
- The owner wanted to run bot AI on its own thread in a server game (`D:\tg`) so bots never cause lag, and could not, because NVGT threads either died silently or crashed the whole program. Zodiac's threading must make that use case safe and must never fail silently.
- Engine warnings must never be spoken by the screen reader or shown in a window during development; they belong in the console and the log file.

## 2. Goals

- **S1.** One naming style across all modules (8.1), with `bgt_compat` names bound to the same native functions.
- **S2.** BGT-style `wait()` loops work unchanged, and the platform main thread is never blocked (8.2).
- **S3.** Heavy work (bot AI, projectile simulation) can run on other CPU cores in two ways: isolated workers that cannot corrupt each other's state (8.3), and free threads with locks for developers who want shared state (8.4).
- **S4.** No silent failure anywhere: a crashed worker or thread, an uncaught exception, or a call from the wrong thread is always reported with file and line (8.6, 8.8).
- **S5.** A dev-mode race checker that finds unlocked concurrent access to arrays and dictionaries, at zero cost in release builds (8.7).
- **S6.** Log verbosity is a single level the developer sets in the project file, on the command line, or from game code at runtime (8.9).
- **S7.** A tutorial manual that teaches both AngelScript and the engine, plus a generated API reference, both in English and Turkish and readable with a screen reader (8.10).

## 3. Non-goals

- Event-driven or callback-only game loops replacing `wait()`. Callbacks exist for async operations only (8.5).
- Sharing script objects (class instances, script arrays, globals) between workers. Workers exchange copies.
- Automatic restart of crashed workers or threads.
- Engine-generated speech for warnings and errors, and engine dialogs by default (the `dialog` sink is opt-in, see 8.9).
- Worker-to-worker direct channels in v1. All messages go through the script that owns the worker. Can be added later without breaking scripts.
- Translating the manual into languages other than English and Turkish in v1.

## 4. Anti-goals

- **NVGT's thread experience again:** a thread that dies with no trace, or a data race that takes down the whole program with no hint of where.
- **The screen reader as a log channel:** any engine diagnostic that is spoken or shown in a window during development.
- **Docs that drift:** a reference that describes functions that no longer exist, a tutorial example that no longer compiles, or a Turkish page silently older than its English source.
- **Parallelism that costs single-threaded games:** a game that never starts a worker or thread must not pay for locks, checks or extra threads.

## 5. Constraints

- Everything in the foundation spec still applies (C++23, clang, AngelScript 2.39.0 built with thread support, plugin ABI v1 append-only, portability rules in its 8.9, diagnostics sinks in its 8.10).
- Plugin ABI changes needed here (thread-affinity flags, 8.6) are additive: new entries appended to `zodiac_host`, `abi_version` stays 1, older plugins keep loading.
- The platform main thread must never block on script code (foundation spec 8.9 rule 4).
- Release builds behave identically to dev builds except for the dev-only checks this document names explicitly (race checker, yield watchdog); by default those only add diagnostics. The one exception is `race_check = "stop"`, an explicit developer opt-in that stops a dev run at the first race.

## 6. Approaches considered

**Naming style**

| Option | Example | Trade-off |
|---|---|---|
| A. Flat BGT-style globals | `speak("x")`, `string_split(s, ",")` | Familiar and short; hundreds of names in one pile, module ownership invisible. |
| B. Everything namespaced | `screen_reader::speak("x")`, `keyboard::pressed(key::space)` | Orderly; long, and `::`-heavy code is tiring to read with a screen reader. |
| **C. Mixed, NVGT-like (chosen)** | `speak("x")`, `s.split(",")`, `net::client` | Short names for everyday calls, methods on value types, prefixes for heavy modules. Module use is visible in the code. |

**Parallelism**

| Option | Summary | Trade-off |
|---|---|---|
| A. Coroutines only | Bots as coroutines on the main thread. | Cannot crash; one core only, heavy AI still lags. |
| B. Free threads + mutex (NVGT-like) | Shared globals, explicit locks. | Flexible; data races remain possible. |
| C. Isolated workers | Separate script, own globals, message passing. | Structurally race-free; data must be sent explicitly. |

**Chosen: B and C together, plus coroutines.** C is what the manual recommends for heavy work; B is there for developers who want shared state, with the safeguards in 8.4, 8.6 and 8.7.

**Map data for workers**

| Option | Summary | Trade-off |
|---|---|---|
| 1. Event messages | Worker keeps its own map copy; main sends "door 3 open". | Two copies; forgetting an update makes bots walk into closed doors. |
| **2. Shared native object (chosen)** | The native `map` module object is shared; the module locks internally. | One copy, changes visible immediately, no locks in script code. |

**Crash behavior in a released game**

| Option | Summary |
|---|---|
| **A. No engine dialog by default (chosen)** | Errors are logged by level (8.9); the developer may opt in to the `dialog` sink; the game handles what it wants with `try`/`catch`. |
| B. Accessible system message box | Engine shows "the game closed because of an error". Rejected by the owner. |

## 7. Decisions made with the project owner

- Naming style C (NVGT-like).
- Both free threads (B) and isolated workers (C).
- Native engine objects such as `map` may be shared across threads and lock internally; script objects never are.
- Dev-mode race checker: runtime, dev builds only, console and log only, never spoken or shown, one report per location, configurable `warn` / `stop` / `off`.
- No engine dialog by default; the `dialog` sink stays configurable and is off unless the developer enables it. Developers handle errors with `try`/`catch`.
- Log verbosity: one `verbose` level, 0 = nothing, 1 = crashes and errors, 2 = plus warnings, 3 = everything. Readable and writable from game code.
- A tutorial manual (language and engine) and an API reference, in English and Turkish.

## 8. Design

### 8.1 Naming conventions

- All names are `snake_case`; constants are `UPPER_SNAKE` only inside `bgt_compat`. Modern enums use namespaced lower-case values (`key::space`).
- **Everyday calls are short globals**, provided by core and the always-small modules: `wait`, `yield`, `speak`, `key_pressed`, `key_down`, `random`, `timer`, `exit`.
- **Value types carry their operations as methods:** `s.split(",")`, `s.replace(a, b)`, `s.to_number()`, `a.insert_last(x)`, `d.exists(k)`.
- **Heavy or optional modules use their module name as namespace:** `net::`, `http::`, `crypto::`, `db::`, `map::`, `ffi::`, `compression::`. A game reading `net::client` can see it depends on `net`.
- Every `bgt_compat` name (e.g. `string_split(s, delim, full)`) registers against the same native function as its modern form, per the foundation spec's performance rule. Where BGT semantics differ, the compat binding adapts arguments natively, never via an AngelScript wrapper.
- Each module's names are listed in its manifest so the build tool and the reference generator know who owns what.

### 8.2 Game loop and coroutines

- The engine owns the platform main thread and runs a frame loop: pump OS events (window, keyboard), advance audio, deliver async completions (8.5), then resume every runnable script coroutine on that thread.
- `void main()` / `int main()` runs as a coroutine. `wait(ms)` suspends it until the time has passed; the frame loop keeps running in the meantime, so the window never freezes. `wait(0)` and `yield()` give up the rest of the frame.
- Blood Bath style code works unchanged, including nested modal loops: a menu's `while(true){ wait(5); ... }` inside the main loop is just the same coroutine waiting again.
- Additional coroutines: `coroutine c(func)` starts `func` on the main thread; it interleaves with `main` at every `wait()`/`yield()`. `c.running`, `c.error` (8.8), `c.cancel()`. Coroutines share globals safely because only one runs at a time.
- `key_pressed()` and friends report state as of the last frame pump, matching BGT polling semantics.
- **Timing (my call):** the frame loop is not tied to vsync or a fixed tick. It sleeps until the earliest coroutine deadline or the next OS event, using high-resolution timers (on Windows a high-resolution waitable timer), so `wait(5)` means about 5 ms as in BGT. Target accuracy: within 1 ms on an idle machine. Audio runs on its own device thread and is not paced by the frame loop.
- **Headless (my call):** a game with `window` excluded (`[core] exclude`), such as a Linux server, runs the same frame loop without a window or keyboard pump. Scripts behave identically.
- **Where `wait()` can suspend (my call):** AngelScript can only suspend a context whose call stack is pure script. Calling `wait()`/`yield()` from a script function that native code is calling (an array `sort` comparator, a plugin callback) raises a catchable exception: `"wait() cannot be used inside a callback called by native code"`. It never blocks the frame loop.
- **Yield watchdog (dev only):** if a coroutine runs more than a configurable time (default 250 ms) without waiting, a warning with file and line goes to the log. Nothing is killed; release builds don't run the watchdog.
- On a worker or free thread, `wait(ms)` simply sleeps that thread.

### 8.3 Workers (isolated, recommended for heavy work)

```
worker bots("bots.zs");
void main(){
bots.send(map_name);
while(true){
wait(5);
netloop();
bots.send(snapshot());
dictionary@ cmd;
while(bots.receive(cmd)) apply_bot_command(cmd);
}
}
```

```
void main(){
map@ m=map::open(worker::receive_string());
while(true){
dictionary@ world=worker::receive();
worker::send(think(m,world));
}
}
```

- A worker is a separate script file (path relative to the project), compiled into its own module with its own globals, running on its own OS thread with its own AngelScript context. It sees the same project modules as the main script. In release builds worker scripts are bundled and protected exactly like the main script.
- **Messages are copies.** Sendable types: primitives, `string`, `array<T>` and `dictionary` of sendable types (deep-copied), and shareable native handles (8.6). Sending anything else (script class instances, function handles, non-shareable native objects) raises a catchable script exception naming the type.
- Owner side: `w.send(value)` never blocks; `w.receive(out)` never blocks and returns `false` when the queue is empty; `w.receive(out, timeout_ms)` blocks up to the timeout (on the main thread it waits as a coroutine, never blocking the frame loop).
- Worker side: `worker::receive()` blocks until a message arrives (no CPU use while idle); `worker::send(value)`.
- Each worker processes messages in send order. Results reach the owner on the next `receive`, so the documented latency is at most one owner frame when the worker keeps up; send early in the frame and receive late to usually get results in the same frame.
- **A crashed worker never takes the owner down.** An uncaught exception ends the worker; the error (message, file, line, call stack) goes to the log and arrives at the owner as an error message from `receive` (and in `w.error`). `w.running` reports state. `w.restart()` is explicit; there is no automatic restart.
- Starting more workers and threads than logical CPU cores logs a warning (level 2).

### 8.4 Free threads (shared state, for those who want it)

```
mutex players_lock;
void bot_think(){
while(true){
{
lock l(players_lock);
update_bots();
}
wait(10);
}
}
void main(){
thread t(bot_think);
...
}
```

- `thread t(func)` runs `func` of the same module on a new OS thread with its own context. It shares the module's globals.
- Tools: `mutex`, `lock` (scope guard that releases on leaving the block, including via exception), `atomic_int`, `atomic_bool`, and a thread-safe `queue<T>` with blocking and non-blocking pop.
- `t.join()`, `t.running`, `t.error`. An uncaught exception in a thread ends that thread only, is logged with thread name, file and line, and is readable from `t.error`. Never silent.
- Data races on shared script objects remain possible by nature; the race checker (8.7) is the safeguard. The manual teaches workers first and threads second.
- AngelScript's thread support (`asPrepareMultithread`, per-thread contexts, `asThreadCleanup` on thread exit) is managed by the engine; scripts never see it.

### 8.5 Async operations and their callbacks

- Engine-provided background work (e.g. `http::download(url, on_done)`, async file or DB operations) runs on engine-owned threads.
- **A completion callback always runs on the thread that started the operation**, at that thread's next `wait()`/`yield()` (the frame loop's delivery step on the main thread). Never on an arbitrary thread. So callbacks touch game state without locks.
- An operation started from a worker delivers its callback inside that worker.
- **Each callback starts as its own coroutine on that thread (my call),** so a callback may itself `wait()`. Callbacks start in completion order; once a callback waits, it interleaves with other coroutines like any coroutine.

### 8.6 Thread affinity of native functions

- Every registered function and method carries an affinity: `any` (callable from every thread; must be thread-safe) or `main` (main thread only, e.g. window, keyboard focus, some audio device calls).
- Calling a `main` function from a worker or thread raises a catchable script exception (`"<function> can only be called from the main thread"`); it never crashes.
- Plugins declare affinity through new `zodiac_host` entries appended to the v1 table (additive). Functions registered through the existing v1 entries default to `any`, matching the ABI v1 rule that registered functions must be thread-safe unless documented otherwise.
- **Shareable native objects:** a module can mark a type shareable (e.g. `map`). Such handles may be sent to workers and used from several threads; the module guarantees internal locking. The `map` module uses a reader-writer lock: path finding and tile queries read, door/wall changes write.
- Affinity and shareability appear in the generated reference (8.10).

### 8.7 Dev-mode race checker

- Runtime check, dev builds (`zodiac run`) only. Release builds compile it out completely.
- Instruments script arrays and dictionaries. **Definition (my call):** two accesses from different threads, at least one a write, that are not ordered by happens-before. Happens-before edges come from the engine's own synchronization: `mutex`/`lock`, atomics, `queue<T>` push/pop, `thread` start and `join`, worker start and messages. So handing an array over through a `queue` or a `join` is silent, and the result does not depend on timing: a seeded unlocked access is reported deterministically. Coroutines on the same thread never trigger it.
- Report goes to the diagnostic sinks only (console and log file), never to the screen reader or a window:

```
warning: unlocked concurrent access to array 'players'
  thread 'bot_think' at bots.zs:42 (write)
  thread 'main' at server.zs:118 (read)
```

- One report per pair of source locations, however often it repeats.
- `[diagnostics] race_check = "warn"` (default: report and continue), `"stop"` (report, then stop the program with exit code 1), `"off"`.

### 8.8 Errors and exceptions

- `try`/`catch` is available everywhere. Plugin errors raised via `set_exception` and engine errors (wrong-thread calls, unsendable types, file and network failures) are catchable script exceptions with a readable message.
- An uncaught exception in `main` ends the program: the error with call stack is logged at level 1 and the exit code is 1 (foundation spec).
- Uncaught exceptions in coroutines, workers and threads end only that unit and are logged at level 1; the owner can read them from `.error`.
- Native crashes (access violation, abort) are caught by a platform crash handler that writes a crash entry (signal, module, script file and line if known) to the sinks at level 1 before the process ends.
- No engine dialog appears unless the developer enabled the `dialog` sink (8.9), and there is no `on_crash` hook; developers decide what players hear by catching errors in script.

### 8.9 Log verbosity and diagnostics changes

| `verbose` | Written to the sinks |
|---|---|
| 0 | nothing |
| 1 | crashes and errors |
| 2 | plus warnings (race checker, watchdog, core-count warning, clamped values) |
| 3 | plus information (module loaded, worker started, async operation finished) |

- Set in `zodiac.toml` (`[diagnostics] verbose = 2`), on the command line (`--verbose 3`, overrides the file), and from game code: `verbose` is a global `int` property, readable and writable at runtime from any thread (atomic).
- Defaults: 2 in dev mode, 1 in release builds.
- Out-of-range values are clamped to 0–3 and a warning is logged (if the new level allows it); the script never fails.
- **Change to the foundation spec 8.10 (approved by the owner):** the `dialog` sink stays a developer choice (`[diagnostics] dialog = "auto"`, `"always"` or `"never"`, and a launch flag), but its default becomes `never`, so the engine shows no window unless the developer turns it on. When enabled, the dialog shows messages at the current `verbose` level. To keep errors from vanishing in a released game started by double-click, release builds without a configured `file` write to `<game name>.log` next to the executable (falling back to the per-user data directory if that is not writable). Level 0 still writes nothing, by the developer's explicit choice.

### 8.10a Program shutdown (my call)

When `main` returns, `exit()` is called, or `main` ends with an uncaught exception:

1. Every other coroutine is cancelled.
2. Every worker and free thread gets a stop request. Blocking engine primitives (`worker::receive()`, `queue` pop, `wait`, `w.receive(out, timeout)`) wake immediately by raising a non-catchable stop in that unit, so they unwind normally and release `lock` guards.
3. The engine joins them with a timeout (default 2000 ms, `[runtime] shutdown_timeout_ms`).
4. If all stopped: normal teardown (foundation spec order: discard modules, GC, plugin `shutdown_module`, unload).
5. If some unit is still running (e.g. a busy loop that never calls a blocking primitive): each is logged at level 1 by name, file and line, then the process ends immediately without running plugin shutdown, because the ABI promises plugins no calls after shutdown starts.
6. Exit code: the value `main` returned or passed to `exit()`; 1 for an uncaught exception in `main`, even when step 5 happens.

An unjoined `thread` object going out of scope does not stop or block anything; the thread keeps running until it ends or shutdown stops it.

### 8.10 Manual and API reference

- **Tutorial manual** (`docs/manual/en/`, `docs/manual/tr/`): teaches AngelScript from zero (variables, functions, arrays, dictionaries, classes, handles, `try`/`catch`) and then the engine (first game loop, `wait()`, sound, screen reader, keyboard, menus, networking, coroutines, workers, threads). Includes a "coming from BGT" and a "coming from NVGT" chapter.
- **Every example in the manual is a real file** compiled and run by CI, so the manual never contains broken code.
- **API reference** generated, never hand-written: from each module's registered declarations plus per-declaration descriptions in English and Turkish kept beside the module source. Each entry shows declaration, parameters, return value, owning module, thread affinity and shareability.
- CI fails when a registered declaration lacks a description in either language, and flags a Turkish page or description older than its English source (and vice versa).
- Format: Markdown in the repository, plus a generated plain HTML version. Heading-navigable, no images, no information conveyed only visually.

## 9. Success criteria

- Blood Bath-style code (nested `wait(5)` loops, menus inside the game loop) runs unchanged, and the window stays responsive during every `wait()`.
- A sample server with bots in a worker sharing a `map` handle runs with the main frame time unaffected by bot path-finding load (measured in a test).
- A crashing worker, a crashing thread and an uncaught exception in a coroutine each produce a log entry with file and line, and the main program keeps running.
- Calling a main-thread-only function from a thread produces a catchable exception, not a crash.
- The race checker reports a seeded unlocked access test with both locations, exactly once; release builds contain no checker code.
- `verbose` set from the project file, the command line and script code each takes effect, and out-of-range values are clamped with a warning.
- A blind developer can complete the getting-started tutorial with only a screen reader, in English and in Turkish.

## 10. Left to the builder

- Exact coroutine implementation (AngelScript context suspension per coroutine, scheduling order within a frame).
- Message queue implementation and the exact `receive` overload set (typed overloads vs. a variant message type), within 8.3.
- How the race checker instruments arrays and dictionaries (wrapped add-ons, shadow ownership state).
- Crash handler mechanism per platform.
- Layout of per-module description files and the reference generator.
- Watchdog default and its configuration key name.

## 11. Scope and timing (my call)

- Plan 2 delivers this approved document only. No runtime code is written in plan 2.
- Plan 4 (core platform) implements the core runtime parts: frame loop (with window and keyboard pump, and headless), coroutines and `wait`, workers, threads and sync primitives, affinity ABI entries, race checker, watchdog, `verbose`, crash handler, shutdown, and the 8.9 diagnostics change. It also builds the reference generator and the docs CI checks.
- Each later module plan implements that module's API in the 8.1 style, with its English and Turkish descriptions and its manual chapter, in the same plan.
- The manual chapters are written in both languages by the implementation work; the owner reviews the Turkish text.
