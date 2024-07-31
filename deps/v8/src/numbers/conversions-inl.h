// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_CONVERSIONS_INL_H_
#define V8_NUMBERS_CONVERSIONS_INL_H_

#include <float.h>   // Required for DBL_MAX and on Win32 for finite()
#include <limits.h>  // Required for INT_MAX etc.
#include <stdarg.h>
#include <cmath>
#include "src/common/globals.h"  // Required for V8_INFINITY

// ----------------------------------------------------------------------------
// Extra POSIX/ANSI functions for Win32/MSVC.

#include "src/base/bits.h"
#include "src/base/numbers/double.h"
#include "src/base/platform/platform.h"
#include "src/numbers/conversions.h"
#include "src/objects/heap-number-inl.h"
#include "src/objects/smi-inl.h"

namespace v8 {
namespace internal {

// The fast double-to-unsigned-int conversion routine does not guarantee
// rounding towards zero, or any reasonable value if the argument is larger
// than what fits in an unsigned 32-bit integer.
inline unsigned int FastD2UI(double x) {
  // There is no unsigned version of lrint, so there is no fast path
  // in this function as there is in FastD2I. Using lrint doesn't work
  // for values of 2^31 and above.

  // Convert "small enough" doubles to uint32_t by fixing the 32
  // least significant non-fractional bits in the low 32 bits of the
  // double, and reading them from there.
  const double k2Pow52 = 4503599627370496.0;
  bool negative = x < 0;
  if (negative) {
    x = -x;
  }
  if (x < k2Pow52) {
    x += k2Pow52;
    uint32_t result;
#ifndef V8_TARGET_BIG_ENDIAN
    void* mantissa_ptr = reinterpret_cast<void*>(&x);
#else
    void* mantissa_ptr =
        reinterpret_cast<void*>(reinterpret_cast<Address>(&x) + kInt32Size);
#endif
    // Copy least significant 32 bits of mantissa.
    memcpy(&result, mantissa_ptr, sizeof(result));
    return negative ? ~result + 1 : result;
  }
  // Large number (outside uint32 range), Infinity or NaN.
  return 0x80000000u;  // Return integer indefinite.
}

// Adopted from https://gist.github.com/rygorous/2156668
inline uint16_t DoubleToFloat16(double value) {
  uint64_t in = base::bit_cast<uint64_t>(value);
  uint16_t out = 0;

  // Take the absolute value of the input.
  uint64_t sign = in & kFP64SignMask;
  in ^= sign;

  if (in >= kFP16InfinityAndNaNInfimum) {
    // Result is infinity or NaN.
    out = (in > kFP64Infinity) ? kFP16qNaN       // NaN->qNaN
                               : kFP16Infinity;  // Inf->Inf
  } else {
    // Result is a (de)normalized number or zero.

    if (in < kFP16DenormalThreshold) {
      // Result is a denormal or zero. Use the magic value and FP addition to
      // align 10 mantissa bits at the bottom of the float. Depends on FP
      // addition being round-to-nearest-even.
      double temp = base::bit_cast<double>(in) +
                    base::bit_cast<double>(kFP64To16DenormalMagic);
      out = base::bit_cast<uint64_t>(temp) - kFP64To16DenormalMagic;
    } else {
      // Result is not a denormal.

      // Remember if the result mantissa will be odd before rounding.
      uint64_t mant_odd = (in >> (kFP64MantissaBits - kFP16MantissaBits)) & 1;

      // Update the exponent and round to nearest even.
      //
      // Rounding to nearest even is handled in two parts. First, adding
      // kFP64To16RebiasExponentAndRound has the effect of rebiasing the
      // exponent and that if any of the lower 41 bits of the mantissa are set,
      // the 11th mantissa bit from the front becomes set. Second, adding
      // mant_odd ensures ties are rounded to even.
      in += kFP64To16RebiasExponentAndRound;
      in += mant_odd;

      out = in >> (kFP64MantissaBits - kFP16MantissaBits);
    }
  }

  out |= sign >> 48;
  return out;
}

inline float DoubleToFloat32(double x) {
  using limits = std::numeric_limits<float>;
  if (x > limits::max()) {
    // kRoundingThreshold is the maximum double that rounds down to
    // the maximum representable float. Its mantissa bits are:
    // 1111111111111111111111101111111111111111111111111111
    // [<--- float range --->]
    // Note the zero-bit right after the float mantissa range, which
    // determines the rounding-down.
    static const double kRoundingThreshold = 3.4028235677973362e+38;
    if (x <= kRoundingThreshold) return limits::max();
    return limits::infinity();
  }
  if (x < limits::lowest()) {
    // Same as above, mirrored to negative numbers.
    static const double kRoundingThreshold = -3.4028235677973362e+38;
    if (x >= kRoundingThreshold) return limits::lowest();
    return -limits::infinity();
  }
  return static_cast<float>(x);
}

// #sec-tointegerorinfinity
inline double DoubleToInteger(double x) {
  // ToIntegerOrInfinity normalizes -0 to +0. Special case 0 for performance.
  if (std::isnan(x) || x == 0.0) return 0;
  if (!std::isfinite(x)) return x;
  // Add 0.0 in the truncation case to ensure this doesn't return -0.
  return ((x > 0) ? std::floor(x) : std::ceil(x)) + 0.0;
}

// Implements most of https://tc39.github.io/ecma262/#sec-toint32.
int32_t DoubleToInt32(double x) {
  if ((std::isfinite(x)) && (x <= INT_MAX) && (x >= INT_MIN)) {
    // All doubles within these limits are trivially convertable to an int.
    return static_cast<int32_t>(x);
  }
  base::Double d(x);
  int exponent = d.Exponent();
  uint64_t bits;
  if (exponent < 0) {
    if (exponent <= -base::Double::kSignificandSize) return 0;
    bits = d.Significand() >> -exponent;
  } else {
    if (exponent > 31) return 0;
    // Masking to a 32-bit value ensures that the result of the
    // static_cast<int64_t> below is not the minimal int64_t value,
    // which would overflow on multiplication with d.Sign().
    bits = (d.Significand() << exponent) & 0xFFFFFFFFul;
  }
  return static_cast<int32_t>(d.Sign() * static_cast<int64_t>(bits));
}

// Implements https://heycam.github.io/webidl/#abstract-opdef-converttoint for
// the general case (step 1 and steps 8 to 12). Support for Clamp and
// EnforceRange will come in the future.
inline int64_t DoubleToWebIDLInt64(double x) {
  if ((std::isfinite(x)) && (x <= kMaxSafeInteger) && (x >= kMinSafeInteger)) {
    // All doubles within these limits are trivially convertable to an int.
    return static_cast<int64_t>(x);
  }
  base::Double d(x);
  int exponent = d.Exponent();
  uint64_t bits;
  if (exponent < 0) {
    if (exponent <= -base::Double::kSignificandSize) return 0;
    bits = d.Significand() >> -exponent;
  } else {
    if (exponent > 63) return 0;
    bits = (d.Significand() << exponent);
    int64_t bits_int64 = static_cast<int64_t>(bits);
    if (bits_int64 == std::numeric_limits<int64_t>::min()) {
      return bits_int64;
    }
  }
  return static_cast<int64_t>(d.Sign() * static_cast<int64_t>(bits));
}

inline uint64_t DoubleToWebIDLUint64(double x) {
  return static_cast<uint64_t>(DoubleToWebIDLInt64(x));
}

bool DoubleToSmiInteger(double value, int* smi_int_value) {
  if (!IsSmiDouble(value)) return false;
  *smi_int_value = FastD2I(value);
  DCHECK(Smi::IsValid(*smi_int_value));
  return true;
}

bool IsSmiDouble(double value) {
  return value >= Smi::kMinValue && value <= Smi::kMaxValue &&
         !IsMinusZero(value) && value == FastI2D(FastD2I(value));
}

bool IsInt32Double(double value) {
  return value >= kMinInt && value <= kMaxInt && !IsMinusZero(value) &&
         value == FastI2D(FastD2I(value));
}

bool IsUint32Double(double value) {
  return !IsMinusZero(value) && value >= 0 && value <= kMaxUInt32 &&
         value == FastUI2D(FastD2UI(value));
}

bool DoubleToUint32IfEqualToSelf(double value, uint32_t* uint32_value) {
  const double k2Pow52 = 4503599627370496.0;
  const uint32_t kValidTopBits = 0x43300000;
  const uint64_t kBottomBitMask = 0x0000'0000'FFFF'FFFF;

  // Add 2^52 to the double, to place valid uint32 values in the low-significant
  // bits of the exponent, by effectively setting the (implicit) top bit of the
  // significand. Note that this addition also normalises 0.0 and -0.0.
  double shifted_value = value + k2Pow52;

  // At this point, a valid uint32 valued double will be represented as:
  //
  // sign = 0
  // exponent = 52
  // significand = 1. 00...00 <value>
  //       implicit^          ^^^^^^^ 32 bits
  //                  ^^^^^^^^^^^^^^^ 52 bits
  //
  // Therefore, we can first check the top 32 bits to make sure that the sign,
  // exponent and remaining significand bits are valid, and only then check the
  // value in the bottom 32 bits.

  uint64_t result = base::bit_cast<uint64_t>(shifted_value);
  if ((result >> 32) == kValidTopBits) {
    *uint32_value = result & kBottomBitMask;
    return FastUI2D(result & kBottomBitMask) == value;
  }
  return false;
}

int32_t NumberToInt32(Tagged<Object> number) {
  if (IsSmi(number)) return Smi::ToInt(number);
  return DoubleToInt32(HeapNumber::cast(number)->value());
}

uint32_t NumberToUint32(Tagged<Object> number) {
  if (IsSmi(number)) return Smi::ToInt(number);
  return DoubleToUint32(HeapNumber::cast(number)->value());
}

uint32_t PositiveNumberToUint32(Tagged<Object> number) {
  if (IsSmi(number)) {
    int value = Smi::ToInt(number);
    if (value <= 0) return 0;
    return value;
  }
  double value = HeapNumber::cast(number)->value();
  // Catch all values smaller than 1 and use the double-negation trick for NANs.
  if (!(value >= 1)) return 0;
  uint32_t max = std::numeric_limits<uint32_t>::max();
  if (value < max) return static_cast<uint32_t>(value);
  return max;
}

int64_t NumberToInt64(Tagged<Object> number) {
  if (IsSmi(number)) return Smi::ToInt(number);
  double d = HeapNumber::cast(number)->value();
  if (std::isnan(d)) return 0;
  if (d >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    return std::numeric_limits<int64_t>::max();
  }
  if (d <= static_cast<double>(std::numeric_limits<int64_t>::min())) {
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(d);
}

uint64_t PositiveNumberToUint64(Tagged<Object> number) {
  if (IsSmi(number)) {
    int value = Smi::ToInt(number);
    if (value <= 0) return 0;
    return value;
  }
  double value = HeapNumber::cast(number)->value();
  // Catch all values smaller than 1 and use the double-negation trick for NANs.
  if (!(value >= 1)) return 0;
  uint64_t max = std::numeric_limits<uint64_t>::max();
  if (value < max) return static_cast<uint64_t>(value);
  return max;
}

bool TryNumberToSize(Tagged<Object> number, size_t* result) {
  // Do not create handles in this function! Don't use SealHandleScope because
  // the function can be used concurrently.
  if (IsSmi(number)) {
    int value = Smi::ToInt(number);
    DCHECK(static_cast<unsigned>(Smi::kMaxValue) <=
           std::numeric_limits<size_t>::max());
    if (value >= 0) {
      *result = static_cast<size_t>(value);
      return true;
    }
    return false;
  } else {
    double value = HeapNumber::cast(number)->value();
    // If value is compared directly to the limit, the limit will be
    // casted to a double and could end up as limit + 1,
    // because a double might not have enough mantissa bits for it.
    // So we might as well cast the limit first, and use < instead of <=.
    double maxSize = static_cast<double>(std::numeric_limits<size_t>::max());
    if (value >= 0 && value < maxSize) {
      *result = static_cast<size_t>(value);
      return true;
    } else {
      return false;
    }
  }
}

size_t NumberToSize(Tagged<Object> number) {
  size_t result = 0;
  bool is_valid = TryNumberToSize(number, &result);
  CHECK(is_valid);
  return result;
}

uint32_t DoubleToUint32(double x) {
  return static_cast<uint32_t>(DoubleToInt32(x));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_NUMBERS_CONVERSIONS_INL_H_
