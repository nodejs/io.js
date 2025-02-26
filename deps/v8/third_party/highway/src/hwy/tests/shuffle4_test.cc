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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/shuffle4_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

class TestPer4LaneBlockShuffle {
 private:
  template <class D, HWY_IF_LANES_LE_D(D, 1)>
  static HWY_INLINE VFromD<D> InterleaveMaskVectors(D /*d*/, VFromD<D> a,
                                                    VFromD<D> /*b*/) {
    return a;
  }
#if HWY_TARGET != HWY_SCALAR
  template <class D, HWY_IF_LANES_GT_D(D, 1)>
  static HWY_INLINE VFromD<D> InterleaveMaskVectors(D d, VFromD<D> a,
                                                    VFromD<D> b) {
    return InterleaveLower(d, a, b);
  }
#endif
  template <class D>
  static HWY_INLINE Mask<D> Per4LaneBlockShufValidMask(D d, const size_t N,
                                                       const size_t idx1,
                                                       const size_t idx0) {
    if (N < 4) {
      const RebindToSigned<decltype(d)> di;
      using TI = TFromD<decltype(di)>;
      const auto lane_0_valid =
          Set(di, static_cast<TI>(-static_cast<int>(idx0 < N)));
      if (N > 1) {
        const auto lane_1_valid =
            Set(di, static_cast<TI>(-static_cast<int>(idx1 < N)));
        return RebindMask(d, MaskFromVec(InterleaveMaskVectors(di, lane_0_valid,
                                                               lane_1_valid)));
      }
      return RebindMask(d, MaskFromVec(lane_0_valid));
    }

    return FirstN(d, N);
  }

  // TODO(b/287462770): inline to work around incorrect SVE codegen
  template <class D>
  static HWY_INLINE void DoCheckPer4LaneBlkShufResult(
      D d, const size_t N, VFromD<D> actual,
      const TFromD<D>* HWY_RESTRICT src_lanes, TFromD<D>* HWY_RESTRICT expected,
      size_t idx3, size_t idx2, size_t idx1, size_t idx0) {
    for (size_t i = 0; i < N; i += 4) {
      expected[i] = src_lanes[i + idx0];
      expected[i + 1] = src_lanes[i + idx1];
      expected[i + 2] = src_lanes[i + idx2];
      expected[i + 3] = src_lanes[i + idx3];
    }

    if (N < 4) {
      if (idx0 >= N) expected[0] = TFromD<D>{0};
      if (idx1 >= N) expected[1] = TFromD<D>{0};
    }

    const auto valid_lanes_mask = Per4LaneBlockShufValidMask(d, N, idx1, idx0);
    HWY_ASSERT_VEC_EQ(d, expected, IfThenElseZero(valid_lanes_mask, actual));
  }

#if HWY_TARGET != HWY_SCALAR
  template <class D>
  static HWY_NOINLINE void TestTblLookupPer4LaneBlkShuf(
      D d, const size_t N, const TFromD<D>* HWY_RESTRICT src_lanes,
      TFromD<D>* HWY_RESTRICT expected) {
    const auto v = Load(d, src_lanes);
    for (size_t idx3210 = 0; idx3210 <= 0xFF; idx3210++) {
      const size_t idx3 = (idx3210 >> 6) & 3;
      const size_t idx2 = (idx3210 >> 4) & 3;
      const size_t idx1 = (idx3210 >> 2) & 3;
      const size_t idx0 = idx3210 & 3;

      const auto actual = detail::TblLookupPer4LaneBlkShuf(v, idx3210);
      DoCheckPer4LaneBlkShufResult(d, N, actual, src_lanes, expected, idx3,
                                   idx2, idx1, idx0);
    }
  }
#endif

  template <size_t kIdx3, size_t kIdx2, size_t kIdx1, size_t kIdx0, class D>
  static HWY_INLINE void DoTestPer4LaneBlkShuffle(
      D d, const size_t N, const VFromD<D> v,
      const TFromD<D>* HWY_RESTRICT src_lanes,
      TFromD<D>* HWY_RESTRICT expected) {
    const auto actual = Per4LaneBlockShuffle<kIdx3, kIdx2, kIdx1, kIdx0>(v);
    DoCheckPer4LaneBlkShufResult(d, N, actual, src_lanes, expected, kIdx3,
                                 kIdx2, kIdx1, kIdx0);
  }

  template <class D>
  static HWY_NOINLINE void DoTestPer4LaneBlkShuffles(
      D d, const size_t N, const VecArg<VFromD<D>> v,
      TFromD<D>* HWY_RESTRICT src_lanes, TFromD<D>* HWY_RESTRICT expected) {
    Store(v, d, src_lanes);
#if HWY_TARGET != HWY_SCALAR
    TestTblLookupPer4LaneBlkShuf(d, N, src_lanes, expected);
#endif
    DoTestPer4LaneBlkShuffle<0, 1, 2, 3>(d, N, v, src_lanes, expected);
#if !HWY_COMPILER_MSVC  // speed up MSVC builds
    DoTestPer4LaneBlkShuffle<0, 1, 3, 2>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<0, 2, 3, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<0, 3, 0, 2>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 0, 1, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 0, 3, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 0, 3, 2>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 2, 0, 3>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 2, 1, 3>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<1, 1, 0, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 0, 1, 3>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 0, 2, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 1, 2, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 2, 0, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 3, 0, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<2, 3, 3, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 0, 2, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 1, 0, 3>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 1, 3, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 2, 1, 0>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 2, 3, 2>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 3, 0, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 3, 1, 1>(d, N, v, src_lanes, expected);
    DoTestPer4LaneBlkShuffle<3, 3, 2, 2>(d, N, v, src_lanes, expected);
#endif
  }

  template <class D>
  static HWY_INLINE Vec<D> GenerateTestVect(hwy::NonFloatTag /*tag*/, D d) {
    const RebindToUnsigned<decltype(d)> du;
    using TU = TFromD<decltype(du)>;
    constexpr TU kIotaStart =
        static_cast<TU>(0x0706050403020101u & LimitsMax<TU>());
    return BitCast(d, Iota(du, kIotaStart));
  }

  template <class D>
  static HWY_INLINE Vec<D> GenerateTestVect(hwy::FloatTag /*tag*/, D d) {
    const RebindToUnsigned<decltype(d)> du;
    using T = TFromD<decltype(d)>;
    using TU = TFromD<decltype(du)>;

    constexpr size_t kNumOfBitsInT = sizeof(T) * 8;
    constexpr TU kIntBitsMask =
        (kNumOfBitsInT > 16) ? static_cast<TU>(static_cast<TU>(~TU{0}) >> 16)
                             : TU{0};

    const auto flt_iota = Set(d, 1);
    if (kIntBitsMask == 0) return flt_iota;

    const auto int_iota =
        And(GenerateTestVect(hwy::NonFloatTag(), du), Set(du, kIntBitsMask));
    return Or(flt_iota, BitCast(d, int_iota));
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const size_t alloc_len = static_cast<size_t>((N + 3) & (~size_t{3}));
    HWY_ASSERT(alloc_len >= 4);

    auto expected = AllocateAligned<T>(alloc_len);
    auto src_lanes = AllocateAligned<T>(alloc_len);
    HWY_ASSERT(expected && src_lanes);

    const T k0 = ConvertScalarTo<T>(0);
    expected[alloc_len - 4] = k0;
    expected[alloc_len - 3] = k0;
    expected[alloc_len - 2] = k0;
    expected[alloc_len - 1] = k0;
    src_lanes[alloc_len - 4] = k0;
    src_lanes[alloc_len - 3] = k0;
    src_lanes[alloc_len - 2] = k0;
    src_lanes[alloc_len - 1] = k0;

    const auto v = GenerateTestVect(hwy::IsFloatTag<T>(), d);
    DoTestPer4LaneBlkShuffles(d, N, v, src_lanes.get(), expected.get());

    const RebindToUnsigned<decltype(d)> du;
    using TU = TFromD<decltype(du)>;
    const auto msb_mask =
        BitCast(d, Set(du, static_cast<TU>(TU{1} << (sizeof(TU) * 8 - 1))));

    DoTestPer4LaneBlkShuffles(d, N, Xor(v, msb_mask), src_lanes.get(),
                              expected.get());
  }
};

HWY_NOINLINE void TestAllPer4LaneBlockShuffle() {
  ForAllTypes(ForPartialFixedOrFullScalableVectors<TestPer4LaneBlockShuffle>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyShuffle4Test);
HWY_EXPORT_AND_TEST_P(HwyShuffle4Test, TestAllPer4LaneBlockShuffle);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
