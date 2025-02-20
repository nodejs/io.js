// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/base.h"

#include <limits>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "base_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

HWY_NOINLINE void TestAllLimits() {
  HWY_ASSERT_EQ(uint8_t{0}, LimitsMin<uint8_t>());
  HWY_ASSERT_EQ(uint16_t{0}, LimitsMin<uint16_t>());
  HWY_ASSERT_EQ(uint32_t{0}, LimitsMin<uint32_t>());
  HWY_ASSERT_EQ(uint64_t{0}, LimitsMin<uint64_t>());

  HWY_ASSERT_EQ(int8_t{-128}, LimitsMin<int8_t>());
  HWY_ASSERT_EQ(int16_t{-32768}, LimitsMin<int16_t>());
  HWY_ASSERT_EQ(static_cast<int32_t>(0x80000000u), LimitsMin<int32_t>());
  HWY_ASSERT_EQ(static_cast<int64_t>(0x8000000000000000ull),
                LimitsMin<int64_t>());

  HWY_ASSERT_EQ(uint8_t{0xFF}, LimitsMax<uint8_t>());
  HWY_ASSERT_EQ(uint16_t{0xFFFF}, LimitsMax<uint16_t>());
  HWY_ASSERT_EQ(uint32_t{0xFFFFFFFFu}, LimitsMax<uint32_t>());
  HWY_ASSERT_EQ(uint64_t{0xFFFFFFFFFFFFFFFFull}, LimitsMax<uint64_t>());

  HWY_ASSERT_EQ(int8_t{0x7F}, LimitsMax<int8_t>());
  HWY_ASSERT_EQ(int16_t{0x7FFF}, LimitsMax<int16_t>());
  HWY_ASSERT_EQ(int32_t{0x7FFFFFFFu}, LimitsMax<int32_t>());
  HWY_ASSERT_EQ(int64_t{0x7FFFFFFFFFFFFFFFull}, LimitsMax<int64_t>());

  HWY_ASSERT(LimitsMin<signed char>() == LimitsMin<int8_t>());
  HWY_ASSERT(LimitsMin<short>() <= LimitsMin<int16_t>());  // NOLINT
  HWY_ASSERT(LimitsMin<int>() <= LimitsMin<int16_t>());
  HWY_ASSERT(LimitsMin<long>() <= LimitsMin<int32_t>());       // NOLINT
  HWY_ASSERT(LimitsMin<long long>() <= LimitsMin<int64_t>());  // NOLINT

  HWY_ASSERT(LimitsMax<signed char>() == LimitsMax<int8_t>());
  HWY_ASSERT(LimitsMax<short>() >= LimitsMax<int16_t>());  // NOLINT
  HWY_ASSERT(LimitsMax<int>() >= LimitsMax<int16_t>());
  HWY_ASSERT(LimitsMax<long>() >= LimitsMax<int32_t>());       // NOLINT
  HWY_ASSERT(LimitsMax<long long>() >= LimitsMax<int64_t>());  // NOLINT

  HWY_ASSERT_EQ(static_cast<unsigned char>(0), LimitsMin<unsigned char>());
  HWY_ASSERT_EQ(static_cast<unsigned short>(0), LimitsMin<unsigned short>());
  HWY_ASSERT_EQ(0u, LimitsMin<unsigned>());
  HWY_ASSERT_EQ(0ul, LimitsMin<unsigned long>());        // NOLINT
  HWY_ASSERT_EQ(0ull, LimitsMin<unsigned long long>());  // NOLINT

  HWY_ASSERT(LimitsMax<unsigned char>() == LimitsMax<uint8_t>());
  HWY_ASSERT(LimitsMax<unsigned short>() >= LimitsMax<uint16_t>());  // NOLINT
  HWY_ASSERT(LimitsMax<unsigned>() >= LimitsMax<uint16_t>());
  HWY_ASSERT(LimitsMax<unsigned long>() >= LimitsMax<uint32_t>());  // NOLINT
  // NOLINTNEXTLINE
  HWY_ASSERT(LimitsMax<unsigned long long>() >= LimitsMax<uint64_t>());

  HWY_ASSERT(LimitsMin<char>() == 0 ||
             LimitsMin<char>() == LimitsMin<int8_t>());
  HWY_ASSERT(LimitsMax<char>() == LimitsMax<int8_t>() ||
             LimitsMax<char>() == LimitsMax<uint8_t>());

  HWY_ASSERT_EQ(size_t{0}, LimitsMin<size_t>());
  HWY_ASSERT(LimitsMin<ptrdiff_t>() < ptrdiff_t{0});
  HWY_ASSERT(LimitsMin<intptr_t>() < intptr_t{0});
  HWY_ASSERT_EQ(uintptr_t{0}, LimitsMin<uintptr_t>());
  HWY_ASSERT(LimitsMin<wchar_t>() <= wchar_t{0});

  HWY_ASSERT(LimitsMax<size_t>() > size_t{0});
  HWY_ASSERT(LimitsMax<ptrdiff_t>() > ptrdiff_t{0});
  HWY_ASSERT(LimitsMax<intptr_t>() > intptr_t{0});
  HWY_ASSERT(LimitsMax<uintptr_t>() > uintptr_t{0});
  HWY_ASSERT(LimitsMax<wchar_t>() > wchar_t{0});
}

struct TestLowestHighest {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    // numeric_limits<T>::lowest is only guaranteed to be what we expect (-max)
    // for built-in floating-point types.
    if (!IsSpecialFloat<T>()) {
      HWY_ASSERT_EQ(std::numeric_limits<T>::lowest(), LowestValue<T>());
      HWY_ASSERT_EQ(std::numeric_limits<T>::max(), HighestValue<T>());
    }
  }
};

HWY_NOINLINE void TestAllLowestHighest() { ForAllTypes(TestLowestHighest()); }
struct TestIsUnsigned {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    static_assert(!IsFloat<T>(), "Expected !IsFloat");
    static_assert(!IsSigned<T>(), "Expected !IsSigned");
    static_assert(IsInteger<T>(), "Expected IsInteger");
  }
};

struct TestIsSigned {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    static_assert(!IsFloat<T>(), "Expected !IsFloat");
    static_assert(IsSigned<T>(), "Expected IsSigned");
    static_assert(IsInteger<T>(), "Expected IsInteger");
  }
};

struct TestIsFloat {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    static_assert(IsFloat<T>(), "Expected IsFloat");
    static_assert(!IsInteger<T>(), "Expected !IsInteger");
    static_assert(IsSigned<T>(), "Floats are also considered signed");
  }
};

HWY_NOINLINE void TestAllType() {
  const TestIsUnsigned is_unsigned_test;
  const TestIsSigned is_signed_test;

  ForUnsignedTypes(is_unsigned_test);
  ForSignedTypes(is_signed_test);
  ForFloatTypes(TestIsFloat());

  is_unsigned_test(static_cast<unsigned char>(0));
  is_unsigned_test(static_cast<unsigned short>(0));
  is_unsigned_test(0u);
  is_unsigned_test(0ul);
  is_unsigned_test(0ull);
  is_unsigned_test(size_t{0});
  is_unsigned_test(uintptr_t{0});

  is_signed_test(static_cast<signed char>(0));
  is_signed_test(static_cast<signed short>(0));
  is_signed_test(0);
  is_signed_test(0L);
  is_signed_test(0LL);
  is_signed_test(ptrdiff_t{0});
  is_signed_test(intptr_t{0});

  static_assert(!IsFloat<char>(), "Expected !IsFloat<char>()");
  static_assert(!IsFloat<wchar_t>(), "Expected !IsFloat<wchar_t>()");
  static_assert(IsInteger<char>(), "Expected IsInteger<char>()");
  static_assert(IsInteger<wchar_t>(), "Expected IsInteger<wchar_t>()");

  static_assert(sizeof(MakeUnsigned<hwy::uint128_t>) == 16, "");
  static_assert(sizeof(MakeWide<uint64_t>) == 16, "Expected uint128_t");
  static_assert(sizeof(MakeNarrow<hwy::uint128_t>) == 8, "Expected uint64_t");
}

struct TestIsSame {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    static_assert(IsSame<T, T>(), "T == T");
    static_assert(!IsSame<MakeSigned<T>, MakeUnsigned<T>>(), "S != U");
    static_assert(!IsSame<MakeUnsigned<T>, MakeSigned<T>>(), "U != S");
  }
};

HWY_NOINLINE void TestAllIsSame() { ForAllTypes(TestIsSame()); }

HWY_NOINLINE void TestAllBitScan() {
  HWY_ASSERT_EQ(size_t{0}, Num0BitsAboveMS1Bit_Nonzero32(0x80000000u));
  HWY_ASSERT_EQ(size_t{0}, Num0BitsAboveMS1Bit_Nonzero32(0xFFFFFFFFu));
  HWY_ASSERT_EQ(size_t{1}, Num0BitsAboveMS1Bit_Nonzero32(0x40000000u));
  HWY_ASSERT_EQ(size_t{1}, Num0BitsAboveMS1Bit_Nonzero32(0x40108210u));
  HWY_ASSERT_EQ(size_t{30}, Num0BitsAboveMS1Bit_Nonzero32(2u));
  HWY_ASSERT_EQ(size_t{30}, Num0BitsAboveMS1Bit_Nonzero32(3u));
  HWY_ASSERT_EQ(size_t{31}, Num0BitsAboveMS1Bit_Nonzero32(1u));

  HWY_ASSERT_EQ(size_t{0},
                Num0BitsAboveMS1Bit_Nonzero64(0x8000000000000000ull));
  HWY_ASSERT_EQ(size_t{0},
                Num0BitsAboveMS1Bit_Nonzero64(0xFFFFFFFFFFFFFFFFull));
  HWY_ASSERT_EQ(size_t{1},
                Num0BitsAboveMS1Bit_Nonzero64(0x4000000000000000ull));
  HWY_ASSERT_EQ(size_t{1},
                Num0BitsAboveMS1Bit_Nonzero64(0x4010821004200011ull));
  HWY_ASSERT_EQ(size_t{62}, Num0BitsAboveMS1Bit_Nonzero64(2ull));
  HWY_ASSERT_EQ(size_t{62}, Num0BitsAboveMS1Bit_Nonzero64(3ull));
  HWY_ASSERT_EQ(size_t{63}, Num0BitsAboveMS1Bit_Nonzero64(1ull));

  HWY_ASSERT_EQ(size_t{0}, Num0BitsBelowLS1Bit_Nonzero32(1u));
  HWY_ASSERT_EQ(size_t{1}, Num0BitsBelowLS1Bit_Nonzero32(2u));
  HWY_ASSERT_EQ(size_t{30}, Num0BitsBelowLS1Bit_Nonzero32(0xC0000000u));
  HWY_ASSERT_EQ(size_t{31}, Num0BitsBelowLS1Bit_Nonzero32(0x80000000u));

  HWY_ASSERT_EQ(size_t{0}, Num0BitsBelowLS1Bit_Nonzero64(1ull));
  HWY_ASSERT_EQ(size_t{1}, Num0BitsBelowLS1Bit_Nonzero64(2ull));
  HWY_ASSERT_EQ(size_t{62},
                Num0BitsBelowLS1Bit_Nonzero64(0xC000000000000000ull));
  HWY_ASSERT_EQ(size_t{63},
                Num0BitsBelowLS1Bit_Nonzero64(0x8000000000000000ull));
}

HWY_NOINLINE void TestAllPopCount() {
  HWY_ASSERT_EQ(size_t{0}, PopCount(0u));
  HWY_ASSERT_EQ(size_t{1}, PopCount(1u));
  HWY_ASSERT_EQ(size_t{1}, PopCount(2u));
  HWY_ASSERT_EQ(size_t{2}, PopCount(3u));
  HWY_ASSERT_EQ(size_t{1}, PopCount(0x80000000u));
  HWY_ASSERT_EQ(size_t{31}, PopCount(0x7FFFFFFFu));
  HWY_ASSERT_EQ(size_t{32}, PopCount(0xFFFFFFFFu));

  HWY_ASSERT_EQ(size_t{1}, PopCount(0x80000000ull));
  HWY_ASSERT_EQ(size_t{31}, PopCount(0x7FFFFFFFull));
  HWY_ASSERT_EQ(size_t{32}, PopCount(0xFFFFFFFFull));
  HWY_ASSERT_EQ(size_t{33}, PopCount(0x10FFFFFFFFull));
  HWY_ASSERT_EQ(size_t{63}, PopCount(0xFFFEFFFFFFFFFFFFull));
  HWY_ASSERT_EQ(size_t{64}, PopCount(0xFFFFFFFFFFFFFFFFull));
}

// Exhaustive test for small/large dividends and divisors
HWY_NOINLINE void TestAllDivisor() {
  // Small d, small n
  for (uint32_t d = 1; d < 256; ++d) {
    const Divisor divisor(d);
    for (uint32_t n = 0; n < 256; ++n) {
      HWY_ASSERT(divisor.Divide(n) == n / d);
      HWY_ASSERT(divisor.Remainder(n) == n % d);
    }
  }

  // Large d, small n
  for (uint32_t d = 0xFFFFFF00u; d != 0; ++d) {
    const Divisor divisor(d);
    for (uint32_t n = 0; n < 256; ++n) {
      HWY_ASSERT(divisor.Divide(n) == n / d);
      HWY_ASSERT(divisor.Remainder(n) == n % d);
    }
  }

  // Small d, large n
  for (uint32_t d = 1; d < 256; ++d) {
    const Divisor divisor(d);
    for (uint32_t n = 0xFFFFFF00u; n != 0; ++n) {
      HWY_ASSERT(divisor.Divide(n) == n / d);
      HWY_ASSERT(divisor.Remainder(n) == n % d);
    }
  }

  // Large d, large n
  for (uint32_t d = 0xFFFFFF00u; d != 0; ++d) {
    const Divisor divisor(d);
    for (uint32_t n = 0xFFFFFF00u; n != 0; ++n) {
      HWY_ASSERT(divisor.Divide(n) == n / d);
      HWY_ASSERT(divisor.Remainder(n) == n % d);
    }
  }
}

struct TestScalarShr {
  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    using TU = MakeUnsigned<T>;
    constexpr T kMsb = static_cast<T>(1ULL << (sizeof(T) * 8 - 1));
    constexpr int kSizeInBits = static_cast<int>(sizeof(T) * 8);

    constexpr T kVal1 = static_cast<T>(0x776B0405296C183BULL & LimitsMax<TU>());
    constexpr T kVal2 = static_cast<T>(kVal1 | kMsb);

    for (int i = 0; i < kSizeInBits; i++) {
      T expected1;
      T expected2;

      const TU expected1_bits = static_cast<TU>(static_cast<TU>(kVal1) >> i);
      const TU expected2_bits = static_cast<TU>(
          (static_cast<TU>(kVal2) >> i) |
          ((IsSigned<T>() && i > 0)
               ? (~((static_cast<TU>(1) << (kSizeInBits - i)) - 1))
               : 0));

      CopySameSize(&expected1_bits, &expected1);
      CopySameSize(&expected2_bits, &expected2);

      HWY_ASSERT_EQ(expected1, ScalarShr(kVal1, i));
      HWY_ASSERT_EQ(expected2, ScalarShr(kVal2, i));
    }
  }
};

HWY_NOINLINE void TestAllScalarShr() { ForIntegerTypes(TestScalarShr()); }

template <class T>
static HWY_INLINE void AssertMul128Result(T expected_hi, T expected_lo, T a,
                                          T b, const char* file,
                                          const int line) {
  RemoveCvRef<T> actual_hi;
  const RemoveCvRef<T> actual_lo = Mul128(a, b, &actual_hi);
  hwy::AssertEqual(expected_lo, actual_lo, hwy::TargetName(HWY_TARGET), file,
                   line);
  hwy::AssertEqual(expected_hi, actual_hi, hwy::TargetName(HWY_TARGET), file,
                   line);
}

HWY_NOINLINE void TestAllMul128() {
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(0),
                     static_cast<int64_t>(0), static_cast<int64_t>(0), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(0),
                     static_cast<int64_t>(0), static_cast<int64_t>(1), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(0),
                     static_cast<int64_t>(0), static_cast<int64_t>(-1),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(0),
                     static_cast<int64_t>(1), static_cast<int64_t>(0), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(0),
                     static_cast<int64_t>(-1), static_cast<int64_t>(0),
                     __FILE__, __LINE__);

  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(1),
                     static_cast<int64_t>(1), static_cast<int64_t>(1), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(-1), static_cast<int64_t>(-1),
                     static_cast<int64_t>(-1), static_cast<int64_t>(1),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<int64_t>(-1), static_cast<int64_t>(-1),
                     static_cast<int64_t>(1), static_cast<int64_t>(-1),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<int64_t>(0), static_cast<int64_t>(1),
                     static_cast<int64_t>(-1), static_cast<int64_t>(-1),
                     __FILE__, __LINE__);

  AssertMul128Result(static_cast<uint64_t>(0), static_cast<uint64_t>(0),
                     static_cast<uint64_t>(0), static_cast<uint64_t>(0),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0), static_cast<uint64_t>(0),
                     static_cast<uint64_t>(0), static_cast<uint64_t>(1),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0), static_cast<uint64_t>(0),
                     static_cast<uint64_t>(1), static_cast<uint64_t>(0),
                     __FILE__, __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0), static_cast<uint64_t>(1),
                     static_cast<uint64_t>(1), static_cast<uint64_t>(1),
                     __FILE__, __LINE__);

  AssertMul128Result(static_cast<int64_t>(0x24E331A77C96011DULL),
                     static_cast<int64_t>(0x3C5385F8E294E438ULL),
                     static_cast<int64_t>(0x4F87AE233A08DD18ULL),
                     static_cast<int64_t>(0x76BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0xFD1F5A95DF919291ULL),
                     static_cast<int64_t>(0x3C5385F8E294E438ULL),
                     static_cast<int64_t>(0x4F87AE233A08DD18ULL),
                     static_cast<int64_t>(0xF6BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0xE984CB0E30E8DC36ULL),
                     static_cast<int64_t>(0xBC5385F8E294E438ULL),
                     static_cast<int64_t>(0xCF87AE233A08DD18ULL),
                     static_cast<int64_t>(0x76BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<int64_t>(0x01C0F3FC93E46DAAULL),
                     static_cast<int64_t>(0xBC5385F8E294E438ULL),
                     static_cast<int64_t>(0xCF87AE233A08DD18ULL),
                     static_cast<int64_t>(0xF6BCCD32975A49CDULL), __FILE__,
                     __LINE__);

  AssertMul128Result(static_cast<uint64_t>(0x24E331A77C96011DULL),
                     static_cast<uint64_t>(0x3C5385F8E294E438ULL),
                     static_cast<uint64_t>(0x4F87AE233A08DD18ULL),
                     static_cast<uint64_t>(0x76BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0x4CA708B9199A6FA9ULL),
                     static_cast<uint64_t>(0x3C5385F8E294E438ULL),
                     static_cast<uint64_t>(0x4F87AE233A08DD18ULL),
                     static_cast<uint64_t>(0xF6BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0x60419840C8432603ULL),
                     static_cast<uint64_t>(0xBC5385F8E294E438ULL),
                     static_cast<uint64_t>(0xCF87AE233A08DD18ULL),
                     static_cast<uint64_t>(0x76BCCD32975A49CDULL), __FILE__,
                     __LINE__);
  AssertMul128Result(static_cast<uint64_t>(0xC8056F526547948FULL),
                     static_cast<uint64_t>(0xBC5385F8E294E438ULL),
                     static_cast<uint64_t>(0xCF87AE233A08DD18ULL),
                     static_cast<uint64_t>(0xF6BCCD32975A49CDULL), __FILE__,
                     __LINE__);
}

template <class T>
static HWY_INLINE T TestEndianGetIntegerVal(T val) {
  static_assert(!IsFloat<T>() && !IsSpecialFloat<T>(),
                "T must not be a floating-point type");
  using TU = MakeUnsigned<T>;
  static_assert(sizeof(T) == sizeof(TU),
                "sizeof(T) == sizeof(TU) must be true");

  uint8_t result_bytes[sizeof(T)];
  const TU val_u = static_cast<TU>(val);

  for (size_t i = 0; i < sizeof(T); i++) {
#if HWY_IS_BIG_ENDIAN
    const size_t shift_amt = (sizeof(T) - 1 - i) * 8;
#else
    const size_t shift_amt = i * 8;
#endif
    result_bytes[i] = static_cast<uint8_t>((val_u >> shift_amt) & 0xFF);
  }

  T result;
  CopyBytes<sizeof(T)>(result_bytes, &result);
  return result;
}

template <class T, class... Bytes>
static HWY_INLINE T TestEndianCreateValueFromBytes(Bytes&&... bytes) {
  static_assert(sizeof(T) > 0, "sizeof(T) > 0 must be true");
  static_assert(sizeof...(Bytes) == sizeof(T),
                "sizeof...(Bytes) == sizeof(T) must be true");

  const uint8_t src_bytes[sizeof(T)]{static_cast<uint8_t>(bytes)...};

  T result;
  CopyBytes<sizeof(T)>(src_bytes, &result);
  return result;
}

#define HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(val) \
  HWY_ASSERT_EQ(val, TestEndianGetIntegerVal(val))

HWY_NOINLINE void TestAllEndian() {
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int8_t{0x01});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint8_t{0x01});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int16_t{0x0102});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint16_t{0x0102});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int32_t{0x01020304});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint32_t{0x01020304});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int64_t{0x0102030405060708});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint64_t{0x0102030405060708});

  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int16_t{0x0201});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint16_t{0x0201});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int32_t{0x04030201});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint32_t{0x04030201});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(int64_t{0x0807060504030201});
  HWY_TEST_ENDIAN_CHECK_INTEGER_VAL(uint64_t{0x0807060504030201});

  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? int16_t{0x0102} : int16_t{0x0201},
                TestEndianCreateValueFromBytes<int16_t>(0x01, 0x02));
  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? uint16_t{0x0102} : uint16_t{0x0201},
                TestEndianCreateValueFromBytes<uint16_t>(0x01, 0x02));
  HWY_ASSERT_EQ(
      HWY_IS_BIG_ENDIAN ? int32_t{0x01020304} : int32_t{0x04030201},
      TestEndianCreateValueFromBytes<int32_t>(0x01, 0x02, 0x03, 0x04));
  HWY_ASSERT_EQ(
      HWY_IS_BIG_ENDIAN ? uint32_t{0x01020304} : uint32_t{0x04030201},
      TestEndianCreateValueFromBytes<uint32_t>(0x01, 0x02, 0x03, 0x04));
  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? int64_t{0x0102030405060708}
                                  : int64_t{0x0807060504030201},
                TestEndianCreateValueFromBytes<int64_t>(
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08));
  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? uint64_t{0x0102030405060708}
                                  : uint64_t{0x0807060504030201},
                TestEndianCreateValueFromBytes<uint64_t>(
                    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08));

  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? int16_t{-0x5EFE} : int16_t{0x02A1},
                TestEndianCreateValueFromBytes<int16_t>(0xA1, 0x02));
  HWY_ASSERT_EQ(
      HWY_IS_BIG_ENDIAN ? int32_t{-0x5E4D3CFC} : int32_t{0x04C3B2A1},
      TestEndianCreateValueFromBytes<int32_t>(0xA1, 0xB2, 0xC3, 0x04));
  HWY_ASSERT_EQ(HWY_IS_BIG_ENDIAN ? int64_t{-0x6E5D4C3B2A1908F8}
                                  : int64_t{0x08F7E6D5C4B3A291},
                TestEndianCreateValueFromBytes<int64_t>(
                    0x91, 0xA2, 0xB3, 0xC4, 0xD5, 0xE6, 0xF7, 0x08));

  HWY_ASSERT_EQ(HWY_IS_LITTLE_ENDIAN ? int16_t{-0x5DFF} : int16_t{0x01A2},
                TestEndianCreateValueFromBytes<int16_t>(0x01, 0xA2));
  HWY_ASSERT_EQ(
      HWY_IS_LITTLE_ENDIAN ? int32_t{-0x3B4C5DFF} : int32_t{0x01A2B3C4},
      TestEndianCreateValueFromBytes<int32_t>(0x01, 0xA2, 0xB3, 0xC4));
  HWY_ASSERT_EQ(HWY_IS_LITTLE_ENDIAN ? int64_t{-0x0718293A4B5C6DFF}
                                     : int64_t{0x0192A3B4C5D6E7F8},
                TestEndianCreateValueFromBytes<int64_t>(
                    0x01, 0x92, 0xA3, 0xB4, 0xC5, 0xD6, 0xE7, 0xF8));

#if HWY_IS_BIG_ENDIAN
  HWY_ASSERT_EQ(1.0f,
                TestEndianCreateValueFromBytes<float>(0x3F, 0x80, 0x00, 0x00));
  HWY_ASSERT_EQ(15922433.0f,
                TestEndianCreateValueFromBytes<float>(0x4B, 0x72, 0xF5, 0x01));
  HWY_ASSERT_EQ(-12357485.0f,
                TestEndianCreateValueFromBytes<float>(0xCB, 0x3C, 0x8F, 0x6D));
#else
  HWY_ASSERT_EQ(1.0f,
                TestEndianCreateValueFromBytes<float>(0x00, 0x00, 0x80, 0x3F));
  HWY_ASSERT_EQ(15922433.0f,
                TestEndianCreateValueFromBytes<float>(0x01, 0xF5, 0x72, 0x4B));
  HWY_ASSERT_EQ(-12357485.0f,
                TestEndianCreateValueFromBytes<float>(0x6D, 0x8F, 0x3C, 0xCB));
#endif

#if HWY_HAVE_FLOAT64
#if HWY_IS_BIG_ENDIAN
  HWY_ASSERT_EQ(1.0, TestEndianCreateValueFromBytes<double>(
                         0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00));
  HWY_ASSERT_EQ(8707235690688195.0,
                TestEndianCreateValueFromBytes<double>(0x43, 0x3E, 0xEF, 0x2F,
                                                       0x4A, 0x51, 0xAE, 0xC3));
  HWY_ASSERT_EQ(-6815854340348452.0,
                TestEndianCreateValueFromBytes<double>(0xC3, 0x38, 0x36, 0xFB,
                                                       0xC0, 0xCC, 0x1A, 0x24));
#else
  HWY_ASSERT_EQ(1.0, TestEndianCreateValueFromBytes<double>(
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F));
  HWY_ASSERT_EQ(8707235690688195.0,
                TestEndianCreateValueFromBytes<double>(0xC3, 0xAE, 0x51, 0x4A,
                                                       0x2F, 0xEF, 0x3E, 0x43));
  HWY_ASSERT_EQ(-6815854340348452.0,
                TestEndianCreateValueFromBytes<double>(0x24, 0x1A, 0xCC, 0xC0,
                                                       0xFB, 0x36, 0x38, 0xC3));
#endif  // HWY_IS_BIG_ENDIAN
#endif  // HWY_HAVE_FLOAT64

#if HWY_IS_BIG_ENDIAN
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(1.0f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0x3F, 0x80)));
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(0.333984375f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0x3E, 0xAB)));
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(167121905303526337111381770240.0f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0x70, 0x07)));
#else
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(1.0f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0x80, 0x3F)));
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(0.333984375f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0xAB, 0x3E)));
  HWY_ASSERT_EQ(ConvertScalarTo<bfloat16_t>(167121905303526337111381770240.0f),
                BitCastScalar<bfloat16_t>(
                    TestEndianCreateValueFromBytes<uint16_t>(0x07, 0x70)));
#endif
}

struct TestSpecialFloat {
  template <class T>
  static constexpr bool EnableSpecialFloatArithOpTest() {
    return (hwy::IsSame<T, float16_t>() && HWY_HAVE_SCALAR_F16_OPERATORS) ||
           (hwy::IsSame<T, bfloat16_t>() && HWY_HAVE_SCALAR_BF16_OPERATORS);
  }

  template <class T>
  static constexpr HWY_INLINE T EnsureNotNativeSpecialFloat(T&& val) {
#if HWY_HAVE_SCALAR_F16_TYPE
    static_assert(!hwy::IsSame<RemoveCvRef<T>, float16_t::Native>(),
                  "The operator must not return a float16_t::Native");
#endif
#if HWY_HAVE_SCALAR_BF16_TYPE
    static_assert(!hwy::IsSame<RemoveCvRef<T>, bfloat16_t::Native>(),
                  "The operator must not return a bfloat16_t::Native");
#endif
    return static_cast<T&&>(val);
  }

  template <class T>
  static HWY_INLINE void AssertSpecialFloatOpResultInRange(float min_expected,
                                                           float max_expected,
                                                           T actual,
                                                           const char* filename,
                                                           const int line) {
    if (!(actual >= min_expected && actual <= max_expected)) {
      hwy::Abort(
          filename, line,
          "mismatch: value was expected to be between %g and %g, got %g\n",
          static_cast<double>(min_expected), static_cast<double>(max_expected),
          ConvertScalarTo<double>(actual));
    }
  }

  template <class T,
            hwy::EnableIf<EnableSpecialFloatArithOpTest<T>()>* = nullptr>
  static HWY_NOINLINE void TestSpecialFloatArithOperators(T /*unused*/) {
#if HWY_HAVE_SCALAR_F16_OPERATORS || HWY_HAVE_SCALAR_BF16_OPERATORS
    AssertSpecialFloatOpResultInRange(
        -0.008422852f, -0.008361816f,
        EnsureNotNativeSpecialFloat(static_cast<T>(-0.008911133f) +
                                    static_cast<T>(5.264282E-4f)),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        0.44335937f, 0.4453125f,
        EnsureNotNativeSpecialFloat(static_cast<T>(-0.0014266968f) +
                                    0.4453125f),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        39.25f, 39.5f,
        EnsureNotNativeSpecialFloat(34.25f + static_cast<T>(5.0625f)), __FILE__,
        __LINE__);

    AssertSpecialFloatOpResultInRange(
        7456.0f, 7488.0f,
        EnsureNotNativeSpecialFloat(static_cast<T>(0.29101562f) -
                                    static_cast<T>(-7456.0f)),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        -2.21875f, -2.203125f,
        EnsureNotNativeSpecialFloat(static_cast<T>(1.66893E-4f) - 2.21875f),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        0.32421875f, 0.32617188f,
        EnsureNotNativeSpecialFloat(0.35351562f - static_cast<T>(0.028198242f)),
        __FILE__, __LINE__);

    AssertSpecialFloatOpResultInRange(
        -0.01135254f, -0.011291503f,
        EnsureNotNativeSpecialFloat(static_cast<T>(2.109375f) *
                                    static_cast<T>(-0.0053710938f)),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        2.359375f, 2.375f,
        EnsureNotNativeSpecialFloat(static_cast<T>(0.0019454956f) * 1216.0f),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        -1.1014938E-4f, 3.453125f,
        EnsureNotNativeSpecialFloat(-0.00038146973f *
                                    static_cast<T>(-0.00037956237f)),
        __FILE__, __LINE__);

    AssertSpecialFloatOpResultInRange(
        -27.875f, -27.75f,
        EnsureNotNativeSpecialFloat(static_cast<T>(-56.5f) /
                                    static_cast<T>(2.03125f)),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        0.033203125f, 0.033447266f,
        EnsureNotNativeSpecialFloat(static_cast<T>(470.0f) / 14080.0f),
        __FILE__, __LINE__);
    AssertSpecialFloatOpResultInRange(
        0.51953125f, 0.5234375f,
        EnsureNotNativeSpecialFloat(0.26367188f / static_cast<T>(0.50390625f)),
        __FILE__, __LINE__);

    T incr_assign_result_1 = static_cast<T>(1.373291E-4f);
    EnsureNotNativeSpecialFloat(incr_assign_result_1 +=
                                static_cast<T>(-20.375f));
    AssertSpecialFloatOpResultInRange(-20.375f, -20.25f, incr_assign_result_1,
                                      __FILE__, __LINE__);

    T incr_assign_result_2 = static_cast<T>(2.1457672E-4f);
    EnsureNotNativeSpecialFloat(incr_assign_result_2 += static_cast<int8_t>(7));
    AssertSpecialFloatOpResultInRange(7.0f, 7.03125f, incr_assign_result_2,
                                      __FILE__, __LINE__);

    float incr_assign_result3 = -6.747985f;
    incr_assign_result3 += static_cast<T>(4.15625f);
    AssertSpecialFloatOpResultInRange(-2.59375f, -2.578125f,
                                      incr_assign_result3, __FILE__, __LINE__);

    float incr_assign_result4 = 6.71875;
    incr_assign_result4 += static_cast<T>(2.359375);
    AssertSpecialFloatOpResultInRange(9.0625, 9.125, incr_assign_result4,
                                      __FILE__, __LINE__);

    T decr_assign_result_1 = static_cast<T>(4.4059753E-4f);
    EnsureNotNativeSpecialFloat(decr_assign_result_1 -=
                                static_cast<T>(6880.0f));
    AssertSpecialFloatOpResultInRange(-6880, -6848, decr_assign_result_1,
                                      __FILE__, __LINE__);

    T decr_assign_result_2 = static_cast<T>(85.5f);
    EnsureNotNativeSpecialFloat(decr_assign_result_2 -= static_cast<int8_t>(5));
    AssertSpecialFloatOpResultInRange(80.5f, 80.5f, decr_assign_result_2,
                                      __FILE__, __LINE__);

    float decr_assign_result3 = 9.875f;
    decr_assign_result3 -= static_cast<T>(1.5234375f);
    AssertSpecialFloatOpResultInRange(8.3125f, 8.375f, decr_assign_result3,
                                      __FILE__, __LINE__);

    double decr_assign_result4 = 0.337890625;
    decr_assign_result4 -= static_cast<T>(2.328125);
    AssertSpecialFloatOpResultInRange(-1.9921875, -1.984375,
                                      decr_assign_result4, __FILE__, __LINE__);

    T mul_assign_result_1 = static_cast<T>(15680.0f);
    EnsureNotNativeSpecialFloat(mul_assign_result_1 *=
                                static_cast<T>(0.001373291f));
    AssertSpecialFloatOpResultInRange(21.5f, 21.625f, mul_assign_result_1,
                                      __FILE__, __LINE__);

    T mul_assign_result_2 = static_cast<T>(2.609375f);
    EnsureNotNativeSpecialFloat(mul_assign_result_2 *= static_cast<int8_t>(7));
    AssertSpecialFloatOpResultInRange(18.25, 18.375, mul_assign_result_2,
                                      __FILE__, __LINE__);

    float mul_assign_result3 = 4.125f;
    mul_assign_result3 *= static_cast<T>(3.375f);
    AssertSpecialFloatOpResultInRange(13.875f, 13.9375f, mul_assign_result3,
                                      __FILE__, __LINE__);

    double mul_assign_result4 = 7.9375;
    mul_assign_result4 *= static_cast<T>(0.79296875);
    AssertSpecialFloatOpResultInRange(6.28125, 6.3125, mul_assign_result4,
                                      __FILE__, __LINE__);

    T div_assign_result_1 = static_cast<T>(11584.0f);
    EnsureNotNativeSpecialFloat(div_assign_result_1 /= static_cast<T>(9.5625f));
    AssertSpecialFloatOpResultInRange(1208.0f, 1216.0f, div_assign_result_1,
                                      __FILE__, __LINE__);

    T div_assign_result_2 = static_cast<T>(0.12109375f);
    EnsureNotNativeSpecialFloat(div_assign_result_2 /= static_cast<int8_t>(3));
    AssertSpecialFloatOpResultInRange(0.040283203f, 0.040527344f,
                                      div_assign_result_2, __FILE__, __LINE__);

    float div_assign_result_3 = 0.21679688f;
    div_assign_result_3 /= static_cast<T>(3.421875f);
    AssertSpecialFloatOpResultInRange(0.06298828125f, 0.0634765625f,
                                      div_assign_result_3, __FILE__, __LINE__);

    double div_assign_result_4 = 5.34375;
    div_assign_result_4 /= static_cast<T>(0.337890625);
    AssertSpecialFloatOpResultInRange(15.8125, 15.875, div_assign_result_4,
                                      __FILE__, __LINE__);

    HWY_ASSERT_EQ(static_cast<T>(-1.0f),
                  EnsureNotNativeSpecialFloat(-static_cast<T>(1.0f)));
    HWY_ASSERT_EQ(static_cast<T>(1.0f),
                  EnsureNotNativeSpecialFloat(+static_cast<T>(1.0f)));

    T pre_incr_result_1 = static_cast<T>(1.0f);
    T pre_incr_result_2 = EnsureNotNativeSpecialFloat(++pre_incr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(2.0f), pre_incr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(2.0f), pre_incr_result_2);

    T post_incr_result_1 = static_cast<T>(5.0f);
    T post_incr_result_2 = EnsureNotNativeSpecialFloat(post_incr_result_1++);
    HWY_ASSERT_EQ(static_cast<T>(6.0f), post_incr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(5.0f), post_incr_result_2);

    T pre_decr_result_1 = static_cast<T>(-2.0f);
    T pre_decr_result_2 = EnsureNotNativeSpecialFloat(--pre_decr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(-3.0f), pre_decr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(-3.0f), pre_decr_result_2);

    T post_decr_result_1 = static_cast<T>(-7.0f);
    T post_decr_result_2 = EnsureNotNativeSpecialFloat(post_decr_result_1--);
    HWY_ASSERT_EQ(static_cast<T>(-8.0f), post_decr_result_1);
    HWY_ASSERT_EQ(static_cast<T>(-7.0f), post_decr_result_2);

    HWY_ASSERT(static_cast<T>(1.0f) == 1.0f);
    HWY_ASSERT(static_cast<T>(-2.40625f) != 0.0033416748f);
    HWY_ASSERT(static_cast<T>(-3248.0f) < 0.0018997193f);
    HWY_ASSERT(static_cast<T>(-27904.0f) <= -3.859375f);
    HWY_ASSERT(static_cast<T>(1.078125f) > 0.010009765f);
    HWY_ASSERT(static_cast<T>(45312.0f) >= 0.00024318695f);

    HWY_ASSERT(2.0f == static_cast<T>(2.0f));
    HWY_ASSERT(-5.78125f != static_cast<T>(-15168.0f));
    HWY_ASSERT(-0.056884766f < static_cast<T>(0.000088214875f));
    HWY_ASSERT(0.00008392333f <= static_cast<T>(1384.0f));
    HWY_ASSERT(21888.0f > static_cast<T>(-2.578125f));
    HWY_ASSERT(0.087402344 >= static_cast<T>(-0.65625f));
#endif  // HWY_HAVE_SCALAR_F16_OPERATORS || HWY_HAVE_SCALAR_BF16_OPERATORS
  }

  template <class T,
            hwy::EnableIf<!EnableSpecialFloatArithOpTest<T>()>* = nullptr>
  static HWY_INLINE void TestSpecialFloatArithOperators(T /*unused*/) {}

  template <class T>
  HWY_NOINLINE void operator()(T /*unused*/) const {
    static_assert(IsSpecialFloat<T>(), "IsSpecialFloat<T>() must be true");

    HWY_ASSERT_EQ(static_cast<uint32_t>(0x436B0000u),
                  BitCastScalar<uint32_t>(ConvertScalarTo<float>(
                      BitCastScalar<T>(static_cast<uint16_t>(
                          IsSame<T, hwy::float16_t>() ? 0x5B58u : 0x436Bu)))));
    HWY_ASSERT_EQ(static_cast<uint32_t>(0xBB790000u),
                  BitCastScalar<uint32_t>(ConvertScalarTo<float>(
                      BitCastScalar<T>(static_cast<uint16_t>(
                          IsSame<T, hwy::float16_t>() ? 0x9BC8u : 0xBB79u)))));

    HWY_ASSERT_EQ(static_cast<uint32_t>(
                      IsSame<T, hwy::float16_t>() ? 0xC0B86000u : 0xC5C30000u),
                  BitCastScalar<uint32_t>(ConvertScalarTo<float>(
                      BitCastScalar<T>(static_cast<uint16_t>(0xC5C3u)))));
    HWY_ASSERT_EQ(static_cast<uint32_t>(
                      IsSame<T, hwy::float16_t>() ? 0x41D20000u : 0x4E900000u),
                  BitCastScalar<uint32_t>(ConvertScalarTo<float>(
                      BitCastScalar<T>(static_cast<uint16_t>(0x4E90u)))));

    HWY_ASSERT_EQ(1696.0, ConvertScalarTo<double>(ConvertScalarTo<T>(1696.0f)));
    HWY_ASSERT_EQ(
        -0.00177001953125f,
        ConvertScalarTo<float>(ConvertScalarTo<T>(-0.00177001953125f)));

    HWY_ASSERT_EQ(0.49609375f,
                  ConvertScalarTo<float>(ConvertScalarTo<T>(0.49609375)));
    HWY_ASSERT_EQ(
        0.000553131103515625,
        ConvertScalarTo<double>(ConvertScalarTo<T>(0.000553131103515625)));

    HWY_ASSERT_EQ(ConvertScalarTo<T>(3.0f), ConvertScalarTo<T>(3));
    HWY_ASSERT_EQ(ConvertScalarTo<T>(-5.5f), ConvertScalarTo<T>(-5.5));
    HWY_ASSERT_EQ(ConvertScalarTo<T>(0.82421875f),
                  ConvertScalarTo<T>(BF16FromF32(0.82421875f)));
    HWY_ASSERT_EQ(ConvertScalarTo<T>(-6.375f),
                  ConvertScalarTo<T>(F16FromF32(-6.375f)));

    HWY_ASSERT(ConvertScalarTo<T>(-3.671875f) <
               ConvertScalarTo<T>(0.0218505859375f));
    HWY_ASSERT(ConvertScalarTo<T>(-0.033447265625f) <=
               ConvertScalarTo<T>(8.249282836914062E-5f));
    HWY_ASSERT(ConvertScalarTo<T>(23296.0f) > ConvertScalarTo<T>(192.0f));
    HWY_ASSERT(ConvertScalarTo<T>(41984.0f) >= ConvertScalarTo<T>(370.0f));

    TestSpecialFloatArithOperators(T());
  }
};

HWY_NOINLINE void TestAllSpecialFloat() {
  TestSpecialFloat test;
  test(float16_t());
  test(bfloat16_t());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(BaseTest);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllLimits);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllLowestHighest);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllType);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllIsSame);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllBitScan);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllPopCount);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllDivisor);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllScalarShr);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllMul128);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllEndian);
HWY_EXPORT_AND_TEST_P(BaseTest, TestAllSpecialFloat);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
