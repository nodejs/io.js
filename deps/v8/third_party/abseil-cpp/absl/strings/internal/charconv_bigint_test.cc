// Copyright 2018 The Abseil Authors.
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

#include "absl/strings/internal/charconv_bigint.h"

#include <string>

#include "gtest/gtest.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace strings_internal {

TEST(BigUnsigned, ShiftLeft) {
  {
    // Check that 3 * 2**100 is calculated correctly
    BigUnsigned<4> num(3u);
    num.ShiftLeft(100);
    EXPECT_EQ(num, BigUnsigned<4>("3802951800684688204490109616128"));
  }
  {
    // Test that overflow is truncated properly.
    // 15 is 4 bits long, and BigUnsigned<4> is a 128-bit bigint.
    // Shifting left by 125 bits should truncate off the high bit, so that
    //   15 << 125 == 7 << 125
    // after truncation.
    BigUnsigned<4> a(15u);
    BigUnsigned<4> b(7u);
    BigUnsigned<4> c(3u);
    a.ShiftLeft(125);
    b.ShiftLeft(125);
    c.ShiftLeft(125);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
  }
  {
    // Same test, larger bigint:
    BigUnsigned<84> a(15u);
    BigUnsigned<84> b(7u);
    BigUnsigned<84> c(3u);
    a.ShiftLeft(84 * 32 - 3);
    b.ShiftLeft(84 * 32 - 3);
    c.ShiftLeft(84 * 32 - 3);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
  }
  {
    // Check that incrementally shifting has the same result as doing it all at
    // once (attempting to capture corner cases.)
    const std::string seed = "1234567890123456789012345678901234567890";
    BigUnsigned<84> a(seed);
    for (int i = 1; i <= 84 * 32; ++i) {
      a.ShiftLeft(1);
      BigUnsigned<84> b(seed);
      b.ShiftLeft(i);
      EXPECT_EQ(a, b);
    }
    // And we should have fully rotated all bits off by now:
    EXPECT_EQ(a, BigUnsigned<84>(0u));
  }
  {
    // Bit shifting large and small numbers by large and small offsets.
    // Intended to exercise bounds-checking corner on ShiftLeft() (directly
    // and under asan).

    // 2**(32*84)-1
    const BigUnsigned<84> all_bits_one(
        "1474444211396924248063325089479706787923460402125687709454567433186613"
        "6228083464060749874845919674257665016359189106695900028098437021384227"
        "3285029708032466536084583113729486015826557532750465299832071590813090"
        "2011853039837649252477307070509704043541368002938784757296893793903797"
        "8180292336310543540677175225040919704702800559606097685920595947397024"
        "8303316808753252115729411497720357971050627997031988036134171378490368"
        "6008000778741115399296162550786288457245180872759047016734959330367829"
        "5235612397427686310674725251378116268607113017720538636924549612987647"
        "5767411074510311386444547332882472126067840027882117834454260409440463"
        "9345147252664893456053258463203120637089916304618696601333953616715125"
        "2115882482473279040772264257431663818610405673876655957323083702713344"
        "4201105427930770976052393421467136557055");
    const BigUnsigned<84> zero(0u);
    const BigUnsigned<84> one(1u);
    // in bounds shifts
    for (int i = 1; i < 84*32; ++i) {
      // shifting all_bits_one to the left should result in a smaller number,
      // since the high bits rotate off and the low bits are replaced with
      // zeroes.
      BigUnsigned<84> big_shifted = all_bits_one;
      big_shifted.ShiftLeft(i);
      EXPECT_GT(all_bits_one, big_shifted);
      // Shifting 1 to the left should instead result in a larger number.
      BigUnsigned<84> small_shifted = one;
      small_shifted.ShiftLeft(i);
      EXPECT_LT(one, small_shifted);
    }
    // Shifting by zero or a negative number has no effect
    for (int no_op_shift : {0, -1, -84 * 32, std::numeric_limits<int>::min()}) {
      BigUnsigned<84> big_shifted = all_bits_one;
      big_shifted.ShiftLeft(no_op_shift);
      EXPECT_EQ(all_bits_one, big_shifted);
      BigUnsigned<84> small_shifted = one;
      big_shifted.ShiftLeft(no_op_shift);
      EXPECT_EQ(one, small_shifted);
    }
    // Shifting by an amount greater than the number of bits should result in
    // zero.
    for (int out_of_bounds_shift :
         {84 * 32, 84 * 32 + 1, std::numeric_limits<int>::max()}) {
      BigUnsigned<84> big_shifted = all_bits_one;
      big_shifted.ShiftLeft(out_of_bounds_shift);
      EXPECT_EQ(zero, big_shifted);
      BigUnsigned<84> small_shifted = one;
      small_shifted.ShiftLeft(out_of_bounds_shift);
      EXPECT_EQ(zero, small_shifted);
    }
  }
}

TEST(BigUnsigned, MultiplyByUint32) {
  const BigUnsigned<84> factorial_100(
      "933262154439441526816992388562667004907159682643816214685929638952175999"
      "932299156089414639761565182862536979208272237582511852109168640000000000"
      "00000000000000");
  BigUnsigned<84> a(1u);
  for (uint32_t i = 1; i <= 100; ++i) {
    a.MultiplyBy(i);
  }
  EXPECT_EQ(a, BigUnsigned<84>(factorial_100));
}

TEST(BigUnsigned, MultiplyByBigUnsigned) {
  {
    // Put the terms of factorial_200 into two bigints, and multiply them
    // together.
    const BigUnsigned<84> factorial_200(
        "7886578673647905035523632139321850622951359776871732632947425332443594"
        "4996340334292030428401198462390417721213891963883025764279024263710506"
        "1926624952829931113462857270763317237396988943922445621451664240254033"
        "2918641312274282948532775242424075739032403212574055795686602260319041"
        "7032406235170085879617892222278962370389737472000000000000000000000000"
        "0000000000000000000000000");
    BigUnsigned<84> evens(1u);
    BigUnsigned<84> odds(1u);
    for (uint32_t i = 1; i < 200; i += 2) {
      odds.MultiplyBy(i);
      evens.MultiplyBy(i + 1);
    }
    evens.MultiplyBy(odds);
    EXPECT_EQ(evens, factorial_200);
  }
  {
    // Multiply various powers of 10 together.
    for (int a = 0 ; a < 700; a += 25) {
      SCOPED_TRACE(a);
      BigUnsigned<84> a_value("3" + std::string(a, '0'));
      for (int b = 0; b < (700 - a); b += 25) {
        SCOPED_TRACE(b);
        BigUnsigned<84> b_value("2" + std::string(b, '0'));
        BigUnsigned<84> expected_product("6" + std::string(a + b, '0'));
        b_value.MultiplyBy(a_value);
        EXPECT_EQ(b_value, expected_product);
      }
    }
  }
}

TEST(BigUnsigned, MultiplyByOverflow) {
  {
    // Check that multiplcation overflow predictably truncates.

    // A big int with all bits on.
    BigUnsigned<4> all_bits_on("340282366920938463463374607431768211455");
    // Modulo 2**128, this is equal to -1.  Therefore the square of this,
    // modulo 2**128, should be 1.
    all_bits_on.MultiplyBy(all_bits_on);
    EXPECT_EQ(all_bits_on, BigUnsigned<4>(1u));
  }
  {
    // Try multiplying a large bigint by 2**50, and compare the result to
    // shifting.
    BigUnsigned<4> value_1("12345678901234567890123456789012345678");
    BigUnsigned<4> value_2("12345678901234567890123456789012345678");
    BigUnsigned<4> two_to_fiftieth(1u);
    two_to_fiftieth.ShiftLeft(50);

    value_1.ShiftLeft(50);
    value_2.MultiplyBy(two_to_fiftieth);
    EXPECT_EQ(value_1, value_2);
  }
}

TEST(BigUnsigned, FiveToTheNth) {
  {
    // Sanity check that MultiplyByFiveToTheNth gives consistent answers, up to
    // and including overflow.
    for (int i = 0; i < 1160; ++i) {
      SCOPED_TRACE(i);
      BigUnsigned<84> value_1(123u);
      BigUnsigned<84> value_2(123u);
      value_1.MultiplyByFiveToTheNth(i);
      for (int j = 0; j < i; j++) {
        value_2.MultiplyBy(5u);
      }
      EXPECT_EQ(value_1, value_2);
    }
  }
  {
    // Check that the faster, table-lookup-based static method returns the same
    // result that multiplying in-place would return, up to and including
    // overflow.
    for (int i = 0; i < 1160; ++i) {
      SCOPED_TRACE(i);
      BigUnsigned<84> value_1(1u);
      value_1.MultiplyByFiveToTheNth(i);
      BigUnsigned<84> value_2 = BigUnsigned<84>::FiveToTheNth(i);
      EXPECT_EQ(value_1, value_2);
    }
  }
}

TEST(BigUnsigned, TenToTheNth) {
  {
    // Sanity check MultiplyByTenToTheNth.
    for (int i = 0; i < 800; ++i) {
      SCOPED_TRACE(i);
      BigUnsigned<84> value_1(123u);
      BigUnsigned<84> value_2(123u);
      value_1.MultiplyByTenToTheNth(i);
      for (int j = 0; j < i; j++) {
        value_2.MultiplyBy(10u);
      }
      EXPECT_EQ(value_1, value_2);
    }
  }
  {
    // Alternate testing approach, taking advantage of the decimal parser.
    for (int i = 0; i < 200; ++i) {
      SCOPED_TRACE(i);
      BigUnsigned<84> value_1(135u);
      value_1.MultiplyByTenToTheNth(i);
      BigUnsigned<84> value_2("135" + std::string(i, '0'));
      EXPECT_EQ(value_1, value_2);
    }
  }
}


}  // namespace strings_internal
ABSL_NAMESPACE_END
}  // namespace absl
