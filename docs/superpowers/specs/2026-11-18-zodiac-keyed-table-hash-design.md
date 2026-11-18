# Keyed table hash for strings and dictionaries — design

Status: approved by the owner on 2026-11-18. It amends the core-types spec (2026-11-16-zodiac-core-types-design.md, sections 8.1 and 8.3).

## Intent
Plan 4a's final wave made the dictionary's slot placement random per process (a keyed multiply-shift over `String::hash()`). That stopped attackers who pick keys that collide in a few bits.

It does not stop keys whose full 64-bit `String::hash()` values are equal. The hash is a fixed-seed wyhash with a public seed, so equal hashes can be searched for offline. Such keys share one probe run whatever the multiplier, and a few thousand of them stall a lookup-heavy server for seconds.

Untrusted keys will reach dictionaries from:
- players and the network (the ENet plan);
- save and pack files (Plan 4b).

So this is fixed now, before both.

## Goals
- No offline-computable key set makes dictionary probe runs long. Keys that collide in `String::hash()`, fully or in part, spread like random keys.
- Script-visible `hash()` stays exactly as today: 64-bit, stable across runs and platforms, with the golden values unchanged.
- Dictionary lookup, insert, erase and iterate, and `String` hot paths, are not slower than main at 3a8c7ab.
  - Paired runs, ruling criterion: paired geomean ≤ 1.00, and no op above 1.02 beyond its A/A noise in every pass.
  - The bench gate stays ≤ 1.05.
- Insertion-order iteration and the `serialize` output are unchanged.

## Non-goals
- No key-count limits anywhere. The owner rejected them: legit data such as a 100-item inventory would hit any fixed number.
- No change to what scripts can observe.

## Approaches
1. The dictionary computes a second keyed hash per lookup, and the cached hash stays the stable one.
   - Rejected: long keys would be rehashed on every lookup because the keyed value is not cached, so lookups get slower.
2. **Chosen.** The hash `String` caches becomes the keyed table hash. The stable script hash is computed on demand.
   - Dictionary speed stays as today.
   - Every C++ hash table keyed by `String` (for example through `std::hash<String>`) gets the protection.
   - Cost: script `hash()` on long strings is no longer cached. Scripts rarely call it.

## Design
- **`String::hash()`** stays the stable fixed-seed hash, but is no longer cached.
  - The script binding `uint64 hash() const` and the golden-value tests keep using it.
  - The spec 8.1 text "stable across runs and platforms" still holds.
- **New `std::uint64_t String::table_hash() const noexcept`.** It is a keyed hash with a per-process secret drawn once from `platform::fill_entropy`, with the fallback the dictionary uses today.
  - It is cached in `Heap::hash` / `hash_known` under the existing alignment and atomic rules.
  - Inline strings compute it each call, as today.
  - The keying must not allow seed-independent collisions: equal stable hashes must not imply equal table hashes. Changing only wyhash's seed while keeping public secret constants is not enough unless the builder shows it is.
  - Go's runtime is precedent: a random per-process key fed into wyhash-style mixing.
  - The builder picks the fastest construction that meets this and justifies it in the report.
- **Users of the cached hash move to `table_hash()`:**
  - the dictionary's slots, lookups and inserts;
  - `std::hash<String>`;
  - the string binding's internal tables (string_binding.cpp ~39-73).
- **Dictionary.** `home()`'s random multiplier becomes redundant once the slot hash is keyed. Use the cheapest correct mapping from the keyed hash, `& mask` or multiply-shift, whichever measures faster, and delete `slot_multiplier()` if it is unused. Update the dictionary contract comments.
- **One entropy source.** The per-process secrets come from one place, shared with Plan 4b's random later.

## Tests
- The golden `hash()` values (test_string.cpp ~955-963) are unchanged.
- `table_hash()`:
  - is equal for equal bytes in every storage (inline, heap, shared copy);
  - changes when the bytes change;
  - takes the same value in two calls in one process;
  - differs from `hash()` in general.
- **Flooding test.** Generate keys whose full 64-bit `String::hash()` values are equal, using the table hash's construction with the public seed or any offline method the builder has. Then assert they do not share a probe run in a dictionary, with a bound on displacement similar to the existing collide test.
  - If finding full collisions is too slow for a unit test, the test may build the keyed construction with a fixed test key, find keys that collide under a different key, and show they spread under the real one.
  - The existing low-bit and high-bit collide test stays.
- Serialize text is byte-identical to main for the existing fixtures.
