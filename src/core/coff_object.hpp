// zodiac::write_payload_object turns a game payload into an x86-64 COFF object file that the release link step adds to the executable.
// Layout: file header (machine 0x8664, 1 section, no optional header), one section header ".zodpay" (initialized data, read-only, 16-byte aligned),
// the section data (u64 little-endian payload size followed by the payload), a symbol table with two external symbols, "zodiac_payload"
// (section 1, value 0) and "zodiac_payload_end" (section 1, value 8 + payload size, just past the data), and the string table that holds their names.
// The end symbol lets the release stub check the stored size against the linked data instead of trusting it. Every field is written byte by byte in little-endian order, so the output is identical on every host.
// Threads: pure, may be called from any thread.
#pragma once
#include <string>
#include <string_view>

namespace zodiac {
// Throws std::length_error when the section would not fit in a COFF object (more than 4 GiB).
std::string write_payload_object(std::string_view payload);
}
