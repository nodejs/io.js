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
#define HWY_TARGET_INCLUDE "tests/count_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/tests/test_util-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

struct TestPopulationCount {
  template <class T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    size_t N = Lanes(d);
    auto data = AllocateAligned<T>(N);
    auto popcnt = AllocateAligned<T>(N);
    HWY_ASSERT(data && popcnt);
    for (size_t i = 0; i < AdjustedReps(1 << 18) / N; i++) {
      for (size_t j = 0; j < N; j++) {
        data[j] = static_cast<T>(rng());
        popcnt[j] = static_cast<T>(PopCount(data[j]));
      }
      HWY_ASSERT_VEC_EQ(d, popcnt.get(), PopulationCount(Load(d, data.get())));
    }
  }
};

HWY_NOINLINE void TestAllPopulationCount() {
  ForUnsignedTypes(ForPartialVectors<TestPopulationCount>());
}

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T), HWY_IF_T_SIZE(T, 4)>
static HWY_INLINE T LeadingZeroCountOfValue(T val) {
  const uint32_t u32_val = static_cast<uint32_t>(val);
  return static_cast<T>(u32_val ? Num0BitsAboveMS1Bit_Nonzero32(u32_val) : 32);
}
template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T), HWY_IF_T_SIZE(T, 8)>
static HWY_INLINE T LeadingZeroCountOfValue(T val) {
  const uint64_t u64_val = static_cast<uint64_t>(val);
  return static_cast<T>(u64_val ? Num0BitsAboveMS1Bit_Nonzero64(u64_val) : 64);
}
template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
static HWY_INLINE T LeadingZeroCountOfValue(T val) {
  using TU = MakeUnsigned<T>;
  constexpr uint32_t kNumOfExtraLeadingZeros{32 - (sizeof(T) * 8)};
  return static_cast<T>(
      LeadingZeroCountOfValue(static_cast<uint32_t>(static_cast<TU>(val))) -
      kNumOfExtraLeadingZeros);
}

struct TestLeadingZeroCount {
  template <class T, class D>
  HWY_ATTR_NO_MSAN HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TU = MakeUnsigned<T>;
    const RebindToUnsigned<decltype(d)> du;
    size_t N = Lanes(d);
    auto data = AllocateAligned<T>(N);
    auto lzcnt = AllocateAligned<T>(N);
    HWY_ASSERT(data && lzcnt);

    constexpr T kNumOfBitsInT = static_cast<T>(sizeof(T) * 8);
    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = kNumOfBitsInT;
    }
    HWY_ASSERT_VEC_EQ(d, lzcnt.get(), LeadingZeroCount(Zero(d)));

    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = static_cast<T>(kNumOfBitsInT - 1);
    }
    HWY_ASSERT_VEC_EQ(d, lzcnt.get(),
                      LeadingZeroCount(Set(d, static_cast<T>(1))));

    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = static_cast<T>(kNumOfBitsInT - 2);
    }
    HWY_ASSERT_VEC_EQ(d, lzcnt.get(),
                      LeadingZeroCount(Set(d, static_cast<T>(2))));

    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = static_cast<T>(0);
    }
    HWY_ASSERT_VEC_EQ(
        d, lzcnt.get(),
        LeadingZeroCount(BitCast(d, Set(du, TU{1} << (kNumOfBitsInT - 1)))));

    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = static_cast<T>(1);
    }
    HWY_ASSERT_VEC_EQ(
        d, lzcnt.get(),
        LeadingZeroCount(Set(d, static_cast<T>(1) << (kNumOfBitsInT - 2))));

    for (size_t j = 0; j < N; j++) {
      lzcnt[j] = static_cast<T>(kNumOfBitsInT - 5);
    }
    HWY_ASSERT_VEC_EQ(d, lzcnt.get(),
                      LeadingZeroCount(Set(d, static_cast<T>(0x1D))));

    for (size_t i = 0; i < AdjustedReps(1000); i++) {
      for (size_t j = 0; j < N; j++) {
        data[j] = static_cast<T>(rng());
        lzcnt[j] = LeadingZeroCountOfValue(data[j]);
      }
      HWY_ASSERT_VEC_EQ(d, lzcnt.get(), LeadingZeroCount(Load(d, data.get())));
    }
  }
};

HWY_NOINLINE void TestAllLeadingZeroCount() {
  ForIntegerTypes(ForPartialVectors<TestLeadingZeroCount>());
}

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
static HWY_INLINE T TrailingZeroCountOfValue(T val) {
  using TU = MakeUnsigned<T>;
  constexpr size_t kNumOfBitsInT = sizeof(T) * 8;
  const uint32_t u32_val = static_cast<uint32_t>(static_cast<TU>(val));
  return static_cast<T>(u32_val ? Num0BitsBelowLS1Bit_Nonzero32(u32_val)
                                : kNumOfBitsInT);
}
template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T), HWY_IF_T_SIZE(T, 8)>
static HWY_INLINE T TrailingZeroCountOfValue(T val) {
  const uint64_t u64_val = static_cast<uint64_t>(val);
  return static_cast<T>(u64_val ? Num0BitsBelowLS1Bit_Nonzero64(u64_val) : 64);
}

struct TestTrailingZeroCount {
  template <class T, class D>
  HWY_ATTR_NO_MSAN HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TU = MakeUnsigned<T>;
    const RebindToUnsigned<decltype(d)> du;

    size_t N = Lanes(d);
    auto data = AllocateAligned<T>(N);
    auto tzcnt = AllocateAligned<T>(N);
    HWY_ASSERT(data && tzcnt);

    constexpr T kNumOfBitsInT = static_cast<T>(sizeof(T) * 8);
    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = kNumOfBitsInT;
    }
    HWY_ASSERT_VEC_EQ(d, tzcnt.get(), TrailingZeroCount(Zero(d)));

    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = static_cast<T>(0);
    }
    HWY_ASSERT_VEC_EQ(d, tzcnt.get(),
                      TrailingZeroCount(Set(d, static_cast<T>(1))));

    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = static_cast<T>(1);
    }
    HWY_ASSERT_VEC_EQ(d, tzcnt.get(),
                      TrailingZeroCount(Set(d, static_cast<T>(2))));

    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = static_cast<T>(kNumOfBitsInT - 1);
    }
    HWY_ASSERT_VEC_EQ(
        d, tzcnt.get(),
        TrailingZeroCount(BitCast(d, Set(du, TU{1} << (kNumOfBitsInT - 1)))));

    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = static_cast<T>(kNumOfBitsInT - 2);
    }
    HWY_ASSERT_VEC_EQ(
        d, tzcnt.get(),
        TrailingZeroCount(Set(d, static_cast<T>(1) << (kNumOfBitsInT - 2))));

    for (size_t j = 0; j < N; j++) {
      tzcnt[j] = static_cast<T>(3);
    }
    HWY_ASSERT_VEC_EQ(d, tzcnt.get(),
                      TrailingZeroCount(Set(d, static_cast<T>(0x68))));

    for (size_t i = 0; i < AdjustedReps(1000); i++) {
      for (size_t j = 0; j < N; j++) {
        data[j] = static_cast<T>(rng());
        tzcnt[j] = TrailingZeroCountOfValue(data[j]);
      }
      HWY_ASSERT_VEC_EQ(d, tzcnt.get(), TrailingZeroCount(Load(d, data.get())));
    }
  }
};

HWY_NOINLINE void TestAllTrailingZeroCount() {
  ForIntegerTypes(ForPartialVectors<TestTrailingZeroCount>());
}

class TestHighestSetBitIndex {
 private:
  template <class V>
  static HWY_INLINE V NormalizedHighestSetBitIndex(V v) {
    const DFromV<decltype(v)> d;
    const RebindToSigned<decltype(d)> di;
    const auto hsb_idx = BitCast(di, HighestSetBitIndex(v));
    return BitCast(d, Or(BroadcastSignBit(hsb_idx), hsb_idx));
  }

 public:
  template <class T, class D>
  HWY_ATTR_NO_MSAN HWY_NOINLINE void operator()(T /*unused*/, D d) {
    RandomState rng;
    using TU = MakeUnsigned<T>;
    const RebindToUnsigned<decltype(d)> du;

    size_t N = Lanes(d);
    auto data = AllocateAligned<T>(N);
    auto hsb_index = AllocateAligned<T>(N);
    HWY_ASSERT(data && hsb_index);

    constexpr T kNumOfBitsInT = static_cast<T>(sizeof(T) * 8);
    constexpr T kMsbIdx = static_cast<T>(kNumOfBitsInT - 1);

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(-1);
    }
    HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                      NormalizedHighestSetBitIndex(Zero(d)));

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(0);
    }
    HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                      NormalizedHighestSetBitIndex(Set(d, static_cast<T>(1))));

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(1);
    }
    HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                      NormalizedHighestSetBitIndex(Set(d, static_cast<T>(3))));

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(kNumOfBitsInT - 1);
    }
    HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                      NormalizedHighestSetBitIndex(
                          BitCast(d, Set(du, TU{1} << (kNumOfBitsInT - 1)))));

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(kNumOfBitsInT - 2);
    }
    HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                      NormalizedHighestSetBitIndex(
                          Set(d, static_cast<T>(1) << (kNumOfBitsInT - 2))));

    for (size_t j = 0; j < N; j++) {
      hsb_index[j] = static_cast<T>(5);
    }
    HWY_ASSERT_VEC_EQ(
        d, hsb_index.get(),
        NormalizedHighestSetBitIndex(Set(d, static_cast<T>(0x2B))));

    for (size_t i = 0; i < AdjustedReps(1000); i++) {
      for (size_t j = 0; j < N; j++) {
        data[j] = static_cast<T>(rng());
        hsb_index[j] =
            static_cast<T>(kMsbIdx - LeadingZeroCountOfValue(data[j]));
      }
      HWY_ASSERT_VEC_EQ(d, hsb_index.get(),
                        NormalizedHighestSetBitIndex(Load(d, data.get())));
    }
  }
};

HWY_NOINLINE void TestAllHighestSetBitIndex() {
  ForIntegerTypes(ForPartialVectors<TestHighestSetBitIndex>());
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(HwyCountTest);
HWY_EXPORT_AND_TEST_P(HwyCountTest, TestAllPopulationCount);
HWY_EXPORT_AND_TEST_P(HwyCountTest, TestAllLeadingZeroCount);
HWY_EXPORT_AND_TEST_P(HwyCountTest, TestAllTrailingZeroCount);
HWY_EXPORT_AND_TEST_P(HwyCountTest, TestAllHighestSetBitIndex);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
