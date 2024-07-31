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

#include "absl/random/internal/generate_real.h"

#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "absl/flags/flag.h"
#include "absl/numeric/bits.h"

ABSL_FLAG(int64_t, absl_random_test_trials, 50000,
          "Number of trials for the probability tests.");

using absl::random_internal::GenerateNegativeTag;
using absl::random_internal::GeneratePositiveTag;
using absl::random_internal::GenerateRealFromBits;
using absl::random_internal::GenerateSignedTag;

namespace {

TEST(GenerateRealTest, U64ToFloat_Positive_NoZero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GeneratePositiveTag, false>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 2.710505431e-20f);
  EXPECT_EQ(ToFloat(0x0000000000000001), 5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x8000000000000000), 0.5);
  EXPECT_EQ(ToFloat(0x8000000000000001), 0.5);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), 0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Positive_Zero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GeneratePositiveTag, true>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 0.0);
  EXPECT_EQ(ToFloat(0x0000000000000001), 5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x8000000000000000), 0.5);
  EXPECT_EQ(ToFloat(0x8000000000000001), 0.5);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), 0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Negative_NoZero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GenerateNegativeTag, false>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), -2.710505431e-20f);
  EXPECT_EQ(ToFloat(0x0000000000000001), -5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x8000000000000000), -0.5);
  EXPECT_EQ(ToFloat(0x8000000000000001), -0.5);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), -0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Negative_Zero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GenerateNegativeTag, true>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 0.0);
  EXPECT_EQ(ToFloat(0x0000000000000001), -5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x8000000000000000), -0.5);
  EXPECT_EQ(ToFloat(0x8000000000000001), -0.5);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), -0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Signed_NoZero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GenerateSignedTag, false>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x0000000000000001), 1.084202172e-19f);
  EXPECT_EQ(ToFloat(0x7FFFFFFFFFFFFFFF), 0.9999999404f);
  EXPECT_EQ(ToFloat(0x8000000000000000), -5.421010862e-20f);
  EXPECT_EQ(ToFloat(0x8000000000000001), -1.084202172e-19f);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), -0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Signed_Zero_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GenerateSignedTag, true>(a);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 0);
  EXPECT_EQ(ToFloat(0x0000000000000001), 1.084202172e-19f);
  EXPECT_EQ(ToFloat(0x7FFFFFFFFFFFFFFF), 0.9999999404f);
  EXPECT_EQ(ToFloat(0x8000000000000000), 0);
  EXPECT_EQ(ToFloat(0x8000000000000001), -1.084202172e-19f);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), -0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloat_Signed_Bias_Test) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GenerateSignedTag, true>(a, 1);
  };
  EXPECT_EQ(ToFloat(0x0000000000000000), 0);
  EXPECT_EQ(ToFloat(0x0000000000000001), 2 * 1.084202172e-19f);
  EXPECT_EQ(ToFloat(0x7FFFFFFFFFFFFFFF), 2 * 0.9999999404f);
  EXPECT_EQ(ToFloat(0x8000000000000000), 0);
  EXPECT_EQ(ToFloat(0x8000000000000001), 2 * -1.084202172e-19f);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), 2 * -0.9999999404f);
}

TEST(GenerateRealTest, U64ToFloatTest) {
  auto ToFloat = [](uint64_t a) -> float {
    return GenerateRealFromBits<float, GeneratePositiveTag, true>(a);
  };

  EXPECT_EQ(ToFloat(0x0000000000000000), 0.0f);

  EXPECT_EQ(ToFloat(0x8000000000000000), 0.5f);
  EXPECT_EQ(ToFloat(0x8000000000000001), 0.5f);
  EXPECT_EQ(ToFloat(0x800000FFFFFFFFFF), 0.5f);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), 0.9999999404f);

  EXPECT_GT(ToFloat(0x0000000000000001), 0.0f);

  EXPECT_NE(ToFloat(0x7FFFFF0000000000), ToFloat(0x7FFFFEFFFFFFFFFF));

  EXPECT_LT(ToFloat(0xFFFFFFFFFFFFFFFF), 1.0f);
  int32_t two_to_24 = 1 << 24;
  EXPECT_EQ(static_cast<int32_t>(ToFloat(0xFFFFFFFFFFFFFFFF) * two_to_24),
            two_to_24 - 1);
  EXPECT_NE(static_cast<int32_t>(ToFloat(0xFFFFFFFFFFFFFFFF) * two_to_24 * 2),
            two_to_24 * 2 - 1);
  EXPECT_EQ(ToFloat(0xFFFFFFFFFFFFFFFF), ToFloat(0xFFFFFF0000000000));
  EXPECT_NE(ToFloat(0xFFFFFFFFFFFFFFFF), ToFloat(0xFFFFFEFFFFFFFFFF));
  EXPECT_EQ(ToFloat(0x7FFFFFFFFFFFFFFF), ToFloat(0x7FFFFF8000000000));
  EXPECT_NE(ToFloat(0x7FFFFFFFFFFFFFFF), ToFloat(0x7FFFFF7FFFFFFFFF));
  EXPECT_EQ(ToFloat(0x3FFFFFFFFFFFFFFF), ToFloat(0x3FFFFFC000000000));
  EXPECT_NE(ToFloat(0x3FFFFFFFFFFFFFFF), ToFloat(0x3FFFFFBFFFFFFFFF));

  // For values where every bit counts, the values scale as multiples of the
  // input.
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(i * ToFloat(0x0000000000000001), ToFloat(i));
  }

  // For each i: value generated from (1 << i).
  float exp_values[64];
  exp_values[63] = 0.5f;
  for (int i = 62; i >= 0; --i) exp_values[i] = 0.5f * exp_values[i + 1];
  constexpr uint64_t one = 1;
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(ToFloat(one << i), exp_values[i]);
    for (int j = 1; j < FLT_MANT_DIG && i - j >= 0; ++j) {
      EXPECT_NE(exp_values[i] + exp_values[i - j], exp_values[i]);
      EXPECT_EQ(ToFloat((one << i) + (one << (i - j))),
                exp_values[i] + exp_values[i - j]);
    }
    for (int j = FLT_MANT_DIG; i - j >= 0; ++j) {
      EXPECT_EQ(exp_values[i] + exp_values[i - j], exp_values[i]);
      EXPECT_EQ(ToFloat((one << i) + (one << (i - j))), exp_values[i]);
    }
  }
}

TEST(GenerateRealTest, U64ToDouble_Positive_NoZero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GeneratePositiveTag, false>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 2.710505431213761085e-20);
  EXPECT_EQ(ToDouble(0x0000000000000001), 5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x0000000000000002), 1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0x8000000000000000), 0.5);
  EXPECT_EQ(ToDouble(0x8000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), 0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_Positive_Zero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GeneratePositiveTag, true>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 0.0);
  EXPECT_EQ(ToDouble(0x0000000000000001), 5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x8000000000000000), 0.5);
  EXPECT_EQ(ToDouble(0x8000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), 0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_Negative_NoZero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateNegativeTag, false>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), -2.710505431213761085e-20);
  EXPECT_EQ(ToDouble(0x0000000000000001), -5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x0000000000000002), -1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0x8000000000000000), -0.5);
  EXPECT_EQ(ToDouble(0x8000000000000001), -0.5);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), -0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_Negative_Zero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateNegativeTag, true>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 0.0);
  EXPECT_EQ(ToDouble(0x0000000000000001), -5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x0000000000000002), -1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0x8000000000000000), -0.5);
  EXPECT_EQ(ToDouble(0x8000000000000001), -0.5);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), -0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_Signed_NoZero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateSignedTag, false>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x0000000000000001), 1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFF), 0.999999999999999888978);
  EXPECT_EQ(ToDouble(0x8000000000000000), -5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x8000000000000001), -1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), -0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_Signed_Zero_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateSignedTag, true>(a);
  };
  EXPECT_EQ(ToDouble(0x0000000000000000), 0);
  EXPECT_EQ(ToDouble(0x0000000000000001), 1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFF), 0.999999999999999888978);
  EXPECT_EQ(ToDouble(0x8000000000000000), 0);
  EXPECT_EQ(ToDouble(0x8000000000000001), -1.084202172485504434e-19);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), -0.999999999999999888978);
}

TEST(GenerateRealTest, U64ToDouble_GenerateSignedTag_Bias_Test) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateSignedTag, true>(a, -1);
  };
  EXPECT_EQ(ToDouble(0x0000000000000000), 0);
  EXPECT_EQ(ToDouble(0x0000000000000001), 1.084202172485504434e-19 / 2);
  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFF), 0.999999999999999888978 / 2);
  EXPECT_EQ(ToDouble(0x8000000000000000), 0);
  EXPECT_EQ(ToDouble(0x8000000000000001), -1.084202172485504434e-19 / 2);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), -0.999999999999999888978 / 2);
}

TEST(GenerateRealTest, U64ToDoubleTest) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GeneratePositiveTag, true>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 0.0);
  EXPECT_EQ(ToDouble(0x0000000000000000), 0.0);

  EXPECT_EQ(ToDouble(0x0000000000000001), 5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x7fffffffffffffef), 0.499999999999999944489);
  EXPECT_EQ(ToDouble(0x8000000000000000), 0.5);

  // For values > 0.5, RandU64ToDouble discards up to 11 bits. (64-53).
  EXPECT_EQ(ToDouble(0x8000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0x80000000000007FF), 0.5);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), 0.999999999999999888978);
  EXPECT_NE(ToDouble(0x7FFFFFFFFFFFF800), ToDouble(0x7FFFFFFFFFFFF7FF));

  EXPECT_LT(ToDouble(0xFFFFFFFFFFFFFFFF), 1.0);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFF), ToDouble(0xFFFFFFFFFFFFF800));
  EXPECT_NE(ToDouble(0xFFFFFFFFFFFFFFFF), ToDouble(0xFFFFFFFFFFFFF7FF));
  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFF), ToDouble(0x7FFFFFFFFFFFFC00));
  EXPECT_NE(ToDouble(0x7FFFFFFFFFFFFFFF), ToDouble(0x7FFFFFFFFFFFFBFF));
  EXPECT_EQ(ToDouble(0x3FFFFFFFFFFFFFFF), ToDouble(0x3FFFFFFFFFFFFE00));
  EXPECT_NE(ToDouble(0x3FFFFFFFFFFFFFFF), ToDouble(0x3FFFFFFFFFFFFDFF));

  EXPECT_EQ(ToDouble(0x1000000000000001), 0.0625);
  EXPECT_EQ(ToDouble(0x2000000000000001), 0.125);
  EXPECT_EQ(ToDouble(0x3000000000000001), 0.1875);
  EXPECT_EQ(ToDouble(0x4000000000000001), 0.25);
  EXPECT_EQ(ToDouble(0x5000000000000001), 0.3125);
  EXPECT_EQ(ToDouble(0x6000000000000001), 0.375);
  EXPECT_EQ(ToDouble(0x7000000000000001), 0.4375);
  EXPECT_EQ(ToDouble(0x8000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0x9000000000000001), 0.5625);
  EXPECT_EQ(ToDouble(0xa000000000000001), 0.625);
  EXPECT_EQ(ToDouble(0xb000000000000001), 0.6875);
  EXPECT_EQ(ToDouble(0xc000000000000001), 0.75);
  EXPECT_EQ(ToDouble(0xd000000000000001), 0.8125);
  EXPECT_EQ(ToDouble(0xe000000000000001), 0.875);
  EXPECT_EQ(ToDouble(0xf000000000000001), 0.9375);

  // Large powers of 2.
  int64_t two_to_53 = int64_t{1} << 53;
  EXPECT_EQ(static_cast<int64_t>(ToDouble(0xFFFFFFFFFFFFFFFF) * two_to_53),
            two_to_53 - 1);
  EXPECT_NE(static_cast<int64_t>(ToDouble(0xFFFFFFFFFFFFFFFF) * two_to_53 * 2),
            two_to_53 * 2 - 1);

  // For values where every bit counts, the values scale as multiples of the
  // input.
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(i * ToDouble(0x0000000000000001), ToDouble(i));
  }

  // For each i: value generated from (1 << i).
  double exp_values[64];
  exp_values[63] = 0.5;
  for (int i = 62; i >= 0; --i) exp_values[i] = 0.5 * exp_values[i + 1];
  constexpr uint64_t one = 1;
  for (int i = 0; i < 64; ++i) {
    EXPECT_EQ(ToDouble(one << i), exp_values[i]);
    for (int j = 1; j < DBL_MANT_DIG && i - j >= 0; ++j) {
      EXPECT_NE(exp_values[i] + exp_values[i - j], exp_values[i]);
      EXPECT_EQ(ToDouble((one << i) + (one << (i - j))),
                exp_values[i] + exp_values[i - j]);
    }
    for (int j = DBL_MANT_DIG; i - j >= 0; ++j) {
      EXPECT_EQ(exp_values[i] + exp_values[i - j], exp_values[i]);
      EXPECT_EQ(ToDouble((one << i) + (one << (i - j))), exp_values[i]);
    }
  }
}

TEST(GenerateRealTest, U64ToDoubleSignedTest) {
  auto ToDouble = [](uint64_t a) {
    return GenerateRealFromBits<double, GenerateSignedTag, false>(a);
  };

  EXPECT_EQ(ToDouble(0x0000000000000000), 5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x0000000000000001), 1.084202172485504434e-19);

  EXPECT_EQ(ToDouble(0x8000000000000000), -5.42101086242752217004e-20);
  EXPECT_EQ(ToDouble(0x8000000000000001), -1.084202172485504434e-19);

  const double e_plus = ToDouble(0x0000000000000001);
  const double e_minus = ToDouble(0x8000000000000001);
  EXPECT_EQ(e_plus, 1.084202172485504434e-19);
  EXPECT_EQ(e_minus, -1.084202172485504434e-19);

  EXPECT_EQ(ToDouble(0x3fffffffffffffef), 0.499999999999999944489);
  EXPECT_EQ(ToDouble(0xbfffffffffffffef), -0.499999999999999944489);

  // For values > 0.5, RandU64ToDouble discards up to 10 bits. (63-53).
  EXPECT_EQ(ToDouble(0x4000000000000000), 0.5);
  EXPECT_EQ(ToDouble(0x4000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0x40000000000003FF), 0.5);

  EXPECT_EQ(ToDouble(0xC000000000000000), -0.5);
  EXPECT_EQ(ToDouble(0xC000000000000001), -0.5);
  EXPECT_EQ(ToDouble(0xC0000000000003FF), -0.5);

  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFe), 0.999999999999999888978);
  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFe), -0.999999999999999888978);

  EXPECT_NE(ToDouble(0x7FFFFFFFFFFFF800), ToDouble(0x7FFFFFFFFFFFF7FF));

  EXPECT_LT(ToDouble(0x7FFFFFFFFFFFFFFF), 1.0);
  EXPECT_GT(ToDouble(0x7FFFFFFFFFFFFFFF), 0.9999999999);

  EXPECT_GT(ToDouble(0xFFFFFFFFFFFFFFFe), -1.0);
  EXPECT_LT(ToDouble(0xFFFFFFFFFFFFFFFe), -0.999999999);

  EXPECT_EQ(ToDouble(0xFFFFFFFFFFFFFFFe), ToDouble(0xFFFFFFFFFFFFFC00));
  EXPECT_EQ(ToDouble(0x7FFFFFFFFFFFFFFF), ToDouble(0x7FFFFFFFFFFFFC00));
  EXPECT_NE(ToDouble(0xFFFFFFFFFFFFFFFe), ToDouble(0xFFFFFFFFFFFFF3FF));
  EXPECT_NE(ToDouble(0x7FFFFFFFFFFFFFFF), ToDouble(0x7FFFFFFFFFFFF3FF));

  EXPECT_EQ(ToDouble(0x1000000000000001), 0.125);
  EXPECT_EQ(ToDouble(0x2000000000000001), 0.25);
  EXPECT_EQ(ToDouble(0x3000000000000001), 0.375);
  EXPECT_EQ(ToDouble(0x4000000000000001), 0.5);
  EXPECT_EQ(ToDouble(0x5000000000000001), 0.625);
  EXPECT_EQ(ToDouble(0x6000000000000001), 0.75);
  EXPECT_EQ(ToDouble(0x7000000000000001), 0.875);
  EXPECT_EQ(ToDouble(0x7800000000000001), 0.9375);
  EXPECT_EQ(ToDouble(0x7c00000000000001), 0.96875);
  EXPECT_EQ(ToDouble(0x7e00000000000001), 0.984375);
  EXPECT_EQ(ToDouble(0x7f00000000000001), 0.9921875);

  // 0x8000000000000000 ~= 0
  EXPECT_EQ(ToDouble(0x9000000000000001), -0.125);
  EXPECT_EQ(ToDouble(0xa000000000000001), -0.25);
  EXPECT_EQ(ToDouble(0xb000000000000001), -0.375);
  EXPECT_EQ(ToDouble(0xc000000000000001), -0.5);
  EXPECT_EQ(ToDouble(0xd000000000000001), -0.625);
  EXPECT_EQ(ToDouble(0xe000000000000001), -0.75);
  EXPECT_EQ(ToDouble(0xf000000000000001), -0.875);

  // Large powers of 2.
  int64_t two_to_53 = int64_t{1} << 53;
  EXPECT_EQ(static_cast<int64_t>(ToDouble(0x7FFFFFFFFFFFFFFF) * two_to_53),
            two_to_53 - 1);
  EXPECT_EQ(static_cast<int64_t>(ToDouble(0xFFFFFFFFFFFFFFFF) * two_to_53),
            -(two_to_53 - 1));

  EXPECT_NE(static_cast<int64_t>(ToDouble(0x7FFFFFFFFFFFFFFF) * two_to_53 * 2),
            two_to_53 * 2 - 1);

  // For values where every bit counts, the values scale as multiples of the
  // input.
  for (int i = 1; i < 100; ++i) {
    EXPECT_EQ(i * e_plus, ToDouble(i)) << i;
    EXPECT_EQ(i * e_minus, ToDouble(0x8000000000000000 | i)) << i;
  }
}

TEST(GenerateRealTest, ExhaustiveFloat) {
  auto ToFloat = [](uint64_t a) {
    return GenerateRealFromBits<float, GeneratePositiveTag, true>(a);
  };

  // Rely on RandU64ToFloat generating values from greatest to least when
  // supplied with uint64_t values from greatest (0xfff...) to least (0x0).
  // Thus, this algorithm stores the previous value, and if the new value is at
  // greater than or equal to the previous value, then there is a collision in
  // the generation algorithm.
  //
  // Use the computation below to convert the random value into a result:
  //   double res = a() * (1.0f - sample) + b() * sample;
  float last_f = 1.0, last_g = 2.0;
  uint64_t f_collisions = 0, g_collisions = 0;
  uint64_t f_unique = 0, g_unique = 0;
  uint64_t total = 0;
  auto count = [&](const float r) {
    total++;
    // `f` is mapped to the range [0, 1) (default)
    const float f = 0.0f * (1.0f - r) + 1.0f * r;
    if (f >= last_f) {
      f_collisions++;
    } else {
      f_unique++;
      last_f = f;
    }
    // `g` is mapped to the range [1, 2)
    const float g = 1.0f * (1.0f - r) + 2.0f * r;
    if (g >= last_g) {
      g_collisions++;
    } else {
      g_unique++;
      last_g = g;
    }
  };

  size_t limit = absl::GetFlag(FLAGS_absl_random_test_trials);

  // Generate all uint64_t which have unique floating point values.
  // Counting down from 0xFFFFFFFFFFFFFFFFu ... 0x0u
  uint64_t x = ~uint64_t(0);
  for (; x != 0 && limit > 0;) {
    constexpr int kDig = (64 - FLT_MANT_DIG);
    // Set a decrement value & the next point at which to change
    // the decrement value. By default these are 1, 0.
    uint64_t dec = 1;
    uint64_t chk = 0;

    // Adjust decrement and check value based on how many leading 0
    // bits are set in the current value.
    const int clz = absl::countl_zero(x);
    if (clz < kDig) {
      dec <<= (kDig - clz);
      chk = (~uint64_t(0)) >> (clz + 1);
    }
    for (; x > chk && limit > 0; x -= dec) {
      count(ToFloat(x));
      --limit;
    }
  }

  static_assert(FLT_MANT_DIG == 24,
                "The float type is expected to have a 24 bit mantissa.");

  if (limit != 0) {
    // There are between 2^28 and 2^29 unique values in the range [0, 1).  For
    // the low values of x, there are 2^24 -1 unique values.  Once x > 2^24,
    // there are 40 * 2^24 unique values. Thus:
    // (2 + 4 + 8 ... + 2^23) + 40 * 2^23
    EXPECT_LT(1 << 28, f_unique);
    EXPECT_EQ((1 << 24) + 40 * (1 << 23) - 1, f_unique);
    EXPECT_EQ(total, f_unique);
    EXPECT_EQ(0, f_collisions);

    // Expect at least 2^23 unique values for the range [1, 2)
    EXPECT_LE(1 << 23, g_unique);
    EXPECT_EQ(total - g_unique, g_collisions);
  }
}

}  // namespace
