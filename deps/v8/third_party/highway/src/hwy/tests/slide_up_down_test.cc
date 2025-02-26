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

#include <string.h>  // memset

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/slide_up_down_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

class TestSlideUpLanes {
 private:
  template <class D>
  static HWY_INLINE void DoTestSlideUpLanes(D d,
                                            TFromD<D>* HWY_RESTRICT expected,
                                            const size_t N,
                                            const size_t slide_amt) {
    for (size_t i = 0; i < N; i++) {
      expected[i] = ConvertScalarTo<TFromD<D>>(
          (i >= slide_amt) ? (i - slide_amt + 1) : 0);
    }

    const auto v = Iota(d, 1);
    HWY_ASSERT_VEC_EQ(d, expected, SlideUpLanes(d, v, slide_amt));
    if (slide_amt == 1) {
      HWY_ASSERT_VEC_EQ(d, expected, Slide1Up(d, v));
    }
  }
#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
  template <class D>
  static HWY_NOINLINE void DoTestSlideUpLanesWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    DoTestSlideUpLanes(d, expected, N, 0);
    if (N <= 1) return;
    DoTestSlideUpLanes(d, expected, N, 1);
    if (N <= 2) return;
    DoTestSlideUpLanes(d, expected, N, 2);
    DoTestSlideUpLanes(d, expected, N, 3);
    if (N <= 4) return;
    DoTestSlideUpLanes(d, expected, N, 4);
    DoTestSlideUpLanes(d, expected, N, 5);
    DoTestSlideUpLanes(d, expected, N, 6);
    DoTestSlideUpLanes(d, expected, N, 7);
  }
  template <class D, HWY_IF_LANES_LE_D(D, 8)>
  static HWY_INLINE void DoTestSlideUpLanesWithConstAmt_8_15(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 8)>
  static HWY_NOINLINE void DoTestSlideUpLanesWithConstAmt_8_15(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 8) return;
    DoTestSlideUpLanes(d, expected, N, 8);
    DoTestSlideUpLanes(d, expected, N, 9);
    DoTestSlideUpLanes(d, expected, N, 10);
    DoTestSlideUpLanes(d, expected, N, 11);
    DoTestSlideUpLanes(d, expected, N, 12);
    DoTestSlideUpLanes(d, expected, N, 13);
    DoTestSlideUpLanes(d, expected, N, 14);
    DoTestSlideUpLanes(d, expected, N, 15);
  }
#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
  template <class D, HWY_IF_LANES_LE_D(D, 16)>
  static HWY_INLINE void DoTestSlideUpLanesWithConstAmt_16_31(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 16)>
  static HWY_NOINLINE void DoTestSlideUpLanesWithConstAmt_16_31(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 16) return;
    DoTestSlideUpLanes(d, expected, N, 16);
    DoTestSlideUpLanes(d, expected, N, 17);
    DoTestSlideUpLanes(d, expected, N, 18);
    DoTestSlideUpLanes(d, expected, N, 19);
    DoTestSlideUpLanes(d, expected, N, 20);
    DoTestSlideUpLanes(d, expected, N, 21);
    DoTestSlideUpLanes(d, expected, N, 22);
    DoTestSlideUpLanes(d, expected, N, 23);
    DoTestSlideUpLanes(d, expected, N, 24);
    DoTestSlideUpLanes(d, expected, N, 25);
    DoTestSlideUpLanes(d, expected, N, 26);
    DoTestSlideUpLanes(d, expected, N, 27);
    DoTestSlideUpLanes(d, expected, N, 28);
    DoTestSlideUpLanes(d, expected, N, 29);
    DoTestSlideUpLanes(d, expected, N, 30);
    DoTestSlideUpLanes(d, expected, N, 31);
  }
#if HWY_TARGET <= HWY_AVX3
  template <class D, HWY_IF_LANES_LE_D(D, 32)>
  static HWY_INLINE void DoTestSlideUpLanesWithConstAmt_32_63(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 32)>
  static HWY_NOINLINE void DoTestSlideUpLanesWithConstAmt_32_63(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 32) return;
    DoTestSlideUpLanes(d, expected, N, 32);
    DoTestSlideUpLanes(d, expected, N, 33);
    DoTestSlideUpLanes(d, expected, N, 34);
    DoTestSlideUpLanes(d, expected, N, 35);
    DoTestSlideUpLanes(d, expected, N, 36);
    DoTestSlideUpLanes(d, expected, N, 37);
    DoTestSlideUpLanes(d, expected, N, 38);
    DoTestSlideUpLanes(d, expected, N, 39);
    DoTestSlideUpLanes(d, expected, N, 40);
    DoTestSlideUpLanes(d, expected, N, 41);
    DoTestSlideUpLanes(d, expected, N, 42);
    DoTestSlideUpLanes(d, expected, N, 43);
    DoTestSlideUpLanes(d, expected, N, 44);
    DoTestSlideUpLanes(d, expected, N, 45);
    DoTestSlideUpLanes(d, expected, N, 46);
    DoTestSlideUpLanes(d, expected, N, 47);
    DoTestSlideUpLanes(d, expected, N, 48);
    DoTestSlideUpLanes(d, expected, N, 49);
    DoTestSlideUpLanes(d, expected, N, 50);
    DoTestSlideUpLanes(d, expected, N, 51);
    DoTestSlideUpLanes(d, expected, N, 52);
    DoTestSlideUpLanes(d, expected, N, 53);
    DoTestSlideUpLanes(d, expected, N, 54);
    DoTestSlideUpLanes(d, expected, N, 55);
    DoTestSlideUpLanes(d, expected, N, 56);
    DoTestSlideUpLanes(d, expected, N, 57);
    DoTestSlideUpLanes(d, expected, N, 58);
    DoTestSlideUpLanes(d, expected, N, 59);
    DoTestSlideUpLanes(d, expected, N, 60);
    DoTestSlideUpLanes(d, expected, N, 61);
    DoTestSlideUpLanes(d, expected, N, 62);
    DoTestSlideUpLanes(d, expected, N, 63);
  }
#endif  // HWY_TARGET <= HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    for (size_t i = 0; i < N; i++) {
      size_t slide_amt = i;
#if !HWY_COMPILER_MSVC
      PreventElision(slide_amt);
#endif
      DoTestSlideUpLanes(d, expected.get(), N, slide_amt);
    }

#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
    DoTestSlideUpLanesWithConstAmt_0_7(d, expected.get(), N);
    DoTestSlideUpLanesWithConstAmt_8_15(d, expected.get(), N);
#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
    DoTestSlideUpLanesWithConstAmt_16_31(d, expected.get(), N);
#if HWY_TARGET <= HWY_AVX3
    DoTestSlideUpLanesWithConstAmt_32_63(d, expected.get(), N);
#endif  // HWY_TARGET <= HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
  }
};

HWY_NOINLINE void TestAllSlideUpLanes() {
  ForAllTypes(ForPartialVectors<TestSlideUpLanes>());
}

#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
// DoTestSlideDownLanes needs to be inlined on targets where
// DoTestSlideDownLanesWithConstAmt_0_7, DoTestSlideDownLanesWithConstAmt_8_15,
// DoTestSlideDownLanesWithConstAmt_16_31, and
// DoTestSlideDownLanesWithConstAmt_32_63 are called since the implementation
// of SlideDownLanes(d, v, N) for the SSE2/SSSE3/SSE4/AVX2/AVX3/NEON/WASM
// targets has an optimized path for the case where __builtin_constant_p(N) is
// true (or in other words, when N is known to be a constant) when compiled with
// GCC or Clang and optimizations are enabled.

// If DoTestSlideDownLanes is not inlined on the
// SSE2/SSSE3/SSE4/AVX2/AVX3/NEON/WASM targets,
// DoTestSlideDownLanesWithConstAmt_0_7, DoTestSlideDownLanesWithConstAmt_8_15,
// DoTestSlideDownLanesWithConstAmt_16_31, and
// DoTestSlideDownLanesWithConstAmt_32_63 will fail to throughly test the
// implementations of SlideDownLanes(d, v, N) in optimized builds compiled with
// GCC or Clang for the case where N is known to be a constant.
#define HWY_SLIDE_DOWN_TEST_INLINE HWY_INLINE
#else
// DoTestSlideDownLanes should not be inlined on RVV targets to work around RVV
// miscompilation.
#define HWY_SLIDE_DOWN_TEST_INLINE HWY_NOINLINE
#endif

class TestSlideDownLanes {
 private:
  // HWY_SLIDE_DOWN_TEST_INLINE is required here to work around RVV
  // miscompilation.
  template <class D>
  static HWY_SLIDE_DOWN_TEST_INLINE void DoTestSlideDownLanes(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N,
      const size_t slide_amt) {
    for (size_t i = 0; i < N; i++) {
      const size_t src_idx = slide_amt + i;
      expected[i] = ConvertScalarTo<TFromD<D>>((src_idx < N) ? src_idx : 0);
    }

    const Vec<D> v = Iota(d, 0);
    HWY_ASSERT_VEC_EQ(d, expected, SlideDownLanes(d, v, slide_amt));
    if (slide_amt == 1) {
      HWY_ASSERT_VEC_EQ(d, expected, Slide1Down(d, v));
    }
  }
#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
  template <class D>
  static HWY_NOINLINE void DoTestSlideDownLanesWithConstAmt_0_7(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    DoTestSlideDownLanes(d, expected, N, 0);
    if (N <= 1) return;
    DoTestSlideDownLanes(d, expected, N, 1);
    if (N <= 2) return;
    DoTestSlideDownLanes(d, expected, N, 2);
    DoTestSlideDownLanes(d, expected, N, 3);
    if (N <= 4) return;
    DoTestSlideDownLanes(d, expected, N, 4);
    DoTestSlideDownLanes(d, expected, N, 5);
    DoTestSlideDownLanes(d, expected, N, 6);
    DoTestSlideDownLanes(d, expected, N, 7);
  }
  template <class D, HWY_IF_LANES_LE_D(D, 8)>
  static HWY_INLINE void DoTestSlideDownLanesWithConstAmt_8_15(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 8)>
  static HWY_NOINLINE void DoTestSlideDownLanesWithConstAmt_8_15(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 8) return;
    DoTestSlideDownLanes(d, expected, N, 8);
    DoTestSlideDownLanes(d, expected, N, 9);
    DoTestSlideDownLanes(d, expected, N, 10);
    DoTestSlideDownLanes(d, expected, N, 11);
    DoTestSlideDownLanes(d, expected, N, 12);
    DoTestSlideDownLanes(d, expected, N, 13);
    DoTestSlideDownLanes(d, expected, N, 14);
    DoTestSlideDownLanes(d, expected, N, 15);
  }
#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
  template <class D, HWY_IF_LANES_LE_D(D, 16)>
  static HWY_INLINE void DoTestSlideDownLanesWithConstAmt_16_31(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 16)>
  static HWY_NOINLINE void DoTestSlideDownLanesWithConstAmt_16_31(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 16) return;
    DoTestSlideDownLanes(d, expected, N, 16);
    DoTestSlideDownLanes(d, expected, N, 17);
    DoTestSlideDownLanes(d, expected, N, 18);
    DoTestSlideDownLanes(d, expected, N, 19);
    DoTestSlideDownLanes(d, expected, N, 20);
    DoTestSlideDownLanes(d, expected, N, 21);
    DoTestSlideDownLanes(d, expected, N, 22);
    DoTestSlideDownLanes(d, expected, N, 23);
    DoTestSlideDownLanes(d, expected, N, 24);
    DoTestSlideDownLanes(d, expected, N, 25);
    DoTestSlideDownLanes(d, expected, N, 26);
    DoTestSlideDownLanes(d, expected, N, 27);
    DoTestSlideDownLanes(d, expected, N, 28);
    DoTestSlideDownLanes(d, expected, N, 29);
    DoTestSlideDownLanes(d, expected, N, 30);
    DoTestSlideDownLanes(d, expected, N, 31);
  }
#if HWY_TARGET <= HWY_AVX3
  template <class D, HWY_IF_LANES_LE_D(D, 32)>
  static HWY_INLINE void DoTestSlideDownLanesWithConstAmt_32_63(
      D /*d*/, TFromD<D>* HWY_RESTRICT /*expected*/, const size_t /*N*/) {}
  template <class D, HWY_IF_LANES_GT_D(D, 32)>
  static HWY_NOINLINE void DoTestSlideDownLanesWithConstAmt_32_63(
      D d, TFromD<D>* HWY_RESTRICT expected, const size_t N) {
    if (N <= 32) return;
    DoTestSlideDownLanes(d, expected, N, 32);
    DoTestSlideDownLanes(d, expected, N, 33);
    DoTestSlideDownLanes(d, expected, N, 34);
    DoTestSlideDownLanes(d, expected, N, 35);
    DoTestSlideDownLanes(d, expected, N, 36);
    DoTestSlideDownLanes(d, expected, N, 37);
    DoTestSlideDownLanes(d, expected, N, 38);
    DoTestSlideDownLanes(d, expected, N, 39);
    DoTestSlideDownLanes(d, expected, N, 40);
    DoTestSlideDownLanes(d, expected, N, 41);
    DoTestSlideDownLanes(d, expected, N, 42);
    DoTestSlideDownLanes(d, expected, N, 43);
    DoTestSlideDownLanes(d, expected, N, 44);
    DoTestSlideDownLanes(d, expected, N, 45);
    DoTestSlideDownLanes(d, expected, N, 46);
    DoTestSlideDownLanes(d, expected, N, 47);
    DoTestSlideDownLanes(d, expected, N, 48);
    DoTestSlideDownLanes(d, expected, N, 49);
    DoTestSlideDownLanes(d, expected, N, 50);
    DoTestSlideDownLanes(d, expected, N, 51);
    DoTestSlideDownLanes(d, expected, N, 52);
    DoTestSlideDownLanes(d, expected, N, 53);
    DoTestSlideDownLanes(d, expected, N, 54);
    DoTestSlideDownLanes(d, expected, N, 55);
    DoTestSlideDownLanes(d, expected, N, 56);
    DoTestSlideDownLanes(d, expected, N, 57);
    DoTestSlideDownLanes(d, expected, N, 58);
    DoTestSlideDownLanes(d, expected, N, 59);
    DoTestSlideDownLanes(d, expected, N, 60);
    DoTestSlideDownLanes(d, expected, N, 61);
    DoTestSlideDownLanes(d, expected, N, 62);
    DoTestSlideDownLanes(d, expected, N, 63);
  }
#endif  // HWY_TARGET <= HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    for (size_t i = 0; i < N; i++) {
      size_t slide_amt = i;
#if !HWY_COMPILER_MSVC
      PreventElision(slide_amt);
#endif
      DoTestSlideDownLanes(d, expected.get(), N, slide_amt);
    }

#if !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
    DoTestSlideDownLanesWithConstAmt_0_7(d, expected.get(), N);
    DoTestSlideDownLanesWithConstAmt_8_15(d, expected.get(), N);
#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
    DoTestSlideDownLanesWithConstAmt_16_31(d, expected.get(), N);
#if HWY_TARGET <= HWY_AVX3
    DoTestSlideDownLanesWithConstAmt_32_63(d, expected.get(), N);
#endif  // HWY_TARGET <= HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_WASM_EMU256
#endif  // !HWY_HAVE_SCALABLE && HWY_TARGET < HWY_EMU128 && !HWY_TARGET_IS_SVE
  }
};

#undef HWY_SLIDE_DOWN_TEST_INLINE

HWY_NOINLINE void TestAllSlideDownLanes() {
  ForAllTypes(ForPartialVectors<TestSlideDownLanes>());
}

struct TestSlide1 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto iota0 = Iota(d, 0);
    const auto iota1 = Iota(d, 1);

    const auto expected_slide_down_result =
        IfThenElseZero(FirstN(d, Lanes(d) - 1), iota1);

    HWY_ASSERT_VEC_EQ(d, iota0, Slide1Up(d, iota1));
    HWY_ASSERT_VEC_EQ(d, expected_slide_down_result, Slide1Down(d, iota0));
  }
};

HWY_NOINLINE void TestAllSlide1() {
  ForAllTypes(ForPartialVectors<TestSlide1>());
}

class TestSlideBlocks {
 private:
  template <int kBlocks, class D>
  static HWY_INLINE void DoTestSlideByKBlocks(D d) {
    using T = TFromD<D>;
    constexpr size_t kLanesPerBlock = 16 / sizeof(T);
    constexpr size_t kLanesToSlide =
        static_cast<size_t>(kBlocks) * kLanesPerBlock;

    const auto iota_0 = Iota(d, 0);
    const auto iota_k = Iota(d, kLanesToSlide);

    const auto first_k_lanes_mask = FirstN(d, kLanesToSlide);
    const auto expected_slide_up_result =
        IfThenZeroElse(first_k_lanes_mask, iota_0);
    HWY_ASSERT_VEC_EQ(d, expected_slide_up_result,
                      SlideUpBlocks<kBlocks>(d, iota_k));

    const RebindToUnsigned<decltype(d)> du;
    using TU = TFromD<decltype(du)>;
    const auto slide_down_result_mask = BitCast(
        d, Reverse(du, IfThenZeroElse(RebindMask(du, first_k_lanes_mask),
                                      Set(du, hwy::LimitsMax<TU>()))));

    const auto expected_slide_down_result = And(slide_down_result_mask, iota_k);
    HWY_ASSERT_VEC_EQ(d, expected_slide_down_result,
                      SlideDownBlocks<kBlocks>(d, iota_0));
  }

#if HWY_MAX_BYTES >= 32
  template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
  static HWY_INLINE void DoTestSlideBy1Block(D /*d*/, size_t /*N*/) {}

  template <class D, HWY_IF_V_SIZE_GT_D(D, 16)>
  static HWY_INLINE void DoTestSlideBy1Block(D d, size_t N) {
    if (N < (32 / sizeof(TFromD<D>))) return;
    DoTestSlideByKBlocks<1>(d);
  }

#if HWY_MAX_BYTES >= 64
  template <class D, HWY_IF_V_SIZE_LE_D(D, 32)>
  static HWY_INLINE void DoTestSlideBy2And3Blocks(D /*d*/, size_t /*N*/) {}

  template <class D, HWY_IF_V_SIZE_GT_D(D, 32)>
  static HWY_INLINE void DoTestSlideBy2And3Blocks(D d, size_t N) {
    if (N < (64 / sizeof(TFromD<D>))) return;
    DoTestSlideByKBlocks<2>(d);
    DoTestSlideByKBlocks<3>(d);
  }
#endif  // HWY_MAX_BYTES >= 64
#endif  // HWY_MAX_BYTES >= 32

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    DoTestSlideByKBlocks<0>(d);
#if HWY_MAX_BYTES >= 32
    const size_t N = Lanes(d);
    DoTestSlideBy1Block(d, N);
#if HWY_MAX_BYTES >= 64
    DoTestSlideBy2And3Blocks(d, N);
#endif  // HWY_MAX_BYTES >= 64
#endif  // HWY_MAX_BYTES >= 32
  }
};

HWY_NOINLINE void TestAllSlideBlocks() {
  ForAllTypes(ForPartialVectors<TestSlideBlocks>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwySlideUpDownTest);
HWY_EXPORT_AND_TEST_P(HwySlideUpDownTest, TestAllSlideUpLanes);
HWY_EXPORT_AND_TEST_P(HwySlideUpDownTest, TestAllSlideDownLanes);
HWY_EXPORT_AND_TEST_P(HwySlideUpDownTest, TestAllSlide1);
HWY_EXPORT_AND_TEST_P(HwySlideUpDownTest, TestAllSlideBlocks);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
