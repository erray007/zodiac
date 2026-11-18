// Random bytes from the operating system's entropy source: RtlGenRandom on Windows, getrandom (then /dev/urandom) elsewhere.
// Implemented per platform in src/platform/<os>/entropy.cpp. It seeds Zodiac's per-process secrets, such as the keys of
// String::table_hash() (src/core/types/string.cpp), which places every dictionary key, and is where the script random
// generator of plan 4b is to be seeded from.
#pragma once
#include <cstddef>

namespace zodiac::platform {
// Fills size bytes at buffer; false when the source fails (the bytes are then unspecified). Thread-safe.
bool fill_entropy(void* buffer,std::size_t size) noexcept;
}
