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

#include <stddef.h>
#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/reduction_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestSumOfLanes {
  template <typename D,
            hwy::EnableIf<!IsSigned<TFromD<D>>() ||
                          ((HWY_MAX_LANES_D(D) & 1) != 0)>* = nullptr>
  HWY_NOINLINE void SignedEvenLengthVectorTests(D /*d*/) {
    // do nothing
  }
  template <typename D,
            hwy::EnableIf<IsSigned<TFromD<D>>() &&
                          ((HWY_MAX_LANES_D(D) & 1) == 0)>* = nullptr>
  HWY_NOINLINE void SignedEvenLengthVectorTests(D d) {
    using T = TFromD<D>;

    const size_t lanes = Lanes(d);

#if HWY_HAVE_SCALABLE
    // On platforms that use scalable vectors, it is possible for Lanes(d) to be
    // odd but for MaxLanes(d) to be even if Lanes(d) < 2 is true.
    if (lanes < 2) return;
#endif

    const T pairs = ConvertScalarTo<T>(lanes / 2);

    // Lanes are the repeated sequence -2, 1, [...]; each pair sums to -1,
    // so the eventual total is just -(N/2).
    Vec<decltype(d)> v = InterleaveLower(Set(d, ConvertScalarTo<T>(-2)),
                                         Set(d, ConvertScalarTo<T>(1)));
    HWY_ASSERT_VEC_EQ(d, Set(d, ConvertScalarTo<T>(-pairs)), SumOfLanes(d, v));
    HWY_ASSERT_EQ(ConvertScalarTo<T>(-pairs), ReduceSum(d, v));

    // Similar test with a positive result.
    v = InterleaveLower(Set(d, ConvertScalarTo<T>(-2)),
                        Set(d, ConvertScalarTo<T>(4)));
    HWY_ASSERT_VEC_EQ(d,
                      Set(d, ConvertScalarTo<T>(pairs * ConvertScalarTo<T>(2))),
                      SumOfLanes(d, v));
    HWY_ASSERT_EQ(ConvertScalarTo<T>(pairs * ConvertScalarTo<T>(2)),
                  ReduceSum(d, v));
  }

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(in_lanes);

    // Lane i = bit i, higher lanes 0
    T sum = ConvertScalarTo<T>(0);
    // Avoid setting sign bit and cap so that f16 precision is not exceeded.
    constexpr size_t kBits = HWY_MIN(sizeof(T) * 8 - 1, 9);
    for (size_t i = 0; i < N; ++i) {
      in_lanes[i] = ConvertScalarTo<T>(i < kBits ? 1ull << i : 0ull);
      sum = AddWithWraparound(sum, in_lanes[i]);
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, sum), SumOfLanes(d, Load(d, in_lanes.get())));
    HWY_ASSERT_EQ(T(sum), ReduceSum(d, Load(d, in_lanes.get())));
    // Lane i = i (iota) to include upper lanes
    sum = ConvertScalarTo<T>(0);
    for (size_t i = 0; i < N; ++i) {
      sum = AddWithWraparound(sum, ConvertScalarTo<T>(i));
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, sum), SumOfLanes(d, Iota(d, 0)));
    HWY_ASSERT_EQ(T(sum), ReduceSum(d, Iota(d, 0)));

    // Run more tests only for signed types with even vector lengths. Some of
    // this code may not otherwise compile, so put it in a templated function.
    SignedEvenLengthVectorTests(d);
  }
};

HWY_NOINLINE void TestAllSumOfLanes() {
  ForAllTypes(ForPartialVectors<TestSumOfLanes>());
}

struct TestMinOfLanes {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(in_lanes);

    // Lane i = bit i, higher lanes = 2 (not the minimum)
    T min = HighestValue<T>();
    // Avoid setting sign bit and cap at double precision
    constexpr size_t kBits = HWY_MIN(sizeof(T) * 8 - 1, 51);
    for (size_t i = 0; i < N; ++i) {
      in_lanes[i] = ConvertScalarTo<T>(i < kBits ? 1ull << i : 2ull);
      min = HWY_MIN(min, in_lanes[i]);
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, min), MinOfLanes(d, Load(d, in_lanes.get())));

    // Lane i = N - i to include upper lanes
    min = HighestValue<T>();
    for (size_t i = 0; i < N; ++i) {
      in_lanes[i] = ConvertScalarTo<T>(N - i);  // no 8-bit T so no wraparound
      min = HWY_MIN(min, in_lanes[i]);
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, min), MinOfLanes(d, Load(d, in_lanes.get())));

    // Bug #910: also check negative values
    min = HighestValue<T>();
    const T input_copy[] = {ConvertScalarTo<T>(-1),
                            ConvertScalarTo<T>(-2),
                            1,
                            2,
                            3,
                            4,
                            5,
                            6,
                            7,
                            8,
                            9,
                            10,
                            11,
                            12,
                            13,
                            14};
    size_t i = 0;
    for (; i < HWY_MIN(N, sizeof(input_copy) / sizeof(T)); ++i) {
      in_lanes[i] = input_copy[i];
      min = HWY_MIN(min, input_copy[i]);
    }
    // Pad with neutral element to full vector (so we can load)
    for (; i < N; ++i) {
      in_lanes[i] = min;
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, min), MinOfLanes(d, Load(d, in_lanes.get())));
    HWY_ASSERT_EQ(min, ReduceMin(d, Load(d, in_lanes.get())));
  }
};

struct TestMaxOfLanes {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in_lanes = AllocateAligned<T>(N);
    HWY_ASSERT(in_lanes);

    T max = LowestValue<T>();
    // Avoid setting sign bit and cap at double precision
    constexpr size_t kBits = HWY_MIN(sizeof(T) * 8 - 1, 51);
    for (size_t i = 0; i < N; ++i) {
      in_lanes[i] = ConvertScalarTo<T>(i < kBits ? 1ull << i : 0ull);
      max = HWY_MAX(max, in_lanes[i]);
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, max), MaxOfLanes(d, Load(d, in_lanes.get())));

    // Lane i = i to include upper lanes
    max = LowestValue<T>();
    for (size_t i = 0; i < N; ++i) {
      in_lanes[i] = ConvertScalarTo<T>(i);  // no 8-bit T so no wraparound
      max = HWY_MAX(max, in_lanes[i]);
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, max), MaxOfLanes(d, Load(d, in_lanes.get())));

    // Bug #910: also check negative values
    max = LowestValue<T>();
    const T input_copy[] = {ConvertScalarTo<T>(-1),
                            ConvertScalarTo<T>(-2),
                            1,
                            2,
                            3,
                            4,
                            5,
                            6,
                            7,
                            8,
                            9,
                            10,
                            11,
                            12,
                            13,
                            14};
    size_t i = 0;
    for (; i < HWY_MIN(N, sizeof(input_copy) / sizeof(T)); ++i) {
      in_lanes[i] = input_copy[i];
      max = HWY_MAX(max, in_lanes[i]);
    }
    // Pad with neutral element to full vector (so we can load)
    for (; i < N; ++i) {
      in_lanes[i] = max;
    }
    HWY_ASSERT_VEC_EQ(d, Set(d, max), MaxOfLanes(d, Load(d, in_lanes.get())));
    HWY_ASSERT_EQ(max, ReduceMax(d, Load(d, in_lanes.get())));
  }
};

HWY_NOINLINE void TestAllMinMaxOfLanes() {
  ForAllTypes(ForPartialVectors<TestMinOfLanes>());
  ForAllTypes(ForPartialVectors<TestMaxOfLanes>());
}

struct TestSumsOf2 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TW = MakeWide<T>;

    const size_t N = Lanes(d);
    if (N < 2) return;
    const RepartitionToWide<D> dw;

    auto in_lanes = AllocateAligned<T>(N);
    auto sum_lanes = AllocateAligned<TW>(N / 2);
    HWY_ASSERT(in_lanes && sum_lanes);

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        in_lanes[i] = RandomFiniteValue<T>(&rng);
      }

      for (size_t idx_sum = 0; idx_sum < N / 2; ++idx_sum) {
        TW sum = static_cast<TW>(static_cast<TW>(in_lanes[idx_sum * 2]) +
                                 static_cast<TW>(in_lanes[idx_sum * 2 + 1]));
        sum_lanes[idx_sum] = sum;
      }

      const Vec<D> in = Load(d, in_lanes.get());
      HWY_ASSERT_VEC_EQ(dw, sum_lanes.get(), SumsOf2(in));
    }
  }
};

HWY_NOINLINE void TestAllSumsOf2() {
  ForGEVectors<16, TestSumsOf2>()(int8_t());
  ForGEVectors<16, TestSumsOf2>()(uint8_t());

  ForGEVectors<32, TestSumsOf2>()(int16_t());
  ForGEVectors<32, TestSumsOf2>()(uint16_t());
#if HWY_HAVE_FLOAT16
  ForGEVectors<32, TestSumsOf2>()(float16_t());
#endif

#if HWY_HAVE_INTEGER64
  ForGEVectors<64, TestSumsOf2>()(int32_t());
  ForGEVectors<64, TestSumsOf2>()(uint32_t());
#endif

#if HWY_HAVE_FLOAT64
  ForGEVectors<64, TestSumsOf2>()(float());
#endif
}

struct TestSumsOf4 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TW = MakeWide<T>;
    using TW2 = MakeWide<TW>;

    const size_t N = Lanes(d);
    if (N < 4) return;
    const Repartition<TW2, D> dw2;

    auto in_lanes = AllocateAligned<T>(N);
    auto sum_lanes = AllocateAligned<TW2>(N / 4);
    HWY_ASSERT(in_lanes && sum_lanes);

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        in_lanes[i] = RandomFiniteValue<T>(&rng);
      }

      for (size_t idx_sum = 0; idx_sum < N / 4; ++idx_sum) {
        TW2 sum = static_cast<TW2>(static_cast<TW>(in_lanes[idx_sum * 4]) +
                                   static_cast<TW>(in_lanes[idx_sum * 4 + 1]) +
                                   static_cast<TW>(in_lanes[idx_sum * 4 + 2]) +
                                   static_cast<TW>(in_lanes[idx_sum * 4 + 3]));
        sum_lanes[idx_sum] = sum;
      }

      const Vec<D> in = Load(d, in_lanes.get());
      HWY_ASSERT_VEC_EQ(dw2, sum_lanes.get(), SumsOf4(in));
    }
  }
};

HWY_NOINLINE void TestAllSumsOf4() {
  ForGEVectors<32, TestSumsOf4>()(int8_t());
  ForGEVectors<32, TestSumsOf4>()(uint8_t());

#if HWY_HAVE_INTEGER64
  ForGEVectors<64, TestSumsOf4>()(int16_t());
  ForGEVectors<64, TestSumsOf4>()(uint16_t());
#endif
}

struct TestSumsOf8 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TW = MakeWide<MakeWide<MakeWide<T>>>;

    const size_t N = Lanes(d);
    if (N < 8) return;
    const Repartition<TW, D> d64;

    auto in_lanes = AllocateAligned<T>(N);
    auto sum_lanes = AllocateAligned<TW>(N / 8);
    HWY_ASSERT(in_lanes && sum_lanes);

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        in_lanes[i] = ConvertScalarTo<T>(Random64(&rng) & 0xFF);
      }

      for (size_t idx_sum = 0; idx_sum < N / 8; ++idx_sum) {
        TW sum = 0;
        for (size_t i = 0; i < 8; ++i) {
          sum += in_lanes[idx_sum * 8 + i];
        }
        sum_lanes[idx_sum] = sum;
      }

      const Vec<D> in = Load(d, in_lanes.get());
      HWY_ASSERT_VEC_EQ(d64, sum_lanes.get(), SumsOf8(in));
    }
  }
};

HWY_NOINLINE void TestAllSumsOf8() {
  ForGEVectors<64, TestSumsOf8>()(int8_t());
  ForGEVectors<64, TestSumsOf8>()(uint8_t());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyReductionTest);
HWY_EXPORT_AND_TEST_P(HwyReductionTest, TestAllSumOfLanes);
HWY_EXPORT_AND_TEST_P(HwyReductionTest, TestAllMinMaxOfLanes);
HWY_EXPORT_AND_TEST_P(HwyReductionTest, TestAllSumsOf2);
HWY_EXPORT_AND_TEST_P(HwyReductionTest, TestAllSumsOf4);
HWY_EXPORT_AND_TEST_P(HwyReductionTest, TestAllSumsOf8);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
