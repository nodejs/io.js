//
// Copyright 2017 The Abseil Authors.
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
//
// -----------------------------------------------------------------------------
// File: int128.h
// -----------------------------------------------------------------------------
//
// This header file defines 128-bit integer types, `uint128` and `int128`.
//
// TODO(absl-team): This module is inconsistent as many inline `uint128` methods
// are defined in this file, while many inline `int128` methods are defined in
// the `int128_*_intrinsic.inc` files.

#ifndef ABSL_NUMERIC_INT128_H_
#define ABSL_NUMERIC_INT128_H_

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iosfwd>
#include <limits>
#include <string>
#include <utility>

#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/port.h"

#if defined(_MSC_VER)
// In very old versions of MSVC and when the /Zc:wchar_t flag is off, wchar_t is
// a typedef for unsigned short.  Otherwise wchar_t is mapped to the __wchar_t
// builtin type.  We need to make sure not to define operator wchar_t()
// alongside operator unsigned short() in these instances.
#define ABSL_INTERNAL_WCHAR_T __wchar_t
#if defined(_M_X64) && !defined(_M_ARM64EC)
#include <intrin.h>
#pragma intrinsic(_umul128)
#endif  // defined(_M_X64)
#else   // defined(_MSC_VER)
#define ABSL_INTERNAL_WCHAR_T wchar_t
#endif  // defined(_MSC_VER)

namespace absl {
ABSL_NAMESPACE_BEGIN

class int128;

// uint128
//
// An unsigned 128-bit integer type. The API is meant to mimic an intrinsic type
// as closely as is practical, including exhibiting undefined behavior in
// analogous cases (e.g. division by zero). This type is intended to be a
// drop-in replacement once C++ supports an intrinsic `uint128_t` type; when
// that occurs, existing well-behaved uses of `uint128` will continue to work
// using that new type.
//
// Note: code written with this type will continue to compile once `uint128_t`
// is introduced, provided the replacement helper functions
// `Uint128(Low|High)64()` and `MakeUint128()` are made.
//
// A `uint128` supports the following:
//
//   * Implicit construction from integral types
//   * Explicit conversion to integral types
//
// Additionally, if your compiler supports `__int128`, `uint128` is
// interoperable with that type. (Abseil checks for this compatibility through
// the `ABSL_HAVE_INTRINSIC_INT128` macro.)
//
// However, a `uint128` differs from intrinsic integral types in the following
// ways:
//
//   * Errors on implicit conversions that do not preserve value (such as
//     loss of precision when converting to float values).
//   * Requires explicit construction from and conversion to floating point
//     types.
//   * Conversion to integral types requires an explicit static_cast() to
//     mimic use of the `-Wnarrowing` compiler flag.
//   * The alignment requirement of `uint128` may differ from that of an
//     intrinsic 128-bit integer type depending on platform and build
//     configuration.
//
// Example:
//
//     float y = absl::Uint128Max();  // Error. uint128 cannot be implicitly
//                                    // converted to float.
//
//     absl::uint128 v;
//     uint64_t i = v;                         // Error
//     uint64_t i = static_cast<uint64_t>(v);  // OK
//
class
#if defined(ABSL_HAVE_INTRINSIC_INT128)
    alignas(unsigned __int128)
#endif  // ABSL_HAVE_INTRINSIC_INT128
        uint128 {
 public:
  uint128() = default;

  // Constructors from arithmetic types
  constexpr uint128(int v);                 // NOLINT(runtime/explicit)
  constexpr uint128(unsigned int v);        // NOLINT(runtime/explicit)
  constexpr uint128(long v);                // NOLINT(runtime/int)
  constexpr uint128(unsigned long v);       // NOLINT(runtime/int)
  constexpr uint128(long long v);           // NOLINT(runtime/int)
  constexpr uint128(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr uint128(__int128 v);           // NOLINT(runtime/explicit)
  constexpr uint128(unsigned __int128 v);  // NOLINT(runtime/explicit)
#endif                                     // ABSL_HAVE_INTRINSIC_INT128
  constexpr uint128(int128 v);             // NOLINT(runtime/explicit)
  explicit uint128(float v);
  explicit uint128(double v);
  explicit uint128(long double v);

  // Assignment operators from arithmetic types
  uint128& operator=(int v);
  uint128& operator=(unsigned int v);
  uint128& operator=(long v);                // NOLINT(runtime/int)
  uint128& operator=(unsigned long v);       // NOLINT(runtime/int)
  uint128& operator=(long long v);           // NOLINT(runtime/int)
  uint128& operator=(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  uint128& operator=(__int128 v);
  uint128& operator=(unsigned __int128 v);
#endif  // ABSL_HAVE_INTRINSIC_INT128
  uint128& operator=(int128 v);

  // Conversion operators to other arithmetic types
  constexpr explicit operator bool() const;
  constexpr explicit operator char() const;
  constexpr explicit operator signed char() const;
  constexpr explicit operator unsigned char() const;
  constexpr explicit operator char16_t() const;
  constexpr explicit operator char32_t() const;
  constexpr explicit operator ABSL_INTERNAL_WCHAR_T() const;
  constexpr explicit operator short() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned short() const;
  constexpr explicit operator int() const;
  constexpr explicit operator unsigned int() const;
  constexpr explicit operator long() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator long long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long long() const;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit operator __int128() const;
  constexpr explicit operator unsigned __int128() const;
#endif  // ABSL_HAVE_INTRINSIC_INT128
  explicit operator float() const;
  explicit operator double() const;
  explicit operator long double() const;

  // Trivial copy constructor, assignment operator and destructor.

  // Arithmetic operators.
  uint128& operator+=(uint128 other);
  uint128& operator-=(uint128 other);
  uint128& operator*=(uint128 other);
  // Long division/modulo for uint128.
  uint128& operator/=(uint128 other);
  uint128& operator%=(uint128 other);
  uint128 operator++(int);
  uint128 operator--(int);
  uint128& operator<<=(int);
  uint128& operator>>=(int);
  uint128& operator&=(uint128 other);
  uint128& operator|=(uint128 other);
  uint128& operator^=(uint128 other);
  uint128& operator++();
  uint128& operator--();

  // Uint128Low64()
  //
  // Returns the lower 64-bit value of a `uint128` value.
  friend constexpr uint64_t Uint128Low64(uint128 v);

  // Uint128High64()
  //
  // Returns the higher 64-bit value of a `uint128` value.
  friend constexpr uint64_t Uint128High64(uint128 v);

  // MakeUInt128()
  //
  // Constructs a `uint128` numeric value from two 64-bit unsigned integers.
  // Note that this factory function is the only way to construct a `uint128`
  // from integer values greater than 2^64.
  //
  // Example:
  //
  //   absl::uint128 big = absl::MakeUint128(1, 0);
  friend constexpr uint128 MakeUint128(uint64_t high, uint64_t low);

  // Uint128Max()
  //
  // Returns the highest value for a 128-bit unsigned integer.
  friend constexpr uint128 Uint128Max();

  // Support for absl::Hash.
  template <typename H>
  friend H AbslHashValue(H h, uint128 v) {
    return H::combine(std::move(h), Uint128High64(v), Uint128Low64(v));
  }

  // Support for absl::StrCat() etc.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, uint128 v) {
    sink.Append(v.ToString());
  }

 private:
  constexpr uint128(uint64_t high, uint64_t low);

  std::string ToString() const;

  // TODO(strel) Update implementation to use __int128 once all users of
  // uint128 are fixed to not depend on alignof(uint128) == 8. Also add
  // alignas(16) to class definition to keep alignment consistent across
  // platforms.
#if defined(ABSL_IS_LITTLE_ENDIAN)
  uint64_t lo_;
  uint64_t hi_;
#elif defined(ABSL_IS_BIG_ENDIAN)
  uint64_t hi_;
  uint64_t lo_;
#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order
};

// allow uint128 to be logged
std::ostream& operator<<(std::ostream& os, uint128 v);

// TODO(strel) add operator>>(std::istream&, uint128)

constexpr uint128 Uint128Max() {
  return uint128((std::numeric_limits<uint64_t>::max)(),
                 (std::numeric_limits<uint64_t>::max)());
}

ABSL_NAMESPACE_END
}  // namespace absl

// Specialized numeric_limits for uint128.
namespace std {
template <>
class numeric_limits<absl::uint128> {
 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = false;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = true;
  static constexpr int digits = 128;
  static constexpr int digits10 = 38;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool traps = numeric_limits<unsigned __int128>::traps;
#else   // ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool traps = numeric_limits<uint64_t>::traps;
#endif  // ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool tinyness_before = false;

  static constexpr absl::uint128(min)() { return 0; }
  static constexpr absl::uint128 lowest() { return 0; }
  static constexpr absl::uint128(max)() { return absl::Uint128Max(); }
  static constexpr absl::uint128 epsilon() { return 0; }
  static constexpr absl::uint128 round_error() { return 0; }
  static constexpr absl::uint128 infinity() { return 0; }
  static constexpr absl::uint128 quiet_NaN() { return 0; }
  static constexpr absl::uint128 signaling_NaN() { return 0; }
  static constexpr absl::uint128 denorm_min() { return 0; }
};
}  // namespace std

namespace absl {
ABSL_NAMESPACE_BEGIN

// int128
//
// A signed 128-bit integer type. The API is meant to mimic an intrinsic
// integral type as closely as is practical, including exhibiting undefined
// behavior in analogous cases (e.g. division by zero).
//
// An `int128` supports the following:
//
//   * Implicit construction from integral types
//   * Explicit conversion to integral types
//
// However, an `int128` differs from intrinsic integral types in the following
// ways:
//
//   * It is not implicitly convertible to other integral types.
//   * Requires explicit construction from and conversion to floating point
//     types.

// Additionally, if your compiler supports `__int128`, `int128` is
// interoperable with that type. (Abseil checks for this compatibility through
// the `ABSL_HAVE_INTRINSIC_INT128` macro.)
//
// The design goal for `int128` is that it will be compatible with a future
// `int128_t`, if that type becomes a part of the standard.
//
// Example:
//
//     float y = absl::int128(17);  // Error. int128 cannot be implicitly
//                                  // converted to float.
//
//     absl::int128 v;
//     int64_t i = v;                        // Error
//     int64_t i = static_cast<int64_t>(v);  // OK
//
class int128 {
 public:
  int128() = default;

  // Constructors from arithmetic types
  constexpr int128(int v);                 // NOLINT(runtime/explicit)
  constexpr int128(unsigned int v);        // NOLINT(runtime/explicit)
  constexpr int128(long v);                // NOLINT(runtime/int)
  constexpr int128(unsigned long v);       // NOLINT(runtime/int)
  constexpr int128(long long v);           // NOLINT(runtime/int)
  constexpr int128(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr int128(__int128 v);  // NOLINT(runtime/explicit)
  constexpr explicit int128(unsigned __int128 v);
#endif  // ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit int128(uint128 v);
  explicit int128(float v);
  explicit int128(double v);
  explicit int128(long double v);

  // Assignment operators from arithmetic types
  int128& operator=(int v);
  int128& operator=(unsigned int v);
  int128& operator=(long v);                // NOLINT(runtime/int)
  int128& operator=(unsigned long v);       // NOLINT(runtime/int)
  int128& operator=(long long v);           // NOLINT(runtime/int)
  int128& operator=(unsigned long long v);  // NOLINT(runtime/int)
#ifdef ABSL_HAVE_INTRINSIC_INT128
  int128& operator=(__int128 v);
#endif  // ABSL_HAVE_INTRINSIC_INT128

  // Conversion operators to other arithmetic types
  constexpr explicit operator bool() const;
  constexpr explicit operator char() const;
  constexpr explicit operator signed char() const;
  constexpr explicit operator unsigned char() const;
  constexpr explicit operator char16_t() const;
  constexpr explicit operator char32_t() const;
  constexpr explicit operator ABSL_INTERNAL_WCHAR_T() const;
  constexpr explicit operator short() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned short() const;
  constexpr explicit operator int() const;
  constexpr explicit operator unsigned int() const;
  constexpr explicit operator long() const;  // NOLINT(runtime/int)
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator long long() const;
  // NOLINTNEXTLINE(runtime/int)
  constexpr explicit operator unsigned long long() const;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  constexpr explicit operator __int128() const;
  constexpr explicit operator unsigned __int128() const;
#endif  // ABSL_HAVE_INTRINSIC_INT128
  explicit operator float() const;
  explicit operator double() const;
  explicit operator long double() const;

  // Trivial copy constructor, assignment operator and destructor.

  // Arithmetic operators
  int128& operator+=(int128 other);
  int128& operator-=(int128 other);
  int128& operator*=(int128 other);
  int128& operator/=(int128 other);
  int128& operator%=(int128 other);
  int128 operator++(int);  // postfix increment: i++
  int128 operator--(int);  // postfix decrement: i--
  int128& operator++();    // prefix increment:  ++i
  int128& operator--();    // prefix decrement:  --i
  int128& operator&=(int128 other);
  int128& operator|=(int128 other);
  int128& operator^=(int128 other);
  int128& operator<<=(int amount);
  int128& operator>>=(int amount);

  // Int128Low64()
  //
  // Returns the lower 64-bit value of a `int128` value.
  friend constexpr uint64_t Int128Low64(int128 v);

  // Int128High64()
  //
  // Returns the higher 64-bit value of a `int128` value.
  friend constexpr int64_t Int128High64(int128 v);

  // MakeInt128()
  //
  // Constructs a `int128` numeric value from two 64-bit integers. Note that
  // signedness is conveyed in the upper `high` value.
  //
  //   (absl::int128(1) << 64) * high + low
  //
  // Note that this factory function is the only way to construct a `int128`
  // from integer values greater than 2^64 or less than -2^64.
  //
  // Example:
  //
  //   absl::int128 big = absl::MakeInt128(1, 0);
  //   absl::int128 big_n = absl::MakeInt128(-1, 0);
  friend constexpr int128 MakeInt128(int64_t high, uint64_t low);

  // Int128Max()
  //
  // Returns the maximum value for a 128-bit signed integer.
  friend constexpr int128 Int128Max();

  // Int128Min()
  //
  // Returns the minimum value for a 128-bit signed integer.
  friend constexpr int128 Int128Min();

  // Support for absl::Hash.
  template <typename H>
  friend H AbslHashValue(H h, int128 v) {
    return H::combine(std::move(h), Int128High64(v), Int128Low64(v));
  }

  // Support for absl::StrCat() etc.
  template <typename Sink>
  friend void AbslStringify(Sink& sink, int128 v) {
    sink.Append(v.ToString());
  }

 private:
  constexpr int128(int64_t high, uint64_t low);

  std::string ToString() const;

#if defined(ABSL_HAVE_INTRINSIC_INT128)
  __int128 v_;
#else  // ABSL_HAVE_INTRINSIC_INT128
#if defined(ABSL_IS_LITTLE_ENDIAN)
  uint64_t lo_;
  int64_t hi_;
#elif defined(ABSL_IS_BIG_ENDIAN)
  int64_t hi_;
  uint64_t lo_;
#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order
#endif  // ABSL_HAVE_INTRINSIC_INT128
};

std::ostream& operator<<(std::ostream& os, int128 v);

// TODO(absl-team) add operator>>(std::istream&, int128)

constexpr int128 Int128Max() {
  return int128((std::numeric_limits<int64_t>::max)(),
                (std::numeric_limits<uint64_t>::max)());
}

constexpr int128 Int128Min() {
  return int128((std::numeric_limits<int64_t>::min)(), 0);
}

ABSL_NAMESPACE_END
}  // namespace absl

// Specialized numeric_limits for int128.
namespace std {
template <>
class numeric_limits<absl::int128> {
 public:
  static constexpr bool is_specialized = true;
  static constexpr bool is_signed = true;
  static constexpr bool is_integer = true;
  static constexpr bool is_exact = true;
  static constexpr bool has_infinity = false;
  static constexpr bool has_quiet_NaN = false;
  static constexpr bool has_signaling_NaN = false;
  static constexpr float_denorm_style has_denorm = denorm_absent;
  static constexpr bool has_denorm_loss = false;
  static constexpr float_round_style round_style = round_toward_zero;
  static constexpr bool is_iec559 = false;
  static constexpr bool is_bounded = true;
  static constexpr bool is_modulo = false;
  static constexpr int digits = 127;
  static constexpr int digits10 = 38;
  static constexpr int max_digits10 = 0;
  static constexpr int radix = 2;
  static constexpr int min_exponent = 0;
  static constexpr int min_exponent10 = 0;
  static constexpr int max_exponent = 0;
  static constexpr int max_exponent10 = 0;
#ifdef ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool traps = numeric_limits<__int128>::traps;
#else   // ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool traps = numeric_limits<uint64_t>::traps;
#endif  // ABSL_HAVE_INTRINSIC_INT128
  static constexpr bool tinyness_before = false;

  static constexpr absl::int128(min)() { return absl::Int128Min(); }
  static constexpr absl::int128 lowest() { return absl::Int128Min(); }
  static constexpr absl::int128(max)() { return absl::Int128Max(); }
  static constexpr absl::int128 epsilon() { return 0; }
  static constexpr absl::int128 round_error() { return 0; }
  static constexpr absl::int128 infinity() { return 0; }
  static constexpr absl::int128 quiet_NaN() { return 0; }
  static constexpr absl::int128 signaling_NaN() { return 0; }
  static constexpr absl::int128 denorm_min() { return 0; }
};
}  // namespace std

// --------------------------------------------------------------------------
//                      Implementation details follow
// --------------------------------------------------------------------------
namespace absl {
ABSL_NAMESPACE_BEGIN

constexpr uint128 MakeUint128(uint64_t high, uint64_t low) {
  return uint128(high, low);
}

// Assignment from integer types.

inline uint128& uint128::operator=(int v) { return *this = uint128(v); }

inline uint128& uint128::operator=(unsigned int v) {
  return *this = uint128(v);
}

inline uint128& uint128::operator=(long v) {  // NOLINT(runtime/int)
  return *this = uint128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(unsigned long v) {
  return *this = uint128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(long long v) { return *this = uint128(v); }

// NOLINTNEXTLINE(runtime/int)
inline uint128& uint128::operator=(unsigned long long v) {
  return *this = uint128(v);
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
inline uint128& uint128::operator=(__int128 v) { return *this = uint128(v); }

inline uint128& uint128::operator=(unsigned __int128 v) {
  return *this = uint128(v);
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

inline uint128& uint128::operator=(int128 v) { return *this = uint128(v); }

// Arithmetic operators.

constexpr uint128 operator<<(uint128 lhs, int amount);
constexpr uint128 operator>>(uint128 lhs, int amount);
constexpr uint128 operator+(uint128 lhs, uint128 rhs);
constexpr uint128 operator-(uint128 lhs, uint128 rhs);
uint128 operator*(uint128 lhs, uint128 rhs);
uint128 operator/(uint128 lhs, uint128 rhs);
uint128 operator%(uint128 lhs, uint128 rhs);

inline uint128& uint128::operator<<=(int amount) {
  *this = *this << amount;
  return *this;
}

inline uint128& uint128::operator>>=(int amount) {
  *this = *this >> amount;
  return *this;
}

inline uint128& uint128::operator+=(uint128 other) {
  *this = *this + other;
  return *this;
}

inline uint128& uint128::operator-=(uint128 other) {
  *this = *this - other;
  return *this;
}

inline uint128& uint128::operator*=(uint128 other) {
  *this = *this * other;
  return *this;
}

inline uint128& uint128::operator/=(uint128 other) {
  *this = *this / other;
  return *this;
}

inline uint128& uint128::operator%=(uint128 other) {
  *this = *this % other;
  return *this;
}

constexpr uint64_t Uint128Low64(uint128 v) { return v.lo_; }

constexpr uint64_t Uint128High64(uint128 v) { return v.hi_; }

// Constructors from integer types.

#if defined(ABSL_IS_LITTLE_ENDIAN)

constexpr uint128::uint128(uint64_t high, uint64_t low) : lo_{low}, hi_{high} {}

constexpr uint128::uint128(int v)
    : lo_{static_cast<uint64_t>(v)},
      hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0} {}
constexpr uint128::uint128(long v)  // NOLINT(runtime/int)
    : lo_{static_cast<uint64_t>(v)},
      hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0} {}
constexpr uint128::uint128(long long v)  // NOLINT(runtime/int)
    : lo_{static_cast<uint64_t>(v)},
      hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0} {}

constexpr uint128::uint128(unsigned int v) : lo_{v}, hi_{0} {}
// NOLINTNEXTLINE(runtime/int)
constexpr uint128::uint128(unsigned long v) : lo_{v}, hi_{0} {}
// NOLINTNEXTLINE(runtime/int)
constexpr uint128::uint128(unsigned long long v) : lo_{v}, hi_{0} {}

#ifdef ABSL_HAVE_INTRINSIC_INT128
constexpr uint128::uint128(__int128 v)
    : lo_{static_cast<uint64_t>(v & ~uint64_t{0})},
      hi_{static_cast<uint64_t>(static_cast<unsigned __int128>(v) >> 64)} {}
constexpr uint128::uint128(unsigned __int128 v)
    : lo_{static_cast<uint64_t>(v & ~uint64_t{0})},
      hi_{static_cast<uint64_t>(v >> 64)} {}
#endif  // ABSL_HAVE_INTRINSIC_INT128

constexpr uint128::uint128(int128 v)
    : lo_{Int128Low64(v)}, hi_{static_cast<uint64_t>(Int128High64(v))} {}

#elif defined(ABSL_IS_BIG_ENDIAN)

constexpr uint128::uint128(uint64_t high, uint64_t low) : hi_{high}, lo_{low} {}

constexpr uint128::uint128(int v)
    : hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0},
      lo_{static_cast<uint64_t>(v)} {}
constexpr uint128::uint128(long v)  // NOLINT(runtime/int)
    : hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0},
      lo_{static_cast<uint64_t>(v)} {}
constexpr uint128::uint128(long long v)  // NOLINT(runtime/int)
    : hi_{v < 0 ? (std::numeric_limits<uint64_t>::max)() : 0},
      lo_{static_cast<uint64_t>(v)} {}

constexpr uint128::uint128(unsigned int v) : hi_{0}, lo_{v} {}
// NOLINTNEXTLINE(runtime/int)
constexpr uint128::uint128(unsigned long v) : hi_{0}, lo_{v} {}
// NOLINTNEXTLINE(runtime/int)
constexpr uint128::uint128(unsigned long long v) : hi_{0}, lo_{v} {}

#ifdef ABSL_HAVE_INTRINSIC_INT128
constexpr uint128::uint128(__int128 v)
    : hi_{static_cast<uint64_t>(static_cast<unsigned __int128>(v) >> 64)},
      lo_{static_cast<uint64_t>(v & ~uint64_t{0})} {}
constexpr uint128::uint128(unsigned __int128 v)
    : hi_{static_cast<uint64_t>(v >> 64)},
      lo_{static_cast<uint64_t>(v & ~uint64_t{0})} {}
#endif  // ABSL_HAVE_INTRINSIC_INT128

constexpr uint128::uint128(int128 v)
    : hi_{static_cast<uint64_t>(Int128High64(v))}, lo_{Int128Low64(v)} {}

#else  // byte order
#error "Unsupported byte order: must be little-endian or big-endian."
#endif  // byte order

// Conversion operators to integer types.

constexpr uint128::operator bool() const { return lo_ || hi_; }

constexpr uint128::operator char() const { return static_cast<char>(lo_); }

constexpr uint128::operator signed char() const {
  return static_cast<signed char>(lo_);
}

constexpr uint128::operator unsigned char() const {
  return static_cast<unsigned char>(lo_);
}

constexpr uint128::operator char16_t() const {
  return static_cast<char16_t>(lo_);
}

constexpr uint128::operator char32_t() const {
  return static_cast<char32_t>(lo_);
}

constexpr uint128::operator ABSL_INTERNAL_WCHAR_T() const {
  return static_cast<ABSL_INTERNAL_WCHAR_T>(lo_);
}

// NOLINTNEXTLINE(runtime/int)
constexpr uint128::operator short() const { return static_cast<short>(lo_); }

constexpr uint128::operator unsigned short() const {  // NOLINT(runtime/int)
  return static_cast<unsigned short>(lo_);            // NOLINT(runtime/int)
}

constexpr uint128::operator int() const { return static_cast<int>(lo_); }

constexpr uint128::operator unsigned int() const {
  return static_cast<unsigned int>(lo_);
}

// NOLINTNEXTLINE(runtime/int)
constexpr uint128::operator long() const { return static_cast<long>(lo_); }

constexpr uint128::operator unsigned long() const {  // NOLINT(runtime/int)
  return static_cast<unsigned long>(lo_);            // NOLINT(runtime/int)
}

constexpr uint128::operator long long() const {  // NOLINT(runtime/int)
  return static_cast<long long>(lo_);            // NOLINT(runtime/int)
}

constexpr uint128::operator unsigned long long() const {  // NOLINT(runtime/int)
  return static_cast<unsigned long long>(lo_);            // NOLINT(runtime/int)
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
constexpr uint128::operator __int128() const {
  return (static_cast<__int128>(hi_) << 64) + lo_;
}

constexpr uint128::operator unsigned __int128() const {
  return (static_cast<unsigned __int128>(hi_) << 64) + lo_;
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

// Conversion operators to floating point types.

inline uint128::operator float() const {
  return static_cast<float>(lo_) + std::ldexp(static_cast<float>(hi_), 64);
}

inline uint128::operator double() const {
  return static_cast<double>(lo_) + std::ldexp(static_cast<double>(hi_), 64);
}

inline uint128::operator long double() const {
  return static_cast<long double>(lo_) +
         std::ldexp(static_cast<long double>(hi_), 64);
}

// Comparison operators.

constexpr bool operator==(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) ==
         static_cast<unsigned __int128>(rhs);
#else
  return (Uint128Low64(lhs) == Uint128Low64(rhs) &&
          Uint128High64(lhs) == Uint128High64(rhs));
#endif
}

constexpr bool operator!=(uint128 lhs, uint128 rhs) { return !(lhs == rhs); }

constexpr bool operator<(uint128 lhs, uint128 rhs) {
#ifdef ABSL_HAVE_INTRINSIC_INT128
  return static_cast<unsigned __int128>(lhs) <
         static_cast<unsigned __int128>(rhs);
#else
  return (Uint128High64(lhs) == Uint128High64(rhs))
             ? (Uint128Low64(lhs) < Uint128Low64(rhs))
             : (Uint128High64(lhs) < Uint128High64(rhs));
#endif
}

constexpr bool operator>(uint128 lhs, uint128 rhs) { return rhs < lhs; }

constexpr bool operator<=(uint128 lhs, uint128 rhs) { return !(rhs < lhs); }

constexpr bool operator>=(uint128 lhs, uint128 rhs) { return !(lhs < rhs); }

// Unary operators.

constexpr inline uint128 operator+(uint128 val) { return val; }

constexpr inline int128 operator+(int128 val) { return val; }

constexpr uint128 operator-(uint128 val) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return -static_cast<unsigned __int128>(val);
#else
  return MakeUint128(
      ~Uint128High64(val) + static_cast<unsigned long>(Uint128Low64(val) == 0),
      ~Uint128Low64(val) + 1);
#endif
}

constexpr inline bool operator!(uint128 val) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return !static_cast<unsigned __int128>(val);
#else
  return !Uint128High64(val) && !Uint128Low64(val);
#endif
}

// Logical operators.

constexpr inline uint128 operator~(uint128 val) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return ~static_cast<unsigned __int128>(val);
#else
  return MakeUint128(~Uint128High64(val), ~Uint128Low64(val));
#endif
}

constexpr inline uint128 operator|(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) |
         static_cast<unsigned __int128>(rhs);
#else
  return MakeUint128(Uint128High64(lhs) | Uint128High64(rhs),
                     Uint128Low64(lhs) | Uint128Low64(rhs));
#endif
}

constexpr inline uint128 operator&(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) &
         static_cast<unsigned __int128>(rhs);
#else
  return MakeUint128(Uint128High64(lhs) & Uint128High64(rhs),
                     Uint128Low64(lhs) & Uint128Low64(rhs));
#endif
}

constexpr inline uint128 operator^(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) ^
         static_cast<unsigned __int128>(rhs);
#else
  return MakeUint128(Uint128High64(lhs) ^ Uint128High64(rhs),
                     Uint128Low64(lhs) ^ Uint128Low64(rhs));
#endif
}

inline uint128& uint128::operator|=(uint128 other) {
  *this = *this | other;
  return *this;
}

inline uint128& uint128::operator&=(uint128 other) {
  *this = *this & other;
  return *this;
}

inline uint128& uint128::operator^=(uint128 other) {
  *this = *this ^ other;
  return *this;
}

// Arithmetic operators.

constexpr uint128 operator<<(uint128 lhs, int amount) {
#ifdef ABSL_HAVE_INTRINSIC_INT128
  return static_cast<unsigned __int128>(lhs) << amount;
#else
  // uint64_t shifts of >= 64 are undefined, so we will need some
  // special-casing.
  return amount >= 64  ? MakeUint128(Uint128Low64(lhs) << (amount - 64), 0)
         : amount == 0 ? lhs
                       : MakeUint128((Uint128High64(lhs) << amount) |
                                         (Uint128Low64(lhs) >> (64 - amount)),
                                     Uint128Low64(lhs) << amount);
#endif
}

constexpr uint128 operator>>(uint128 lhs, int amount) {
#ifdef ABSL_HAVE_INTRINSIC_INT128
  return static_cast<unsigned __int128>(lhs) >> amount;
#else
  // uint64_t shifts of >= 64 are undefined, so we will need some
  // special-casing.
  return amount >= 64  ? MakeUint128(0, Uint128High64(lhs) >> (amount - 64))
         : amount == 0 ? lhs
                       : MakeUint128(Uint128High64(lhs) >> amount,
                                     (Uint128Low64(lhs) >> amount) |
                                         (Uint128High64(lhs) << (64 - amount)));
#endif
}

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
namespace int128_internal {
constexpr uint128 AddResult(uint128 result, uint128 lhs) {
  // check for carry
  return (Uint128Low64(result) < Uint128Low64(lhs))
             ? MakeUint128(Uint128High64(result) + 1, Uint128Low64(result))
             : result;
}
}  // namespace int128_internal
#endif

constexpr uint128 operator+(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) +
         static_cast<unsigned __int128>(rhs);
#else
  return int128_internal::AddResult(
      MakeUint128(Uint128High64(lhs) + Uint128High64(rhs),
                  Uint128Low64(lhs) + Uint128Low64(rhs)),
      lhs);
#endif
}

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
namespace int128_internal {
constexpr uint128 SubstructResult(uint128 result, uint128 lhs, uint128 rhs) {
  // check for carry
  return (Uint128Low64(lhs) < Uint128Low64(rhs))
             ? MakeUint128(Uint128High64(result) - 1, Uint128Low64(result))
             : result;
}
}  // namespace int128_internal
#endif

constexpr uint128 operator-(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  return static_cast<unsigned __int128>(lhs) -
         static_cast<unsigned __int128>(rhs);
#else
  return int128_internal::SubstructResult(
      MakeUint128(Uint128High64(lhs) - Uint128High64(rhs),
                  Uint128Low64(lhs) - Uint128Low64(rhs)),
      lhs, rhs);
#endif
}

inline uint128 operator*(uint128 lhs, uint128 rhs) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  // TODO(strel) Remove once alignment issues are resolved and unsigned __int128
  // can be used for uint128 storage.
  return static_cast<unsigned __int128>(lhs) *
         static_cast<unsigned __int128>(rhs);
#elif defined(_MSC_VER) && defined(_M_X64) && !defined(_M_ARM64EC)
  uint64_t carry;
  uint64_t low = _umul128(Uint128Low64(lhs), Uint128Low64(rhs), &carry);
  return MakeUint128(Uint128Low64(lhs) * Uint128High64(rhs) +
                         Uint128High64(lhs) * Uint128Low64(rhs) + carry,
                     low);
#else   // ABSL_HAVE_INTRINSIC128
  uint64_t a32 = Uint128Low64(lhs) >> 32;
  uint64_t a00 = Uint128Low64(lhs) & 0xffffffff;
  uint64_t b32 = Uint128Low64(rhs) >> 32;
  uint64_t b00 = Uint128Low64(rhs) & 0xffffffff;
  uint128 result =
      MakeUint128(Uint128High64(lhs) * Uint128Low64(rhs) +
                      Uint128Low64(lhs) * Uint128High64(rhs) + a32 * b32,
                  a00 * b00);
  result += uint128(a32 * b00) << 32;
  result += uint128(a00 * b32) << 32;
  return result;
#endif  // ABSL_HAVE_INTRINSIC128
}

#if defined(ABSL_HAVE_INTRINSIC_INT128)
inline uint128 operator/(uint128 lhs, uint128 rhs) {
  return static_cast<unsigned __int128>(lhs) /
         static_cast<unsigned __int128>(rhs);
}

inline uint128 operator%(uint128 lhs, uint128 rhs) {
  return static_cast<unsigned __int128>(lhs) %
         static_cast<unsigned __int128>(rhs);
}
#endif

// Increment/decrement operators.

inline uint128 uint128::operator++(int) {
  uint128 tmp(*this);
  *this += 1;
  return tmp;
}

inline uint128 uint128::operator--(int) {
  uint128 tmp(*this);
  *this -= 1;
  return tmp;
}

inline uint128& uint128::operator++() {
  *this += 1;
  return *this;
}

inline uint128& uint128::operator--() {
  *this -= 1;
  return *this;
}

constexpr int128 MakeInt128(int64_t high, uint64_t low) {
  return int128(high, low);
}

// Assignment from integer types.
inline int128& int128::operator=(int v) { return *this = int128(v); }

inline int128& int128::operator=(unsigned int v) { return *this = int128(v); }

inline int128& int128::operator=(long v) {  // NOLINT(runtime/int)
  return *this = int128(v);
}

// NOLINTNEXTLINE(runtime/int)
inline int128& int128::operator=(unsigned long v) { return *this = int128(v); }

// NOLINTNEXTLINE(runtime/int)
inline int128& int128::operator=(long long v) { return *this = int128(v); }

// NOLINTNEXTLINE(runtime/int)
inline int128& int128::operator=(unsigned long long v) {
  return *this = int128(v);
}

// Arithmetic operators.
constexpr int128 operator-(int128 v);
constexpr int128 operator+(int128 lhs, int128 rhs);
constexpr int128 operator-(int128 lhs, int128 rhs);
int128 operator*(int128 lhs, int128 rhs);
int128 operator/(int128 lhs, int128 rhs);
int128 operator%(int128 lhs, int128 rhs);
constexpr int128 operator|(int128 lhs, int128 rhs);
constexpr int128 operator&(int128 lhs, int128 rhs);
constexpr int128 operator^(int128 lhs, int128 rhs);
constexpr int128 operator<<(int128 lhs, int amount);
constexpr int128 operator>>(int128 lhs, int amount);

inline int128& int128::operator+=(int128 other) {
  *this = *this + other;
  return *this;
}

inline int128& int128::operator-=(int128 other) {
  *this = *this - other;
  return *this;
}

inline int128& int128::operator*=(int128 other) {
  *this = *this * other;
  return *this;
}

inline int128& int128::operator/=(int128 other) {
  *this = *this / other;
  return *this;
}

inline int128& int128::operator%=(int128 other) {
  *this = *this % other;
  return *this;
}

inline int128& int128::operator|=(int128 other) {
  *this = *this | other;
  return *this;
}

inline int128& int128::operator&=(int128 other) {
  *this = *this & other;
  return *this;
}

inline int128& int128::operator^=(int128 other) {
  *this = *this ^ other;
  return *this;
}

inline int128& int128::operator<<=(int amount) {
  *this = *this << amount;
  return *this;
}

inline int128& int128::operator>>=(int amount) {
  *this = *this >> amount;
  return *this;
}

// Forward declaration for comparison operators.
constexpr bool operator!=(int128 lhs, int128 rhs);

namespace int128_internal {

// Casts from unsigned to signed while preserving the underlying binary
// representation.
constexpr int64_t BitCastToSigned(uint64_t v) {
  // Casting an unsigned integer to a signed integer of the same
  // width is implementation defined behavior if the source value would not fit
  // in the destination type. We step around it with a roundtrip bitwise not
  // operation to make sure this function remains constexpr. Clang, GCC, and
  // MSVC optimize this to a no-op on x86-64.
  return v & (uint64_t{1} << 63) ? ~static_cast<int64_t>(~v)
                                 : static_cast<int64_t>(v);
}

}  // namespace int128_internal

#if defined(ABSL_HAVE_INTRINSIC_INT128)
#include "absl/numeric/int128_have_intrinsic.inc"  // IWYU pragma: export
#else  // ABSL_HAVE_INTRINSIC_INT128
#include "absl/numeric/int128_no_intrinsic.inc"  // IWYU pragma: export
#endif  // ABSL_HAVE_INTRINSIC_INT128

ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_INTERNAL_WCHAR_T

#endif  // ABSL_NUMERIC_INT128_H_
