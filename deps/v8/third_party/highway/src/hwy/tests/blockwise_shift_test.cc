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
#define HWY_TARGET_INCLUDE "tests/blockwise_shift_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestShiftBytes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // Scalar does not define Shift*Bytes.
#if HWY_TARGET != HWY_SCALAR || HWY_IDE
    const Repartition<uint8_t, D> du8;
    const size_t N8 = Lanes(du8);
    const size_t N = Lanes(d);

    // Zero remains zero
    const auto v0 = Zero(d);
    HWY_ASSERT_VEC_EQ(d, v0, ShiftLeftBytes<1>(v0));
    HWY_ASSERT_VEC_EQ(d, v0, ShiftLeftBytes<1>(d, v0));
    HWY_ASSERT_VEC_EQ(d, v0, ShiftRightBytes<1>(d, v0));

    auto bytes = AllocateAligned<uint8_t>(N8);
    auto in = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(bytes && in && expected);

    // Zero after shifting out the high/low byte
    ZeroBytes(bytes.get(), N8);
    bytes[N8 - 1] = 0x7F;
    const auto vhi = BitCast(d, Load(du8, bytes.get()));
    bytes[N8 - 1] = 0;
    bytes[0] = 0x7F;
    const auto vlo = BitCast(d, Load(du8, bytes.get()));
    HWY_ASSERT_VEC_EQ(d, v0, ShiftLeftBytes<1>(vhi));
    HWY_ASSERT_VEC_EQ(d, v0, ShiftLeftBytes<1>(d, vhi));
    HWY_ASSERT_VEC_EQ(d, v0, ShiftRightBytes<1>(d, vlo));

    // Check expected result with Iota
    const uint8_t* in_bytes = reinterpret_cast<const uint8_t*>(in.get());
    const auto v = BitCast(d, Iota(du8, 1));
    Store(v, d, in.get());

    uint8_t* expected_bytes = reinterpret_cast<uint8_t*>(expected.get());

    const size_t block_size = HWY_MIN(N8, 16);
    for (size_t block = 0; block < N8; block += block_size) {
      expected_bytes[block] = 0;
      CopyBytes(in_bytes + block, expected_bytes + block + 1, block_size - 1);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftLeftBytes<1>(v));
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftLeftBytes<1>(d, v));

    for (size_t block = 0; block < N8; block += block_size) {
      CopyBytes(in_bytes + block + 1, expected_bytes + block, block_size - 1);
      expected_bytes[block + block_size - 1] = 0;
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftRightBytes<1>(d, v));
#else
    (void)d;
#endif  // #if HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllShiftBytes() {
  ForIntegerTypes(ForPartialVectors<TestShiftBytes>());
}

struct TestShiftLeftLanes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // Scalar does not define Shift*Lanes.
#if HWY_TARGET != HWY_SCALAR || HWY_IDE
    const auto v = Iota(d, 1);
    const size_t N = Lanes(d);
    if (N == 1) return;
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    HWY_ASSERT_VEC_EQ(d, v, ShiftLeftLanes<0>(v));
    HWY_ASSERT_VEC_EQ(d, v, ShiftLeftLanes<0>(d, v));

    constexpr size_t kLanesPerBlock = 16 / sizeof(T);

    for (size_t i = 0; i < N; ++i) {
      expected[i] = ConvertScalarTo<T>((i % kLanesPerBlock) == 0 ? 0 : i);
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftLeftLanes<1>(v));
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftLeftLanes<1>(d, v));
#else
    (void)d;
#endif  // #if HWY_TARGET != HWY_SCALAR
  }
};

struct TestShiftRightLanes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // Scalar does not define Shift*Lanes.
#if HWY_TARGET != HWY_SCALAR || HWY_IDE
    const auto v = Iota(d, 1);
    const size_t N = Lanes(d);
    if (N == 1) return;
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(expected);

    HWY_ASSERT_VEC_EQ(d, v, ShiftRightLanes<0>(d, v));

    constexpr size_t kLanesPerBlock = 16 / sizeof(T);

    for (size_t i = 0; i < N; ++i) {
      const size_t mod = i % kLanesPerBlock;
      expected[i] = ConvertScalarTo<T>(
          ((mod == kLanesPerBlock - 1) || (i >= N - 1)) ? 0 : (2 + i));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ShiftRightLanes<1>(d, v));
#else
    (void)d;
#endif  // #if HWY_TARGET != HWY_SCALAR
  }
};

HWY_NOINLINE void TestAllShiftLeftLanes() {
  ForAllTypes(ForPartialVectors<TestShiftLeftLanes>());
}

HWY_NOINLINE void TestAllShiftRightLanes() {
  ForAllTypes(ForPartialVectors<TestShiftRightLanes>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyBlockwiseShiftTest);
HWY_EXPORT_AND_TEST_P(HwyBlockwiseShiftTest, TestAllShiftBytes);
HWY_EXPORT_AND_TEST_P(HwyBlockwiseShiftTest, TestAllShiftLeftLanes);
HWY_EXPORT_AND_TEST_P(HwyBlockwiseShiftTest, TestAllShiftRightLanes);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
