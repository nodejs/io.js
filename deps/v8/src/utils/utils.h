// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_UTILS_H_
#define V8_UTILS_UTILS_H_

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <string>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/compiler-specific.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/safe_conversions.h"
#include "src/base/vector.h"
#include "src/common/globals.h"

#if defined(V8_USE_SIPHASH)
#include "third_party/siphash/halfsiphash.h"
#endif

#if defined(V8_OS_AIX)
#include <fenv.h>  // NOLINT(build/c++11)

#include "src/wasm/float16.h"
#endif

#ifdef _MSC_VER
// MSVC doesn't define SSE3. However, it does define AVX, and AVX implies SSE3.
#ifdef __AVX__
#ifndef __SSE3__
#define __SSE3__
#endif
#endif
#endif

#ifdef __SSE3__
#include <pmmintrin.h>
#endif

#if defined(V8_TARGET_ARCH_ARM64) && \
    (defined(__ARM_NEON) || defined(__ARM_NEON__))
#define V8_OPTIMIZE_WITH_NEON
#include <arm_neon.h>
#endif

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// General helper functions

template <typename T>
static T ArithmeticShiftRight(T x, int shift) {
  DCHECK_LE(0, shift);
  if (x < 0) {
    // Right shift of signed values is implementation defined. Simulate a
    // true arithmetic right shift by adding leading sign bits.
    using UnsignedT = typename std::make_unsigned<T>::type;
    UnsignedT mask = ~(static_cast<UnsignedT>(~0) >> shift);
    return (static_cast<UnsignedT>(x) >> shift) | mask;
  } else {
    return x >> shift;
  }
}

// Returns the maximum of the two parameters according to JavaScript semantics.
template <typename T>
T JSMax(T x, T y) {
  if (std::isnan(x)) return x;
  if (std::isnan(y)) return y;
  if (std::signbit(x) < std::signbit(y)) return x;
  return x > y ? x : y;
}

// Returns the maximum of the two parameters according to JavaScript semantics.
template <typename T>
T JSMin(T x, T y) {
  if (std::isnan(x)) return x;
  if (std::isnan(y)) return y;
  if (std::signbit(x) < std::signbit(y)) return y;
  return x > y ? y : x;
}

// Returns the absolute value of its argument.
template <typename T>
typename std::make_unsigned<T>::type Abs(T a)
  requires std::is_signed<T>::value
{
  // This is a branch-free implementation of the absolute value function and is
  // described in Warren's "Hacker's Delight", chapter 2. It avoids undefined
  // behavior with the arithmetic negation operation on signed values as well.
  using unsignedT = typename std::make_unsigned<T>::type;
  unsignedT x = static_cast<unsignedT>(a);
  unsignedT y = static_cast<unsignedT>(a >> (sizeof(T) * 8 - 1));
  return (x ^ y) - y;
}

inline double Modulo(double x, double y) {
#if defined(V8_OS_WIN)
  // Workaround MS fmod bugs. ECMA-262 says:
  // dividend is finite and divisor is an infinity => result equals dividend
  // dividend is a zero and divisor is nonzero finite => result equals dividend
  if (!(std::isfinite(x) && (!std::isfinite(y) && !std::isnan(y))) &&
      !(x == 0 && (y != 0 && std::isfinite(y)))) {
    double result = fmod(x, y);
    // Workaround MS bug in VS CRT in some OS versions, https://crbug.com/915045
    // fmod(-17, +/-1) should equal -0.0 but now returns 0.0.
    if (x < 0 && result == 0) result = -0.0;
    x = result;
  }
  return x;
#elif defined(V8_OS_AIX)
  // AIX raises an underflow exception for (Number.MIN_VALUE % Number.MAX_VALUE)
  feclearexcept(FE_ALL_EXCEPT);
  double result = std::fmod(x, y);
  int exception = fetestexcept(FE_UNDERFLOW);
  return (exception ? x : result);
#else
  return std::fmod(x, y);
#endif
}

template <typename T>
T SaturateAdd(T a, T b) {
  if (std::is_signed<T>::value) {
    if (a > 0 && b > 0) {
      if (a > std::numeric_limits<T>::max() - b) {
        return std::numeric_limits<T>::max();
      }
    } else if (a < 0 && b < 0) {
      if (a < std::numeric_limits<T>::min() - b) {
        return std::numeric_limits<T>::min();
      }
    }
  } else {
    CHECK(std::is_unsigned<T>::value);
    if (a > std::numeric_limits<T>::max() - b) {
      return std::numeric_limits<T>::max();
    }
  }
  return a + b;
}

template <typename T>
T SaturateSub(T a, T b) {
  if (std::is_signed<T>::value) {
    if (a >= 0 && b < 0) {
      if (a > std::numeric_limits<T>::max() + b) {
        return std::numeric_limits<T>::max();
      }
    } else if (a < 0 && b > 0) {
      if (a < std::numeric_limits<T>::min() + b) {
        return std::numeric_limits<T>::min();
      }
    }
  } else {
    CHECK(std::is_unsigned<T>::value);
    if (a < b) {
      return static_cast<T>(0);
    }
  }
  return a - b;
}

template <typename T>
T SaturateRoundingQMul(T a, T b) {
  // Saturating rounding multiplication for Q-format numbers. See
  // https://en.wikipedia.org/wiki/Q_(number_format) for a description.
  // Specifically this supports Q7, Q15, and Q31. This follows the
  // implementation in simulator-logic-arm64.cc (sqrdmulh) to avoid overflow
  // when a == b == int32 min.
  static_assert(std::is_integral<T>::value, "only integral types");

  constexpr int size_in_bits = sizeof(T) * 8;
  int round_const = 1 << (size_in_bits - 2);
  int64_t product = a * b;
  product += round_const;
  product >>= (size_in_bits - 1);
  return base::saturated_cast<T>(product);
}

// Multiply two numbers, returning a result that is twice as wide, no overflow.
// Put Wide first so we can use function template argument deduction for Narrow,
// and callers can provide only Wide.
template <typename Wide, typename Narrow>
Wide MultiplyLong(Narrow a, Narrow b) {
  static_assert(
      std::is_integral<Narrow>::value && std::is_integral<Wide>::value,
      "only integral types");
  static_assert(std::is_signed<Narrow>::value == std::is_signed<Wide>::value,
                "both must have same signedness");
  static_assert(sizeof(Narrow) * 2 == sizeof(Wide), "only twice as long");

  return static_cast<Wide>(a) * static_cast<Wide>(b);
}

// Add two numbers, returning a result that is twice as wide, no overflow.
// Put Wide first so we can use function template argument deduction for Narrow,
// and callers can provide only Wide.
template <typename Wide, typename Narrow>
Wide AddLong(Narrow a, Narrow b) {
  static_assert(
      std::is_integral<Narrow>::value && std::is_integral<Wide>::value,
      "only integral types");
  static_assert(std::is_signed<Narrow>::value == std::is_signed<Wide>::value,
                "both must have same signedness");
  static_assert(sizeof(Narrow) * 2 == sizeof(Wide), "only twice as long");

  return static_cast<Wide>(a) + static_cast<Wide>(b);
}

template <typename T>
inline T RoundingAverageUnsigned(T a, T b) {
  static_assert(std::is_unsigned<T>::value, "Only for unsiged types");
  static_assert(sizeof(T) < sizeof(uint64_t), "Must be smaller than uint64_t");
  return (static_cast<uint64_t>(a) + static_cast<uint64_t>(b) + 1) >> 1;
}

// Helper macros for defining a contiguous sequence of field offset constants.
// Example: (backslashes at the ends of respective lines of this multi-line
// macro definition are omitted here to please the compiler)
//
// #define MAP_FIELDS(V)
//   V(kField1Offset, kTaggedSize)
//   V(kField2Offset, kIntSize)
//   V(kField3Offset, kIntSize)
//   V(kField4Offset, kSystemPointerSize)
//   V(kSize, 0)
//
// DEFINE_FIELD_OFFSET_CONSTANTS(HeapObject::kHeaderSize, MAP_FIELDS)
//
#define DEFINE_ONE_FIELD_OFFSET(Name, Size, ...) \
  Name, Name##End = Name + (Size)-1,

#define DEFINE_FIELD_OFFSET_CONSTANTS(StartOffset, LIST_MACRO) \
  enum {                                                       \
    LIST_MACRO##_StartOffset = StartOffset - 1,                \
    LIST_MACRO(DEFINE_ONE_FIELD_OFFSET)                        \
  };

#define DEFINE_ONE_FIELD_OFFSET_PURE_NAME(CamelName, Size, ...) \
  k##CamelName##Offset,                                         \
      k##CamelName##OffsetEnd = k##CamelName##Offset + (Size)-1,

#define DEFINE_FIELD_OFFSET_CONSTANTS_WITH_PURE_NAME(StartOffset, LIST_MACRO) \
  enum {                                                                      \
    LIST_MACRO##_StartOffset = StartOffset - 1,                               \
    LIST_MACRO(DEFINE_ONE_FIELD_OFFSET_PURE_NAME)                             \
  };

// Size of the field defined by DEFINE_FIELD_OFFSET_CONSTANTS
#define FIELD_SIZE(Name) (Name##End + 1 - Name)

// Compare two offsets with static cast
#define STATIC_ASSERT_FIELD_OFFSETS_EQUAL(Offset1, Offset2) \
  static_assert(static_cast<int>(Offset1) == Offset2)
// ----------------------------------------------------------------------------
// Hash function.

static const uint64_t kZeroHashSeed = 0;

// Thomas Wang, Integer Hash Functions.
// http://www.concentric.net/~Ttwang/tech/inthash.htm`
inline uint32_t ComputeUnseededHash(uint32_t key) {
  uint32_t hash = key;
  hash = ~hash + (hash << 15);  // hash = (hash << 15) - hash - 1;
  hash = hash ^ (hash >> 12);
  hash = hash + (hash << 2);
  hash = hash ^ (hash >> 4);
  hash = hash * 2057;  // hash = (hash + (hash << 3)) + (hash << 11);
  hash = hash ^ (hash >> 16);
  return hash & 0x3fffffff;
}

inline uint32_t ComputeLongHash(uint64_t key) {
  uint64_t hash = key;
  hash = ~hash + (hash << 18);  // hash = (hash << 18) - hash - 1;
  hash = hash ^ (hash >> 31);
  hash = hash * 21;  // hash = (hash + (hash << 2)) + (hash << 4);
  hash = hash ^ (hash >> 11);
  hash = hash + (hash << 6);
  hash = hash ^ (hash >> 22);
  return static_cast<uint32_t>(hash & 0x3fffffff);
}

inline uint32_t ComputeSeededHash(uint32_t key, uint64_t seed) {
#ifdef V8_USE_SIPHASH
  return halfsiphash(key, seed);
#else
  return ComputeLongHash(static_cast<uint64_t>(key) ^ seed);
#endif  // V8_USE_SIPHASH
}

inline uint32_t ComputePointerHash(void* ptr) {
  return ComputeUnseededHash(
      static_cast<uint32_t>(reinterpret_cast<intptr_t>(ptr)));
}

inline uint32_t ComputeAddressHash(Address address) {
  return ComputeUnseededHash(static_cast<uint32_t>(address & 0xFFFFFFFFul));
}

// ----------------------------------------------------------------------------
// Miscellaneous

// Memory offset for lower and higher bits in a 64 bit integer.
#if defined(V8_TARGET_LITTLE_ENDIAN)
static const int kInt64LowerHalfMemoryOffset = 0;
static const int kInt64UpperHalfMemoryOffset = 4;
#elif defined(V8_TARGET_BIG_ENDIAN)
static const int kInt64LowerHalfMemoryOffset = 4;
static const int kInt64UpperHalfMemoryOffset = 0;
#endif  // V8_TARGET_LITTLE_ENDIAN

// A pointer that can only be set once and doesn't allow NULL values.
template <typename T>
class SetOncePointer {
 public:
  SetOncePointer() = default;

  bool is_set() const { return pointer_ != nullptr; }

  T* get() const {
    DCHECK_NOT_NULL(pointer_);
    return pointer_;
  }

  void set(T* value) {
    DCHECK(pointer_ == nullptr && value != nullptr);
    pointer_ = value;
  }

  SetOncePointer& operator=(T* value) {
    set(value);
    return *this;
  }

  bool operator==(std::nullptr_t) const { return pointer_ == nullptr; }
  bool operator!=(std::nullptr_t) const { return pointer_ != nullptr; }

 private:
  T* pointer_ = nullptr;
};

#if defined(__SSE3__)

template <typename Char>
V8_INLINE bool SimdMemEqual(const Char* lhs, const Char* rhs, size_t count,
                            size_t order) {
  static_assert(sizeof(Char) == 1);
  DCHECK_GE(order, 5);

  static constexpr uint16_t kSIMDMatched16Mask = UINT16_MAX;
  static constexpr uint32_t kSIMDMatched32Mask = UINT32_MAX;

  if (order == 5) {  // count: [17, 32]
    // Utilize more simd registers for better pipelining.
    const __m128i lhs128_start =
        _mm_lddqu_si128(reinterpret_cast<const __m128i*>(lhs));
    const __m128i lhs128_end = _mm_lddqu_si128(
        reinterpret_cast<const __m128i*>(lhs + count - sizeof(__m128i)));
    const __m128i rhs128_start =
        _mm_lddqu_si128(reinterpret_cast<const __m128i*>(rhs));
    const __m128i rhs128_end = _mm_lddqu_si128(
        reinterpret_cast<const __m128i*>(rhs + count - sizeof(__m128i)));
    const __m128i res_start = _mm_cmpeq_epi8(lhs128_start, rhs128_start);
    const __m128i res_end = _mm_cmpeq_epi8(lhs128_end, rhs128_end);
    const uint32_t res =
        _mm_movemask_epi8(res_start) << 16 | _mm_movemask_epi8(res_end);
    return res == kSIMDMatched32Mask;
  }

  // count: [33, ...]
  const __m128i lhs128_unrolled =
      _mm_lddqu_si128(reinterpret_cast<const __m128i*>(lhs));
  const __m128i rhs128_unrolled =
      _mm_lddqu_si128(reinterpret_cast<const __m128i*>(rhs));
  const __m128i res_unrolled = _mm_cmpeq_epi8(lhs128_unrolled, rhs128_unrolled);
  const uint16_t res_unrolled_mask = _mm_movemask_epi8(res_unrolled);
  if (res_unrolled_mask != kSIMDMatched16Mask) return false;

  for (size_t i = count % sizeof(__m128i); i < count; i += sizeof(__m128i)) {
    const __m128i lhs128 =
        _mm_lddqu_si128(reinterpret_cast<const __m128i*>(lhs + i));
    const __m128i rhs128 =
        _mm_lddqu_si128(reinterpret_cast<const __m128i*>(rhs + i));
    const __m128i res = _mm_cmpeq_epi8(lhs128, rhs128);
    const uint16_t res_mask = _mm_movemask_epi8(res);
    if (res_mask != kSIMDMatched16Mask) return false;
  }
  return true;
}

#elif defined(V8_OPTIMIZE_WITH_NEON)

// We intentionally use misaligned read/writes for NEON intrinsics, disable
// alignment sanitization explicitly.
template <typename Char>
V8_INLINE V8_CLANG_NO_SANITIZE("alignment") bool SimdMemEqual(const Char* lhs,
                                                              const Char* rhs,
                                                              size_t count,
                                                              size_t order) {
  static_assert(sizeof(Char) == 1);
  DCHECK_GE(order, 5);

  if (order == 5) {  // count: [17, 32]
    // Utilize more simd registers for better pipelining.
    const auto lhs0 = vld1q_u8(lhs);
    const auto lhs1 = vld1q_u8(lhs + count - sizeof(uint8x16_t));
    const auto rhs0 = vld1q_u8(rhs);
    const auto rhs1 = vld1q_u8(rhs + count - sizeof(uint8x16_t));
    const auto xored0 = veorq_u8(lhs0, rhs0);
    const auto xored1 = veorq_u8(lhs1, rhs1);
    const auto ored = vorrq_u8(xored0, xored1);
    return !static_cast<bool>(
        vgetq_lane_u64(vreinterpretq_u64_u8(vpmaxq_u8(ored, ored)), 0));
  }

  // count: [33, ...]
  const auto first_lhs0 = vld1q_u8(lhs);
  const auto first_rhs0 = vld1q_u8(rhs);
  const auto first_xored = veorq_u8(first_lhs0, first_rhs0);
  if (static_cast<bool>(vgetq_lane_u64(
          vreinterpretq_u64_u8(vpmaxq_u8(first_xored, first_xored)), 0))) {
    return false;
  }
  for (size_t i = count % sizeof(uint8x16_t); i < count;
       i += sizeof(uint8x16_t)) {
    const auto lhs0 = vld1q_u8(lhs + i);
    const auto rhs0 = vld1q_u8(rhs + i);
    const auto xored = veorq_u8(lhs0, rhs0);
    if (static_cast<bool>(
            vgetq_lane_u64(vreinterpretq_u64_u8(vpmaxq_u8(xored, xored)), 0)))
      return false;
  }
  return true;
}

#endif

template <typename IntType, typename Char>
V8_CLANG_NO_SANITIZE("alignment")
V8_INLINE bool OverlappingCompare(const Char* lhs, const Char* rhs,
                                  size_t count) {
  static_assert(sizeof(Char) == 1);
  return *reinterpret_cast<const IntType*>(lhs) ==
             *reinterpret_cast<const IntType*>(rhs) &&
         *reinterpret_cast<const IntType*>(lhs + count - sizeof(IntType)) ==
             *reinterpret_cast<const IntType*>(rhs + count - sizeof(IntType));
}

template <typename Char>
V8_CLANG_NO_SANITIZE("alignment")
V8_INLINE bool SimdMemEqual(const Char* lhs, const Char* rhs, size_t count) {
  static_assert(sizeof(Char) == 1);
  if (count == 0) {
    return true;
  }
  if (count == 1) {
    return *lhs == *rhs;
  }
  const size_t order =
      sizeof(count) * CHAR_BIT - base::bits::CountLeadingZeros(count - 1);
  switch (order) {
    case 1:  // count: [2, 2]
      return *reinterpret_cast<const uint16_t*>(lhs) ==
             *reinterpret_cast<const uint16_t*>(rhs);
    case 2:  // count: [3, 4]
      return OverlappingCompare<uint16_t>(lhs, rhs, count);
    case 3:  // count: [5, 8]
      return OverlappingCompare<uint32_t>(lhs, rhs, count);
    case 4:  // count: [9, 16]
      return OverlappingCompare<uint64_t>(lhs, rhs, count);
    default:
      return SimdMemEqual(lhs, rhs, count, order);
  }
}

// Compare 8bit/16bit chars to 8bit/16bit chars.
template <typename lchar, typename rchar>
inline bool CompareCharsEqualUnsigned(const lchar* lhs, const rchar* rhs,
                                      size_t chars) {
  static_assert(std::is_unsigned<lchar>::value);
  static_assert(std::is_unsigned<rchar>::value);
  if constexpr (sizeof(*lhs) == sizeof(*rhs)) {
#if defined(__SSE3__) || defined(V8_OPTIMIZE_WITH_NEON)
    if constexpr (sizeof(*lhs) == 1) {
      return SimdMemEqual(lhs, rhs, chars);
    }
#endif
    // memcmp compares byte-by-byte, but for equality it doesn't matter whether
    // two-byte char comparison is little- or big-endian.
    return memcmp(lhs, rhs, chars * sizeof(*lhs)) == 0;
  }
  for (const lchar* limit = lhs + chars; lhs < limit; ++lhs, ++rhs) {
    if (*lhs != *rhs) return false;
  }
  return true;
}

template <typename lchar, typename rchar>
inline bool CompareCharsEqual(const lchar* lhs, const rchar* rhs,
                              size_t chars) {
  using ulchar = typename std::make_unsigned<lchar>::type;
  using urchar = typename std::make_unsigned<rchar>::type;
  return CompareCharsEqualUnsigned(reinterpret_cast<const ulchar*>(lhs),
                                   reinterpret_cast<const urchar*>(rhs), chars);
}

// Compare 8bit/16bit chars to 8bit/16bit chars.
template <typename lchar, typename rchar>
inline int CompareCharsUnsigned(const lchar* lhs, const rchar* rhs,
                                size_t chars) {
  static_assert(std::is_unsigned<lchar>::value);
  static_assert(std::is_unsigned<rchar>::value);
  if (sizeof(*lhs) == sizeof(char) && sizeof(*rhs) == sizeof(char)) {
    // memcmp compares byte-by-byte, yielding wrong results for two-byte
    // strings on little-endian systems.
    return memcmp(lhs, rhs, chars);
  }
  for (const lchar* limit = lhs + chars; lhs < limit; ++lhs, ++rhs) {
    int r = static_cast<int>(*lhs) - static_cast<int>(*rhs);
    if (r != 0) return r;
  }
  return 0;
}

template <typename lchar, typename rchar>
inline int CompareChars(const lchar* lhs, const rchar* rhs, size_t chars) {
  using ulchar = typename std::make_unsigned<lchar>::type;
  using urchar = typename std::make_unsigned<rchar>::type;
  return CompareCharsUnsigned(reinterpret_cast<const ulchar*>(lhs),
                              reinterpret_cast<const urchar*>(rhs), chars);
}

// Calculate 10^exponent.
inline int TenToThe(int exponent) {
  DCHECK_LE(exponent, 9);
  DCHECK_GE(exponent, 1);
  int answer = 10;
  for (int i = 1; i < exponent; i++) answer *= 10;
  return answer;
}

// Bit field extraction.
inline uint32_t unsigned_bitextract_32(int msb, int lsb, uint32_t x) {
  return (x >> lsb) & ((1 << (1 + msb - lsb)) - 1);
}

inline uint64_t unsigned_bitextract_64(int msb, int lsb, uint64_t x) {
  return (x >> lsb) & ((static_cast<uint64_t>(1) << (1 + msb - lsb)) - 1);
}

inline int32_t signed_bitextract_32(int msb, int lsb, uint32_t x) {
  return static_cast<int32_t>(x << (31 - msb)) >> (lsb + 31 - msb);
}

// Check number width.
inline constexpr bool is_intn(int64_t x, unsigned n) {
  DCHECK((0 < n) && (n < 64));
  int64_t limit = static_cast<int64_t>(1) << (n - 1);
  return (-limit <= x) && (x < limit);
}

inline constexpr bool is_uintn(int64_t x, unsigned n) {
  DCHECK((0 < n) && (n < (sizeof(x) * kBitsPerByte)));
  return !(x >> n);
}

template <class T>
inline constexpr T truncate_to_intn(T x, unsigned n) {
  DCHECK((0 < n) && (n < (sizeof(x) * kBitsPerByte)));
  return (x & ((static_cast<T>(1) << n) - 1));
}

// clang-format off
#define INT_1_TO_63_LIST(V)                                   \
  V(1) V(2) V(3) V(4) V(5) V(6) V(7) V(8) V(9) V(10)          \
  V(11) V(12) V(13) V(14) V(15) V(16) V(17) V(18) V(19) V(20) \
  V(21) V(22) V(23) V(24) V(25) V(26) V(27) V(28) V(29) V(30) \
  V(31) V(32) V(33) V(34) V(35) V(36) V(37) V(38) V(39) V(40) \
  V(41) V(42) V(43) V(44) V(45) V(46) V(47) V(48) V(49) V(50) \
  V(51) V(52) V(53) V(54) V(55) V(56) V(57) V(58) V(59) V(60) \
  V(61) V(62) V(63)
// clang-format on

#define DECLARE_IS_INT_N(N) \
  inline constexpr bool is_int##N(int64_t x) { return is_intn(x, N); }
#define DECLARE_IS_UINT_N(N)              \
  template <class T>                      \
  inline constexpr bool is_uint##N(T x) { \
    return is_uintn(x, N);                \
  }
#define DECLARE_TRUNCATE_TO_INT_N(N)           \
  template <class T>                           \
  inline constexpr T truncate_to_int##N(T x) { \
    return truncate_to_intn(x, N);             \
  }

#define DECLARE_CHECKED_TRUNCATE_TO_INT_N(N)           \
  template <class T>                                   \
  inline constexpr T checked_truncate_to_int##N(T x) { \
    CHECK(is_int##N(x));                               \
    return truncate_to_intn(x, N);                     \
  }
INT_1_TO_63_LIST(DECLARE_IS_INT_N)
INT_1_TO_63_LIST(DECLARE_IS_UINT_N)
INT_1_TO_63_LIST(DECLARE_TRUNCATE_TO_INT_N)
INT_1_TO_63_LIST(DECLARE_CHECKED_TRUNCATE_TO_INT_N)
#undef DECLARE_IS_INT_N
#undef DECLARE_IS_UINT_N
#undef DECLARE_TRUNCATE_TO_INT_N
#undef DECLARE_CHECKED_TRUNCATE_TO_INT_N

// clang-format off
#define INT_0_TO_127_LIST(V)                                          \
V(0)   V(1)   V(2)   V(3)   V(4)   V(5)   V(6)   V(7)   V(8)   V(9)   \
V(10)  V(11)  V(12)  V(13)  V(14)  V(15)  V(16)  V(17)  V(18)  V(19)  \
V(20)  V(21)  V(22)  V(23)  V(24)  V(25)  V(26)  V(27)  V(28)  V(29)  \
V(30)  V(31)  V(32)  V(33)  V(34)  V(35)  V(36)  V(37)  V(38)  V(39)  \
V(40)  V(41)  V(42)  V(43)  V(44)  V(45)  V(46)  V(47)  V(48)  V(49)  \
V(50)  V(51)  V(52)  V(53)  V(54)  V(55)  V(56)  V(57)  V(58)  V(59)  \
V(60)  V(61)  V(62)  V(63)  V(64)  V(65)  V(66)  V(67)  V(68)  V(69)  \
V(70)  V(71)  V(72)  V(73)  V(74)  V(75)  V(76)  V(77)  V(78)  V(79)  \
V(80)  V(81)  V(82)  V(83)  V(84)  V(85)  V(86)  V(87)  V(88)  V(89)  \
V(90)  V(91)  V(92)  V(93)  V(94)  V(95)  V(96)  V(97)  V(98)  V(99)  \
V(100) V(101) V(102) V(103) V(104) V(105) V(106) V(107) V(108) V(109) \
V(110) V(111) V(112) V(113) V(114) V(115) V(116) V(117) V(118) V(119) \
V(120) V(121) V(122) V(123) V(124) V(125) V(126) V(127)
// clang-format on

class FeedbackSlot {
 public:
  FeedbackSlot() : id_(kInvalidSlot) {}
  explicit FeedbackSlot(int id) : id_(id) {}

  int ToInt() const { return id_; }

  static FeedbackSlot Invalid() { return FeedbackSlot(); }
  bool IsInvalid() const { return id_ == kInvalidSlot; }

  bool operator==(FeedbackSlot that) const { return this->id_ == that.id_; }
  bool operator!=(FeedbackSlot that) const { return !(*this == that); }

  friend size_t hash_value(FeedbackSlot slot) { return slot.ToInt(); }
  V8_EXPORT_PRIVATE friend std::ostream& operator<<(std::ostream& os,
                                                    FeedbackSlot);

  FeedbackSlot WithOffset(int offset) const {
    return FeedbackSlot(id_ + offset);
  }

 private:
  static const int kInvalidSlot = -1;

  int id_;
};

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, FeedbackSlot);

class BytecodeOffset {
 public:
  explicit constexpr BytecodeOffset(int id) : id_(id) {}
  constexpr int ToInt() const { return id_; }

  static constexpr BytecodeOffset None() { return BytecodeOffset(kNoneId); }

  // Special bailout id support for deopting into the {JSConstructStub} stub.
  // The following hard-coded deoptimization points are supported by the stub:
  //  - {ConstructStubCreate} maps to {construct_stub_create_deopt_pc_offset}.
  //  - {ConstructStubInvoke} maps to {construct_stub_invoke_deopt_pc_offset}.
  static BytecodeOffset ConstructStubCreate() { return BytecodeOffset(1); }
  static BytecodeOffset ConstructStubInvoke() { return BytecodeOffset(2); }

  constexpr bool IsNone() const { return id_ == kNoneId; }
  bool operator==(const BytecodeOffset& other) const {
    return id_ == other.id_;
  }
  bool operator!=(const BytecodeOffset& other) const {
    return id_ != other.id_;
  }
  friend size_t hash_value(BytecodeOffset);
  V8_EXPORT_PRIVATE friend std::ostream& operator<<(std::ostream&,
                                                    BytecodeOffset);

 private:
  friend class Builtins;

  static const int kNoneId = -1;

  // Using 0 could disguise errors.
  // Builtin continuations bailout ids start here. If you need to add a
  // non-builtin BytecodeOffset, add it before this id so that this Id has the
  // highest number.
  static const int kFirstBuiltinContinuationId = 1;

  int id_;
};

// ----------------------------------------------------------------------------
// I/O support.

// Our version of printf().
V8_EXPORT_PRIVATE void PRINTF_FORMAT(1, 2) PrintF(const char* format, ...);
V8_EXPORT_PRIVATE void PRINTF_FORMAT(2, 3)
    PrintF(FILE* out, const char* format, ...);

// Prepends the current process ID to the output.
void PRINTF_FORMAT(1, 2) PrintPID(const char* format, ...);

// Prepends the current process ID and given isolate pointer to the output.
void PRINTF_FORMAT(2, 3) PrintIsolate(void* isolate, const char* format, ...);

// Read a line of characters after printing the prompt to stdout. The resulting
// char* needs to be disposed off with DeleteArray by the caller.
char* ReadLine(const char* prompt);

// Write size chars from str to the file given by filename.
// The file is overwritten. Returns the number of chars written.
int WriteChars(const char* filename, const char* str, int size,
               bool verbose = true);

// Write size bytes to the file given by filename.
// The file is overwritten. Returns the number of bytes written.
int WriteBytes(const char* filename, const uint8_t* bytes, int size,
               bool verbose = true);

// Simple support to read a file into std::string.
// On return, *exits tells whether the file existed.
V8_EXPORT_PRIVATE std::string ReadFile(const char* filename, bool* exists,
                                       bool verbose = true);
V8_EXPORT_PRIVATE std::string ReadFile(FILE* file, bool* exists,
                                       bool verbose = true);

bool DoubleToBoolean(double d);

template <typename Char>
bool TryAddIndexChar(uint32_t* index, Char c);

enum ToIndexMode { kToArrayIndex, kToIntegerIndex };

// {index_t} is meant to be {uint32_t} or {size_t}.
template <typename Stream, typename index_t,
          enum ToIndexMode mode = kToArrayIndex>
bool StringToIndex(Stream* stream, index_t* index);

// Returns the current stack top. Works correctly with ASAN and SafeStack.
// GetCurrentStackPosition() should not be inlined, because it works on stack
// frames if it were inlined into a function with a huge stack frame it would
// return an address significantly above the actual current stack position.
V8_EXPORT_PRIVATE V8_NOINLINE uintptr_t GetCurrentStackPosition();

static inline uint16_t ByteReverse16(uint16_t value) {
#if V8_HAS_BUILTIN_BSWAP16
  return __builtin_bswap16(value);
#else
  return value << 8 | (value >> 8 & 0x00FF);
#endif
}

static inline uint32_t ByteReverse32(uint32_t value) {
#if V8_HAS_BUILTIN_BSWAP32
  return __builtin_bswap32(value);
#else
  return value << 24 | ((value << 8) & 0x00FF0000) |
         ((value >> 8) & 0x0000FF00) | ((value >> 24) & 0x00000FF);
#endif
}

static inline uint64_t ByteReverse64(uint64_t value) {
#if V8_HAS_BUILTIN_BSWAP64
  return __builtin_bswap64(value);
#else
  size_t bits_of_v = sizeof(value) * kBitsPerByte;
  return value << (bits_of_v - 8) |
         ((value << (bits_of_v - 24)) & 0x00FF000000000000) |
         ((value << (bits_of_v - 40)) & 0x0000FF0000000000) |
         ((value << (bits_of_v - 56)) & 0x000000FF00000000) |
         ((value >> (bits_of_v - 56)) & 0x00000000FF000000) |
         ((value >> (bits_of_v - 40)) & 0x0000000000FF0000) |
         ((value >> (bits_of_v - 24)) & 0x000000000000FF00) |
         ((value >> (bits_of_v - 8)) & 0x00000000000000FF);
#endif
}

template <typename V>
static inline V ByteReverse(V value) {
  size_t size_of_v = sizeof(value);
  switch (size_of_v) {
    case 1:
      return value;
    case 2:
      return static_cast<V>(ByteReverse16(static_cast<uint16_t>(value)));
    case 4:
      return static_cast<V>(ByteReverse32(static_cast<uint32_t>(value)));
    case 8:
      return static_cast<V>(ByteReverse64(static_cast<uint64_t>(value)));
    default:
      UNREACHABLE();
  }
}

#if V8_OS_AIX
// glibc on aix has a bug when using ceil, trunc or nearbyint:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=97086
template <typename T>
T FpOpWorkaround(T input, T value) {
  if (/*if -*/ std::signbit(input) && value == 0.0 &&
      /*if +*/ !std::signbit(value)) {
    return -0.0;
  }
  return value;
}

template <>
inline Float16 FpOpWorkaround(Float16 input, Float16 value) {
  float result = FpOpWorkaround(input.ToFloat32(), value.ToFloat32());
  return Float16::FromFloat32(result);
}

#endif

V8_EXPORT_PRIVATE bool PassesFilter(base::Vector<const char> name,
                                    base::Vector<const char> filter);

// Zap the specified area with a specific byte pattern. This currently defaults
// to int3 on x64 and ia32. On other architectures this will produce unspecified
// instruction sequences.
// TODO(jgruber): Better support for other architectures.
V8_INLINE void ZapCode(Address addr, size_t size_in_bytes) {
  static constexpr int kZapByte = 0xCC;
  std::memset(reinterpret_cast<void*>(addr), kZapByte, size_in_bytes);
}

inline bool RoundUpToPageSize(size_t byte_length, size_t page_size,
                              size_t max_allowed_byte_length, size_t* pages) {
  // This check is needed, since the arithmetic in RoundUp only works when
  // byte_length is not too close to the size_t limit.
  if (byte_length > max_allowed_byte_length) {
    return false;
  }
  size_t bytes_wanted = RoundUp(byte_length, page_size);
  if (bytes_wanted > max_allowed_byte_length) {
    return false;
  }
  *pages = bytes_wanted / page_size;
  return true;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_UTILS_H_
