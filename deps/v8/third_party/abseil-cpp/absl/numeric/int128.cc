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

#include "absl/numeric/int128.h"

#include <stddef.h>

#include <cassert>
#include <iomanip>
#include <ostream>  // NOLINT(readability/streams)
#include <sstream>
#include <string>
#include <type_traits>

#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

// Returns the 0-based position of the last set bit (i.e., most significant bit)
// in the given uint128. The argument is not 0.
//
// For example:
//   Given: 5 (decimal) == 101 (binary)
//   Returns: 2
inline ABSL_ATTRIBUTE_ALWAYS_INLINE int Fls128(uint128 n) {
  if (uint64_t hi = Uint128High64(n)) {
    ABSL_ASSUME(hi != 0);
    return 127 - countl_zero(hi);
  }
  const uint64_t low = Uint128Low64(n);
  ABSL_ASSUME(low != 0);
  return 63 - countl_zero(low);
}

// Long division/modulo for uint128 implemented using the shift-subtract
// division algorithm adapted from:
// https://stackoverflow.com/questions/5386377/division-without-using
inline void DivModImpl(uint128 dividend, uint128 divisor, uint128* quotient_ret,
                       uint128* remainder_ret) {
  assert(divisor != 0);

  if (divisor > dividend) {
    *quotient_ret = 0;
    *remainder_ret = dividend;
    return;
  }

  if (divisor == dividend) {
    *quotient_ret = 1;
    *remainder_ret = 0;
    return;
  }

  uint128 denominator = divisor;
  uint128 quotient = 0;

  // Left aligns the MSB of the denominator and the dividend.
  const int shift = Fls128(dividend) - Fls128(denominator);
  denominator <<= shift;

  // Uses shift-subtract algorithm to divide dividend by denominator. The
  // remainder will be left in dividend.
  for (int i = 0; i <= shift; ++i) {
    quotient <<= 1;
    if (dividend >= denominator) {
      dividend -= denominator;
      quotient |= 1;
    }
    denominator >>= 1;
  }

  *quotient_ret = quotient;
  *remainder_ret = dividend;
}

template <typename T>
uint128 MakeUint128FromFloat(T v) {
  static_assert(std::is_floating_point<T>::value, "");

  // Rounding behavior is towards zero, same as for built-in types.

  // Undefined behavior if v is NaN or cannot fit into uint128.
  assert(std::isfinite(v) && v > -1 &&
         (std::numeric_limits<T>::max_exponent <= 128 ||
          v < std::ldexp(static_cast<T>(1), 128)));

  if (v >= std::ldexp(static_cast<T>(1), 64)) {
    uint64_t hi = static_cast<uint64_t>(std::ldexp(v, -64));
    uint64_t lo = static_cast<uint64_t>(v - std::ldexp(static_cast<T>(hi), 64));
    return MakeUint128(hi, lo);
  }

  return MakeUint128(0, static_cast<uint64_t>(v));
}

#if defined(__clang__) && (__clang_major__ < 9) && !defined(__SSE3__)
// Workaround for clang bug: https://bugs.llvm.org/show_bug.cgi?id=38289
// Casting from long double to uint64_t is miscompiled and drops bits.
// It is more work, so only use when we need the workaround.
uint128 MakeUint128FromFloat(long double v) {
  // Go 50 bits at a time, that fits in a double
  static_assert(std::numeric_limits<double>::digits >= 50, "");
  static_assert(std::numeric_limits<long double>::digits <= 150, "");
  // Undefined behavior if v is not finite or cannot fit into uint128.
  assert(std::isfinite(v) && v > -1 && v < std::ldexp(1.0L, 128));

  v = std::ldexp(v, -100);
  uint64_t w0 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  v = std::ldexp(v - static_cast<double>(w0), 50);
  uint64_t w1 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  v = std::ldexp(v - static_cast<double>(w1), 50);
  uint64_t w2 = static_cast<uint64_t>(static_cast<double>(std::trunc(v)));
  return (static_cast<uint128>(w0) << 100) | (static_cast<uint128>(w1) << 50) |
         static_cast<uint128>(w2);
}
#endif  // __clang__ && (__clang_major__ < 9) && !__SSE3__
}  // namespace

uint128::uint128(float v) : uint128(MakeUint128FromFloat(v)) {}
uint128::uint128(double v) : uint128(MakeUint128FromFloat(v)) {}
uint128::uint128(long double v) : uint128(MakeUint128FromFloat(v)) {}

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
uint128 operator/(uint128 lhs, uint128 rhs) {
  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(lhs, rhs, &quotient, &remainder);
  return quotient;
}

uint128 operator%(uint128 lhs, uint128 rhs) {
  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(lhs, rhs, &quotient, &remainder);
  return remainder;
}
#endif  // !defined(ABSL_HAVE_INTRINSIC_INT128)

namespace {

std::string Uint128ToFormattedString(uint128 v, std::ios_base::fmtflags flags) {
  // Select a divisor which is the largest power of the base < 2^64.
  uint128 div;
  int div_base_log;
  switch (flags & std::ios::basefield) {
    case std::ios::hex:
      div = 0x1000000000000000;  // 16^15
      div_base_log = 15;
      break;
    case std::ios::oct:
      div = 01000000000000000000000;  // 8^21
      div_base_log = 21;
      break;
    default:  // std::ios::dec
      div = 10000000000000000000u;  // 10^19
      div_base_log = 19;
      break;
  }

  // Now piece together the uint128 representation from three chunks of the
  // original value, each less than "div" and therefore representable as a
  // uint64_t.
  std::ostringstream os;
  std::ios_base::fmtflags copy_mask =
      std::ios::basefield | std::ios::showbase | std::ios::uppercase;
  os.setf(flags & copy_mask, copy_mask);
  uint128 high = v;
  uint128 low;
  DivModImpl(high, div, &high, &low);
  uint128 mid;
  DivModImpl(high, div, &high, &mid);
  if (Uint128Low64(high) != 0) {
    os << Uint128Low64(high);
    os << std::noshowbase << std::setfill('0') << std::setw(div_base_log);
    os << Uint128Low64(mid);
    os << std::setw(div_base_log);
  } else if (Uint128Low64(mid) != 0) {
    os << Uint128Low64(mid);
    os << std::noshowbase << std::setfill('0') << std::setw(div_base_log);
  }
  os << Uint128Low64(low);
  return os.str();
}

}  // namespace

std::string uint128::ToString() const {
  return Uint128ToFormattedString(*this, std::ios_base::dec);
}

std::ostream& operator<<(std::ostream& os, uint128 v) {
  std::ios_base::fmtflags flags = os.flags();
  std::string rep = Uint128ToFormattedString(v, flags);

  // Add the requisite padding.
  std::streamsize width = os.width(0);
  if (static_cast<size_t>(width) > rep.size()) {
    const size_t count = static_cast<size_t>(width) - rep.size();
    std::ios::fmtflags adjustfield = flags & std::ios::adjustfield;
    if (adjustfield == std::ios::left) {
      rep.append(count, os.fill());
    } else if (adjustfield == std::ios::internal &&
               (flags & std::ios::showbase) &&
               (flags & std::ios::basefield) == std::ios::hex && v != 0) {
      rep.insert(size_t{2}, count, os.fill());
    } else {
      rep.insert(size_t{0}, count, os.fill());
    }
  }

  return os << rep;
}

namespace {

uint128 UnsignedAbsoluteValue(int128 v) {
  // Cast to uint128 before possibly negating because -Int128Min() is undefined.
  return Int128High64(v) < 0 ? -uint128(v) : uint128(v);
}

}  // namespace

#if !defined(ABSL_HAVE_INTRINSIC_INT128)
namespace {

template <typename T>
int128 MakeInt128FromFloat(T v) {
  // Conversion when v is NaN or cannot fit into int128 would be undefined
  // behavior if using an intrinsic 128-bit integer.
  assert(std::isfinite(v) && (std::numeric_limits<T>::max_exponent <= 127 ||
                              (v >= -std::ldexp(static_cast<T>(1), 127) &&
                               v < std::ldexp(static_cast<T>(1), 127))));

  // We must convert the absolute value and then negate as needed, because
  // floating point types are typically sign-magnitude. Otherwise, the
  // difference between the high and low 64 bits when interpreted as two's
  // complement overwhelms the precision of the mantissa.
  uint128 result = v < 0 ? -MakeUint128FromFloat(-v) : MakeUint128FromFloat(v);
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(result)),
                    Uint128Low64(result));
}

}  // namespace

int128::int128(float v) : int128(MakeInt128FromFloat(v)) {}
int128::int128(double v) : int128(MakeInt128FromFloat(v)) {}
int128::int128(long double v) : int128(MakeInt128FromFloat(v)) {}

int128 operator/(int128 lhs, int128 rhs) {
  assert(lhs != Int128Min() || rhs != -1);  // UB on two's complement.

  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(UnsignedAbsoluteValue(lhs), UnsignedAbsoluteValue(rhs),
             &quotient, &remainder);
  if ((Int128High64(lhs) < 0) != (Int128High64(rhs) < 0)) quotient = -quotient;
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(quotient)),
                    Uint128Low64(quotient));
}

int128 operator%(int128 lhs, int128 rhs) {
  assert(lhs != Int128Min() || rhs != -1);  // UB on two's complement.

  uint128 quotient = 0;
  uint128 remainder = 0;
  DivModImpl(UnsignedAbsoluteValue(lhs), UnsignedAbsoluteValue(rhs),
             &quotient, &remainder);
  if (Int128High64(lhs) < 0) remainder = -remainder;
  return MakeInt128(int128_internal::BitCastToSigned(Uint128High64(remainder)),
                    Uint128Low64(remainder));
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

std::string int128::ToString() const {
  std::string rep;
  if (Int128High64(*this) < 0) rep = "-";
  rep.append(Uint128ToFormattedString(UnsignedAbsoluteValue(*this),
                                      std::ios_base::dec));
  return rep;
}

std::ostream& operator<<(std::ostream& os, int128 v) {
  std::ios_base::fmtflags flags = os.flags();
  std::string rep;

  // Add the sign if needed.
  bool print_as_decimal =
      (flags & std::ios::basefield) == std::ios::dec ||
      (flags & std::ios::basefield) == std::ios_base::fmtflags();
  if (print_as_decimal) {
    if (Int128High64(v) < 0) {
      rep = "-";
    } else if (flags & std::ios::showpos) {
      rep = "+";
    }
  }

  rep.append(Uint128ToFormattedString(
      print_as_decimal ? UnsignedAbsoluteValue(v) : uint128(v), os.flags()));

  // Add the requisite padding.
  std::streamsize width = os.width(0);
  if (static_cast<size_t>(width) > rep.size()) {
    const size_t count = static_cast<size_t>(width) - rep.size();
    switch (flags & std::ios::adjustfield) {
      case std::ios::left:
        rep.append(count, os.fill());
        break;
      case std::ios::internal:
        if (print_as_decimal && (rep[0] == '+' || rep[0] == '-')) {
          rep.insert(size_t{1}, count, os.fill());
        } else if ((flags & std::ios::basefield) == std::ios::hex &&
                   (flags & std::ios::showbase) && v != 0) {
          rep.insert(size_t{2}, count, os.fill());
        } else {
          rep.insert(size_t{0}, count, os.fill());
        }
        break;
      default:  // std::ios::right
        rep.insert(size_t{0}, count, os.fill());
        break;
    }
  }

  return os << rep;
}

ABSL_NAMESPACE_END
}  // namespace absl

#ifdef ABSL_INTERNAL_NEED_REDUNDANT_CONSTEXPR_DECL
namespace std {
constexpr bool numeric_limits<absl::uint128>::is_specialized;
constexpr bool numeric_limits<absl::uint128>::is_signed;
constexpr bool numeric_limits<absl::uint128>::is_integer;
constexpr bool numeric_limits<absl::uint128>::is_exact;
constexpr bool numeric_limits<absl::uint128>::has_infinity;
constexpr bool numeric_limits<absl::uint128>::has_quiet_NaN;
constexpr bool numeric_limits<absl::uint128>::has_signaling_NaN;
constexpr float_denorm_style numeric_limits<absl::uint128>::has_denorm;
constexpr bool numeric_limits<absl::uint128>::has_denorm_loss;
constexpr float_round_style numeric_limits<absl::uint128>::round_style;
constexpr bool numeric_limits<absl::uint128>::is_iec559;
constexpr bool numeric_limits<absl::uint128>::is_bounded;
constexpr bool numeric_limits<absl::uint128>::is_modulo;
constexpr int numeric_limits<absl::uint128>::digits;
constexpr int numeric_limits<absl::uint128>::digits10;
constexpr int numeric_limits<absl::uint128>::max_digits10;
constexpr int numeric_limits<absl::uint128>::radix;
constexpr int numeric_limits<absl::uint128>::min_exponent;
constexpr int numeric_limits<absl::uint128>::min_exponent10;
constexpr int numeric_limits<absl::uint128>::max_exponent;
constexpr int numeric_limits<absl::uint128>::max_exponent10;
constexpr bool numeric_limits<absl::uint128>::traps;
constexpr bool numeric_limits<absl::uint128>::tinyness_before;

constexpr bool numeric_limits<absl::int128>::is_specialized;
constexpr bool numeric_limits<absl::int128>::is_signed;
constexpr bool numeric_limits<absl::int128>::is_integer;
constexpr bool numeric_limits<absl::int128>::is_exact;
constexpr bool numeric_limits<absl::int128>::has_infinity;
constexpr bool numeric_limits<absl::int128>::has_quiet_NaN;
constexpr bool numeric_limits<absl::int128>::has_signaling_NaN;
constexpr float_denorm_style numeric_limits<absl::int128>::has_denorm;
constexpr bool numeric_limits<absl::int128>::has_denorm_loss;
constexpr float_round_style numeric_limits<absl::int128>::round_style;
constexpr bool numeric_limits<absl::int128>::is_iec559;
constexpr bool numeric_limits<absl::int128>::is_bounded;
constexpr bool numeric_limits<absl::int128>::is_modulo;
constexpr int numeric_limits<absl::int128>::digits;
constexpr int numeric_limits<absl::int128>::digits10;
constexpr int numeric_limits<absl::int128>::max_digits10;
constexpr int numeric_limits<absl::int128>::radix;
constexpr int numeric_limits<absl::int128>::min_exponent;
constexpr int numeric_limits<absl::int128>::min_exponent10;
constexpr int numeric_limits<absl::int128>::max_exponent;
constexpr int numeric_limits<absl::int128>::max_exponent10;
constexpr bool numeric_limits<absl::int128>::traps;
constexpr bool numeric_limits<absl::int128>::tinyness_before;
}  // namespace std
#endif
