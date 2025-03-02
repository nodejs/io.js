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

#include <stdint.h>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "tests/compare_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

// All types.
struct TestEquality {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto v2 = Iota(d, 2);
    const auto v2b = Iota(d, 2);
    const auto v3 = Iota(d, 3);

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq(v2, v3));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq(v3, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq(v2, v2b));

    HWY_ASSERT_MASK_EQ(d, mask_true, Ne(v2, v3));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne(v3, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne(v2, v2b));
  }
};

HWY_NOINLINE void TestAllEquality() {
  ForAllTypes(ForPartialVectors<TestEquality>());
}

// a > b should be true, verify that for Gt/Lt and with swapped args.
template <class D>
void EnsureGreater(D d, TFromD<D> a, TFromD<D> b, const char* file, int line) {
  const auto mask_false = MaskFalse(d);
  const auto mask_true = MaskTrue(d);

  const auto va = Set(d, a);
  const auto vb = Set(d, b);
  AssertMaskEqual(d, mask_true, Gt(va, vb), file, line);
  AssertMaskEqual(d, mask_false, Lt(va, vb), file, line);

  // Swapped order
  AssertMaskEqual(d, mask_false, Gt(vb, va), file, line);
  AssertMaskEqual(d, mask_true, Lt(vb, va), file, line);

  // Also ensure irreflexive
  AssertMaskEqual(d, mask_false, Gt(va, va), file, line);
  AssertMaskEqual(d, mask_false, Gt(vb, vb), file, line);
  AssertMaskEqual(d, mask_false, Lt(va, va), file, line);
  AssertMaskEqual(d, mask_false, Lt(vb, vb), file, line);
}

#define HWY_ENSURE_GREATER(d, a, b) EnsureGreater(d, a, b, __FILE__, __LINE__)

// a >= b should be true, verify that for Ge/Le and with swapped args.
template <class D>
void EnsureGreaterOrEqual(D d, TFromD<D> a, TFromD<D> b, const char* file,
                          int line) {
  const auto mask_true = MaskTrue(d);

  const auto va = Set(d, a);
  const auto vb = Set(d, b);

  const auto mask_eq = Eq(va, vb);

  AssertMaskEqual(d, mask_true, Ge(va, vb), file, line);
  AssertMaskEqual(d, mask_eq, Le(va, vb), file, line);

  // Swapped order
  AssertMaskEqual(d, mask_eq, Ge(vb, va), file, line);
  AssertMaskEqual(d, mask_true, Le(vb, va), file, line);

  // va >= va, vb >= vb, va <= va, and vb <= vb should all be true if
  // both a and b are non-NaN values
  AssertMaskEqual(d, mask_true, Ge(va, va), file, line);
  AssertMaskEqual(d, mask_true, Ge(vb, vb), file, line);
  AssertMaskEqual(d, mask_true, Le(va, va), file, line);
  AssertMaskEqual(d, mask_true, Le(vb, vb), file, line);
}

#define HWY_ENSURE_GREATER_OR_EQUAL(d, a, b) \
  EnsureGreaterOrEqual(d, a, b, __FILE__, __LINE__)

struct TestStrictUnsigned {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const T max = LimitsMax<T>();
    const Vec<D> v0 = Zero(d);
    const Vec<D> v2 = And(Iota(d, 2), Set(d, 255));  // 0..255

    const Mask<D> mask_false = MaskFalse(d);

    // Individual values of interest
    HWY_ENSURE_GREATER(d, 2, 1);
    HWY_ENSURE_GREATER(d, 1, 0);
    HWY_ENSURE_GREATER(d, 128, 127);
    HWY_ENSURE_GREATER(d, max, max / 2);
    HWY_ENSURE_GREATER(d, max, 1);
    HWY_ENSURE_GREATER(d, max, 0);

    // Also use Iota to ensure lanes are independent
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v0, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v2, v2));
  }
};

HWY_NOINLINE void TestAllStrictUnsigned() {
  ForUnsignedTypes(ForPartialVectors<TestStrictUnsigned>());
}

struct TestWeakUnsigned {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const T max = LimitsMax<T>();
    const Vec<D> v0 = Zero(d);
    const Vec<D> v1 = Set(d, 1u);
    const Vec<D> v2 = And(Iota(d, 2), Set(d, 255u));  // 0..255

    const Mask<D> mask_true = MaskTrue(d);

    // Individual values of interest
    HWY_ENSURE_GREATER_OR_EQUAL(d, 2, 2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 2, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 1, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 1, 0);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 0, 0);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 128, 127);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 128, 128);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 127, 127);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, max);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, max / 2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, 0);

    // Also use Iota to ensure lanes are independent
    const auto mask_v2_is_eq_to_v0 = Eq(v2, v0);
    HWY_ASSERT_MASK_EQ(d, mask_v2_is_eq_to_v0, Le(v2, v0));
    HWY_ASSERT_MASK_EQ(d, mask_v2_is_eq_to_v0, Ge(v0, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v2, v2));

    const auto v2_plus_1 = Add(v2, v1);
    HWY_ASSERT_MASK_EQ(d, Lt(v2, v2_plus_1), Le(v2, v2_plus_1));
    HWY_ASSERT_MASK_EQ(d, Gt(v2, v2_plus_1), Ge(v2, v2_plus_1));
    HWY_ASSERT_MASK_EQ(d, Lt(v2_plus_1, v2), Le(v2_plus_1, v2));
    HWY_ASSERT_MASK_EQ(d, Gt(v2_plus_1, v2), Ge(v2_plus_1, v2));

    const auto v2_minus_1 = Sub(v2, v1);
    HWY_ASSERT_MASK_EQ(d, Lt(v2, v2_minus_1), Le(v2, v2_minus_1));
    HWY_ASSERT_MASK_EQ(d, Gt(v2, v2_minus_1), Ge(v2, v2_minus_1));
    HWY_ASSERT_MASK_EQ(d, Lt(v2_minus_1, v2), Le(v2_minus_1, v2));
    HWY_ASSERT_MASK_EQ(d, Gt(v2_minus_1, v2), Ge(v2_minus_1, v2));
  }
};

HWY_NOINLINE void TestAllWeakUnsigned() {
  ForUnsignedTypes(ForPartialVectors<TestStrictUnsigned>());
}

struct TestStrictInt {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const T min = LimitsMin<T>();
    const T max = LimitsMax<T>();
    const Vec<D> v0 = Zero(d);
    const Vec<D> v2 = And(Iota(d, 2), Set(d, 127));  // 0..127
    const Vec<D> vn = Sub(Neg(v2), Set(d, 1));       // -1..-128

    const Mask<D> mask_false = MaskFalse(d);
    const Mask<D> mask_true = MaskTrue(d);

    // Individual values of interest
    HWY_ENSURE_GREATER(d, 2, 1);
    HWY_ENSURE_GREATER(d, 1, 0);
    HWY_ENSURE_GREATER(d, 0, -1);
    HWY_ENSURE_GREATER(d, -1, -2);
    HWY_ENSURE_GREATER(d, max, max / 2);
    HWY_ENSURE_GREATER(d, max, 1);
    HWY_ENSURE_GREATER(d, max, 0);
    HWY_ENSURE_GREATER(d, max, -1);
    HWY_ENSURE_GREATER(d, max, min);
    HWY_ENSURE_GREATER(d, 0, min);
    HWY_ENSURE_GREATER(d, min / 2, min);

    // Also use Iota to ensure lanes are independent
    HWY_ASSERT_MASK_EQ(d, mask_true, Gt(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt(vn, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(vn, v2));

    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(vn, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(vn, vn));
  }
};

// S-SSE3 bug (#795): same upper, differing MSB in lower
struct TestStrictInt64 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const auto m0 = MaskFalse(d);
    const auto m1 = MaskTrue(d);
    HWY_ASSERT_MASK_EQ(d, m0, Lt(Set(d, 0x380000000LL), Set(d, 0x300000001LL)));
    HWY_ASSERT_MASK_EQ(d, m1, Lt(Set(d, 0xF00000000LL), Set(d, 0xF80000000LL)));
    HWY_ASSERT_MASK_EQ(d, m1, Lt(Set(d, 0xF00000000LL), Set(d, 0xF80000001LL)));
  }
};

HWY_NOINLINE void TestAllStrictInt() {
  ForSignedTypes(ForPartialVectors<TestStrictInt>());
  ForPartialVectors<TestStrictInt64>()(int64_t());
}

struct TestWeakInt {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const T min = LimitsMin<T>();
    const T max = LimitsMax<T>();
    const Vec<D> v0 = Zero(d);
    const Vec<D> v1 = Set(d, 1);
    const Vec<D> v2 = And(Iota(d, 2), Set(d, 127));  // 0..127
    const Vec<D> vn = Sub(Neg(v2), Set(d, 1));       // -1..-128

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    // Individual values of interest
    HWY_ENSURE_GREATER_OR_EQUAL(d, 2, 2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 2, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 1, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 1, 0);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 0, 0);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 0, -1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, -1, -1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, -1, -2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, -2, -2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, max);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, max / 2);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, 1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, 0);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, -1);
    HWY_ENSURE_GREATER_OR_EQUAL(d, max, min);
    HWY_ENSURE_GREATER_OR_EQUAL(d, 0, min);
    HWY_ENSURE_GREATER_OR_EQUAL(d, min / 2, min);
    HWY_ENSURE_GREATER_OR_EQUAL(d, min, min);

    // Also use Iota to ensure lanes are independent
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(vn, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Le(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ge(vn, v2));

    HWY_ASSERT_MASK_EQ(d, mask_true, Le(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(vn, vn));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(vn, vn));

    const auto v2_plus_1 = Add(v2, v1);
    HWY_ASSERT_MASK_EQ(d, Lt(v2, v2_plus_1), Le(v2, v2_plus_1));
    HWY_ASSERT_MASK_EQ(d, Gt(v2, v2_plus_1), Ge(v2, v2_plus_1));
    HWY_ASSERT_MASK_EQ(d, Lt(v2_plus_1, v2), Le(v2_plus_1, v2));
    HWY_ASSERT_MASK_EQ(d, Gt(v2_plus_1, v2), Ge(v2_plus_1, v2));

    const auto v2_minus_1 = Sub(v2, v1);
    HWY_ASSERT_MASK_EQ(d, Lt(v2, v2_minus_1), Le(v2, v2_minus_1));
    HWY_ASSERT_MASK_EQ(d, Gt(v2, v2_minus_1), Ge(v2, v2_minus_1));
    HWY_ASSERT_MASK_EQ(d, Lt(v2_minus_1, v2), Le(v2_minus_1, v2));
    HWY_ASSERT_MASK_EQ(d, Gt(v2_minus_1, v2), Ge(v2_minus_1, v2));
  }
};

HWY_NOINLINE void TestAllWeakInt() {
  ForSignedTypes(ForPartialVectors<TestWeakInt>());
}

struct TestStrictFloat {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const T huge_pos = ConvertScalarTo<T>(sizeof(T) >= 4 ? 1E36 : 1E4);
    const T huge_neg = -huge_pos;
    const Vec<D> v0 = Zero(d);
    const Vec<D> v2 = Iota(d, 2);
    const Vec<D> vn = Neg(v2);

    const Mask<D> mask_false = MaskFalse(d);
    const Mask<D> mask_true = MaskTrue(d);

    // Individual values of interest
    HWY_ENSURE_GREATER(d, 2, 1);
    HWY_ENSURE_GREATER(d, 1, 0);
    HWY_ENSURE_GREATER(d, 0, -1);
    HWY_ENSURE_GREATER(d, -1, -2);
    HWY_ENSURE_GREATER(d, huge_pos, 1);
    HWY_ENSURE_GREATER(d, huge_pos, 0);
    HWY_ENSURE_GREATER(d, huge_pos, -1);
    HWY_ENSURE_GREATER(d, huge_pos, huge_neg);
    HWY_ENSURE_GREATER(d, 0, huge_neg);

    // Also use Iota to ensure lanes are independent
    HWY_ASSERT_MASK_EQ(d, mask_true, Gt(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt(vn, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(vn, v2));

    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt(vn, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v0, v0));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_false, Gt(vn, vn));
  }
};

HWY_NOINLINE void TestAllStrictFloat() {
  ForFloatTypes(ForPartialVectors<TestStrictFloat>());
}

struct TestWeakFloat {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const Vec<D> v2 = Iota(d, 2);
    const Vec<D> vn = Iota(d, -ConvertScalarTo<T>(Lanes(d)));

    const Mask<D> mask_false = MaskFalse(d);
    const Mask<D> mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v2, v2));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(vn, vn));

    HWY_ASSERT_MASK_EQ(d, mask_true, Ge(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_true, Le(vn, v2));

    HWY_ASSERT_MASK_EQ(d, mask_false, Le(v2, vn));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ge(vn, v2));
  }
};

HWY_NOINLINE void TestAllWeakFloat() {
  ForFloatTypes(ForPartialVectors<TestWeakFloat>());
}

struct TestIsNegative {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const RebindToSigned<decltype(d)> di;
    const RebindToUnsigned<decltype(d)> du;
    using TU = TFromD<decltype(du)>;

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_false, IsNegative(Zero(d)));
    HWY_ASSERT_MASK_EQ(d, mask_true,
                       IsNegative(Set(d, ConvertScalarTo<T>(-1))));

    const auto vsignbit = SignBit(d);
    const auto vp = AndNot(vsignbit, Iota(d, 1));
    const auto vn = Or(vp, vsignbit);

    HWY_ASSERT_MASK_EQ(d, mask_false, IsNegative(vp));
    HWY_ASSERT_MASK_EQ(d, mask_true, IsNegative(vn));

    const auto s1 = BitCast(d, ShiftLeft<sizeof(TU) * 8 - 1>(Iota(du, 1)));

    const auto x1 = Xor3(vp, s1, BitCast(d, Set(du, TU{0x71})));
    const auto x2 = Xor(x1, vsignbit);

    HWY_ASSERT_MASK_EQ(d, mask_false, And(IsNegative(x1), IsNegative(x2)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Or(IsNegative(x1), IsNegative(x2)));

    const auto expected_1 =
        RebindMask(d, MaskFromVec(BroadcastSignBit(BitCast(di, x1))));
    const auto expected_2 =
        RebindMask(d, MaskFromVec(BroadcastSignBit(BitCast(di, x2))));

    HWY_ASSERT_MASK_EQ(d, expected_1, IsNegative(x1));
    HWY_ASSERT_MASK_EQ(d, expected_2, IsNegative(x2));
  }
};

HWY_NOINLINE void TestAllIsNegative() {
  ForFloatTypes(ForPartialVectors<TestIsNegative>());
  ForSignedTypes(ForPartialVectors<TestIsNegative>());
}

template <class D>
static HWY_NOINLINE Vec<D> Make128(D d, uint64_t hi, uint64_t lo) {
  alignas(16) uint64_t in[2];
  in[0] = lo;
  in[1] = hi;
  return LoadDup128(d, in);
}

struct TestLt128 {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using V = Vec<D>;
    const V v00 = Zero(d);
    const V v01 = Make128(d, 0, 1);
    const V v10 = Make128(d, 1, 0);
    const V v11 = Add(v01, v10);

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v10, v10));

    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v00, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v01, v11));

    // Reversed order
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v01, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, v11, v01));

    // Also check 128-bit blocks are independent
    const V iota = Iota(d, 1);
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, Add(iota, v10), iota));

    // Max value
    const V vm = Make128(d, LimitsMax<T>(), LimitsMax<T>());
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, vm, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128(d, v11, vm));
  }
};

HWY_NOINLINE void TestAllLt128() { ForGEVectors<128, TestLt128>()(uint64_t()); }

struct TestLt128Upper {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using V = Vec<D>;
    const V v00 = Zero(d);
    const V v01 = Make128(d, 0, 1);
    const V v10 = Make128(d, 1, 0);
    const V v11 = Add(v01, v10);

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v10, v10));

    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v00, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v01, v11));

    // Reversed order
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v01, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, v11, v01));

    // Also check 128-bit blocks are independent
    const V iota = Iota(d, 1);
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, Add(iota, v10), iota));

    // Max value
    const V vm = Make128(d, LimitsMax<T>(), LimitsMax<T>());
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, vm, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Lt128Upper(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Lt128Upper(d, v11, vm));
  }
};

HWY_NOINLINE void TestAllLt128Upper() {
  ForGEVectors<128, TestLt128Upper>()(uint64_t());
}

struct TestEq128 {  // Also Ne128
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using V = Vec<D>;
    const V v00 = Zero(d);
    const V v01 = Make128(d, 0, 1);
    const V v10 = Make128(d, 1, 0);
    const V v11 = Add(v01, v10);

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128(d, v10, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128(d, v10, v10));

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v00, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v01, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v00, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v01, v11));

    // Reversed order
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v01, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v11, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v01, v00));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v11, v01));

    // Also check 128-bit blocks are independent
    const V iota = Iota(d, 1);
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, Add(iota, v10), iota));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, Add(iota, v10), iota));

    // Max value
    const V vm = Make128(d, LimitsMax<T>(), LimitsMax<T>());
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128(d, vm, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128(d, vm, vm));

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128(d, v11, vm));

    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128(d, v11, vm));
  }
};

HWY_NOINLINE void TestAllEq128() { ForGEVectors<128, TestEq128>()(uint64_t()); }

struct TestEq128Upper {  // Also Ne128Upper
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using V = Vec<D>;
    const V v00 = Zero(d);
    const V v01 = Make128(d, 0, 1);
    const V v10 = Make128(d, 1, 0);
    const V v11 = Add(v01, v10);

    const auto mask_false = MaskFalse(d);
    const auto mask_true = MaskTrue(d);

    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, v10, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, v00, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, v01, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, v10, v10));

    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, v00, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, v00, v01));

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v01, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v01, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v01, v11));

    // Reversed order
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, v01, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, v01, v00));

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v11, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v10, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v11, v01));

    // Also check 128-bit blocks are independent
    const V iota = Iota(d, 1);
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, iota, Add(iota, v01)));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, iota, Add(iota, v10)));
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, Add(iota, v01), iota));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, Add(iota, v10), iota));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, Add(iota, v10), iota));

    // Max value
    const V vm = Make128(d, LimitsMax<T>(), LimitsMax<T>());
    HWY_ASSERT_MASK_EQ(d, mask_true, Eq128Upper(d, vm, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Ne128Upper(d, vm, vm));

    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_false, Eq128Upper(d, v11, vm));

    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, vm, v00));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, vm, v01));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, vm, v10));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, vm, v11));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v00, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v01, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v10, vm));
    HWY_ASSERT_MASK_EQ(d, mask_true, Ne128Upper(d, v11, vm));
  }
};

HWY_NOINLINE void TestAllEq128Upper() {
  ForGEVectors<128, TestEq128Upper>()(uint64_t());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCompareTest);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllEquality);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllStrictUnsigned);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllStrictInt);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllStrictFloat);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllWeakUnsigned);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllWeakInt);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllWeakFloat);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllIsNegative);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllLt128);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllLt128Upper);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllEq128);
HWY_EXPORT_AND_TEST_P(HwyCompareTest, TestAllEq128Upper);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
