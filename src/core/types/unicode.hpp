// Unicode character services behind Zodiac's string: simple case mapping (invariant and Turkic), White_Space and NFC.
// Data is the Unicode Character Database 16.0.0, generated into unicode_tables.cpp by tools/gen_unicode.py (two-level tables).
// The per-character functions are noexcept, never allocate, have an inline ASCII fast path, and return values without a mapping
// (unassigned code points, surrogates, anything above U+10FFFF) unchanged.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace zodiac::unicode {
namespace detail {
char32_t to_lower_table(char32_t c) noexcept;
char32_t to_upper_table(char32_t c) noexcept;
bool is_whitespace_table(char32_t c) noexcept;
// Bit c is set for ASCII White_Space c (all below 64); unicode.cpp checks it against the generated table.
inline constexpr std::uint64_t ascii_whitespace_mask=0x100003E00u;
}
// Simple (one-to-one) lowercase mapping from UnicodeData.txt; locale-independent, so to_lower(U'I')==U'i' and to_lower(U'İ')==U'i'.
inline char32_t to_lower(char32_t c) noexcept
{
if(c<0x80)return c-U'A'<26?c+32:c;
return detail::to_lower_table(c);
}
// Simple (one-to-one) uppercase mapping from UnicodeData.txt; locale-independent, so to_upper(U'ı')==U'I' and to_upper(U'ß')==U'ß'.
inline char32_t to_upper(char32_t c) noexcept
{
if(c<0x80)return c-U'a'<26?c-32:c;
return detail::to_upper_table(c);
}
// Turkish and Azerbaijani lowercase: I becomes ı (U+0131) and İ (U+0130) becomes i; every other code point as to_lower.
inline char32_t to_lower_turkic(char32_t c) noexcept
{
return c==U'I'?char32_t{0x131}:to_lower(c);
}
// Turkish and Azerbaijani uppercase: i becomes İ (U+0130) and ı becomes I; every other code point as to_upper.
inline char32_t to_upper_turkic(char32_t c) noexcept
{
return c==U'i'?char32_t{0x130}:to_upper(c);
}
// The White_Space property (PropList.txt): the ASCII tab, line feed, vertical tab, form feed, carriage return and space, plus
// U+0085, U+00A0, U+1680, U+2000..U+200A, U+2028, U+2029, U+202F, U+205F and U+3000. Zero-width space and BOM are not white space.
inline bool is_whitespace(char32_t c) noexcept
{
if(c<0x80)return c<64&&((detail::ascii_whitespace_mask>>c)&1)!=0;
return detail::is_whitespace_table(c);
}
// True when the NFC quick check (UAX #15) proves text is already in Normalization Form C; false when it may not be.
// Allocation-free; nfc() runs the same check first and returns a plain copy when it passes.
bool quick_check_nfc(std::u32string_view text) noexcept;
// Normalization Form C (UAX #15): full canonical decomposition (Hangul algorithmically), canonical ordering by combining
// class, then canonical composition skipping Full_Composition_Exclusion (excluded, singleton and non-starter decompositions).
// Only the part from the last safe starter before the first quick-check failure is renormalized; the rest is copied.
// Values above U+10FFFF are kept unchanged and nothing composes or reorders across them.
std::u32string nfc(std::u32string_view text);
}
