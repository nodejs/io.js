#ifndef FASTFLOAT_ASCII_NUMBER_H
#define FASTFLOAT_ASCII_NUMBER_H

#include <cctype>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <type_traits>

#include "float_common.h"

#ifdef FASTFLOAT_SSE2
#include <emmintrin.h>
#endif

#ifdef FASTFLOAT_NEON
#include <arm_neon.h>
#endif

namespace fast_float {

template <typename UC> fastfloat_really_inline constexpr bool has_simd_opt() {
#ifdef FASTFLOAT_HAS_SIMD
  return std::is_same<UC, char16_t>::value;
#else
  return false;
#endif
}

// Next function can be micro-optimized, but compilers are entirely
// able to optimize it well.
template <typename UC>
fastfloat_really_inline constexpr bool is_integer(UC c) noexcept {
  return !(c > UC('9') || c < UC('0'));
}

fastfloat_really_inline constexpr uint64_t byteswap(uint64_t val) {
  return (val & 0xFF00000000000000) >> 56 | (val & 0x00FF000000000000) >> 40 |
         (val & 0x0000FF0000000000) >> 24 | (val & 0x000000FF00000000) >> 8 |
         (val & 0x00000000FF000000) << 8 | (val & 0x0000000000FF0000) << 24 |
         (val & 0x000000000000FF00) << 40 | (val & 0x00000000000000FF) << 56;
}

// Read 8 UC into a u64. Truncates UC if not char.
template <typename UC>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint64_t
read8_to_u64(const UC *chars) {
  if (cpp20_and_in_constexpr() || !std::is_same<UC, char>::value) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
      val |= uint64_t(uint8_t(*chars)) << (i * 8);
      ++chars;
    }
    return val;
  }
  uint64_t val;
  ::memcpy(&val, chars, sizeof(uint64_t));
#if FASTFLOAT_IS_BIG_ENDIAN == 1
  // Need to read as-if the number was in little-endian order.
  val = byteswap(val);
#endif
  return val;
}

#ifdef FASTFLOAT_SSE2

fastfloat_really_inline uint64_t simd_read8_to_u64(const __m128i data) {
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  const __m128i packed = _mm_packus_epi16(data, data);
#ifdef FASTFLOAT_64BIT
  return uint64_t(_mm_cvtsi128_si64(packed));
#else
  uint64_t value;
  // Visual Studio + older versions of GCC don't support _mm_storeu_si64
  _mm_storel_epi64(reinterpret_cast<__m128i *>(&value), packed);
  return value;
#endif
  FASTFLOAT_SIMD_RESTORE_WARNINGS
}

fastfloat_really_inline uint64_t simd_read8_to_u64(const char16_t *chars) {
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  return simd_read8_to_u64(
      _mm_loadu_si128(reinterpret_cast<const __m128i *>(chars)));
  FASTFLOAT_SIMD_RESTORE_WARNINGS
}

#elif defined(FASTFLOAT_NEON)

fastfloat_really_inline uint64_t simd_read8_to_u64(const uint16x8_t data) {
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  uint8x8_t utf8_packed = vmovn_u16(data);
  return vget_lane_u64(vreinterpret_u64_u8(utf8_packed), 0);
  FASTFLOAT_SIMD_RESTORE_WARNINGS
}

fastfloat_really_inline uint64_t simd_read8_to_u64(const char16_t *chars) {
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  return simd_read8_to_u64(
      vld1q_u16(reinterpret_cast<const uint16_t *>(chars)));
  FASTFLOAT_SIMD_RESTORE_WARNINGS
}

#endif // FASTFLOAT_SSE2

// MSVC SFINAE is broken pre-VS2017
#if defined(_MSC_VER) && _MSC_VER <= 1900
template <typename UC>
#else
template <typename UC, FASTFLOAT_ENABLE_IF(!has_simd_opt<UC>()) = 0>
#endif
// dummy for compile
uint64_t simd_read8_to_u64(UC const *) {
  return 0;
}

// credit  @aqrit
fastfloat_really_inline FASTFLOAT_CONSTEXPR14 uint32_t
parse_eight_digits_unrolled(uint64_t val) {
  const uint64_t mask = 0x000000FF000000FF;
  const uint64_t mul1 = 0x000F424000000064; // 100 + (1000000ULL << 32)
  const uint64_t mul2 = 0x0000271000000001; // 1 + (10000ULL << 32)
  val -= 0x3030303030303030;
  val = (val * 10) + (val >> 8); // val = (val * 2561) >> 8;
  val = (((val & mask) * mul1) + (((val >> 16) & mask) * mul2)) >> 32;
  return uint32_t(val);
}

// Call this if chars are definitely 8 digits.
template <typename UC>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 uint32_t
parse_eight_digits_unrolled(UC const *chars) noexcept {
  if (cpp20_and_in_constexpr() || !has_simd_opt<UC>()) {
    return parse_eight_digits_unrolled(read8_to_u64(chars)); // truncation okay
  }
  return parse_eight_digits_unrolled(simd_read8_to_u64(chars));
}

// credit @aqrit
fastfloat_really_inline constexpr bool
is_made_of_eight_digits_fast(uint64_t val) noexcept {
  return !((((val + 0x4646464646464646) | (val - 0x3030303030303030)) &
            0x8080808080808080));
}

#ifdef FASTFLOAT_HAS_SIMD

// Call this if chars might not be 8 digits.
// Using this style (instead of is_made_of_eight_digits_fast() then
// parse_eight_digits_unrolled()) ensures we don't load SIMD registers twice.
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 bool
simd_parse_if_eight_digits_unrolled(const char16_t *chars,
                                    uint64_t &i) noexcept {
  if (cpp20_and_in_constexpr()) {
    return false;
  }
#ifdef FASTFLOAT_SSE2
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  const __m128i data =
      _mm_loadu_si128(reinterpret_cast<const __m128i *>(chars));

  // (x - '0') <= 9
  // http://0x80.pl/articles/simd-parsing-int-sequences.html
  const __m128i t0 = _mm_add_epi16(data, _mm_set1_epi16(32720));
  const __m128i t1 = _mm_cmpgt_epi16(t0, _mm_set1_epi16(-32759));

  if (_mm_movemask_epi8(t1) == 0) {
    i = i * 100000000 + parse_eight_digits_unrolled(simd_read8_to_u64(data));
    return true;
  } else
    return false;
  FASTFLOAT_SIMD_RESTORE_WARNINGS
#elif defined(FASTFLOAT_NEON)
  FASTFLOAT_SIMD_DISABLE_WARNINGS
  const uint16x8_t data = vld1q_u16(reinterpret_cast<const uint16_t *>(chars));

  // (x - '0') <= 9
  // http://0x80.pl/articles/simd-parsing-int-sequences.html
  const uint16x8_t t0 = vsubq_u16(data, vmovq_n_u16('0'));
  const uint16x8_t mask = vcltq_u16(t0, vmovq_n_u16('9' - '0' + 1));

  if (vminvq_u16(mask) == 0xFFFF) {
    i = i * 100000000 + parse_eight_digits_unrolled(simd_read8_to_u64(data));
    return true;
  } else
    return false;
  FASTFLOAT_SIMD_RESTORE_WARNINGS
#else
  (void)chars;
  (void)i;
  return false;
#endif // FASTFLOAT_SSE2
}

#endif // FASTFLOAT_HAS_SIMD

// MSVC SFINAE is broken pre-VS2017
#if defined(_MSC_VER) && _MSC_VER <= 1900
template <typename UC>
#else
template <typename UC, FASTFLOAT_ENABLE_IF(!has_simd_opt<UC>()) = 0>
#endif
// dummy for compile
bool simd_parse_if_eight_digits_unrolled(UC const *, uint64_t &) {
  return 0;
}

template <typename UC, FASTFLOAT_ENABLE_IF(!std::is_same<UC, char>::value) = 0>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 void
loop_parse_if_eight_digits(const UC *&p, const UC *const pend, uint64_t &i) {
  if (!has_simd_opt<UC>()) {
    return;
  }
  while ((std::distance(p, pend) >= 8) &&
         simd_parse_if_eight_digits_unrolled(
             p, i)) { // in rare cases, this will overflow, but that's ok
    p += 8;
  }
}

fastfloat_really_inline FASTFLOAT_CONSTEXPR20 void
loop_parse_if_eight_digits(const char *&p, const char *const pend,
                           uint64_t &i) {
  // optimizes better than parse_if_eight_digits_unrolled() for UC = char.
  while ((std::distance(p, pend) >= 8) &&
         is_made_of_eight_digits_fast(read8_to_u64(p))) {
    i = i * 100000000 +
        parse_eight_digits_unrolled(read8_to_u64(
            p)); // in rare cases, this will overflow, but that's ok
    p += 8;
  }
}

enum class parse_error {
  no_error,
  // [JSON-only] The minus sign must be followed by an integer.
  missing_integer_after_sign,
  // A sign must be followed by an integer or dot.
  missing_integer_or_dot_after_sign,
  // [JSON-only] The integer part must not have leading zeros.
  leading_zeros_in_integer_part,
  // [JSON-only] The integer part must have at least one digit.
  no_digits_in_integer_part,
  // [JSON-only] If there is a decimal point, there must be digits in the
  // fractional part.
  no_digits_in_fractional_part,
  // The mantissa must have at least one digit.
  no_digits_in_mantissa,
  // Scientific notation requires an exponential part.
  missing_exponential_part,
};

template <typename UC> struct parsed_number_string_t {
  int64_t exponent{0};
  uint64_t mantissa{0};
  UC const *lastmatch{nullptr};
  bool negative{false};
  bool valid{false};
  bool too_many_digits{false};
  // contains the range of the significant digits
  span<const UC> integer{};  // non-nullable
  span<const UC> fraction{}; // nullable
  parse_error error{parse_error::no_error};
};

using byte_span = span<const char>;
using parsed_number_string = parsed_number_string_t<char>;

template <typename UC>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 parsed_number_string_t<UC>
report_parse_error(UC const *p, parse_error error) {
  parsed_number_string_t<UC> answer;
  answer.valid = false;
  answer.lastmatch = p;
  answer.error = error;
  return answer;
}

// Assuming that you use no more than 19 digits, this will
// parse an ASCII string.
template <typename UC>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 parsed_number_string_t<UC>
parse_number_string(UC const *p, UC const *pend,
                    parse_options_t<UC> options) noexcept {
  chars_format const fmt = options.format;
  UC const decimal_point = options.decimal_point;

  parsed_number_string_t<UC> answer;
  answer.valid = false;
  answer.too_many_digits = false;
  answer.negative = (*p == UC('-'));
#ifdef FASTFLOAT_ALLOWS_LEADING_PLUS // disabled by default
  if ((*p == UC('-')) || (!(fmt & FASTFLOAT_JSONFMT) && *p == UC('+'))) {
#else
  if (*p == UC('-')) { // C++17 20.19.3.(7.1) explicitly forbids '+' sign here
#endif
    ++p;
    if (p == pend) {
      return report_parse_error<UC>(
          p, parse_error::missing_integer_or_dot_after_sign);
    }
    if (fmt & FASTFLOAT_JSONFMT) {
      if (!is_integer(*p)) { // a sign must be followed by an integer
        return report_parse_error<UC>(p,
                                      parse_error::missing_integer_after_sign);
      }
    } else {
      if (!is_integer(*p) &&
          (*p !=
           decimal_point)) { // a sign must be followed by an integer or the dot
        return report_parse_error<UC>(
            p, parse_error::missing_integer_or_dot_after_sign);
      }
    }
  }
  UC const *const start_digits = p;

  uint64_t i = 0; // an unsigned int avoids signed overflows (which are bad)

  while ((p != pend) && is_integer(*p)) {
    // a multiplication by 10 is cheaper than an arbitrary integer
    // multiplication
    i = 10 * i +
        uint64_t(*p -
                 UC('0')); // might overflow, we will handle the overflow later
    ++p;
  }
  UC const *const end_of_integer_part = p;
  int64_t digit_count = int64_t(end_of_integer_part - start_digits);
  answer.integer = span<const UC>(start_digits, size_t(digit_count));
  if (fmt & FASTFLOAT_JSONFMT) {
    // at least 1 digit in integer part, without leading zeros
    if (digit_count == 0) {
      return report_parse_error<UC>(p, parse_error::no_digits_in_integer_part);
    }
    if ((start_digits[0] == UC('0') && digit_count > 1)) {
      return report_parse_error<UC>(start_digits,
                                    parse_error::leading_zeros_in_integer_part);
    }
  }

  int64_t exponent = 0;
  const bool has_decimal_point = (p != pend) && (*p == decimal_point);
  if (has_decimal_point) {
    ++p;
    UC const *before = p;
    // can occur at most twice without overflowing, but let it occur more, since
    // for integers with many digits, digit parsing is the primary bottleneck.
    loop_parse_if_eight_digits(p, pend, i);

    while ((p != pend) && is_integer(*p)) {
      uint8_t digit = uint8_t(*p - UC('0'));
      ++p;
      i = i * 10 + digit; // in rare cases, this will overflow, but that's ok
    }
    exponent = before - p;
    answer.fraction = span<const UC>(before, size_t(p - before));
    digit_count -= exponent;
  }
  if (fmt & FASTFLOAT_JSONFMT) {
    // at least 1 digit in fractional part
    if (has_decimal_point && exponent == 0) {
      return report_parse_error<UC>(p,
                                    parse_error::no_digits_in_fractional_part);
    }
  } else if (digit_count ==
             0) { // we must have encountered at least one integer!
    return report_parse_error<UC>(p, parse_error::no_digits_in_mantissa);
  }
  int64_t exp_number = 0; // explicit exponential part
  if (((fmt & chars_format::scientific) && (p != pend) &&
       ((UC('e') == *p) || (UC('E') == *p))) ||
      ((fmt & FASTFLOAT_FORTRANFMT) && (p != pend) &&
       ((UC('+') == *p) || (UC('-') == *p) || (UC('d') == *p) ||
        (UC('D') == *p)))) {
    UC const *location_of_e = p;
    if ((UC('e') == *p) || (UC('E') == *p) || (UC('d') == *p) ||
        (UC('D') == *p)) {
      ++p;
    }
    bool neg_exp = false;
    if ((p != pend) && (UC('-') == *p)) {
      neg_exp = true;
      ++p;
    } else if ((p != pend) &&
               (UC('+') ==
                *p)) { // '+' on exponent is allowed by C++17 20.19.3.(7.1)
      ++p;
    }
    if ((p == pend) || !is_integer(*p)) {
      if (!(fmt & chars_format::fixed)) {
        // The exponential part is invalid for scientific notation, so it must
        // be a trailing token for fixed notation. However, fixed notation is
        // disabled, so report a scientific notation error.
        return report_parse_error<UC>(p, parse_error::missing_exponential_part);
      }
      // Otherwise, we will be ignoring the 'e'.
      p = location_of_e;
    } else {
      while ((p != pend) && is_integer(*p)) {
        uint8_t digit = uint8_t(*p - UC('0'));
        if (exp_number < 0x10000000) {
          exp_number = 10 * exp_number + digit;
        }
        ++p;
      }
      if (neg_exp) {
        exp_number = -exp_number;
      }
      exponent += exp_number;
    }
  } else {
    // If it scientific and not fixed, we have to bail out.
    if ((fmt & chars_format::scientific) && !(fmt & chars_format::fixed)) {
      return report_parse_error<UC>(p, parse_error::missing_exponential_part);
    }
  }
  answer.lastmatch = p;
  answer.valid = true;

  // If we frequently had to deal with long strings of digits,
  // we could extend our code by using a 128-bit integer instead
  // of a 64-bit integer. However, this is uncommon.
  //
  // We can deal with up to 19 digits.
  if (digit_count > 19) { // this is uncommon
    // It is possible that the integer had an overflow.
    // We have to handle the case where we have 0.0000somenumber.
    // We need to be mindful of the case where we only have zeroes...
    // E.g., 0.000000000...000.
    UC const *start = start_digits;
    while ((start != pend) && (*start == UC('0') || *start == decimal_point)) {
      if (*start == UC('0')) {
        digit_count--;
      }
      start++;
    }

    if (digit_count > 19) {
      answer.too_many_digits = true;
      // Let us start again, this time, avoiding overflows.
      // We don't need to check if is_integer, since we use the
      // pre-tokenized spans from above.
      i = 0;
      p = answer.integer.ptr;
      UC const *int_end = p + answer.integer.len();
      const uint64_t minimal_nineteen_digit_integer{1000000000000000000};
      while ((i < minimal_nineteen_digit_integer) && (p != int_end)) {
        i = i * 10 + uint64_t(*p - UC('0'));
        ++p;
      }
      if (i >= minimal_nineteen_digit_integer) { // We have a big integers
        exponent = end_of_integer_part - p + exp_number;
      } else { // We have a value with a fractional component.
        p = answer.fraction.ptr;
        UC const *frac_end = p + answer.fraction.len();
        while ((i < minimal_nineteen_digit_integer) && (p != frac_end)) {
          i = i * 10 + uint64_t(*p - UC('0'));
          ++p;
        }
        exponent = answer.fraction.ptr - p + exp_number;
      }
      // We have now corrected both exponent and i, to a truncated value
    }
  }
  answer.exponent = exponent;
  answer.mantissa = i;
  return answer;
}

template <typename T, typename UC>
fastfloat_really_inline FASTFLOAT_CONSTEXPR20 from_chars_result_t<UC>
parse_int_string(UC const *p, UC const *pend, T &value, int base) {
  from_chars_result_t<UC> answer;

  UC const *const first = p;

  bool negative = (*p == UC('-'));
  if (!std::is_signed<T>::value && negative) {
    answer.ec = std::errc::invalid_argument;
    answer.ptr = first;
    return answer;
  }
#ifdef FASTFLOAT_ALLOWS_LEADING_PLUS // disabled by default
  if ((*p == UC('-')) || (*p == UC('+'))) {
#else
  if (*p == UC('-')) {
#endif
    ++p;
  }

  UC const *const start_num = p;

  while (p != pend && *p == UC('0')) {
    ++p;
  }

  const bool has_leading_zeros = p > start_num;

  UC const *const start_digits = p;

  uint64_t i = 0;
  if (base == 10) {
    loop_parse_if_eight_digits(p, pend, i); // use SIMD if possible
  }
  while (p != pend) {
    uint8_t digit = ch_to_digit(*p);
    if (digit >= base) {
      break;
    }
    i = uint64_t(base) * i + digit; // might overflow, check this later
    p++;
  }

  size_t digit_count = size_t(p - start_digits);

  if (digit_count == 0) {
    if (has_leading_zeros) {
      value = 0;
      answer.ec = std::errc();
      answer.ptr = p;
    } else {
      answer.ec = std::errc::invalid_argument;
      answer.ptr = first;
    }
    return answer;
  }

  answer.ptr = p;

  // check u64 overflow
  size_t max_digits = max_digits_u64(base);
  if (digit_count > max_digits) {
    answer.ec = std::errc::result_out_of_range;
    return answer;
  }
  // this check can be eliminated for all other types, but they will all require
  // a max_digits(base) equivalent
  if (digit_count == max_digits && i < min_safe_u64(base)) {
    answer.ec = std::errc::result_out_of_range;
    return answer;
  }

  // check other types overflow
  if (!std::is_same<T, uint64_t>::value) {
    if (i > uint64_t(std::numeric_limits<T>::max()) + uint64_t(negative)) {
      answer.ec = std::errc::result_out_of_range;
      return answer;
    }
  }

  if (negative) {
#ifdef FASTFLOAT_VISUAL_STUDIO
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
    // this weird workaround is required because:
    // - converting unsigned to signed when its value is greater than signed max
    // is UB pre-C++23.
    // - reinterpret_casting (~i + 1) would work, but it is not constexpr
    // this is always optimized into a neg instruction (note: T is an integer
    // type)
    value = T(-std::numeric_limits<T>::max() -
              T(i - uint64_t(std::numeric_limits<T>::max())));
#ifdef FASTFLOAT_VISUAL_STUDIO
#pragma warning(pop)
#endif
  } else {
    value = T(i);
  }

  answer.ec = std::errc();
  return answer;
}

} // namespace fast_float

#endif
