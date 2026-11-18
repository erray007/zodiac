// Rounding of the script maths library (spec 8.1 of the library-and-packs design): half away from zero, as BGT rounds.
// Contract:
// - round_half_away(x): the integer nearest x, halves away from zero; the sign of a zero result is x's. NaN and the
//   infinities come back unchanged.
// - round_digits(x, digits): x rounded half away from zero at digits decimal places (digits < 0 rounds to tens, hundreds
//   and so on), applied to the shortest decimal form that reads back as x, so binary representation error never shows:
//   round_digits(2.675, 2) is 2.68. The result is the double nearest that decimal (infinity when it rounds past the largest
//   double), with x's sign. NaN and the infinities come back unchanged, and so does x for digits >= 17.
// - round_digits_exact(x, digits): the same result, always through the decimal digits. round_digits takes a multiply-round
//   fast path where it provably agrees and calls this otherwise; it is exported for the property test.
#pragma once

namespace zodiac {
double round_half_away(double x) noexcept;
double round_digits(double x,int digits) noexcept;
double round_digits_exact(double x,int digits) noexcept;
}
