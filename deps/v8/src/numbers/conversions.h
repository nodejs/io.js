// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_CONVERSIONS_H_
#define V8_NUMBERS_CONVERSIONS_H_

#include "src/base/export-template.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/optional.h"
#include "src/base/strings.h"
#include "src/base/vector.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

class BigInt;
class SharedStringAccessGuardIfNeeded;

// uint64_t constants prefixed with kFP64 are bit patterns of doubles.
// uint64_t constants prefixed with kFP16 are bit patterns of doubles encoding
// limits of half-precision floating point values.
constexpr int kFP64ExponentBits = 11;
constexpr int kFP64MantissaBits = 52;
constexpr uint64_t kFP64ExponentBias = 1023;
constexpr uint64_t kFP64SignMask = uint64_t{1}
                                   << (kFP64ExponentBits + kFP64MantissaBits);
constexpr uint64_t kFP64Infinity = uint64_t{2047} << kFP64MantissaBits;
constexpr uint64_t kFP16InfinityAndNaNInfimum = (kFP64ExponentBias + 16)
                                                << kFP64MantissaBits;
constexpr uint64_t kFP16MinExponent = kFP64ExponentBias - 14;
constexpr uint64_t kFP16DenormalThreshold = kFP16MinExponent
                                            << kFP64MantissaBits;

constexpr int kFP16MantissaBits = 10;
constexpr uint16_t kFP16qNaN = 0x7e00;
constexpr uint16_t kFP16Infinity = 0x7c00;

// A value that, when added, has the effect that if any of the lower 41 bits of
// the mantissa are set, the 11th mantissa bit from the front becomes set. Used
// for rounding when converting from double to half-precision.
constexpr uint64_t kFP64To16RoundingAddend =
    (uint64_t{1} << ((kFP64MantissaBits - kFP16MantissaBits) - 1)) - 1;
// A value that, when added, rebiases the exponent of a double to the range of
// the half precision and performs rounding as described above in
// kFP64To16RoundingAddend. Note that 15-kFP64ExponentBias overflows into the
// sign bit, but that bit is implicitly cut off when assigning the 64-bit double
// to a 16-bit output.
constexpr uint64_t kFP64To16RebiasExponentAndRound =
    ((uint64_t{15} - kFP64ExponentBias) << kFP64MantissaBits) +
    kFP64To16RoundingAddend;
// A magic value that aligns 10 mantissa bits at the bottom of the double when
// added to a double using floating point addition. Depends on floating point
// addition being round-to-nearest-even.
constexpr uint64_t kFP64To16DenormalMagic =
    (kFP16MinExponent + (kFP64MantissaBits - kFP16MantissaBits))
    << kFP64MantissaBits;

// The limit for the the fractionDigits/precision for toFixed, toPrecision
// and toExponential.
const int kMaxFractionDigits = 100;

// The fast double-to-(unsigned-)int conversion routine does not guarantee
// rounding towards zero.
// If x is NaN, the result is INT_MIN.  Otherwise the result is the argument x,
// clamped to [INT_MIN, INT_MAX] and then rounded to an integer.
inline int FastD2IChecked(double x) {
  if (!(x >= INT_MIN)) return INT_MIN;  // Negation to catch NaNs.
  if (x > INT_MAX) return INT_MAX;
  return static_cast<int>(x);
}

// The fast double-to-(unsigned-)int conversion routine does not guarantee
// rounding towards zero.
// The result is undefined if x is infinite or NaN, or if the rounded
// integer value is outside the range of type int.
inline int FastD2I(double x) {
  DCHECK(x <= INT_MAX);
  DCHECK(x >= INT_MIN);
  return static_cast<int32_t>(x);
}

inline unsigned int FastD2UI(double x);

inline double FastI2D(int x) {
  // There is no rounding involved in converting an integer to a
  // double, so this code should compile to a few instructions without
  // any FPU pipeline stalls.
  return static_cast<double>(x);
}

inline double FastUI2D(unsigned x) {
  // There is no rounding involved in converting an unsigned integer to a
  // double, so this code should compile to a few instructions without
  // any FPU pipeline stalls.
  return static_cast<double>(x);
}

// This function should match the exact semantics of ECMA-262 20.2.2.17.
inline float DoubleToFloat32(double x);
V8_EXPORT_PRIVATE float DoubleToFloat32_NoInline(double x);

// This function should match the exact semantics of truncating x to
// IEEE 754-2019 binary16 format using roundTiesToEven mode.
inline uint16_t DoubleToFloat16(double x);

// This function should match the exact semantics of ECMA-262 9.4.
inline double DoubleToInteger(double x);

// This function should match the exact semantics of ECMA-262 9.5.
inline int32_t DoubleToInt32(double x);
V8_EXPORT_PRIVATE int32_t DoubleToInt32_NoInline(double x);

// This function should match the exact semantics of ECMA-262 9.6.
inline uint32_t DoubleToUint32(double x);

// These functions have similar semantics as the ones above, but are
// added for 64-bit integer types.
inline int64_t DoubleToInt64(double x);
inline uint64_t DoubleToUint64(double x);

// Enumeration for allowing octals and ignoring junk when converting
// strings to numbers.
enum ConversionFlags {
  NO_CONVERSION_FLAGS = 0,
  ALLOW_HEX = 1,
  ALLOW_OCTAL = 2,
  ALLOW_IMPLICIT_OCTAL = 4,
  ALLOW_BINARY = 8,
  ALLOW_TRAILING_JUNK = 16
};

// Converts a string into a double value according to ECMA-262 9.3.1
double StringToDouble(base::Vector<const uint8_t> str, int flags,
                      double empty_string_val = 0);
double StringToDouble(base::Vector<const base::uc16> str, int flags,
                      double empty_string_val = 0);
// This version expects a zero-terminated character array.
double V8_EXPORT_PRIVATE StringToDouble(const char* str, int flags,
                                        double empty_string_val = 0);

double StringToInt(Isolate* isolate, Handle<String> string, int radix);

// This follows https://tc39.github.io/proposal-bigint/#sec-string-to-bigint
// semantics: "" => 0n.
MaybeHandle<BigInt> StringToBigInt(Isolate* isolate, Handle<String> string);

// This version expects a zero-terminated character array. Radix will
// be inferred from string prefix (case-insensitive):
//   0x -> hex
//   0o -> octal
//   0b -> binary
template <typename IsolateT>
EXPORT_TEMPLATE_DECLARE(V8_EXPORT_PRIVATE)
MaybeHandle<BigInt> BigIntLiteral(IsolateT* isolate, const char* string);

const int kDoubleToCStringMinBufferSize = 100;

// Converts a double to a string value according to ECMA-262 9.8.1.
// The buffer should be large enough for any floating point number.
// 100 characters is enough.
V8_EXPORT_PRIVATE const char* DoubleToCString(double value,
                                              base::Vector<char> buffer);

V8_EXPORT_PRIVATE std::unique_ptr<char[]> BigIntLiteralToDecimal(
    LocalIsolate* isolate, base::Vector<const uint8_t> literal);
// Convert an int to a null-terminated string. The returned string is
// located inside the buffer, but not necessarily at the start.
V8_EXPORT_PRIVATE const char* IntToCString(int n, base::Vector<char> buffer);

// Additional number to string conversions for the number type.
// The caller is responsible for calling free on the returned pointer.
char* DoubleToFixedCString(double value, int f);
char* DoubleToExponentialCString(double value, int f);
char* DoubleToPrecisionCString(double value, int f);
char* DoubleToRadixCString(double value, int radix);

static inline bool IsMinusZero(double value) {
  return base::bit_cast<int64_t>(value) == base::bit_cast<int64_t>(-0.0);
}

// Returns true if value can be converted to a SMI, and returns the resulting
// integer value of the SMI in |smi_int_value|.
inline bool DoubleToSmiInteger(double value, int* smi_int_value);

inline bool IsSmiDouble(double value);

// Integer32 is an integer that can be represented as a signed 32-bit
// integer. It has to be in the range [-2^31, 2^31 - 1].
// We also have to check for negative 0 as it is not an Integer32.
inline bool IsInt32Double(double value);

// UInteger32 is an integer that can be represented as an unsigned 32-bit
// integer. It has to be in the range [0, 2^32 - 1].
// We also have to check for negative 0 as it is not a UInteger32.
inline bool IsUint32Double(double value);

// Tries to convert |value| to a uint32, setting the result in |uint32_value|.
// If the output does not compare equal to the input, returns false and the
// value in |uint32_value| is left unspecified.
// Used for conversions such as in ECMA-262 15.4.2.2, which check "ToUint32(len)
// is equal to len".
inline bool DoubleToUint32IfEqualToSelf(double value, uint32_t* uint32_value);

// Convert from Number object to C integer.
inline uint32_t PositiveNumberToUint32(Tagged<Object> number);
inline int32_t NumberToInt32(Tagged<Object> number);
inline uint32_t NumberToUint32(Tagged<Object> number);
inline int64_t NumberToInt64(Tagged<Object> number);
inline uint64_t PositiveNumberToUint64(Tagged<Object> number);

double StringToDouble(Isolate* isolate, Handle<String> string, int flags,
                      double empty_string_val = 0.0);
double FlatStringToDouble(Tagged<String> string, int flags,
                          double empty_string_val);

// String to double helper without heap allocation.
// Returns base::nullopt if the string is longer than
// {max_length_for_conversion}. 23 was chosen because any representable double
// can be represented using a string of length 23.
V8_EXPORT_PRIVATE base::Optional<double> TryStringToDouble(
    LocalIsolate* isolate, Handle<String> object,
    int max_length_for_conversion = 23);

// Return base::nullopt if the string is longer than 20.
V8_EXPORT_PRIVATE base::Optional<double> TryStringToInt(LocalIsolate* isolate,
                                                        Handle<String> object,
                                                        int radix);

inline bool TryNumberToSize(Tagged<Object> number, size_t* result);

// Converts a number into size_t.
inline size_t NumberToSize(Tagged<Object> number);

// returns DoubleToString(StringToDouble(string)) == string
V8_EXPORT_PRIVATE bool IsSpecialIndex(
    Tagged<String> string, SharedStringAccessGuardIfNeeded& access_guard);
V8_EXPORT_PRIVATE bool IsSpecialIndex(Tagged<String> string);

}  // namespace internal
}  // namespace v8

#endif  // V8_NUMBERS_CONVERSIONS_H_
