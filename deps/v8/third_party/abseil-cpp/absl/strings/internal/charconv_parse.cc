// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/internal/charconv_parse.h"
#include "absl/strings/charconv.h"

#include <cassert>
#include <cstdint>
#include <limits>

#include "absl/strings/internal/memutil.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

// ParseFloat<10> will read the first 19 significant digits of the mantissa.
// This number was chosen for multiple reasons.
//
// (a) First, for whatever integer type we choose to represent the mantissa, we
// want to choose the largest possible number of decimal digits for that integer
// type.  We are using uint64_t, which can express any 19-digit unsigned
// integer.
//
// (b) Second, we need to parse enough digits that the binary value of any
// mantissa we capture has more bits of resolution than the mantissa
// representation in the target float.  Our algorithm requires at least 3 bits
// of headway, but 19 decimal digits give a little more than that.
//
// The following static assertions verify the above comments:
constexpr int kDecimalMantissaDigitsMax = 19;

static_assert(std::numeric_limits<uint64_t>::digits10 ==
                  kDecimalMantissaDigitsMax,
              "(a) above");

// IEEE doubles, which we assume in Abseil, have 53 binary bits of mantissa.
static_assert(std::numeric_limits<double>::is_iec559, "IEEE double assumed");
static_assert(std::numeric_limits<double>::radix == 2, "IEEE double fact");
static_assert(std::numeric_limits<double>::digits == 53, "IEEE double fact");

// The lowest valued 19-digit decimal mantissa we can read still contains
// sufficient information to reconstruct a binary mantissa.
static_assert(1000000000000000000u > (uint64_t{1} << (53 + 3)), "(b) above");

// ParseFloat<16> will read the first 15 significant digits of the mantissa.
//
// Because a base-16-to-base-2 conversion can be done exactly, we do not need
// to maximize the number of scanned hex digits to improve our conversion.  What
// is required is to scan two more bits than the mantissa can represent, so that
// we always round correctly.
//
// (One extra bit does not suffice to perform correct rounding, since a number
// exactly halfway between two representable floats has unique rounding rules,
// so we need to differentiate between a "halfway between" number and a "closer
// to the larger value" number.)
constexpr int kHexadecimalMantissaDigitsMax = 15;

// The minimum number of significant bits that will be read from
// kHexadecimalMantissaDigitsMax hex digits.  We must subtract by three, since
// the most significant digit can be a "1", which only contributes a single
// significant bit.
constexpr int kGuaranteedHexadecimalMantissaBitPrecision =
    4 * kHexadecimalMantissaDigitsMax - 3;

static_assert(kGuaranteedHexadecimalMantissaBitPrecision >
                  std::numeric_limits<double>::digits + 2,
              "kHexadecimalMantissaDigitsMax too small");

// We also impose a limit on the number of significant digits we will read from
// an exponent, to avoid having to deal with integer overflow.  We use 9 for
// this purpose.
//
// If we read a 9 digit exponent, the end result of the conversion will
// necessarily be infinity or zero, depending on the sign of the exponent.
// Therefore we can just drop extra digits on the floor without any extra
// logic.
constexpr int kDecimalExponentDigitsMax = 9;
static_assert(std::numeric_limits<int>::digits10 >= kDecimalExponentDigitsMax,
              "int type too small");

// To avoid incredibly large inputs causing integer overflow for our exponent,
// we impose an arbitrary but very large limit on the number of significant
// digits we will accept.  The implementation refuses to match a string with
// more consecutive significant mantissa digits than this.
constexpr int kDecimalDigitLimit = 50000000;

// Corresponding limit for hexadecimal digit inputs.  This is one fourth the
// amount of kDecimalDigitLimit, since each dropped hexadecimal digit requires
// a binary exponent adjustment of 4.
constexpr int kHexadecimalDigitLimit = kDecimalDigitLimit / 4;

// The largest exponent we can read is 999999999 (per
// kDecimalExponentDigitsMax), and the largest exponent adjustment we can get
// from dropped mantissa digits is 2 * kDecimalDigitLimit, and the sum of these
// comfortably fits in an integer.
//
// We count kDecimalDigitLimit twice because there are independent limits for
// numbers before and after the decimal point.  (In the case where there are no
// significant digits before the decimal point, there are independent limits for
// post-decimal-point leading zeroes and for significant digits.)
static_assert(999999999 + 2 * kDecimalDigitLimit <
                  std::numeric_limits<int>::max(),
              "int type too small");
static_assert(999999999 + 2 * (4 * kHexadecimalDigitLimit) <
                  std::numeric_limits<int>::max(),
              "int type too small");

// Returns true if the provided bitfield allows parsing an exponent value
// (e.g., "1.5e100").
bool AllowExponent(chars_format flags) {
  bool fixed = (flags & chars_format::fixed) == chars_format::fixed;
  bool scientific =
      (flags & chars_format::scientific) == chars_format::scientific;
  return scientific || !fixed;
}

// Returns true if the provided bitfield requires an exponent value be present.
bool RequireExponent(chars_format flags) {
  bool fixed = (flags & chars_format::fixed) == chars_format::fixed;
  bool scientific =
      (flags & chars_format::scientific) == chars_format::scientific;
  return scientific && !fixed;
}

const int8_t kAsciiToInt[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,
    9,  -1, -1, -1, -1, -1, -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, 10, 11, 12, 13, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};

// Returns true if `ch` is a digit in the given base
template <int base>
bool IsDigit(char ch);

// Converts a valid `ch` to its digit value in the given base.
template <int base>
unsigned ToDigit(char ch);

// Returns true if `ch` is the exponent delimiter for the given base.
template <int base>
bool IsExponentCharacter(char ch);

// Returns the maximum number of significant digits we will read for a float
// in the given base.
template <int base>
constexpr int MantissaDigitsMax();

// Returns the largest consecutive run of digits we will accept when parsing a
// number in the given base.
template <int base>
constexpr int DigitLimit();

// Returns the amount the exponent must be adjusted by for each dropped digit.
// (For decimal this is 1, since the digits are in base 10 and the exponent base
// is also 10, but for hexadecimal this is 4, since the digits are base 16 but
// the exponent base is 2.)
template <int base>
constexpr int DigitMagnitude();

template <>
bool IsDigit<10>(char ch) {
  return ch >= '0' && ch <= '9';
}
template <>
bool IsDigit<16>(char ch) {
  return kAsciiToInt[static_cast<unsigned char>(ch)] >= 0;
}

template <>
unsigned ToDigit<10>(char ch) {
  return static_cast<unsigned>(ch - '0');
}
template <>
unsigned ToDigit<16>(char ch) {
  return static_cast<unsigned>(kAsciiToInt[static_cast<unsigned char>(ch)]);
}

template <>
bool IsExponentCharacter<10>(char ch) {
  return ch == 'e' || ch == 'E';
}

template <>
bool IsExponentCharacter<16>(char ch) {
  return ch == 'p' || ch == 'P';
}

template <>
constexpr int MantissaDigitsMax<10>() {
  return kDecimalMantissaDigitsMax;
}
template <>
constexpr int MantissaDigitsMax<16>() {
  return kHexadecimalMantissaDigitsMax;
}

template <>
constexpr int DigitLimit<10>() {
  return kDecimalDigitLimit;
}
template <>
constexpr int DigitLimit<16>() {
  return kHexadecimalDigitLimit;
}

template <>
constexpr int DigitMagnitude<10>() {
  return 1;
}
template <>
constexpr int DigitMagnitude<16>() {
  return 4;
}

// Reads decimal digits from [begin, end) into *out.  Returns the number of
// digits consumed.
//
// After max_digits has been read, keeps consuming characters, but no longer
// adjusts *out.  If a nonzero digit is dropped this way, *dropped_nonzero_digit
// is set; otherwise, it is left unmodified.
//
// If no digits are matched, returns 0 and leaves *out unchanged.
//
// ConsumeDigits does not protect against overflow on *out; max_digits must
// be chosen with respect to type T to avoid the possibility of overflow.
template <int base, typename T>
int ConsumeDigits(const char* begin, const char* end, int max_digits, T* out,
                  bool* dropped_nonzero_digit) {
  if (base == 10) {
    assert(max_digits <= std::numeric_limits<T>::digits10);
  } else if (base == 16) {
    assert(max_digits * 4 <= std::numeric_limits<T>::digits);
  }
  const char* const original_begin = begin;

  // Skip leading zeros, but only if *out is zero.
  // They don't cause an overflow so we don't have to count them for
  // `max_digits`.
  while (!*out && end != begin && *begin == '0') ++begin;

  T accumulator = *out;
  const char* significant_digits_end =
      (end - begin > max_digits) ? begin + max_digits : end;
  while (begin < significant_digits_end && IsDigit<base>(*begin)) {
    // Do not guard against *out overflow; max_digits was chosen to avoid this.
    // Do assert against it, to detect problems in debug builds.
    auto digit = static_cast<T>(ToDigit<base>(*begin));
    assert(accumulator * base >= accumulator);
    accumulator *= base;
    assert(accumulator + digit >= accumulator);
    accumulator += digit;
    ++begin;
  }
  bool dropped_nonzero = false;
  while (begin < end && IsDigit<base>(*begin)) {
    dropped_nonzero = dropped_nonzero || (*begin != '0');
    ++begin;
  }
  if (dropped_nonzero && dropped_nonzero_digit != nullptr) {
    *dropped_nonzero_digit = true;
  }
  *out = accumulator;
  return static_cast<int>(begin - original_begin);
}

// Returns true if `v` is one of the chars allowed inside parentheses following
// a NaN.
bool IsNanChar(char v) {
  return (v == '_') || (v >= '0' && v <= '9') || (v >= 'a' && v <= 'z') ||
         (v >= 'A' && v <= 'Z');
}

// Checks the range [begin, end) for a strtod()-formatted infinity or NaN.  If
// one is found, sets `out` appropriately and returns true.
bool ParseInfinityOrNan(const char* begin, const char* end,
                        strings_internal::ParsedFloat* out) {
  if (end - begin < 3) {
    return false;
  }
  switch (*begin) {
    case 'i':
    case 'I': {
      // An infinity string consists of the characters "inf" or "infinity",
      // case insensitive.
      if (strings_internal::memcasecmp(begin + 1, "nf", 2) != 0) {
        return false;
      }
      out->type = strings_internal::FloatType::kInfinity;
      if (end - begin >= 8 &&
          strings_internal::memcasecmp(begin + 3, "inity", 5) == 0) {
        out->end = begin + 8;
      } else {
        out->end = begin + 3;
      }
      return true;
    }
    case 'n':
    case 'N': {
      // A NaN consists of the characters "nan", case insensitive, optionally
      // followed by a parenthesized sequence of zero or more alphanumeric
      // characters and/or underscores.
      if (strings_internal::memcasecmp(begin + 1, "an", 2) != 0) {
        return false;
      }
      out->type = strings_internal::FloatType::kNan;
      out->end = begin + 3;
      // NaN is allowed to be followed by a parenthesized string, consisting of
      // only the characters [a-zA-Z0-9_].  Match that if it's present.
      begin += 3;
      if (begin < end && *begin == '(') {
        const char* nan_begin = begin + 1;
        while (nan_begin < end && IsNanChar(*nan_begin)) {
          ++nan_begin;
        }
        if (nan_begin < end && *nan_begin == ')') {
          // We found an extra NaN specifier range
          out->subrange_begin = begin + 1;
          out->subrange_end = nan_begin;
          out->end = nan_begin + 1;
        }
      }
      return true;
    }
    default:
      return false;
  }
}
}  // namespace

namespace strings_internal {

template <int base>
strings_internal::ParsedFloat ParseFloat(const char* begin, const char* end,
                                         chars_format format_flags) {
  strings_internal::ParsedFloat result;

  // Exit early if we're given an empty range.
  if (begin == end) return result;

  // Handle the infinity and NaN cases.
  if (ParseInfinityOrNan(begin, end, &result)) {
    return result;
  }

  const char* const mantissa_begin = begin;
  while (begin < end && *begin == '0') {
    ++begin;  // skip leading zeros
  }
  uint64_t mantissa = 0;

  int exponent_adjustment = 0;
  bool mantissa_is_inexact = false;
  int pre_decimal_digits = ConsumeDigits<base>(
      begin, end, MantissaDigitsMax<base>(), &mantissa, &mantissa_is_inexact);
  begin += pre_decimal_digits;
  int digits_left;
  if (pre_decimal_digits >= DigitLimit<base>()) {
    // refuse to parse pathological inputs
    return result;
  } else if (pre_decimal_digits > MantissaDigitsMax<base>()) {
    // We dropped some non-fraction digits on the floor.  Adjust our exponent
    // to compensate.
    exponent_adjustment =
        static_cast<int>(pre_decimal_digits - MantissaDigitsMax<base>());
    digits_left = 0;
  } else {
    digits_left =
        static_cast<int>(MantissaDigitsMax<base>() - pre_decimal_digits);
  }
  if (begin < end && *begin == '.') {
    ++begin;
    if (mantissa == 0) {
      // If we haven't seen any nonzero digits yet, keep skipping zeros.  We
      // have to adjust the exponent to reflect the changed place value.
      const char* begin_zeros = begin;
      while (begin < end && *begin == '0') {
        ++begin;
      }
      int zeros_skipped = static_cast<int>(begin - begin_zeros);
      if (zeros_skipped >= DigitLimit<base>()) {
        // refuse to parse pathological inputs
        return result;
      }
      exponent_adjustment -= static_cast<int>(zeros_skipped);
    }
    int post_decimal_digits = ConsumeDigits<base>(
        begin, end, digits_left, &mantissa, &mantissa_is_inexact);
    begin += post_decimal_digits;

    // Since `mantissa` is an integer, each significant digit we read after
    // the decimal point requires an adjustment to the exponent. "1.23e0" will
    // be stored as `mantissa` == 123 and `exponent` == -2 (that is,
    // "123e-2").
    if (post_decimal_digits >= DigitLimit<base>()) {
      // refuse to parse pathological inputs
      return result;
    } else if (post_decimal_digits > digits_left) {
      exponent_adjustment -= digits_left;
    } else {
      exponent_adjustment -= post_decimal_digits;
    }
  }
  // If we've found no mantissa whatsoever, this isn't a number.
  if (mantissa_begin == begin) {
    return result;
  }
  // A bare "." doesn't count as a mantissa either.
  if (begin - mantissa_begin == 1 && *mantissa_begin == '.') {
    return result;
  }

  if (mantissa_is_inexact) {
    // We dropped significant digits on the floor.  Handle this appropriately.
    if (base == 10) {
      // If we truncated significant decimal digits, store the full range of the
      // mantissa for future big integer math for exact rounding.
      result.subrange_begin = mantissa_begin;
      result.subrange_end = begin;
    } else if (base == 16) {
      // If we truncated hex digits, reflect this fact by setting the low
      // ("sticky") bit.  This allows for correct rounding in all cases.
      mantissa |= 1;
    }
  }
  result.mantissa = mantissa;

  const char* const exponent_begin = begin;
  result.literal_exponent = 0;
  bool found_exponent = false;
  if (AllowExponent(format_flags) && begin < end &&
      IsExponentCharacter<base>(*begin)) {
    bool negative_exponent = false;
    ++begin;
    if (begin < end && *begin == '-') {
      negative_exponent = true;
      ++begin;
    } else if (begin < end && *begin == '+') {
      ++begin;
    }
    const char* const exponent_digits_begin = begin;
    // Exponent is always expressed in decimal, even for hexadecimal floats.
    begin += ConsumeDigits<10>(begin, end, kDecimalExponentDigitsMax,
                               &result.literal_exponent, nullptr);
    if (begin == exponent_digits_begin) {
      // there were no digits where we expected an exponent.  We failed to read
      // an exponent and should not consume the 'e' after all.  Rewind 'begin'.
      found_exponent = false;
      begin = exponent_begin;
    } else {
      found_exponent = true;
      if (negative_exponent) {
        result.literal_exponent = -result.literal_exponent;
      }
    }
  }

  if (!found_exponent && RequireExponent(format_flags)) {
    // Provided flags required an exponent, but none was found.  This results
    // in a failure to scan.
    return result;
  }

  // Success!
  result.type = strings_internal::FloatType::kNumber;
  if (result.mantissa > 0) {
    result.exponent = result.literal_exponent +
                      (DigitMagnitude<base>() * exponent_adjustment);
  } else {
    result.exponent = 0;
  }
  result.end = begin;
  return result;
}

template ParsedFloat ParseFloat<10>(const char* begin, const char* end,
                                    chars_format format_flags);
template ParsedFloat ParseFloat<16>(const char* begin, const char* end,
                                    chars_format format_flags);

}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
