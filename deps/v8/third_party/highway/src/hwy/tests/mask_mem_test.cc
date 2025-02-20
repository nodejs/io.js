// Copyright 2019 Google LLC
// Copyright 2023 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause
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
#include <stdio.h>
#include <string.h>  // memcmp

#include "hwy/base.h"
#include "hwy/nanobenchmark.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/mask_mem_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestMaskedLoad {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && lanes);

    const Vec<D> v = IotaForSpecial(d, 1);
    const Vec<D> v2 = IotaForSpecial(d, 2);
    Store(v, d, lanes.get());

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
      }

      const auto mask_i = Load(di, bool_lanes.get());
      const auto mask = RebindMask(d, Gt(mask_i, Zero(di)));
      const auto expected = IfThenElseZero(mask, Load(d, lanes.get()));
      const auto expected2 = IfThenElse(mask, Load(d, lanes.get()), v2);
      const auto actual = MaskedLoad(mask, d, lanes.get());
      const auto actual2 = MaskedLoadOr(v2, mask, d, lanes.get());
      HWY_ASSERT_VEC_EQ(d, expected, actual);
      HWY_ASSERT_VEC_EQ(d, expected2, actual2);
    }
  }
};

HWY_NOINLINE void TestAllMaskedLoad() {
  ForAllTypesAndSpecial(ForPartialVectors<TestMaskedLoad>());
}

struct TestMaskedScatter {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto lanes = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && lanes && expected);

    const Vec<D> v = Iota(d, hwy::Unpredictable1() - 1);
    Store(v, d, lanes.get());

    const VI indices = Reverse(di, Iota(di, hwy::Unpredictable1() - 1));

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      ZeroBytes(expected.get(), N * sizeof(T));
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        if (bool_lanes[i]) {
          expected[N - 1 - i] = ConvertScalarTo<T>(i);
        }
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const auto mask = RebindMask(d, Gt(mask_i, Zero(di)));
      ZeroBytes(lanes.get(), N * sizeof(T));
      MaskedScatterIndex(v, mask, d, lanes.get(), indices);
      HWY_ASSERT_VEC_EQ(d, expected.get(), Load(d, lanes.get()));
    }
  }
};

HWY_NOINLINE void TestAllMaskedScatter() {
  ForUIF3264(ForPartialVectors<TestMaskedScatter>());
}

struct TestScatterIndexN {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto lanes = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(lanes && expected);

    const Vec<D> v = Iota(d, hwy::Unpredictable1() - 1);
    Store(v, d, lanes.get());

    const VI indices = Reverse(di, Iota(di, hwy::Unpredictable1() - 1));

    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      // Choose 1 to N lanes to store
      const size_t max_lanes_to_store = (Random32(&rng) % N) + 1;

      ZeroBytes(expected.get(), N * sizeof(T));
      for (size_t i = 0; i < max_lanes_to_store; ++i) {
        expected[N - 1 - i] = ConvertScalarTo<T>(i);
      }

      ZeroBytes(lanes.get(), N * sizeof(T));
      ScatterIndexN(v, d, lanes.get(), indices, max_lanes_to_store);
      HWY_ASSERT_VEC_EQ(d, expected.get(), Load(d, lanes.get()));
    }

    // Zero store is just zeroes
    ZeroBytes(expected.get(), N * sizeof(T));
    ZeroBytes(lanes.get(), N * sizeof(T));
    ScatterIndexN(v, d, lanes.get(), indices, 0);
    HWY_ASSERT_VEC_EQ(d, expected.get(), Load(d, lanes.get()));

    // Load is clamped at min(N, max_lanes_to_load)
    auto larger_memory = AllocateAligned<T>(N * 2);
    auto larger_expected = AllocateAligned<T>(N * 2);
    HWY_ASSERT(larger_memory && larger_expected);
    ZeroBytes(larger_expected.get(), N * sizeof(T) * 2);
    for (size_t i = 0; i < N; ++i) {
      larger_expected[N - 1 - i] = ConvertScalarTo<T>(i);
    }
    ZeroBytes(larger_memory.get(), N * sizeof(T));
    ScatterIndexN(v, d, larger_memory.get(), indices, N + 1);
    HWY_ASSERT_VEC_EQ(d, larger_expected.get(), Load(d, larger_memory.get()));
  }
};

HWY_NOINLINE void TestAllScatterIndexN() {
  ForUIF3264(ForPartialVectors<TestScatterIndexN>());
}

struct TestMaskedGather {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && lanes);

    const Vec<D> v = Iota(d, hwy::Unpredictable1() - 1);
    Store(v, d, lanes.get());

    const Vec<D> no = Set(d, ConvertScalarTo<T>(2));

    const VI indices = Reverse(di, Iota(di, hwy::Unpredictable1() - 1));

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = static_cast<TI>((Random32(&rng) & 1024) ? 1 : 0);
      }

      const VI mask_i = Load(di, bool_lanes.get());
      const auto mask = RebindMask(d, Gt(mask_i, Zero(di)));
      const Vec<D> expected_z = IfThenElseZero(mask, Reverse(d, v));
      const Vec<D> expected_or = IfThenElse(mask, Reverse(d, v), no);
      const Vec<D> actual_z = MaskedGatherIndex(mask, d, lanes.get(), indices);
      const Vec<D> actual_or =
          MaskedGatherIndexOr(no, mask, d, lanes.get(), indices);
      HWY_ASSERT_VEC_EQ(d, expected_z, actual_z);
      HWY_ASSERT_VEC_EQ(d, expected_or, actual_or);
    }
  }
};

HWY_NOINLINE void TestAllMaskedGather() {
  ForUIF3264(ForPartialVectors<TestMaskedGather>());
}

struct TestGatherIndexN {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    using VI = Vec<decltype(di)>;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto lanes = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && lanes);

    const Vec<D> v = Iota(d, hwy::Unpredictable1() - 1);
    Store(v, d, lanes.get());

    const VI indices = Reverse(di, Iota(di, hwy::Unpredictable1() - 1));

    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      // Choose 1 to N lanes to load
      const size_t max_lanes_to_load = (Random32(&rng) % N) + 1;

      // Convert lane count to mask to compare results
      const auto mask = FirstN(d, max_lanes_to_load);

      const Vec<D> expected = IfThenElseZero(mask, Reverse(d, v));
      const Vec<D> actual =
          GatherIndexN(d, lanes.get(), indices, max_lanes_to_load);
      HWY_ASSERT_VEC_EQ(d, expected, actual);
    }

    // Zero load is just zeroes
    const Vec<D> zeroes = Zero(d);
    const Vec<D> actual_zero = GatherIndexN(d, lanes.get(), indices, 0);
    HWY_ASSERT_VEC_EQ(d, zeroes, actual_zero);

    // Load is clamped at min(N, max_lanes_to_load)
    const auto clamped_mask = FirstN(d, N);
    const Vec<D> expected_clamped = IfThenElseZero(clamped_mask, Reverse(d, v));
    const Vec<D> actual_clamped = GatherIndexN(d, lanes.get(), indices, N + 1);
    HWY_ASSERT_VEC_EQ(d, expected_clamped, actual_clamped);
  }
};

HWY_NOINLINE void TestAllGatherIndexN() {
  ForUIF3264(ForPartialVectors<TestGatherIndexN>());
}

struct TestBlendedStore {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;

    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(d);
    auto bool_lanes = AllocateAligned<TI>(N);
    auto actual = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(bool_lanes && actual && expected);

    const Vec<D> v = IotaForSpecial(d, 1);

    // Each lane should have a chance of having mask=true.
    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = (Random32(&rng) & 1024) ? TI(1) : TI(0);
        // Re-initialize to something distinct from v[i].
        actual[i] = ConvertScalarTo<T>(127 - (i & 127));
        expected[i] = bool_lanes[i] ? ConvertScalarTo<T>(i + 1) : actual[i];
      }

      const auto mask = RebindMask(d, Gt(Load(di, bool_lanes.get()), Zero(di)));
      BlendedStore(v, mask, d, actual.get());
      HWY_ASSERT_VEC_EQ(d, expected.get(), Load(d, actual.get()));
    }
  }
};

HWY_NOINLINE void TestAllBlendedStore() {
  ForAllTypesAndSpecial(ForPartialVectors<TestBlendedStore>());
}

class TestStoreMaskBits {
 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*t*/, D /*d*/) {
    RandomState rng;
    using TI = MakeSigned<T>;  // For mask > 0 comparison
    const Rebind<TI, D> di;
    const size_t N = Lanes(di);
    const size_t expected_num_bytes = (N + 7) / 8;
    auto bool_lanes = AllocateAligned<TI>(N);
    auto expected = AllocateAligned<uint8_t>(expected_num_bytes);
    auto actual = AllocateAligned<uint8_t>(HWY_MAX(8, expected_num_bytes));
    HWY_ASSERT(bool_lanes && actual && expected);

    const ScalableTag<uint8_t, -3> d_bits;

    for (size_t rep = 0; rep < AdjustedReps(200); ++rep) {
      // Generate random mask pattern.
      for (size_t i = 0; i < N; ++i) {
        bool_lanes[i] = static_cast<TI>((rng() & 1024) ? 1 : 0);
      }
      const auto bools = Load(di, bool_lanes.get());
      const auto mask = Gt(bools, Zero(di));

      // Requires at least 8 bytes, ensured above.
      const size_t bytes_written = StoreMaskBits(di, mask, actual.get());
      if (bytes_written != expected_num_bytes) {
        fprintf(stderr, "%s expected %d bytes, actual %d\n",
                TypeName(T(), N).c_str(), static_cast<int>(expected_num_bytes),
                static_cast<int>(bytes_written));

        HWY_ASSERT(false);
      }

      // Requires at least 8 bytes, ensured above.
      const auto mask2 = LoadMaskBits(di, actual.get());
      HWY_ASSERT_MASK_EQ(di, mask, mask2);

      memset(expected.get(), 0, expected_num_bytes);
      for (size_t i = 0; i < N; ++i) {
        expected[i / 8] =
            static_cast<uint8_t>(expected[i / 8] | (bool_lanes[i] << (i % 8)));
      }

      size_t i = 0;
      // Stored bits must match original mask
      for (; i < N; ++i) {
        const TI is_set = (actual[i / 8] & (1 << (i % 8))) ? 1 : 0;
        if (is_set != bool_lanes[i]) {
          fprintf(stderr, "%s lane %d: expected %d, actual %d\n",
                  TypeName(T(), N).c_str(), static_cast<int>(i),
                  static_cast<int>(bool_lanes[i]), static_cast<int>(is_set));
          Print(di, "bools", bools, 0, N);
          Print(d_bits, "expected bytes", Load(d_bits, expected.get()), 0,
                expected_num_bytes);
          Print(d_bits, "actual bytes", Load(d_bits, actual.get()), 0,
                expected_num_bytes);

          HWY_ASSERT(false);
        }
      }
      // Any partial bits in the last byte must be zero
      for (; i < 8 * bytes_written; ++i) {
        const int bit = (actual[i / 8] & (1 << (i % 8)));
        if (bit != 0) {
          fprintf(stderr, "%s: bit #%d should be zero\n",
                  TypeName(T(), N).c_str(), static_cast<int>(i));
          Print(di, "bools", bools, 0, N);
          Print(d_bits, "expected bytes", Load(d_bits, expected.get()), 0,
                expected_num_bytes);
          Print(d_bits, "actual bytes", Load(d_bits, actual.get()), 0,
                expected_num_bytes);

          HWY_ASSERT(false);
        }
      }
    }
  }
};

HWY_NOINLINE void TestAllStoreMaskBits() {
  ForAllTypes(ForPartialVectors<TestStoreMaskBits>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyMaskMemTest);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllMaskedLoad);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllMaskedScatter);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllScatterIndexN);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllMaskedGather);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllGatherIndexN);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllBlendedStore);
HWY_EXPORT_AND_TEST_P(HwyMaskMemTest, TestAllStoreMaskBits);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
