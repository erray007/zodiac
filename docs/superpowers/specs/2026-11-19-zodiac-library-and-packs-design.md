# Zodiac Library and Packs (Plan 4b) — Design

Status: approved by the owner on 2026-11-19. This is Plan 4b of the series in `docs/superpowers/specs/2026-11-14-bgdk-design.md`.
- It implements sections 8.5 to 8.7 of `docs/superpowers/specs/2026-11-16-zodiac-core-types-design.md` (maths, random, time, files and the VFS). Those sections were approved on 2026-11-16 and are binding here unchanged, except where this document says otherwise.
- It adds encrypted pack files. The roadmap approved on 2026-11-18 moved them from plan 7 into Plan 4b, together with the VFS.

## 1. Intent

Give game scripts the library a BGT or NVGT developer expects next to strings, arrays and dictionaries: maths, random numbers, timers, dates and files.

Let games ship their files in encrypted packs.
- `zodiac build` makes the pack automatically for release.
- Scripts can make and read their own packs at run time: downloadable maps, mods and tools.

Plugins keep full freedom to use their own pack systems. The owner's `tg_audio` plugin, for example, keeps `king_pack` and `king_virtual_pack`.

## 2. Goals

- **L1.** Everything in core-types spec 8.5 to 8.7, with the names and behaviour written there.
- **L2.** A script `pack` object that can create, add to, remove from, extract, list and compact an encrypted pack, and read files from it. Its scope matches the operations the owner uses in tg_audio's packs.
- **L3.** A pack mounted into the VFS. After mounting, `read_file`, the `file` object and every helper read files from the pack by their normal paths, with no change to the script.
- **L4.** Release builds pack the project's declared assets into an encrypted pack automatically.
  - The key is random for each build and is never visible to scripts.
  - The project can choose which assets go in, or turn packing off.
- **L5.** Plugins can read files through the VFS, packs included, by an appended plugin-ABI entry. They never have to.
- **L6.** Speed and behaviour are measured and enforced.
  - Maths and random calls are no slower than the AngelScript sample add-on or the plain C++ equivalent a binding would use.
  - Reading from a mounted pack is measured against reading the same file from disk.

## 3. Non-goals

- Reading or converting tg_audio's or king_audio_engine's pack formats (MAPK and the encrypted folder). Those formats stay inside their plugin. Zodiac's core does not know them.
- Compression inside packs. Game assets (ogg, opus, png) are already compressed. The format reserves a flag so compression can be added later without a new version.
- Payload (bytecode) encryption, string-literal encryption and `obfuscate`. Those belong to the protection plan, which reuses this plan's cipher code.
- Writable mounts of packs. A mounted pack is read-only. Writing goes through the `pack` object.
- BGT's `pack_file` name and format: the `bgt_compat` module (plan 9).

## 4. Anti-goals

- **Packs that only hide files.** Script packs and release packs must use real authenticated encryption: a known plaintext such as an OGG header must reveal nothing, and a modified pack must be refused, not read as garbage. tg_audio's XOR cipher is the example of what not to do.
- **A release game that behaves differently from `zodiac run`.** The same paths open the same bytes. The only difference is where they come from.
- **Breaking plugins.** The plugin ABI stays append-only. No existing member changes.
- **A crash on bad input.** A truncated, corrupt or wrong-key pack, or a bad path, gives a documented failure or a catchable script exception, never undefined behaviour.

## 5. Constraints

- C++23, clang only, no indentation where not needed, English only in code, one-line diagnostics.
- Dev and release are identical (the plan 3 identity rule).
- Linux is a first-class target: the Blood Bath server runs there.
- Speed beats size, but the hello gate stays at 5 MB in Release.
- Thread rules of the Script API spec:
  - `pack` and `file` objects are not internally synchronized.
  - The VFS mount table and pack reads through mounts must be safe from several threads. The workers of Plan 4's second half and plugins' audio threads will read concurrently.
- Third-party code needs a permissive licence, recorded in `THIRD_PARTY_LICENSES`.

## 6. Approaches considered

**Who makes packs**

| Option | Summary |
|---|---|
| A. Only `zodiac build` | Simplest and safest: the key never reaches scripts. But no runtime packs. |
| **B. `zodiac build` plus a script `pack` object (chosen)** | Also covers downloadable maps, mods and tools. The owner already makes packs from script in tg_audio. |

**Packs and plugins**

| Option | Summary |
|---|---|
| Engine pack mandatory for all file access | One system, but forces tg_audio and others to give up their own packs. |
| **Engine pack for the VFS; plugins free (chosen)** | Plugins keep their own packs. They may read through the VFS by an optional ABI entry. |

**Old pack formats**

| Option | Summary |
|---|---|
| Read MAPK and encrypted folders in core | No migration, but core carries a weak cipher and weak-crypto distribution continues. |
| **Leave them to the plugin (chosen)** | The tg_audio port keeps its own formats. |

## 7. Decisions made with the owner

- Script packs are wanted (option B). tg_audio already uses its own pack system from script.
- Plugins may use their own pack systems, and the engine's pack is not mandatory for them. The engine pack serves non-audio files and automatic release protection.
- Core does not read or convert old king/tg packs.
- In dev (`zodiac run`, double-click) no pack is made. Files are read straight from the folders. Packs are made by `zodiac build`, and by scripts that ask.
- A build warning for literal paths that scripts open but no declared asset matches. This was proposed by Claude, and the owner did not object.
- **Read-only or writable is chosen per pack.**
  - A mount is read-only unless it is made writable: `mount_pack(..., writable = true)` from script, or `[pack] writable = true` for the release pack.
  - Patch packs are always read-only.
  - Writing to a path that only a read-only pack provides fails, and the error text says the file is in a read-only pack and that saves belong in `data:/`.
- **Player data:** settings and saves belong in `data:/`. Defaults may live in the pack.
- **Updates come as patch packs.** `zodiac build --patch` packs only the files that changed since the last release, and a release game mounts patch packs over its build pack. Full rebuilds stay possible.
- **The release key is per game, not per build.**
  - The first release build generates it and stores it in the project as `.zodiac/pack.key`, which is git-ignored and which the owner backs up.
  - Later builds and patches reuse it.
  - This replaces "random key per build" in the foundation spec's protection section and in the protection plan, for packs. The protection plan decides separately for the bytecode payload.

## 8. Design

### 8.1 Library (core-types spec 8.5 to 8.7, unchanged)

- Build it as written there: maths, `random` and `random_generator` (xoshiro256\*\*), `ticks`, `timer`, `date_time`, the VFS with case-insensitive lookup, the `file` object, the helpers and the failure rule.
- The process entropy source is `platform::fill_entropy` (src/platform/entropy.hpp). It already seeds the table-hash secrets, and it seeds the global random generator.
- Rounding is half away from zero.
- `round(x, digits)` must give the decimal result a BGT script expects: `round(2.675, 2)` is 2.68.
  - **My call:** it rounds the shortest round-trip decimal form of `x`, so binary representation error never shows.
  - Its speed gate compares it with the plain C++ multiply-round-divide. A slower result is reported as a ruling, not waived.

### 8.2 Pack format (my call, within the anti-goals)

- **One file:** a header, then data, an encrypted index, and a footer that points at the index.
  - The header holds a magic number, a format version, flags (compression is reserved and unused) and a random per-pack salt.
- **Cipher:** ChaCha20-Poly1305 (RFC 8439).
  - Each file's data is split into fixed-size chunks (64 KiB, or whatever measures best). Each chunk is encrypted and authenticated on its own, with a nonce derived from the file's entry id and the chunk number.
  - Seeking reads and verifies only the chunks it touches, so audio streaming works.
  - The index (names, sizes, offsets) is encrypted and authenticated as one record. A pack opened with the wrong key fails at open with "wrong key or damaged pack".
  - A chunk that fails authentication makes the read fail. The `file` object returns an error or throws, following the 8.7 failure rule. It never returns unauthenticated bytes.
- **Keys:**
  - A script pack's key is a string. It is stretched with a salted key-derivation function, which the builder picks: a modern, permissive one, fast enough that opening a pack takes well under 100 ms.
  - Release and patch packs use the game's random 256-bit key from `.zodiac/pack.key`, embedded in the executable.
    - The executable reads it through one function, so the protection plan can replace it with a hidden form.
    - Builds of one game are therefore not byte-identical across machines without that file. That is accepted.
- **Implementation:** the builder picks between a pinned permissive library (for example Monocypher) and an own RFC 8439 implementation checked against the RFC test vectors. A vectorized ChaCha20 (SSE2/AVX2 on x86-64) is expected if it measures faster.

### 8.3 Script `pack` object

- `pack` is a reference type:
  - `open(path, key, create = false)` → bool;
  - `close()`;
  - `is_open`, `error`;
  - `exists(name)`, `list()` → `array<string>`, `count`, `size(name)`;
  - `add_file(disk_path, name)` → bool and `add(name, data)` → bool, where `data` is a string that may hold any bytes;
  - `remove(name)` → bool;
  - `read(name)` → string, which throws when the entry is missing, as `read_file` does;
  - `extract(name, path)` → bool;
  - `compact()` → bool;
  - `flush()` → bool, which writes the index;
  - `wasted_bytes`.
- **Paths:**
  - The pack file's own path and the paths given to `add_file` and `extract` are VFS paths, under the 8.7 rules.
  - Entry names use `/` and are matched case-insensitively, as VFS paths are.
- **Writing:**
  - Adding or replacing an entry appends to the file.
  - `remove` and replace leave dead space, which `compact()` reclaims by rewriting into a temporary file and replacing the old one atomically.
  - A new index and a new footer are appended at `flush()` and `close()`. The previous index stays valid until the new footer is complete.
    - A crash loses only the entries added since the last flush; the pack opens at its last flushed state.
    - Tests assert this by truncating a pack mid-append.
    - Old indexes count as wasted bytes.
  - The builder may add batching if measurements call for it.
- **Mounting:**
  - `mount_pack(pack@ p, string at = "/", bool writable = false)` → bool and `unmount_pack(pack@ p)`.
  - `at` may be any VFS folder, including `data:/` and subfolders. Entry names are relative to `at`, and the `..` rule and case-insensitive matching apply as for folders.
  - Mounts at one point are searched newest first, and the first that has the name wins. Every mount, writable or not, is in front of the folder mounted at that point.
- **Writes through a mount:**
  - A path that a writable pack mount covers, by its mount point, is written into the newest writable pack there. This covers new files too.
  - A path that only read-only packs provide fails to write, with the error text from section 7.
  - Other paths go to the folder as before.
  - In dev, `zodiac run` warns when a script writes to a path that a release build would put into a read-only pack.
- **Lifetimes:**
  - The mount table, open `file` objects and plugin VFS handles hold references to the pack's storage.
  - `close()` or `unmount_pack` stops new lookups at once. Reads already open finish against the old storage, which is freed when the last reference goes.

### 8.4 Release packing (`zodiac build`)

- **Today:** plan 3 copies `[build] assets` globs next to the executable.
- **Now:** in a release build these assets go into one encrypted pack instead.
  - By default the pack is appended to the executable, making a single-file game.
  - With `[pack] external = true` it is written next to it as `<game>.zpk`.
- **`[pack] exclude = [globs]`** keeps listed assets as plain copied files. This is for files other programs must read, such as documentation or plugin data in another format.
- **`[pack] enabled = false`** keeps plan 3's copying.
- **`[pack] writable = true`** mounts the release pack writable. Because a running executable is locked by the OS, this forces the external `<game>.zpk`.
- The compiled script payload stays where plan 3 puts it. The pack follows it in the executable, and a trailer at the end of the file locates both.
- **Patch packs:**
  - `zodiac build --patch` writes `patch_<n>.zpk` with the files that differ from the last release build, comparing against a manifest that build recorded in `.zodiac/`.
  - A release game mounts every `data:/patches/*.zpk` and every `patches/*.zpk` next to the executable over its build pack, in ascending `n`, read-only.
  - A patch made with another key is refused with a logged error, and the game runs without it.
  - **Patches can also delete files (owner's request).**
    - A file that was in the last release build but is no longer among the declared assets goes into the patch as a deletion marker.
    - A marker in a newer mount hides that name in every older mount at the same point. `file_exists` is then false, reads fail as for a missing file, and listings leave it out.
    - A later patch that adds the name again makes it visible again.
- At start-up a release game mounts its build pack at `/` before the script runs, then the patches. Scripts see the same paths as in dev.
- **Build warning:** for every string literal passed directly to a file-opening function or a `pack`/`mount` function, `zodiac build` warns when no declared asset or project file matches it.
  - It is only ever a warning, never an error.
  - Paths computed at run time are not checked.
  - `data:/` paths are skipped.

### 8.5 Plugin access to the VFS

- **Appended `zodiac_host` members:**
  - `vfs_open(host, path, &handle)`;
  - `vfs_read(handle, buffer, size)` → bytes read;
  - `vfs_seek(handle, offset, origin)`;
  - `vfs_size(handle)`;
  - `vfs_close(handle)`;
  - `vfs_exists(host, path)`.
- They use C types and integer error codes, and a plugin must check `struct_size` before using them. **My calls:**
  - Every call takes the host as its first argument.
  - The handle is an opaque pointer.
  - `vfs_read` returns an `int64_t`: the number of bytes read, 0 at end of file, or a negative error code.
  - The error codes are defined in plugin.h next to the existing ones.
- The calls may be made from any thread, for example an audio streaming thread. One handle is used by one thread at a time; different handles may be used concurrently.
- They see exactly what scripts see: mounts, case-insensitive lookup and packs.

## 9. Success criteria

- Every API in core-types spec 8.5 to 8.7 and in 8.3 here behaves as specified on Windows and Linux.
- The Blood Bath–style script from core-types spec section 9 runs in dev and as a packed release with identical output.
- **Pack security tests:**
  - a wrong key fails at open;
  - flipping any byte of the index or a data chunk is detected;
  - the same plaintext in two packs gives different ciphertext;
  - a known-plaintext OGG header does not reveal the other bytes of its chunk;
  - the RFC 8439 test vectors pass.
- Concurrent reads of one mounted pack from several threads give correct bytes, under a stress test.
- **Benchmarks (in the 4a harness):**
  - the maths and random ops against the add-on or plain C++;
  - sequential and random-seek reads from a mounted pack against the same file from disk, with the ratio reported and gated at a limit the builder sets from the first measurement and records as a ruling.
- **Pack behaviour tests:**
  - writable and read-only mounts, including the error text for a write to a read-only pack;
  - a crash-truncated pack opens at its last flush;
  - patch order: the newest patch wins;
  - a patch deletion hides the file in older packs, and a later re-add shows it again;
  - a patch with a foreign key is refused;
  - unmount while a read is open.
- The hello gate stays under 5 MB in Release.
- A plugin test reads a packed file through the new ABI entries.

## 10. Left to the builder

- The chunk size, the exact binary layout, the KDF and the cipher implementation, within 8.2.
- The task order. The suggested order is: maths and random, then time, then VFS and files, then packs, then release packing, then the plugin VFS entry.
- How the build pack is appended to the executable and found at start-up.
- How the build warning finds literal paths. It may use the compiled bytecode's string constants or a source scan.
