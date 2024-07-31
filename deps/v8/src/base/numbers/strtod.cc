// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/numbers/strtod.h"

#include <stdarg.h>

#include <cmath>
#include <limits>

#include "src/base/numbers/bignum.h"
#include "src/base/numbers/cached-powers.h"
#include "src/base/numbers/double.h"

namespace v8 {
namespace base {

// 2^53 = 9007199254740992.
// Any integer with at most 15 decimal digits will hence fit into a double
// (which has a 53bit significand) without loss of precision.
static const int kMaxExactDoubleIntegerDecimalDigits = 15;
// 2^64 = 18446744073709551616 > 10^19
static const int kMaxUint64DecimalDigits = 19;

// Max double: 1.7976931348623157 x 10^308
// Min non-zero double: 4.9406564584124654 x 10^-324
// Any x >= 10^309 is interpreted as +infinity.
// Any x <= 10^-324 is interpreted as 0.
// Note that 2.5e-324 (despite being smaller than the min double) will be read
// as non-zero (equal to the min non-zero double).
static const int kMaxDecimalPower = 309;
static const int kMinDecimalPower = -324;

// 2^64 = 18446744073709551616
static const uint64_t kMaxUint64 = 0xFFFF'FFFF'FFFF'FFFF;

// clang-format off
static const double exact_powers_of_ten[] = {
  1.0,  // 10^0
  10.0,
  100.0,
  1000.0,
  10000.0,
  100000.0,
  1000000.0,
  10000000.0,
  100000000.0,
  1000000000.0,
  10000000000.0,  // 10^10
  100000000000.0,
  1000000000000.0,
  10000000000000.0,
  100000000000000.0,
  1000000000000000.0,
  10000000000000000.0,
  100000000000000000.0,
  1000000000000000000.0,
  10000000000000000000.0,
  100000000000000000000.0,  // 10^20
  1000000000000000000000.0,
  // 10^22 = 0x21E19E0C9BAB2400000 = 0x878678326EAC9 * 2^22
  10000000000000000000000.0
};
// clang-format on
static const int kExactPowersOfTenSize = arraysize(exact_powers_of_ten);

// Maximum number of significant digits in the decimal representation.
// In fact the value is 772 (see conversions.cc), but to give us some margin
// we round up to 780.
static const int kMaxSignificantDecimalDigits = 780;

static Vector<const char> TrimLeadingZeros(Vector<const char> buffer) {
  for (int i = 0; i < buffer.length(); i++) {
    if (buffer[i] != '0') {
      return buffer.SubVector(i, buffer.length());
    }
  }
  return Vector<const char>(buffer.begin(), 0);
}

static Vector<const char> TrimTrailingZeros(Vector<const char> buffer) {
  for (int i = buffer.length() - 1; i >= 0; --i) {
    if (buffer[i] != '0') {
      return buffer.SubVector(0, i + 1);
    }
  }
  return Vector<const char>(buffer.begin(), 0);
}

static void TrimToMaxSignificantDigits(Vector<const char> buffer, int exponent,
                                       char* significant_buffer,
                                       int* significant_exponent) {
  for (int i = 0; i < kMaxSignificantDecimalDigits - 1; ++i) {
    significant_buffer[i] = buffer[i];
  }
  // The input buffer has been trimmed. Therefore the last digit must be
  // different from '0'.
  DCHECK_NE(buffer[buffer.length() - 1], '0');
  // Set the last digit to be non-zero. This is sufficient to guarantee
  // correct rounding.
  significant_buffer[kMaxSignificantDecimalDigits - 1] = '1';
  *significant_exponent =
      exponent + (buffer.length() - kMaxSignificantDecimalDigits);
}

// Reads digits from the buffer and converts them to a uint64.
// Reads in as many digits as fit into a uint64.
// When the string starts with "1844674407370955161" no further digit is read.
// Since 2^64 = 18446744073709551616 it would still be possible read another
// digit if it was less or equal than 6, but this would complicate the code.
static uint64_t ReadUint64(Vector<const char> buffer,
                           int* number_of_read_digits) {
  uint64_t result = 0;
  int i = 0;
  while (i < buffer.length() && result <= (kMaxUint64 / 10 - 1)) {
    int digit = buffer[i++] - '0';
    DCHECK(0 <= digit && digit <= 9);
    result = 10 * result + digit;
  }
  *number_of_read_digits = i;
  return result;
}

// Reads a DiyFp from the buffer.
// The returned DiyFp is not necessarily normalized.
// If remaining_decimals is zero then the returned DiyFp is accurate.
// Otherwise it has been rounded and has error of at most 1/2 ulp.
static void ReadDiyFp(Vector<const char> buffer, DiyFp* result,
                      int* remaining_decimals) {
  int read_digits;
  uint64_t significand = ReadUint64(buffer, &read_digits);
  if (buffer.length() == read_digits) {
    *result = DiyFp(significand, 0);
    *remaining_decimals = 0;
  } else {
    // Round the significand.
    if (buffer[read_digits] >= '5') {
      significand++;
    }
    // Compute the binary exponent.
    int exponent = 0;
    *result = DiyFp(significand, exponent);
    *remaining_decimals = buffer.length() - read_digits;
  }
}

static bool DoubleStrtod(Vector<const char> trimmed, int exponent,
                         double* result) {
#if (V8_TARGET_ARCH_IA32 || defined(USE_SIMULATOR)) && !defined(_MSC_VER)
  // On x86 the floating-point stack can be 64 or 80 bits wide. If it is
  // 80 bits wide (as is the case on Linux) then double-rounding occurs and the
  // result is not accurate.
  // We know that Windows32 with MSVC, unlike with MinGW32, uses 64 bits and is
  // therefore accurate.
  // Note that the ARM simulators are compiled for 32bits. They
  // therefore exhibit the same problem.
  USE(exact_powers_of_ten);
  USE(kMaxExactDoubleIntegerDecimalDigits);
  USE(kExactPowersOfTenSize);
  return false;
#else
  if (trimmed.length() <= kMaxExactDoubleIntegerDecimalDigits) {
    int read_digits;
    // The trimmed input fits into a double.
    // If the 10^exponent (resp. 10^-exponent) fits into a double too then we
    // can compute the result-double simply by multiplying (resp. dividing) the
    // two numbers.
    // This is possible because IEEE guarantees that floating-point operations
    // return the best possible approximation.
    if (exponent < 0 && -exponent < kExactPowersOfTenSize) {
      // 10^-exponent fits into a double.
      *result = static_cast<double>(ReadUint64(trimmed, &read_digits));
      DCHECK(read_digits == trimmed.length());
      *result /= exact_powers_of_ten[-exponent];
      return true;
    }
    if (0 <= exponent && exponent < kExactPowersOfTenSize) {
      // 10^exponent fits into a double.
      *result = static_cast<double>(ReadUint64(trimmed, &read_digits));
      DCHECK(read_digits == trimmed.length());
      *result *= exact_powers_of_ten[exponent];
      return true;
    }
    int remaining_digits =
        kMaxExactDoubleIntegerDecimalDigits - trimmed.length();
    if ((0 <= exponent) &&
        (exponent - remaining_digits < kExactPowersOfTenSize)) {
      // The trimmed string was short and we can multiply it with
      // 10^remaining_digits. As a result the remaining exponent now fits
      // into a double too.
      *result = static_cast<double>(ReadUint64(trimmed, &read_digits));
      DCHECK(read_digits == trimmed.length());
      *result *= exact_powers_of_ten[remaining_digits];
      *result *= exact_powers_of_ten[exponent - remaining_digits];
      return true;
    }
  }
  return false;
#endif
}

// Returns 10^exponent as an exact DiyFp.
// The given exponent must be in the range [1; kDecimalExponentDistance[.
static DiyFp AdjustmentPowerOfTen(int exponent) {
  DCHECK_LT(0, exponent);
  DCHECK_LT(exponent, PowersOfTenCache::kDecimalExponentDistance);
  // Simply hardcode the remaining powers for the given decimal exponent
  // distance.
  DCHECK_EQ(PowersOfTenCache::kDecimalExponentDistance, 8);
  switch (exponent) {
    case 1:
      return DiyFp(0xA000'0000'0000'0000, -60);
    case 2:
      return DiyFp(0xC800'0000'0000'0000, -57);
    case 3:
      return DiyFp(0xFA00'0000'0000'0000, -54);
    case 4:
      return DiyFp(0x9C40'0000'0000'0000, -50);
    case 5:
      return DiyFp(0xC350'0000'0000'0000, -47);
    case 6:
      return DiyFp(0xF424'0000'0000'0000, -44);
    case 7:
      return DiyFp(0x9896'8000'0000'0000, -40);
    default:
      UNREACHABLE();
  }
}

// If the function returns true then the result is the correct double.
// Otherwise it is either the correct double or the double that is just below
// the correct double.
static bool DiyFpStrtod(Vector<const char> buffer, int exponent,
                        double* result) {
  DiyFp input;
  int remaining_decimals;
  ReadDiyFp(buffer, &input, &remaining_decimals);
  // Since we may have dropped some digits the input is not accurate.
  // If remaining_decimals is different than 0 than the error is at most
  // .5 ulp (unit in the last place).
  // We don't want to deal with fractions and therefore keep a common
  // denominator.
  const int kDenominatorLog = 3;
  const int kDenominator = 1 << kDenominatorLog;
  // Move the remaining decimals into the exponent.
  exponent += remaining_decimals;
  int64_t error = (remaining_decimals == 0 ? 0 : kDenominator / 2);

  int old_e = input.e();
  input.Normalize();
  error <<= old_e - input.e();

  DCHECK_LE(exponent, PowersOfTenCache::kMaxDecimalExponent);
  if (exponent < PowersOfTenCache::kMinDecimalExponent) {
    *result = 0.0;
    return true;
  }
  DiyFp cached_power;
  int cached_decimal_exponent;
  PowersOfTenCache::GetCachedPowerForDecimalExponent(exponent, &cached_power,
                                                     &cached_decimal_exponent);

  if (cached_decimal_exponent != exponent) {
    int adjustment_exponent = exponent - cached_decimal_exponent;
    DiyFp adjustment_power = AdjustmentPowerOfTen(adjustment_exponent);
    input.Multiply(adjustment_power);
    if (kMaxUint64DecimalDigits - buffer.length() >= adjustment_exponent) {
      // The product of input with the adjustment power fits into a 64 bit
      // integer.
      DCHECK_EQ(DiyFp::kSignificandSize, 64);
    } else {
      // The adjustment power is exact. There is hence only an error of 0.5.
      error += kDenominator / 2;
    }
  }

  input.Multiply(cached_power);
  // The error introduced by a multiplication of a*b equals
  //   error_a + error_b + error_a*error_b/2^64 + 0.5
  // Substituting a with 'input' and b with 'cached_power' we have
  //   error_b = 0.5  (all cached powers have an error of less than 0.5 ulp),
  //   error_ab = 0 or 1 / kDenominator > error_a*error_b/ 2^64
  int error_b = kDenominator / 2;
  int error_ab = (error == 0 ? 0 : 1);  // We round up to 1.
  int fixed_error = kDenominator / 2;
  error += error_b + error_ab + fixed_error;

  old_e = input.e();
  input.Normalize();
  error <<= old_e - input.e();

  // See if the double's significand changes if we add/subtract the error.
  int order_of_magnitude = DiyFp::kSignificandSize + input.e();
  int effective_significand_size =
      Double::SignificandSizeForOrderOfMagnitude(order_of_magnitude);
  int precision_digits_count =
      DiyFp::kSignificandSize - effective_significand_size;
  if (precision_digits_count + kDenominatorLog >= DiyFp::kSignificandSize) {
    // This can only happen for very small denormals. In this case the
    // half-way multiplied by the denominator exceeds the range of an uint64.
    // Simply shift everything to the right.
    int shift_amount = (precision_digits_count + kDenominatorLog) -
                       DiyFp::kSignificandSize + 1;
    input.set_f(input.f() >> shift_amount);
    input.set_e(input.e() + shift_amount);
    // We add 1 for the lost precision of error, and kDenominator for
    // the lost precision of input.f().
    error = (error >> shift_amount) + 1 + kDenominator;
    precision_digits_count -= shift_amount;
  }
  // We use uint64_ts now. This only works if the DiyFp uses uint64_ts too.
  DCHECK_EQ(DiyFp::kSignificandSize, 64);
  DCHECK_LT(precision_digits_count, 64);
  uint64_t one64 = 1;
  uint64_t precision_bits_mask = (one64 << precision_digits_count) - 1;
  uint64_t precision_bits = input.f() & precision_bits_mask;
  uint64_t half_way = one64 << (precision_digits_count - 1);
  precision_bits *= kDenominator;
  half_way *= kDenominator;
  DiyFp rounded_input(input.f() >> precision_digits_count,
                      input.e() + precision_digits_count);
  if (precision_bits >= half_way + error) {
    rounded_input.set_f(rounded_input.f() + 1);
  }
  // If the last_bits are too close to the half-way case than we are too
  // inaccurate and round down. In this case we return false so that we can
  // fall back to a more precise algorithm.

  *result = Double(rounded_input).value();
  if (half_way - error < precision_bits && precision_bits < half_way + error) {
    // Too imprecise. The caller will have to fall back to a slower version.
    // However the returned number is guaranteed to be either the correct
    // double, or the next-lower double.
    return false;
  } else {
    return true;
  }
}

// Returns the correct double for the buffer*10^exponent.
// The variable guess should be a close guess that is either the correct double
// or its lower neighbor (the nearest double less than the correct one).
// Preconditions:
//   buffer.length() + exponent <= kMaxDecimalPower + 1
//   buffer.length() + exponent > kMinDecimalPower
//   buffer.length() <= kMaxDecimalSignificantDigits
static double BignumStrtod(Vector<const char> buffer, int exponent,
                           double guess) {
  if (guess == std::numeric_limits<double>::infinity()) {
    return guess;
  }

  DiyFp upper_boundary = Double(guess).UpperBoundary();

  DCHECK(buffer.length() + exponent <= kMaxDecimalPower + 1);
  DCHECK_GT(buffer.length() + exponent, kMinDecimalPower);
  DCHECK_LE(buffer.length(), kMaxSignificantDecimalDigits);
  // Make sure that the Bignum will be able to hold all our numbers.
  // Our Bignum implementation has a separate field for exponents. Shifts will
  // consume at most one bigit (< 64 bits).
  // ln(10) == 3.3219...
  DCHECK_LT((kMaxDecimalPower + 1) * 333 / 100, Bignum::kMaxSignificantBits);
  Bignum input;
  Bignum boundary;
  input.AssignDecimalString(buffer);
  boundary.AssignUInt64(upper_boundary.f());
  if (exponent >= 0) {
    input.MultiplyByPowerOfTen(exponent);
  } else {
    boundary.MultiplyByPowerOfTen(-exponent);
  }
  if (upper_boundary.e() > 0) {
    boundary.ShiftLeft(upper_boundary.e());
  } else {
    input.ShiftLeft(-upper_boundary.e());
  }
  int comparison = Bignum::Compare(input, boundary);
  if (comparison < 0) {
    return guess;
  } else if (comparison > 0) {
    return Double(guess).NextDouble();
  } else if ((Double(guess).Significand() & 1) == 0) {
    // Round towards even.
    return guess;
  } else {
    return Double(guess).NextDouble();
  }
}

double Strtod(Vector<const char> buffer, int exponent) {
  Vector<const char> left_trimmed = TrimLeadingZeros(buffer);
  Vector<const char> trimmed = TrimTrailingZeros(left_trimmed);
  exponent += left_trimmed.length() - trimmed.length();
  if (trimmed.empty()) return 0.0;
  if (trimmed.length() > kMaxSignificantDecimalDigits) {
    char significant_buffer[kMaxSignificantDecimalDigits];
    int significant_exponent;
    TrimToMaxSignificantDigits(trimmed, exponent, significant_buffer,
                               &significant_exponent);
    return Strtod(
        Vector<const char>(significant_buffer, kMaxSignificantDecimalDigits),
        significant_exponent);
  }
  if (exponent + trimmed.length() - 1 >= kMaxDecimalPower)
    return std::numeric_limits<double>::infinity();
  if (exponent + trimmed.length() <= kMinDecimalPower) return 0.0;

  double guess;
  if (DoubleStrtod(trimmed, exponent, &guess) ||
      DiyFpStrtod(trimmed, exponent, &guess)) {
    return guess;
  }
  return BignumStrtod(trimmed, exponent, guess);
}

}  // namespace base
}  // namespace v8
