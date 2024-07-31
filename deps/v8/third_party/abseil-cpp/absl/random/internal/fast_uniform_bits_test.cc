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

#include "absl/random/internal/fast_uniform_bits.h"

#include <random>

#include "gtest/gtest.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {
namespace {

template <typename IntType>
class FastUniformBitsTypedTest : public ::testing::Test {};

using IntTypes = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;

TYPED_TEST_SUITE(FastUniformBitsTypedTest, IntTypes);

TYPED_TEST(FastUniformBitsTypedTest, BasicTest) {
  using Limits = std::numeric_limits<TypeParam>;
  using FastBits = FastUniformBits<TypeParam>;

  EXPECT_EQ(0, (FastBits::min)());
  EXPECT_EQ((Limits::max)(), (FastBits::max)());

  constexpr int kIters = 10000;
  std::random_device rd;
  std::mt19937 gen(rd());
  FastBits fast;
  for (int i = 0; i < kIters; i++) {
    const auto v = fast(gen);
    EXPECT_LE(v, (FastBits::max)());
    EXPECT_GE(v, (FastBits::min)());
  }
}

template <typename UIntType, UIntType Lo, UIntType Hi, UIntType Val = Lo>
struct FakeUrbg {
  using result_type = UIntType;

  FakeUrbg() = default;
  explicit FakeUrbg(bool r) : reject(r) {}

  static constexpr result_type(max)() { return Hi; }
  static constexpr result_type(min)() { return Lo; }
  result_type operator()() {
    // when reject is set, return Hi half the time.
    return ((++calls % 2) == 1 && reject) ? Hi : Val;
  }

  bool reject = false;
  size_t calls = 0;
};

TEST(FastUniformBitsTest, IsPowerOfTwoOrZero) {
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint8_t{0}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint8_t{1}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint8_t{2}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint8_t{3}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint8_t{4}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint8_t{16}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint8_t{17}));
  EXPECT_FALSE(IsPowerOfTwoOrZero((std::numeric_limits<uint8_t>::max)()));

  EXPECT_TRUE(IsPowerOfTwoOrZero(uint16_t{0}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint16_t{1}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint16_t{2}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint16_t{3}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint16_t{4}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint16_t{16}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint16_t{17}));
  EXPECT_FALSE(IsPowerOfTwoOrZero((std::numeric_limits<uint16_t>::max)()));

  EXPECT_TRUE(IsPowerOfTwoOrZero(uint32_t{0}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint32_t{1}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint32_t{2}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint32_t{3}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint32_t{32}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint32_t{17}));
  EXPECT_FALSE(IsPowerOfTwoOrZero((std::numeric_limits<uint32_t>::max)()));

  EXPECT_TRUE(IsPowerOfTwoOrZero(uint64_t{0}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint64_t{1}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint64_t{2}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint64_t{3}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint64_t{4}));
  EXPECT_TRUE(IsPowerOfTwoOrZero(uint64_t{64}));
  EXPECT_FALSE(IsPowerOfTwoOrZero(uint64_t{17}));
  EXPECT_FALSE(IsPowerOfTwoOrZero((std::numeric_limits<uint64_t>::max)()));
}

TEST(FastUniformBitsTest, IntegerLog2) {
  EXPECT_EQ(0, IntegerLog2(uint16_t{0}));
  EXPECT_EQ(0, IntegerLog2(uint16_t{1}));
  EXPECT_EQ(1, IntegerLog2(uint16_t{2}));
  EXPECT_EQ(1, IntegerLog2(uint16_t{3}));
  EXPECT_EQ(2, IntegerLog2(uint16_t{4}));
  EXPECT_EQ(2, IntegerLog2(uint16_t{5}));
  EXPECT_EQ(2, IntegerLog2(uint16_t{7}));
  EXPECT_EQ(3, IntegerLog2(uint16_t{8}));
  EXPECT_EQ(63, IntegerLog2((std::numeric_limits<uint64_t>::max)()));
}

TEST(FastUniformBitsTest, RangeSize) {
  EXPECT_EQ(2, (RangeSize<FakeUrbg<uint8_t, 0, 1>>()));
  EXPECT_EQ(3, (RangeSize<FakeUrbg<uint8_t, 0, 2>>()));
  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint8_t, 0, 3>>()));
  //  EXPECT_EQ(0, (RangeSize<FakeUrbg<uint8_t, 2, 2>>()));
  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint8_t, 2, 5>>()));
  EXPECT_EQ(5, (RangeSize<FakeUrbg<uint8_t, 2, 6>>()));
  EXPECT_EQ(9, (RangeSize<FakeUrbg<uint8_t, 2, 10>>()));
  EXPECT_EQ(
      0, (RangeSize<
             FakeUrbg<uint8_t, 0, (std::numeric_limits<uint8_t>::max)()>>()));

  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint16_t, 0, 3>>()));
  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint16_t, 2, 5>>()));
  EXPECT_EQ(5, (RangeSize<FakeUrbg<uint16_t, 2, 6>>()));
  EXPECT_EQ(18, (RangeSize<FakeUrbg<uint16_t, 1000, 1017>>()));
  EXPECT_EQ(
      0, (RangeSize<
             FakeUrbg<uint16_t, 0, (std::numeric_limits<uint16_t>::max)()>>()));

  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint32_t, 0, 3>>()));
  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint32_t, 2, 5>>()));
  EXPECT_EQ(5, (RangeSize<FakeUrbg<uint32_t, 2, 6>>()));
  EXPECT_EQ(18, (RangeSize<FakeUrbg<uint32_t, 1000, 1017>>()));
  EXPECT_EQ(0, (RangeSize<FakeUrbg<uint32_t, 0, 0xffffffff>>()));
  EXPECT_EQ(0xffffffff, (RangeSize<FakeUrbg<uint32_t, 1, 0xffffffff>>()));
  EXPECT_EQ(0xfffffffe, (RangeSize<FakeUrbg<uint32_t, 1, 0xfffffffe>>()));
  EXPECT_EQ(0xfffffffd, (RangeSize<FakeUrbg<uint32_t, 2, 0xfffffffe>>()));
  EXPECT_EQ(
      0, (RangeSize<
             FakeUrbg<uint32_t, 0, (std::numeric_limits<uint32_t>::max)()>>()));

  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint64_t, 0, 3>>()));
  EXPECT_EQ(4, (RangeSize<FakeUrbg<uint64_t, 2, 5>>()));
  EXPECT_EQ(5, (RangeSize<FakeUrbg<uint64_t, 2, 6>>()));
  EXPECT_EQ(18, (RangeSize<FakeUrbg<uint64_t, 1000, 1017>>()));
  EXPECT_EQ(0x100000000, (RangeSize<FakeUrbg<uint64_t, 0, 0xffffffff>>()));
  EXPECT_EQ(0xffffffff, (RangeSize<FakeUrbg<uint64_t, 1, 0xffffffff>>()));
  EXPECT_EQ(0xfffffffe, (RangeSize<FakeUrbg<uint64_t, 1, 0xfffffffe>>()));
  EXPECT_EQ(0xfffffffd, (RangeSize<FakeUrbg<uint64_t, 2, 0xfffffffe>>()));
  EXPECT_EQ(0, (RangeSize<FakeUrbg<uint64_t, 0, 0xffffffffffffffff>>()));
  EXPECT_EQ(0xffffffffffffffff,
            (RangeSize<FakeUrbg<uint64_t, 1, 0xffffffffffffffff>>()));
  EXPECT_EQ(0xfffffffffffffffe,
            (RangeSize<FakeUrbg<uint64_t, 1, 0xfffffffffffffffe>>()));
  EXPECT_EQ(0xfffffffffffffffd,
            (RangeSize<FakeUrbg<uint64_t, 2, 0xfffffffffffffffe>>()));
  EXPECT_EQ(
      0, (RangeSize<
             FakeUrbg<uint64_t, 0, (std::numeric_limits<uint64_t>::max)()>>()));
}

// The constants need to be chosen so that an infinite rejection loop doesn't
// happen...
using Urng1_5bit = FakeUrbg<uint8_t, 0, 2, 0>;  // ~1.5 bits (range 3)
using Urng4bits = FakeUrbg<uint8_t, 1, 0x10, 2>;
using Urng22bits = FakeUrbg<uint32_t, 0, 0x3fffff, 0x301020>;
using Urng31bits = FakeUrbg<uint32_t, 1, 0xfffffffe, 0x60070f03>;  // ~31.9 bits
using Urng32bits = FakeUrbg<uint32_t, 0, 0xffffffff, 0x74010f01>;
using Urng33bits =
    FakeUrbg<uint64_t, 1, 0x1ffffffff, 0x013301033>;  // ~32.9 bits
using Urng63bits = FakeUrbg<uint64_t, 1, 0xfffffffffffffffe,
                            0xfedcba9012345678>;  // ~63.9 bits
using Urng64bits =
    FakeUrbg<uint64_t, 0, 0xffffffffffffffff, 0x123456780fedcba9>;

TEST(FastUniformBitsTest, OutputsUpTo32Bits) {
  // Tests that how values are composed; the single-bit deltas should be spread
  // across each invocation.
  Urng1_5bit urng1_5;
  Urng4bits urng4;
  Urng22bits urng22;
  Urng31bits urng31;
  Urng32bits urng32;
  Urng33bits urng33;
  Urng63bits urng63;
  Urng64bits urng64;

  // 8-bit types
  {
    FastUniformBits<uint8_t> fast8;
    EXPECT_EQ(0x0, fast8(urng1_5));
    EXPECT_EQ(0x11, fast8(urng4));
    EXPECT_EQ(0x20, fast8(urng22));
    EXPECT_EQ(0x2, fast8(urng31));
    EXPECT_EQ(0x1, fast8(urng32));
    EXPECT_EQ(0x32, fast8(urng33));
    EXPECT_EQ(0x77, fast8(urng63));
    EXPECT_EQ(0xa9, fast8(urng64));
  }

  // 16-bit types
  {
    FastUniformBits<uint16_t> fast16;
    EXPECT_EQ(0x0, fast16(urng1_5));
    EXPECT_EQ(0x1111, fast16(urng4));
    EXPECT_EQ(0x1020, fast16(urng22));
    EXPECT_EQ(0x0f02, fast16(urng31));
    EXPECT_EQ(0x0f01, fast16(urng32));
    EXPECT_EQ(0x1032, fast16(urng33));
    EXPECT_EQ(0x5677, fast16(urng63));
    EXPECT_EQ(0xcba9, fast16(urng64));
  }

  // 32-bit types
  {
    FastUniformBits<uint32_t> fast32;
    EXPECT_EQ(0x0, fast32(urng1_5));
    EXPECT_EQ(0x11111111, fast32(urng4));
    EXPECT_EQ(0x08301020, fast32(urng22));
    EXPECT_EQ(0x0f020f02, fast32(urng31));
    EXPECT_EQ(0x74010f01, fast32(urng32));
    EXPECT_EQ(0x13301032, fast32(urng33));
    EXPECT_EQ(0x12345677, fast32(urng63));
    EXPECT_EQ(0x0fedcba9, fast32(urng64));
  }
}

TEST(FastUniformBitsTest, Outputs64Bits) {
  // Tests that how values are composed; the single-bit deltas should be spread
  // across each invocation.
  FastUniformBits<uint64_t> fast64;

  {
    FakeUrbg<uint8_t, 0, 1, 0> urng0;
    FakeUrbg<uint8_t, 0, 1, 1> urng1;
    Urng4bits urng4;
    Urng22bits urng22;
    Urng31bits urng31;
    Urng32bits urng32;
    Urng33bits urng33;
    Urng63bits urng63;
    Urng64bits urng64;

    // somewhat degenerate cases only create a single bit.
    EXPECT_EQ(0x0, fast64(urng0));
    EXPECT_EQ(64, urng0.calls);
    EXPECT_EQ(0xffffffffffffffff, fast64(urng1));
    EXPECT_EQ(64, urng1.calls);

    // less degenerate cases.
    EXPECT_EQ(0x1111111111111111, fast64(urng4));
    EXPECT_EQ(16, urng4.calls);
    EXPECT_EQ(0x01020c0408301020, fast64(urng22));
    EXPECT_EQ(3, urng22.calls);
    EXPECT_EQ(0x387811c3c0870f02, fast64(urng31));
    EXPECT_EQ(3, urng31.calls);
    EXPECT_EQ(0x74010f0174010f01, fast64(urng32));
    EXPECT_EQ(2, urng32.calls);
    EXPECT_EQ(0x808194040cb01032, fast64(urng33));
    EXPECT_EQ(3, urng33.calls);
    EXPECT_EQ(0x1234567712345677, fast64(urng63));
    EXPECT_EQ(2, urng63.calls);
    EXPECT_EQ(0x123456780fedcba9, fast64(urng64));
    EXPECT_EQ(1, urng64.calls);
  }

  // The 1.5 bit case is somewhat interesting in that the algorithm refinement
  // causes one extra small sample. Comments here reference the names used in
  // [rand.adapt.ibits] that correspond to this case.
  {
    Urng1_5bit urng1_5;

    // w = 64
    // R = 3
    // m = 1
    // n' = 64
    // w0' = 1
    // y0' = 2
    // n = (1 <= 0) > 64 : 65 = 65
    // n0 = 65 - (64%65) = 1
    // n1 = 64
    // w0 = 0
    // y0 = 3
    // w1 = 1
    // y1 = 2
    EXPECT_EQ(0x0, fast64(urng1_5));
    EXPECT_EQ(65, urng1_5.calls);
  }

  // Validate rejections for non-power-of-2 cases.
  {
    Urng1_5bit urng1_5(true);
    Urng31bits urng31(true);
    Urng33bits urng33(true);
    Urng63bits urng63(true);

    // For 1.5 bits, there would be 1+2*64, except the first
    // value was accepted and shifted off the end.
    EXPECT_EQ(0, fast64(urng1_5));
    EXPECT_EQ(128, urng1_5.calls);
    EXPECT_EQ(0x387811c3c0870f02, fast64(urng31));
    EXPECT_EQ(6, urng31.calls);
    EXPECT_EQ(0x808194040cb01032, fast64(urng33));
    EXPECT_EQ(6, urng33.calls);
    EXPECT_EQ(0x1234567712345677, fast64(urng63));
    EXPECT_EQ(4, urng63.calls);
  }
}

TEST(FastUniformBitsTest, URBG32bitRegression) {
  // Validate with deterministic 32-bit std::minstd_rand
  // to ensure that operator() performs as expected.

  EXPECT_EQ(2147483646, RangeSize<std::minstd_rand>());
  EXPECT_EQ(30, IntegerLog2(RangeSize<std::minstd_rand>()));

  std::minstd_rand gen(1);
  FastUniformBits<uint64_t> fast64;

  EXPECT_EQ(0x05e47095f8791f45, fast64(gen));
  EXPECT_EQ(0x028be17e3c07c122, fast64(gen));
  EXPECT_EQ(0x55d2847c1626e8c2, fast64(gen));
}

}  // namespace
}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl
