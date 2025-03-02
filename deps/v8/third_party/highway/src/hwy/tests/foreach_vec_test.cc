// Copyright 2023 Google LLC
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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/foreach_vec_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct ForeachVectorTestPerLaneSizeState {
  size_t num_of_lanes_mask;
#if HWY_HAVE_SCALABLE
  int pow2_mask;
#endif
};

struct ForeachVectorTestState {
  ForeachVectorTestPerLaneSizeState per_lane_size_states[16];
  int lane_sizes_mask;
};

template <class D>
static HWY_INLINE void UpdateForeachVectorTestState(
    ForeachVectorTestState &state, D d) {
  using T = TFromD<D>;
  static_assert(sizeof(T) >= 1 && sizeof(T) <= 8,
                "sizeof(T) must be between 1 and 8");

  state.lane_sizes_mask |= (1 << sizeof(T));

  ForeachVectorTestPerLaneSizeState *per_lane_size_state =
      &state.per_lane_size_states[sizeof(T)];

  const size_t lanes = Lanes(d);
  HWY_ASSERT(lanes > 0 && (lanes & (lanes - 1)) == 0);

  per_lane_size_state->num_of_lanes_mask |= lanes;

#if HWY_HAVE_SCALABLE
  constexpr int kPow2 = D().Pow2();
#if HWY_TARGET == HWY_RVV
  static_assert(kPow2 >= detail::MinPow2<T>(),
                "kPow2 >= detail::MinPow2<T>() must be true");
#endif
  static_assert(kPow2 <= detail::MaxPow2(),
                "kPow2 <= detail::MaxPow2() must be true");

  if (HWY_TARGET == HWY_RVV || kPow2 >= -3) {
    per_lane_size_state->pow2_mask |= (1 << (kPow2 + 3));
  }
#endif
}

static constexpr int kMaxSupportedLaneSize = HWY_HAVE_INTEGER64 ? 8 : 4;
static constexpr int kSupportedLaneSizesMask =
    (1 << 1) | (1 << 2) | (1 << 4) | (HWY_HAVE_INTEGER64 ? (1 << 8) : 0);

#if HWY_HAVE_SCALABLE
static constexpr int kSupportedU8Pow2Mask =
    (HWY_TARGET == HWY_RVV) ? 0x7F : 0x0F;
#endif

static HWY_INLINE size_t LanesPerVectWithLaneSize(size_t lanes_per_u8_vect,
                                                  int lane_size) {
#if HWY_TARGET == HWY_SCALAR
  (void)lanes_per_u8_vect;
  (void)lane_size;
  return 1;
#else
  return lanes_per_u8_vect / static_cast<size_t>(lane_size);
#endif
}

#define HWY_DECLARE_FOREACH_VECTOR_TEST(TestClass)       \
  static ForeachVectorTestState TestClass##State;        \
                                                         \
  struct TestClass {                                     \
    template <class T, class D>                          \
    HWY_INLINE void operator()(T, D d) {                 \
      UpdateForeachVectorTestState(TestClass##State, d); \
    }                                                    \
  };

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForMaxPow2)

HWY_NOINLINE void TestAllForMaxPow2() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForMaxPow2State);
  ForUnsignedTypes(ForMaxPow2<TestForMaxPow2>());

  HWY_ASSERT(TestForMaxPow2State.lane_sizes_mask == kSupportedLaneSizesMask);

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForMaxPow2State.per_lane_size_states[lane_size];

    const size_t lanes = LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
               ((lanes << (HWY_TARGET == HWY_RVV ? 2 : 1)) - 1));

#if HWY_HAVE_SCALABLE
    const int expected_pow2_mask =
        (kSupportedU8Pow2Mask & 0x1F) & (-((lane_size + 1) / 2));
    HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
  }
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForExtendableVectors)

#if HWY_TARGET == HWY_RVV
template <int kPow2, class Test, class T,
          hwy::EnableIf<(-kPow2 < detail::MinPow2<T>())> * = nullptr>
static HWY_INLINE void ExecuteTestForExtendableVectors(const Test & /*test*/,
                                                       T /*unused*/) {}
template <int kPow2, class Test, class T,
          hwy::EnableIf<(-kPow2 >= detail::MinPow2<T>())> * = nullptr>
static HWY_INLINE void ExecuteTestForExtendableVectors(const Test &test,
                                                       T /*unused*/) {
  test(T());
}
#endif

template <int kPow2>
static HWY_NOINLINE void DoTestAllForExtendableVectors() {
  static_assert(kPow2 >= 0 && kPow2 <= 3, "kPow2 must be between 0 and 3");

  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForExtendableVectorsState);

  const ForExtendableVectors<TestForExtendableVectors, kPow2> test;
#if HWY_TARGET == HWY_RVV
  test(uint8_t());
  ExecuteTestForExtendableVectors<kPow2>(test, uint16_t());
  ExecuteTestForExtendableVectors<kPow2>(test, uint32_t());
  ExecuteTestForExtendableVectors<kPow2>(test, uint64_t());
#else
  ForUnsignedTypes(test);
#endif

#if HWY_TARGET == HWY_SCALAR
  HWY_ASSERT(TestForExtendableVectorsState.lane_sizes_mask == 0);
#else  // HWY_TARGET != HWY_SCALAR
  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t, -kPow2>());
#if HWY_TARGET == HWY_RVV
  const int expected_lane_sizes_mask =
      kSupportedLaneSizesMask &
      ((1 << 1) | (1 << 2) | ((kPow2 <= 2) ? (1 << 4) : 0) |
       ((kPow2 <= 1) ? (1 << 8) : 0));
#else
  const int expected_lane_sizes_mask =
      kSupportedLaneSizesMask & (((lanes_per_u8_vect >= 1) ? (1 << 1) : 0) |
                                 ((lanes_per_u8_vect >= 2) ? (1 << 2) : 0) |
                                 ((lanes_per_u8_vect >= 4) ? (1 << 4) : 0) |
                                 ((lanes_per_u8_vect >= 8) ? (1 << 8) : 0));
#endif
  HWY_ASSERT(TestForExtendableVectorsState.lane_sizes_mask ==
             expected_lane_sizes_mask);
#endif  // HWY_TARGET == HWY_SCALAR

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForExtendableVectorsState.per_lane_size_states[lane_size];

#if HWY_TARGET == HWY_SCALAR
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#else
    if ((expected_lane_sizes_mask & (1 << lane_size)) != 0) {
      const size_t lanes =
          LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
                 ((lanes << (HWY_TARGET == HWY_RVV ? 4 : 1)) - 1));

#if HWY_HAVE_SCALABLE
      const int expected_pow2_mask =
          ((kSupportedU8Pow2Mask >> kPow2) & (-((lane_size + 1) / 2))) |
          ((HWY_TARGET == HWY_RVV) ? 0 : (1 << (3 - kPow2)));
      HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
    } else {
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#if HWY_HAVE_SCALABLE
      HWY_ASSERT(per_lane_size_state->pow2_mask == 0);
#endif
    }
#endif  // HWY_TARGET == HWY_SCALAR
  }
}

HWY_NOINLINE void TestAllForExtendableVectors() {
  DoTestAllForExtendableVectors<1>();
  DoTestAllForExtendableVectors<2>();
  DoTestAllForExtendableVectors<3>();
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForShrinkableVectors)

HWY_NOINLINE void TestAllForShrinkableVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForShrinkableVectorsState);
  ForUnsignedTypes(ForShrinkableVectors<TestForShrinkableVectors>());

#if HWY_TARGET == HWY_SCALAR
  HWY_ASSERT(TestForShrinkableVectorsState.lane_sizes_mask == 0);
#else   // HWY_TARGET != HWY_SCALAR
  HWY_ASSERT(TestForShrinkableVectorsState.lane_sizes_mask ==
             kSupportedLaneSizesMask);
  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());
#endif  // HWY_TARGET == HWY_SCALAR

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForShrinkableVectorsState.per_lane_size_states[lane_size];

#if HWY_TARGET == HWY_SCALAR
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#else  // HWY_TARGET != HWY_SCALAR
    const size_t lanes = LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);

#if HWY_HAVE_SCALABLE
    const int expected_pow2_mask =
        kSupportedU8Pow2Mask & (-2 * ((lane_size + 1) / 2));
    const size_t expected_lanes_mask =
        (lanes * static_cast<size_t>(expected_pow2_mask)) >> 3;

    HWY_ASSERT((per_lane_size_state->num_of_lanes_mask & expected_lanes_mask) ==
               expected_lanes_mask);
    HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#else   // !HWY_HAVE_SCALABLE
    const size_t expected_lanes_mask =
        static_cast<size_t>(((lanes << 1) - 1) & (~size_t{1}));
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == expected_lanes_mask);
#endif  // HWY_HAVE_SCALABLE
#endif  // HWY_TARGET == HWY_SCALAR
  }
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForGEVectors)

template <size_t kMinBits, class Test, class T,
          HWY_IF_LANES_LE(kMinBits, sizeof(T) * 8 - 1)>
static HWY_INLINE void ExecuteTestForGEVectors(const Test & /*test*/,
                                               T /*unused*/) {}

template <size_t kMinBits, class Test, class T,
          HWY_IF_LANES_GT(kMinBits, sizeof(T) * 8 - 1)>
static HWY_INLINE void ExecuteTestForGEVectors(const Test &test, T /*unused*/) {
  test(T());
}

template <size_t kMinBits, class Test>
static HWY_NOINLINE void DoTestAllForGEVectors(const Test &test) {
  static_assert(kMinBits >= 16, "kMinBits >= 16 must be true");

  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForGEVectorsState);

  test(uint8_t());
  test(uint16_t());
  ExecuteTestForGEVectors<kMinBits>(test, uint32_t());
#if HWY_HAVE_INTEGER64
  ExecuteTestForGEVectors<kMinBits>(test, uint64_t());
#endif

#if HWY_TARGET == HWY_SCALAR
  HWY_ASSERT(TestForGEVectorsState.lane_sizes_mask == 0);
#else  // HWY_TARGET != HWY_SCALAR
  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

#if HWY_TARGET == HWY_RVV
  const size_t lanes_per_largest_u8_vect = lanes_per_u8_vect * 8;
#else
  const size_t lanes_per_largest_u8_vect = lanes_per_u8_vect;
#endif  // HWY_TARGET == HWY_RVV

  constexpr int kGEVectSupportedLaneSizesMask =
      kSupportedLaneSizesMask &
      ((1 << 1) | (1 << 2) | ((kMinBits >= 32) ? (1 << 4) : 0) |
       ((kMinBits >= 64) ? (1 << 8) : 0));

  const int expected_lane_sizes_mask =
      (lanes_per_largest_u8_vect >= (kMinBits / 8))
          ? kGEVectSupportedLaneSizesMask
          : 0;

  constexpr size_t kSupportedU8VecSizesMask =
      static_cast<size_t>(((static_cast<size_t>(HWY_MAX_BYTES) << 1) - 1) &
                          (~((kMinBits / 8) - 1)));

  HWY_ASSERT(TestForGEVectorsState.lane_sizes_mask == expected_lane_sizes_mask);
#endif  // HWY_TARGET == HWY_SCALAR

#if HWY_HAVE_SCALABLE
  constexpr int kMinVecPow2 =
      static_cast<int>(CeilLog2(HWY_MIN(kMinBits / 16, 8))) - 3;
  static_assert(kMinVecPow2 >= -3 && kMinVecPow2 <= 0,
                "kMinVecPow2 must be between -3 and 0");

  constexpr int kGEVectSupportedU8Pow2Mask =
      kSupportedU8Pow2Mask & (-(1 << (kMinVecPow2 + 3)));

#if HWY_TARGET == HWY_RVV
  const int ge_vect_supported_u8_pow2_mask =
      kGEVectSupportedU8Pow2Mask &
      ((kMinBits <= 128)
           ? -1
           : ((lanes_per_u8_vect < (kMinBits / 64))
                  ? 0
                  : (0x40 |
                     ((lanes_per_u8_vect >= (kMinBits / 32)) ? 0x20 : 0) |
                     ((lanes_per_u8_vect >= (kMinBits / 16)) ? 0x10 : 0) |
                     ((lanes_per_u8_vect >= (kMinBits / 8)) ? 0x08 : 0))));
#else
  const int ge_vect_supported_u8_pow2_mask =
      (lanes_per_u8_vect >= (kMinBits / 8)) ? kGEVectSupportedU8Pow2Mask : 0;
#endif

#endif

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForGEVectorsState.per_lane_size_states[lane_size];

#if HWY_TARGET == HWY_SCALAR
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#else  // HWY_TARGET != HWY_SCALAR
    if (kMinBits >= static_cast<size_t>(lane_size * 8)) {
      const size_t expected_lanes_mask =
          (((lanes_per_largest_u8_vect << 1) - 1) & kSupportedU8VecSizesMask) /
          static_cast<size_t>(lane_size);
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == expected_lanes_mask);

#if HWY_HAVE_SCALABLE
      const int expected_pow2_mask =
          ge_vect_supported_u8_pow2_mask & (-((lane_size + 1) / 2));
      HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
    } else {
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#if HWY_HAVE_SCALABLE
      HWY_ASSERT(per_lane_size_state->pow2_mask == 0);
#endif
    }
#endif  // HWY_TARGET == HWY_SCALAR
  }
}

HWY_NOINLINE void TestAllForGEVectors() {
  DoTestAllForGEVectors<16>(ForGEVectors<16, TestForGEVectors>());
  DoTestAllForGEVectors<32>(ForGEVectors<32, TestForGEVectors>());
  DoTestAllForGEVectors<64>(ForGEVectors<64, TestForGEVectors>());
  DoTestAllForGEVectors<128>(ForGEVectors<128, TestForGEVectors>());
  DoTestAllForGEVectors<256>(ForGEVectors<256, TestForGEVectors>());
  DoTestAllForGEVectors<512>(ForGEVectors<512, TestForGEVectors>());
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForPromoteVectors)

template <int kSrcLaneSizePow2, int kPromotePow2, class Test, class T,
          hwy::EnableIf<(kSrcLaneSizePow2 + kPromotePow2 <=
                         (HWY_HAVE_INTEGER64 ? 3 : 2))> * = nullptr>
static HWY_INLINE void ExecuteTestForPromoteVectors(const Test &test,
                                                    T /*unused*/) {
  test(T());
}

template <int kSrcLaneSizePow2, int kPromotePow2, class Test, class T,
          hwy::EnableIf<(kSrcLaneSizePow2 + kPromotePow2 >
                         (HWY_HAVE_INTEGER64 ? 3 : 2))> * = nullptr>
static HWY_INLINE void ExecuteTestForPromoteVectors(const Test & /*test*/,
                                                    T /*unused*/) {}

template <int kPow2>
static HWY_NOINLINE void DoTestAllForPromoteVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForPromoteVectorsState);

  const ForPromoteVectors<TestForPromoteVectors, kPow2> test;
  test(uint8_t());
  ExecuteTestForPromoteVectors<1, kPow2>(test, uint16_t());
  ExecuteTestForPromoteVectors<2, kPow2>(test, uint32_t());

  constexpr int kMaxSupportedPromoteLaneSize = kMaxSupportedLaneSize >> kPow2;
  static_assert(kMaxSupportedPromoteLaneSize > 0,
                "kMaxSupportedPromoteLaneSize > 0 must be true");

  constexpr int kSupportedPromoteLaneSizesMask =
      kSupportedLaneSizesMask & ((2 << kMaxSupportedPromoteLaneSize) - 1);

  HWY_ASSERT(TestForPromoteVectorsState.lane_sizes_mask ==
             kSupportedPromoteLaneSizesMask);

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForPromoteVectorsState.per_lane_size_states[lane_size];

    if (lane_size <= kMaxSupportedPromoteLaneSize) {
      const size_t lanes =
          LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size) >>
          (HWY_TARGET == HWY_SCALAR ? 0 : kPow2);
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
                 ((lanes << (HWY_TARGET == HWY_RVV ? 4 : 1)) - 1));

#if HWY_HAVE_SCALABLE
      const int expected_pow2_mask =
          ((kSupportedU8Pow2Mask >> kPow2) & (-((lane_size + 1) / 2))) |
          ((HWY_TARGET == HWY_RVV) ? 0 : (1 << (3 - kPow2)));
      HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
    } else {
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#if HWY_HAVE_SCALABLE
      HWY_ASSERT(per_lane_size_state->pow2_mask == 0);
#endif
    }
  }
}

HWY_NOINLINE void TestAllForPromoteVectors() {
  DoTestAllForPromoteVectors<1>();
  DoTestAllForPromoteVectors<2>();
#if HWY_HAVE_INTEGER64
  DoTestAllForPromoteVectors<3>();
#endif
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForDemoteVectors)

template <int kSrcLaneSizePow2, int kDemotePow2, class Test, class T,
          hwy::EnableIf<(kSrcLaneSizePow2 >= kDemotePow2)> * = nullptr>
static HWY_INLINE void ExecuteTestForDemoteVectors(const Test &test,
                                                   T /*unused*/) {
  test(T());
}

template <int kSrcLaneSizePow2, int kDemotePow2, class Test, class T,
          hwy::EnableIf<(kSrcLaneSizePow2 < kDemotePow2)> * = nullptr>
static HWY_INLINE void ExecuteTestForDemoteVectors(const Test & /*test*/,
                                                   T /*unused*/) {}

template <int kPow2>
HWY_NOINLINE void DoTestAllForDemoteVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForDemoteVectorsState);

  const ForDemoteVectors<TestForDemoteVectors, kPow2> test;
  ExecuteTestForDemoteVectors<1, kPow2>(test, uint16_t());
  ExecuteTestForDemoteVectors<2, kPow2>(test, uint32_t());
#if HWY_HAVE_INTEGER64
  ExecuteTestForDemoteVectors<3, kPow2>(test, uint64_t());
#endif

  constexpr int kMinDemotableLaneSize = 1 << kPow2;
  constexpr int kSupportedDemoteLaneSizesMask =
      kSupportedLaneSizesMask & (-(1 << kMinDemotableLaneSize));

  HWY_ASSERT(TestForDemoteVectorsState.lane_sizes_mask ==
             kSupportedDemoteLaneSizesMask);

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForDemoteVectorsState.per_lane_size_states[lane_size];

    if (lane_size >= kMinDemotableLaneSize) {
      const size_t lanes =
          LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
                 ((lanes << (HWY_TARGET == HWY_RVV ? 4 : 1)) - 1));

#if HWY_HAVE_SCALABLE
      const int expected_pow2_mask =
          kSupportedU8Pow2Mask &
          ((-lane_size) | (((lane_size >> kPow2) > 1) ? (lane_size >> 1) : 0));
      HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
    } else {
      HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == 0);
#if HWY_HAVE_SCALABLE
      HWY_ASSERT(per_lane_size_state->pow2_mask == 0);
#endif
    }
  }
}

HWY_NOINLINE void TestAllForDemoteVectors() {
  DoTestAllForDemoteVectors<1>();
  DoTestAllForDemoteVectors<2>();
#if HWY_HAVE_INTEGER64
  DoTestAllForDemoteVectors<3>();
#endif
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForHalfVectors)

template <int kPow2>
static HWY_NOINLINE void DoTestAllForHalfVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForHalfVectorsState);
  ForUnsignedTypes(ForHalfVectors<TestForHalfVectors, kPow2>());

#if HWY_TARGET == HWY_SCALAR
  const size_t kMinSrcVectLanes = 1;
#else
  const size_t kMinSrcVectLanes = size_t{1} << kPow2;
#endif

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

#if HWY_TARGET == HWY_SCALAR || HWY_TARGET == HWY_RVV
  const int expected_lane_sizes_mask = kSupportedLaneSizesMask;
#else
  const int expected_lane_sizes_mask =
      kSupportedLaneSizesMask &
      (((lanes_per_u8_vect >= kMinSrcVectLanes) ? (1 << 1) : 0) |
       ((lanes_per_u8_vect >= 2 * kMinSrcVectLanes) ? (1 << 2) : 0) |
       ((lanes_per_u8_vect >= 4 * kMinSrcVectLanes) ? (1 << 4) : 0) |
       ((lanes_per_u8_vect >= 8 * kMinSrcVectLanes) ? (1 << 8) : 0));
#endif

  HWY_ASSERT(TestForHalfVectorsState.lane_sizes_mask ==
             expected_lane_sizes_mask);

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForHalfVectorsState.per_lane_size_states[lane_size];

    const size_t lanes = LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
               (((lanes << (HWY_TARGET == HWY_RVV ? 4 : 1)) - 1) &
                (size_t{0} - kMinSrcVectLanes)));

#if HWY_HAVE_SCALABLE
    const int expected_pow2_mask =
        kSupportedU8Pow2Mask & ((-(((lane_size + 1) / 2) << kPow2)) |
                                (lanes >= kMinSrcVectLanes ? 8 : 0));
    HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
  }
}

HWY_NOINLINE void TestAllForHalfVectors() {
  DoTestAllForHalfVectors<1>();
  DoTestAllForHalfVectors<2>();
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForPartialVectors)

HWY_NOINLINE void TestAllForPartialVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(&TestForPartialVectorsState);
  ForUnsignedTypes(ForPartialVectors<TestForPartialVectors>());

  HWY_ASSERT(TestForPartialVectorsState.lane_sizes_mask ==
             kSupportedLaneSizesMask);

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForPartialVectorsState.per_lane_size_states[lane_size];

    const size_t lanes = LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask ==
               ((lanes << (HWY_TARGET == HWY_RVV ? 4 : 1)) - 1));

#if HWY_HAVE_SCALABLE
    const int expected_pow2_mask =
        kSupportedU8Pow2Mask & (-((lane_size + 1) / 2));
    HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif
  }
}

HWY_DECLARE_FOREACH_VECTOR_TEST(TestForPartialFixedOrFullVectors)

HWY_NOINLINE void TestAllForPartialFixedOrFullVectors() {
  ZeroBytes<sizeof(ForeachVectorTestState)>(
      &TestForPartialFixedOrFullVectorsState);
  ForUnsignedTypes(
      ForPartialFixedOrFullScalableVectors<TestForPartialFixedOrFullVectors>());

  HWY_ASSERT(TestForPartialFixedOrFullVectorsState.lane_sizes_mask ==
             kSupportedLaneSizesMask);

  const size_t lanes_per_u8_vect = Lanes(ScalableTag<uint8_t>());

  for (int lane_size = 1; lane_size <= kMaxSupportedLaneSize; lane_size <<= 1) {
    ForeachVectorTestPerLaneSizeState *per_lane_size_state =
        &TestForPartialFixedOrFullVectorsState.per_lane_size_states[lane_size];

    const size_t lanes = LanesPerVectWithLaneSize(lanes_per_u8_vect, lane_size);
#if HWY_TARGET == HWY_RVV
    const size_t expected_lanes_mask =
        ((lanes * 16) - 1) & (size_t{0} - ((lanes_per_u8_vect + 7) / 8));
#elif HWY_HAVE_SCALABLE || HWY_TARGET_IS_SVE
    const size_t expected_lanes_mask = lanes;
#else
    const size_t expected_lanes_mask = (lanes << 1) - 1;
#endif

    HWY_ASSERT(per_lane_size_state->num_of_lanes_mask == expected_lanes_mask);

#if HWY_HAVE_SCALABLE
#if HWY_TARGET == HWY_RVV
    const int expected_pow2_mask = kSupportedU8Pow2Mask & (-lane_size);
#else
    const int expected_pow2_mask = 8;
#endif
    HWY_ASSERT(per_lane_size_state->pow2_mask == expected_pow2_mask);
#endif  // HWY_HAVE_SCALABLE
  }
}

#undef HWY_DECLARE_FOREACH_VECTOR_TEST

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyForeachVecTest);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForMaxPow2);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForExtendableVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForShrinkableVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForGEVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForPromoteVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForDemoteVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForHalfVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForPartialVectors);
HWY_EXPORT_AND_TEST_P(HwyForeachVecTest, TestAllForPartialFixedOrFullVectors);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
