# Keyed Table Hash Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the hash that `String` caches, and that dictionaries place keys by, a keyed per-process hash. Script `hash()` stays stable.

**Architecture:**
- `String::hash()` stays the stable fixed-seed hash, computed on demand.
- New `String::table_hash()` is keyed by a per-process secret from `platform::fill_entropy` and is cached in `Heap::hash`.
- The dictionary, `std::hash<String>` and the string binding's tables use `table_hash()`. The dictionary's `home()` multiplier is removed.

**Tech Stack:** C++23, clang (MSVC-ABI on Windows, libc++ on Linux), AngelScript 2.39.0, doctest, CMake presets, `tests/bench/bench_types.cpp` with `tools/check_bench.py`.

**Spec:** docs/superpowers/specs/2026-11-18-zodiac-keyed-table-hash-design.md (amends docs/superpowers/specs/2026-11-16-zodiac-core-types-design.md 8.1 and 8.3).

## Global Constraints
- English only in code, comments and commit messages. No indentation where the code doesn't need it. Match the surrounding style (the repo's files are flush-left).
- Edit files with the Edit and Write tools only: no heredoc, no `sed -i`.
- Never touch the untracked `D:\bgdk\test.zs`.
- The golden `String::hash()` values stay exactly as they are (tests/test_string.cpp, "hash values are fixed across platforms and runs").
- Speed: paired runs against main at 3a8c7ab must give a paired geomean ≤ 1.00, and no op above 1.02 beyond its A/A noise in every pass. The bench gate stays ≤ 1.05.
- The alignment and atomic rules in string.hpp's contract block still hold: `Heap::hash` is accessed only relaxed, and acquire/release only on `hash_known`.
- Dictionary insertion-order iteration and the `serialize` output are unchanged.
- Build trees go on C: (D: is nearly full). Commit often. Push only `feat/keyed-dict-hash`. CI must be green.

## Review Focus
1. A key set with equal full `String::hash()` must spread in the dictionary. This is the whole point, so the test must really produce such keys or an equivalent keyed proof.
2. `table_hash()` must be equal for equal bytes in every storage: inline, heap, shared copy, and after a mutation.
3. Cache invalidation: every path that today resets `hash_known` must still do so for the table hash.
4. Script `hash()` on long strings must not leave a stale cache and must not touch `Heap::hash`.
5. Concurrent `table_hash()` on a shared constant string from several threads must be race-free (the existing concurrency test pattern).

---

### Task 1: Keyed table hash

**Files:**
- Modify: `src/core/types/string.hpp` (`hash()`, new `table_hash()`, `cache_hash`, the contract block, `std::hash<String>` ~515)
- Modify: `src/core/types/string.cpp` (`hash_bytes`, the keyed variant, `cache_hash`)
- Modify: `src/core/types/dictionary.hpp` and `src/core/types/dictionary.cpp` (slots, `home()`, `slot_multiplier()`, contract comments)
- Modify: `src/core/types/string_binding.cpp` (~39-73 internal tables)
- Modify: `src/platform/entropy.hpp` comment (the secret's new user)
- Modify: `docs/superpowers/specs/2026-11-16-zodiac-core-types-design.md` (8.3 line ~101: "the string's cached hash" becomes "the string's cached keyed table hash")
- Test: `tests/test_string.cpp`, `tests/test_dictionary.cpp`

**Interfaces:**
- Produces:
  - `std::uint64_t String::table_hash() const noexcept`, keyed per process and cached;
  - `std::uint64_t String::hash() const noexcept`, stable and uncached;
  - `std::hash<String>` returns `table_hash()`.

- [ ] **Step 1: Write the failing tests.**
  - In test_string.cpp, the case "table_hash is equal for equal bytes, whatever the storage" mirrors the existing hash test, including `std::hash<String>{}(a)==static_cast<std::size_t>(a.table_hash())`. Also check:
    - `table_hash()` is stable between two calls;
    - it changes after a mutation;
    - `String("abc").table_hash()!=String("abc").hash()`.
  - Add "script hash stays uncached and stable on heap strings": a long string's `hash()` equals its golden-style recomputation before and after `table_hash()` was cached.
  - In test_dictionary.cpp, add "keys with equal String::hash() do not share a probe run". Use keys with equal full 64-bit stable hashes, found offline or by construction, or the keyed proof the spec allows. Assert largest displacement ≤ 128 and total ≤ 3 per key, the same bounds as the existing collide test.
- [ ] **Step 2: Run them and verify they fail.**
  - Run `ctest --preset windows-debug -R "string|dictionary"`.
  - Expected: FAIL (`table_hash` not declared).
- [ ] **Step 3: Implement.**
  - The keyed construction must not admit seed-independent collisions. Feed the random secret into the mixing constants, not only the seed. Justify the choice and its speed in the report.
  - Draw the secret once, with the fallback `slot_multiplier()` uses today. Keep the hot path free of a lazy-initialization guard if a static-init approach measures faster.
  - Remove `home()`'s multiplier when it measures no slower. Otherwise keep the faster mapping and say why.
- [ ] **Step 4: Run the full Windows debug and release suites.**
  - `ctest --preset windows-debug` and `ctest --preset windows-release`.
  - Expected: all pass, with the golden hash values unchanged.
- [ ] **Step 5: Bench.**
  - Run paired, rotated release bench passes against 3a8c7ab: at least two full passes plus A/A.
  - Report every `dict_*` and `string_*` op.
  - Expected: the Global Constraints speed rule holds.
- [ ] **Step 6: Commit, push `feat/keyed-dict-hash`, and get CI green.**
