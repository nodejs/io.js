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

#ifndef ABSL_NUMERIC_INTERNAL_BITS_H_
#define ABSL_NUMERIC_INTERNAL_BITS_H_

#include <cstdint>
#include <limits>
#include <type_traits>

// Clang on Windows has __builtin_clzll; otherwise we need to use the
// windows intrinsic functions.
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#include "absl/base/attributes.h"
#include "absl/base/config.h"

#if defined(__GNUC__) && !defined(__clang__)
// GCC
#define ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(x) 1
#else
#define ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(x) ABSL_HAVE_BUILTIN(x)
#endif

#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_popcountl) && \
    ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_popcountll)
#define ABSL_INTERNAL_CONSTEXPR_POPCOUNT constexpr
#define ABSL_INTERNAL_HAS_CONSTEXPR_POPCOUNT 1
#else
#define ABSL_INTERNAL_CONSTEXPR_POPCOUNT
#define ABSL_INTERNAL_HAS_CONSTEXPR_POPCOUNT 0
#endif

#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_clz) && \
    ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_clzll)
#define ABSL_INTERNAL_CONSTEXPR_CLZ constexpr
#define ABSL_INTERNAL_HAS_CONSTEXPR_CLZ 1
#else
#define ABSL_INTERNAL_CONSTEXPR_CLZ
#define ABSL_INTERNAL_HAS_CONSTEXPR_CLZ 0
#endif

#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_ctz) && \
    ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_ctzll)
#define ABSL_INTERNAL_CONSTEXPR_CTZ constexpr
#define ABSL_INTERNAL_HAS_CONSTEXPR_CTZ 1
#else
#define ABSL_INTERNAL_CONSTEXPR_CTZ
#define ABSL_INTERNAL_HAS_CONSTEXPR_CTZ 0
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace numeric_internal {

constexpr bool IsPowerOf2(unsigned int x) noexcept {
  return x != 0 && (x & (x - 1)) == 0;
}

template <class T>
ABSL_MUST_USE_RESULT ABSL_ATTRIBUTE_ALWAYS_INLINE constexpr T RotateRight(
    T x, int s) noexcept {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  static_assert(IsPowerOf2(std::numeric_limits<T>::digits),
                "T must have a power-of-2 size");

  return static_cast<T>(x >> (s & (std::numeric_limits<T>::digits - 1))) |
         static_cast<T>(x << ((-s) & (std::numeric_limits<T>::digits - 1)));
}

template <class T>
ABSL_MUST_USE_RESULT ABSL_ATTRIBUTE_ALWAYS_INLINE constexpr T RotateLeft(
    T x, int s) noexcept {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  static_assert(IsPowerOf2(std::numeric_limits<T>::digits),
                "T must have a power-of-2 size");

  return static_cast<T>(x << (s & (std::numeric_limits<T>::digits - 1))) |
         static_cast<T>(x >> ((-s) & (std::numeric_limits<T>::digits - 1)));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_POPCOUNT inline int
Popcount32(uint32_t x) noexcept {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_popcount)
  static_assert(sizeof(unsigned int) == sizeof(x),
                "__builtin_popcount does not take 32-bit arg");
  return __builtin_popcount(x);
#else
  x -= ((x >> 1) & 0x55555555);
  x = ((x >> 2) & 0x33333333) + (x & 0x33333333);
  return static_cast<int>((((x + (x >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24);
#endif
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_POPCOUNT inline int
Popcount64(uint64_t x) noexcept {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_popcountll)
  static_assert(sizeof(unsigned long long) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_popcount does not take 64-bit arg");
  return __builtin_popcountll(x);
#else
  x -= (x >> 1) & 0x5555555555555555ULL;
  x = ((x >> 2) & 0x3333333333333333ULL) + (x & 0x3333333333333333ULL);
  return static_cast<int>(
      (((x + (x >> 4)) & 0xF0F0F0F0F0F0F0FULL) * 0x101010101010101ULL) >> 56);
#endif
}

template <class T>
ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_POPCOUNT inline int
Popcount(T x) noexcept {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  static_assert(IsPowerOf2(std::numeric_limits<T>::digits),
                "T must have a power-of-2 size");
  static_assert(sizeof(x) <= sizeof(uint64_t), "T is too large");
  return sizeof(x) <= sizeof(uint32_t) ? Popcount32(x) : Popcount64(x);
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline int
CountLeadingZeroes32(uint32_t x) {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_clz)
  // Use __builtin_clz, which uses the following instructions:
  //  x86: bsr, lzcnt
  //  ARM64: clz
  //  PPC: cntlzd

  static_assert(sizeof(unsigned int) == sizeof(x),
                "__builtin_clz does not take 32-bit arg");
  // Handle 0 as a special case because __builtin_clz(0) is undefined.
  return x == 0 ? 32 : __builtin_clz(x);
#elif defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (_BitScanReverse(&result, x)) {
    return 31 - result;
  }
  return 32;
#else
  int zeroes = 28;
  if (x >> 16) {
    zeroes -= 16;
    x >>= 16;
  }
  if (x >> 8) {
    zeroes -= 8;
    x >>= 8;
  }
  if (x >> 4) {
    zeroes -= 4;
    x >>= 4;
  }
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[x] + zeroes;
#endif
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline int
CountLeadingZeroes16(uint16_t x) {
#if ABSL_HAVE_BUILTIN(__builtin_clzs)
  static_assert(sizeof(unsigned short) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_clzs does not take 16-bit arg");
  return x == 0 ? 16 : __builtin_clzs(x);
#else
  return CountLeadingZeroes32(x) - 16;
#endif
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline int
CountLeadingZeroes64(uint64_t x) {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_clzll)
  // Use __builtin_clzll, which uses the following instructions:
  //  x86: bsr, lzcnt
  //  ARM64: clz
  //  PPC: cntlzd
  static_assert(sizeof(unsigned long long) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_clzll does not take 64-bit arg");

  // Handle 0 as a special case because __builtin_clzll(0) is undefined.
  return x == 0 ? 64 : __builtin_clzll(x);
#elif defined(_MSC_VER) && !defined(__clang__) && \
    (defined(_M_X64) || defined(_M_ARM64))
  // MSVC does not have __buitin_clzll. Use _BitScanReverse64.
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (_BitScanReverse64(&result, x)) {
    return 63 - result;
  }
  return 64;
#elif defined(_MSC_VER) && !defined(__clang__)
  // MSVC does not have __buitin_clzll. Compose two calls to _BitScanReverse
  unsigned long result = 0;  // NOLINT(runtime/int)
  if ((x >> 32) &&
      _BitScanReverse(&result, static_cast<unsigned long>(x >> 32))) {
    return 31 - result;
  }
  if (_BitScanReverse(&result, static_cast<unsigned long>(x))) {
    return 63 - result;
  }
  return 64;
#else
  int zeroes = 60;
  if (x >> 32) {
    zeroes -= 32;
    x >>= 32;
  }
  if (x >> 16) {
    zeroes -= 16;
    x >>= 16;
  }
  if (x >> 8) {
    zeroes -= 8;
    x >>= 8;
  }
  if (x >> 4) {
    zeroes -= 4;
    x >>= 4;
  }
  return "\4\3\2\2\1\1\1\1\0\0\0\0\0\0\0"[x] + zeroes;
#endif
}

template <typename T>
ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline int
CountLeadingZeroes(T x) {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  static_assert(IsPowerOf2(std::numeric_limits<T>::digits),
                "T must have a power-of-2 size");
  static_assert(sizeof(T) <= sizeof(uint64_t), "T too large");
  return sizeof(T) <= sizeof(uint16_t)
             ? CountLeadingZeroes16(static_cast<uint16_t>(x)) -
                   (std::numeric_limits<uint16_t>::digits -
                    std::numeric_limits<T>::digits)
             : (sizeof(T) <= sizeof(uint32_t)
                    ? CountLeadingZeroes32(static_cast<uint32_t>(x)) -
                          (std::numeric_limits<uint32_t>::digits -
                           std::numeric_limits<T>::digits)
                    : CountLeadingZeroes64(x));
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CTZ inline int
CountTrailingZeroesNonzero32(uint32_t x) {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_ctz)
  static_assert(sizeof(unsigned int) == sizeof(x),
                "__builtin_ctz does not take 32-bit arg");
  return __builtin_ctz(x);
#elif defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanForward(&result, x);
  return result;
#else
  int c = 31;
  x &= ~x + 1;
  if (x & 0x0000FFFF) c -= 16;
  if (x & 0x00FF00FF) c -= 8;
  if (x & 0x0F0F0F0F) c -= 4;
  if (x & 0x33333333) c -= 2;
  if (x & 0x55555555) c -= 1;
  return c;
#endif
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CTZ inline int
CountTrailingZeroesNonzero64(uint64_t x) {
#if ABSL_NUMERIC_INTERNAL_HAVE_BUILTIN_OR_GCC(__builtin_ctzll)
  static_assert(sizeof(unsigned long long) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_ctzll does not take 64-bit arg");
  return __builtin_ctzll(x);
#elif defined(_MSC_VER) && !defined(__clang__) && \
    (defined(_M_X64) || defined(_M_ARM64))
  unsigned long result = 0;  // NOLINT(runtime/int)
  _BitScanForward64(&result, x);
  return result;
#elif defined(_MSC_VER) && !defined(__clang__)
  unsigned long result = 0;  // NOLINT(runtime/int)
  if (static_cast<uint32_t>(x) == 0) {
    _BitScanForward(&result, static_cast<unsigned long>(x >> 32));
    return result + 32;
  }
  _BitScanForward(&result, static_cast<unsigned long>(x));
  return result;
#else
  int c = 63;
  x &= ~x + 1;
  if (x & 0x00000000FFFFFFFF) c -= 32;
  if (x & 0x0000FFFF0000FFFF) c -= 16;
  if (x & 0x00FF00FF00FF00FF) c -= 8;
  if (x & 0x0F0F0F0F0F0F0F0F) c -= 4;
  if (x & 0x3333333333333333) c -= 2;
  if (x & 0x5555555555555555) c -= 1;
  return c;
#endif
}

ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CTZ inline int
CountTrailingZeroesNonzero16(uint16_t x) {
#if ABSL_HAVE_BUILTIN(__builtin_ctzs)
  static_assert(sizeof(unsigned short) == sizeof(x),  // NOLINT(runtime/int)
                "__builtin_ctzs does not take 16-bit arg");
  return __builtin_ctzs(x);
#else
  return CountTrailingZeroesNonzero32(x);
#endif
}

template <class T>
ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CTZ inline int
CountTrailingZeroes(T x) noexcept {
  static_assert(std::is_unsigned<T>::value, "T must be unsigned");
  static_assert(IsPowerOf2(std::numeric_limits<T>::digits),
                "T must have a power-of-2 size");
  static_assert(sizeof(T) <= sizeof(uint64_t), "T too large");
  return x == 0 ? std::numeric_limits<T>::digits
                : (sizeof(T) <= sizeof(uint16_t)
                       ? CountTrailingZeroesNonzero16(static_cast<uint16_t>(x))
                       : (sizeof(T) <= sizeof(uint32_t)
                              ? CountTrailingZeroesNonzero32(
                                    static_cast<uint32_t>(x))
                              : CountTrailingZeroesNonzero64(x)));
}

// If T is narrower than unsigned, T{1} << bit_width will be promoted.  We
// want to force it to wraparound so that bit_ceil of an invalid value are not
// core constant expressions.
template <class T>
ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    BitCeilPromotionHelper(T x, T promotion) {
  return (T{1} << (x + promotion)) >> promotion;
}

template <class T>
ABSL_ATTRIBUTE_ALWAYS_INLINE ABSL_INTERNAL_CONSTEXPR_CLZ inline
    typename std::enable_if<std::is_unsigned<T>::value, T>::type
    BitCeilNonPowerOf2(T x) {
  // If T is narrower than unsigned, it undergoes promotion to unsigned when we
  // shift.  We calculate the number of bits added by the wider type.
  return BitCeilPromotionHelper(
      static_cast<T>(std::numeric_limits<T>::digits - CountLeadingZeroes(x)),
      T{sizeof(T) >= sizeof(unsigned) ? 0
                                      : std::numeric_limits<unsigned>::digits -
                                            std::numeric_limits<T>::digits});
}

}  // namespace numeric_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_NUMERIC_INTERNAL_BITS_H_
