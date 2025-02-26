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
#define HWY_TARGET_INCLUDE "tests/fma_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

#ifndef HWY_NATIVE_FMA
#error "Bug in set_macros-inl.h, did not set HWY_NATIVE_FMA"
#endif

struct TestMulAdd {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> k0 = Zero(d);
    const Vec<D> v1 = Iota(d, 1);
    const Vec<D> v2 = Iota(d, 2);

    // Unlike RebindToSigned, we want to leave floating-point unchanged.
    // This allows Neg for unsigned types.
    const Rebind<If<IsFloat<T>(), T, MakeSigned<T>>, D> dif;
    const Vec<D> neg_v2 = BitCast(d, Neg(BitCast(dif, v2)));

    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);
    HWY_ASSERT_VEC_EQ(d, k0, MulAdd(k0, k0, k0));
    HWY_ASSERT_VEC_EQ(d, v2, MulAdd(k0, v1, v2));
    HWY_ASSERT_VEC_EQ(d, v2, MulAdd(v1, k0, v2));
    HWY_ASSERT_VEC_EQ(d, k0, NegMulAdd(k0, k0, k0));
    HWY_ASSERT_VEC_EQ(d, v2, NegMulAdd(k0, v1, v2));
    HWY_ASSERT_VEC_EQ(d, v2, NegMulAdd(v1, k0, v2));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i + 1) * (i + 2));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulAdd(v2, v1, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulAdd(v1, v2, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulAdd(neg_v2, v1, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulAdd(v1, neg_v2, k0));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i + 2) * (i + 2) + (i + 1));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulAdd(v2, v2, v1));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulAdd(neg_v2, v2, v1));

    for (size_t i = 0; i < N; ++i) {
      const T nm = ConvertScalarTo<T>(-static_cast<int>(i + 2));
      const T f = ConvertScalarTo<T>(i + 2);
      const T a = ConvertScalarTo<T>(i + 1);
      expected[i] = ConvertScalarTo<T>(nm * f + a);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulAdd(v2, v2, v1));
  }
};

HWY_NOINLINE void TestAllMulAdd() {
  ForAllTypes(ForPartialVectors<TestMulAdd>());
}

struct TestMulSub {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> k0 = Zero(d);
    const Vec<D> kNeg0 = Set(d, ConvertScalarTo<T>(-0.0));
    const Vec<D> v1 = Iota(d, 1);
    const Vec<D> v2 = Iota(d, 2);
    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    // Unlike RebindToSigned, we want to leave floating-point unchanged.
    // This allows Neg for unsigned types.
    const Rebind<If<IsFloat<T>(), T, MakeSigned<T>>, D> dif;

    HWY_ASSERT_VEC_EQ(d, k0, MulSub(k0, k0, k0));
    HWY_ASSERT_VEC_EQ(d, kNeg0, NegMulSub(k0, k0, k0));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(-static_cast<int>(i + 2));
    }
    const auto neg_k0 = BitCast(d, Neg(BitCast(dif, k0)));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulSub(k0, v1, v2));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulSub(v1, k0, v2));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(neg_k0, v1, v2));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(v1, neg_k0, v2));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i + 1) * (i + 2));
    }
    const auto neg_v1 = BitCast(d, Neg(BitCast(dif, v1)));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulSub(v1, v2, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulSub(v2, v1, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(neg_v1, v2, k0));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(v2, neg_v1, k0));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i + 2) * (i + 2) - (1 + i));
    }
    const auto neg_v2 = BitCast(d, Neg(BitCast(dif, v2)));
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulSub(v2, v2, v1));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(neg_v2, v2, v1));
    HWY_ASSERT_VEC_EQ(d, expected.get(), NegMulSub(v2, neg_v2, v1));
  }
};

HWY_NOINLINE void TestAllMulSub() {
  ForAllTypes(ForPartialVectors<TestMulSub>());
}

struct TestMulAddSub {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> k0 = Zero(d);
    const Vec<D> v1 = Iota(d, 1);
    const Vec<D> v2 = Iota(d, 2);

    // Unlike RebindToSigned, we want to leave floating-point unchanged.
    // This allows Neg for unsigned types.
    const Rebind<If<IsFloat<T>(), T, MakeSigned<T>>, D> dif;
    const Vec<D> neg_v2 = BitCast(d, Neg(BitCast(dif, v2)));

    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    HWY_ASSERT_VEC_EQ(d, k0, MulAddSub(k0, k0, k0));

    const auto v2_negated_if_even = OddEven(v2, neg_v2);
    HWY_ASSERT_VEC_EQ(d, v2_negated_if_even, MulAddSub(k0, v1, v2));
    HWY_ASSERT_VEC_EQ(d, v2_negated_if_even, MulAddSub(v1, k0, v2));

    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>(((i & 1) == 0) ? ((i + 2) * (i + 2) - (i + 1))
                                            : ((i + 2) * (i + 2) + (i + 1)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), MulAddSub(v2, v2, v1));
  }
};

HWY_NOINLINE void TestAllMulAddSub() {
  ForAllTypes(ForPartialVectors<TestMulAddSub>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyFmaTest);
HWY_EXPORT_AND_TEST_P(HwyFmaTest, TestAllMulAdd);
HWY_EXPORT_AND_TEST_P(HwyFmaTest, TestAllMulSub);
HWY_EXPORT_AND_TEST_P(HwyFmaTest, TestAllMulAddSub);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
