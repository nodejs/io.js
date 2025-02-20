// Copyright 2022 Google LLC
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
#define HWY_TARGET_INCLUDE "tests/reverse_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestReverse {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const RebindToUnsigned<D> du;  // Iota does not support float16_t.
    const auto v = BitCast(d, Iota(du, 1));
    auto expected = AllocateAligned<T>(N);
    auto copy = AllocateAligned<T>(N);
    HWY_ASSERT(expected && copy);

    // Can't set float16_t value directly, need to permute in memory.
    Store(v, d, copy.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] = copy[N - 1 - i];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), Reverse(d, v));
  }
};

struct TestReverse2 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const RebindToUnsigned<D> du;  // Iota does not support float16_t.
    const auto v = BitCast(d, Iota(du, 1));
    auto expected = AllocateAligned<T>(N);
    auto copy = AllocateAligned<T>(N);
    HWY_ASSERT(expected && copy);
    if (N == 1) {
      Store(v, d, expected.get());
      HWY_ASSERT_VEC_EQ(d, expected.get(), Reverse2(d, v));
      return;
    }

    // Can't set float16_t value directly, need to permute in memory.
    Store(v, d, copy.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] = copy[i ^ 1];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), Reverse2(d, v));
  }
};

struct TestReverse4 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const RebindToUnsigned<D> du;  // Iota does not support float16_t.
    const auto v = BitCast(d, Iota(du, 1));
    auto expected = AllocateAligned<T>(N);
    auto copy = AllocateAligned<T>(N);
    HWY_ASSERT(expected && copy);

    // Can't set float16_t value directly, need to permute in memory.
    Store(v, d, copy.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] = copy[i ^ 3];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), Reverse4(d, v));
  }
};

struct TestReverse8 {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const RebindToUnsigned<D> du;  // Iota does not support float16_t.
    const auto v = BitCast(d, Iota(du, 1));
    auto expected = AllocateAligned<T>(N);
    auto copy = AllocateAligned<T>(N);
    HWY_ASSERT(expected && copy);

    // Can't set float16_t value directly, need to permute in memory.
    Store(v, d, copy.get());
    for (size_t i = 0; i < N; ++i) {
      expected[i] = copy[i ^ 7];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), Reverse8(d, v));
  }
};

static HWY_INLINE uint8_t ReverseBytesOfValue(uint8_t val) { return val; }

static HWY_INLINE uint16_t ReverseBytesOfValue(uint16_t val) {
  const uint32_t u32_val = val;
  return static_cast<uint16_t>(((u32_val << 8) & 0xFF00u) |
                               ((u32_val >> 8) & 0x00FFu));
}

static HWY_INLINE uint32_t ReverseBytesOfValue(uint32_t val) {
  return static_cast<uint32_t>(
      ((val << 24) & 0xFF000000u) | ((val << 8) & 0x00FF0000u) |
      ((val >> 8) & 0x0000FF00u) | ((val >> 24) & 0x000000FFu));
}

static HWY_INLINE uint64_t ReverseBytesOfValue(uint64_t val) {
  return static_cast<uint64_t>(
      ((val << 56) & 0xFF00000000000000u) |
      ((val << 40) & 0x00FF000000000000u) |
      ((val << 24) & 0x0000FF0000000000u) | ((val << 8) & 0x000000FF00000000u) |
      ((val >> 8) & 0x00000000FF000000u) | ((val >> 24) & 0x0000000000FF0000u) |
      ((val >> 40) & 0x000000000000FF00u) |
      ((val >> 56) & 0x00000000000000FFu));
}

template <class T, HWY_IF_SIGNED(T)>
static HWY_INLINE T ReverseBytesOfValue(T val) {
  using TU = MakeUnsigned<T>;
  return static_cast<T>(ReverseBytesOfValue(static_cast<TU>(val)));
}

struct TestReverseLaneBytes {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(in && expected);

    const auto v_iota = Iota(d, 0);
    for (size_t i = 0; i < N; i++) {
      expected[i] = ReverseBytesOfValue(ConvertScalarTo<T>(i));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ReverseLaneBytes(v_iota));

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(10000); ++rep) {
      for (size_t i = 0; i < N; i++) {
        in[i] = ConvertScalarTo<T>(Random64(&rng));
        expected[i] = ReverseBytesOfValue(in[i]);
      }

      const auto v = Load(d, in.get());
      HWY_ASSERT_VEC_EQ(d, expected.get(), ReverseLaneBytes(v));
    }
  }
};

class TestReverseBits {
 private:
  template <class T>
  static HWY_INLINE T ReverseBitsOfEachByte(T val) {
    using TU = MakeUnsigned<T>;
    constexpr TU kMaxUnsignedVal{LimitsMax<TU>()};
    constexpr TU kShrMask1 =
        static_cast<TU>(0x5555555555555555u & kMaxUnsignedVal);
    constexpr TU kShrMask2 =
        static_cast<TU>(0x3333333333333333u & kMaxUnsignedVal);
    constexpr TU kShrMask3 =
        static_cast<TU>(0x0F0F0F0F0F0F0F0Fu & kMaxUnsignedVal);

    constexpr TU kShlMask1 = static_cast<TU>(~kShrMask1);
    constexpr TU kShlMask2 = static_cast<TU>(~kShrMask2);
    constexpr TU kShlMask3 = static_cast<TU>(~kShrMask3);

    TU result = static_cast<TU>(val);
    result = static_cast<TU>(((result << 1) & kShlMask1) |
                             ((result >> 1) & kShrMask1));
    result = static_cast<TU>(((result << 2) & kShlMask2) |
                             ((result >> 2) & kShrMask2));
    result = static_cast<TU>(((result << 4) & kShlMask3) |
                             ((result >> 4) & kShrMask3));
    return static_cast<T>(result);
  }

  template <class T>
  static HWY_INLINE T ReverseBitsOfValue(T val) {
    return ReverseBytesOfValue(ReverseBitsOfEachByte(val));
  }

 public:
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    auto in = AllocateAligned<T>(N);
    auto expected = AllocateAligned<T>(N);
    HWY_ASSERT(in && expected);

    const auto v_iota = Iota(d, 0);
    for (size_t i = 0; i < N; i++) {
      expected[i] = ReverseBitsOfValue(ConvertScalarTo<T>(i));
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ReverseBits(v_iota));

    RandomState rng;
    for (size_t rep = 0; rep < AdjustedReps(10000); ++rep) {
      for (size_t i = 0; i < N; i++) {
        in[i] = ConvertScalarTo<T>(Random64(&rng));
        expected[i] = ReverseBitsOfValue(in[i]);
      }

      const auto v = Load(d, in.get());
      HWY_ASSERT_VEC_EQ(d, expected.get(), ReverseBits(v));
    }
  }
};

HWY_NOINLINE void TestAllReverse() {
  ForAllTypes(ForPartialVectors<TestReverse>());
}

HWY_NOINLINE void TestAllReverse2() {
  ForUIF64(ForGEVectors<128, TestReverse2>());
  ForUIF32(ForGEVectors<64, TestReverse2>());
  ForUIF16(ForGEVectors<32, TestReverse2>());
  ForUI8(ForGEVectors<16, TestReverse2>());
}

HWY_NOINLINE void TestAllReverse4() {
  ForUIF64(ForGEVectors<256, TestReverse4>());
  ForUIF32(ForGEVectors<128, TestReverse4>());
  ForUIF16(ForGEVectors<64, TestReverse4>());
  ForUI8(ForGEVectors<32, TestReverse4>());
}

HWY_NOINLINE void TestAllReverse8() {
  ForUIF64(ForGEVectors<512, TestReverse8>());
  ForUIF32(ForGEVectors<256, TestReverse8>());
  ForUIF16(ForGEVectors<128, TestReverse8>());
  ForUI8(ForGEVectors<64, TestReverse8>());
}

HWY_NOINLINE void TestAllReverseLaneBytes() {
  ForUI163264(ForPartialVectors<TestReverseLaneBytes>());
}

HWY_NOINLINE void TestAllReverseBits() {
  ForIntegerTypes(ForPartialVectors<TestReverseBits>());
}

struct TestReverseBlocks {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    const size_t N = Lanes(d);
    const RebindToUnsigned<D> du;  // Iota does not support float16_t.
    const auto v = BitCast(d, Iota(du, 1));
    auto expected = AllocateAligned<T>(N);
    auto copy = AllocateAligned<T>(N);
    HWY_ASSERT(expected && copy);

    constexpr size_t kLanesPerBlock = 16 / sizeof(T);
    const size_t num_blocks = N / kLanesPerBlock;
    HWY_ASSERT(num_blocks != 0);

    // Can't set float16_t value directly, need to permute in memory.
    Store(v, d, copy.get());
    for (size_t i = 0; i < N; ++i) {
      const size_t idx_block = i / kLanesPerBlock;
      const size_t base = (num_blocks - 1 - idx_block) * kLanesPerBlock;
      expected[i] = copy[base + (i % kLanesPerBlock)];
    }
    HWY_ASSERT_VEC_EQ(d, expected.get(), ReverseBlocks(d, v));
  }
};

HWY_NOINLINE void TestAllReverseBlocks() {
  ForAllTypes(ForGEVectors<128, TestReverseBlocks>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyReverseTest);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverse);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverse2);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverse4);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverse8);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverseLaneBytes);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverseBits);
HWY_EXPORT_AND_TEST_P(HwyReverseTest, TestAllReverseBlocks);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
