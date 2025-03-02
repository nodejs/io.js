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

#include "absl/strings/charconv.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <system_error>  // NOLINT(build/c++11)

#include "absl/base/casts.h"
#include "absl/base/config.h"
#include "absl/base/nullability.h"
#include "absl/numeric/bits.h"
#include "absl/numeric/int128.h"
#include "absl/strings/internal/charconv_bigint.h"
#include "absl/strings/internal/charconv_parse.h"

// The macro ABSL_BIT_PACK_FLOATS is defined on x86-64, where IEEE floating
// point numbers have the same endianness in memory as a bitfield struct
// containing the corresponding parts.
//
// When set, we replace calls to ldexp() with manual bit packing, which is
// faster and is unaffected by floating point environment.
#ifdef ABSL_BIT_PACK_FLOATS
#error ABSL_BIT_PACK_FLOATS cannot be directly set
#elif defined(__x86_64__) || defined(_M_X64)
#define ABSL_BIT_PACK_FLOATS 1
#endif

// A note about subnormals:
//
// The code below talks about "normals" and "subnormals".  A normal IEEE float
// has a fixed-width mantissa and power of two exponent.  For example, a normal
// `double` has a 53-bit mantissa.  Because the high bit is always 1, it is not
// stored in the representation.  The implicit bit buys an extra bit of
// resolution in the datatype.
//
// The downside of this scheme is that there is a large gap between DBL_MIN and
// zero.  (Large, at least, relative to the different between DBL_MIN and the
// next representable number).  This gap is softened by the "subnormal" numbers,
// which have the same power-of-two exponent as DBL_MIN, but no implicit 53rd
// bit.  An all-bits-zero exponent in the encoding represents subnormals.  (Zero
// is represented as a subnormal with an all-bits-zero mantissa.)
//
// The code below, in calculations, represents the mantissa as a uint64_t.  The
// end result normally has the 53rd bit set.  It represents subnormals by using
// narrower mantissas.

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

template <typename FloatType>
struct FloatTraits;

template <>
struct FloatTraits<double> {
  using mantissa_t = uint64_t;

  // The number of bits in the given float type.
  static constexpr int kTargetBits = 64;

  // The number of exponent bits in the given float type.
  static constexpr int kTargetExponentBits = 11;

  // The number of mantissa bits in the given float type.  This includes the
  // implied high bit.
  static constexpr int kTargetMantissaBits = 53;

  // The largest supported IEEE exponent, in our integral mantissa
  // representation.
  //
  // If `m` is the largest possible int kTargetMantissaBits bits wide, then
  // m * 2**kMaxExponent is exactly equal to DBL_MAX.
  static constexpr int kMaxExponent = 971;

  // The smallest supported IEEE normal exponent, in our integral mantissa
  // representation.
  //
  // If `m` is the smallest possible int kTargetMantissaBits bits wide, then
  // m * 2**kMinNormalExponent is exactly equal to DBL_MIN.
  static constexpr int kMinNormalExponent = -1074;

  // The IEEE exponent bias.  It equals ((1 << (kTargetExponentBits - 1)) - 1).
  static constexpr int kExponentBias = 1023;

  // The Eisel-Lemire "Shifting to 54/25 Bits" adjustment.  It equals (63 - 1 -
  // kTargetMantissaBits).
  static constexpr int kEiselLemireShift = 9;

  // The Eisel-Lemire high64_mask.  It equals ((1 << kEiselLemireShift) - 1).
  static constexpr uint64_t kEiselLemireMask = uint64_t{0x1FF};

  // The smallest negative integer N (smallest negative means furthest from
  // zero) such that parsing 9999999999999999999eN, with 19 nines, is still
  // positive. Parsing a smaller (more negative) N will produce zero.
  //
  // Adjusting the decimal point and exponent, without adjusting the value,
  // 9999999999999999999eN equals 9.999999999999999999eM where M = N + 18.
  //
  // 9999999999999999999, with 19 nines but no decimal point, is the largest
  // "repeated nines" integer that fits in a uint64_t.
  static constexpr int kEiselLemireMinInclusiveExp10 = -324 - 18;

  // The smallest positive integer N such that parsing 1eN produces infinity.
  // Parsing a smaller N will produce something finite.
  static constexpr int kEiselLemireMaxExclusiveExp10 = 309;

  static double MakeNan(absl::Nonnull<const char*> tagp) {
#if ABSL_HAVE_BUILTIN(__builtin_nan)
    // Use __builtin_nan() if available since it has a fix for
    // https://bugs.llvm.org/show_bug.cgi?id=37778
    // std::nan may use the glibc implementation.
    return __builtin_nan(tagp);
#else
    // Support nan no matter which namespace it's in.  Some platforms
    // incorrectly don't put it in namespace std.
    using namespace std;  // NOLINT
    return nan(tagp);
#endif
  }

  // Builds a nonzero floating point number out of the provided parts.
  //
  // This is intended to do the same operation as ldexp(mantissa, exponent),
  // but using purely integer math, to avoid -ffastmath and floating
  // point environment issues.  Using type punning is also faster. We fall back
  // to ldexp on a per-platform basis for portability.
  //
  // `exponent` must be between kMinNormalExponent and kMaxExponent.
  //
  // `mantissa` must either be exactly kTargetMantissaBits wide, in which case
  // a normal value is made, or it must be less narrow than that, in which case
  // `exponent` must be exactly kMinNormalExponent, and a subnormal value is
  // made.
  static double Make(mantissa_t mantissa, int exponent, bool sign) {
#ifndef ABSL_BIT_PACK_FLOATS
    // Support ldexp no matter which namespace it's in.  Some platforms
    // incorrectly don't put it in namespace std.
    using namespace std;  // NOLINT
    return sign ? -ldexp(mantissa, exponent) : ldexp(mantissa, exponent);
#else
    constexpr uint64_t kMantissaMask =
        (uint64_t{1} << (kTargetMantissaBits - 1)) - 1;
    uint64_t dbl = static_cast<uint64_t>(sign) << 63;
    if (mantissa > kMantissaMask) {
      // Normal value.
      // Adjust by 1023 for the exponent representation bias, and an additional
      // 52 due to the implied decimal point in the IEEE mantissa
      // representation.
      dbl += static_cast<uint64_t>(exponent + 1023 + kTargetMantissaBits - 1)
             << 52;
      mantissa &= kMantissaMask;
    } else {
      // subnormal value
      assert(exponent == kMinNormalExponent);
    }
    dbl += mantissa;
    return absl::bit_cast<double>(dbl);
#endif  // ABSL_BIT_PACK_FLOATS
  }
};

// Specialization of floating point traits for the `float` type.  See the
// FloatTraits<double> specialization above for meaning of each of the following
// members and methods.
template <>
struct FloatTraits<float> {
  using mantissa_t = uint32_t;

  static constexpr int kTargetBits = 32;
  static constexpr int kTargetExponentBits = 8;
  static constexpr int kTargetMantissaBits = 24;
  static constexpr int kMaxExponent = 104;
  static constexpr int kMinNormalExponent = -149;
  static constexpr int kExponentBias = 127;
  static constexpr int kEiselLemireShift = 38;
  static constexpr uint64_t kEiselLemireMask = uint64_t{0x3FFFFFFFFF};
  static constexpr int kEiselLemireMinInclusiveExp10 = -46 - 18;
  static constexpr int kEiselLemireMaxExclusiveExp10 = 39;

  static float MakeNan(absl::Nonnull<const char*> tagp) {
#if ABSL_HAVE_BUILTIN(__builtin_nanf)
    // Use __builtin_nanf() if available since it has a fix for
    // https://bugs.llvm.org/show_bug.cgi?id=37778
    // std::nanf may use the glibc implementation.
    return __builtin_nanf(tagp);
#else
    // Support nanf no matter which namespace it's in.  Some platforms
    // incorrectly don't put it in namespace std.
    using namespace std;  // NOLINT
    return std::nanf(tagp);
#endif
  }

  static float Make(mantissa_t mantissa, int exponent, bool sign) {
#ifndef ABSL_BIT_PACK_FLOATS
    // Support ldexpf no matter which namespace it's in.  Some platforms
    // incorrectly don't put it in namespace std.
    using namespace std;  // NOLINT
    return sign ? -ldexpf(mantissa, exponent) : ldexpf(mantissa, exponent);
#else
    constexpr uint32_t kMantissaMask =
        (uint32_t{1} << (kTargetMantissaBits - 1)) - 1;
    uint32_t flt = static_cast<uint32_t>(sign) << 31;
    if (mantissa > kMantissaMask) {
      // Normal value.
      // Adjust by 127 for the exponent representation bias, and an additional
      // 23 due to the implied decimal point in the IEEE mantissa
      // representation.
      flt += static_cast<uint32_t>(exponent + 127 + kTargetMantissaBits - 1)
             << 23;
      mantissa &= kMantissaMask;
    } else {
      // subnormal value
      assert(exponent == kMinNormalExponent);
    }
    flt += mantissa;
    return absl::bit_cast<float>(flt);
#endif  // ABSL_BIT_PACK_FLOATS
  }
};

// Decimal-to-binary conversions require coercing powers of 10 into a mantissa
// and a power of 2.  The two helper functions Power10Mantissa(n) and
// Power10Exponent(n) perform this task.  Together, these represent a hand-
// rolled floating point value which is equal to or just less than 10**n.
//
// The return values satisfy two range guarantees:
//
//   Power10Mantissa(n) * 2**Power10Exponent(n) <= 10**n
//     < (Power10Mantissa(n) + 1) * 2**Power10Exponent(n)
//
//   2**63 <= Power10Mantissa(n) < 2**64.
//
// See the "Table of powers of 10" comment below for a "1e60" example.
//
// Lookups into the power-of-10 table must first check the Power10Overflow() and
// Power10Underflow() functions, to avoid out-of-bounds table access.
//
// Indexes into these tables are biased by -kPower10TableMinInclusive. Valid
// indexes range from kPower10TableMinInclusive to kPower10TableMaxExclusive.
extern const uint64_t kPower10MantissaHighTable[];  // High 64 of 128 bits.
extern const uint64_t kPower10MantissaLowTable[];   // Low  64 of 128 bits.

// The smallest (inclusive) allowed value for use with the Power10Mantissa()
// and Power10Exponent() functions below.  (If a smaller exponent is needed in
// calculations, the end result is guaranteed to underflow.)
constexpr int kPower10TableMinInclusive = -342;

// The largest (exclusive) allowed value for use with the Power10Mantissa() and
// Power10Exponent() functions below.  (If a larger-or-equal exponent is needed
// in calculations, the end result is guaranteed to overflow.)
constexpr int kPower10TableMaxExclusive = 309;

uint64_t Power10Mantissa(int n) {
  return kPower10MantissaHighTable[n - kPower10TableMinInclusive];
}

int Power10Exponent(int n) {
  // The 217706 etc magic numbers encode the results as a formula instead of a
  // table. Their equivalence (over the kPower10TableMinInclusive ..
  // kPower10TableMaxExclusive range) is confirmed by
  // https://github.com/google/wuffs/blob/315b2e52625ebd7b02d8fac13e3cd85ea374fb80/script/print-mpb-powers-of-10.go
  return (217706 * n >> 16) - 63;
}

// Returns true if n is large enough that 10**n always results in an IEEE
// overflow.
bool Power10Overflow(int n) { return n >= kPower10TableMaxExclusive; }

// Returns true if n is small enough that 10**n times a ParsedFloat mantissa
// always results in an IEEE underflow.
bool Power10Underflow(int n) { return n < kPower10TableMinInclusive; }

// Returns true if Power10Mantissa(n) * 2**Power10Exponent(n) is exactly equal
// to 10**n numerically.  Put another way, this returns true if there is no
// truncation error in Power10Mantissa(n).
bool Power10Exact(int n) { return n >= 0 && n <= 27; }

// Sentinel exponent values for representing numbers too large or too close to
// zero to represent in a double.
constexpr int kOverflow = 99999;
constexpr int kUnderflow = -99999;

// Struct representing the calculated conversion result of a positive (nonzero)
// floating point number.
//
// The calculated number is mantissa * 2**exponent (mantissa is treated as an
// integer.)  `mantissa` is chosen to be the correct width for the IEEE float
// representation being calculated.  (`mantissa` will always have the same bit
// width for normal values, and narrower bit widths for subnormals.)
//
// If the result of conversion was an underflow or overflow, exponent is set
// to kUnderflow or kOverflow.
struct CalculatedFloat {
  uint64_t mantissa = 0;
  int exponent = 0;
};

// Returns the bit width of the given uint128.  (Equivalently, returns 128
// minus the number of leading zero bits.)
int BitWidth(uint128 value) {
  if (Uint128High64(value) == 0) {
    // This static_cast is only needed when using a std::bit_width()
    // implementation that does not have the fix for LWG 3656 applied.
    return static_cast<int>(bit_width(Uint128Low64(value)));
  }
  return 128 - countl_zero(Uint128High64(value));
}

// Calculates how far to the right a mantissa needs to be shifted to create a
// properly adjusted mantissa for an IEEE floating point number.
//
// `mantissa_width` is the bit width of the mantissa to be shifted, and
// `binary_exponent` is the exponent of the number before the shift.
//
// This accounts for subnormal values, and will return a larger-than-normal
// shift if binary_exponent would otherwise be too low.
template <typename FloatType>
int NormalizedShiftSize(int mantissa_width, int binary_exponent) {
  const int normal_shift =
      mantissa_width - FloatTraits<FloatType>::kTargetMantissaBits;
  const int minimum_shift =
      FloatTraits<FloatType>::kMinNormalExponent - binary_exponent;
  return std::max(normal_shift, minimum_shift);
}

// Right shifts a uint128 so that it has the requested bit width.  (The
// resulting value will have 128 - bit_width leading zeroes.)  The initial
// `value` must be wider than the requested bit width.
//
// Returns the number of bits shifted.
int TruncateToBitWidth(int bit_width, absl::Nonnull<uint128*> value) {
  const int current_bit_width = BitWidth(*value);
  const int shift = current_bit_width - bit_width;
  *value >>= shift;
  return shift;
}

// Checks if the given ParsedFloat represents one of the edge cases that are
// not dependent on number base: zero, infinity, or NaN.  If so, sets *value
// the appropriate double, and returns true.
template <typename FloatType>
bool HandleEdgeCase(const strings_internal::ParsedFloat& input, bool negative,
                    absl::Nonnull<FloatType*> value) {
  if (input.type == strings_internal::FloatType::kNan) {
    // A bug in gcc would cause the compiler to optimize away the buffer we are
    // building below.  Declaring the buffer volatile avoids the issue, and has
    // no measurable performance impact in microbenchmarks.
    //
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=86113
    constexpr ptrdiff_t kNanBufferSize = 128;
#if (defined(__GNUC__) && !defined(__clang__))
    volatile char n_char_sequence[kNanBufferSize];
#else
    char n_char_sequence[kNanBufferSize];
#endif
    if (input.subrange_begin == nullptr) {
      n_char_sequence[0] = '\0';
    } else {
      ptrdiff_t nan_size = input.subrange_end - input.subrange_begin;
      nan_size = std::min(nan_size, kNanBufferSize - 1);
      std::copy_n(input.subrange_begin, nan_size, n_char_sequence);
      n_char_sequence[nan_size] = '\0';
    }
    char* nan_argument = const_cast<char*>(n_char_sequence);
    *value = negative ? -FloatTraits<FloatType>::MakeNan(nan_argument)
                      : FloatTraits<FloatType>::MakeNan(nan_argument);
    return true;
  }
  if (input.type == strings_internal::FloatType::kInfinity) {
    *value = negative ? -std::numeric_limits<FloatType>::infinity()
                      : std::numeric_limits<FloatType>::infinity();
    return true;
  }
  if (input.mantissa == 0) {
    *value = negative ? -0.0 : 0.0;
    return true;
  }
  return false;
}

// Given a CalculatedFloat result of a from_chars conversion, generate the
// correct output values.
//
// CalculatedFloat can represent an underflow or overflow, in which case the
// error code in *result is set.  Otherwise, the calculated floating point
// number is stored in *value.
template <typename FloatType>
void EncodeResult(const CalculatedFloat& calculated, bool negative,
                  absl::Nonnull<absl::from_chars_result*> result,
                  absl::Nonnull<FloatType*> value) {
  if (calculated.exponent == kOverflow) {
    result->ec = std::errc::result_out_of_range;
    *value = negative ? -std::numeric_limits<FloatType>::max()
                      : std::numeric_limits<FloatType>::max();
    return;
  } else if (calculated.mantissa == 0 || calculated.exponent == kUnderflow) {
    result->ec = std::errc::result_out_of_range;
    *value = negative ? -0.0 : 0.0;
    return;
  }
  *value = FloatTraits<FloatType>::Make(
      static_cast<typename FloatTraits<FloatType>::mantissa_t>(
          calculated.mantissa),
      calculated.exponent, negative);
}

// Returns the given uint128 shifted to the right by `shift` bits, and rounds
// the remaining bits using round_to_nearest logic.  The value is returned as a
// uint64_t, since this is the type used by this library for storing calculated
// floating point mantissas.
//
// It is expected that the width of the input value shifted by `shift` will
// be the correct bit-width for the target mantissa, which is strictly narrower
// than a uint64_t.
//
// If `input_exact` is false, then a nonzero error epsilon is assumed.  For
// rounding purposes, the true value being rounded is strictly greater than the
// input value.  The error may represent a single lost carry bit.
//
// When input_exact, shifted bits of the form 1000000... represent a tie, which
// is broken by rounding to even -- the rounding direction is chosen so the low
// bit of the returned value is 0.
//
// When !input_exact, shifted bits of the form 10000000... represent a value
// strictly greater than one half (due to the error epsilon), and so ties are
// always broken by rounding up.
//
// When !input_exact, shifted bits of the form 01111111... are uncertain;
// the true value may or may not be greater than 10000000..., due to the
// possible lost carry bit.  The correct rounding direction is unknown.  In this
// case, the result is rounded down, and `output_exact` is set to false.
//
// Zero and negative values of `shift` are accepted, in which case the word is
// shifted left, as necessary.
uint64_t ShiftRightAndRound(uint128 value, int shift, bool input_exact,
                            absl::Nonnull<bool*> output_exact) {
  if (shift <= 0) {
    *output_exact = input_exact;
    return static_cast<uint64_t>(value << -shift);
  }
  if (shift >= 128) {
    // Exponent is so small that we are shifting away all significant bits.
    // Answer will not be representable, even as a subnormal, so return a zero
    // mantissa (which represents underflow).
    *output_exact = true;
    return 0;
  }

  *output_exact = true;
  const uint128 shift_mask = (uint128(1) << shift) - 1;
  const uint128 halfway_point = uint128(1) << (shift - 1);

  const uint128 shifted_bits = value & shift_mask;
  value >>= shift;
  if (shifted_bits > halfway_point) {
    // Shifted bits greater than 10000... require rounding up.
    return static_cast<uint64_t>(value + 1);
  }
  if (shifted_bits == halfway_point) {
    // In exact mode, shifted bits of 10000... mean we're exactly halfway
    // between two numbers, and we must round to even.  So only round up if
    // the low bit of `value` is set.
    //
    // In inexact mode, the nonzero error means the actual value is greater
    // than the halfway point and we must always round up.
    if ((value & 1) == 1 || !input_exact) {
      ++value;
    }
    return static_cast<uint64_t>(value);
  }
  if (!input_exact && shifted_bits == halfway_point - 1) {
    // Rounding direction is unclear, due to error.
    *output_exact = false;
  }
  // Otherwise, round down.
  return static_cast<uint64_t>(value);
}

// Checks if a floating point guess needs to be rounded up, using high precision
// math.
//
// `guess_mantissa` and `guess_exponent` represent a candidate guess for the
// number represented by `parsed_decimal`.
//
// The exact number represented by `parsed_decimal` must lie between the two
// numbers:
//   A = `guess_mantissa * 2**guess_exponent`
//   B = `(guess_mantissa + 1) * 2**guess_exponent`
//
// This function returns false if `A` is the better guess, and true if `B` is
// the better guess, with rounding ties broken by rounding to even.
bool MustRoundUp(uint64_t guess_mantissa, int guess_exponent,
                 const strings_internal::ParsedFloat& parsed_decimal) {
  // 768 is the number of digits needed in the worst case.  We could determine a
  // better limit dynamically based on the value of parsed_decimal.exponent.
  // This would optimize pathological input cases only.  (Sane inputs won't have
  // hundreds of digits of mantissa.)
  absl::strings_internal::BigUnsigned<84> exact_mantissa;
  int exact_exponent = exact_mantissa.ReadFloatMantissa(parsed_decimal, 768);

  // Adjust the `guess` arguments to be halfway between A and B.
  guess_mantissa = guess_mantissa * 2 + 1;
  guess_exponent -= 1;

  // In our comparison:
  // lhs = exact = exact_mantissa * 10**exact_exponent
  //             = exact_mantissa * 5**exact_exponent * 2**exact_exponent
  // rhs = guess = guess_mantissa * 2**guess_exponent
  //
  // Because we are doing integer math, we can't directly deal with negative
  // exponents.  We instead move these to the other side of the inequality.
  absl::strings_internal::BigUnsigned<84>& lhs = exact_mantissa;
  int comparison;
  if (exact_exponent >= 0) {
    lhs.MultiplyByFiveToTheNth(exact_exponent);
    absl::strings_internal::BigUnsigned<84> rhs(guess_mantissa);
    // There are powers of 2 on both sides of the inequality; reduce this to
    // a single bit-shift.
    if (exact_exponent > guess_exponent) {
      lhs.ShiftLeft(exact_exponent - guess_exponent);
    } else {
      rhs.ShiftLeft(guess_exponent - exact_exponent);
    }
    comparison = Compare(lhs, rhs);
  } else {
    // Move the power of 5 to the other side of the equation, giving us:
    // lhs = exact_mantissa * 2**exact_exponent
    // rhs = guess_mantissa * 5**(-exact_exponent) * 2**guess_exponent
    absl::strings_internal::BigUnsigned<84> rhs =
        absl::strings_internal::BigUnsigned<84>::FiveToTheNth(-exact_exponent);
    rhs.MultiplyBy(guess_mantissa);
    if (exact_exponent > guess_exponent) {
      lhs.ShiftLeft(exact_exponent - guess_exponent);
    } else {
      rhs.ShiftLeft(guess_exponent - exact_exponent);
    }
    comparison = Compare(lhs, rhs);
  }
  if (comparison < 0) {
    return false;
  } else if (comparison > 0) {
    return true;
  } else {
    // When lhs == rhs, the decimal input is exactly between A and B.
    // Round towards even -- round up only if the low bit of the initial
    // `guess_mantissa` was a 1.  We shifted guess_mantissa left 1 bit at
    // the beginning of this function, so test the 2nd bit here.
    return (guess_mantissa & 2) == 2;
  }
}

// Constructs a CalculatedFloat from a given mantissa and exponent, but
// with the following normalizations applied:
//
// If rounding has caused mantissa to increase just past the allowed bit
// width, shift and adjust exponent.
//
// If exponent is too high, sets kOverflow.
//
// If mantissa is zero (representing a non-zero value not representable, even
// as a subnormal), sets kUnderflow.
template <typename FloatType>
CalculatedFloat CalculatedFloatFromRawValues(uint64_t mantissa, int exponent) {
  CalculatedFloat result;
  if (mantissa == uint64_t{1} << FloatTraits<FloatType>::kTargetMantissaBits) {
    mantissa >>= 1;
    exponent += 1;
  }
  if (exponent > FloatTraits<FloatType>::kMaxExponent) {
    result.exponent = kOverflow;
  } else if (mantissa == 0) {
    result.exponent = kUnderflow;
  } else {
    result.exponent = exponent;
    result.mantissa = mantissa;
  }
  return result;
}

template <typename FloatType>
CalculatedFloat CalculateFromParsedHexadecimal(
    const strings_internal::ParsedFloat& parsed_hex) {
  uint64_t mantissa = parsed_hex.mantissa;
  int exponent = parsed_hex.exponent;
  // This static_cast is only needed when using a std::bit_width()
  // implementation that does not have the fix for LWG 3656 applied.
  int mantissa_width = static_cast<int>(bit_width(mantissa));
  const int shift = NormalizedShiftSize<FloatType>(mantissa_width, exponent);
  bool result_exact;
  exponent += shift;
  mantissa = ShiftRightAndRound(mantissa, shift,
                                /* input exact= */ true, &result_exact);
  // ParseFloat handles rounding in the hexadecimal case, so we don't have to
  // check `result_exact` here.
  return CalculatedFloatFromRawValues<FloatType>(mantissa, exponent);
}

template <typename FloatType>
CalculatedFloat CalculateFromParsedDecimal(
    const strings_internal::ParsedFloat& parsed_decimal) {
  CalculatedFloat result;

  // Large or small enough decimal exponents will always result in overflow
  // or underflow.
  if (Power10Underflow(parsed_decimal.exponent)) {
    result.exponent = kUnderflow;
    return result;
  } else if (Power10Overflow(parsed_decimal.exponent)) {
    result.exponent = kOverflow;
    return result;
  }

  // Otherwise convert our power of 10 into a power of 2 times an integer
  // mantissa, and multiply this by our parsed decimal mantissa.
  uint128 wide_binary_mantissa = parsed_decimal.mantissa;
  wide_binary_mantissa *= Power10Mantissa(parsed_decimal.exponent);
  int binary_exponent = Power10Exponent(parsed_decimal.exponent);

  // Discard bits that are inaccurate due to truncation error.  The magic
  // `mantissa_width` constants below are justified in
  // https://abseil.io/about/design/charconv. They represent the number of bits
  // in `wide_binary_mantissa` that are guaranteed to be unaffected by error
  // propagation.
  bool mantissa_exact;
  int mantissa_width;
  if (parsed_decimal.subrange_begin) {
    // Truncated mantissa
    mantissa_width = 58;
    mantissa_exact = false;
    binary_exponent +=
        TruncateToBitWidth(mantissa_width, &wide_binary_mantissa);
  } else if (!Power10Exact(parsed_decimal.exponent)) {
    // Exact mantissa, truncated power of ten
    mantissa_width = 63;
    mantissa_exact = false;
    binary_exponent +=
        TruncateToBitWidth(mantissa_width, &wide_binary_mantissa);
  } else {
    // Product is exact
    mantissa_width = BitWidth(wide_binary_mantissa);
    mantissa_exact = true;
  }

  // Shift into an FloatType-sized mantissa, and round to nearest.
  const int shift =
      NormalizedShiftSize<FloatType>(mantissa_width, binary_exponent);
  bool result_exact;
  binary_exponent += shift;
  uint64_t binary_mantissa = ShiftRightAndRound(wide_binary_mantissa, shift,
                                                mantissa_exact, &result_exact);
  if (!result_exact) {
    // We could not determine the rounding direction using int128 math.  Use
    // full resolution math instead.
    if (MustRoundUp(binary_mantissa, binary_exponent, parsed_decimal)) {
      binary_mantissa += 1;
    }
  }

  return CalculatedFloatFromRawValues<FloatType>(binary_mantissa,
                                                 binary_exponent);
}

// As discussed in https://nigeltao.github.io/blog/2020/eisel-lemire.html the
// primary goal of the Eisel-Lemire algorithm is speed, for 99+% of the cases,
// not 100% coverage. As long as Eisel-Lemire doesn’t claim false positives,
// the combined approach (falling back to an alternative implementation when
// this function returns false) is both fast and correct.
template <typename FloatType>
bool EiselLemire(const strings_internal::ParsedFloat& input, bool negative,
                 absl::Nonnull<FloatType*> value,
                 absl::Nonnull<std::errc*> ec) {
  uint64_t man = input.mantissa;
  int exp10 = input.exponent;
  if (exp10 < FloatTraits<FloatType>::kEiselLemireMinInclusiveExp10) {
    *value = negative ? -0.0 : 0.0;
    *ec = std::errc::result_out_of_range;
    return true;
  } else if (exp10 >= FloatTraits<FloatType>::kEiselLemireMaxExclusiveExp10) {
    // Return max (a finite value) consistent with from_chars and DR 3081. For
    // SimpleAtod and SimpleAtof, post-processing will return infinity.
    *value = negative ? -std::numeric_limits<FloatType>::max()
                      : std::numeric_limits<FloatType>::max();
    *ec = std::errc::result_out_of_range;
    return true;
  }

  // Assert kPower10TableMinInclusive <= exp10 < kPower10TableMaxExclusive.
  // Equivalently, !Power10Underflow(exp10) and !Power10Overflow(exp10).
  static_assert(
      FloatTraits<FloatType>::kEiselLemireMinInclusiveExp10 >=
          kPower10TableMinInclusive,
      "(exp10-kPower10TableMinInclusive) in kPower10MantissaHighTable bounds");
  static_assert(
      FloatTraits<FloatType>::kEiselLemireMaxExclusiveExp10 <=
          kPower10TableMaxExclusive,
      "(exp10-kPower10TableMinInclusive) in kPower10MantissaHighTable bounds");

  // The terse (+) comments in this function body refer to sections of the
  // https://nigeltao.github.io/blog/2020/eisel-lemire.html blog post.
  //
  // That blog post discusses double precision (11 exponent bits with a -1023
  // bias, 52 mantissa bits), but the same approach applies to single precision
  // (8 exponent bits with a -127 bias, 23 mantissa bits). Either way, the
  // computation here happens with 64-bit values (e.g. man) or 128-bit values
  // (e.g. x) before finally converting to 64- or 32-bit floating point.
  //
  // See also "Number Parsing at a Gigabyte per Second, Software: Practice and
  // Experience 51 (8), 2021" (https://arxiv.org/abs/2101.11408) for detail.

  // (+) Normalization.
  int clz = countl_zero(man);
  man <<= static_cast<unsigned int>(clz);
  // The 217706 etc magic numbers are from the Power10Exponent function.
  uint64_t ret_exp2 =
      static_cast<uint64_t>((217706 * exp10 >> 16) + 64 +
                            FloatTraits<FloatType>::kExponentBias - clz);

  // (+) Multiplication.
  uint128 x = static_cast<uint128>(man) *
              static_cast<uint128>(
                  kPower10MantissaHighTable[exp10 - kPower10TableMinInclusive]);

  // (+) Wider Approximation.
  static constexpr uint64_t high64_mask =
      FloatTraits<FloatType>::kEiselLemireMask;
  if (((Uint128High64(x) & high64_mask) == high64_mask) &&
      (man > (std::numeric_limits<uint64_t>::max() - Uint128Low64(x)))) {
    uint128 y =
        static_cast<uint128>(man) *
        static_cast<uint128>(
            kPower10MantissaLowTable[exp10 - kPower10TableMinInclusive]);
    x += Uint128High64(y);
    // For example, parsing "4503599627370497.5" will take the if-true
    // branch here (for double precision), since:
    //  - x   = 0x8000000000000BFF_FFFFFFFFFFFFFFFF
    //  - y   = 0x8000000000000BFF_7FFFFFFFFFFFF400
    //  - man = 0xA000000000000F00
    // Likewise, when parsing "0.0625" for single precision:
    //  - x   = 0x7FFFFFFFFFFFFFFF_FFFFFFFFFFFFFFFF
    //  - y   = 0x813FFFFFFFFFFFFF_8A00000000000000
    //  - man = 0x9C40000000000000
    if (((Uint128High64(x) & high64_mask) == high64_mask) &&
        ((Uint128Low64(x) + 1) == 0) &&
        (man > (std::numeric_limits<uint64_t>::max() - Uint128Low64(y)))) {
      return false;
    }
  }

  // (+) Shifting to 54 Bits (or for single precision, to 25 bits).
  uint64_t msb = Uint128High64(x) >> 63;
  uint64_t ret_man =
      Uint128High64(x) >> (msb + FloatTraits<FloatType>::kEiselLemireShift);
  ret_exp2 -= 1 ^ msb;

  // (+) Half-way Ambiguity.
  //
  // For example, parsing "1e+23" will take the if-true branch here (for double
  // precision), since:
  //  - x       = 0x54B40B1F852BDA00_0000000000000000
  //  - ret_man = 0x002A5A058FC295ED
  // Likewise, when parsing "20040229.0" for single precision:
  //  - x       = 0x4C72894000000000_0000000000000000
  //  - ret_man = 0x000000000131CA25
  if ((Uint128Low64(x) == 0) && ((Uint128High64(x) & high64_mask) == 0) &&
      ((ret_man & 3) == 1)) {
    return false;
  }

  // (+) From 54 to 53 Bits (or for single precision, from 25 to 24 bits).
  ret_man += ret_man & 1;  // Line From54a.
  ret_man >>= 1;           // Line From54b.
  // Incrementing ret_man (at line From54a) may have overflowed 54 bits (53
  // bits after the right shift by 1 at line From54b), so adjust for that.
  //
  // For example, parsing "9223372036854775807" will take the if-true branch
  // here (for double precision), since:
  //  - ret_man = 0x0020000000000000 = (1 << 53)
  // Likewise, when parsing "2147483647.0" for single precision:
  //  - ret_man = 0x0000000001000000 = (1 << 24)
  if ((ret_man >> FloatTraits<FloatType>::kTargetMantissaBits) > 0) {
    ret_exp2 += 1;
    // Conceptually, we need a "ret_man >>= 1" in this if-block to balance
    // incrementing ret_exp2 in the line immediately above. However, we only
    // get here when line From54a overflowed (after adding a 1), so ret_man
    // here is (1 << 53). Its low 53 bits are therefore all zeroes. The only
    // remaining use of ret_man is to mask it with ((1 << 52) - 1), so only its
    // low 52 bits matter. A "ret_man >>= 1" would have no effect in practice.
    //
    // We omit the "ret_man >>= 1", even if it is cheap (and this if-branch is
    // rarely taken) and technically 'more correct', so that mutation tests
    // that would otherwise modify or omit that "ret_man >>= 1" don't complain
    // that such code mutations have no observable effect.
  }

  // ret_exp2 is a uint64_t. Zero or underflow means that we're in subnormal
  // space. max_exp2 (0x7FF for double precision, 0xFF for single precision) or
  // above means that we're in Inf/NaN space.
  //
  // The if block is equivalent to (but has fewer branches than):
  //   if ((ret_exp2 <= 0) || (ret_exp2 >= max_exp2)) { etc }
  //
  // For example, parsing "4.9406564584124654e-324" will take the if-true
  // branch here, since ret_exp2 = -51.
  static constexpr uint64_t max_exp2 =
      (1 << FloatTraits<FloatType>::kTargetExponentBits) - 1;
  if ((ret_exp2 - 1) >= (max_exp2 - 1)) {
    return false;
  }

#ifndef ABSL_BIT_PACK_FLOATS
  if (FloatTraits<FloatType>::kTargetBits == 64) {
    *value = FloatTraits<FloatType>::Make(
        (ret_man & 0x000FFFFFFFFFFFFFu) | 0x0010000000000000u,
        static_cast<int>(ret_exp2) - 1023 - 52, negative);
    return true;
  } else if (FloatTraits<FloatType>::kTargetBits == 32) {
    *value = FloatTraits<FloatType>::Make(
        (static_cast<uint32_t>(ret_man) & 0x007FFFFFu) | 0x00800000u,
        static_cast<int>(ret_exp2) - 127 - 23, negative);
    return true;
  }
#else
  if (FloatTraits<FloatType>::kTargetBits == 64) {
    uint64_t ret_bits = (ret_exp2 << 52) | (ret_man & 0x000FFFFFFFFFFFFFu);
    if (negative) {
      ret_bits |= 0x8000000000000000u;
    }
    *value = absl::bit_cast<double>(ret_bits);
    return true;
  } else if (FloatTraits<FloatType>::kTargetBits == 32) {
    uint32_t ret_bits = (static_cast<uint32_t>(ret_exp2) << 23) |
                        (static_cast<uint32_t>(ret_man) & 0x007FFFFFu);
    if (negative) {
      ret_bits |= 0x80000000u;
    }
    *value = absl::bit_cast<float>(ret_bits);
    return true;
  }
#endif  // ABSL_BIT_PACK_FLOATS
  return false;
}

template <typename FloatType>
from_chars_result FromCharsImpl(absl::Nonnull<const char*> first,
                                absl::Nonnull<const char*> last,
                                FloatType& value, chars_format fmt_flags) {
  from_chars_result result;
  result.ptr = first;  // overwritten on successful parse
  result.ec = std::errc();

  bool negative = false;
  if (first != last && *first == '-') {
    ++first;
    negative = true;
  }
  // If the `hex` flag is *not* set, then we will accept a 0x prefix and try
  // to parse a hexadecimal float.
  if ((fmt_flags & chars_format::hex) == chars_format{} && last - first >= 2 &&
      *first == '0' && (first[1] == 'x' || first[1] == 'X')) {
    const char* hex_first = first + 2;
    strings_internal::ParsedFloat hex_parse =
        strings_internal::ParseFloat<16>(hex_first, last, fmt_flags);
    if (hex_parse.end == nullptr ||
        hex_parse.type != strings_internal::FloatType::kNumber) {
      // Either we failed to parse a hex float after the "0x", or we read
      // "0xinf" or "0xnan" which we don't want to match.
      //
      // However, a string that begins with "0x" also begins with "0", which
      // is normally a valid match for the number zero.  So we want these
      // strings to match zero unless fmt_flags is `scientific`.  (This flag
      // means an exponent is required, which the string "0" does not have.)
      if (fmt_flags == chars_format::scientific) {
        result.ec = std::errc::invalid_argument;
      } else {
        result.ptr = first + 1;
        value = negative ? -0.0 : 0.0;
      }
      return result;
    }
    // We matched a value.
    result.ptr = hex_parse.end;
    if (HandleEdgeCase(hex_parse, negative, &value)) {
      return result;
    }
    CalculatedFloat calculated =
        CalculateFromParsedHexadecimal<FloatType>(hex_parse);
    EncodeResult(calculated, negative, &result, &value);
    return result;
  }
  // Otherwise, we choose the number base based on the flags.
  if ((fmt_flags & chars_format::hex) == chars_format::hex) {
    strings_internal::ParsedFloat hex_parse =
        strings_internal::ParseFloat<16>(first, last, fmt_flags);
    if (hex_parse.end == nullptr) {
      result.ec = std::errc::invalid_argument;
      return result;
    }
    result.ptr = hex_parse.end;
    if (HandleEdgeCase(hex_parse, negative, &value)) {
      return result;
    }
    CalculatedFloat calculated =
        CalculateFromParsedHexadecimal<FloatType>(hex_parse);
    EncodeResult(calculated, negative, &result, &value);
    return result;
  } else {
    strings_internal::ParsedFloat decimal_parse =
        strings_internal::ParseFloat<10>(first, last, fmt_flags);
    if (decimal_parse.end == nullptr) {
      result.ec = std::errc::invalid_argument;
      return result;
    }
    result.ptr = decimal_parse.end;
    if (HandleEdgeCase(decimal_parse, negative, &value)) {
      return result;
    }
    // A nullptr subrange_begin means that the decimal_parse.mantissa is exact
    // (not truncated), a precondition of the Eisel-Lemire algorithm.
    if ((decimal_parse.subrange_begin == nullptr) &&
        EiselLemire<FloatType>(decimal_parse, negative, &value, &result.ec)) {
      return result;
    }
    CalculatedFloat calculated =
        CalculateFromParsedDecimal<FloatType>(decimal_parse);
    EncodeResult(calculated, negative, &result, &value);
    return result;
  }
}
}  // namespace

from_chars_result from_chars(absl::Nonnull<const char*> first,
                             absl::Nonnull<const char*> last, double& value,
                             chars_format fmt) {
  return FromCharsImpl(first, last, value, fmt);
}

from_chars_result from_chars(absl::Nonnull<const char*> first,
                             absl::Nonnull<const char*> last, float& value,
                             chars_format fmt) {
  return FromCharsImpl(first, last, value, fmt);
}

namespace {

// Table of powers of 10, from kPower10TableMinInclusive to
// kPower10TableMaxExclusive.
//
// kPower10MantissaHighTable[i - kPower10TableMinInclusive] stores the 64-bit
// mantissa. The high bit is always on.
//
// kPower10MantissaLowTable extends that 64-bit mantissa to 128 bits.
//
// Power10Exponent(i) calculates the power-of-two exponent.
//
// For a number i, this gives the unique mantissaHigh and exponent such that
// (mantissaHigh * 2**exponent) <= 10**i < ((mantissaHigh + 1) * 2**exponent).
//
// For example, Python can confirm that the exact hexadecimal value of 1e60 is:
//    >>> a = 1000000000000000000000000000000000000000000000000000000000000
//    >>> hex(a)
//    '0x9f4f2726179a224501d762422c946590d91000000000000000'
// Adding underscores at every 8th hex digit shows 50 hex digits:
//    '0x9f4f2726_179a2245_01d76242_2c946590_d9100000_00000000_00'.
// In this case, the high bit of the first hex digit, 9, is coincidentally set,
// so we do not have to do further shifting to deduce the 128-bit mantissa:
//   - kPower10MantissaHighTable[60 - kP10TMI] = 0x9f4f2726179a2245U
//   - kPower10MantissaLowTable[ 60 - kP10TMI] = 0x01d762422c946590U
// where kP10TMI is kPower10TableMinInclusive. The low 18 of those 50 hex
// digits are truncated.
//
// 50 hex digits (with the high bit set) is 200 bits and mantissaHigh holds 64
// bits, so Power10Exponent(60) = 200 - 64 = 136. Again, Python can confirm:
//    >>> b = 0x9f4f2726179a2245
//    >>> ((b+0)<<136) <= a
//    True
//    >>> ((b+1)<<136) <= a
//    False
//
// The tables were generated by
// https://github.com/google/wuffs/blob/315b2e52625ebd7b02d8fac13e3cd85ea374fb80/script/print-mpb-powers-of-10.go
// after re-formatting its output into two arrays of N uint64_t values (instead
// of an N element array of uint64_t pairs).

const uint64_t kPower10MantissaHighTable[] = {
    0xeef453d6923bd65aU, 0x9558b4661b6565f8U, 0xbaaee17fa23ebf76U,
    0xe95a99df8ace6f53U, 0x91d8a02bb6c10594U, 0xb64ec836a47146f9U,
    0xe3e27a444d8d98b7U, 0x8e6d8c6ab0787f72U, 0xb208ef855c969f4fU,
    0xde8b2b66b3bc4723U, 0x8b16fb203055ac76U, 0xaddcb9e83c6b1793U,
    0xd953e8624b85dd78U, 0x87d4713d6f33aa6bU, 0xa9c98d8ccb009506U,
    0xd43bf0effdc0ba48U, 0x84a57695fe98746dU, 0xa5ced43b7e3e9188U,
    0xcf42894a5dce35eaU, 0x818995ce7aa0e1b2U, 0xa1ebfb4219491a1fU,
    0xca66fa129f9b60a6U, 0xfd00b897478238d0U, 0x9e20735e8cb16382U,
    0xc5a890362fddbc62U, 0xf712b443bbd52b7bU, 0x9a6bb0aa55653b2dU,
    0xc1069cd4eabe89f8U, 0xf148440a256e2c76U, 0x96cd2a865764dbcaU,
    0xbc807527ed3e12bcU, 0xeba09271e88d976bU, 0x93445b8731587ea3U,
    0xb8157268fdae9e4cU, 0xe61acf033d1a45dfU, 0x8fd0c16206306babU,
    0xb3c4f1ba87bc8696U, 0xe0b62e2929aba83cU, 0x8c71dcd9ba0b4925U,
    0xaf8e5410288e1b6fU, 0xdb71e91432b1a24aU, 0x892731ac9faf056eU,
    0xab70fe17c79ac6caU, 0xd64d3d9db981787dU, 0x85f0468293f0eb4eU,
    0xa76c582338ed2621U, 0xd1476e2c07286faaU, 0x82cca4db847945caU,
    0xa37fce126597973cU, 0xcc5fc196fefd7d0cU, 0xff77b1fcbebcdc4fU,
    0x9faacf3df73609b1U, 0xc795830d75038c1dU, 0xf97ae3d0d2446f25U,
    0x9becce62836ac577U, 0xc2e801fb244576d5U, 0xf3a20279ed56d48aU,
    0x9845418c345644d6U, 0xbe5691ef416bd60cU, 0xedec366b11c6cb8fU,
    0x94b3a202eb1c3f39U, 0xb9e08a83a5e34f07U, 0xe858ad248f5c22c9U,
    0x91376c36d99995beU, 0xb58547448ffffb2dU, 0xe2e69915b3fff9f9U,
    0x8dd01fad907ffc3bU, 0xb1442798f49ffb4aU, 0xdd95317f31c7fa1dU,
    0x8a7d3eef7f1cfc52U, 0xad1c8eab5ee43b66U, 0xd863b256369d4a40U,
    0x873e4f75e2224e68U, 0xa90de3535aaae202U, 0xd3515c2831559a83U,
    0x8412d9991ed58091U, 0xa5178fff668ae0b6U, 0xce5d73ff402d98e3U,
    0x80fa687f881c7f8eU, 0xa139029f6a239f72U, 0xc987434744ac874eU,
    0xfbe9141915d7a922U, 0x9d71ac8fada6c9b5U, 0xc4ce17b399107c22U,
    0xf6019da07f549b2bU, 0x99c102844f94e0fbU, 0xc0314325637a1939U,
    0xf03d93eebc589f88U, 0x96267c7535b763b5U, 0xbbb01b9283253ca2U,
    0xea9c227723ee8bcbU, 0x92a1958a7675175fU, 0xb749faed14125d36U,
    0xe51c79a85916f484U, 0x8f31cc0937ae58d2U, 0xb2fe3f0b8599ef07U,
    0xdfbdcece67006ac9U, 0x8bd6a141006042bdU, 0xaecc49914078536dU,
    0xda7f5bf590966848U, 0x888f99797a5e012dU, 0xaab37fd7d8f58178U,
    0xd5605fcdcf32e1d6U, 0x855c3be0a17fcd26U, 0xa6b34ad8c9dfc06fU,
    0xd0601d8efc57b08bU, 0x823c12795db6ce57U, 0xa2cb1717b52481edU,
    0xcb7ddcdda26da268U, 0xfe5d54150b090b02U, 0x9efa548d26e5a6e1U,
    0xc6b8e9b0709f109aU, 0xf867241c8cc6d4c0U, 0x9b407691d7fc44f8U,
    0xc21094364dfb5636U, 0xf294b943e17a2bc4U, 0x979cf3ca6cec5b5aU,
    0xbd8430bd08277231U, 0xece53cec4a314ebdU, 0x940f4613ae5ed136U,
    0xb913179899f68584U, 0xe757dd7ec07426e5U, 0x9096ea6f3848984fU,
    0xb4bca50b065abe63U, 0xe1ebce4dc7f16dfbU, 0x8d3360f09cf6e4bdU,
    0xb080392cc4349decU, 0xdca04777f541c567U, 0x89e42caaf9491b60U,
    0xac5d37d5b79b6239U, 0xd77485cb25823ac7U, 0x86a8d39ef77164bcU,
    0xa8530886b54dbdebU, 0xd267caa862a12d66U, 0x8380dea93da4bc60U,
    0xa46116538d0deb78U, 0xcd795be870516656U, 0x806bd9714632dff6U,
    0xa086cfcd97bf97f3U, 0xc8a883c0fdaf7df0U, 0xfad2a4b13d1b5d6cU,
    0x9cc3a6eec6311a63U, 0xc3f490aa77bd60fcU, 0xf4f1b4d515acb93bU,
    0x991711052d8bf3c5U, 0xbf5cd54678eef0b6U, 0xef340a98172aace4U,
    0x9580869f0e7aac0eU, 0xbae0a846d2195712U, 0xe998d258869facd7U,
    0x91ff83775423cc06U, 0xb67f6455292cbf08U, 0xe41f3d6a7377eecaU,
    0x8e938662882af53eU, 0xb23867fb2a35b28dU, 0xdec681f9f4c31f31U,
    0x8b3c113c38f9f37eU, 0xae0b158b4738705eU, 0xd98ddaee19068c76U,
    0x87f8a8d4cfa417c9U, 0xa9f6d30a038d1dbcU, 0xd47487cc8470652bU,
    0x84c8d4dfd2c63f3bU, 0xa5fb0a17c777cf09U, 0xcf79cc9db955c2ccU,
    0x81ac1fe293d599bfU, 0xa21727db38cb002fU, 0xca9cf1d206fdc03bU,
    0xfd442e4688bd304aU, 0x9e4a9cec15763e2eU, 0xc5dd44271ad3cdbaU,
    0xf7549530e188c128U, 0x9a94dd3e8cf578b9U, 0xc13a148e3032d6e7U,
    0xf18899b1bc3f8ca1U, 0x96f5600f15a7b7e5U, 0xbcb2b812db11a5deU,
    0xebdf661791d60f56U, 0x936b9fcebb25c995U, 0xb84687c269ef3bfbU,
    0xe65829b3046b0afaU, 0x8ff71a0fe2c2e6dcU, 0xb3f4e093db73a093U,
    0xe0f218b8d25088b8U, 0x8c974f7383725573U, 0xafbd2350644eeacfU,
    0xdbac6c247d62a583U, 0x894bc396ce5da772U, 0xab9eb47c81f5114fU,
    0xd686619ba27255a2U, 0x8613fd0145877585U, 0xa798fc4196e952e7U,
    0xd17f3b51fca3a7a0U, 0x82ef85133de648c4U, 0xa3ab66580d5fdaf5U,
    0xcc963fee10b7d1b3U, 0xffbbcfe994e5c61fU, 0x9fd561f1fd0f9bd3U,
    0xc7caba6e7c5382c8U, 0xf9bd690a1b68637bU, 0x9c1661a651213e2dU,
    0xc31bfa0fe5698db8U, 0xf3e2f893dec3f126U, 0x986ddb5c6b3a76b7U,
    0xbe89523386091465U, 0xee2ba6c0678b597fU, 0x94db483840b717efU,
    0xba121a4650e4ddebU, 0xe896a0d7e51e1566U, 0x915e2486ef32cd60U,
    0xb5b5ada8aaff80b8U, 0xe3231912d5bf60e6U, 0x8df5efabc5979c8fU,
    0xb1736b96b6fd83b3U, 0xddd0467c64bce4a0U, 0x8aa22c0dbef60ee4U,
    0xad4ab7112eb3929dU, 0xd89d64d57a607744U, 0x87625f056c7c4a8bU,
    0xa93af6c6c79b5d2dU, 0xd389b47879823479U, 0x843610cb4bf160cbU,
    0xa54394fe1eedb8feU, 0xce947a3da6a9273eU, 0x811ccc668829b887U,
    0xa163ff802a3426a8U, 0xc9bcff6034c13052U, 0xfc2c3f3841f17c67U,
    0x9d9ba7832936edc0U, 0xc5029163f384a931U, 0xf64335bcf065d37dU,
    0x99ea0196163fa42eU, 0xc06481fb9bcf8d39U, 0xf07da27a82c37088U,
    0x964e858c91ba2655U, 0xbbe226efb628afeaU, 0xeadab0aba3b2dbe5U,
    0x92c8ae6b464fc96fU, 0xb77ada0617e3bbcbU, 0xe55990879ddcaabdU,
    0x8f57fa54c2a9eab6U, 0xb32df8e9f3546564U, 0xdff9772470297ebdU,
    0x8bfbea76c619ef36U, 0xaefae51477a06b03U, 0xdab99e59958885c4U,
    0x88b402f7fd75539bU, 0xaae103b5fcd2a881U, 0xd59944a37c0752a2U,
    0x857fcae62d8493a5U, 0xa6dfbd9fb8e5b88eU, 0xd097ad07a71f26b2U,
    0x825ecc24c873782fU, 0xa2f67f2dfa90563bU, 0xcbb41ef979346bcaU,
    0xfea126b7d78186bcU, 0x9f24b832e6b0f436U, 0xc6ede63fa05d3143U,
    0xf8a95fcf88747d94U, 0x9b69dbe1b548ce7cU, 0xc24452da229b021bU,
    0xf2d56790ab41c2a2U, 0x97c560ba6b0919a5U, 0xbdb6b8e905cb600fU,
    0xed246723473e3813U, 0x9436c0760c86e30bU, 0xb94470938fa89bceU,
    0xe7958cb87392c2c2U, 0x90bd77f3483bb9b9U, 0xb4ecd5f01a4aa828U,
    0xe2280b6c20dd5232U, 0x8d590723948a535fU, 0xb0af48ec79ace837U,
    0xdcdb1b2798182244U, 0x8a08f0f8bf0f156bU, 0xac8b2d36eed2dac5U,
    0xd7adf884aa879177U, 0x86ccbb52ea94baeaU, 0xa87fea27a539e9a5U,
    0xd29fe4b18e88640eU, 0x83a3eeeef9153e89U, 0xa48ceaaab75a8e2bU,
    0xcdb02555653131b6U, 0x808e17555f3ebf11U, 0xa0b19d2ab70e6ed6U,
    0xc8de047564d20a8bU, 0xfb158592be068d2eU, 0x9ced737bb6c4183dU,
    0xc428d05aa4751e4cU, 0xf53304714d9265dfU, 0x993fe2c6d07b7fabU,
    0xbf8fdb78849a5f96U, 0xef73d256a5c0f77cU, 0x95a8637627989aadU,
    0xbb127c53b17ec159U, 0xe9d71b689dde71afU, 0x9226712162ab070dU,
    0xb6b00d69bb55c8d1U, 0xe45c10c42a2b3b05U, 0x8eb98a7a9a5b04e3U,
    0xb267ed1940f1c61cU, 0xdf01e85f912e37a3U, 0x8b61313bbabce2c6U,
    0xae397d8aa96c1b77U, 0xd9c7dced53c72255U, 0x881cea14545c7575U,
    0xaa242499697392d2U, 0xd4ad2dbfc3d07787U, 0x84ec3c97da624ab4U,
    0xa6274bbdd0fadd61U, 0xcfb11ead453994baU, 0x81ceb32c4b43fcf4U,
    0xa2425ff75e14fc31U, 0xcad2f7f5359a3b3eU, 0xfd87b5f28300ca0dU,
    0x9e74d1b791e07e48U, 0xc612062576589ddaU, 0xf79687aed3eec551U,
    0x9abe14cd44753b52U, 0xc16d9a0095928a27U, 0xf1c90080baf72cb1U,
    0x971da05074da7beeU, 0xbce5086492111aeaU, 0xec1e4a7db69561a5U,
    0x9392ee8e921d5d07U, 0xb877aa3236a4b449U, 0xe69594bec44de15bU,
    0x901d7cf73ab0acd9U, 0xb424dc35095cd80fU, 0xe12e13424bb40e13U,
    0x8cbccc096f5088cbU, 0xafebff0bcb24aafeU, 0xdbe6fecebdedd5beU,
    0x89705f4136b4a597U, 0xabcc77118461cefcU, 0xd6bf94d5e57a42bcU,
    0x8637bd05af6c69b5U, 0xa7c5ac471b478423U, 0xd1b71758e219652bU,
    0x83126e978d4fdf3bU, 0xa3d70a3d70a3d70aU, 0xccccccccccccccccU,
    0x8000000000000000U, 0xa000000000000000U, 0xc800000000000000U,
    0xfa00000000000000U, 0x9c40000000000000U, 0xc350000000000000U,
    0xf424000000000000U, 0x9896800000000000U, 0xbebc200000000000U,
    0xee6b280000000000U, 0x9502f90000000000U, 0xba43b74000000000U,
    0xe8d4a51000000000U, 0x9184e72a00000000U, 0xb5e620f480000000U,
    0xe35fa931a0000000U, 0x8e1bc9bf04000000U, 0xb1a2bc2ec5000000U,
    0xde0b6b3a76400000U, 0x8ac7230489e80000U, 0xad78ebc5ac620000U,
    0xd8d726b7177a8000U, 0x878678326eac9000U, 0xa968163f0a57b400U,
    0xd3c21bcecceda100U, 0x84595161401484a0U, 0xa56fa5b99019a5c8U,
    0xcecb8f27f4200f3aU, 0x813f3978f8940984U, 0xa18f07d736b90be5U,
    0xc9f2c9cd04674edeU, 0xfc6f7c4045812296U, 0x9dc5ada82b70b59dU,
    0xc5371912364ce305U, 0xf684df56c3e01bc6U, 0x9a130b963a6c115cU,
    0xc097ce7bc90715b3U, 0xf0bdc21abb48db20U, 0x96769950b50d88f4U,
    0xbc143fa4e250eb31U, 0xeb194f8e1ae525fdU, 0x92efd1b8d0cf37beU,
    0xb7abc627050305adU, 0xe596b7b0c643c719U, 0x8f7e32ce7bea5c6fU,
    0xb35dbf821ae4f38bU, 0xe0352f62a19e306eU, 0x8c213d9da502de45U,
    0xaf298d050e4395d6U, 0xdaf3f04651d47b4cU, 0x88d8762bf324cd0fU,
    0xab0e93b6efee0053U, 0xd5d238a4abe98068U, 0x85a36366eb71f041U,
    0xa70c3c40a64e6c51U, 0xd0cf4b50cfe20765U, 0x82818f1281ed449fU,
    0xa321f2d7226895c7U, 0xcbea6f8ceb02bb39U, 0xfee50b7025c36a08U,
    0x9f4f2726179a2245U, 0xc722f0ef9d80aad6U, 0xf8ebad2b84e0d58bU,
    0x9b934c3b330c8577U, 0xc2781f49ffcfa6d5U, 0xf316271c7fc3908aU,
    0x97edd871cfda3a56U, 0xbde94e8e43d0c8ecU, 0xed63a231d4c4fb27U,
    0x945e455f24fb1cf8U, 0xb975d6b6ee39e436U, 0xe7d34c64a9c85d44U,
    0x90e40fbeea1d3a4aU, 0xb51d13aea4a488ddU, 0xe264589a4dcdab14U,
    0x8d7eb76070a08aecU, 0xb0de65388cc8ada8U, 0xdd15fe86affad912U,
    0x8a2dbf142dfcc7abU, 0xacb92ed9397bf996U, 0xd7e77a8f87daf7fbU,
    0x86f0ac99b4e8dafdU, 0xa8acd7c0222311bcU, 0xd2d80db02aabd62bU,
    0x83c7088e1aab65dbU, 0xa4b8cab1a1563f52U, 0xcde6fd5e09abcf26U,
    0x80b05e5ac60b6178U, 0xa0dc75f1778e39d6U, 0xc913936dd571c84cU,
    0xfb5878494ace3a5fU, 0x9d174b2dcec0e47bU, 0xc45d1df942711d9aU,
    0xf5746577930d6500U, 0x9968bf6abbe85f20U, 0xbfc2ef456ae276e8U,
    0xefb3ab16c59b14a2U, 0x95d04aee3b80ece5U, 0xbb445da9ca61281fU,
    0xea1575143cf97226U, 0x924d692ca61be758U, 0xb6e0c377cfa2e12eU,
    0xe498f455c38b997aU, 0x8edf98b59a373fecU, 0xb2977ee300c50fe7U,
    0xdf3d5e9bc0f653e1U, 0x8b865b215899f46cU, 0xae67f1e9aec07187U,
    0xda01ee641a708de9U, 0x884134fe908658b2U, 0xaa51823e34a7eedeU,
    0xd4e5e2cdc1d1ea96U, 0x850fadc09923329eU, 0xa6539930bf6bff45U,
    0xcfe87f7cef46ff16U, 0x81f14fae158c5f6eU, 0xa26da3999aef7749U,
    0xcb090c8001ab551cU, 0xfdcb4fa002162a63U, 0x9e9f11c4014dda7eU,
    0xc646d63501a1511dU, 0xf7d88bc24209a565U, 0x9ae757596946075fU,
    0xc1a12d2fc3978937U, 0xf209787bb47d6b84U, 0x9745eb4d50ce6332U,
    0xbd176620a501fbffU, 0xec5d3fa8ce427affU, 0x93ba47c980e98cdfU,
    0xb8a8d9bbe123f017U, 0xe6d3102ad96cec1dU, 0x9043ea1ac7e41392U,
    0xb454e4a179dd1877U, 0xe16a1dc9d8545e94U, 0x8ce2529e2734bb1dU,
    0xb01ae745b101e9e4U, 0xdc21a1171d42645dU, 0x899504ae72497ebaU,
    0xabfa45da0edbde69U, 0xd6f8d7509292d603U, 0x865b86925b9bc5c2U,
    0xa7f26836f282b732U, 0xd1ef0244af2364ffU, 0x8335616aed761f1fU,
    0xa402b9c5a8d3a6e7U, 0xcd036837130890a1U, 0x802221226be55a64U,
    0xa02aa96b06deb0fdU, 0xc83553c5c8965d3dU, 0xfa42a8b73abbf48cU,
    0x9c69a97284b578d7U, 0xc38413cf25e2d70dU, 0xf46518c2ef5b8cd1U,
    0x98bf2f79d5993802U, 0xbeeefb584aff8603U, 0xeeaaba2e5dbf6784U,
    0x952ab45cfa97a0b2U, 0xba756174393d88dfU, 0xe912b9d1478ceb17U,
    0x91abb422ccb812eeU, 0xb616a12b7fe617aaU, 0xe39c49765fdf9d94U,
    0x8e41ade9fbebc27dU, 0xb1d219647ae6b31cU, 0xde469fbd99a05fe3U,
    0x8aec23d680043beeU, 0xada72ccc20054ae9U, 0xd910f7ff28069da4U,
    0x87aa9aff79042286U, 0xa99541bf57452b28U, 0xd3fa922f2d1675f2U,
    0x847c9b5d7c2e09b7U, 0xa59bc234db398c25U, 0xcf02b2c21207ef2eU,
    0x8161afb94b44f57dU, 0xa1ba1ba79e1632dcU, 0xca28a291859bbf93U,
    0xfcb2cb35e702af78U, 0x9defbf01b061adabU, 0xc56baec21c7a1916U,
    0xf6c69a72a3989f5bU, 0x9a3c2087a63f6399U, 0xc0cb28a98fcf3c7fU,
    0xf0fdf2d3f3c30b9fU, 0x969eb7c47859e743U, 0xbc4665b596706114U,
    0xeb57ff22fc0c7959U, 0x9316ff75dd87cbd8U, 0xb7dcbf5354e9beceU,
    0xe5d3ef282a242e81U, 0x8fa475791a569d10U, 0xb38d92d760ec4455U,
    0xe070f78d3927556aU, 0x8c469ab843b89562U, 0xaf58416654a6babbU,
    0xdb2e51bfe9d0696aU, 0x88fcf317f22241e2U, 0xab3c2fddeeaad25aU,
    0xd60b3bd56a5586f1U, 0x85c7056562757456U, 0xa738c6bebb12d16cU,
    0xd106f86e69d785c7U, 0x82a45b450226b39cU, 0xa34d721642b06084U,
    0xcc20ce9bd35c78a5U, 0xff290242c83396ceU, 0x9f79a169bd203e41U,
    0xc75809c42c684dd1U, 0xf92e0c3537826145U, 0x9bbcc7a142b17ccbU,
    0xc2abf989935ddbfeU, 0xf356f7ebf83552feU, 0x98165af37b2153deU,
    0xbe1bf1b059e9a8d6U, 0xeda2ee1c7064130cU, 0x9485d4d1c63e8be7U,
    0xb9a74a0637ce2ee1U, 0xe8111c87c5c1ba99U, 0x910ab1d4db9914a0U,
    0xb54d5e4a127f59c8U, 0xe2a0b5dc971f303aU, 0x8da471a9de737e24U,
    0xb10d8e1456105dadU, 0xdd50f1996b947518U, 0x8a5296ffe33cc92fU,
    0xace73cbfdc0bfb7bU, 0xd8210befd30efa5aU, 0x8714a775e3e95c78U,
    0xa8d9d1535ce3b396U, 0xd31045a8341ca07cU, 0x83ea2b892091e44dU,
    0xa4e4b66b68b65d60U, 0xce1de40642e3f4b9U, 0x80d2ae83e9ce78f3U,
    0xa1075a24e4421730U, 0xc94930ae1d529cfcU, 0xfb9b7cd9a4a7443cU,
    0x9d412e0806e88aa5U, 0xc491798a08a2ad4eU, 0xf5b5d7ec8acb58a2U,
    0x9991a6f3d6bf1765U, 0xbff610b0cc6edd3fU, 0xeff394dcff8a948eU,
    0x95f83d0a1fb69cd9U, 0xbb764c4ca7a4440fU, 0xea53df5fd18d5513U,
    0x92746b9be2f8552cU, 0xb7118682dbb66a77U, 0xe4d5e82392a40515U,
    0x8f05b1163ba6832dU, 0xb2c71d5bca9023f8U, 0xdf78e4b2bd342cf6U,
    0x8bab8eefb6409c1aU, 0xae9672aba3d0c320U, 0xda3c0f568cc4f3e8U,
    0x8865899617fb1871U, 0xaa7eebfb9df9de8dU, 0xd51ea6fa85785631U,
    0x8533285c936b35deU, 0xa67ff273b8460356U, 0xd01fef10a657842cU,
    0x8213f56a67f6b29bU, 0xa298f2c501f45f42U, 0xcb3f2f7642717713U,
    0xfe0efb53d30dd4d7U, 0x9ec95d1463e8a506U, 0xc67bb4597ce2ce48U,
    0xf81aa16fdc1b81daU, 0x9b10a4e5e9913128U, 0xc1d4ce1f63f57d72U,
    0xf24a01a73cf2dccfU, 0x976e41088617ca01U, 0xbd49d14aa79dbc82U,
    0xec9c459d51852ba2U, 0x93e1ab8252f33b45U, 0xb8da1662e7b00a17U,
    0xe7109bfba19c0c9dU, 0x906a617d450187e2U, 0xb484f9dc9641e9daU,
    0xe1a63853bbd26451U, 0x8d07e33455637eb2U, 0xb049dc016abc5e5fU,
    0xdc5c5301c56b75f7U, 0x89b9b3e11b6329baU, 0xac2820d9623bf429U,
    0xd732290fbacaf133U, 0x867f59a9d4bed6c0U, 0xa81f301449ee8c70U,
    0xd226fc195c6a2f8cU, 0x83585d8fd9c25db7U, 0xa42e74f3d032f525U,
    0xcd3a1230c43fb26fU, 0x80444b5e7aa7cf85U, 0xa0555e361951c366U,
    0xc86ab5c39fa63440U, 0xfa856334878fc150U, 0x9c935e00d4b9d8d2U,
    0xc3b8358109e84f07U, 0xf4a642e14c6262c8U, 0x98e7e9cccfbd7dbdU,
    0xbf21e44003acdd2cU, 0xeeea5d5004981478U, 0x95527a5202df0ccbU,
    0xbaa718e68396cffdU, 0xe950df20247c83fdU, 0x91d28b7416cdd27eU,
    0xb6472e511c81471dU, 0xe3d8f9e563a198e5U, 0x8e679c2f5e44ff8fU,
};

const uint64_t kPower10MantissaLowTable[] = {
    0x113faa2906a13b3fU, 0x4ac7ca59a424c507U, 0x5d79bcf00d2df649U,
    0xf4d82c2c107973dcU, 0x79071b9b8a4be869U, 0x9748e2826cdee284U,
    0xfd1b1b2308169b25U, 0xfe30f0f5e50e20f7U, 0xbdbd2d335e51a935U,
    0xad2c788035e61382U, 0x4c3bcb5021afcc31U, 0xdf4abe242a1bbf3dU,
    0xd71d6dad34a2af0dU, 0x8672648c40e5ad68U, 0x680efdaf511f18c2U,
    0x0212bd1b2566def2U, 0x014bb630f7604b57U, 0x419ea3bd35385e2dU,
    0x52064cac828675b9U, 0x7343efebd1940993U, 0x1014ebe6c5f90bf8U,
    0xd41a26e077774ef6U, 0x8920b098955522b4U, 0x55b46e5f5d5535b0U,
    0xeb2189f734aa831dU, 0xa5e9ec7501d523e4U, 0x47b233c92125366eU,
    0x999ec0bb696e840aU, 0xc00670ea43ca250dU, 0x380406926a5e5728U,
    0xc605083704f5ecf2U, 0xf7864a44c633682eU, 0x7ab3ee6afbe0211dU,
    0x5960ea05bad82964U, 0x6fb92487298e33bdU, 0xa5d3b6d479f8e056U,
    0x8f48a4899877186cU, 0x331acdabfe94de87U, 0x9ff0c08b7f1d0b14U,
    0x07ecf0ae5ee44dd9U, 0xc9e82cd9f69d6150U, 0xbe311c083a225cd2U,
    0x6dbd630a48aaf406U, 0x092cbbccdad5b108U, 0x25bbf56008c58ea5U,
    0xaf2af2b80af6f24eU, 0x1af5af660db4aee1U, 0x50d98d9fc890ed4dU,
    0xe50ff107bab528a0U, 0x1e53ed49a96272c8U, 0x25e8e89c13bb0f7aU,
    0x77b191618c54e9acU, 0xd59df5b9ef6a2417U, 0x4b0573286b44ad1dU,
    0x4ee367f9430aec32U, 0x229c41f793cda73fU, 0x6b43527578c1110fU,
    0x830a13896b78aaa9U, 0x23cc986bc656d553U, 0x2cbfbe86b7ec8aa8U,
    0x7bf7d71432f3d6a9U, 0xdaf5ccd93fb0cc53U, 0xd1b3400f8f9cff68U,
    0x23100809b9c21fa1U, 0xabd40a0c2832a78aU, 0x16c90c8f323f516cU,
    0xae3da7d97f6792e3U, 0x99cd11cfdf41779cU, 0x40405643d711d583U,
    0x482835ea666b2572U, 0xda3243650005eecfU, 0x90bed43e40076a82U,
    0x5a7744a6e804a291U, 0x711515d0a205cb36U, 0x0d5a5b44ca873e03U,
    0xe858790afe9486c2U, 0x626e974dbe39a872U, 0xfb0a3d212dc8128fU,
    0x7ce66634bc9d0b99U, 0x1c1fffc1ebc44e80U, 0xa327ffb266b56220U,
    0x4bf1ff9f0062baa8U, 0x6f773fc3603db4a9U, 0xcb550fb4384d21d3U,
    0x7e2a53a146606a48U, 0x2eda7444cbfc426dU, 0xfa911155fefb5308U,
    0x793555ab7eba27caU, 0x4bc1558b2f3458deU, 0x9eb1aaedfb016f16U,
    0x465e15a979c1cadcU, 0x0bfacd89ec191ec9U, 0xcef980ec671f667bU,
    0x82b7e12780e7401aU, 0xd1b2ecb8b0908810U, 0x861fa7e6dcb4aa15U,
    0x67a791e093e1d49aU, 0xe0c8bb2c5c6d24e0U, 0x58fae9f773886e18U,
    0xaf39a475506a899eU, 0x6d8406c952429603U, 0xc8e5087ba6d33b83U,
    0xfb1e4a9a90880a64U, 0x5cf2eea09a55067fU, 0xf42faa48c0ea481eU,
    0xf13b94daf124da26U, 0x76c53d08d6b70858U, 0x54768c4b0c64ca6eU,
    0xa9942f5dcf7dfd09U, 0xd3f93b35435d7c4cU, 0xc47bc5014a1a6dafU,
    0x359ab6419ca1091bU, 0xc30163d203c94b62U, 0x79e0de63425dcf1dU,
    0x985915fc12f542e4U, 0x3e6f5b7b17b2939dU, 0xa705992ceecf9c42U,
    0x50c6ff782a838353U, 0xa4f8bf5635246428U, 0x871b7795e136be99U,
    0x28e2557b59846e3fU, 0x331aeada2fe589cfU, 0x3ff0d2c85def7621U,
    0x0fed077a756b53a9U, 0xd3e8495912c62894U, 0x64712dd7abbbd95cU,
    0xbd8d794d96aacfb3U, 0xecf0d7a0fc5583a0U, 0xf41686c49db57244U,
    0x311c2875c522ced5U, 0x7d633293366b828bU, 0xae5dff9c02033197U,
    0xd9f57f830283fdfcU, 0xd072df63c324fd7bU, 0x4247cb9e59f71e6dU,
    0x52d9be85f074e608U, 0x67902e276c921f8bU, 0x00ba1cd8a3db53b6U,
    0x80e8a40eccd228a4U, 0x6122cd128006b2cdU, 0x796b805720085f81U,
    0xcbe3303674053bb0U, 0xbedbfc4411068a9cU, 0xee92fb5515482d44U,
    0x751bdd152d4d1c4aU, 0xd262d45a78a0635dU, 0x86fb897116c87c34U,
    0xd45d35e6ae3d4da0U, 0x8974836059cca109U, 0x2bd1a438703fc94bU,
    0x7b6306a34627ddcfU, 0x1a3bc84c17b1d542U, 0x20caba5f1d9e4a93U,
    0x547eb47b7282ee9cU, 0xe99e619a4f23aa43U, 0x6405fa00e2ec94d4U,
    0xde83bc408dd3dd04U, 0x9624ab50b148d445U, 0x3badd624dd9b0957U,
    0xe54ca5d70a80e5d6U, 0x5e9fcf4ccd211f4cU, 0x7647c3200069671fU,
    0x29ecd9f40041e073U, 0xf468107100525890U, 0x7182148d4066eeb4U,
    0xc6f14cd848405530U, 0xb8ada00e5a506a7cU, 0xa6d90811f0e4851cU,
    0x908f4a166d1da663U, 0x9a598e4e043287feU, 0x40eff1e1853f29fdU,
    0xd12bee59e68ef47cU, 0x82bb74f8301958ceU, 0xe36a52363c1faf01U,
    0xdc44e6c3cb279ac1U, 0x29ab103a5ef8c0b9U, 0x7415d448f6b6f0e7U,
    0x111b495b3464ad21U, 0xcab10dd900beec34U, 0x3d5d514f40eea742U,
    0x0cb4a5a3112a5112U, 0x47f0e785eaba72abU, 0x59ed216765690f56U,
    0x306869c13ec3532cU, 0x1e414218c73a13fbU, 0xe5d1929ef90898faU,
    0xdf45f746b74abf39U, 0x6b8bba8c328eb783U, 0x066ea92f3f326564U,
    0xc80a537b0efefebdU, 0xbd06742ce95f5f36U, 0x2c48113823b73704U,
    0xf75a15862ca504c5U, 0x9a984d73dbe722fbU, 0xc13e60d0d2e0ebbaU,
    0x318df905079926a8U, 0xfdf17746497f7052U, 0xfeb6ea8bedefa633U,
    0xfe64a52ee96b8fc0U, 0x3dfdce7aa3c673b0U, 0x06bea10ca65c084eU,
    0x486e494fcff30a62U, 0x5a89dba3c3efccfaU, 0xf89629465a75e01cU,
    0xf6bbb397f1135823U, 0x746aa07ded582e2cU, 0xa8c2a44eb4571cdcU,
    0x92f34d62616ce413U, 0x77b020baf9c81d17U, 0x0ace1474dc1d122eU,
    0x0d819992132456baU, 0x10e1fff697ed6c69U, 0xca8d3ffa1ef463c1U,
    0xbd308ff8a6b17cb2U, 0xac7cb3f6d05ddbdeU, 0x6bcdf07a423aa96bU,
    0x86c16c98d2c953c6U, 0xe871c7bf077ba8b7U, 0x11471cd764ad4972U,
    0xd598e40d3dd89bcfU, 0x4aff1d108d4ec2c3U, 0xcedf722a585139baU,
    0xc2974eb4ee658828U, 0x733d226229feea32U, 0x0806357d5a3f525fU,
    0xca07c2dcb0cf26f7U, 0xfc89b393dd02f0b5U, 0xbbac2078d443ace2U,
    0xd54b944b84aa4c0dU, 0x0a9e795e65d4df11U, 0x4d4617b5ff4a16d5U,
    0x504bced1bf8e4e45U, 0xe45ec2862f71e1d6U, 0x5d767327bb4e5a4cU,
    0x3a6a07f8d510f86fU, 0x890489f70a55368bU, 0x2b45ac74ccea842eU,
    0x3b0b8bc90012929dU, 0x09ce6ebb40173744U, 0xcc420a6a101d0515U,
    0x9fa946824a12232dU, 0x47939822dc96abf9U, 0x59787e2b93bc56f7U,
    0x57eb4edb3c55b65aU, 0xede622920b6b23f1U, 0xe95fab368e45ecedU,
    0x11dbcb0218ebb414U, 0xd652bdc29f26a119U, 0x4be76d3346f0495fU,
    0x6f70a4400c562ddbU, 0xcb4ccd500f6bb952U, 0x7e2000a41346a7a7U,
    0x8ed400668c0c28c8U, 0x728900802f0f32faU, 0x4f2b40a03ad2ffb9U,
    0xe2f610c84987bfa8U, 0x0dd9ca7d2df4d7c9U, 0x91503d1c79720dbbU,
    0x75a44c6397ce912aU, 0xc986afbe3ee11abaU, 0xfbe85badce996168U,
    0xfae27299423fb9c3U, 0xdccd879fc967d41aU, 0x5400e987bbc1c920U,
    0x290123e9aab23b68U, 0xf9a0b6720aaf6521U, 0xf808e40e8d5b3e69U,
    0xb60b1d1230b20e04U, 0xb1c6f22b5e6f48c2U, 0x1e38aeb6360b1af3U,
    0x25c6da63c38de1b0U, 0x579c487e5a38ad0eU, 0x2d835a9df0c6d851U,
    0xf8e431456cf88e65U, 0x1b8e9ecb641b58ffU, 0xe272467e3d222f3fU,
    0x5b0ed81dcc6abb0fU, 0x98e947129fc2b4e9U, 0x3f2398d747b36224U,
    0x8eec7f0d19a03aadU, 0x1953cf68300424acU, 0x5fa8c3423c052dd7U,
    0x3792f412cb06794dU, 0xe2bbd88bbee40bd0U, 0x5b6aceaeae9d0ec4U,
    0xf245825a5a445275U, 0xeed6e2f0f0d56712U, 0x55464dd69685606bU,
    0xaa97e14c3c26b886U, 0xd53dd99f4b3066a8U, 0xe546a8038efe4029U,
    0xde98520472bdd033U, 0x963e66858f6d4440U, 0xdde7001379a44aa8U,
    0x5560c018580d5d52U, 0xaab8f01e6e10b4a6U, 0xcab3961304ca70e8U,
    0x3d607b97c5fd0d22U, 0x8cb89a7db77c506aU, 0x77f3608e92adb242U,
    0x55f038b237591ed3U, 0x6b6c46dec52f6688U, 0x2323ac4b3b3da015U,
    0xabec975e0a0d081aU, 0x96e7bd358c904a21U, 0x7e50d64177da2e54U,
    0xdde50bd1d5d0b9e9U, 0x955e4ec64b44e864U, 0xbd5af13bef0b113eU,
    0xecb1ad8aeacdd58eU, 0x67de18eda5814af2U, 0x80eacf948770ced7U,
    0xa1258379a94d028dU, 0x096ee45813a04330U, 0x8bca9d6e188853fcU,
    0x775ea264cf55347dU, 0x95364afe032a819dU, 0x3a83ddbd83f52204U,
    0xc4926a9672793542U, 0x75b7053c0f178293U, 0x5324c68b12dd6338U,
    0xd3f6fc16ebca5e03U, 0x88f4bb1ca6bcf584U, 0x2b31e9e3d06c32e5U,
    0x3aff322e62439fcfU, 0x09befeb9fad487c2U, 0x4c2ebe687989a9b3U,
    0x0f9d37014bf60a10U, 0x538484c19ef38c94U, 0x2865a5f206b06fb9U,
    0xf93f87b7442e45d3U, 0xf78f69a51539d748U, 0xb573440e5a884d1bU,
    0x31680a88f8953030U, 0xfdc20d2b36ba7c3dU, 0x3d32907604691b4cU,
    0xa63f9a49c2c1b10fU, 0x0fcf80dc33721d53U, 0xd3c36113404ea4a8U,
    0x645a1cac083126e9U, 0x3d70a3d70a3d70a3U, 0xccccccccccccccccU,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x0000000000000000U, 0x0000000000000000U,
    0x0000000000000000U, 0x4000000000000000U, 0x5000000000000000U,
    0xa400000000000000U, 0x4d00000000000000U, 0xf020000000000000U,
    0x6c28000000000000U, 0xc732000000000000U, 0x3c7f400000000000U,
    0x4b9f100000000000U, 0x1e86d40000000000U, 0x1314448000000000U,
    0x17d955a000000000U, 0x5dcfab0800000000U, 0x5aa1cae500000000U,
    0xf14a3d9e40000000U, 0x6d9ccd05d0000000U, 0xe4820023a2000000U,
    0xdda2802c8a800000U, 0xd50b2037ad200000U, 0x4526f422cc340000U,
    0x9670b12b7f410000U, 0x3c0cdd765f114000U, 0xa5880a69fb6ac800U,
    0x8eea0d047a457a00U, 0x72a4904598d6d880U, 0x47a6da2b7f864750U,
    0x999090b65f67d924U, 0xfff4b4e3f741cf6dU, 0xbff8f10e7a8921a4U,
    0xaff72d52192b6a0dU, 0x9bf4f8a69f764490U, 0x02f236d04753d5b4U,
    0x01d762422c946590U, 0x424d3ad2b7b97ef5U, 0xd2e0898765a7deb2U,
    0x63cc55f49f88eb2fU, 0x3cbf6b71c76b25fbU, 0x8bef464e3945ef7aU,
    0x97758bf0e3cbb5acU, 0x3d52eeed1cbea317U, 0x4ca7aaa863ee4bddU,
    0x8fe8caa93e74ef6aU, 0xb3e2fd538e122b44U, 0x60dbbca87196b616U,
    0xbc8955e946fe31cdU, 0x6babab6398bdbe41U, 0xc696963c7eed2dd1U,
    0xfc1e1de5cf543ca2U, 0x3b25a55f43294bcbU, 0x49ef0eb713f39ebeU,
    0x6e3569326c784337U, 0x49c2c37f07965404U, 0xdc33745ec97be906U,
    0x69a028bb3ded71a3U, 0xc40832ea0d68ce0cU, 0xf50a3fa490c30190U,
    0x792667c6da79e0faU, 0x577001b891185938U, 0xed4c0226b55e6f86U,
    0x544f8158315b05b4U, 0x696361ae3db1c721U, 0x03bc3a19cd1e38e9U,
    0x04ab48a04065c723U, 0x62eb0d64283f9c76U, 0x3ba5d0bd324f8394U,
    0xca8f44ec7ee36479U, 0x7e998b13cf4e1ecbU, 0x9e3fedd8c321a67eU,
    0xc5cfe94ef3ea101eU, 0xbba1f1d158724a12U, 0x2a8a6e45ae8edc97U,
    0xf52d09d71a3293bdU, 0x593c2626705f9c56U, 0x6f8b2fb00c77836cU,
    0x0b6dfb9c0f956447U, 0x4724bd4189bd5eacU, 0x58edec91ec2cb657U,
    0x2f2967b66737e3edU, 0xbd79e0d20082ee74U, 0xecd8590680a3aa11U,
    0xe80e6f4820cc9495U, 0x3109058d147fdcddU, 0xbd4b46f0599fd415U,
    0x6c9e18ac7007c91aU, 0x03e2cf6bc604ddb0U, 0x84db8346b786151cU,
    0xe612641865679a63U, 0x4fcb7e8f3f60c07eU, 0xe3be5e330f38f09dU,
    0x5cadf5bfd3072cc5U, 0x73d9732fc7c8f7f6U, 0x2867e7fddcdd9afaU,
    0xb281e1fd541501b8U, 0x1f225a7ca91a4226U, 0x3375788de9b06958U,
    0x0052d6b1641c83aeU, 0xc0678c5dbd23a49aU, 0xf840b7ba963646e0U,
    0xb650e5a93bc3d898U, 0xa3e51f138ab4cebeU, 0xc66f336c36b10137U,
    0xb80b0047445d4184U, 0xa60dc059157491e5U, 0x87c89837ad68db2fU,
    0x29babe4598c311fbU, 0xf4296dd6fef3d67aU, 0x1899e4a65f58660cU,
    0x5ec05dcff72e7f8fU, 0x76707543f4fa1f73U, 0x6a06494a791c53a8U,
    0x0487db9d17636892U, 0x45a9d2845d3c42b6U, 0x0b8a2392ba45a9b2U,
    0x8e6cac7768d7141eU, 0x3207d795430cd926U, 0x7f44e6bd49e807b8U,
    0x5f16206c9c6209a6U, 0x36dba887c37a8c0fU, 0xc2494954da2c9789U,
    0xf2db9baa10b7bd6cU, 0x6f92829494e5acc7U, 0xcb772339ba1f17f9U,
    0xff2a760414536efbU, 0xfef5138519684abaU, 0x7eb258665fc25d69U,
    0xef2f773ffbd97a61U, 0xaafb550ffacfd8faU, 0x95ba2a53f983cf38U,
    0xdd945a747bf26183U, 0x94f971119aeef9e4U, 0x7a37cd5601aab85dU,
    0xac62e055c10ab33aU, 0x577b986b314d6009U, 0xed5a7e85fda0b80bU,
    0x14588f13be847307U, 0x596eb2d8ae258fc8U, 0x6fca5f8ed9aef3bbU,
    0x25de7bb9480d5854U, 0xaf561aa79a10ae6aU, 0x1b2ba1518094da04U,
    0x90fb44d2f05d0842U, 0x353a1607ac744a53U, 0x42889b8997915ce8U,
    0x69956135febada11U, 0x43fab9837e699095U, 0x94f967e45e03f4bbU,
    0x1d1be0eebac278f5U, 0x6462d92a69731732U, 0x7d7b8f7503cfdcfeU,
    0x5cda735244c3d43eU, 0x3a0888136afa64a7U, 0x088aaa1845b8fdd0U,
    0x8aad549e57273d45U, 0x36ac54e2f678864bU, 0x84576a1bb416a7ddU,
    0x656d44a2a11c51d5U, 0x9f644ae5a4b1b325U, 0x873d5d9f0dde1feeU,
    0xa90cb506d155a7eaU, 0x09a7f12442d588f2U, 0x0c11ed6d538aeb2fU,
    0x8f1668c8a86da5faU, 0xf96e017d694487bcU, 0x37c981dcc395a9acU,
    0x85bbe253f47b1417U, 0x93956d7478ccec8eU, 0x387ac8d1970027b2U,
    0x06997b05fcc0319eU, 0x441fece3bdf81f03U, 0xd527e81cad7626c3U,
    0x8a71e223d8d3b074U, 0xf6872d5667844e49U, 0xb428f8ac016561dbU,
    0xe13336d701beba52U, 0xecc0024661173473U, 0x27f002d7f95d0190U,
    0x31ec038df7b441f4U, 0x7e67047175a15271U, 0x0f0062c6e984d386U,
    0x52c07b78a3e60868U, 0xa7709a56ccdf8a82U, 0x88a66076400bb691U,
    0x6acff893d00ea435U, 0x0583f6b8c4124d43U, 0xc3727a337a8b704aU,
    0x744f18c0592e4c5cU, 0x1162def06f79df73U, 0x8addcb5645ac2ba8U,
    0x6d953e2bd7173692U, 0xc8fa8db6ccdd0437U, 0x1d9c9892400a22a2U,
    0x2503beb6d00cab4bU, 0x2e44ae64840fd61dU, 0x5ceaecfed289e5d2U,
    0x7425a83e872c5f47U, 0xd12f124e28f77719U, 0x82bd6b70d99aaa6fU,
    0x636cc64d1001550bU, 0x3c47f7e05401aa4eU, 0x65acfaec34810a71U,
    0x7f1839a741a14d0dU, 0x1ede48111209a050U, 0x934aed0aab460432U,
    0xf81da84d5617853fU, 0x36251260ab9d668eU, 0xc1d72b7c6b426019U,
    0xb24cf65b8612f81fU, 0xdee033f26797b627U, 0x169840ef017da3b1U,
    0x8e1f289560ee864eU, 0xf1a6f2bab92a27e2U, 0xae10af696774b1dbU,
    0xacca6da1e0a8ef29U, 0x17fd090a58d32af3U, 0xddfc4b4cef07f5b0U,
    0x4abdaf101564f98eU, 0x9d6d1ad41abe37f1U, 0x84c86189216dc5edU,
    0x32fd3cf5b4e49bb4U, 0x3fbc8c33221dc2a1U, 0x0fabaf3feaa5334aU,
    0x29cb4d87f2a7400eU, 0x743e20e9ef511012U, 0x914da9246b255416U,
    0x1ad089b6c2f7548eU, 0xa184ac2473b529b1U, 0xc9e5d72d90a2741eU,
    0x7e2fa67c7a658892U, 0xddbb901b98feeab7U, 0x552a74227f3ea565U,
    0xd53a88958f87275fU, 0x8a892abaf368f137U, 0x2d2b7569b0432d85U,
    0x9c3b29620e29fc73U, 0x8349f3ba91b47b8fU, 0x241c70a936219a73U,
    0xed238cd383aa0110U, 0xf4363804324a40aaU, 0xb143c6053edcd0d5U,
    0xdd94b7868e94050aU, 0xca7cf2b4191c8326U, 0xfd1c2f611f63a3f0U,
    0xbc633b39673c8cecU, 0xd5be0503e085d813U, 0x4b2d8644d8a74e18U,
    0xddf8e7d60ed1219eU, 0xcabb90e5c942b503U, 0x3d6a751f3b936243U,
    0x0cc512670a783ad4U, 0x27fb2b80668b24c5U, 0xb1f9f660802dedf6U,
    0x5e7873f8a0396973U, 0xdb0b487b6423e1e8U, 0x91ce1a9a3d2cda62U,
    0x7641a140cc7810fbU, 0xa9e904c87fcb0a9dU, 0x546345fa9fbdcd44U,
    0xa97c177947ad4095U, 0x49ed8eabcccc485dU, 0x5c68f256bfff5a74U,
    0x73832eec6fff3111U, 0xc831fd53c5ff7eabU, 0xba3e7ca8b77f5e55U,
    0x28ce1bd2e55f35ebU, 0x7980d163cf5b81b3U, 0xd7e105bcc332621fU,
    0x8dd9472bf3fefaa7U, 0xb14f98f6f0feb951U, 0x6ed1bf9a569f33d3U,
    0x0a862f80ec4700c8U, 0xcd27bb612758c0faU, 0x8038d51cb897789cU,
    0xe0470a63e6bd56c3U, 0x1858ccfce06cac74U, 0x0f37801e0c43ebc8U,
    0xd30560258f54e6baU, 0x47c6b82ef32a2069U, 0x4cdc331d57fa5441U,
    0xe0133fe4adf8e952U, 0x58180fddd97723a6U, 0x570f09eaa7ea7648U,
};

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
