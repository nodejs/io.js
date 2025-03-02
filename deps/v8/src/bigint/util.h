// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// "Generic" helper functions (not specific to BigInts).

#include <stdint.h>

#include <type_traits>

#ifdef _MSC_VER
#include <intrin.h>  // For _BitScanReverse.
#endif

#ifndef V8_BIGINT_UTIL_H_
#define V8_BIGINT_UTIL_H_

// Integer division, rounding up.
#define DIV_CEIL(x, y) (((x)-1) / (y) + 1)

namespace v8 {
namespace bigint {

// Rounds up x to a multiple of y.
inline constexpr int RoundUp(int x, int y) { return (x + y - 1) & -y; }

// Different environments disagree on how 64-bit uintptr_t and uint64_t are
// defined, so we have to use templates to be generic.
template <typename T>
constexpr int CountLeadingZeros(T value)
  requires(std::is_unsigned<T>::value && sizeof(T) == 8)
{
#if __GNUC__ || __clang__
  return value == 0 ? 64 : __builtin_clzll(value);
#elif _MSC_VER
  unsigned long index = 0;  // NOLINT(runtime/int). MSVC insists.
  return _BitScanReverse64(&index, value) ? 63 - index : 64;
#else
#error Unsupported compiler.
#endif
}

constexpr int CountLeadingZeros(uint32_t value) {
#if __GNUC__ || __clang__
  return value == 0 ? 32 : __builtin_clz(value);
#elif _MSC_VER
  unsigned long index = 0;  // NOLINT(runtime/int). MSVC insists.
  return _BitScanReverse(&index, value) ? 31 - index : 32;
#else
#error Unsupported compiler.
#endif
}

inline constexpr int CountTrailingZeros(uint32_t value) {
#if __GNUC__ || __clang__
  return value == 0 ? 32 : __builtin_ctz(value);
#elif _MSC_VER
  unsigned long index = 0;  // NOLINT(runtime/int).
  return _BitScanForward(&index, value) ? index : 32;
#else
#error Unsupported compiler.
#endif
}

inline constexpr int BitLength(int n) {
  return 32 - CountLeadingZeros(static_cast<uint32_t>(n));
}

inline constexpr bool IsPowerOfTwo(int value) {
  return value > 0 && (value & (value - 1)) == 0;
}

}  // namespace bigint
}  // namespace v8

#endif  // V8_BIGINT_UTIL_H_
