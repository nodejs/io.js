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
#define HWY_TARGET_INCLUDE "tests/rotate_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestRotateLeft {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;

    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    constexpr size_t kBits = sizeof(T) * 8;
    const Vec<D> mask_shift = Set(d, static_cast<T>(kBits - 1));
    // Cover as many bit positions as possible to test shifting out
    const Vec<D> values =
        Shl(Set(d, static_cast<T>(1)), And(Iota(d, 0), mask_shift));
    const Vec<D> values2 = Xor(values, SignBit(d));

    // Rotate by 0
    HWY_ASSERT_VEC_EQ(d, values, RotateLeft<0>(values));
    HWY_ASSERT_VEC_EQ(d, values2, RotateLeft<0>(values2));

    // Rotate by 1
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) << 1) |
                             (static_cast<TU>(expected[i]) >> (kBits - 1)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<1>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(expected[i] ^ static_cast<T>(1));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<1>(values2));

    // Rotate by half
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) << (kBits / 2)) |
                             (static_cast<TU>(expected[i]) >> (kBits / 2)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<kBits / 2>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(
          expected[i] ^ (static_cast<T>(1) << ((kBits / 2) - 1)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<kBits / 2>(values2));

    // Rotate by max
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) << (kBits - 1)) |
                             (static_cast<TU>(expected[i]) >> 1));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<kBits - 1>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>(expected[i] ^ (static_cast<T>(1) << (kBits - 2)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateLeft<kBits - 1>(values2));
  }
};

HWY_NOINLINE void TestAllRotateLeft() {
  ForIntegerTypes(ForPartialVectors<TestRotateLeft>());
}

struct TestRotateRight {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;

    const size_t N = Lanes(d);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    constexpr size_t kBits = sizeof(T) * 8;
    const Vec<D> mask_shift = Set(d, static_cast<T>(kBits - 1));
    // Cover as many bit positions as possible to test shifting out
    const Vec<D> values =
        Shl(Set(d, static_cast<T>(1)), And(Iota(d, 0), mask_shift));
    const Vec<D> values2 = Xor(values, SignBit(d));

    // Rotate by 0
    HWY_ASSERT_VEC_EQ(d, values, RotateRight<0>(values));
    HWY_ASSERT_VEC_EQ(d, values2, RotateRight<0>(values2));

    // Rotate by 1
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) >> 1) |
                             (static_cast<TU>(expected[i]) << (kBits - 1)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<1>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>(expected[i] ^ (static_cast<T>(1) << (kBits - 2)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<1>(values2));

    // Rotate by half
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) >> (kBits / 2)) |
                             (static_cast<TU>(expected[i]) << (kBits / 2)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<kBits / 2>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(
          expected[i] ^ (static_cast<T>(1) << ((kBits / 2) - 1)));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<kBits / 2>(values2));

    // Rotate by max
    Store(values, d, expected.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] =
          ConvertScalarTo<T>((static_cast<TU>(expected[i]) >> (kBits - 1)) |
                             (static_cast<TU>(expected[i]) << 1));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<kBits - 1>(values));

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>(expected[i] ^ static_cast<T>(1));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), RotateRight<kBits - 1>(values2));
  }
};

HWY_NOINLINE void TestAllRotateRight() {
  ForIntegerTypes(ForPartialVectors<TestRotateRight>());
}

struct TestVariableRotations {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    using TU = MakeUnsigned<T>;

    constexpr TU kBits1 = static_cast<TU>(0x7C29085C41482973ULL);
    constexpr TU kBits2 = static_cast<TU>(0xD3C8835FBD1A89BAULL);

    const auto viota0 = Iota(d, 0);
    const auto va = Xor(Set(d, static_cast<T>(kBits1)), viota0);
    const auto vb = Xor(Set(d, static_cast<T>(kBits2)), viota0);

    const size_t N = Lanes(d);
    auto expected1 = AllocateAligned<T>(N);
    auto expected2 = AllocateAligned<T>(N);
    auto expected3 = AllocateAligned<T>(N);
    auto expected4 = AllocateAligned<T>(N);
    HWY_ASSERT(expected1 && expected2 && expected3 && expected4);

    constexpr size_t kBits = sizeof(T) * 8;

    auto vrotate_amt1 = viota0;
    auto vrotate_amt2 = Sub(Set(d, static_cast<T>(kBits)), viota0);
    auto vrotate_amt_incr = Set(d, static_cast<T>(N));

    const RebindToSigned<decltype(d)> di;

    for (size_t i = 0; i < kBits; i += N) {
      for (size_t j = 0; j < N; j++) {
        const size_t shift_amt_1 = (i + j) & (kBits - 1);
        const size_t shift_amt_2 = (size_t{0} - shift_amt_1) & (kBits - 1);

        const TU val_a = static_cast<TU>(kBits1 ^ j);
        const TU val_b = static_cast<TU>(kBits2 ^ j);

        expected1[j] =
            static_cast<T>((val_a << shift_amt_1) | (val_a >> shift_amt_2));
        expected2[j] =
            static_cast<T>((val_a >> shift_amt_1) | (val_a << shift_amt_2));
        expected3[j] =
            static_cast<T>((val_b << shift_amt_1) | (val_b >> shift_amt_2));
        expected4[j] =
            static_cast<T>((val_b >> shift_amt_1) | (val_b << shift_amt_2));
      }

      const auto vrotate_amt3 = BitCast(d, Neg(BitCast(di, vrotate_amt1)));
      const auto vrotate_amt4 = BitCast(d, Neg(BitCast(di, vrotate_amt2)));

      HWY_ASSERT_VEC_EQ(d, expected1.get(), Rol(va, vrotate_amt1));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), Ror(va, vrotate_amt1));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), Rol(vb, vrotate_amt1));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), Ror(vb, vrotate_amt1));

      HWY_ASSERT_VEC_EQ(d, expected1.get(), Ror(va, vrotate_amt2));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), Rol(va, vrotate_amt2));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), Ror(vb, vrotate_amt2));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), Rol(vb, vrotate_amt2));

      HWY_ASSERT_VEC_EQ(d, expected1.get(), Ror(va, vrotate_amt3));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), Rol(va, vrotate_amt3));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), Ror(vb, vrotate_amt3));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), Rol(vb, vrotate_amt3));

      HWY_ASSERT_VEC_EQ(d, expected1.get(), Rol(va, vrotate_amt4));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), Ror(va, vrotate_amt4));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), Rol(vb, vrotate_amt4));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), Ror(vb, vrotate_amt4));

      vrotate_amt1 = Add(vrotate_amt1, vrotate_amt_incr);
      vrotate_amt2 = Sub(vrotate_amt2, vrotate_amt_incr);
    }

    for (int i = 0; i < static_cast<int>(kBits); ++i) {
      for (size_t j = 0; j < N; j++) {
        const int shift_amt_2 =
            static_cast<int>(static_cast<size_t>(-i) & (kBits - 1));

        const TU val_a = static_cast<TU>(kBits1 ^ j);
        const TU val_b = static_cast<TU>(kBits2 ^ j);

        expected1[j] = static_cast<T>((val_a << i) | (val_a >> shift_amt_2));
        expected2[j] = static_cast<T>((val_a >> i) | (val_a << shift_amt_2));
        expected3[j] = static_cast<T>((val_b << i) | (val_b >> shift_amt_2));
        expected4[j] = static_cast<T>((val_b >> i) | (val_b << shift_amt_2));
      }

      HWY_ASSERT_VEC_EQ(d, expected1.get(), RotateLeftSame(va, i));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), RotateRightSame(va, i));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), RotateLeftSame(vb, i));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), RotateRightSame(vb, i));

      HWY_ASSERT_VEC_EQ(d, expected1.get(), RotateRightSame(va, -i));
      HWY_ASSERT_VEC_EQ(d, expected2.get(), RotateLeftSame(va, -i));
      HWY_ASSERT_VEC_EQ(d, expected3.get(), RotateRightSame(vb, -i));
      HWY_ASSERT_VEC_EQ(d, expected4.get(), RotateLeftSame(vb, -i));

      HWY_ASSERT_VEC_EQ(d, expected1.get(),
                        RotateRightSame(va, static_cast<int>(kBits) - i));
      HWY_ASSERT_VEC_EQ(d, expected2.get(),
                        RotateLeftSame(va, static_cast<int>(kBits) - i));
      HWY_ASSERT_VEC_EQ(d, expected3.get(),
                        RotateRightSame(vb, static_cast<int>(kBits) - i));
      HWY_ASSERT_VEC_EQ(d, expected4.get(),
                        RotateLeftSame(vb, static_cast<int>(kBits) - i));
    }
  }
};

HWY_NOINLINE void TestAllVariableRotations() {
  ForIntegerTypes(ForPartialVectors<TestVariableRotations>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyRotateTest);
HWY_EXPORT_AND_TEST_P(HwyRotateTest, TestAllRotateLeft);
HWY_EXPORT_AND_TEST_P(HwyRotateTest, TestAllRotateRight);
HWY_EXPORT_AND_TEST_P(HwyRotateTest, TestAllVariableRotations);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
