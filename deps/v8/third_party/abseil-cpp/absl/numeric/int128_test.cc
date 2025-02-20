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

#include <algorithm>
#include <limits>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/casts.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/hash/hash_testing.h"
#include "absl/meta/type_traits.h"
#include "absl/types/compare.h"

#define MAKE_INT128(HI, LO) absl::MakeInt128(static_cast<int64_t>(HI), LO)

namespace {

template <typename T>
class Uint128IntegerTraitsTest : public ::testing::Test {};
typedef ::testing::Types<bool, char, signed char, unsigned char, char16_t,
                         char32_t, wchar_t,
                         short,           // NOLINT(runtime/int)
                         unsigned short,  // NOLINT(runtime/int)
                         int, unsigned int,
                         long,                // NOLINT(runtime/int)
                         unsigned long,       // NOLINT(runtime/int)
                         long long,           // NOLINT(runtime/int)
                         unsigned long long>  // NOLINT(runtime/int)
    IntegerTypes;

template <typename T>
class Uint128FloatTraitsTest : public ::testing::Test {};
typedef ::testing::Types<float, double, long double> FloatingPointTypes;

TYPED_TEST_SUITE(Uint128IntegerTraitsTest, IntegerTypes);

TYPED_TEST(Uint128IntegerTraitsTest, ConstructAssignTest) {
  static_assert(std::is_constructible<absl::uint128, TypeParam>::value,
                "absl::uint128 must be constructible from TypeParam");
  static_assert(std::is_assignable<absl::uint128&, TypeParam>::value,
                "absl::uint128 must be assignable from TypeParam");
  static_assert(!std::is_assignable<TypeParam&, absl::uint128>::value,
                "TypeParam must not be assignable from absl::uint128");
}

TYPED_TEST_SUITE(Uint128FloatTraitsTest, FloatingPointTypes);

TYPED_TEST(Uint128FloatTraitsTest, ConstructAssignTest) {
  static_assert(std::is_constructible<absl::uint128, TypeParam>::value,
                "absl::uint128 must be constructible from TypeParam");
  static_assert(!std::is_assignable<absl::uint128&, TypeParam>::value,
                "absl::uint128 must not be assignable from TypeParam");
  static_assert(!std::is_assignable<TypeParam&, absl::uint128>::value,
                "TypeParam must not be assignable from absl::uint128");
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
// These type traits done separately as TYPED_TEST requires typeinfo, and not
// all platforms have this for __int128 even though they define the type.
TEST(Uint128, IntrinsicTypeTraitsTest) {
  static_assert(std::is_constructible<absl::uint128, __int128>::value,
                "absl::uint128 must be constructible from __int128");
  static_assert(std::is_assignable<absl::uint128&, __int128>::value,
                "absl::uint128 must be assignable from __int128");
  static_assert(!std::is_assignable<__int128&, absl::uint128>::value,
                "__int128 must not be assignable from absl::uint128");

  static_assert(std::is_constructible<absl::uint128, unsigned __int128>::value,
                "absl::uint128 must be constructible from unsigned __int128");
  static_assert(std::is_assignable<absl::uint128&, unsigned __int128>::value,
                "absl::uint128 must be assignable from unsigned __int128");
  static_assert(!std::is_assignable<unsigned __int128&, absl::uint128>::value,
                "unsigned __int128 must not be assignable from absl::uint128");
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

TEST(Uint128, TrivialTraitsTest) {
  static_assert(absl::is_trivially_default_constructible<absl::uint128>::value,
                "");
  static_assert(absl::is_trivially_copy_constructible<absl::uint128>::value,
                "");
  static_assert(absl::is_trivially_copy_assignable<absl::uint128>::value, "");
  static_assert(std::is_trivially_destructible<absl::uint128>::value, "");
}

TEST(Uint128, AllTests) {
  absl::uint128 zero = 0;
  absl::uint128 one = 1;
  absl::uint128 one_2arg = absl::MakeUint128(0, 1);
  absl::uint128 two = 2;
  absl::uint128 three = 3;
  absl::uint128 big = absl::MakeUint128(2000, 2);
  absl::uint128 big_minus_one = absl::MakeUint128(2000, 1);
  absl::uint128 bigger = absl::MakeUint128(2001, 1);
  absl::uint128 biggest = absl::Uint128Max();
  absl::uint128 high_low = absl::MakeUint128(1, 0);
  absl::uint128 low_high =
      absl::MakeUint128(0, std::numeric_limits<uint64_t>::max());
  EXPECT_LT(one, two);
  EXPECT_GT(two, one);
  EXPECT_LT(one, big);
  EXPECT_LT(one, big);
  EXPECT_EQ(one, one_2arg);
  EXPECT_NE(one, two);
  EXPECT_GT(big, one);
  EXPECT_GE(big, two);
  EXPECT_GE(big, big_minus_one);
  EXPECT_GT(big, big_minus_one);
  EXPECT_LT(big_minus_one, big);
  EXPECT_LE(big_minus_one, big);
  EXPECT_NE(big_minus_one, big);
  EXPECT_LT(big, biggest);
  EXPECT_LE(big, biggest);
  EXPECT_GT(biggest, big);
  EXPECT_GE(biggest, big);
  EXPECT_EQ(big, ~~big);
  EXPECT_EQ(one, one | one);
  EXPECT_EQ(big, big | big);
  EXPECT_EQ(one, one | zero);
  EXPECT_EQ(one, one & one);
  EXPECT_EQ(big, big & big);
  EXPECT_EQ(zero, one & zero);
  EXPECT_EQ(zero, big & ~big);
  EXPECT_EQ(zero, one ^ one);
  EXPECT_EQ(zero, big ^ big);
  EXPECT_EQ(one, one ^ zero);

  // Shift operators.
  EXPECT_EQ(big, big << 0);
  EXPECT_EQ(big, big >> 0);
  EXPECT_GT(big << 1, big);
  EXPECT_LT(big >> 1, big);
  EXPECT_EQ(big, (big << 10) >> 10);
  EXPECT_EQ(big, (big >> 1) << 1);
  EXPECT_EQ(one, (one << 80) >> 80);
  EXPECT_EQ(zero, (one >> 80) << 80);

  // Shift assignments.
  absl::uint128 big_copy = big;
  EXPECT_EQ(big << 0, big_copy <<= 0);
  big_copy = big;
  EXPECT_EQ(big >> 0, big_copy >>= 0);
  big_copy = big;
  EXPECT_EQ(big << 1, big_copy <<= 1);
  big_copy = big;
  EXPECT_EQ(big >> 1, big_copy >>= 1);
  big_copy = big;
  EXPECT_EQ(big << 10, big_copy <<= 10);
  big_copy = big;
  EXPECT_EQ(big >> 10, big_copy >>= 10);
  big_copy = big;
  EXPECT_EQ(big << 64, big_copy <<= 64);
  big_copy = big;
  EXPECT_EQ(big >> 64, big_copy >>= 64);
  big_copy = big;
  EXPECT_EQ(big << 73, big_copy <<= 73);
  big_copy = big;
  EXPECT_EQ(big >> 73, big_copy >>= 73);

  EXPECT_EQ(absl::Uint128High64(biggest), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(absl::Uint128Low64(biggest), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(zero + one, one);
  EXPECT_EQ(one + one, two);
  EXPECT_EQ(big_minus_one + one, big);
  EXPECT_EQ(one - one, zero);
  EXPECT_EQ(one - zero, one);
  EXPECT_EQ(zero - one, biggest);
  EXPECT_EQ(big - big, zero);
  EXPECT_EQ(big - one, big_minus_one);
  EXPECT_EQ(big + std::numeric_limits<uint64_t>::max(), bigger);
  EXPECT_EQ(biggest + 1, zero);
  EXPECT_EQ(zero - 1, biggest);
  EXPECT_EQ(high_low - one, low_high);
  EXPECT_EQ(low_high + one, high_low);
  EXPECT_EQ(absl::Uint128High64((absl::uint128(1) << 64) - 1), 0);
  EXPECT_EQ(absl::Uint128Low64((absl::uint128(1) << 64) - 1),
            std::numeric_limits<uint64_t>::max());
  EXPECT_TRUE(!!one);
  EXPECT_TRUE(!!high_low);
  EXPECT_FALSE(!!zero);
  EXPECT_FALSE(!one);
  EXPECT_FALSE(!high_low);
  EXPECT_TRUE(!zero);
  EXPECT_TRUE(zero == 0);       // NOLINT(readability/check)
  EXPECT_FALSE(zero != 0);      // NOLINT(readability/check)
  EXPECT_FALSE(one == 0);       // NOLINT(readability/check)
  EXPECT_TRUE(one != 0);        // NOLINT(readability/check)
  EXPECT_FALSE(high_low == 0);  // NOLINT(readability/check)
  EXPECT_TRUE(high_low != 0);   // NOLINT(readability/check)

  absl::uint128 test = zero;
  EXPECT_EQ(++test, one);
  EXPECT_EQ(test, one);
  EXPECT_EQ(test++, one);
  EXPECT_EQ(test, two);
  EXPECT_EQ(test -= 2, zero);
  EXPECT_EQ(test, zero);
  EXPECT_EQ(test += 2, two);
  EXPECT_EQ(test, two);
  EXPECT_EQ(--test, one);
  EXPECT_EQ(test, one);
  EXPECT_EQ(test--, one);
  EXPECT_EQ(test, zero);
  EXPECT_EQ(test |= three, three);
  EXPECT_EQ(test &= one, one);
  EXPECT_EQ(test ^= three, two);
  EXPECT_EQ(test >>= 1, one);
  EXPECT_EQ(test <<= 1, two);

  EXPECT_EQ(big, +big);
  EXPECT_EQ(two, +two);
  EXPECT_EQ(absl::Uint128Max(), +absl::Uint128Max());
  EXPECT_EQ(zero, +zero);

  EXPECT_EQ(big, -(-big));
  EXPECT_EQ(two, -((-one) - 1));
  EXPECT_EQ(absl::Uint128Max(), -one);
  EXPECT_EQ(zero, -zero);
}

TEST(Int128, RightShiftOfNegativeNumbers) {
  absl::int128 minus_six = -6;
  absl::int128 minus_three = -3;
  absl::int128 minus_two = -2;
  absl::int128 minus_one = -1;
  if ((-6 >> 1) == -3) {
    // Right shift is arithmetic (sign propagates)
    EXPECT_EQ(minus_six >> 1, minus_three);
    EXPECT_EQ(minus_six >> 2, minus_two);
    EXPECT_EQ(minus_six >> 65, minus_one);
  } else {
    // Right shift is logical (zeros shifted in at MSB)
    EXPECT_EQ(minus_six >> 1, absl::int128(absl::uint128(minus_six) >> 1));
    EXPECT_EQ(minus_six >> 2, absl::int128(absl::uint128(minus_six) >> 2));
    EXPECT_EQ(minus_six >> 65, absl::int128(absl::uint128(minus_six) >> 65));
  }
}

TEST(Uint128, ConversionTests) {
  EXPECT_TRUE(absl::MakeUint128(1, 0));

#ifdef ABSL_HAVE_INTRINSIC_INT128
  unsigned __int128 intrinsic =
      (static_cast<unsigned __int128>(0x3a5b76c209de76f6) << 64) +
      0x1f25e1d63a2b46c5;
  absl::uint128 custom =
      absl::MakeUint128(0x3a5b76c209de76f6, 0x1f25e1d63a2b46c5);

  EXPECT_EQ(custom, absl::uint128(intrinsic));
  EXPECT_EQ(custom, absl::uint128(static_cast<__int128>(intrinsic)));
  EXPECT_EQ(intrinsic, static_cast<unsigned __int128>(custom));
  EXPECT_EQ(intrinsic, static_cast<__int128>(custom));
#endif  // ABSL_HAVE_INTRINSIC_INT128

  // verify that an integer greater than 2**64 that can be stored precisely
  // inside a double is converted to a absl::uint128 without loss of
  // information.
  double precise_double = 0x530e * std::pow(2.0, 64.0) + 0xda74000000000000;
  absl::uint128 from_precise_double(precise_double);
  absl::uint128 from_precise_ints =
      absl::MakeUint128(0x530e, 0xda74000000000000);
  EXPECT_EQ(from_precise_double, from_precise_ints);
  EXPECT_DOUBLE_EQ(static_cast<double>(from_precise_ints), precise_double);

  double approx_double =
      static_cast<double>(0xffffeeeeddddcccc) * std::pow(2.0, 64.0) +
      static_cast<double>(0xbbbbaaaa99998888);
  absl::uint128 from_approx_double(approx_double);
  EXPECT_DOUBLE_EQ(static_cast<double>(from_approx_double), approx_double);

  double round_to_zero = 0.7;
  double round_to_five = 5.8;
  double round_to_nine = 9.3;
  EXPECT_EQ(static_cast<absl::uint128>(round_to_zero), 0);
  EXPECT_EQ(static_cast<absl::uint128>(round_to_five), 5);
  EXPECT_EQ(static_cast<absl::uint128>(round_to_nine), 9);

  absl::uint128 highest_precision_in_long_double =
      ~absl::uint128{} >> (128 - std::numeric_limits<long double>::digits);
  EXPECT_EQ(highest_precision_in_long_double,
            static_cast<absl::uint128>(
                static_cast<long double>(highest_precision_in_long_double)));
  // Apply a mask just to make sure all the bits are the right place.
  const absl::uint128 arbitrary_mask =
      absl::MakeUint128(0xa29f622677ded751, 0xf8ca66add076f468);
  EXPECT_EQ(highest_precision_in_long_double & arbitrary_mask,
            static_cast<absl::uint128>(static_cast<long double>(
                highest_precision_in_long_double & arbitrary_mask)));

  EXPECT_EQ(static_cast<absl::uint128>(-0.1L), 0);
}

TEST(Uint128, OperatorAssignReturnRef) {
  absl::uint128 v(1);
  (v += 4) -= 3;
  EXPECT_EQ(2, v);
}

TEST(Uint128, Multiply) {
  absl::uint128 a, b, c;

  // Zero test.
  a = 0;
  b = 0;
  c = a * b;
  EXPECT_EQ(0, c);

  // Max carries.
  a = absl::uint128(0) - 1;
  b = absl::uint128(0) - 1;
  c = a * b;
  EXPECT_EQ(1, c);

  // Self-operation with max carries.
  c = absl::uint128(0) - 1;
  c *= c;
  EXPECT_EQ(1, c);

  // 1-bit x 1-bit.
  for (int i = 0; i < 64; ++i) {
    for (int j = 0; j < 64; ++j) {
      a = absl::uint128(1) << i;
      b = absl::uint128(1) << j;
      c = a * b;
      EXPECT_EQ(absl::uint128(1) << (i + j), c);
    }
  }

  // Verified with dc.
  a = absl::MakeUint128(0xffffeeeeddddcccc, 0xbbbbaaaa99998888);
  b = absl::MakeUint128(0x7777666655554444, 0x3333222211110000);
  c = a * b;
  EXPECT_EQ(absl::MakeUint128(0x530EDA741C71D4C3, 0xBF25975319080000), c);
  EXPECT_EQ(0, c - b * a);
  EXPECT_EQ(a*a - b*b, (a+b) * (a-b));

  // Verified with dc.
  a = absl::MakeUint128(0x0123456789abcdef, 0xfedcba9876543210);
  b = absl::MakeUint128(0x02468ace13579bdf, 0xfdb97531eca86420);
  c = a * b;
  EXPECT_EQ(absl::MakeUint128(0x97a87f4f261ba3f2, 0x342d0bbf48948200), c);
  EXPECT_EQ(0, c - b * a);
  EXPECT_EQ(a*a - b*b, (a+b) * (a-b));
}

TEST(Uint128, AliasTests) {
  absl::uint128 x1 = absl::MakeUint128(1, 2);
  absl::uint128 x2 = absl::MakeUint128(2, 4);
  x1 += x1;
  EXPECT_EQ(x2, x1);

  absl::uint128 x3 = absl::MakeUint128(1, static_cast<uint64_t>(1) << 63);
  absl::uint128 x4 = absl::MakeUint128(3, 0);
  x3 += x3;
  EXPECT_EQ(x4, x3);
}

TEST(Uint128, DivideAndMod) {
  using std::swap;

  // a := q * b + r
  absl::uint128 a, b, q, r;

  // Zero test.
  a = 0;
  b = 123;
  q = a / b;
  r = a % b;
  EXPECT_EQ(0, q);
  EXPECT_EQ(0, r);

  a = absl::MakeUint128(0x530eda741c71d4c3, 0xbf25975319080000);
  q = absl::MakeUint128(0x4de2cab081, 0x14c34ab4676e4bab);
  b = absl::uint128(0x1110001);
  r = absl::uint128(0x3eb455);
  ASSERT_EQ(a, q * b + r);  // Sanity-check.

  absl::uint128 result_q, result_r;
  result_q = a / b;
  result_r = a % b;
  EXPECT_EQ(q, result_q);
  EXPECT_EQ(r, result_r);

  // Try the other way around.
  swap(q, b);
  result_q = a / b;
  result_r = a % b;
  EXPECT_EQ(q, result_q);
  EXPECT_EQ(r, result_r);
  // Restore.
  swap(b, q);

  // Dividend < divisor; result should be q:0 r:<dividend>.
  swap(a, b);
  result_q = a / b;
  result_r = a % b;
  EXPECT_EQ(0, result_q);
  EXPECT_EQ(a, result_r);
  // Try the other way around.
  swap(a, q);
  result_q = a / b;
  result_r = a % b;
  EXPECT_EQ(0, result_q);
  EXPECT_EQ(a, result_r);
  // Restore.
  swap(q, a);
  swap(b, a);

  // Try a large remainder.
  b = a / 2 + 1;
  absl::uint128 expected_r =
      absl::MakeUint128(0x29876d3a0e38ea61, 0xdf92cba98c83ffff);
  // Sanity checks.
  ASSERT_EQ(a / 2 - 1, expected_r);
  ASSERT_EQ(a, b + expected_r);
  result_q = a / b;
  result_r = a % b;
  EXPECT_EQ(1, result_q);
  EXPECT_EQ(expected_r, result_r);
}

TEST(Uint128, DivideAndModRandomInputs) {
  const int kNumIters = 1 << 18;
  std::minstd_rand random(testing::UnitTest::GetInstance()->random_seed());
  std::uniform_int_distribution<uint64_t> uniform_uint64;
  for (int i = 0; i < kNumIters; ++i) {
    const absl::uint128 a =
        absl::MakeUint128(uniform_uint64(random), uniform_uint64(random));
    const absl::uint128 b =
        absl::MakeUint128(uniform_uint64(random), uniform_uint64(random));
    if (b == 0) {
      continue;  // Avoid a div-by-zero.
    }
    const absl::uint128 q = a / b;
    const absl::uint128 r = a % b;
    ASSERT_EQ(a, b * q + r);
  }
}

TEST(Uint128, ConstexprTest) {
  constexpr absl::uint128 zero = absl::uint128();
  constexpr absl::uint128 one = 1;
  constexpr absl::uint128 minus_two = -2;
  EXPECT_EQ(zero, absl::uint128(0));
  EXPECT_EQ(one, absl::uint128(1));
  EXPECT_EQ(minus_two, absl::MakeUint128(-1, -2));
}

TEST(Uint128, NumericLimitsTest) {
  static_assert(std::numeric_limits<absl::uint128>::is_specialized, "");
  static_assert(!std::numeric_limits<absl::uint128>::is_signed, "");
  static_assert(std::numeric_limits<absl::uint128>::is_integer, "");
  EXPECT_EQ(static_cast<int>(128 * std::log10(2)),
            std::numeric_limits<absl::uint128>::digits10);
  EXPECT_EQ(0, std::numeric_limits<absl::uint128>::min());
  EXPECT_EQ(0, std::numeric_limits<absl::uint128>::lowest());
  EXPECT_EQ(absl::Uint128Max(), std::numeric_limits<absl::uint128>::max());
}

// Some arbitrary constant to test hashing. The first hex digits of pi.
constexpr absl::uint128 kPi = (absl::uint128(0x3243f6a8885a308d) << 64) |
                              absl::uint128(0x313198a2e0370734);

TEST(Uint128, Hash) {
#if defined(ABSL_HAVE_INTRINSIC_INT128)
  using Ext128 = unsigned __int128;
#endif
  // Make the tuple outside the EXPECT_TRUE because putting the #if inside the
  // macro argument is not ok.
  const auto values = std::make_tuple(
      // Some simple values
      absl::uint128{0}, absl::uint128{1}, ~absl::uint128{},
      // 64 bit limits
      absl::uint128{std::numeric_limits<int64_t>::max()},
      absl::uint128{std::numeric_limits<uint64_t>::max()} + 0,
      absl::uint128{std::numeric_limits<uint64_t>::max()} + 1,
      absl::uint128{std::numeric_limits<uint64_t>::max()} + 2,
      // Keeping high same
      absl::uint128{1} << 62, absl::uint128{1} << 63,
      // Keeping low same
      absl::uint128{1} << 64, absl::uint128{1} << 65,
      // 128 bit limits
      std::numeric_limits<absl::uint128>::max(),
      std::numeric_limits<absl::uint128>::max() - 1,
      std::numeric_limits<absl::uint128>::min() + 1,
      std::numeric_limits<absl::uint128>::min(),
      // arbitrary constant
      kPi
#if defined(ABSL_HAVE_INTRINSIC_INT128)
      // Same but with the intrinsic to verify that they match
      ,
      Ext128{0}, Ext128{1}, ~Ext128{},
      Ext128{std::numeric_limits<int64_t>::max()},
      Ext128{std::numeric_limits<uint64_t>::max()} + 0,
      Ext128{std::numeric_limits<uint64_t>::max()} + 1,
      Ext128{std::numeric_limits<uint64_t>::max()} + 2, Ext128{1} << 62,
      Ext128{1} << 63, Ext128{1} << 64, Ext128{1} << 65,
      std::numeric_limits<Ext128>::max(),
      std::numeric_limits<Ext128>::max() - 1,
      std::numeric_limits<Ext128>::min() + 1,
      std::numeric_limits<Ext128>::min(), static_cast<Ext128>(kPi)
#endif
  );
  EXPECT_TRUE(absl::VerifyTypeImplementsAbslHashCorrectly(values));
}


TEST(Int128Uint128, ConversionTest) {
  absl::int128 nonnegative_signed_values[] = {
      0,
      1,
      0xffeeddccbbaa9988,
      absl::MakeInt128(0x7766554433221100, 0),
      absl::MakeInt128(0x1234567890abcdef, 0xfedcba0987654321),
      absl::Int128Max()};
  for (absl::int128 value : nonnegative_signed_values) {
    EXPECT_EQ(value, absl::int128(absl::uint128(value)));

    absl::uint128 assigned_value;
    assigned_value = value;
    EXPECT_EQ(value, absl::int128(assigned_value));
  }

  absl::int128 negative_values[] = {
      -1, -0x1234567890abcdef,
      absl::MakeInt128(-0x5544332211ffeedd, 0),
      -absl::MakeInt128(0x76543210fedcba98, 0xabcdef0123456789)};
  for (absl::int128 value : negative_values) {
    EXPECT_EQ(absl::uint128(-value), -absl::uint128(value));

    absl::uint128 assigned_value;
    assigned_value = value;
    EXPECT_EQ(absl::uint128(-value), -assigned_value);
  }
}

template <typename T>
class Int128IntegerTraitsTest : public ::testing::Test {};

TYPED_TEST_SUITE(Int128IntegerTraitsTest, IntegerTypes);

TYPED_TEST(Int128IntegerTraitsTest, ConstructAssignTest) {
  static_assert(std::is_constructible<absl::int128, TypeParam>::value,
                "absl::int128 must be constructible from TypeParam");
  static_assert(std::is_assignable<absl::int128&, TypeParam>::value,
                "absl::int128 must be assignable from TypeParam");
  static_assert(!std::is_assignable<TypeParam&, absl::int128>::value,
                "TypeParam must not be assignable from absl::int128");
}

template <typename T>
class Int128FloatTraitsTest : public ::testing::Test {};

TYPED_TEST_SUITE(Int128FloatTraitsTest, FloatingPointTypes);

TYPED_TEST(Int128FloatTraitsTest, ConstructAssignTest) {
  static_assert(std::is_constructible<absl::int128, TypeParam>::value,
                "absl::int128 must be constructible from TypeParam");
  static_assert(!std::is_assignable<absl::int128&, TypeParam>::value,
                "absl::int128 must not be assignable from TypeParam");
  static_assert(!std::is_assignable<TypeParam&, absl::int128>::value,
                "TypeParam must not be assignable from absl::int128");
}

#ifdef ABSL_HAVE_INTRINSIC_INT128
// These type traits done separately as TYPED_TEST requires typeinfo, and not
// all platforms have this for __int128 even though they define the type.
TEST(Int128, IntrinsicTypeTraitsTest) {
  static_assert(std::is_constructible<absl::int128, __int128>::value,
                "absl::int128 must be constructible from __int128");
  static_assert(std::is_assignable<absl::int128&, __int128>::value,
                "absl::int128 must be assignable from __int128");
  static_assert(!std::is_assignable<__int128&, absl::int128>::value,
                "__int128 must not be assignable from absl::int128");

  static_assert(std::is_constructible<absl::int128, unsigned __int128>::value,
                "absl::int128 must be constructible from unsigned __int128");
  static_assert(!std::is_assignable<absl::int128&, unsigned __int128>::value,
                "absl::int128 must be assignable from unsigned __int128");
  static_assert(!std::is_assignable<unsigned __int128&, absl::int128>::value,
                "unsigned __int128 must not be assignable from absl::int128");
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

TEST(Int128, TrivialTraitsTest) {
  static_assert(absl::is_trivially_default_constructible<absl::int128>::value,
                "");
  static_assert(absl::is_trivially_copy_constructible<absl::int128>::value, "");
  static_assert(absl::is_trivially_copy_assignable<absl::int128>::value, "");
  static_assert(std::is_trivially_destructible<absl::int128>::value, "");
}

TEST(Int128, BoolConversionTest) {
  EXPECT_FALSE(absl::int128(0));
  for (int i = 0; i < 64; ++i) {
    EXPECT_TRUE(absl::MakeInt128(0, uint64_t{1} << i));
  }
  for (int i = 0; i < 63; ++i) {
    EXPECT_TRUE(absl::MakeInt128(int64_t{1} << i, 0));
  }
  EXPECT_TRUE(absl::Int128Min());

  EXPECT_EQ(absl::int128(1), absl::int128(true));
  EXPECT_EQ(absl::int128(0), absl::int128(false));
}

template <typename T>
class Int128IntegerConversionTest : public ::testing::Test {};

TYPED_TEST_SUITE(Int128IntegerConversionTest, IntegerTypes);

TYPED_TEST(Int128IntegerConversionTest, RoundTripTest) {
  EXPECT_EQ(TypeParam{0}, static_cast<TypeParam>(absl::int128(0)));
  EXPECT_EQ(std::numeric_limits<TypeParam>::min(),
            static_cast<TypeParam>(
                absl::int128(std::numeric_limits<TypeParam>::min())));
  EXPECT_EQ(std::numeric_limits<TypeParam>::max(),
            static_cast<TypeParam>(
                absl::int128(std::numeric_limits<TypeParam>::max())));
}

template <typename T>
class Int128FloatConversionTest : public ::testing::Test {};

TYPED_TEST_SUITE(Int128FloatConversionTest, FloatingPointTypes);

TYPED_TEST(Int128FloatConversionTest, ConstructAndCastTest) {
  // Conversions where the floating point values should be exactly the same.
  // 0x9f5b is a randomly chosen small value.
  for (int i = 0; i < 110; ++i) {  // 110 = 126 - #bits in 0x9f5b
    SCOPED_TRACE(::testing::Message() << "i = " << i);

    TypeParam float_value = std::ldexp(static_cast<TypeParam>(0x9f5b), i);
    absl::int128 int_value = absl::int128(0x9f5b) << i;

    EXPECT_EQ(float_value, static_cast<TypeParam>(int_value));
    EXPECT_EQ(-float_value, static_cast<TypeParam>(-int_value));
    EXPECT_EQ(int_value, absl::int128(float_value));
    EXPECT_EQ(-int_value, absl::int128(-float_value));
  }

  // Round trip conversions with a small sample of randomly generated uint64_t
  // values (less than int64_t max so that value * 2^64 fits into int128).
  uint64_t values[] = {0x6d4492c24fb86199, 0x26ead65e4cb359b5,
                       0x2c43407433ba3fd1, 0x3b574ec668df6b55,
                       0x1c750e55a29f4f0f};
  for (uint64_t value : values) {
    for (int i = 0; i <= 64; ++i) {
      SCOPED_TRACE(::testing::Message()
                   << "value = " << value << "; i = " << i);

      TypeParam fvalue = std::ldexp(static_cast<TypeParam>(value), i);
      EXPECT_DOUBLE_EQ(fvalue, static_cast<TypeParam>(absl::int128(fvalue)));
      EXPECT_DOUBLE_EQ(-fvalue, static_cast<TypeParam>(-absl::int128(fvalue)));
      EXPECT_DOUBLE_EQ(-fvalue, static_cast<TypeParam>(absl::int128(-fvalue)));
      EXPECT_DOUBLE_EQ(fvalue, static_cast<TypeParam>(-absl::int128(-fvalue)));
    }
  }

  // Round trip conversions with a small sample of random large positive values.
  absl::int128 large_values[] = {
      absl::MakeInt128(0x5b0640d96c7b3d9f, 0xb7a7189e51d18622),
      absl::MakeInt128(0x34bed042c6f65270, 0x73b236570669a089),
      absl::MakeInt128(0x43deba9e6da12724, 0xf7f0f83da686797d),
      absl::MakeInt128(0x71e8d383be4e5589, 0x75c3f96fb00752b6)};
  for (absl::int128 value : large_values) {
    // Make value have as many significant bits as can be represented by
    // the mantissa, also making sure the highest and lowest bit in the range
    // are set.
    value >>= (127 - std::numeric_limits<TypeParam>::digits);
    value |= absl::int128(1) << (std::numeric_limits<TypeParam>::digits - 1);
    value |= 1;
    for (int i = 0; i < 127 - std::numeric_limits<TypeParam>::digits; ++i) {
      absl::int128 int_value = value << i;
      EXPECT_EQ(int_value,
                static_cast<absl::int128>(static_cast<TypeParam>(int_value)));
      EXPECT_EQ(-int_value,
                static_cast<absl::int128>(static_cast<TypeParam>(-int_value)));
    }
  }

  // Small sample of checks that rounding is toward zero
  EXPECT_EQ(0, absl::int128(TypeParam(0.1)));
  EXPECT_EQ(17, absl::int128(TypeParam(17.8)));
  EXPECT_EQ(0, absl::int128(TypeParam(-0.8)));
  EXPECT_EQ(-53, absl::int128(TypeParam(-53.1)));
  EXPECT_EQ(0, absl::int128(TypeParam(0.5)));
  EXPECT_EQ(0, absl::int128(TypeParam(-0.5)));
  TypeParam just_lt_one = std::nexttoward(TypeParam(1), TypeParam(0));
  EXPECT_EQ(0, absl::int128(just_lt_one));
  TypeParam just_gt_minus_one = std::nexttoward(TypeParam(-1), TypeParam(0));
  EXPECT_EQ(0, absl::int128(just_gt_minus_one));

  // Check limits
  EXPECT_DOUBLE_EQ(std::ldexp(static_cast<TypeParam>(1), 127),
                   static_cast<TypeParam>(absl::Int128Max()));
  EXPECT_DOUBLE_EQ(-std::ldexp(static_cast<TypeParam>(1), 127),
                   static_cast<TypeParam>(absl::Int128Min()));
}

TEST(Int128, FactoryTest) {
  EXPECT_EQ(absl::int128(-1), absl::MakeInt128(-1, -1));
  EXPECT_EQ(absl::int128(-31), absl::MakeInt128(-1, -31));
  EXPECT_EQ(absl::int128(std::numeric_limits<int64_t>::min()),
            absl::MakeInt128(-1, std::numeric_limits<int64_t>::min()));
  EXPECT_EQ(absl::int128(0), absl::MakeInt128(0, 0));
  EXPECT_EQ(absl::int128(1), absl::MakeInt128(0, 1));
  EXPECT_EQ(absl::int128(std::numeric_limits<int64_t>::max()),
            absl::MakeInt128(0, std::numeric_limits<int64_t>::max()));
}

TEST(Int128, HighLowTest) {
  struct HighLowPair {
    int64_t high;
    uint64_t low;
  };
  HighLowPair values[]{{0, 0}, {0, 1}, {1, 0}, {123, 456}, {-654, 321}};
  for (const HighLowPair& pair : values) {
    absl::int128 value = absl::MakeInt128(pair.high, pair.low);
    EXPECT_EQ(pair.low, absl::Int128Low64(value));
    EXPECT_EQ(pair.high, absl::Int128High64(value));
  }
}

TEST(Int128, LimitsTest) {
  EXPECT_EQ(absl::MakeInt128(0x7fffffffffffffff, 0xffffffffffffffff),
            absl::Int128Max());
  EXPECT_EQ(absl::Int128Max(), ~absl::Int128Min());
}

#if defined(ABSL_HAVE_INTRINSIC_INT128)
TEST(Int128, IntrinsicConversionTest) {
  __int128 intrinsic =
      (static_cast<__int128>(0x3a5b76c209de76f6) << 64) + 0x1f25e1d63a2b46c5;
  absl::int128 custom =
      absl::MakeInt128(0x3a5b76c209de76f6, 0x1f25e1d63a2b46c5);

  EXPECT_EQ(custom, absl::int128(intrinsic));
  EXPECT_EQ(intrinsic, static_cast<__int128>(custom));
}
#endif  // ABSL_HAVE_INTRINSIC_INT128

TEST(Int128, ConstexprTest) {
  constexpr absl::int128 zero = absl::int128();
  constexpr absl::int128 one = 1;
  constexpr absl::int128 minus_two = -2;
  constexpr absl::int128 min = absl::Int128Min();
  constexpr absl::int128 max = absl::Int128Max();
  EXPECT_EQ(zero, absl::int128(0));
  EXPECT_EQ(one, absl::int128(1));
  EXPECT_EQ(minus_two, absl::MakeInt128(-1, -2));
  EXPECT_GT(max, one);
  EXPECT_LT(min, minus_two);
}

TEST(Int128, ComparisonTest) {
  struct TestCase {
    absl::int128 smaller;
    absl::int128 larger;
  };
  TestCase cases[] = {
      {absl::int128(0), absl::int128(123)},
      {absl::MakeInt128(-12, 34), absl::MakeInt128(12, 34)},
      {absl::MakeInt128(1, 1000), absl::MakeInt128(1000, 1)},
      {absl::MakeInt128(-1000, 1000), absl::MakeInt128(-1, 1)},
  };
  for (const TestCase& pair : cases) {
    SCOPED_TRACE(::testing::Message() << "pair.smaller = " << pair.smaller
                                      << "; pair.larger = " << pair.larger);

    EXPECT_TRUE(pair.smaller == pair.smaller);  // NOLINT(readability/check)
    EXPECT_TRUE(pair.larger == pair.larger);    // NOLINT(readability/check)
    EXPECT_FALSE(pair.smaller == pair.larger);  // NOLINT(readability/check)

    EXPECT_TRUE(pair.smaller != pair.larger);    // NOLINT(readability/check)
    EXPECT_FALSE(pair.smaller != pair.smaller);  // NOLINT(readability/check)
    EXPECT_FALSE(pair.larger != pair.larger);    // NOLINT(readability/check)

    EXPECT_TRUE(pair.smaller < pair.larger);   // NOLINT(readability/check)
    EXPECT_FALSE(pair.larger < pair.smaller);  // NOLINT(readability/check)

    EXPECT_TRUE(pair.larger > pair.smaller);   // NOLINT(readability/check)
    EXPECT_FALSE(pair.smaller > pair.larger);  // NOLINT(readability/check)

    EXPECT_TRUE(pair.smaller <= pair.larger);   // NOLINT(readability/check)
    EXPECT_FALSE(pair.larger <= pair.smaller);  // NOLINT(readability/check)
    EXPECT_TRUE(pair.smaller <= pair.smaller);  // NOLINT(readability/check)
    EXPECT_TRUE(pair.larger <= pair.larger);    // NOLINT(readability/check)

    EXPECT_TRUE(pair.larger >= pair.smaller);   // NOLINT(readability/check)
    EXPECT_FALSE(pair.smaller >= pair.larger);  // NOLINT(readability/check)
    EXPECT_TRUE(pair.smaller >= pair.smaller);  // NOLINT(readability/check)
    EXPECT_TRUE(pair.larger >= pair.larger);    // NOLINT(readability/check)

#ifdef __cpp_impl_three_way_comparison
    EXPECT_EQ(pair.smaller <=> pair.larger, absl::strong_ordering::less);
    EXPECT_EQ(pair.larger <=> pair.smaller, absl::strong_ordering::greater);
    EXPECT_EQ(pair.smaller <=> pair.smaller, absl::strong_ordering::equal);
    EXPECT_EQ(pair.larger <=> pair.larger, absl::strong_ordering::equal);
#endif
  }
}

TEST(Int128, UnaryPlusTest) {
  int64_t values64[] = {0, 1, 12345, 0x4000000000000000,
                        std::numeric_limits<int64_t>::max()};
  for (int64_t value : values64) {
    SCOPED_TRACE(::testing::Message() << "value = " << value);

    EXPECT_EQ(absl::int128(value), +absl::int128(value));
    EXPECT_EQ(absl::int128(-value), +absl::int128(-value));
    EXPECT_EQ(absl::MakeInt128(value, 0), +absl::MakeInt128(value, 0));
    EXPECT_EQ(absl::MakeInt128(-value, 0), +absl::MakeInt128(-value, 0));
  }
}

TEST(Int128, UnaryNegationTest) {
  int64_t values64[] = {0, 1, 12345, 0x4000000000000000,
                        std::numeric_limits<int64_t>::max()};
  for (int64_t value : values64) {
    SCOPED_TRACE(::testing::Message() << "value = " << value);

    EXPECT_EQ(absl::int128(-value), -absl::int128(value));
    EXPECT_EQ(absl::int128(value), -absl::int128(-value));
    EXPECT_EQ(absl::MakeInt128(-value, 0), -absl::MakeInt128(value, 0));
    EXPECT_EQ(absl::MakeInt128(value, 0), -absl::MakeInt128(-value, 0));
  }
}

TEST(Int128, LogicalNotTest) {
  EXPECT_TRUE(!absl::int128(0));
  for (int i = 0; i < 64; ++i) {
    EXPECT_FALSE(!absl::MakeInt128(0, uint64_t{1} << i));
  }
  for (int i = 0; i < 63; ++i) {
    EXPECT_FALSE(!absl::MakeInt128(int64_t{1} << i, 0));
  }
}

TEST(Int128, AdditionSubtractionTest) {
  // 64 bit pairs that will not cause overflow / underflow. These test negative
  // carry; positive carry must be checked separately.
  std::pair<int64_t, int64_t> cases[]{
      {0, 0},                              // 0, 0
      {0, 2945781290834},                  // 0, +
      {1908357619234, 0},                  // +, 0
      {0, -1204895918245},                 // 0, -
      {-2957928523560, 0},                 // -, 0
      {89023982312461, 98346012567134},    // +, +
      {-63454234568239, -23456235230773},  // -, -
      {98263457263502, -21428561935925},   // +, -
      {-88235237438467, 15923659234573},   // -, +
  };
  for (const auto& pair : cases) {
    SCOPED_TRACE(::testing::Message()
                 << "pair = {" << pair.first << ", " << pair.second << '}');

    EXPECT_EQ(absl::int128(pair.first + pair.second),
              absl::int128(pair.first) + absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.second + pair.first),
              absl::int128(pair.second) += absl::int128(pair.first));

    EXPECT_EQ(absl::int128(pair.first - pair.second),
              absl::int128(pair.first) - absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.second - pair.first),
              absl::int128(pair.second) -= absl::int128(pair.first));

    EXPECT_EQ(
        absl::MakeInt128(pair.second + pair.first, 0),
        absl::MakeInt128(pair.second, 0) + absl::MakeInt128(pair.first, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first + pair.second, 0),
        absl::MakeInt128(pair.first, 0) += absl::MakeInt128(pair.second, 0));

    EXPECT_EQ(
        absl::MakeInt128(pair.second - pair.first, 0),
        absl::MakeInt128(pair.second, 0) - absl::MakeInt128(pair.first, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first - pair.second, 0),
        absl::MakeInt128(pair.first, 0) -= absl::MakeInt128(pair.second, 0));
  }

  // check positive carry
  EXPECT_EQ(absl::MakeInt128(31, 0),
            absl::MakeInt128(20, 1) +
                absl::MakeInt128(10, std::numeric_limits<uint64_t>::max()));
}

TEST(Int128, IncrementDecrementTest) {
  absl::int128 value = 0;
  EXPECT_EQ(0, value++);
  EXPECT_EQ(1, value);
  EXPECT_EQ(1, value--);
  EXPECT_EQ(0, value);
  EXPECT_EQ(-1, --value);
  EXPECT_EQ(-1, value);
  EXPECT_EQ(0, ++value);
  EXPECT_EQ(0, value);
}

TEST(Int128, MultiplicationTest) {
  // 1 bit x 1 bit, and negative combinations
  for (int i = 0; i < 64; ++i) {
    for (int j = 0; j < 127 - i; ++j) {
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      absl::int128 a = absl::int128(1) << i;
      absl::int128 b = absl::int128(1) << j;
      absl::int128 c = absl::int128(1) << (i + j);

      EXPECT_EQ(c, a * b);
      EXPECT_EQ(-c, -a * b);
      EXPECT_EQ(-c, a * -b);
      EXPECT_EQ(c, -a * -b);

      EXPECT_EQ(c, absl::int128(a) *= b);
      EXPECT_EQ(-c, absl::int128(-a) *= b);
      EXPECT_EQ(-c, absl::int128(a) *= -b);
      EXPECT_EQ(c, absl::int128(-a) *= -b);
    }
  }

  // Pairs of random values that will not overflow signed 64-bit multiplication
  std::pair<int64_t, int64_t> small_values[] = {
      {0x5e61, 0xf29f79ca14b4},    // +, +
      {0x3e033b, -0x612c0ee549},   // +, -
      {-0x052ce7e8, 0x7c728f0f},   // -, +
      {-0x3af7054626, -0xfb1e1d},  // -, -
  };
  for (const std::pair<int64_t, int64_t>& pair : small_values) {
    SCOPED_TRACE(::testing::Message()
                 << "pair = {" << pair.first << ", " << pair.second << '}');

    EXPECT_EQ(absl::int128(pair.first * pair.second),
              absl::int128(pair.first) * absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.first * pair.second),
              absl::int128(pair.first) *= absl::int128(pair.second));

    EXPECT_EQ(absl::MakeInt128(pair.first * pair.second, 0),
              absl::MakeInt128(pair.first, 0) * absl::int128(pair.second));
    EXPECT_EQ(absl::MakeInt128(pair.first * pair.second, 0),
              absl::MakeInt128(pair.first, 0) *= absl::int128(pair.second));
  }

  // Pairs of positive random values that will not overflow 64-bit
  // multiplication and can be left shifted by 32 without overflow
  std::pair<int64_t, int64_t> small_values2[] = {
      {0x1bb0a110, 0x31487671},
      {0x4792784e, 0x28add7d7},
      {0x7b66553a, 0x11dff8ef},
  };
  for (const std::pair<int64_t, int64_t>& pair : small_values2) {
    SCOPED_TRACE(::testing::Message()
                 << "pair = {" << pair.first << ", " << pair.second << '}');

    absl::int128 a = absl::int128(pair.first << 32);
    absl::int128 b = absl::int128(pair.second << 32);
    absl::int128 c = absl::MakeInt128(pair.first * pair.second, 0);

    EXPECT_EQ(c, a * b);
    EXPECT_EQ(-c, -a * b);
    EXPECT_EQ(-c, a * -b);
    EXPECT_EQ(c, -a * -b);

    EXPECT_EQ(c, absl::int128(a) *= b);
    EXPECT_EQ(-c, absl::int128(-a) *= b);
    EXPECT_EQ(-c, absl::int128(a) *= -b);
    EXPECT_EQ(c, absl::int128(-a) *= -b);
  }

  // check 0, 1, and -1 behavior with large values
  absl::int128 large_values[] = {
      {absl::MakeInt128(0xd66f061af02d0408, 0x727d2846cb475b53)},
      {absl::MakeInt128(0x27b8d5ed6104452d, 0x03f8a33b0ee1df4f)},
      {-absl::MakeInt128(0x621b6626b9e8d042, 0x27311ac99df00938)},
      {-absl::MakeInt128(0x34e0656f1e95fb60, 0x4281cfd731257a47)},
  };
  for (absl::int128 value : large_values) {
    EXPECT_EQ(0, 0 * value);
    EXPECT_EQ(0, value * 0);
    EXPECT_EQ(0, absl::int128(0) *= value);
    EXPECT_EQ(0, value *= 0);

    EXPECT_EQ(value, 1 * value);
    EXPECT_EQ(value, value * 1);
    EXPECT_EQ(value, absl::int128(1) *= value);
    EXPECT_EQ(value, value *= 1);

    EXPECT_EQ(-value, -1 * value);
    EXPECT_EQ(-value, value * -1);
    EXPECT_EQ(-value, absl::int128(-1) *= value);
    EXPECT_EQ(-value, value *= -1);
  }

  // Manually calculated random large value cases
  EXPECT_EQ(absl::MakeInt128(0xcd0efd3442219bb, 0xde47c05bcd9df6e1),
            absl::MakeInt128(0x7c6448, 0x3bc4285c47a9d253) * 0x1a6037537b);
  EXPECT_EQ(-absl::MakeInt128(0x1f8f149850b1e5e6, 0x1e50d6b52d272c3e),
            -absl::MakeInt128(0x23, 0x2e68a513ca1b8859) * 0xe5a434cd14866e);
  EXPECT_EQ(-absl::MakeInt128(0x55cae732029d1fce, 0xca6474b6423263e4),
            0xa9b98a8ddf66bc * -absl::MakeInt128(0x81, 0x672e58231e2469d7));
  EXPECT_EQ(absl::MakeInt128(0x19c8b7620b507dc4, 0xfec042b71a5f29a4),
            -0x3e39341147 * -absl::MakeInt128(0x6a14b2, 0x5ed34cca42327b3c));

  EXPECT_EQ(absl::MakeInt128(0xcd0efd3442219bb, 0xde47c05bcd9df6e1),
            absl::MakeInt128(0x7c6448, 0x3bc4285c47a9d253) *= 0x1a6037537b);
  EXPECT_EQ(-absl::MakeInt128(0x1f8f149850b1e5e6, 0x1e50d6b52d272c3e),
            -absl::MakeInt128(0x23, 0x2e68a513ca1b8859) *= 0xe5a434cd14866e);
  EXPECT_EQ(-absl::MakeInt128(0x55cae732029d1fce, 0xca6474b6423263e4),
            absl::int128(0xa9b98a8ddf66bc) *=
            -absl::MakeInt128(0x81, 0x672e58231e2469d7));
  EXPECT_EQ(absl::MakeInt128(0x19c8b7620b507dc4, 0xfec042b71a5f29a4),
            absl::int128(-0x3e39341147) *=
            -absl::MakeInt128(0x6a14b2, 0x5ed34cca42327b3c));
}

TEST(Int128, DivisionAndModuloTest) {
  // Check against 64 bit division and modulo operators with a sample of
  // randomly generated pairs.
  std::pair<int64_t, int64_t> small_pairs[] = {
      {0x15f2a64138, 0x67da05},    {0x5e56d194af43045f, 0xcf1543fb99},
      {0x15e61ed052036a, -0xc8e6}, {0x88125a341e85, -0xd23fb77683},
      {-0xc06e20, 0x5a},           {-0x4f100219aea3e85d, 0xdcc56cb4efe993},
      {-0x168d629105, -0xa7},      {-0x7b44e92f03ab2375, -0x6516},
  };
  for (const std::pair<int64_t, int64_t>& pair : small_pairs) {
    SCOPED_TRACE(::testing::Message()
                 << "pair = {" << pair.first << ", " << pair.second << '}');

    absl::int128 dividend = pair.first;
    absl::int128 divisor = pair.second;
    int64_t quotient = pair.first / pair.second;
    int64_t remainder = pair.first % pair.second;

    EXPECT_EQ(quotient, dividend / divisor);
    EXPECT_EQ(quotient, absl::int128(dividend) /= divisor);
    EXPECT_EQ(remainder, dividend % divisor);
    EXPECT_EQ(remainder, absl::int128(dividend) %= divisor);
  }

  // Test behavior with 0, 1, and -1 with a sample of randomly generated large
  // values.
  absl::int128 values[] = {
      absl::MakeInt128(0x63d26ee688a962b2, 0x9e1411abda5c1d70),
      absl::MakeInt128(0x152f385159d6f986, 0xbf8d48ef63da395d),
      -absl::MakeInt128(0x3098d7567030038c, 0x14e7a8a098dc2164),
      -absl::MakeInt128(0x49a037aca35c809f, 0xa6a87525480ef330),
  };
  for (absl::int128 value : values) {
    SCOPED_TRACE(::testing::Message() << "value = " << value);

    EXPECT_EQ(0, 0 / value);
    EXPECT_EQ(0, absl::int128(0) /= value);
    EXPECT_EQ(0, 0 % value);
    EXPECT_EQ(0, absl::int128(0) %= value);

    EXPECT_EQ(value, value / 1);
    EXPECT_EQ(value, absl::int128(value) /= 1);
    EXPECT_EQ(0, value % 1);
    EXPECT_EQ(0, absl::int128(value) %= 1);

    EXPECT_EQ(-value, value / -1);
    EXPECT_EQ(-value, absl::int128(value) /= -1);
    EXPECT_EQ(0, value % -1);
    EXPECT_EQ(0, absl::int128(value) %= -1);
  }

  // Min and max values
  EXPECT_EQ(0, absl::Int128Max() / absl::Int128Min());
  EXPECT_EQ(absl::Int128Max(), absl::Int128Max() % absl::Int128Min());
  EXPECT_EQ(-1, absl::Int128Min() / absl::Int128Max());
  EXPECT_EQ(-1, absl::Int128Min() % absl::Int128Max());

  // Power of two division and modulo of random large dividends
  absl::int128 positive_values[] = {
      absl::MakeInt128(0x21e1a1cc69574620, 0xe7ac447fab2fc869),
      absl::MakeInt128(0x32c2ff3ab89e66e8, 0x03379a613fd1ce74),
      absl::MakeInt128(0x6f32ca786184dcaf, 0x046f9c9ecb3a9ce1),
      absl::MakeInt128(0x1aeb469dd990e0ee, 0xda2740f243cd37eb),
  };
  for (absl::int128 value : positive_values) {
    for (int i = 0; i < 127; ++i) {
      SCOPED_TRACE(::testing::Message()
                   << "value = " << value << "; i = " << i);
      absl::int128 power_of_two = absl::int128(1) << i;

      EXPECT_EQ(value >> i, value / power_of_two);
      EXPECT_EQ(value >> i, absl::int128(value) /= power_of_two);
      EXPECT_EQ(value & (power_of_two - 1), value % power_of_two);
      EXPECT_EQ(value & (power_of_two - 1),
                absl::int128(value) %= power_of_two);
    }
  }

  // Manually calculated cases with random large dividends
  struct DivisionModCase {
    absl::int128 dividend;
    absl::int128 divisor;
    absl::int128 quotient;
    absl::int128 remainder;
  };
  DivisionModCase manual_cases[] = {
      {absl::MakeInt128(0x6ada48d489007966, 0x3c9c5c98150d5d69),
       absl::MakeInt128(0x8bc308fb, 0x8cb9cc9a3b803344), 0xc3b87e08,
       absl::MakeInt128(0x1b7db5e1, 0xd9eca34b7af04b49)},
      {absl::MakeInt128(0xd6946511b5b, 0x4886c5c96546bf5f),
       -absl::MakeInt128(0x263b, 0xfd516279efcfe2dc), -0x59cbabf0,
       absl::MakeInt128(0x622, 0xf462909155651d1f)},
      {-absl::MakeInt128(0x33db734f9e8d1399, 0x8447ac92482bca4d), 0x37495078240,
       -absl::MakeInt128(0xf01f1, 0xbc0368bf9a77eae8), -0x21a508f404d},
      {-absl::MakeInt128(0x13f837b409a07e7d, 0x7fc8e248a7d73560), -0x1b9f,
       absl::MakeInt128(0xb9157556d724, 0xb14f635714d7563e), -0x1ade},
  };
  for (const DivisionModCase test_case : manual_cases) {
    EXPECT_EQ(test_case.quotient, test_case.dividend / test_case.divisor);
    EXPECT_EQ(test_case.quotient,
              absl::int128(test_case.dividend) /= test_case.divisor);
    EXPECT_EQ(test_case.remainder, test_case.dividend % test_case.divisor);
    EXPECT_EQ(test_case.remainder,
              absl::int128(test_case.dividend) %= test_case.divisor);
  }
}

TEST(Int128, BitwiseLogicTest) {
  EXPECT_EQ(absl::int128(-1), ~absl::int128(0));

  absl::int128 values[]{
      0, -1, 0xde400bee05c3ff6b, absl::MakeInt128(0x7f32178dd81d634a, 0),
      absl::MakeInt128(0xaf539057055613a9, 0x7d104d7d946c2e4d)};
  for (absl::int128 value : values) {
    EXPECT_EQ(value, ~~value);

    EXPECT_EQ(value, value | value);
    EXPECT_EQ(value, value & value);
    EXPECT_EQ(0, value ^ value);

    EXPECT_EQ(value, absl::int128(value) |= value);
    EXPECT_EQ(value, absl::int128(value) &= value);
    EXPECT_EQ(0, absl::int128(value) ^= value);

    EXPECT_EQ(value, value | 0);
    EXPECT_EQ(0, value & 0);
    EXPECT_EQ(value, value ^ 0);

    EXPECT_EQ(absl::int128(-1), value | absl::int128(-1));
    EXPECT_EQ(value, value & absl::int128(-1));
    EXPECT_EQ(~value, value ^ absl::int128(-1));
  }

  // small sample of randomly generated int64_t's
  std::pair<int64_t, int64_t> pairs64[]{
      {0x7f86797f5e991af4, 0x1ee30494fb007c97},
      {0x0b278282bacf01af, 0x58780e0a57a49e86},
      {0x059f266ccb93a666, 0x3d5b731bae9286f5},
      {0x63c0c4820f12108c, 0x58166713c12e1c3a},
      {0x381488bb2ed2a66e, 0x2220a3eb76a3698c},
      {0x2a0a0dfb81e06f21, 0x4b60585927f5523c},
      {0x555b1c3a03698537, 0x25478cd19d8e53cb},
      {0x4750f6f27d779225, 0x16397553c6ff05fc},
  };
  for (const std::pair<int64_t, int64_t>& pair : pairs64) {
    SCOPED_TRACE(::testing::Message()
                 << "pair = {" << pair.first << ", " << pair.second << '}');

    EXPECT_EQ(absl::MakeInt128(~pair.first, ~pair.second),
              ~absl::MakeInt128(pair.first, pair.second));

    EXPECT_EQ(absl::int128(pair.first & pair.second),
              absl::int128(pair.first) & absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.first | pair.second),
              absl::int128(pair.first) | absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.first ^ pair.second),
              absl::int128(pair.first) ^ absl::int128(pair.second));

    EXPECT_EQ(absl::int128(pair.first & pair.second),
              absl::int128(pair.first) &= absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.first | pair.second),
              absl::int128(pair.first) |= absl::int128(pair.second));
    EXPECT_EQ(absl::int128(pair.first ^ pair.second),
              absl::int128(pair.first) ^= absl::int128(pair.second));

    EXPECT_EQ(
        absl::MakeInt128(pair.first & pair.second, 0),
        absl::MakeInt128(pair.first, 0) & absl::MakeInt128(pair.second, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first | pair.second, 0),
        absl::MakeInt128(pair.first, 0) | absl::MakeInt128(pair.second, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first ^ pair.second, 0),
        absl::MakeInt128(pair.first, 0) ^ absl::MakeInt128(pair.second, 0));

    EXPECT_EQ(
        absl::MakeInt128(pair.first & pair.second, 0),
        absl::MakeInt128(pair.first, 0) &= absl::MakeInt128(pair.second, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first | pair.second, 0),
        absl::MakeInt128(pair.first, 0) |= absl::MakeInt128(pair.second, 0));
    EXPECT_EQ(
        absl::MakeInt128(pair.first ^ pair.second, 0),
        absl::MakeInt128(pair.first, 0) ^= absl::MakeInt128(pair.second, 0));
  }
}

TEST(Int128, BitwiseShiftTest) {
  for (int i = 0; i < 64; ++i) {
    for (int j = 0; j <= i; ++j) {
      // Left shift from j-th bit to i-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(uint64_t{1} << i, absl::int128(uint64_t{1} << j) << (i - j));
      EXPECT_EQ(uint64_t{1} << i, absl::int128(uint64_t{1} << j) <<= (i - j));
    }
  }
  for (int i = 0; i < 63; ++i) {
    for (int j = 0; j < 64; ++j) {
      // Left shift from j-th bit to (i + 64)-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::int128(uint64_t{1} << j) << (i + 64 - j));
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::int128(uint64_t{1} << j) <<= (i + 64 - j));
    }
    for (int j = 0; j <= i; ++j) {
      // Left shift from (j + 64)-th bit to (i + 64)-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::MakeInt128(uint64_t{1} << j, 0) << (i - j));
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::MakeInt128(uint64_t{1} << j, 0) <<= (i - j));
    }
  }

  for (int i = 0; i < 64; ++i) {
    for (int j = i; j < 64; ++j) {
      // Right shift from j-th bit to i-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(uint64_t{1} << i, absl::int128(uint64_t{1} << j) >> (j - i));
      EXPECT_EQ(uint64_t{1} << i, absl::int128(uint64_t{1} << j) >>= (j - i));
    }
    for (int j = 0; j < 63; ++j) {
      // Right shift from (j + 64)-th bit to i-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(uint64_t{1} << i,
                absl::MakeInt128(uint64_t{1} << j, 0) >> (j + 64 - i));
      EXPECT_EQ(uint64_t{1} << i,
                absl::MakeInt128(uint64_t{1} << j, 0) >>= (j + 64 - i));
    }
  }
  for (int i = 0; i < 63; ++i) {
    for (int j = i; j < 63; ++j) {
      // Right shift from (j + 64)-th bit to (i + 64)-th bit.
      SCOPED_TRACE(::testing::Message() << "i = " << i << "; j = " << j);
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::MakeInt128(uint64_t{1} << j, 0) >> (j - i));
      EXPECT_EQ(absl::MakeInt128(uint64_t{1} << i, 0),
                absl::MakeInt128(uint64_t{1} << j, 0) >>= (j - i));
    }
  }

  // Manually calculated cases with shift count for positive (val1) and negative
  // (val2) values
  absl::int128 val1 = MAKE_INT128(0x123456789abcdef0, 0x123456789abcdef0);
  absl::int128 val2 = MAKE_INT128(0xfedcba0987654321, 0xfedcba0987654321);

  EXPECT_EQ(val1 << 63, MAKE_INT128(0x91a2b3c4d5e6f78, 0x0));
  EXPECT_EQ(val1 << 64, MAKE_INT128(0x123456789abcdef0, 0x0));
  EXPECT_EQ(val2 << 63, MAKE_INT128(0xff6e5d04c3b2a190, 0x8000000000000000));
  EXPECT_EQ(val2 << 64, MAKE_INT128(0xfedcba0987654321, 0x0));

  EXPECT_EQ(val1 << 126, MAKE_INT128(0x0, 0x0));
  EXPECT_EQ(val2 << 126, MAKE_INT128(0x4000000000000000, 0x0));

  EXPECT_EQ(val1 >> 63, MAKE_INT128(0x0, 0x2468acf13579bde0));
  EXPECT_EQ(val1 >> 64, MAKE_INT128(0x0, 0x123456789abcdef0));
  EXPECT_EQ(val2 >> 63, MAKE_INT128(0xffffffffffffffff, 0xfdb974130eca8643));
  EXPECT_EQ(val2 >> 64, MAKE_INT128(0xffffffffffffffff, 0xfedcba0987654321));

  EXPECT_EQ(val1 >> 126, MAKE_INT128(0x0, 0x0));
  EXPECT_EQ(val2 >> 126, MAKE_INT128(0xffffffffffffffff, 0xffffffffffffffff));
}

TEST(Int128, NumericLimitsTest) {
  static_assert(std::numeric_limits<absl::int128>::is_specialized, "");
  static_assert(std::numeric_limits<absl::int128>::is_signed, "");
  static_assert(std::numeric_limits<absl::int128>::is_integer, "");
  EXPECT_EQ(static_cast<int>(127 * std::log10(2)),
            std::numeric_limits<absl::int128>::digits10);
  EXPECT_EQ(absl::Int128Min(), std::numeric_limits<absl::int128>::min());
  EXPECT_EQ(absl::Int128Min(), std::numeric_limits<absl::int128>::lowest());
  EXPECT_EQ(absl::Int128Max(), std::numeric_limits<absl::int128>::max());
}

TEST(Int128, BitCastable) {
  // NOTE: This test is not intended to be an example that demonstrate usages of
  // `static_cast` and `std::bit_cast`, rather it is here simply to verify
  // behavior. When deciding whether you should use `static_cast` or
  // `std::bit_cast` when converting between `absl::int128` and `absl::uint128`,
  // use your best judgement. As a rule of thumb, use the same cast that you
  // would use when converting between the signed and unsigned counterparts of a
  // builtin integral type.

  // Verify bit casting between signed and unsigned works with regards to two's
  // complement. This verifies we exhibit the same behavior as a theoretical
  // builtin int128_t and uint128_t in C++20 onwards.
  EXPECT_EQ(absl::bit_cast<absl::uint128>(absl::int128(-1)),
            std::numeric_limits<absl::uint128>::max());
  EXPECT_EQ(
      absl::bit_cast<absl::int128>(std::numeric_limits<absl::uint128>::max()),
      absl::int128(-1));
  EXPECT_EQ(
      absl::bit_cast<absl::uint128>(std::numeric_limits<absl::int128>::min()),
      absl::uint128(1) << 127);
  EXPECT_EQ(absl::bit_cast<absl::int128>(absl::uint128(1) << 127),
            std::numeric_limits<absl::int128>::min());
  EXPECT_EQ(
      absl::bit_cast<absl::uint128>(std::numeric_limits<absl::int128>::max()),
      (absl::uint128(1) << 127) - 1);
  EXPECT_EQ(absl::bit_cast<absl::int128>((absl::uint128(1) << 127) - 1),
            std::numeric_limits<absl::int128>::max());

  // Also verify static casting has the same behavior as bit casting.
  EXPECT_EQ(static_cast<absl::uint128>(absl::int128(-1)),
            std::numeric_limits<absl::uint128>::max());
  EXPECT_EQ(
      static_cast<absl::int128>(std::numeric_limits<absl::uint128>::max()),
      absl::int128(-1));
  EXPECT_EQ(
      static_cast<absl::uint128>(std::numeric_limits<absl::int128>::min()),
      absl::uint128(1) << 127);
  EXPECT_EQ(static_cast<absl::int128>(absl::uint128(1) << 127),
            std::numeric_limits<absl::int128>::min());
  EXPECT_EQ(
      static_cast<absl::uint128>(std::numeric_limits<absl::int128>::max()),
      (absl::uint128(1) << 127) - 1);
  EXPECT_EQ(static_cast<absl::int128>((absl::uint128(1) << 127) - 1),
            std::numeric_limits<absl::int128>::max());
}

}  // namespace
