// Copyright 2020 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: bits.h
// -----------------------------------------------------------------------------
//
// This file contains implementations of C++20's bitwise math functions, as
// defined by:
//
// P0553R4:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p0553r4.html
// P0556R3:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0556r3.html
// P1355R2:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2019/p1355r2.html
// P1956R1:
//  http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p1956r1.pdf
//
// When using a standard library that implements these functions, we use the
// standard library's implementation.

#ifndef ABSL_NUMERIC_BITS_H_
#define ABSL_NUMERIC_BITS_H_

#include <cstdint>
#include <limits>
#include <type_traits>

#include "absl/base/config.h"

#if ABSL_INTERNAL_CPLUSPLUS_LANG >= 202002L
#include <bit>
#endif

#include "absl/base/attributes.h"
#include "absl/numeric/internal/bits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// https://github.com/llvm/llvm-project/issues/64544
// libc++ had the wrong signature for std::rotl and std::rotr
// prior to libc++ 18.0.
//
#if (defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L) &&     \
    (!defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 180000)
using std::rotl;
using std::rotr;

#else

// Rotating functions
template <class T>
ABSL_MUST_USE_RESULT constexpr
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    rotl(T x, int s) noexcept {
  return numeric_internal::RotateLeft(x, s);
}

template <class T>
ABSL_MUST_USE_RESULT constexpr
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    rotr(T x, int s) noexcept {
  return numeric_internal::RotateRight(x, s);
}

#endif

// https://github.com/llvm/llvm-project/issues/64544
// libc++ had the wrong signature for std::rotl and std::rotr
// prior to libc++ 18.0.
//
#if (defined(__cpp_lib_bitops) && __cpp_lib_bitops >= 201907L)

using std::countl_one;
using std::countl_zero;
using std::countr_one;
using std::countr_zero;
using std::popcount;

#else

// Counting functions
//
// While these functions are typically constexpr, on some platforms, they may
// not be marked as constexpr due to constraints of the compiler/available
// intrinsics.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    countl_zero(T x) noexcept {
  return numeric_internal::CountLeadingZeroes(x);
}

template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    countl_one(T x) noexcept {
  // Avoid integer promotion to a wider type
  return countl_zero(static_cast<T>(~x));
}

template <class T>
ABSL_INTERNAL_CONSTEXPR_CTZ inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    countr_zero(T x) noexcept {
  return numeric_internal::CountTrailingZeroes(x);
}

template <class T>
ABSL_INTERNAL_CONSTEXPR_CTZ inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    countr_one(T x) noexcept {
  // Avoid integer promotion to a wider type
  return countr_zero(static_cast<T>(~x));
}

template <class T>
ABSL_INTERNAL_CONSTEXPR_POPCOUNT inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    popcount(T x) noexcept {
  return numeric_internal::Popcount(x);
}

#endif

#if (defined(__cpp_lib_int_pow2) && __cpp_lib_int_pow2 >= 202002L)

using std::bit_ceil;
using std::bit_floor;
using std::bit_width;
using std::has_single_bit;

#else

// Returns: true if x is an integral power of two; false otherwise.
template <class T>
constexpr inline typename std::enable_if<std::is_unsigned<T>::value, bool>::type
has_single_bit(T x) noexcept {
  return x != 0 && (x & (x - 1)) == 0;
}

// Returns: If x == 0, 0; otherwise one plus the base-2 logarithm of x, with any
// fractional part discarded.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, int>::type
    bit_width(T x) noexcept {
  return std::numeric_limits<T>::digits - countl_zero(x);
}

// Returns: If x == 0, 0; otherwise the maximal value y such that
// has_single_bit(y) is true and y <= x.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    bit_floor(T x) noexcept {
  return x == 0 ? 0 : T{1} << (bit_width(x) - 1);
}

// Returns: N, where N is the smallest power of 2 greater than or equal to x.
//
// Preconditions: N is representable as a value of type T.
template <class T>
ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    bit_ceil(T x) {
  // If T is narrower than unsigned, T{1} << bit_width will be promoted.  We
  // want to force it to wraparound so that bit_ceil of an invalid value are not
  // core constant expressions.
  //
  // BitCeilNonPowerOf2 triggers an overflow in constexpr contexts if we would
  // undergo promotion to unsigned but not fit the result into T without
  // truncation.
  return has_single_bit(x) ? T{1} << (bit_width(x) - 1)
                           : numeric_internal::BitCeilNonPowerOf2(x);
}

#endif

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_NUMERIC_BITS_H_
