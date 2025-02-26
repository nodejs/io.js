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

#include "hwy/nanobenchmark.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/concat_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestConcat {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    if (N == 1) return;
    const size_t half_bytes = N * sizeof(T) / 2;

    auto hi = AllocateAligned<T>(N);
    auto lo = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(hi && lo && expected);
    RandomState rng;
    for (size_t rep = 0; rep < 10; ++rep) {
      for (size_t i = 0; i < N; ++i) {
        hi[i] = ConvertScalarTo<T>(Random64(&rng) & 0xFF);
        lo[i] = ConvertScalarTo<T>(Random64(&rng) & 0xFF);
      }

      {
        CopyBytes(&hi[N / 2], &expected[N / 2], half_bytes);
        CopyBytes(&lo[0], &expected[0], half_bytes);
        const Vec<D> vhi = Load(d, hi.get());
        const Vec<D> vlo = Load(d, lo.get());
        HWY_ASSERT_VEC_EQ(d, expected.get(), ConcatUpperLower(d, vhi, vlo));
      }

      {
        CopyBytes(&hi[N / 2], &expected[N / 2], half_bytes);
        CopyBytes(&lo[N / 2], &expected[0], half_bytes);
        const Vec<D> vhi = Load(d, hi.get());
        const Vec<D> vlo = Load(d, lo.get());
        HWY_ASSERT_VEC_EQ(d, expected.get(), ConcatUpperUpper(d, vhi, vlo));
      }

      {
        CopyBytes(&hi[0], &expected[N / 2], half_bytes);
        CopyBytes(&lo[N / 2], &expected[0], half_bytes);
        const Vec<D> vhi = Load(d, hi.get());
        const Vec<D> vlo = Load(d, lo.get());
        HWY_ASSERT_VEC_EQ(d, expected.get(), ConcatLowerUpper(d, vhi, vlo));
      }

      {
        CopyBytes(&hi[0], &expected[N / 2], half_bytes);
        CopyBytes(&lo[0], &expected[0], half_bytes);
        const Vec<D> vhi = Load(d, hi.get());
        const Vec<D> vlo = Load(d, lo.get());
        HWY_ASSERT_VEC_EQ(d, expected.get(), ConcatLowerLower(d, vhi, vlo));
      }
    }
  }
};

HWY_NOINLINE void TestAllConcat() {
  ForAllTypes(ForShrinkableVectors<TestConcat>());
}

struct TestConcatOddEven {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
#if HWY_TARGET != HWY_SCALAR
    const size_t N = Lanes(d);
    const Vec<D> hi = Iota(d, hwy::Unpredictable1() + N - 1);  // N, N+1, ...
    const Vec<D> lo = Iota(d, hwy::Unpredictable1() - 1);      // 0,1,2,3,...
    const Vec<D> even = Add(lo, lo);
    const Vec<D> odd = Add(even, Set(d, 1));
    HWY_ASSERT_VEC_EQ(d, odd, ConcatOdd(d, hi, lo));
    HWY_ASSERT_VEC_EQ(d, even, ConcatEven(d, hi, lo));

    const Vec<D> v_1 = Set(d, ConvertScalarTo<T>(1));
    const Vec<D> v_2 = Set(d, ConvertScalarTo<T>(2));
    const Vec<D> v_3 = Set(d, ConvertScalarTo<T>(3));
    const Vec<D> v_4 = Set(d, ConvertScalarTo<T>(4));

    const Half<decltype(d)> dh;
    const Vec<D> v_12 = InterleaveLower(v_1, v_2); /* {1, 2, 1, 2, ...} */
    const Vec<D> v_34 = InterleaveLower(v_3, v_4); /* {3, 4, 3, 4, ...} */
    const Vec<D> v_13 =
        ConcatLowerLower(d, v_3, v_1); /* {1, 1, ..., 3, 3, ...} */
    const Vec<D> v_24 =
        ConcatLowerLower(d, v_4, v_2); /* {2, 2, ..., 4, 4, ...} */

    const Vec<D> concat_even_1234_result = ConcatEven(d, v_34, v_12);
    const Vec<D> concat_odd_1234_result = ConcatOdd(d, v_34, v_12);

    HWY_ASSERT_VEC_EQ(d, v_13, concat_even_1234_result);
    HWY_ASSERT_VEC_EQ(d, v_24, concat_odd_1234_result);
    HWY_ASSERT_VEC_EQ(dh, LowerHalf(dh, v_3),
                      UpperHalf(dh, concat_even_1234_result));
    HWY_ASSERT_VEC_EQ(dh, LowerHalf(dh, v_4),
                      UpperHalf(dh, concat_odd_1234_result));

    // This test catches inadvertent saturation.
    const Vec<D> min = Set(d, LowestValue<T>());
    const Vec<D> max = Set(d, HighestValue<T>());
    HWY_ASSERT_VEC_EQ(d, max, ConcatOdd(d, max, max));
    HWY_ASSERT_VEC_EQ(d, max, ConcatEven(d, max, max));
    HWY_ASSERT_VEC_EQ(d, min, ConcatOdd(d, min, min));
    HWY_ASSERT_VEC_EQ(d, min, ConcatEven(d, min, min));
#else
    (void)d;
#endif  // HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllConcatOddEven() {
  ForAllTypes(ForShrinkableVectors<TestConcatOddEven>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyConcatTest);
HWY_EXPORT_AND_TEST_P(HwyConcatTest, TestAllConcat);
HWY_EXPORT_AND_TEST_P(HwyConcatTest, TestAllConcatOddEven);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
