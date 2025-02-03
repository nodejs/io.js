// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BITS_H_
#define V8_BASE_BITS_H_

#include <stdint.h>
#include <type_traits>

#include "src/base/base-export.h"
#include "src/base/macros.h"
#if V8_CC_MSVC
#include <intrin.h>
#endif
#if V8_OS_WIN32
#include "src/base/win32-headers.h"
#endif

namespace v8 {
namespace base {
namespace bits {

// CountPopulation(value) returns the number of bits set in |value|.
template <typename T>
constexpr inline
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountPopulation(T value) {
  static_assert(sizeof(T) <= 8);
#if V8_HAS_BUILTIN_POPCOUNT
  return sizeof(T) == 8 ? __builtin_popcountll(static_cast<uint64_t>(value))
                        : __builtin_popcount(static_cast<uint32_t>(value));
#else
  // Fall back to divide-and-conquer popcount (see "Hacker's Delight" by Henry
  // S. Warren, Jr.), chapter 5-1.
  constexpr uint64_t mask[] = {0x5555555555555555, 0x3333333333333333,
                               0x0f0f0f0f0f0f0f0f};
  // Start with 64 buckets of 1 bits, holding values from [0,1].
  value = ((value >> 1) & mask[0]) + (value & mask[0]);
  // Having 32 buckets of 2 bits, holding values from [0,2] now.
  value = ((value >> 2) & mask[1]) + (value & mask[1]);
  // Having 16 buckets of 4 bits, holding values from [0,4] now.
  value = ((value >> 4) & mask[2]) + (value & mask[2]);
  // Having 8 buckets of 8 bits, holding values from [0,8] now.
  // From this point on, the buckets are bigger than the number of bits
  // required to hold the values, and the buckets are bigger the maximum
  // result, so there's no need to mask value anymore, since there's no
  // more risk of overflow between buckets.
  if (sizeof(T) > 1) value = (value >> (sizeof(T) > 1 ? 8 : 0)) + value;
  // Having 4 buckets of 16 bits, holding values from [0,16] now.
  if (sizeof(T) > 2) value = (value >> (sizeof(T) > 2 ? 16 : 0)) + value;
  // Having 2 buckets of 32 bits, holding values from [0,32] now.
  if (sizeof(T) > 4) value = (value >> (sizeof(T) > 4 ? 32 : 0)) + value;
  // Having 1 buckets of 64 bits, holding values from [0,64] now.
  return static_cast<unsigned>(value & 0xff);
#endif
}

// ReverseBits(value) returns |value| in reverse bit order.
template <typename T>
T ReverseBits(T value) {
  static_assert((sizeof(value) == 1) || (sizeof(value) == 2) ||
                (sizeof(value) == 4) || (sizeof(value) == 8));
  T result = 0;
  for (unsigned i = 0; i < (sizeof(value) * 8); i++) {
    result = (result << 1) | (value & 1);
    value >>= 1;
  }
  return result;
}

// ReverseBytes(value) returns |value| in reverse byte order.
template <typename T>
T ReverseBytes(T value) {
  static_assert((sizeof(value) == 1) || (sizeof(value) == 2) ||
                (sizeof(value) == 4) || (sizeof(value) == 8));
  T result = 0;
  for (unsigned i = 0; i < sizeof(value); i++) {
    result = (result << 8) | (value & 0xff);
    value >>= 8;
  }
  return result;
}

template <class T>
inline constexpr std::make_unsigned_t<T> Unsigned(T value) {
  static_assert(std::is_signed_v<T>);
  return static_cast<std::make_unsigned_t<T>>(value);
}
template <class T>
inline constexpr std::make_signed_t<T> Signed(T value) {
  static_assert(std::is_unsigned_v<T>);
  return static_cast<std::make_signed_t<T>>(value);
}

// CountLeadingZeros(value) returns the number of zero bits following the most
// significant 1 bit in |value| if |value| is non-zero, otherwise it returns
// {sizeof(T) * 8}.
template <typename T, unsigned bits = sizeof(T) * 8>
inline constexpr
    typename std::enable_if<std::is_unsigned<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountLeadingZeros(T value) {
  static_assert(bits > 0, "invalid instantiation");
#if V8_HAS_BUILTIN_CLZ
  return value == 0
             ? bits
             : bits == 64
                   ? __builtin_clzll(static_cast<uint64_t>(value))
                   : __builtin_clz(static_cast<uint32_t>(value)) - (32 - bits);
#else
  // Binary search algorithm taken from "Hacker's Delight" (by Henry S. Warren,
  // Jr.), figures 5-11 and 5-12.
  if (bits == 1) return static_cast<unsigned>(value) ^ 1;
  T upper_half = value >> (bits / 2);
  T next_value = upper_half != 0 ? upper_half : value;
  unsigned add = upper_half != 0 ? 0 : bits / 2;
  constexpr unsigned next_bits = bits == 1 ? 1 : bits / 2;
  return CountLeadingZeros<T, next_bits>(next_value) + add;
#endif
}

inline constexpr unsigned CountLeadingZeros32(uint32_t value) {
  return CountLeadingZeros(value);
}
inline constexpr unsigned CountLeadingZeros64(uint64_t value) {
  return CountLeadingZeros(value);
}

// The number of leading zeros for a positive number,
// the number of leading ones for a negative number.
template <class T>
constexpr unsigned CountLeadingSignBits(T value) {
  static_assert(std::is_signed_v<T>);
  return value < 0 ? CountLeadingZeros(~Unsigned(value))
                   : CountLeadingZeros(Unsigned(value));
}

// CountTrailingZeros(value) returns the number of zero bits preceding the
// least significant 1 bit in |value| if |value| is non-zero, otherwise it
// returns {sizeof(T) * 8}.
// See CountTrailingZerosNonZero for an optimized version for the case that
// |value| is guaranteed to be non-zero.
template <typename T, unsigned bits = sizeof(T) * 8>
inline constexpr
    typename std::enable_if<std::is_integral<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountTrailingZeros(T value) {
#if V8_HAS_BUILTIN_CTZ
  return value == 0 ? bits
                    : bits == 64 ? __builtin_ctzll(static_cast<uint64_t>(value))
                                 : __builtin_ctz(static_cast<uint32_t>(value));
#else
  // Fall back to popcount (see "Hacker's Delight" by Henry S. Warren, Jr.),
  // chapter 5-4. On x64, since is faster than counting in a loop and faster
  // than doing binary search.
  using U = typename std::make_unsigned<T>::type;
  U u = value;
  return CountPopulation(static_cast<U>(~u & (u - 1u)));
#endif
}

inline constexpr unsigned CountTrailingZeros32(uint32_t value) {
  return CountTrailingZeros(value);
}
inline constexpr unsigned CountTrailingZeros64(uint64_t value) {
  return CountTrailingZeros(value);
}

// CountTrailingZerosNonZero(value) returns the number of zero bits preceding
// the least significant 1 bit in |value| if |value| is non-zero, otherwise the
// behavior is undefined.
// See CountTrailingZeros for an alternative version that allows |value| == 0.
template <typename T, unsigned bits = sizeof(T) * 8>
inline constexpr
    typename std::enable_if<std::is_integral<T>::value && sizeof(T) <= 8,
                            unsigned>::type
    CountTrailingZerosNonZero(T value) {
  DCHECK_NE(0, value);
#if V8_HAS_BUILTIN_CTZ
  return bits == 64 ? __builtin_ctzll(static_cast<uint64_t>(value))
                    : __builtin_ctz(static_cast<uint32_t>(value));
#else
  return CountTrailingZeros<T, bits>(value);
#endif
}

// Returns true iff |value| is a power of 2.
template <typename T,
          typename = typename std::enable_if<std::is_integral<T>::value ||
                                             std::is_enum<T>::value>::type>
constexpr inline bool IsPowerOfTwo(T value) {
  return value > 0 && (value & (value - 1)) == 0;
}

// Identical to {CountTrailingZeros}, but only works for powers of 2.
template <typename T,
          typename = typename std::enable_if<std::is_integral<T>::value>::type>
inline constexpr int WhichPowerOfTwo(T value) {
  DCHECK(IsPowerOfTwo(value));
#if V8_HAS_BUILTIN_CTZ
  static_assert(sizeof(T) <= 8);
  return sizeof(T) == 8 ? __builtin_ctzll(static_cast<uint64_t>(value))
                        : __builtin_ctz(static_cast<uint32_t>(value));
#else
  // Fall back to popcount (see "Hacker's Delight" by Henry S. Warren, Jr.),
  // chapter 5-4. On x64, since is faster than counting in a loop and faster
  // than doing binary search.
  using U = typename std::make_unsigned<T>::type;
  U u = value;
  return CountPopulation(static_cast<U>(u - 1));
#endif
}

// RoundUpToPowerOfTwo32(value) returns the smallest power of two which is
// greater than or equal to |value|. If you pass in a |value| that is already a
// power of two, it is returned as is. |value| must be less than or equal to
// 0x80000000u. Uses computation based on leading zeros if we have compiler
// support for that. Falls back to the implementation from "Hacker's Delight" by
// Henry S. Warren, Jr., figure 3-3, page 48, where the function is called clp2.
V8_BASE_EXPORT constexpr uint32_t RoundUpToPowerOfTwo32(uint32_t value) {
  DCHECK_LE(value, uint32_t{1} << 31);
  if (value) --value;
// Use computation based on leading zeros if we have compiler support for that.
#if V8_HAS_BUILTIN_CLZ || V8_CC_MSVC
  return 1u << (32 - CountLeadingZeros(value));
#else
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  return value + 1;
#endif
}
// Same for 64 bit integers. |value| must be <= 2^63
V8_BASE_EXPORT constexpr uint64_t RoundUpToPowerOfTwo64(uint64_t value) {
  DCHECK_LE(value, uint64_t{1} << 63);
  if (value) --value;
// Use computation based on leading zeros if we have compiler support for that.
#if V8_HAS_BUILTIN_CLZ
  return uint64_t{1} << (64 - CountLeadingZeros(value));
#else
  value |= value >> 1;
  value |= value >> 2;
  value |= value >> 4;
  value |= value >> 8;
  value |= value >> 16;
  value |= value >> 32;
  return value + 1;
#endif
}
// Same for size_t integers.
inline constexpr size_t RoundUpToPowerOfTwo(size_t value) {
  if (sizeof(size_t) == sizeof(uint64_t)) {
    return RoundUpToPowerOfTwo64(value);
  } else {
    // Without windows.h included this line triggers a truncation warning on
    // 64-bit builds. Presumably windows.h disables the relevant warning.
    return RoundUpToPowerOfTwo32(static_cast<uint32_t>(value));
  }
}

// RoundDownToPowerOfTwo32(value) returns the greatest power of two which is
// less than or equal to |value|. If you pass in a |value| that is already a
// power of two, it is returned as is.
inline uint32_t RoundDownToPowerOfTwo32(uint32_t value) {
  if (value > 0x80000000u) return 0x80000000u;
  uint32_t result = RoundUpToPowerOfTwo32(value);
  if (result > value) result >>= 1;
  return result;
}


// Precondition: 0 <= shift < 32
inline constexpr uint32_t RotateRight32(uint32_t value, uint32_t shift) {
  return (value >> shift) | (value << ((32 - shift) & 31));
}

// Precondition: 0 <= shift < 32
inline constexpr uint32_t RotateLeft32(uint32_t value, uint32_t shift) {
  return (value << shift) | (value >> ((32 - shift) & 31));
}

// Precondition: 0 <= shift < 64
inline constexpr uint64_t RotateRight64(uint64_t value, uint64_t shift) {
  return (value >> shift) | (value << ((64 - shift) & 63));
}

// Precondition: 0 <= shift < 64
inline constexpr uint64_t RotateLeft64(uint64_t value, uint64_t shift) {
  return (value << shift) | (value >> ((64 - shift) & 63));
}

// SignedAddOverflow32(lhs,rhs,val) performs a signed summation of |lhs| and
// |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed summation resulted in an overflow.
inline bool SignedAddOverflow32(int32_t lhs, int32_t rhs, int32_t* val) {
#if V8_HAS_BUILTIN_SADD_OVERFLOW
  return __builtin_sadd_overflow(lhs, rhs, val);
#else
  uint32_t res = static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs);
  *val = base::bit_cast<int32_t>(res);
  return ((res ^ lhs) & (res ^ rhs) & (1U << 31)) != 0;
#endif
}


// SignedSubOverflow32(lhs,rhs,val) performs a signed subtraction of |lhs| and
// |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed subtraction resulted in an overflow.
inline bool SignedSubOverflow32(int32_t lhs, int32_t rhs, int32_t* val) {
#if V8_HAS_BUILTIN_SSUB_OVERFLOW
  return __builtin_ssub_overflow(lhs, rhs, val);
#else
  uint32_t res = static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs);
  *val = base::bit_cast<int32_t>(res);
  return ((res ^ lhs) & (res ^ ~rhs) & (1U << 31)) != 0;
#endif
}

// SignedMulOverflow32(lhs,rhs,val) performs a signed multiplication of |lhs|
// and |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed multiplication resulted in an overflow.
inline bool SignedMulOverflow32(int32_t lhs, int32_t rhs, int32_t* val) {
#if V8_HAS_BUILTIN_SMUL_OVERFLOW
  return __builtin_smul_overflow(lhs, rhs, val);
#else
  // Compute the result as {int64_t}, then check for overflow.
  int64_t result = int64_t{lhs} * int64_t{rhs};
  *val = static_cast<int32_t>(result);
  using limits = std::numeric_limits<int32_t>;
  return result < limits::min() || result > limits::max();
#endif
}

// SignedAddOverflow64(lhs,rhs,val) performs a signed summation of |lhs| and
// |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed summation resulted in an overflow.
inline bool SignedAddOverflow64(int64_t lhs, int64_t rhs, int64_t* val) {
#if V8_HAS_BUILTIN_ADD_OVERFLOW
  return __builtin_add_overflow(lhs, rhs, val);
#else
  uint64_t res = static_cast<uint64_t>(lhs) + static_cast<uint64_t>(rhs);
  *val = base::bit_cast<int64_t>(res);
  return ((res ^ lhs) & (res ^ rhs) & (1ULL << 63)) != 0;
#endif
}


// SignedSubOverflow64(lhs,rhs,val) performs a signed subtraction of |lhs| and
// |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed subtraction resulted in an overflow.
inline bool SignedSubOverflow64(int64_t lhs, int64_t rhs, int64_t* val) {
#if V8_HAS_BUILTIN_SUB_OVERFLOW
  return __builtin_sub_overflow(lhs, rhs, val);
#else
  uint64_t res = static_cast<uint64_t>(lhs) - static_cast<uint64_t>(rhs);
  *val = base::bit_cast<int64_t>(res);
  return ((res ^ lhs) & (res ^ ~rhs) & (1ULL << 63)) != 0;
#endif
}

// SignedMulOverflow64(lhs,rhs,val) performs a signed multiplication of |lhs|
// and |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the signed multiplication resulted in an overflow.
inline bool SignedMulOverflow64(int64_t lhs, int64_t rhs, int64_t* val) {
#if V8_HAS_BUILTIN_MUL_OVERFLOW
  return __builtin_mul_overflow(lhs, rhs, val);
#else
  int64_t res = base::bit_cast<int64_t>(static_cast<uint64_t>(lhs) *
                                        static_cast<uint64_t>(rhs));
  *val = res;

  // Check for INT64_MIN / -1 as it's undefined behaviour and could cause
  // hardware exceptions.
  if ((res == INT64_MIN && lhs == -1)) {
    return true;
  }

  return lhs != 0 && (res / lhs) != rhs;
#endif
}

// SignedMulHigh32(lhs, rhs) multiplies two signed 32-bit values |lhs| and
// |rhs|, extracts the most significant 32 bits of the result, and returns
// those.
V8_BASE_EXPORT int32_t SignedMulHigh32(int32_t lhs, int32_t rhs);

// UnsignedMulHigh32(lhs, rhs) multiplies two unsigned 32-bit values |lhs| and
// |rhs|, extracts the most significant 32 bits of the result, and returns
// those.
V8_BASE_EXPORT uint32_t UnsignedMulHigh32(uint32_t lhs, uint32_t rhs);

// SignedMulHigh64(lhs, rhs) multiplies two signed 64-bit values |lhs| and
// |rhs|, extracts the most significant 64 bits of the result, and returns
// those.
V8_BASE_EXPORT int64_t SignedMulHigh64(int64_t lhs, int64_t rhs);

// UnsignedMulHigh64(lhs, rhs) multiplies two unsigned 64-bit values |lhs| and
// |rhs|, extracts the most significant 64 bits of the result, and returns
// those.
V8_BASE_EXPORT uint64_t UnsignedMulHigh64(uint64_t lhs, uint64_t rhs);

// SignedMulHighAndAdd32(lhs, rhs, acc) multiplies two signed 32-bit values
// |lhs| and |rhs|, extracts the most significant 32 bits of the result, and
// adds the accumulate value |acc|.
V8_BASE_EXPORT int32_t SignedMulHighAndAdd32(int32_t lhs, int32_t rhs,
                                             int32_t acc);

// SignedDiv32(lhs, rhs) divides |lhs| by |rhs| and returns the quotient
// truncated to int32. If |rhs| is zero, then zero is returned. If |lhs|
// is minint and |rhs| is -1, it returns minint.
V8_BASE_EXPORT int32_t SignedDiv32(int32_t lhs, int32_t rhs);

// SignedDiv64(lhs, rhs) divides |lhs| by |rhs| and returns the quotient
// truncated to int64. If |rhs| is zero, then zero is returned. If |lhs|
// is minint and |rhs| is -1, it returns minint.
V8_BASE_EXPORT int64_t SignedDiv64(int64_t lhs, int64_t rhs);

// SignedMod32(lhs, rhs) divides |lhs| by |rhs| and returns the remainder
// truncated to int32. If either |rhs| is zero or |lhs| is minint and |rhs|
// is -1, it returns zero.
V8_BASE_EXPORT int32_t SignedMod32(int32_t lhs, int32_t rhs);

// SignedMod64(lhs, rhs) divides |lhs| by |rhs| and returns the remainder
// truncated to int64. If either |rhs| is zero or |lhs| is minint and |rhs|
// is -1, it returns zero.
V8_BASE_EXPORT int64_t SignedMod64(int64_t lhs, int64_t rhs);

// UnsignedAddOverflow32(lhs,rhs,val) performs an unsigned summation of |lhs|
// and |rhs| and stores the result into the variable pointed to by |val| and
// returns true if the unsigned summation resulted in an overflow.
inline bool UnsignedAddOverflow32(uint32_t lhs, uint32_t rhs, uint32_t* val) {
#if V8_HAS_BUILTIN_SADD_OVERFLOW
  return __builtin_uadd_overflow(lhs, rhs, val);
#else
  *val = lhs + rhs;
  return *val < (lhs | rhs);
#endif
}


// UnsignedDiv32(lhs, rhs) divides |lhs| by |rhs| and returns the quotient
// truncated to uint32. If |rhs| is zero, then zero is returned.
inline uint32_t UnsignedDiv32(uint32_t lhs, uint32_t rhs) {
  return rhs ? lhs / rhs : 0u;
}

// UnsignedDiv64(lhs, rhs) divides |lhs| by |rhs| and returns the quotient
// truncated to uint64. If |rhs| is zero, then zero is returned.
inline uint64_t UnsignedDiv64(uint64_t lhs, uint64_t rhs) {
  return rhs ? lhs / rhs : 0u;
}

// UnsignedMod32(lhs, rhs) divides |lhs| by |rhs| and returns the remainder
// truncated to uint32. If |rhs| is zero, then zero is returned.
inline uint32_t UnsignedMod32(uint32_t lhs, uint32_t rhs) {
  return rhs ? lhs % rhs : 0u;
}

// UnsignedMod64(lhs, rhs) divides |lhs| by |rhs| and returns the remainder
// truncated to uint64. If |rhs| is zero, then zero is returned.
inline uint64_t UnsignedMod64(uint64_t lhs, uint64_t rhs) {
  return rhs ? lhs % rhs : 0u;
}

// Wraparound integer arithmetic without undefined behavior.

inline int32_t WraparoundAdd32(int32_t lhs, int32_t rhs) {
  return static_cast<int32_t>(static_cast<uint32_t>(lhs) +
                              static_cast<uint32_t>(rhs));
}

inline int32_t WraparoundNeg32(int32_t x) {
  return static_cast<int32_t>(-static_cast<uint32_t>(x));
}

// SignedSaturatedAdd64(lhs, rhs) adds |lhs| and |rhs|,
// checks and returns the result.
V8_BASE_EXPORT int64_t SignedSaturatedAdd64(int64_t lhs, int64_t rhs);

// SignedSaturatedSub64(lhs, rhs) subtracts |lhs| by |rhs|,
// checks and returns the result.
V8_BASE_EXPORT int64_t SignedSaturatedSub64(int64_t lhs, int64_t rhs);

template <class T>
V8_BASE_EXPORT constexpr int BitWidth(T x) {
  return std::numeric_limits<T>::digits - CountLeadingZeros(x);
}

}  // namespace bits
}  // namespace base
}  // namespace v8

#endif  // V8_BASE_BITS_H_
