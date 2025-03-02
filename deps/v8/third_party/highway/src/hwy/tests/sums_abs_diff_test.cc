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
#define HWY_TARGET_INCLUDE "tests/sums_abs_diff_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestSumsOf8AbsDiff {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TW = MakeWide<MakeWide<MakeWide<T>>>;

    const size_t N = Lanes(d);
    if (N < 8) return;
    const Repartition<TW, D> d64;

    auto in_lanes_a = AllocateAligned<T>(N);
    auto in_lanes_b = AllocateAligned<T>(N);
    auto sum_lanes = AllocateAligned<TW>(N / 8);
    HWY_ASSERT(in_lanes_a && in_lanes_b && sum_lanes);

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        uint64_t rand64_val = Random64(&rng);
        in_lanes_a[i] = ConvertScalarTo<T>(rand64_val & 0xFF);
        in_lanes_b[i] = ConvertScalarTo<T>((rand64_val >> 8) & 0xFF);
      }

      for (size_t idx_sum = 0; idx_sum < N / 8; ++idx_sum) {
        uint64_t sum = 0;
        for (size_t i = 0; i < 8; ++i) {
          const auto lane_diff =
              static_cast<int16_t>(in_lanes_a[idx_sum * 8 + i]) -
              static_cast<int16_t>(in_lanes_b[idx_sum * 8 + i]);
          sum +=
              static_cast<uint64_t>((lane_diff >= 0) ? lane_diff : -lane_diff);
        }
        sum_lanes[idx_sum] = static_cast<TW>(sum);
      }

      const Vec<D> a = Load(d, in_lanes_a.get());
      const Vec<D> b = Load(d, in_lanes_b.get());
      HWY_ASSERT_VEC_EQ(d64, sum_lanes.get(), SumsOf8AbsDiff(a, b));
    }
  }
};

HWY_NOINLINE void TestAllSumsOf8AbsDiff() {
  ForGEVectors<64, TestSumsOf8AbsDiff>()(int8_t());
  ForGEVectors<64, TestSumsOf8AbsDiff>()(uint8_t());
}

struct TestSumsOfAdjQuadAbsDiff {
#if HWY_TARGET != HWY_SCALAR
  template <size_t kAOffset, size_t kBOffset, class D,
            HWY_IF_LANES_LE_D(D, kAOffset * 4 + 3)>
  static HWY_INLINE void DoTestSumsOfAdjQuadAbsDiff(D /*d*/,
                                                    RandomState& /*rng*/) {}

  template <size_t kAOffset, size_t kBOffset, class D,
            HWY_IF_LANES_GT_D(D, kAOffset * 4 + 3),
            HWY_IF_LANES_LE_D(D, kBOffset * 4 + 3)>
  static HWY_INLINE void DoTestSumsOfAdjQuadAbsDiff(D /*d*/,
                                                    RandomState& /*rng*/) {}

  template <size_t kAOffset, size_t kBOffset, class D,
            HWY_IF_LANES_GT_D(D, kAOffset * 4 + 3),
            HWY_IF_LANES_GT_D(D, kBOffset * 4 + 3)>
  static HWY_NOINLINE void DoTestSumsOfAdjQuadAbsDiff(D d, RandomState& rng) {
    static_assert(kAOffset <= 1, "kAOffset <= 1 must be true");
    static_assert(kBOffset <= 3, "kBOffset <= 3 must be true");

    using T = TFromD<D>;
    using TW = MakeWide<T>;
    using TW_I = MakeSigned<TW>;
    static_assert(sizeof(T) == 1, "sizeof(T) == 1 must be true");

    const RepartitionToWide<decltype(d)> dw;

    const size_t N = Lanes(d);
    if (N <= (kAOffset * 4 + 3) || N <= (kBOffset * 4 + 3)) {
      return;
    }

    const size_t num_valid_sum_lanes =
        (N < (kAOffset * 4 + 3 + (N / 2))) ? 1 : (N / 2);

    auto in_lanes_a = AllocateAligned<T>(N);
    auto in_lanes_b = AllocateAligned<T>(N);
    auto sum_lanes = AllocateAligned<TW>(N / 2);
    HWY_ASSERT(in_lanes_a && in_lanes_b && sum_lanes);

    ZeroBytes(sum_lanes.get(), (N / 2) * sizeof(TW));

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        uint64_t rand64_val = Random64(&rng);
        in_lanes_a[i] = ConvertScalarTo<T>(rand64_val & 0xFF);
        in_lanes_b[i] = ConvertScalarTo<T>((rand64_val >> 8) & 0xFF);
      }

      for (size_t i = 0; i < num_valid_sum_lanes; ++i) {
        size_t blk_idx = i / 8;
        size_t idx_in_blk = i & 7;

        const TW_I a0 = static_cast<TW_I>(
            in_lanes_a[blk_idx * 16 + kAOffset * 4 + idx_in_blk]);
        const TW_I a1 = static_cast<TW_I>(
            in_lanes_a[blk_idx * 16 + kAOffset * 4 + idx_in_blk + 1]);
        const TW_I a2 = static_cast<TW_I>(
            in_lanes_a[blk_idx * 16 + kAOffset * 4 + idx_in_blk + 2]);
        const TW_I a3 = static_cast<TW_I>(
            in_lanes_a[blk_idx * 16 + kAOffset * 4 + idx_in_blk + 3]);

        const TW_I b0 =
            static_cast<TW_I>(in_lanes_b[blk_idx * 16 + kBOffset * 4]);
        const TW_I b1 =
            static_cast<TW_I>(in_lanes_b[blk_idx * 16 + kBOffset * 4 + 1]);
        const TW_I b2 =
            static_cast<TW_I>(in_lanes_b[blk_idx * 16 + kBOffset * 4 + 2]);
        const TW_I b3 =
            static_cast<TW_I>(in_lanes_b[blk_idx * 16 + kBOffset * 4 + 3]);

        const TW_I diff0 = static_cast<TW_I>(ScalarAbs(a0 - b0));
        const TW_I diff1 = static_cast<TW_I>(ScalarAbs(a1 - b1));
        const TW_I diff2 = static_cast<TW_I>(ScalarAbs(a2 - b2));
        const TW_I diff3 = static_cast<TW_I>(ScalarAbs(a3 - b3));
        sum_lanes[i] = static_cast<TW>(diff0 + diff1 + diff2 + diff3);
      }

      const Vec<decltype(dw)> actual = IfThenElseZero(
          FirstN(dw, num_valid_sum_lanes),
          SumsOfAdjQuadAbsDiff<kAOffset, kBOffset>(Load(d, in_lanes_a.get()),
                                                   Load(d, in_lanes_b.get())));
      HWY_ASSERT_VEC_EQ(dw, sum_lanes.get(), actual);
    }
  }

  template <class D, class D2 = DFromV<Vec<D>>,
            HWY_IF_LANES_LE_D(D, HWY_MAX_LANES_D(D2) - 1)>
  static HWY_INLINE void FullOrFixedVecQuadSumTests(D /*d*/,
                                                    RandomState& /*rng*/) {}

  template <class D, class D2 = DFromV<Vec<D>>,
            HWY_IF_LANES_GT_D(D, HWY_MAX_LANES_D(D2) - 1)>
  static HWY_INLINE void FullOrFixedVecQuadSumTests(D d, RandomState& rng) {
    DoTestSumsOfAdjQuadAbsDiff<0, 1>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<0, 2>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<0, 3>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<1, 0>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<1, 1>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<1, 2>(d, rng);
    DoTestSumsOfAdjQuadAbsDiff<1, 3>(d, rng);
  }
#endif  // HWY_TARGET != HWY_SCALAR

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    RandomState rng;
    DoTestSumsOfAdjQuadAbsDiff<0, 0>(d, rng);
    FullOrFixedVecQuadSumTests(d, rng);
#else
    (void)d;
#endif  // HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllSumsOfAdjQuadAbsDiff() {
  ForGEVectors<32, TestSumsOfAdjQuadAbsDiff>()(int8_t());
  ForGEVectors<32, TestSumsOfAdjQuadAbsDiff>()(uint8_t());
}

struct TestSumsOfShuffledQuadAbsDiff {
#if HWY_TARGET != HWY_SCALAR
  template <size_t kIdx3, size_t kIdx2, size_t kIdx1, size_t kIdx0, class D>
  static HWY_NOINLINE void DoTestSumsOfShuffledQuadAbsDiff(D d,
                                                           RandomState& rng) {
    static_assert(kIdx0 <= 3, "kIdx0 <= 3 must be true");
    static_assert(kIdx1 <= 3, "kIdx1 <= 3 must be true");
    static_assert(kIdx2 <= 3, "kIdx2 <= 3 must be true");
    static_assert(kIdx3 <= 3, "kIdx3 <= 3 must be true");

    using T = TFromD<D>;
    using TW = MakeWide<T>;
    using TW_I = MakeSigned<TW>;
    static_assert(sizeof(T) == 1, "sizeof(T) == 1 must be true");

    const RepartitionToWide<decltype(d)> dw;
    const RepartitionToWide<decltype(dw)> dw2;

    const size_t N = Lanes(d);
    const size_t num_valid_sum_lanes = (N < 8) ? 1 : (N / 2);

    const size_t in_lanes_a_alloc_len = HWY_MAX(N, 16);
    auto in_lanes_a = AllocateAligned<T>(in_lanes_a_alloc_len);
    auto in_lanes_b = AllocateAligned<T>(N);
    auto a_shuf_lanes = AllocateAligned<T>(in_lanes_a_alloc_len);
    auto sum_lanes = AllocateAligned<TW>(N / 2);
    HWY_ASSERT(in_lanes_a && in_lanes_b && a_shuf_lanes && sum_lanes);

    ZeroBytes(in_lanes_a.get(), sizeof(T) * in_lanes_a_alloc_len);
    ZeroBytes(a_shuf_lanes.get(), sizeof(T) * in_lanes_a_alloc_len);
    ZeroBytes(sum_lanes.get(), (N / 2) * sizeof(TW));

    for (size_t rep = 0; rep < 100; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        uint64_t rand64_val = Random64(&rng);
        in_lanes_a[i] = ConvertScalarTo<T>(rand64_val & 0xFF);
        in_lanes_b[i] = ConvertScalarTo<T>((rand64_val >> 8) & 0xFF);
      }

      const auto a = Load(d, in_lanes_a.get());
      const auto a_shuf = BitCast(
          d, Per4LaneBlockShuffle<kIdx3, kIdx2, kIdx1, kIdx0>(BitCast(dw2, a)));
      Store(a_shuf, d, a_shuf_lanes.get());

      for (size_t i = 0; i < num_valid_sum_lanes; ++i) {
        size_t blk_idx = i / 8;
        size_t idx_in_blk = i & 7;

        const auto a0 =
            static_cast<TW_I>(a_shuf_lanes[blk_idx * 16 + (idx_in_blk / 4) * 8 +
                                           (idx_in_blk & 3)]);
        const auto a1 =
            static_cast<TW_I>(a_shuf_lanes[blk_idx * 16 + (idx_in_blk / 4) * 8 +
                                           (idx_in_blk & 3) + 1]);
        const auto a2 =
            static_cast<TW_I>(a_shuf_lanes[blk_idx * 16 + (idx_in_blk / 4) * 8 +
                                           (idx_in_blk & 3) + 2]);
        const auto a3 =
            static_cast<TW_I>(a_shuf_lanes[blk_idx * 16 + (idx_in_blk / 4) * 8 +
                                           (idx_in_blk & 3) + 3]);

        const auto b0 = static_cast<TW_I>(in_lanes_b[(i / 2) * 4]);
        const auto b1 = static_cast<TW_I>(in_lanes_b[(i / 2) * 4 + 1]);
        const auto b2 = static_cast<TW_I>(in_lanes_b[(i / 2) * 4 + 2]);
        const auto b3 = static_cast<TW_I>(in_lanes_b[(i / 2) * 4 + 3]);

        const auto diff0 = a0 - b0;
        const auto diff1 = a1 - b1;
        const auto diff2 = a2 - b2;
        const auto diff3 = a3 - b3;

        sum_lanes[i] = static_cast<TW>(((diff0 < 0) ? (-diff0) : diff0) +
                                       ((diff1 < 0) ? (-diff1) : diff1) +
                                       ((diff2 < 0) ? (-diff2) : diff2) +
                                       ((diff3 < 0) ? (-diff3) : diff3));
      }

      const auto actual =
          IfThenElseZero(FirstN(dw, num_valid_sum_lanes),
                         SumsOfShuffledQuadAbsDiff<kIdx3, kIdx2, kIdx1, kIdx0>(
                             a, Load(d, in_lanes_b.get())));
      HWY_ASSERT_VEC_EQ(dw, sum_lanes.get(), actual);
    }
  }

  template <class D, HWY_IF_LANES_LE_D(D, 4)>
  static HWY_INLINE void AtLeast8LanesShufQuadSumTests(D /*d*/,
                                                       RandomState& /*rng*/) {}

  template <class D, HWY_IF_LANES_GT_D(D, 4)>
  static HWY_INLINE void AtLeast8LanesShufQuadSumTests(D d, RandomState& rng) {
    if (Lanes(d) >= 8) {
      DoTestSumsOfShuffledQuadAbsDiff<0, 0, 0, 1>(d, rng);
    }
  }

  template <class D, HWY_IF_LANES_LE_D(D, 8)>
  static HWY_INLINE void AtLeast16LanesShufQuadSumTests(D /*d*/,
                                                        RandomState& /*rng*/) {}

  template <class D, HWY_IF_LANES_GT_D(D, 8)>
  static HWY_INLINE void AtLeast16LanesShufQuadSumTests(D d, RandomState& rng) {
    if (Lanes(d) >= 16) {
      DoTestSumsOfShuffledQuadAbsDiff<3, 2, 1, 0>(d, rng);
      DoTestSumsOfShuffledQuadAbsDiff<0, 3, 1, 2>(d, rng);
      DoTestSumsOfShuffledQuadAbsDiff<2, 3, 0, 1>(d, rng);
    }
  }

  template <class D, class D2 = DFromV<Vec<D>>,
            HWY_IF_LANES_LE_D(D, HWY_MAX_LANES_D(D2) - 1)>
  static HWY_INLINE void FullOrFixedVecShufQuadSumTests(D /*d*/,
                                                        RandomState& /*rng*/) {}

  template <class D, class D2 = DFromV<Vec<D>>,
            HWY_IF_LANES_GT_D(D, HWY_MAX_LANES_D(D2) - 1)>
  static HWY_INLINE void FullOrFixedVecShufQuadSumTests(D d, RandomState& rng) {
    AtLeast8LanesShufQuadSumTests(d, rng);
    AtLeast16LanesShufQuadSumTests(d, rng);
  }
#endif  // HWY_TARGET != HWY_SCALAR

  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    RandomState rng;
    DoTestSumsOfShuffledQuadAbsDiff<0, 0, 0, 0>(d, rng);
    FullOrFixedVecShufQuadSumTests(d, rng);
#else
    (void)d;
#endif
  }
};

HWY_NOINLINE void TestAllSumsOfShuffledQuadAbsDiff() {
  ForGEVectors<32, TestSumsOfShuffledQuadAbsDiff>()(int8_t());
  ForGEVectors<32, TestSumsOfShuffledQuadAbsDiff>()(uint8_t());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwySumsAbsDiffTest);
HWY_EXPORT_AND_TEST_P(HwySumsAbsDiffTest, TestAllSumsOf8AbsDiff);
HWY_EXPORT_AND_TEST_P(HwySumsAbsDiffTest, TestAllSumsOfAdjQuadAbsDiff);
HWY_EXPORT_AND_TEST_P(HwySumsAbsDiffTest, TestAllSumsOfShuffledQuadAbsDiff);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
