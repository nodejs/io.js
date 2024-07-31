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

#include "absl/numeric/bits.h"

#include <limits>
#include <type_traits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/random/random.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

template <typename IntT>
class IntegerTypesTest : public ::testing::Test {};

using OneByteIntegerTypes = ::testing::Types<
    unsigned char,
    uint8_t
    >;

TYPED_TEST_SUITE(IntegerTypesTest, OneByteIntegerTypes);

TYPED_TEST(IntegerTypesTest, HandlesTypes) {
  using UIntType = TypeParam;

  EXPECT_EQ(rotl(UIntType{0x12}, 0), uint8_t{0x12});
  EXPECT_EQ(rotr(UIntType{0x12}, -4), uint8_t{0x21});
  static_assert(rotl(UIntType{0x12}, 0) == uint8_t{0x12}, "");

  static_assert(rotr(UIntType{0x12}, 0) == uint8_t{0x12}, "");
  EXPECT_EQ(rotr(UIntType{0x12}, 0), uint8_t{0x12});

#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(countl_zero(UIntType{}) == 8, "");
  static_assert(countl_zero(static_cast<UIntType>(-1)) == 0, "");

  static_assert(countl_one(UIntType{}) == 0, "");
  static_assert(countl_one(static_cast<UIntType>(-1)) == 8, "");

  static_assert(countr_zero(UIntType{}) == 8, "");
  static_assert(countr_zero(static_cast<UIntType>(-1)) == 0, "");

  static_assert(countr_one(UIntType{}) == 0, "");
  static_assert(countr_one(static_cast<UIntType>(-1)) == 8, "");

  static_assert(popcount(UIntType{}) == 0, "");
  static_assert(popcount(UIntType{1}) == 1, "");
  static_assert(popcount(static_cast<UIntType>(-1)) == 8, "");

  static_assert(bit_width(UIntType{}) == 0, "");
  static_assert(bit_width(UIntType{1}) == 1, "");
  static_assert(bit_width(UIntType{3}) == 2, "");
  static_assert(bit_width(static_cast<UIntType>(-1)) == 8, "");
#endif

  EXPECT_EQ(countl_zero(UIntType{}), 8);
  EXPECT_EQ(countl_zero(static_cast<UIntType>(-1)), 0);

  EXPECT_EQ(countl_one(UIntType{}), 0);
  EXPECT_EQ(countl_one(static_cast<UIntType>(-1)), 8);

  EXPECT_EQ(countr_zero(UIntType{}), 8);
  EXPECT_EQ(countr_zero(static_cast<UIntType>(-1)), 0);

  EXPECT_EQ(countr_one(UIntType{}), 0);
  EXPECT_EQ(countr_one(static_cast<UIntType>(-1)), 8);

  EXPECT_EQ(popcount(UIntType{}), 0);
  EXPECT_EQ(popcount(UIntType{1}), 1);

  EXPECT_FALSE(has_single_bit(UIntType{}));
  EXPECT_FALSE(has_single_bit(static_cast<UIntType>(-1)));

  EXPECT_EQ(bit_width(UIntType{}), 0);
  EXPECT_EQ(bit_width(UIntType{1}), 1);
  EXPECT_EQ(bit_width(UIntType{3}), 2);
  EXPECT_EQ(bit_width(static_cast<UIntType>(-1)), 8);
}

TEST(Rotate, Left) {
  static_assert(rotl(uint8_t{0x12}, 0) == uint8_t{0x12}, "");
  static_assert(rotl(uint16_t{0x1234}, 0) == uint16_t{0x1234}, "");
  static_assert(rotl(uint32_t{0x12345678UL}, 0) == uint32_t{0x12345678UL}, "");
  static_assert(rotl(uint64_t{0x12345678ABCDEF01ULL}, 0) ==
                    uint64_t{0x12345678ABCDEF01ULL},
                "");

  EXPECT_EQ(rotl(uint8_t{0x12}, 0), uint8_t{0x12});
  EXPECT_EQ(rotl(uint16_t{0x1234}, 0), uint16_t{0x1234});
  EXPECT_EQ(rotl(uint32_t{0x12345678UL}, 0), uint32_t{0x12345678UL});
  EXPECT_EQ(rotl(uint64_t{0x12345678ABCDEF01ULL}, 0),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotl(uint8_t{0x12}, 8), uint8_t{0x12});
  EXPECT_EQ(rotl(uint16_t{0x1234}, 16), uint16_t{0x1234});
  EXPECT_EQ(rotl(uint32_t{0x12345678UL}, 32), uint32_t{0x12345678UL});
  EXPECT_EQ(rotl(uint64_t{0x12345678ABCDEF01ULL}, 64),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotl(uint8_t{0x12}, -8), uint8_t{0x12});
  EXPECT_EQ(rotl(uint16_t{0x1234}, -16), uint16_t{0x1234});
  EXPECT_EQ(rotl(uint32_t{0x12345678UL}, -32), uint32_t{0x12345678UL});
  EXPECT_EQ(rotl(uint64_t{0x12345678ABCDEF01ULL}, -64),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotl(uint8_t{0x12}, 4), uint8_t{0x21});
  EXPECT_EQ(rotl(uint16_t{0x1234}, 4), uint16_t{0x2341});
  EXPECT_EQ(rotl(uint32_t{0x12345678UL}, 4), uint32_t{0x23456781UL});
  EXPECT_EQ(rotl(uint64_t{0x12345678ABCDEF01ULL}, 4),
            uint64_t{0x2345678ABCDEF011ULL});

  EXPECT_EQ(rotl(uint8_t{0x12}, -4), uint8_t{0x21});
  EXPECT_EQ(rotl(uint16_t{0x1234}, -4), uint16_t{0x4123});
  EXPECT_EQ(rotl(uint32_t{0x12345678UL}, -4), uint32_t{0x81234567UL});
  EXPECT_EQ(rotl(uint64_t{0x12345678ABCDEF01ULL}, -4),
            uint64_t{0x112345678ABCDEF0ULL});
}

TEST(Rotate, Right) {
  static_assert(rotr(uint8_t{0x12}, 0) == uint8_t{0x12}, "");
  static_assert(rotr(uint16_t{0x1234}, 0) == uint16_t{0x1234}, "");
  static_assert(rotr(uint32_t{0x12345678UL}, 0) == uint32_t{0x12345678UL}, "");
  static_assert(rotr(uint64_t{0x12345678ABCDEF01ULL}, 0) ==
                    uint64_t{0x12345678ABCDEF01ULL},
                "");

  EXPECT_EQ(rotr(uint8_t{0x12}, 0), uint8_t{0x12});
  EXPECT_EQ(rotr(uint16_t{0x1234}, 0), uint16_t{0x1234});
  EXPECT_EQ(rotr(uint32_t{0x12345678UL}, 0), uint32_t{0x12345678UL});
  EXPECT_EQ(rotr(uint64_t{0x12345678ABCDEF01ULL}, 0),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotr(uint8_t{0x12}, 8), uint8_t{0x12});
  EXPECT_EQ(rotr(uint16_t{0x1234}, 16), uint16_t{0x1234});
  EXPECT_EQ(rotr(uint32_t{0x12345678UL}, 32), uint32_t{0x12345678UL});
  EXPECT_EQ(rotr(uint64_t{0x12345678ABCDEF01ULL}, 64),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotr(uint8_t{0x12}, -8), uint8_t{0x12});
  EXPECT_EQ(rotr(uint16_t{0x1234}, -16), uint16_t{0x1234});
  EXPECT_EQ(rotr(uint32_t{0x12345678UL}, -32), uint32_t{0x12345678UL});
  EXPECT_EQ(rotr(uint64_t{0x12345678ABCDEF01ULL}, -64),
            uint64_t{0x12345678ABCDEF01ULL});

  EXPECT_EQ(rotr(uint8_t{0x12}, 4), uint8_t{0x21});
  EXPECT_EQ(rotr(uint16_t{0x1234}, 4), uint16_t{0x4123});
  EXPECT_EQ(rotr(uint32_t{0x12345678UL}, 4), uint32_t{0x81234567UL});
  EXPECT_EQ(rotr(uint64_t{0x12345678ABCDEF01ULL}, 4),
            uint64_t{0x112345678ABCDEF0ULL});

  EXPECT_EQ(rotr(uint8_t{0x12}, -4), uint8_t{0x21});
  EXPECT_EQ(rotr(uint16_t{0x1234}, -4), uint16_t{0x2341});
  EXPECT_EQ(rotr(uint32_t{0x12345678UL}, -4), uint32_t{0x23456781UL});
  EXPECT_EQ(rotr(uint64_t{0x12345678ABCDEF01ULL}, -4),
            uint64_t{0x2345678ABCDEF011ULL});
}

TEST(Rotate, Symmetry) {
  // rotr(x, s) is equivalent to rotl(x, -s)
  absl::BitGen rng;
  constexpr int kTrials = 100;

  for (int i = 0; i < kTrials; ++i) {
    uint8_t value = absl::Uniform(rng, std::numeric_limits<uint8_t>::min(),
                                  std::numeric_limits<uint8_t>::max());
    int shift = absl::Uniform(rng, -2 * std::numeric_limits<uint8_t>::digits,
                              2 * std::numeric_limits<uint8_t>::digits);

    EXPECT_EQ(rotl(value, shift), rotr(value, -shift));
  }

  for (int i = 0; i < kTrials; ++i) {
    uint16_t value = absl::Uniform(rng, std::numeric_limits<uint16_t>::min(),
                                   std::numeric_limits<uint16_t>::max());
    int shift = absl::Uniform(rng, -2 * std::numeric_limits<uint16_t>::digits,
                              2 * std::numeric_limits<uint16_t>::digits);

    EXPECT_EQ(rotl(value, shift), rotr(value, -shift));
  }

  for (int i = 0; i < kTrials; ++i) {
    uint32_t value = absl::Uniform(rng, std::numeric_limits<uint32_t>::min(),
                                   std::numeric_limits<uint32_t>::max());
    int shift = absl::Uniform(rng, -2 * std::numeric_limits<uint32_t>::digits,
                              2 * std::numeric_limits<uint32_t>::digits);

    EXPECT_EQ(rotl(value, shift), rotr(value, -shift));
  }

  for (int i = 0; i < kTrials; ++i) {
    uint64_t value = absl::Uniform(rng, std::numeric_limits<uint64_t>::min(),
                                   std::numeric_limits<uint64_t>::max());
    int shift = absl::Uniform(rng, -2 * std::numeric_limits<uint64_t>::digits,
                              2 * std::numeric_limits<uint64_t>::digits);

    EXPECT_EQ(rotl(value, shift), rotr(value, -shift));
  }
}

TEST(Counting, LeadingZeroes) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(countl_zero(uint8_t{}) == 8, "");
  static_assert(countl_zero(static_cast<uint8_t>(-1)) == 0, "");
  static_assert(countl_zero(uint16_t{}) == 16, "");
  static_assert(countl_zero(static_cast<uint16_t>(-1)) == 0, "");
  static_assert(countl_zero(uint32_t{}) == 32, "");
  static_assert(countl_zero(~uint32_t{}) == 0, "");
  static_assert(countl_zero(uint64_t{}) == 64, "");
  static_assert(countl_zero(~uint64_t{}) == 0, "");
#endif

  EXPECT_EQ(countl_zero(uint8_t{}), 8);
  EXPECT_EQ(countl_zero(static_cast<uint8_t>(-1)), 0);
  EXPECT_EQ(countl_zero(uint16_t{}), 16);
  EXPECT_EQ(countl_zero(static_cast<uint16_t>(-1)), 0);
  EXPECT_EQ(countl_zero(uint32_t{}), 32);
  EXPECT_EQ(countl_zero(~uint32_t{}), 0);
  EXPECT_EQ(countl_zero(uint64_t{}), 64);
  EXPECT_EQ(countl_zero(~uint64_t{}), 0);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(countl_zero(static_cast<uint8_t>(1u << i)), 7 - i);
  }

  for (int i = 0; i < 16; i++) {
    EXPECT_EQ(countl_zero(static_cast<uint16_t>(1u << i)), 15 - i);
  }

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(countl_zero(uint32_t{1} << i), 31 - i);
  }

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(countl_zero(uint64_t{1} << i), 63 - i);
  }
}

TEST(Counting, LeadingOnes) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(countl_one(uint8_t{}) == 0, "");
  static_assert(countl_one(static_cast<uint8_t>(-1)) == 8, "");
  static_assert(countl_one(uint16_t{}) == 0, "");
  static_assert(countl_one(static_cast<uint16_t>(-1)) == 16, "");
  static_assert(countl_one(uint32_t{}) == 0, "");
  static_assert(countl_one(~uint32_t{}) == 32, "");
  static_assert(countl_one(uint64_t{}) == 0, "");
  static_assert(countl_one(~uint64_t{}) == 64, "");
#endif

  EXPECT_EQ(countl_one(uint8_t{}), 0);
  EXPECT_EQ(countl_one(static_cast<uint8_t>(-1)), 8);
  EXPECT_EQ(countl_one(uint16_t{}), 0);
  EXPECT_EQ(countl_one(static_cast<uint16_t>(-1)), 16);
  EXPECT_EQ(countl_one(uint32_t{}), 0);
  EXPECT_EQ(countl_one(~uint32_t{}), 32);
  EXPECT_EQ(countl_one(uint64_t{}), 0);
  EXPECT_EQ(countl_one(~uint64_t{}), 64);
}

TEST(Counting, TrailingZeroes) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CTZ
  static_assert(countr_zero(uint8_t{}) == 8, "");
  static_assert(countr_zero(static_cast<uint8_t>(-1)) == 0, "");
  static_assert(countr_zero(uint16_t{}) == 16, "");
  static_assert(countr_zero(static_cast<uint16_t>(-1)) == 0, "");
  static_assert(countr_zero(uint32_t{}) == 32, "");
  static_assert(countr_zero(~uint32_t{}) == 0, "");
  static_assert(countr_zero(uint64_t{}) == 64, "");
  static_assert(countr_zero(~uint64_t{}) == 0, "");
#endif

  EXPECT_EQ(countr_zero(uint8_t{}), 8);
  EXPECT_EQ(countr_zero(static_cast<uint8_t>(-1)), 0);
  EXPECT_EQ(countr_zero(uint16_t{}), 16);
  EXPECT_EQ(countr_zero(static_cast<uint16_t>(-1)), 0);
  EXPECT_EQ(countr_zero(uint32_t{}), 32);
  EXPECT_EQ(countr_zero(~uint32_t{}), 0);
  EXPECT_EQ(countr_zero(uint64_t{}), 64);
  EXPECT_EQ(countr_zero(~uint64_t{}), 0);
}

TEST(Counting, TrailingOnes) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CTZ
  static_assert(countr_one(uint8_t{}) == 0, "");
  static_assert(countr_one(static_cast<uint8_t>(-1)) == 8, "");
  static_assert(countr_one(uint16_t{}) == 0, "");
  static_assert(countr_one(static_cast<uint16_t>(-1)) == 16, "");
  static_assert(countr_one(uint32_t{}) == 0, "");
  static_assert(countr_one(~uint32_t{}) == 32, "");
  static_assert(countr_one(uint64_t{}) == 0, "");
  static_assert(countr_one(~uint64_t{}) == 64, "");
#endif

  EXPECT_EQ(countr_one(uint8_t{}), 0);
  EXPECT_EQ(countr_one(static_cast<uint8_t>(-1)), 8);
  EXPECT_EQ(countr_one(uint16_t{}), 0);
  EXPECT_EQ(countr_one(static_cast<uint16_t>(-1)), 16);
  EXPECT_EQ(countr_one(uint32_t{}), 0);
  EXPECT_EQ(countr_one(~uint32_t{}), 32);
  EXPECT_EQ(countr_one(uint64_t{}), 0);
  EXPECT_EQ(countr_one(~uint64_t{}), 64);
}

TEST(Counting, Popcount) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_POPCOUNT
  static_assert(popcount(uint8_t{}) == 0, "");
  static_assert(popcount(uint8_t{1}) == 1, "");
  static_assert(popcount(static_cast<uint8_t>(-1)) == 8, "");
  static_assert(popcount(uint16_t{}) == 0, "");
  static_assert(popcount(uint16_t{1}) == 1, "");
  static_assert(popcount(static_cast<uint16_t>(-1)) == 16, "");
  static_assert(popcount(uint32_t{}) == 0, "");
  static_assert(popcount(uint32_t{1}) == 1, "");
  static_assert(popcount(~uint32_t{}) == 32, "");
  static_assert(popcount(uint64_t{}) == 0, "");
  static_assert(popcount(uint64_t{1}) == 1, "");
  static_assert(popcount(~uint64_t{}) == 64, "");
#endif  // ABSL_INTERNAL_HAS_CONSTEXPR_POPCOUNT

  EXPECT_EQ(popcount(uint8_t{}), 0);
  EXPECT_EQ(popcount(uint8_t{1}), 1);
  EXPECT_EQ(popcount(static_cast<uint8_t>(-1)), 8);
  EXPECT_EQ(popcount(uint16_t{}), 0);
  EXPECT_EQ(popcount(uint16_t{1}), 1);
  EXPECT_EQ(popcount(static_cast<uint16_t>(-1)), 16);
  EXPECT_EQ(popcount(uint32_t{}), 0);
  EXPECT_EQ(popcount(uint32_t{1}), 1);
  EXPECT_EQ(popcount(~uint32_t{}), 32);
  EXPECT_EQ(popcount(uint64_t{}), 0);
  EXPECT_EQ(popcount(uint64_t{1}), 1);
  EXPECT_EQ(popcount(~uint64_t{}), 64);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(popcount(static_cast<uint8_t>(uint8_t{1} << i)), 1);
    EXPECT_EQ(popcount(static_cast<uint8_t>(static_cast<uint8_t>(-1) ^
                                            (uint8_t{1} << i))),
              7);
  }

  for (int i = 0; i < 16; i++) {
    EXPECT_EQ(popcount(static_cast<uint16_t>(uint16_t{1} << i)), 1);
    EXPECT_EQ(popcount(static_cast<uint16_t>(static_cast<uint16_t>(-1) ^
                                             (uint16_t{1} << i))),
              15);
  }

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(popcount(uint32_t{1} << i), 1);
    EXPECT_EQ(popcount(static_cast<uint32_t>(-1) ^ (uint32_t{1} << i)), 31);
  }

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(popcount(uint64_t{1} << i), 1);
    EXPECT_EQ(popcount(static_cast<uint64_t>(-1) ^ (uint64_t{1} << i)), 63);
  }
}

template <typename T>
struct PopcountInput {
  T value = 0;
  int expected = 0;
};

template <typename T>
PopcountInput<T> GeneratePopcountInput(absl::BitGen& gen) {
  PopcountInput<T> ret;
  for (int i = 0; i < std::numeric_limits<T>::digits; i++) {
    bool coin = absl::Bernoulli(gen, 0.2);
    if (coin) {
      ret.value |= T{1} << i;
      ret.expected++;
    }
  }
  return ret;
}

TEST(Counting, PopcountFuzz) {
  absl::BitGen rng;
  constexpr int kTrials = 100;

  for (int i = 0; i < kTrials; ++i) {
    auto input = GeneratePopcountInput<uint8_t>(rng);
    EXPECT_EQ(popcount(input.value), input.expected);
  }

  for (int i = 0; i < kTrials; ++i) {
    auto input = GeneratePopcountInput<uint16_t>(rng);
    EXPECT_EQ(popcount(input.value), input.expected);
  }

  for (int i = 0; i < kTrials; ++i) {
    auto input = GeneratePopcountInput<uint32_t>(rng);
    EXPECT_EQ(popcount(input.value), input.expected);
  }

  for (int i = 0; i < kTrials; ++i) {
    auto input = GeneratePopcountInput<uint64_t>(rng);
    EXPECT_EQ(popcount(input.value), input.expected);
  }
}

TEST(IntegralPowersOfTwo, SingleBit) {
  EXPECT_FALSE(has_single_bit(uint8_t{}));
  EXPECT_FALSE(has_single_bit(static_cast<uint8_t>(-1)));
  EXPECT_FALSE(has_single_bit(uint16_t{}));
  EXPECT_FALSE(has_single_bit(static_cast<uint16_t>(-1)));
  EXPECT_FALSE(has_single_bit(uint32_t{}));
  EXPECT_FALSE(has_single_bit(~uint32_t{}));
  EXPECT_FALSE(has_single_bit(uint64_t{}));
  EXPECT_FALSE(has_single_bit(~uint64_t{}));

  static_assert(!has_single_bit(0u), "");
  static_assert(has_single_bit(1u), "");
  static_assert(has_single_bit(2u), "");
  static_assert(!has_single_bit(3u), "");
  static_assert(has_single_bit(4u), "");
  static_assert(!has_single_bit(1337u), "");
  static_assert(has_single_bit(65536u), "");
  static_assert(has_single_bit(uint32_t{1} << 30), "");
  static_assert(has_single_bit(uint64_t{1} << 42), "");

  EXPECT_FALSE(has_single_bit(0u));
  EXPECT_TRUE(has_single_bit(1u));
  EXPECT_TRUE(has_single_bit(2u));
  EXPECT_FALSE(has_single_bit(3u));
  EXPECT_TRUE(has_single_bit(4u));
  EXPECT_FALSE(has_single_bit(1337u));
  EXPECT_TRUE(has_single_bit(65536u));
  EXPECT_TRUE(has_single_bit(uint32_t{1} << 30));
  EXPECT_TRUE(has_single_bit(uint64_t{1} << 42));

  EXPECT_TRUE(has_single_bit(
      static_cast<uint8_t>(std::numeric_limits<uint8_t>::max() / 2 + 1)));
  EXPECT_TRUE(has_single_bit(
      static_cast<uint16_t>(std::numeric_limits<uint16_t>::max() / 2 + 1)));
  EXPECT_TRUE(has_single_bit(
      static_cast<uint32_t>(std::numeric_limits<uint32_t>::max() / 2 + 1)));
  EXPECT_TRUE(has_single_bit(
      static_cast<uint64_t>(std::numeric_limits<uint64_t>::max() / 2 + 1)));
}

template <typename T, T arg, T = bit_ceil(arg)>
bool IsBitCeilConstantExpression(int) {
  return true;
}
template <typename T, T arg>
bool IsBitCeilConstantExpression(char) {
  return false;
}

TEST(IntegralPowersOfTwo, Ceiling) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(bit_ceil(0u) == 1, "");
  static_assert(bit_ceil(1u) == 1, "");
  static_assert(bit_ceil(2u) == 2, "");
  static_assert(bit_ceil(3u) == 4, "");
  static_assert(bit_ceil(4u) == 4, "");
  static_assert(bit_ceil(1337u) == 2048, "");
  static_assert(bit_ceil(65536u) == 65536, "");
  static_assert(bit_ceil(65536u - 1337u) == 65536, "");
  static_assert(bit_ceil(uint32_t{0x80000000}) == uint32_t{0x80000000}, "");
  static_assert(bit_ceil(uint64_t{0x40000000000}) == uint64_t{0x40000000000},
                "");
  static_assert(
      bit_ceil(uint64_t{0x8000000000000000}) == uint64_t{0x8000000000000000},
      "");

  EXPECT_TRUE((IsBitCeilConstantExpression<uint8_t, uint8_t{0x0}>(0)));
  EXPECT_TRUE((IsBitCeilConstantExpression<uint8_t, uint8_t{0x80}>(0)));
  EXPECT_FALSE((IsBitCeilConstantExpression<uint8_t, uint8_t{0x81}>(0)));
  EXPECT_FALSE((IsBitCeilConstantExpression<uint8_t, uint8_t{0xff}>(0)));

  EXPECT_TRUE((IsBitCeilConstantExpression<uint16_t, uint16_t{0x0}>(0)));
  EXPECT_TRUE((IsBitCeilConstantExpression<uint16_t, uint16_t{0x8000}>(0)));
  EXPECT_FALSE((IsBitCeilConstantExpression<uint16_t, uint16_t{0x8001}>(0)));
  EXPECT_FALSE((IsBitCeilConstantExpression<uint16_t, uint16_t{0xffff}>(0)));

  EXPECT_TRUE((IsBitCeilConstantExpression<uint32_t, uint32_t{0x0}>(0)));
  EXPECT_TRUE((IsBitCeilConstantExpression<uint32_t, uint32_t{0x80000000}>(0)));
  EXPECT_FALSE(
      (IsBitCeilConstantExpression<uint32_t, uint32_t{0x80000001}>(0)));
  EXPECT_FALSE(
      (IsBitCeilConstantExpression<uint32_t, uint32_t{0xffffffff}>(0)));

  EXPECT_TRUE((IsBitCeilConstantExpression<uint64_t, uint64_t{0x0}>(0)));
  EXPECT_TRUE(
      (IsBitCeilConstantExpression<uint64_t, uint64_t{0x8000000000000000}>(0)));
  EXPECT_FALSE(
      (IsBitCeilConstantExpression<uint64_t, uint64_t{0x8000000000000001}>(0)));
  EXPECT_FALSE(
      (IsBitCeilConstantExpression<uint64_t, uint64_t{0xffffffffffffffff}>(0)));
#endif

  EXPECT_EQ(bit_ceil(0u), 1);
  EXPECT_EQ(bit_ceil(1u), 1);
  EXPECT_EQ(bit_ceil(2u), 2);
  EXPECT_EQ(bit_ceil(3u), 4);
  EXPECT_EQ(bit_ceil(4u), 4);
  EXPECT_EQ(bit_ceil(1337u), 2048);
  EXPECT_EQ(bit_ceil(65536u), 65536);
  EXPECT_EQ(bit_ceil(65536u - 1337u), 65536);
  EXPECT_EQ(bit_ceil(uint64_t{0x40000000000}), uint64_t{0x40000000000});
}

TEST(IntegralPowersOfTwo, Floor) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(bit_floor(0u) == 0, "");
  static_assert(bit_floor(1u) == 1, "");
  static_assert(bit_floor(2u) == 2, "");
  static_assert(bit_floor(3u) == 2, "");
  static_assert(bit_floor(4u) == 4, "");
  static_assert(bit_floor(1337u) == 1024, "");
  static_assert(bit_floor(65536u) == 65536, "");
  static_assert(bit_floor(65536u - 1337u) == 32768, "");
  static_assert(bit_floor(uint64_t{0x40000000000}) == uint64_t{0x40000000000},
                "");
#endif

  EXPECT_EQ(bit_floor(0u), 0);
  EXPECT_EQ(bit_floor(1u), 1);
  EXPECT_EQ(bit_floor(2u), 2);
  EXPECT_EQ(bit_floor(3u), 2);
  EXPECT_EQ(bit_floor(4u), 4);
  EXPECT_EQ(bit_floor(1337u), 1024);
  EXPECT_EQ(bit_floor(65536u), 65536);
  EXPECT_EQ(bit_floor(65536u - 1337u), 32768);
  EXPECT_EQ(bit_floor(uint64_t{0x40000000000}), uint64_t{0x40000000000});

  for (int i = 0; i < 8; i++) {
    uint8_t input = uint8_t{1} << i;
    EXPECT_EQ(bit_floor(input), input);
    if (i > 0) {
      EXPECT_EQ(bit_floor(static_cast<uint8_t>(input + 1)), input);
    }
  }

  for (int i = 0; i < 16; i++) {
    uint16_t input = uint16_t{1} << i;
    EXPECT_EQ(bit_floor(input), input);
    if (i > 0) {
      EXPECT_EQ(bit_floor(static_cast<uint16_t>(input + 1)), input);
    }
  }

  for (int i = 0; i < 32; i++) {
    uint32_t input = uint32_t{1} << i;
    EXPECT_EQ(bit_floor(input), input);
    if (i > 0) {
      EXPECT_EQ(bit_floor(input + 1), input);
    }
  }

  for (int i = 0; i < 64; i++) {
    uint64_t input = uint64_t{1} << i;
    EXPECT_EQ(bit_floor(input), input);
    if (i > 0) {
      EXPECT_EQ(bit_floor(input + 1), input);
    }
  }
}

TEST(IntegralPowersOfTwo, Width) {
#if ABSL_INTERNAL_HAS_CONSTEXPR_CLZ
  static_assert(bit_width(uint8_t{}) == 0, "");
  static_assert(bit_width(uint8_t{1}) == 1, "");
  static_assert(bit_width(uint8_t{3}) == 2, "");
  static_assert(bit_width(static_cast<uint8_t>(-1)) == 8, "");
  static_assert(bit_width(uint16_t{}) == 0, "");
  static_assert(bit_width(uint16_t{1}) == 1, "");
  static_assert(bit_width(uint16_t{3}) == 2, "");
  static_assert(bit_width(static_cast<uint16_t>(-1)) == 16, "");
  static_assert(bit_width(uint32_t{}) == 0, "");
  static_assert(bit_width(uint32_t{1}) == 1, "");
  static_assert(bit_width(uint32_t{3}) == 2, "");
  static_assert(bit_width(~uint32_t{}) == 32, "");
  static_assert(bit_width(uint64_t{}) == 0, "");
  static_assert(bit_width(uint64_t{1}) == 1, "");
  static_assert(bit_width(uint64_t{3}) == 2, "");
  static_assert(bit_width(~uint64_t{}) == 64, "");
#endif

  EXPECT_EQ(bit_width(uint8_t{}), 0);
  EXPECT_EQ(bit_width(uint8_t{1}), 1);
  EXPECT_EQ(bit_width(uint8_t{3}), 2);
  EXPECT_EQ(bit_width(static_cast<uint8_t>(-1)), 8);
  EXPECT_EQ(bit_width(uint16_t{}), 0);
  EXPECT_EQ(bit_width(uint16_t{1}), 1);
  EXPECT_EQ(bit_width(uint16_t{3}), 2);
  EXPECT_EQ(bit_width(static_cast<uint16_t>(-1)), 16);
  EXPECT_EQ(bit_width(uint32_t{}), 0);
  EXPECT_EQ(bit_width(uint32_t{1}), 1);
  EXPECT_EQ(bit_width(uint32_t{3}), 2);
  EXPECT_EQ(bit_width(~uint32_t{}), 32);
  EXPECT_EQ(bit_width(uint64_t{}), 0);
  EXPECT_EQ(bit_width(uint64_t{1}), 1);
  EXPECT_EQ(bit_width(uint64_t{3}), 2);
  EXPECT_EQ(bit_width(~uint64_t{}), 64);

  for (int i = 0; i < 8; i++) {
    EXPECT_EQ(bit_width(static_cast<uint8_t>(uint8_t{1} << i)), i + 1);
  }

  for (int i = 0; i < 16; i++) {
    EXPECT_EQ(bit_width(static_cast<uint16_t>(uint16_t{1} << i)), i + 1);
  }

  for (int i = 0; i < 32; i++) {
    EXPECT_EQ(bit_width(uint32_t{1} << i), i + 1);
  }

  for (int i = 0; i < 64; i++) {
    EXPECT_EQ(bit_width(uint64_t{1} << i), i + 1);
  }
}

// On GCC and Clang, anticiapte that implementations will be constexpr
#if defined(__GNUC__)
static_assert(ABSL_INTERNAL_HAS_CONSTEXPR_POPCOUNT,
              "popcount should be constexpr");
static_assert(ABSL_INTERNAL_HAS_CONSTEXPR_CLZ, "clz should be constexpr");
static_assert(ABSL_INTERNAL_HAS_CONSTEXPR_CTZ, "ctz should be constexpr");
#endif

}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
