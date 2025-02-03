// Copyright 2018 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/container/internal/raw_hash_set.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <ostream>
#include <random>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/prefetch.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"
#include "absl/container/internal/hash_policy_testing.h"
#include "absl/container/internal/hashtable_debug.h"
#include "absl/container/internal/hashtablez_sampler.h"
#include "absl/container/internal/test_allocator.h"
#include "absl/container/internal/test_instance_tracker.h"
#include "absl/container/node_hash_set.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

struct RawHashSetTestOnlyAccess {
  template <typename C>
  static auto GetCommon(const C& c) -> decltype(c.common()) {
    return c.common();
  }
  template <typename C>
  static auto GetSlots(const C& c) -> decltype(c.slot_array()) {
    return c.slot_array();
  }
  template <typename C>
  static size_t CountTombstones(const C& c) {
    return c.common().TombstonesCount();
  }
};

namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Ge;
using ::testing::Lt;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

// Convenience function to static cast to ctrl_t.
ctrl_t CtrlT(int i) { return static_cast<ctrl_t>(i); }

TEST(GrowthInfoTest, GetGrowthLeft) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  EXPECT_EQ(gi.GetGrowthLeft(), 5);
  gi.OverwriteFullAsDeleted();
  EXPECT_EQ(gi.GetGrowthLeft(), 5);
}

TEST(GrowthInfoTest, HasNoDeleted) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  EXPECT_TRUE(gi.HasNoDeleted());
  gi.OverwriteFullAsDeleted();
  EXPECT_FALSE(gi.HasNoDeleted());
  // After reinitialization we have no deleted slots.
  gi.InitGrowthLeftNoDeleted(5);
  EXPECT_TRUE(gi.HasNoDeleted());
}

TEST(GrowthInfoTest, HasNoDeletedAndGrowthLeft) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  EXPECT_TRUE(gi.HasNoDeletedAndGrowthLeft());
  gi.OverwriteFullAsDeleted();
  EXPECT_FALSE(gi.HasNoDeletedAndGrowthLeft());
  gi.InitGrowthLeftNoDeleted(0);
  EXPECT_FALSE(gi.HasNoDeletedAndGrowthLeft());
  gi.OverwriteFullAsDeleted();
  EXPECT_FALSE(gi.HasNoDeletedAndGrowthLeft());
  // After reinitialization we have no deleted slots.
  gi.InitGrowthLeftNoDeleted(5);
  EXPECT_TRUE(gi.HasNoDeletedAndGrowthLeft());
}

TEST(GrowthInfoTest, HasNoGrowthLeftAndNoDeleted) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(1);
  EXPECT_FALSE(gi.HasNoGrowthLeftAndNoDeleted());
  gi.OverwriteEmptyAsFull();
  EXPECT_TRUE(gi.HasNoGrowthLeftAndNoDeleted());
  gi.OverwriteFullAsDeleted();
  EXPECT_FALSE(gi.HasNoGrowthLeftAndNoDeleted());
  gi.OverwriteFullAsEmpty();
  EXPECT_FALSE(gi.HasNoGrowthLeftAndNoDeleted());
  gi.InitGrowthLeftNoDeleted(0);
  EXPECT_TRUE(gi.HasNoGrowthLeftAndNoDeleted());
  gi.OverwriteFullAsEmpty();
  EXPECT_FALSE(gi.HasNoGrowthLeftAndNoDeleted());
}

TEST(GrowthInfoTest, OverwriteFullAsEmpty) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  gi.OverwriteFullAsEmpty();
  EXPECT_EQ(gi.GetGrowthLeft(), 6);
  gi.OverwriteFullAsDeleted();
  EXPECT_EQ(gi.GetGrowthLeft(), 6);
  gi.OverwriteFullAsEmpty();
  EXPECT_EQ(gi.GetGrowthLeft(), 7);
  EXPECT_FALSE(gi.HasNoDeleted());
}

TEST(GrowthInfoTest, OverwriteEmptyAsFull) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  gi.OverwriteEmptyAsFull();
  EXPECT_EQ(gi.GetGrowthLeft(), 4);
  gi.OverwriteFullAsDeleted();
  EXPECT_EQ(gi.GetGrowthLeft(), 4);
  gi.OverwriteEmptyAsFull();
  EXPECT_EQ(gi.GetGrowthLeft(), 3);
  EXPECT_FALSE(gi.HasNoDeleted());
}

TEST(GrowthInfoTest, OverwriteControlAsFull) {
  GrowthInfo gi;
  gi.InitGrowthLeftNoDeleted(5);
  gi.OverwriteControlAsFull(ctrl_t::kEmpty);
  EXPECT_EQ(gi.GetGrowthLeft(), 4);
  gi.OverwriteControlAsFull(ctrl_t::kDeleted);
  EXPECT_EQ(gi.GetGrowthLeft(), 4);
  gi.OverwriteFullAsDeleted();
  gi.OverwriteControlAsFull(ctrl_t::kDeleted);
  // We do not count number of deleted, so the bit sticks till the next rehash.
  EXPECT_FALSE(gi.HasNoDeletedAndGrowthLeft());
  EXPECT_FALSE(gi.HasNoDeleted());
}

TEST(Util, NormalizeCapacity) {
  EXPECT_EQ(1, NormalizeCapacity(0));
  EXPECT_EQ(1, NormalizeCapacity(1));
  EXPECT_EQ(3, NormalizeCapacity(2));
  EXPECT_EQ(3, NormalizeCapacity(3));
  EXPECT_EQ(7, NormalizeCapacity(4));
  EXPECT_EQ(7, NormalizeCapacity(7));
  EXPECT_EQ(15, NormalizeCapacity(8));
  EXPECT_EQ(15, NormalizeCapacity(15));
  EXPECT_EQ(15 * 2 + 1, NormalizeCapacity(15 + 1));
  EXPECT_EQ(15 * 2 + 1, NormalizeCapacity(15 + 2));
}

TEST(Util, GrowthAndCapacity) {
  // Verify that GrowthToCapacity gives the minimum capacity that has enough
  // growth.
  for (size_t growth = 0; growth < 10000; ++growth) {
    SCOPED_TRACE(growth);
    size_t capacity = NormalizeCapacity(GrowthToLowerboundCapacity(growth));
    // The capacity is large enough for `growth`.
    EXPECT_THAT(CapacityToGrowth(capacity), Ge(growth));
    // For (capacity+1) < kWidth, growth should equal capacity.
    if (capacity + 1 < Group::kWidth) {
      EXPECT_THAT(CapacityToGrowth(capacity), Eq(capacity));
    } else {
      EXPECT_THAT(CapacityToGrowth(capacity), Lt(capacity));
    }
    if (growth != 0 && capacity > 1) {
      // There is no smaller capacity that works.
      EXPECT_THAT(CapacityToGrowth(capacity / 2), Lt(growth));
    }
  }

  for (size_t capacity = Group::kWidth - 1; capacity < 10000;
       capacity = 2 * capacity + 1) {
    SCOPED_TRACE(capacity);
    size_t growth = CapacityToGrowth(capacity);
    EXPECT_THAT(growth, Lt(capacity));
    EXPECT_LE(GrowthToLowerboundCapacity(growth), capacity);
    EXPECT_EQ(NormalizeCapacity(GrowthToLowerboundCapacity(growth)), capacity);
  }
}

TEST(Util, probe_seq) {
  probe_seq<16> seq(0, 127);
  auto gen = [&]() {
    size_t res = seq.offset();
    seq.next();
    return res;
  };
  std::vector<size_t> offsets(8);
  std::generate_n(offsets.begin(), 8, gen);
  EXPECT_THAT(offsets, ElementsAre(0, 16, 48, 96, 32, 112, 80, 64));
  seq = probe_seq<16>(128, 127);
  std::generate_n(offsets.begin(), 8, gen);
  EXPECT_THAT(offsets, ElementsAre(0, 16, 48, 96, 32, 112, 80, 64));
}

TEST(BitMask, Smoke) {
  EXPECT_FALSE((BitMask<uint8_t, 8>(0)));
  EXPECT_TRUE((BitMask<uint8_t, 8>(5)));

  EXPECT_THAT((BitMask<uint8_t, 8>(0)), ElementsAre());
  EXPECT_THAT((BitMask<uint8_t, 8>(0x1)), ElementsAre(0));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x2)), ElementsAre(1));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x3)), ElementsAre(0, 1));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x4)), ElementsAre(2));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x5)), ElementsAre(0, 2));
  EXPECT_THAT((BitMask<uint8_t, 8>(0x55)), ElementsAre(0, 2, 4, 6));
  EXPECT_THAT((BitMask<uint8_t, 8>(0xAA)), ElementsAre(1, 3, 5, 7));
}

TEST(BitMask, WithShift_MatchPortable) {
  // See the non-SSE version of Group for details on what this math is for.
  uint64_t ctrl = 0x1716151413121110;
  uint64_t hash = 0x12;
  constexpr uint64_t lsbs = 0x0101010101010101ULL;
  auto x = ctrl ^ (lsbs * hash);
  uint64_t mask = (x - lsbs) & ~x & kMsbs8Bytes;
  EXPECT_EQ(0x0000000080800000, mask);

  BitMask<uint64_t, 8, 3> b(mask);
  EXPECT_EQ(*b, 2);
}

constexpr uint64_t kSome8BytesMask = /*  */ 0x8000808080008000ULL;
constexpr uint64_t kSome8BytesMaskAllOnes = 0xff00ffffff00ff00ULL;
constexpr auto kSome8BytesMaskBits = std::array<int, 5>{1, 3, 4, 5, 7};


TEST(BitMask, WithShift_FullMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(kMsbs8Bytes)),
              ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT(
      (BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(kMsbs8Bytes)),
      ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
  EXPECT_THAT(
      (BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(~uint64_t{0})),
      ElementsAre(0, 1, 2, 3, 4, 5, 6, 7));
}

TEST(BitMask, WithShift_EmptyMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(0)), ElementsAre());
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(0)),
              ElementsAre());
}

TEST(BitMask, WithShift_SomeMask) {
  EXPECT_THAT((BitMask<uint64_t, 8, 3>(kSome8BytesMask)),
              ElementsAreArray(kSome8BytesMaskBits));
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                  kSome8BytesMask)),
              ElementsAreArray(kSome8BytesMaskBits));
  EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                  kSome8BytesMaskAllOnes)),
              ElementsAreArray(kSome8BytesMaskBits));
}

TEST(BitMask, WithShift_SomeMaskExtraBitsForNullify) {
  // Verify that adding extra bits into non zero bytes is fine.
  uint64_t extra_bits = 77;
  for (int i = 0; i < 100; ++i) {
    // Add extra bits, but keep zero bytes untouched.
    uint64_t extra_mask = extra_bits & kSome8BytesMaskAllOnes;
    EXPECT_THAT((BitMask<uint64_t, 8, 3, /*NullifyBitsOnIteration=*/true>(
                    kSome8BytesMask | extra_mask)),
                ElementsAreArray(kSome8BytesMaskBits))
        << i << " " << extra_mask;
    extra_bits = (extra_bits + 1) * 3;
  }
}

TEST(BitMask, LeadingTrailing) {
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00001a40).LeadingZeros()), 3);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00001a40).TrailingZeros()), 6);

  EXPECT_EQ((BitMask<uint32_t, 16>(0x00000001).LeadingZeros()), 15);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00000001).TrailingZeros()), 0);

  EXPECT_EQ((BitMask<uint32_t, 16>(0x00008000).LeadingZeros()), 0);
  EXPECT_EQ((BitMask<uint32_t, 16>(0x00008000).TrailingZeros()), 15);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000008080808000).LeadingZeros()), 3);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000008080808000).TrailingZeros()), 1);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000000000000080).LeadingZeros()), 7);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x0000000000000080).TrailingZeros()), 0);

  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x8000000000000000).LeadingZeros()), 0);
  EXPECT_EQ((BitMask<uint64_t, 8, 3>(0x8000000000000000).TrailingZeros()), 7);
}

TEST(Group, EmptyGroup) {
  for (h2_t h = 0; h != 128; ++h) EXPECT_FALSE(Group{EmptyGroup()}.Match(h));
}

TEST(Group, Match) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty, CtrlT(1), ctrl_t::kDeleted,  CtrlT(3),
                      ctrl_t::kEmpty, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),       CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),       CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.Match(0), ElementsAre());
    EXPECT_THAT(Group{group}.Match(1), ElementsAre(1, 11, 12, 13, 14, 15));
    EXPECT_THAT(Group{group}.Match(3), ElementsAre(3, 10));
    EXPECT_THAT(Group{group}.Match(5), ElementsAre(5, 9));
    EXPECT_THAT(Group{group}.Match(7), ElementsAre(7, 8));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.Match(0), ElementsAre());
    EXPECT_THAT(Group{group}.Match(1), ElementsAre(1, 5, 7));
    EXPECT_THAT(Group{group}.Match(2), ElementsAre(2, 4));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskEmpty) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty, CtrlT(1), ctrl_t::kDeleted,  CtrlT(3),
                      ctrl_t::kEmpty, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),       CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),       CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmpty().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmpty().HighestBitSet(), 4);
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmpty().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmpty().HighestBitSet(), 0);
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskFull) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {
        ctrl_t::kEmpty, CtrlT(1),          ctrl_t::kDeleted,  CtrlT(3),
        ctrl_t::kEmpty, CtrlT(5),          ctrl_t::kSentinel, CtrlT(7),
        CtrlT(7),       CtrlT(5),          ctrl_t::kDeleted,  CtrlT(1),
        CtrlT(1),       ctrl_t::kSentinel, ctrl_t::kEmpty,    CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskFull(),
                ElementsAre(1, 3, 5, 7, 8, 9, 11, 12, 15));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), ctrl_t::kEmpty,
                      ctrl_t::kDeleted,  CtrlT(2), ctrl_t::kSentinel,
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskFull(), ElementsAre(1, 4, 7));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskNonFull) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {
        ctrl_t::kEmpty, CtrlT(1),          ctrl_t::kDeleted,  CtrlT(3),
        ctrl_t::kEmpty, CtrlT(5),          ctrl_t::kSentinel, CtrlT(7),
        CtrlT(7),       CtrlT(5),          ctrl_t::kDeleted,  CtrlT(1),
        CtrlT(1),       ctrl_t::kSentinel, ctrl_t::kEmpty,    CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskNonFull(),
                ElementsAre(0, 2, 4, 6, 10, 13, 14));
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), ctrl_t::kEmpty,
                      ctrl_t::kDeleted,  CtrlT(2), ctrl_t::kSentinel,
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskNonFull(), ElementsAre(0, 2, 3, 5, 6));
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Group, MaskEmptyOrDeleted) {
  if (Group::kWidth == 16) {
    ctrl_t group[] = {ctrl_t::kEmpty,   CtrlT(1), ctrl_t::kEmpty,    CtrlT(3),
                      ctrl_t::kDeleted, CtrlT(5), ctrl_t::kSentinel, CtrlT(7),
                      CtrlT(7),         CtrlT(5), CtrlT(3),          CtrlT(1),
                      CtrlT(1),         CtrlT(1), CtrlT(1),          CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().HighestBitSet(), 4);
  } else if (Group::kWidth == 8) {
    ctrl_t group[] = {ctrl_t::kEmpty,    CtrlT(1), CtrlT(2),
                      ctrl_t::kDeleted,  CtrlT(2), CtrlT(1),
                      ctrl_t::kSentinel, CtrlT(1)};
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().LowestBitSet(), 0);
    EXPECT_THAT(Group{group}.MaskEmptyOrDeleted().HighestBitSet(), 3);
  } else {
    FAIL() << "No test coverage for Group::kWidth==" << Group::kWidth;
  }
}

TEST(Batch, DropDeletes) {
  constexpr size_t kCapacity = 63;
  constexpr size_t kGroupWidth = container_internal::Group::kWidth;
  std::vector<ctrl_t> ctrl(kCapacity + 1 + kGroupWidth);
  ctrl[kCapacity] = ctrl_t::kSentinel;
  std::vector<ctrl_t> pattern = {
      ctrl_t::kEmpty, CtrlT(2), ctrl_t::kDeleted, CtrlT(2),
      ctrl_t::kEmpty, CtrlT(1), ctrl_t::kDeleted};
  for (size_t i = 0; i != kCapacity; ++i) {
    ctrl[i] = pattern[i % pattern.size()];
    if (i < kGroupWidth - 1)
      ctrl[i + kCapacity + 1] = pattern[i % pattern.size()];
  }
  ConvertDeletedToEmptyAndFullToDeleted(ctrl.data(), kCapacity);
  ASSERT_EQ(ctrl[kCapacity], ctrl_t::kSentinel);
  for (size_t i = 0; i < kCapacity + kGroupWidth; ++i) {
    ctrl_t expected = pattern[i % (kCapacity + 1) % pattern.size()];
    if (i == kCapacity) expected = ctrl_t::kSentinel;
    if (expected == ctrl_t::kDeleted) expected = ctrl_t::kEmpty;
    if (IsFull(expected)) expected = ctrl_t::kDeleted;
    EXPECT_EQ(ctrl[i], expected)
        << i << " " << static_cast<int>(pattern[i % pattern.size()]);
  }
}

TEST(Group, CountLeadingEmptyOrDeleted) {
  const std::vector<ctrl_t> empty_examples = {ctrl_t::kEmpty, ctrl_t::kDeleted};
  const std::vector<ctrl_t> full_examples = {
      CtrlT(0), CtrlT(1), CtrlT(2),   CtrlT(3),
      CtrlT(5), CtrlT(9), CtrlT(127), ctrl_t::kSentinel};

  for (ctrl_t empty : empty_examples) {
    std::vector<ctrl_t> e(Group::kWidth, empty);
    EXPECT_EQ(Group::kWidth, Group{e.data()}.CountLeadingEmptyOrDeleted());
    for (ctrl_t full : full_examples) {
      for (size_t i = 0; i != Group::kWidth; ++i) {
        std::vector<ctrl_t> f(Group::kWidth, empty);
        f[i] = full;
        EXPECT_EQ(i, Group{f.data()}.CountLeadingEmptyOrDeleted());
      }
      std::vector<ctrl_t> f(Group::kWidth, empty);
      f[Group::kWidth * 2 / 3] = full;
      f[Group::kWidth / 2] = full;
      EXPECT_EQ(Group::kWidth / 2,
                Group{f.data()}.CountLeadingEmptyOrDeleted());
    }
  }
}

template <class T, bool kTransferable = false, bool kSoo = false>
struct ValuePolicy {
  using slot_type = T;
  using key_type = T;
  using init_type = T;

  template <class Allocator, class... Args>
  static void construct(Allocator* alloc, slot_type* slot, Args&&... args) {
    absl::allocator_traits<Allocator>::construct(*alloc, slot,
                                                 std::forward<Args>(args)...);
  }

  template <class Allocator>
  static void destroy(Allocator* alloc, slot_type* slot) {
    absl::allocator_traits<Allocator>::destroy(*alloc, slot);
  }

  template <class Allocator>
  static std::integral_constant<bool, kTransferable> transfer(
      Allocator* alloc, slot_type* new_slot, slot_type* old_slot) {
    construct(alloc, new_slot, std::move(*old_slot));
    destroy(alloc, old_slot);
    return {};
  }

  static T& element(slot_type* slot) { return *slot; }

  template <class F, class... Args>
  static decltype(absl::container_internal::DecomposeValue(
      std::declval<F>(), std::declval<Args>()...))
  apply(F&& f, Args&&... args) {
    return absl::container_internal::DecomposeValue(
        std::forward<F>(f), std::forward<Args>(args)...);
  }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }

  static constexpr bool soo_enabled() { return kSoo; }
};

using IntPolicy = ValuePolicy<int64_t>;
using Uint8Policy = ValuePolicy<uint8_t>;

using TranferableIntPolicy = ValuePolicy<int64_t, /*kTransferable=*/true>;

// For testing SOO.
template <int N>
class SizedValue {
 public:
  SizedValue(int64_t v) {  // NOLINT
    vals_[0] = v;
  }
  SizedValue() : SizedValue(0) {}
  SizedValue(const SizedValue&) = default;
  SizedValue& operator=(const SizedValue&) = default;

  int64_t operator*() const {
    // Suppress erroneous uninitialized memory errors on GCC.
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
    return vals_[0];
#if !defined(__clang__) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  }
  explicit operator int() const { return **this; }
  explicit operator int64_t() const { return **this; }

  template <typename H>
  friend H AbslHashValue(H h, SizedValue sv) {
    return H::combine(std::move(h), *sv);
  }
  bool operator==(const SizedValue& rhs) const { return **this == *rhs; }

 private:
  int64_t vals_[N / sizeof(int64_t)];
};
template <int N, bool kSoo>
using SizedValuePolicy =
    ValuePolicy<SizedValue<N>, /*kTransferable=*/true, kSoo>;

class StringPolicy {
  template <class F, class K, class V,
            class = typename std::enable_if<
                std::is_convertible<const K&, absl::string_view>::value>::type>
  decltype(std::declval<F>()(
      std::declval<const absl::string_view&>(), std::piecewise_construct,
      std::declval<std::tuple<K>>(),
      std::declval<V>())) static apply_impl(F&& f,
                                            std::pair<std::tuple<K>, V> p) {
    const absl::string_view& key = std::get<0>(p.first);
    return std::forward<F>(f)(key, std::piecewise_construct, std::move(p.first),
                              std::move(p.second));
  }

 public:
  struct slot_type {
    struct ctor {};

    template <class... Ts>
    explicit slot_type(ctor, Ts&&... ts) : pair(std::forward<Ts>(ts)...) {}

    std::pair<std::string, std::string> pair;
  };

  using key_type = std::string;
  using init_type = std::pair<std::string, std::string>;

  template <class allocator_type, class... Args>
  static void construct(allocator_type* alloc, slot_type* slot, Args... args) {
    std::allocator_traits<allocator_type>::construct(
        *alloc, slot, typename slot_type::ctor(), std::forward<Args>(args)...);
  }

  template <class allocator_type>
  static void destroy(allocator_type* alloc, slot_type* slot) {
    std::allocator_traits<allocator_type>::destroy(*alloc, slot);
  }

  template <class allocator_type>
  static void transfer(allocator_type* alloc, slot_type* new_slot,
                       slot_type* old_slot) {
    construct(alloc, new_slot, std::move(old_slot->pair));
    destroy(alloc, old_slot);
  }

  static std::pair<std::string, std::string>& element(slot_type* slot) {
    return slot->pair;
  }

  template <class F, class... Args>
  static auto apply(F&& f, Args&&... args)
      -> decltype(apply_impl(std::forward<F>(f),
                             PairArgs(std::forward<Args>(args)...))) {
    return apply_impl(std::forward<F>(f),
                      PairArgs(std::forward<Args>(args)...));
  }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }
};

struct StringHash : absl::Hash<absl::string_view> {
  using is_transparent = void;
};
struct StringEq : std::equal_to<absl::string_view> {
  using is_transparent = void;
};

struct StringTable
    : raw_hash_set<StringPolicy, StringHash, StringEq, std::allocator<int>> {
  using Base = typename StringTable::raw_hash_set;
  StringTable() = default;
  using Base::Base;
};

template <typename T, bool kTransferable = false, bool kSoo = false>
struct ValueTable
    : raw_hash_set<ValuePolicy<T, kTransferable, kSoo>, hash_default_hash<T>,
                   std::equal_to<T>, std::allocator<T>> {
  using Base = typename ValueTable::raw_hash_set;
  using Base::Base;
};

using IntTable = ValueTable<int64_t>;
using Uint8Table = ValueTable<uint8_t>;

using TransferableIntTable = ValueTable<int64_t, /*kTransferable=*/true>;

constexpr size_t kNonSooSize = sizeof(HeapOrSoo) + 8;
static_assert(sizeof(SizedValue<kNonSooSize>) >= kNonSooSize, "too small");
using NonSooIntTable = ValueTable<SizedValue<kNonSooSize>>;
using SooIntTable = ValueTable<int64_t, /*kTransferable=*/true, /*kSoo=*/true>;

template <typename T>
struct CustomAlloc : std::allocator<T> {
  CustomAlloc() = default;

  template <typename U>
  explicit CustomAlloc(const CustomAlloc<U>& /*other*/) {}

  template <class U>
  struct rebind {
    using other = CustomAlloc<U>;
  };
};

struct CustomAllocIntTable
    : raw_hash_set<IntPolicy, hash_default_hash<int64_t>,
                   std::equal_to<int64_t>, CustomAlloc<int64_t>> {
  using Base = typename CustomAllocIntTable::raw_hash_set;
  using Base::Base;
};

struct MinimumAlignmentUint8Table
    : raw_hash_set<Uint8Policy, hash_default_hash<uint8_t>,
                   std::equal_to<uint8_t>, MinimumAlignmentAlloc<uint8_t>> {
  using Base = typename MinimumAlignmentUint8Table::raw_hash_set;
  using Base::Base;
};

// Allows for freezing the allocator to expect no further allocations.
template <typename T>
struct FreezableAlloc : std::allocator<T> {
  explicit FreezableAlloc(bool* f) : frozen(f) {}

  template <typename U>
  explicit FreezableAlloc(const FreezableAlloc<U>& other)
      : frozen(other.frozen) {}

  template <class U>
  struct rebind {
    using other = FreezableAlloc<U>;
  };

  T* allocate(size_t n) {
    EXPECT_FALSE(*frozen);
    return std::allocator<T>::allocate(n);
  }

  bool* frozen;
};

template <int N>
struct FreezableSizedValueSooTable
    : raw_hash_set<SizedValuePolicy<N, /*kSoo=*/true>,
                   container_internal::hash_default_hash<SizedValue<N>>,
                   std::equal_to<SizedValue<N>>,
                   FreezableAlloc<SizedValue<N>>> {
  using Base = typename FreezableSizedValueSooTable::raw_hash_set;
  using Base::Base;
};

struct BadFastHash {
  template <class T>
  size_t operator()(const T&) const {
    return 0;
  }
};

struct BadHashFreezableIntTable
    : raw_hash_set<IntPolicy, BadFastHash, std::equal_to<int64_t>,
                   FreezableAlloc<int64_t>> {
  using Base = typename BadHashFreezableIntTable::raw_hash_set;
  using Base::Base;
};

struct BadTable : raw_hash_set<IntPolicy, BadFastHash, std::equal_to<int>,
                               std::allocator<int>> {
  using Base = typename BadTable::raw_hash_set;
  BadTable() = default;
  using Base::Base;
};

TEST(Table, EmptyFunctorOptimization) {
  static_assert(std::is_empty<std::equal_to<absl::string_view>>::value, "");
  static_assert(std::is_empty<std::allocator<int>>::value, "");

  struct MockTable {
    void* ctrl;
    void* slots;
    size_t size;
    size_t capacity;
  };
  struct StatelessHash {
    size_t operator()(absl::string_view) const { return 0; }
  };
  struct StatefulHash : StatelessHash {
    size_t dummy;
  };

  struct GenerationData {
    size_t reserved_growth;
    size_t reservation_size;
    GenerationType* generation;
  };

// Ignore unreachable-code warning. Compiler thinks one branch of each ternary
// conditional is unreachable.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif
  constexpr size_t mock_size = sizeof(MockTable);
  constexpr size_t generation_size =
      SwisstableGenerationsEnabled() ? sizeof(GenerationData) : 0;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

  EXPECT_EQ(
      mock_size + generation_size,
      sizeof(
          raw_hash_set<StringPolicy, StatelessHash,
                       std::equal_to<absl::string_view>, std::allocator<int>>));

  EXPECT_EQ(
      mock_size + sizeof(StatefulHash) + generation_size,
      sizeof(
          raw_hash_set<StringPolicy, StatefulHash,
                       std::equal_to<absl::string_view>, std::allocator<int>>));
}

template <class TableType>
class SooTest : public testing::Test {};

using SooTableTypes = ::testing::Types<SooIntTable, NonSooIntTable>;
TYPED_TEST_SUITE(SooTest, SooTableTypes);

TYPED_TEST(SooTest, Empty) {
  TypeParam t;
  EXPECT_EQ(0, t.size());
  EXPECT_TRUE(t.empty());
}

TYPED_TEST(SooTest, LookupEmpty) {
  TypeParam t;
  auto it = t.find(0);
  EXPECT_TRUE(it == t.end());
}

TYPED_TEST(SooTest, Insert1) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  auto res = t.emplace(0);
  EXPECT_TRUE(res.second);
  EXPECT_THAT(*res.first, 0);
  EXPECT_EQ(1, t.size());
  EXPECT_THAT(*t.find(0), 0);
}

TYPED_TEST(SooTest, Insert2) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  auto res = t.emplace(0);
  EXPECT_TRUE(res.second);
  EXPECT_THAT(*res.first, 0);
  EXPECT_EQ(1, t.size());
  EXPECT_TRUE(t.find(1) == t.end());
  res = t.emplace(1);
  EXPECT_TRUE(res.second);
  EXPECT_THAT(*res.first, 1);
  EXPECT_EQ(2, t.size());
  EXPECT_THAT(*t.find(0), 0);
  EXPECT_THAT(*t.find(1), 1);
}

TEST(Table, InsertCollision) {
  BadTable t;
  EXPECT_TRUE(t.find(1) == t.end());
  auto res = t.emplace(1);
  EXPECT_TRUE(res.second);
  EXPECT_THAT(*res.first, 1);
  EXPECT_EQ(1, t.size());

  EXPECT_TRUE(t.find(2) == t.end());
  res = t.emplace(2);
  EXPECT_THAT(*res.first, 2);
  EXPECT_TRUE(res.second);
  EXPECT_EQ(2, t.size());

  EXPECT_THAT(*t.find(1), 1);
  EXPECT_THAT(*t.find(2), 2);
}

// Test that we do not add existent element in case we need to search through
// many groups with deleted elements
TEST(Table, InsertCollisionAndFindAfterDelete) {
  BadTable t;  // all elements go to the same group.
  // Have at least 2 groups with Group::kWidth collisions
  // plus some extra collisions in the last group.
  constexpr size_t kNumInserts = Group::kWidth * 2 + 5;
  for (size_t i = 0; i < kNumInserts; ++i) {
    auto res = t.emplace(i);
    EXPECT_TRUE(res.second);
    EXPECT_THAT(*res.first, i);
    EXPECT_EQ(i + 1, t.size());
  }

  // Remove elements one by one and check
  // that we still can find all other elements.
  for (size_t i = 0; i < kNumInserts; ++i) {
    EXPECT_EQ(1, t.erase(i)) << i;
    for (size_t j = i + 1; j < kNumInserts; ++j) {
      EXPECT_THAT(*t.find(j), j);
      auto res = t.emplace(j);
      EXPECT_FALSE(res.second) << i << " " << j;
      EXPECT_THAT(*res.first, j);
      EXPECT_EQ(kNumInserts - i - 1, t.size());
    }
  }
  EXPECT_TRUE(t.empty());
}

TYPED_TEST(SooTest, EraseInSmallTables) {
  for (int64_t size = 0; size < 64; ++size) {
    TypeParam t;
    for (int64_t i = 0; i < size; ++i) {
      t.insert(i);
    }
    for (int64_t i = 0; i < size; ++i) {
      t.erase(i);
      EXPECT_EQ(t.size(), size - i - 1);
      for (int64_t j = i + 1; j < size; ++j) {
        EXPECT_THAT(*t.find(j), j);
      }
    }
    EXPECT_TRUE(t.empty());
  }
}

TYPED_TEST(SooTest, InsertWithinCapacity) {
  TypeParam t;
  t.reserve(10);
  const size_t original_capacity = t.capacity();
  const auto addr = [&](int i) {
    return reinterpret_cast<uintptr_t>(&*t.find(i));
  };
  // Inserting an element does not change capacity.
  t.insert(0);
  EXPECT_THAT(t.capacity(), original_capacity);
  const uintptr_t original_addr_0 = addr(0);
  // Inserting another element does not rehash.
  t.insert(1);
  EXPECT_THAT(t.capacity(), original_capacity);
  EXPECT_THAT(addr(0), original_addr_0);
  // Inserting lots of duplicate elements does not rehash.
  for (int i = 0; i < 100; ++i) {
    t.insert(i % 10);
  }
  EXPECT_THAT(t.capacity(), original_capacity);
  EXPECT_THAT(addr(0), original_addr_0);
  // Inserting a range of duplicate elements does not rehash.
  std::vector<int> dup_range;
  for (int i = 0; i < 100; ++i) {
    dup_range.push_back(i % 10);
  }
  t.insert(dup_range.begin(), dup_range.end());
  EXPECT_THAT(t.capacity(), original_capacity);
  EXPECT_THAT(addr(0), original_addr_0);
}

template <class TableType>
class SmallTableResizeTest : public testing::Test {};

using SmallTableTypes =
    ::testing::Types<IntTable, TransferableIntTable, SooIntTable>;
TYPED_TEST_SUITE(SmallTableResizeTest, SmallTableTypes);

TYPED_TEST(SmallTableResizeTest, InsertIntoSmallTable) {
  TypeParam t;
  for (int i = 0; i < 32; ++i) {
    t.insert(i);
    ASSERT_EQ(t.size(), i + 1);
    for (int j = 0; j < i + 1; ++j) {
      EXPECT_TRUE(t.find(j) != t.end());
      EXPECT_EQ(*t.find(j), j);
    }
  }
}

TYPED_TEST(SmallTableResizeTest, ResizeGrowSmallTables) {
  for (size_t source_size = 0; source_size < 32; ++source_size) {
    for (size_t target_size = source_size; target_size < 32; ++target_size) {
      for (bool rehash : {false, true}) {
        TypeParam t;
        for (size_t i = 0; i < source_size; ++i) {
          t.insert(static_cast<int>(i));
        }
        if (rehash) {
          t.rehash(target_size);
        } else {
          t.reserve(target_size);
        }
        for (size_t i = 0; i < source_size; ++i) {
          EXPECT_TRUE(t.find(static_cast<int>(i)) != t.end());
          EXPECT_EQ(*t.find(static_cast<int>(i)), static_cast<int>(i));
        }
      }
    }
  }
}

TYPED_TEST(SmallTableResizeTest, ResizeReduceSmallTables) {
  for (size_t source_size = 0; source_size < 32; ++source_size) {
    for (size_t target_size = 0; target_size <= source_size; ++target_size) {
      TypeParam t;
      size_t inserted_count = std::min<size_t>(source_size, 5);
      for (size_t i = 0; i < inserted_count; ++i) {
        t.insert(static_cast<int>(i));
      }
      const size_t minimum_capacity = t.capacity();
      t.reserve(source_size);
      t.rehash(target_size);
      if (target_size == 0) {
        EXPECT_EQ(t.capacity(), minimum_capacity)
            << "rehash(0) must resize to the minimum capacity";
      }
      for (size_t i = 0; i < inserted_count; ++i) {
        EXPECT_TRUE(t.find(static_cast<int>(i)) != t.end());
        EXPECT_EQ(*t.find(static_cast<int>(i)), static_cast<int>(i));
      }
    }
  }
}

TEST(Table, LazyEmplace) {
  StringTable t;
  bool called = false;
  auto it = t.lazy_emplace("abc", [&](const StringTable::constructor& f) {
    called = true;
    f("abc", "ABC");
  });
  EXPECT_TRUE(called);
  EXPECT_THAT(*it, Pair("abc", "ABC"));
  called = false;
  it = t.lazy_emplace("abc", [&](const StringTable::constructor& f) {
    called = true;
    f("abc", "DEF");
  });
  EXPECT_FALSE(called);
  EXPECT_THAT(*it, Pair("abc", "ABC"));
}

TYPED_TEST(SooTest, ContainsEmpty) {
  TypeParam t;

  EXPECT_FALSE(t.contains(0));
}

TYPED_TEST(SooTest, Contains1) {
  TypeParam t;

  EXPECT_TRUE(t.insert(0).second);
  EXPECT_TRUE(t.contains(0));
  EXPECT_FALSE(t.contains(1));

  EXPECT_EQ(1, t.erase(0));
  EXPECT_FALSE(t.contains(0));
}

TYPED_TEST(SooTest, Contains2) {
  TypeParam t;

  EXPECT_TRUE(t.insert(0).second);
  EXPECT_TRUE(t.contains(0));
  EXPECT_FALSE(t.contains(1));

  t.clear();
  EXPECT_FALSE(t.contains(0));
}

int decompose_constructed;
int decompose_copy_constructed;
int decompose_copy_assigned;
int decompose_move_constructed;
int decompose_move_assigned;
struct DecomposeType {
  DecomposeType(int i = 0) : i(i) {  // NOLINT
    ++decompose_constructed;
  }

  explicit DecomposeType(const char* d) : DecomposeType(*d) {}

  DecomposeType(const DecomposeType& other) : i(other.i) {
    ++decompose_copy_constructed;
  }
  DecomposeType& operator=(const DecomposeType& other) {
    ++decompose_copy_assigned;
    i = other.i;
    return *this;
  }
  DecomposeType(DecomposeType&& other) : i(other.i) {
    ++decompose_move_constructed;
  }
  DecomposeType& operator=(DecomposeType&& other) {
    ++decompose_move_assigned;
    i = other.i;
    return *this;
  }

  int i;
};

struct DecomposeHash {
  using is_transparent = void;
  size_t operator()(const DecomposeType& a) const { return a.i; }
  size_t operator()(int a) const { return a; }
  size_t operator()(const char* a) const { return *a; }
};

struct DecomposeEq {
  using is_transparent = void;
  bool operator()(const DecomposeType& a, const DecomposeType& b) const {
    return a.i == b.i;
  }
  bool operator()(const DecomposeType& a, int b) const { return a.i == b; }
  bool operator()(const DecomposeType& a, const char* b) const {
    return a.i == *b;
  }
};

struct DecomposePolicy {
  using slot_type = DecomposeType;
  using key_type = DecomposeType;
  using init_type = DecomposeType;

  template <typename T>
  static void construct(void*, DecomposeType* slot, T&& v) {
    ::new (slot) DecomposeType(std::forward<T>(v));
  }
  static void destroy(void*, DecomposeType* slot) { slot->~DecomposeType(); }
  static DecomposeType& element(slot_type* slot) { return *slot; }

  template <class F, class T>
  static auto apply(F&& f, const T& x) -> decltype(std::forward<F>(f)(x, x)) {
    return std::forward<F>(f)(x, x);
  }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return nullptr;
  }
};

template <typename Hash, typename Eq>
void TestDecompose(bool construct_three) {
  DecomposeType elem{0};
  const int one = 1;
  const char* three_p = "3";
  const auto& three = three_p;
  const int elem_vector_count = 256;
  std::vector<DecomposeType> elem_vector(elem_vector_count, DecomposeType{0});
  std::iota(elem_vector.begin(), elem_vector.end(), 0);

  using DecomposeSet =
      raw_hash_set<DecomposePolicy, Hash, Eq, std::allocator<int>>;
  DecomposeSet set1;

  decompose_constructed = 0;
  int expected_constructed = 0;
  EXPECT_EQ(expected_constructed, decompose_constructed);
  set1.insert(elem);
  EXPECT_EQ(expected_constructed, decompose_constructed);
  set1.insert(1);
  EXPECT_EQ(++expected_constructed, decompose_constructed);
  set1.emplace("3");
  EXPECT_EQ(++expected_constructed, decompose_constructed);
  EXPECT_EQ(expected_constructed, decompose_constructed);

  {  // insert(T&&)
    set1.insert(1);
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  {  // insert(const T&)
    set1.insert(one);
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  {  // insert(hint, T&&)
    set1.insert(set1.begin(), 1);
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  {  // insert(hint, const T&)
    set1.insert(set1.begin(), one);
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  {  // emplace(...)
    set1.emplace(1);
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace("3");
    expected_constructed += construct_three;
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace(one);
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace(three);
    expected_constructed += construct_three;
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  {  // emplace_hint(...)
    set1.emplace_hint(set1.begin(), 1);
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace_hint(set1.begin(), "3");
    expected_constructed += construct_three;
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace_hint(set1.begin(), one);
    EXPECT_EQ(expected_constructed, decompose_constructed);
    set1.emplace_hint(set1.begin(), three);
    expected_constructed += construct_three;
    EXPECT_EQ(expected_constructed, decompose_constructed);
  }

  decompose_copy_constructed = 0;
  decompose_copy_assigned = 0;
  decompose_move_constructed = 0;
  decompose_move_assigned = 0;
  int expected_copy_constructed = 0;
  int expected_move_constructed = 0;
  {  // raw_hash_set(first, last) with random-access iterators
    DecomposeSet set2(elem_vector.begin(), elem_vector.end());
    // Expect exactly one copy-constructor call for each element if no
    // rehashing is done.
    expected_copy_constructed += elem_vector_count;
    EXPECT_EQ(expected_copy_constructed, decompose_copy_constructed);
    EXPECT_EQ(expected_move_constructed, decompose_move_constructed);
    EXPECT_EQ(0, decompose_move_assigned);
    EXPECT_EQ(0, decompose_copy_assigned);
  }

  {  // raw_hash_set(first, last) with forward iterators
    std::list<DecomposeType> elem_list(elem_vector.begin(), elem_vector.end());
    expected_copy_constructed = decompose_copy_constructed;
    DecomposeSet set2(elem_list.begin(), elem_list.end());
    // Expect exactly N elements copied into set, expect at most 2*N elements
    // moving internally for all resizing needed (for a growth factor of 2).
    expected_copy_constructed += elem_vector_count;
    EXPECT_EQ(expected_copy_constructed, decompose_copy_constructed);
    expected_move_constructed += elem_vector_count;
    EXPECT_LT(expected_move_constructed, decompose_move_constructed);
    expected_move_constructed += elem_vector_count;
    EXPECT_GE(expected_move_constructed, decompose_move_constructed);
    EXPECT_EQ(0, decompose_move_assigned);
    EXPECT_EQ(0, decompose_copy_assigned);
    expected_copy_constructed = decompose_copy_constructed;
    expected_move_constructed = decompose_move_constructed;
  }

  {  // insert(first, last)
    DecomposeSet set2;
    set2.insert(elem_vector.begin(), elem_vector.end());
    // Expect exactly N elements copied into set, expect at most 2*N elements
    // moving internally for all resizing needed (for a growth factor of 2).
    const int expected_new_elements = elem_vector_count;
    const int expected_max_element_moves = 2 * elem_vector_count;
    expected_copy_constructed += expected_new_elements;
    EXPECT_EQ(expected_copy_constructed, decompose_copy_constructed);
    expected_move_constructed += expected_max_element_moves;
    EXPECT_GE(expected_move_constructed, decompose_move_constructed);
    EXPECT_EQ(0, decompose_move_assigned);
    EXPECT_EQ(0, decompose_copy_assigned);
    expected_copy_constructed = decompose_copy_constructed;
    expected_move_constructed = decompose_move_constructed;
  }
}

TEST(Table, Decompose) {
  if (SwisstableGenerationsEnabled()) {
    GTEST_SKIP() << "Generations being enabled causes extra rehashes.";
  }

  TestDecompose<DecomposeHash, DecomposeEq>(false);

  struct TransparentHashIntOverload {
    size_t operator()(const DecomposeType& a) const { return a.i; }
    size_t operator()(int a) const { return a; }
  };
  struct TransparentEqIntOverload {
    bool operator()(const DecomposeType& a, const DecomposeType& b) const {
      return a.i == b.i;
    }
    bool operator()(const DecomposeType& a, int b) const { return a.i == b; }
  };
  TestDecompose<TransparentHashIntOverload, DecomposeEq>(true);
  TestDecompose<TransparentHashIntOverload, TransparentEqIntOverload>(true);
  TestDecompose<DecomposeHash, TransparentEqIntOverload>(true);
}

// Returns the largest m such that a table with m elements has the same number
// of buckets as a table with n elements.
size_t MaxDensitySize(size_t n) {
  IntTable t;
  t.reserve(n);
  for (size_t i = 0; i != n; ++i) t.emplace(i);
  const size_t c = t.bucket_count();
  while (c == t.bucket_count()) t.emplace(n++);
  return t.size() - 1;
}

struct Modulo1000Hash {
  size_t operator()(int64_t x) const { return static_cast<size_t>(x) % 1000; }
};

struct Modulo1000HashTable
    : public raw_hash_set<IntPolicy, Modulo1000Hash, std::equal_to<int>,
                          std::allocator<int>> {};

// Test that rehash with no resize happen in case of many deleted slots.
TEST(Table, RehashWithNoResize) {
  if (SwisstableGenerationsEnabled()) {
    GTEST_SKIP() << "Generations being enabled causes extra rehashes.";
  }

  Modulo1000HashTable t;
  // Adding the same length (and the same hash) strings
  // to have at least kMinFullGroups groups
  // with Group::kWidth collisions. Then fill up to MaxDensitySize;
  const size_t kMinFullGroups = 7;
  std::vector<int> keys;
  for (size_t i = 0; i < MaxDensitySize(Group::kWidth * kMinFullGroups); ++i) {
    int k = i * 1000;
    t.emplace(k);
    keys.push_back(k);
  }
  const size_t capacity = t.capacity();

  // Remove elements from all groups except the first and the last one.
  // All elements removed from full groups will be marked as ctrl_t::kDeleted.
  const size_t erase_begin = Group::kWidth / 2;
  const size_t erase_end = (t.size() / Group::kWidth - 1) * Group::kWidth;
  for (size_t i = erase_begin; i < erase_end; ++i) {
    EXPECT_EQ(1, t.erase(keys[i])) << i;
  }
  keys.erase(keys.begin() + erase_begin, keys.begin() + erase_end);

  auto last_key = keys.back();
  size_t last_key_num_probes = GetHashtableDebugNumProbes(t, last_key);

  // Make sure that we have to make a lot of probes for last key.
  ASSERT_GT(last_key_num_probes, kMinFullGroups);

  int x = 1;
  // Insert and erase one element, before inplace rehash happen.
  while (last_key_num_probes == GetHashtableDebugNumProbes(t, last_key)) {
    t.emplace(x);
    ASSERT_EQ(capacity, t.capacity());
    // All elements should be there.
    ASSERT_TRUE(t.find(x) != t.end()) << x;
    for (const auto& k : keys) {
      ASSERT_TRUE(t.find(k) != t.end()) << k;
    }
    t.erase(x);
    ++x;
  }
}

TYPED_TEST(SooTest, InsertEraseStressTest) {
  TypeParam t;
  const size_t kMinElementCount = 250;
  std::deque<int> keys;
  size_t i = 0;
  for (; i < MaxDensitySize(kMinElementCount); ++i) {
    t.emplace(i);
    keys.push_back(i);
  }
  const size_t kNumIterations = 1000000;
  for (; i < kNumIterations; ++i) {
    ASSERT_EQ(1, t.erase(keys.front()));
    keys.pop_front();
    t.emplace(i);
    keys.push_back(i);
  }
}

TEST(Table, InsertOverloads) {
  StringTable t;
  // These should all trigger the insert(init_type) overload.
  t.insert({{}, {}});
  t.insert({"ABC", {}});
  t.insert({"DEF", "!!!"});

  EXPECT_THAT(t, UnorderedElementsAre(Pair("", ""), Pair("ABC", ""),
                                      Pair("DEF", "!!!")));
}

TYPED_TEST(SooTest, LargeTable) {
  TypeParam t;
  for (int64_t i = 0; i != 100000; ++i) t.emplace(i << 40);
  for (int64_t i = 0; i != 100000; ++i)
    ASSERT_EQ(i << 40, static_cast<int64_t>(*t.find(i << 40)));
}

// Timeout if copy is quadratic as it was in Rust.
TYPED_TEST(SooTest, EnsureNonQuadraticAsInRust) {
  static const size_t kLargeSize = 1 << 15;

  TypeParam t;
  for (size_t i = 0; i != kLargeSize; ++i) {
    t.insert(i);
  }

  // If this is quadratic, the test will timeout.
  TypeParam t2;
  for (const auto& entry : t) t2.insert(entry);
}

TYPED_TEST(SooTest, ClearBug) {
  if (SwisstableGenerationsEnabled()) {
    GTEST_SKIP() << "Generations being enabled causes extra rehashes.";
  }

  TypeParam t;
  constexpr size_t capacity = container_internal::Group::kWidth - 1;
  constexpr size_t max_size = capacity / 2 + 1;
  for (size_t i = 0; i < max_size; ++i) {
    t.insert(i);
  }
  ASSERT_EQ(capacity, t.capacity());
  intptr_t original = reinterpret_cast<intptr_t>(&*t.find(2));
  t.clear();
  ASSERT_EQ(capacity, t.capacity());
  for (size_t i = 0; i < max_size; ++i) {
    t.insert(i);
  }
  ASSERT_EQ(capacity, t.capacity());
  intptr_t second = reinterpret_cast<intptr_t>(&*t.find(2));
  // We are checking that original and second are close enough to each other
  // that they are probably still in the same group.  This is not strictly
  // guaranteed.
  EXPECT_LT(static_cast<size_t>(std::abs(original - second)),
            capacity * sizeof(typename TypeParam::value_type));
}

TYPED_TEST(SooTest, Erase) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  auto res = t.emplace(0);
  EXPECT_TRUE(res.second);
  EXPECT_EQ(1, t.size());
  t.erase(res.first);
  EXPECT_EQ(0, t.size());
  EXPECT_TRUE(t.find(0) == t.end());
}

TYPED_TEST(SooTest, EraseMaintainsValidIterator) {
  TypeParam t;
  const int kNumElements = 100;
  for (int i = 0; i < kNumElements; i++) {
    EXPECT_TRUE(t.emplace(i).second);
  }
  EXPECT_EQ(t.size(), kNumElements);

  int num_erase_calls = 0;
  auto it = t.begin();
  while (it != t.end()) {
    t.erase(it++);
    num_erase_calls++;
  }

  EXPECT_TRUE(t.empty());
  EXPECT_EQ(num_erase_calls, kNumElements);
}

TYPED_TEST(SooTest, EraseBeginEnd) {
  TypeParam t;
  for (int i = 0; i < 10; ++i) t.insert(i);
  EXPECT_EQ(t.size(), 10);
  t.erase(t.begin(), t.end());
  EXPECT_EQ(t.size(), 0);
}

// Collect N bad keys by following algorithm:
// 1. Create an empty table and reserve it to 2 * N.
// 2. Insert N random elements.
// 3. Take first Group::kWidth - 1 to bad_keys array.
// 4. Clear the table without resize.
// 5. Go to point 2 while N keys not collected
std::vector<int64_t> CollectBadMergeKeys(size_t N) {
  static constexpr int kGroupSize = Group::kWidth - 1;

  auto topk_range = [](size_t b, size_t e,
                       IntTable* t) -> std::vector<int64_t> {
    for (size_t i = b; i != e; ++i) {
      t->emplace(i);
    }
    std::vector<int64_t> res;
    res.reserve(kGroupSize);
    auto it = t->begin();
    for (size_t i = b; i != e && i != b + kGroupSize; ++i, ++it) {
      res.push_back(*it);
    }
    return res;
  };

  std::vector<int64_t> bad_keys;
  bad_keys.reserve(N);
  IntTable t;
  t.reserve(N * 2);

  for (size_t b = 0; bad_keys.size() < N; b += N) {
    auto keys = topk_range(b, b + N, &t);
    bad_keys.insert(bad_keys.end(), keys.begin(), keys.end());
    t.erase(t.begin(), t.end());
    EXPECT_TRUE(t.empty());
  }
  return bad_keys;
}

struct ProbeStats {
  // Number of elements with specific probe length over all tested tables.
  std::vector<size_t> all_probes_histogram;
  // Ratios total_probe_length/size for every tested table.
  std::vector<double> single_table_ratios;

  // Average ratio total_probe_length/size over tables.
  double AvgRatio() const {
    return std::accumulate(single_table_ratios.begin(),
                           single_table_ratios.end(), 0.0) /
           single_table_ratios.size();
  }

  // Maximum ratio total_probe_length/size over tables.
  double MaxRatio() const {
    return *std::max_element(single_table_ratios.begin(),
                             single_table_ratios.end());
  }

  // Percentile ratio total_probe_length/size over tables.
  double PercentileRatio(double Percentile = 0.95) const {
    auto r = single_table_ratios;
    auto mid = r.begin() + static_cast<size_t>(r.size() * Percentile);
    if (mid != r.end()) {
      std::nth_element(r.begin(), mid, r.end());
      return *mid;
    } else {
      return MaxRatio();
    }
  }

  // Maximum probe length over all elements and all tables.
  size_t MaxProbe() const { return all_probes_histogram.size(); }

  // Fraction of elements with specified probe length.
  std::vector<double> ProbeNormalizedHistogram() const {
    double total_elements = std::accumulate(all_probes_histogram.begin(),
                                            all_probes_histogram.end(), 0ull);
    std::vector<double> res;
    for (size_t p : all_probes_histogram) {
      res.push_back(p / total_elements);
    }
    return res;
  }

  size_t PercentileProbe(double Percentile = 0.99) const {
    size_t idx = 0;
    for (double p : ProbeNormalizedHistogram()) {
      if (Percentile > p) {
        Percentile -= p;
        ++idx;
      } else {
        return idx;
      }
    }
    return idx;
  }

  friend std::ostream& operator<<(std::ostream& out, const ProbeStats& s) {
    out << "{AvgRatio:" << s.AvgRatio() << ", MaxRatio:" << s.MaxRatio()
        << ", PercentileRatio:" << s.PercentileRatio()
        << ", MaxProbe:" << s.MaxProbe() << ", Probes=[";
    for (double p : s.ProbeNormalizedHistogram()) {
      out << p << ",";
    }
    out << "]}";

    return out;
  }
};

struct ExpectedStats {
  double avg_ratio;
  double max_ratio;
  std::vector<std::pair<double, double>> pecentile_ratios;
  std::vector<std::pair<double, double>> pecentile_probes;

  friend std::ostream& operator<<(std::ostream& out, const ExpectedStats& s) {
    out << "{AvgRatio:" << s.avg_ratio << ", MaxRatio:" << s.max_ratio
        << ", PercentileRatios: [";
    for (auto el : s.pecentile_ratios) {
      out << el.first << ":" << el.second << ", ";
    }
    out << "], PercentileProbes: [";
    for (auto el : s.pecentile_probes) {
      out << el.first << ":" << el.second << ", ";
    }
    out << "]}";

    return out;
  }
};

void VerifyStats(size_t size, const ExpectedStats& exp,
                 const ProbeStats& stats) {
  EXPECT_LT(stats.AvgRatio(), exp.avg_ratio) << size << " " << stats;
  EXPECT_LT(stats.MaxRatio(), exp.max_ratio) << size << " " << stats;
  for (auto pr : exp.pecentile_ratios) {
    EXPECT_LE(stats.PercentileRatio(pr.first), pr.second)
        << size << " " << pr.first << " " << stats;
  }

  for (auto pr : exp.pecentile_probes) {
    EXPECT_LE(stats.PercentileProbe(pr.first), pr.second)
        << size << " " << pr.first << " " << stats;
  }
}

using ProbeStatsPerSize = std::map<size_t, ProbeStats>;

// Collect total ProbeStats on num_iters iterations of the following algorithm:
// 1. Create new table and reserve it to keys.size() * 2
// 2. Insert all keys xored with seed
// 3. Collect ProbeStats from final table.
ProbeStats CollectProbeStatsOnKeysXoredWithSeed(
    const std::vector<int64_t>& keys, size_t num_iters) {
  const size_t reserve_size = keys.size() * 2;

  ProbeStats stats;

  int64_t seed = 0x71b1a19b907d6e33;
  while (num_iters--) {
    seed = static_cast<int64_t>(static_cast<uint64_t>(seed) * 17 + 13);
    IntTable t1;
    t1.reserve(reserve_size);
    for (const auto& key : keys) {
      t1.emplace(key ^ seed);
    }

    auto probe_histogram = GetHashtableDebugNumProbesHistogram(t1);
    stats.all_probes_histogram.resize(
        std::max(stats.all_probes_histogram.size(), probe_histogram.size()));
    std::transform(probe_histogram.begin(), probe_histogram.end(),
                   stats.all_probes_histogram.begin(),
                   stats.all_probes_histogram.begin(), std::plus<size_t>());

    size_t total_probe_seq_length = 0;
    for (size_t i = 0; i < probe_histogram.size(); ++i) {
      total_probe_seq_length += i * probe_histogram[i];
    }
    stats.single_table_ratios.push_back(total_probe_seq_length * 1.0 /
                                        keys.size());
    t1.erase(t1.begin(), t1.end());
  }
  return stats;
}

ExpectedStats XorSeedExpectedStats() {
  constexpr bool kRandomizesInserts =
#ifdef NDEBUG
      false;
#else   // NDEBUG
      true;
#endif  // NDEBUG

  // The effective load factor is larger in non-opt mode because we insert
  // elements out of order.
  switch (container_internal::Group::kWidth) {
    case 8:
      if (kRandomizesInserts) {
        return {0.05,
                1.0,
                {{0.95, 0.5}},
                {{0.95, 0}, {0.99, 2}, {0.999, 4}, {0.9999, 10}}};
      } else {
        return {0.05,
                2.0,
                {{0.95, 0.1}},
                {{0.95, 0}, {0.99, 2}, {0.999, 4}, {0.9999, 10}}};
      }
    case 16:
      if (kRandomizesInserts) {
        return {0.1,
                2.0,
                {{0.95, 0.1}},
                {{0.95, 0}, {0.99, 1}, {0.999, 8}, {0.9999, 15}}};
      } else {
        return {0.05,
                1.0,
                {{0.95, 0.05}},
                {{0.95, 0}, {0.99, 1}, {0.999, 4}, {0.9999, 10}}};
      }
  }
  LOG(FATAL) << "Unknown Group width";
  return {};
}

// TODO(b/80415403): Figure out why this test is so flaky, esp. on MSVC
TEST(Table, DISABLED_EnsureNonQuadraticTopNXorSeedByProbeSeqLength) {
  ProbeStatsPerSize stats;
  std::vector<size_t> sizes = {Group::kWidth << 5, Group::kWidth << 10};
  for (size_t size : sizes) {
    stats[size] =
        CollectProbeStatsOnKeysXoredWithSeed(CollectBadMergeKeys(size), 200);
  }
  auto expected = XorSeedExpectedStats();
  for (size_t size : sizes) {
    auto& stat = stats[size];
    VerifyStats(size, expected, stat);
    LOG(INFO) << size << " " << stat;
  }
}

// Collect total ProbeStats on num_iters iterations of the following algorithm:
// 1. Create new table
// 2. Select 10% of keys and insert 10 elements key * 17 + j * 13
// 3. Collect ProbeStats from final table
ProbeStats CollectProbeStatsOnLinearlyTransformedKeys(
    const std::vector<int64_t>& keys, size_t num_iters) {
  ProbeStats stats;

  std::random_device rd;
  std::mt19937 rng(rd());
  auto linear_transform = [](size_t x, size_t y) { return x * 17 + y * 13; };
  std::uniform_int_distribution<size_t> dist(0, keys.size() - 1);
  while (num_iters--) {
    IntTable t1;
    size_t num_keys = keys.size() / 10;
    size_t start = dist(rng);
    for (size_t i = 0; i != num_keys; ++i) {
      for (size_t j = 0; j != 10; ++j) {
        t1.emplace(linear_transform(keys[(i + start) % keys.size()], j));
      }
    }

    auto probe_histogram = GetHashtableDebugNumProbesHistogram(t1);
    stats.all_probes_histogram.resize(
        std::max(stats.all_probes_histogram.size(), probe_histogram.size()));
    std::transform(probe_histogram.begin(), probe_histogram.end(),
                   stats.all_probes_histogram.begin(),
                   stats.all_probes_histogram.begin(), std::plus<size_t>());

    size_t total_probe_seq_length = 0;
    for (size_t i = 0; i < probe_histogram.size(); ++i) {
      total_probe_seq_length += i * probe_histogram[i];
    }
    stats.single_table_ratios.push_back(total_probe_seq_length * 1.0 /
                                        t1.size());
    t1.erase(t1.begin(), t1.end());
  }
  return stats;
}

ExpectedStats LinearTransformExpectedStats() {
  constexpr bool kRandomizesInserts =
#ifdef NDEBUG
      false;
#else   // NDEBUG
      true;
#endif  // NDEBUG

  // The effective load factor is larger in non-opt mode because we insert
  // elements out of order.
  switch (container_internal::Group::kWidth) {
    case 8:
      if (kRandomizesInserts) {
        return {0.1,
                0.5,
                {{0.95, 0.3}},
                {{0.95, 0}, {0.99, 1}, {0.999, 8}, {0.9999, 15}}};
      } else {
        return {0.4,
                0.6,
                {{0.95, 0.5}},
                {{0.95, 1}, {0.99, 14}, {0.999, 23}, {0.9999, 26}}};
      }
    case 16:
      if (kRandomizesInserts) {
        return {0.1,
                0.4,
                {{0.95, 0.3}},
                {{0.95, 1}, {0.99, 2}, {0.999, 9}, {0.9999, 15}}};
      } else {
        return {0.05,
                0.2,
                {{0.95, 0.1}},
                {{0.95, 0}, {0.99, 1}, {0.999, 6}, {0.9999, 10}}};
      }
  }
  LOG(FATAL) << "Unknown Group width";
  return {};
}

// TODO(b/80415403): Figure out why this test is so flaky.
TEST(Table, DISABLED_EnsureNonQuadraticTopNLinearTransformByProbeSeqLength) {
  ProbeStatsPerSize stats;
  std::vector<size_t> sizes = {Group::kWidth << 5, Group::kWidth << 10};
  for (size_t size : sizes) {
    stats[size] = CollectProbeStatsOnLinearlyTransformedKeys(
        CollectBadMergeKeys(size), 300);
  }
  auto expected = LinearTransformExpectedStats();
  for (size_t size : sizes) {
    auto& stat = stats[size];
    VerifyStats(size, expected, stat);
    LOG(INFO) << size << " " << stat;
  }
}

TEST(Table, EraseCollision) {
  BadTable t;

  // 1 2 3
  t.emplace(1);
  t.emplace(2);
  t.emplace(3);
  EXPECT_THAT(*t.find(1), 1);
  EXPECT_THAT(*t.find(2), 2);
  EXPECT_THAT(*t.find(3), 3);
  EXPECT_EQ(3, t.size());

  // 1 DELETED 3
  t.erase(t.find(2));
  EXPECT_THAT(*t.find(1), 1);
  EXPECT_TRUE(t.find(2) == t.end());
  EXPECT_THAT(*t.find(3), 3);
  EXPECT_EQ(2, t.size());

  // DELETED DELETED 3
  t.erase(t.find(1));
  EXPECT_TRUE(t.find(1) == t.end());
  EXPECT_TRUE(t.find(2) == t.end());
  EXPECT_THAT(*t.find(3), 3);
  EXPECT_EQ(1, t.size());

  // DELETED DELETED DELETED
  t.erase(t.find(3));
  EXPECT_TRUE(t.find(1) == t.end());
  EXPECT_TRUE(t.find(2) == t.end());
  EXPECT_TRUE(t.find(3) == t.end());
  EXPECT_EQ(0, t.size());
}

TEST(Table, EraseInsertProbing) {
  BadTable t(100);

  // 1 2 3 4
  t.emplace(1);
  t.emplace(2);
  t.emplace(3);
  t.emplace(4);

  // 1 DELETED 3 DELETED
  t.erase(t.find(2));
  t.erase(t.find(4));

  // 1 10 3 11 12
  t.emplace(10);
  t.emplace(11);
  t.emplace(12);

  EXPECT_EQ(5, t.size());
  EXPECT_THAT(t, UnorderedElementsAre(1, 10, 3, 11, 12));
}

TEST(Table, GrowthInfoDeletedBit) {
  BadTable t;
  EXPECT_TRUE(
      RawHashSetTestOnlyAccess::GetCommon(t).growth_info().HasNoDeleted());
  int64_t init_count = static_cast<int64_t>(
      CapacityToGrowth(NormalizeCapacity(Group::kWidth + 1)));
  for (int64_t i = 0; i < init_count; ++i) {
    t.insert(i);
  }
  EXPECT_TRUE(
      RawHashSetTestOnlyAccess::GetCommon(t).growth_info().HasNoDeleted());
  t.erase(0);
  EXPECT_EQ(RawHashSetTestOnlyAccess::CountTombstones(t), 1);
  EXPECT_FALSE(
      RawHashSetTestOnlyAccess::GetCommon(t).growth_info().HasNoDeleted());
  t.rehash(0);
  EXPECT_EQ(RawHashSetTestOnlyAccess::CountTombstones(t), 0);
  EXPECT_TRUE(
      RawHashSetTestOnlyAccess::GetCommon(t).growth_info().HasNoDeleted());
}

TYPED_TEST(SooTest, Clear) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  t.clear();
  EXPECT_TRUE(t.find(0) == t.end());
  auto res = t.emplace(0);
  EXPECT_TRUE(res.second);
  EXPECT_EQ(1, t.size());
  t.clear();
  EXPECT_EQ(0, t.size());
  EXPECT_TRUE(t.find(0) == t.end());
}

TYPED_TEST(SooTest, Swap) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  auto res = t.emplace(0);
  EXPECT_TRUE(res.second);
  EXPECT_EQ(1, t.size());
  TypeParam u;
  t.swap(u);
  EXPECT_EQ(0, t.size());
  EXPECT_EQ(1, u.size());
  EXPECT_TRUE(t.find(0) == t.end());
  EXPECT_THAT(*u.find(0), 0);
}

TYPED_TEST(SooTest, Rehash) {
  TypeParam t;
  EXPECT_TRUE(t.find(0) == t.end());
  t.emplace(0);
  t.emplace(1);
  EXPECT_EQ(2, t.size());
  t.rehash(128);
  EXPECT_EQ(2, t.size());
  EXPECT_THAT(*t.find(0), 0);
  EXPECT_THAT(*t.find(1), 1);
}

TYPED_TEST(SooTest, RehashDoesNotRehashWhenNotNecessary) {
  TypeParam t;
  t.emplace(0);
  t.emplace(1);
  auto* p = &*t.find(0);
  t.rehash(1);
  EXPECT_EQ(p, &*t.find(0));
}

// Following two tests use non-SOO table because they test for 0 capacity.
TEST(Table, RehashZeroDoesNotAllocateOnEmptyTable) {
  NonSooIntTable t;
  t.rehash(0);
  EXPECT_EQ(0, t.bucket_count());
}

TEST(Table, RehashZeroDeallocatesEmptyTable) {
  NonSooIntTable t;
  t.emplace(0);
  t.clear();
  EXPECT_NE(0, t.bucket_count());
  t.rehash(0);
  EXPECT_EQ(0, t.bucket_count());
}

TYPED_TEST(SooTest, RehashZeroForcesRehash) {
  TypeParam t;
  t.emplace(0);
  t.emplace(1);
  auto* p = &*t.find(0);
  t.rehash(0);
  EXPECT_NE(p, &*t.find(0));
}

TEST(Table, ConstructFromInitList) {
  using P = std::pair<std::string, std::string>;
  struct Q {
    operator P() const { return {}; }  // NOLINT
  };
  StringTable t = {P(), Q(), {}, {{}, {}}};
}

TYPED_TEST(SooTest, CopyConstruct) {
  TypeParam t;
  t.emplace(0);
  EXPECT_EQ(1, t.size());
  {
    TypeParam u(t);
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find(0), 0);
  }
  {
    TypeParam u{t};
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find(0), 0);
  }
  {
    TypeParam u = t;
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find(0), 0);
  }
}

TYPED_TEST(SooTest, CopyDifferentSizes) {
  TypeParam t;

  for (int i = 0; i < 100; ++i) {
    t.emplace(i);
    TypeParam c = t;
    for (int j = 0; j <= i; ++j) {
      ASSERT_TRUE(c.find(j) != c.end()) << "i=" << i << " j=" << j;
    }
    // Testing find miss to verify that table is not full.
    ASSERT_TRUE(c.find(-1) == c.end());
  }
}

TYPED_TEST(SooTest, CopyDifferentCapacities) {
  for (int cap = 1; cap < 100; cap = cap * 2 + 1) {
    TypeParam t;
    t.reserve(static_cast<size_t>(cap));
    for (int i = 0; i <= cap; ++i) {
      t.emplace(i);
      if (i != cap && i % 5 != 0) {
        continue;
      }
      TypeParam c = t;
      for (int j = 0; j <= i; ++j) {
        ASSERT_TRUE(c.find(j) != c.end())
            << "cap=" << cap << " i=" << i << " j=" << j;
      }
      // Testing find miss to verify that table is not full.
      ASSERT_TRUE(c.find(-1) == c.end());
    }
  }
}

TEST(Table, CopyConstructWithAlloc) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  StringTable u(t, Alloc<std::pair<std::string, std::string>>());
  EXPECT_EQ(1, u.size());
  EXPECT_THAT(*u.find("a"), Pair("a", "b"));
}

struct ExplicitAllocIntTable
    : raw_hash_set<IntPolicy, hash_default_hash<int64_t>,
                   std::equal_to<int64_t>, Alloc<int64_t>> {
  ExplicitAllocIntTable() = default;
};

TEST(Table, AllocWithExplicitCtor) {
  ExplicitAllocIntTable t;
  EXPECT_EQ(0, t.size());
}

TEST(Table, MoveConstruct) {
  {
    StringTable t;
    t.emplace("a", "b");
    EXPECT_EQ(1, t.size());

    StringTable u(std::move(t));
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find("a"), Pair("a", "b"));
  }
  {
    StringTable t;
    t.emplace("a", "b");
    EXPECT_EQ(1, t.size());

    StringTable u{std::move(t)};
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find("a"), Pair("a", "b"));
  }
  {
    StringTable t;
    t.emplace("a", "b");
    EXPECT_EQ(1, t.size());

    StringTable u = std::move(t);
    EXPECT_EQ(1, u.size());
    EXPECT_THAT(*u.find("a"), Pair("a", "b"));
  }
}

TEST(Table, MoveConstructWithAlloc) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  StringTable u(std::move(t), Alloc<std::pair<std::string, std::string>>());
  EXPECT_EQ(1, u.size());
  EXPECT_THAT(*u.find("a"), Pair("a", "b"));
}

TEST(Table, CopyAssign) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  StringTable u;
  u = t;
  EXPECT_EQ(1, u.size());
  EXPECT_THAT(*u.find("a"), Pair("a", "b"));
}

TEST(Table, CopySelfAssign) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  t = *&t;
  EXPECT_EQ(1, t.size());
  EXPECT_THAT(*t.find("a"), Pair("a", "b"));
}

TEST(Table, MoveAssign) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  StringTable u;
  u = std::move(t);
  EXPECT_EQ(1, u.size());
  EXPECT_THAT(*u.find("a"), Pair("a", "b"));
}

TEST(Table, MoveSelfAssign) {
  StringTable t;
  t.emplace("a", "b");
  EXPECT_EQ(1, t.size());
  t = std::move(*&t);
  if (SwisstableGenerationsEnabled()) {
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_DEATH_IF_SUPPORTED(t.contains("a"), "self-move-assigned");
  }
  // As long as we don't crash, it's fine.
}

TEST(Table, Equality) {
  StringTable t;
  std::vector<std::pair<std::string, std::string>> v = {{"a", "b"},
                                                        {"aa", "bb"}};
  t.insert(std::begin(v), std::end(v));
  StringTable u = t;
  EXPECT_EQ(u, t);
}

TEST(Table, Equality2) {
  StringTable t;
  std::vector<std::pair<std::string, std::string>> v1 = {{"a", "b"},
                                                         {"aa", "bb"}};
  t.insert(std::begin(v1), std::end(v1));
  StringTable u;
  std::vector<std::pair<std::string, std::string>> v2 = {{"a", "a"},
                                                         {"aa", "aa"}};
  u.insert(std::begin(v2), std::end(v2));
  EXPECT_NE(u, t);
}

TEST(Table, Equality3) {
  StringTable t;
  std::vector<std::pair<std::string, std::string>> v1 = {{"b", "b"},
                                                         {"bb", "bb"}};
  t.insert(std::begin(v1), std::end(v1));
  StringTable u;
  std::vector<std::pair<std::string, std::string>> v2 = {{"a", "a"},
                                                         {"aa", "aa"}};
  u.insert(std::begin(v2), std::end(v2));
  EXPECT_NE(u, t);
}

TYPED_TEST(SooTest, NumDeletedRegression) {
  TypeParam t;
  t.emplace(0);
  t.erase(t.find(0));
  // construct over a deleted slot.
  t.emplace(0);
  t.clear();
}

TYPED_TEST(SooTest, FindFullDeletedRegression) {
  TypeParam t;
  for (int i = 0; i < 1000; ++i) {
    t.emplace(i);
    t.erase(t.find(i));
  }
  EXPECT_EQ(0, t.size());
}

TYPED_TEST(SooTest, ReplacingDeletedSlotDoesNotRehash) {
  // We need to disable hashtablez to avoid issues related to SOO and sampling.
  SetHashtablezEnabled(false);

  size_t n;
  {
    // Compute n such that n is the maximum number of elements before rehash.
    TypeParam t;
    t.emplace(0);
    size_t c = t.bucket_count();
    for (n = 1; c == t.bucket_count(); ++n) t.emplace(n);
    --n;
  }
  TypeParam t;
  t.rehash(n);
  const size_t c = t.bucket_count();
  for (size_t i = 0; i != n; ++i) t.emplace(i);
  EXPECT_EQ(c, t.bucket_count()) << "rehashing threshold = " << n;
  t.erase(0);
  t.emplace(0);
  EXPECT_EQ(c, t.bucket_count()) << "rehashing threshold = " << n;
}

TEST(Table, NoThrowMoveConstruct) {
  ASSERT_TRUE(
      std::is_nothrow_copy_constructible<absl::Hash<absl::string_view>>::value);
  ASSERT_TRUE(std::is_nothrow_copy_constructible<
              std::equal_to<absl::string_view>>::value);
  ASSERT_TRUE(std::is_nothrow_copy_constructible<std::allocator<int>>::value);
  EXPECT_TRUE(std::is_nothrow_move_constructible<StringTable>::value);
}

TEST(Table, NoThrowMoveAssign) {
  ASSERT_TRUE(
      std::is_nothrow_move_assignable<absl::Hash<absl::string_view>>::value);
  ASSERT_TRUE(
      std::is_nothrow_move_assignable<std::equal_to<absl::string_view>>::value);
  ASSERT_TRUE(std::is_nothrow_move_assignable<std::allocator<int>>::value);
  ASSERT_TRUE(
      absl::allocator_traits<std::allocator<int>>::is_always_equal::value);
  EXPECT_TRUE(std::is_nothrow_move_assignable<StringTable>::value);
}

TEST(Table, NoThrowSwappable) {
  ASSERT_TRUE(
      container_internal::IsNoThrowSwappable<absl::Hash<absl::string_view>>());
  ASSERT_TRUE(container_internal::IsNoThrowSwappable<
              std::equal_to<absl::string_view>>());
  ASSERT_TRUE(container_internal::IsNoThrowSwappable<std::allocator<int>>());
  EXPECT_TRUE(container_internal::IsNoThrowSwappable<StringTable>());
}

TEST(Table, HeterogeneousLookup) {
  struct Hash {
    size_t operator()(int64_t i) const { return i; }
    size_t operator()(double i) const {
      ADD_FAILURE();
      return i;
    }
  };
  struct Eq {
    bool operator()(int64_t a, int64_t b) const { return a == b; }
    bool operator()(double a, int64_t b) const {
      ADD_FAILURE();
      return a == b;
    }
    bool operator()(int64_t a, double b) const {
      ADD_FAILURE();
      return a == b;
    }
    bool operator()(double a, double b) const {
      ADD_FAILURE();
      return a == b;
    }
  };

  struct THash {
    using is_transparent = void;
    size_t operator()(int64_t i) const { return i; }
    size_t operator()(double i) const { return i; }
  };
  struct TEq {
    using is_transparent = void;
    bool operator()(int64_t a, int64_t b) const { return a == b; }
    bool operator()(double a, int64_t b) const { return a == b; }
    bool operator()(int64_t a, double b) const { return a == b; }
    bool operator()(double a, double b) const { return a == b; }
  };

  raw_hash_set<IntPolicy, Hash, Eq, Alloc<int64_t>> s{0, 1, 2};
  // It will convert to int64_t before the query.
  EXPECT_EQ(1, *s.find(double{1.1}));

  raw_hash_set<IntPolicy, THash, TEq, Alloc<int64_t>> ts{0, 1, 2};
  // It will try to use the double, and fail to find the object.
  EXPECT_TRUE(ts.find(1.1) == ts.end());
}

template <class Table>
using CallFind = decltype(std::declval<Table&>().find(17));

template <class Table>
using CallErase = decltype(std::declval<Table&>().erase(17));

template <class Table>
using CallExtract = decltype(std::declval<Table&>().extract(17));

template <class Table>
using CallPrefetch = decltype(std::declval<Table&>().prefetch(17));

template <class Table>
using CallCount = decltype(std::declval<Table&>().count(17));

template <template <typename> class C, class Table, class = void>
struct VerifyResultOf : std::false_type {};

template <template <typename> class C, class Table>
struct VerifyResultOf<C, Table, absl::void_t<C<Table>>> : std::true_type {};

TEST(Table, HeterogeneousLookupOverloads) {
  using NonTransparentTable =
      raw_hash_set<StringPolicy, absl::Hash<absl::string_view>,
                   std::equal_to<absl::string_view>, std::allocator<int>>;

  EXPECT_FALSE((VerifyResultOf<CallFind, NonTransparentTable>()));
  EXPECT_FALSE((VerifyResultOf<CallErase, NonTransparentTable>()));
  EXPECT_FALSE((VerifyResultOf<CallExtract, NonTransparentTable>()));
  EXPECT_FALSE((VerifyResultOf<CallPrefetch, NonTransparentTable>()));
  EXPECT_FALSE((VerifyResultOf<CallCount, NonTransparentTable>()));

  using TransparentTable =
      raw_hash_set<StringPolicy, hash_default_hash<absl::string_view>,
                   hash_default_eq<absl::string_view>, std::allocator<int>>;

  EXPECT_TRUE((VerifyResultOf<CallFind, TransparentTable>()));
  EXPECT_TRUE((VerifyResultOf<CallErase, TransparentTable>()));
  EXPECT_TRUE((VerifyResultOf<CallExtract, TransparentTable>()));
  EXPECT_TRUE((VerifyResultOf<CallPrefetch, TransparentTable>()));
  EXPECT_TRUE((VerifyResultOf<CallCount, TransparentTable>()));
}

TEST(Iterator, IsDefaultConstructible) {
  StringTable::iterator i;
  EXPECT_TRUE(i == StringTable::iterator());
}

TEST(ConstIterator, IsDefaultConstructible) {
  StringTable::const_iterator i;
  EXPECT_TRUE(i == StringTable::const_iterator());
}

TEST(Iterator, ConvertsToConstIterator) {
  StringTable::iterator i;
  EXPECT_TRUE(i == StringTable::const_iterator());
}

TEST(Iterator, Iterates) {
  IntTable t;
  for (size_t i = 3; i != 6; ++i) EXPECT_TRUE(t.emplace(i).second);
  EXPECT_THAT(t, UnorderedElementsAre(3, 4, 5));
}

TEST(Table, Merge) {
  StringTable t1, t2;
  t1.emplace("0", "-0");
  t1.emplace("1", "-1");
  t2.emplace("0", "~0");
  t2.emplace("2", "~2");

  EXPECT_THAT(t1, UnorderedElementsAre(Pair("0", "-0"), Pair("1", "-1")));
  EXPECT_THAT(t2, UnorderedElementsAre(Pair("0", "~0"), Pair("2", "~2")));

  t1.merge(t2);
  EXPECT_THAT(t1, UnorderedElementsAre(Pair("0", "-0"), Pair("1", "-1"),
                                       Pair("2", "~2")));
  EXPECT_THAT(t2, UnorderedElementsAre(Pair("0", "~0")));
}

TEST(Table, IteratorEmplaceConstructibleRequirement) {
  struct Value {
    explicit Value(absl::string_view view) : value(view) {}
    std::string value;

    bool operator==(const Value& other) const { return value == other.value; }
  };
  struct H {
    size_t operator()(const Value& v) const {
      return absl::Hash<std::string>{}(v.value);
    }
  };

  struct Table : raw_hash_set<ValuePolicy<Value>, H, std::equal_to<Value>,
                              std::allocator<Value>> {
    using Base = typename Table::raw_hash_set;
    using Base::Base;
  };

  std::string input[3]{"A", "B", "C"};

  Table t(std::begin(input), std::end(input));
  EXPECT_THAT(t, UnorderedElementsAre(Value{"A"}, Value{"B"}, Value{"C"}));

  input[0] = "D";
  input[1] = "E";
  input[2] = "F";
  t.insert(std::begin(input), std::end(input));
  EXPECT_THAT(t, UnorderedElementsAre(Value{"A"}, Value{"B"}, Value{"C"},
                                      Value{"D"}, Value{"E"}, Value{"F"}));
}

TEST(Nodes, EmptyNodeType) {
  using node_type = StringTable::node_type;
  node_type n;
  EXPECT_FALSE(n);
  EXPECT_TRUE(n.empty());

  EXPECT_TRUE((std::is_same<node_type::allocator_type,
                            StringTable::allocator_type>::value));
}

TEST(Nodes, ExtractInsert) {
  constexpr char k0[] = "Very long string zero.";
  constexpr char k1[] = "Very long string one.";
  constexpr char k2[] = "Very long string two.";
  StringTable t = {{k0, ""}, {k1, ""}, {k2, ""}};
  EXPECT_THAT(t,
              UnorderedElementsAre(Pair(k0, ""), Pair(k1, ""), Pair(k2, "")));

  auto node = t.extract(k0);
  EXPECT_THAT(t, UnorderedElementsAre(Pair(k1, ""), Pair(k2, "")));
  EXPECT_TRUE(node);
  EXPECT_FALSE(node.empty());

  StringTable t2;
  StringTable::insert_return_type res = t2.insert(std::move(node));
  EXPECT_TRUE(res.inserted);
  EXPECT_THAT(*res.position, Pair(k0, ""));
  EXPECT_FALSE(res.node);
  EXPECT_THAT(t2, UnorderedElementsAre(Pair(k0, "")));

  // Not there.
  EXPECT_THAT(t, UnorderedElementsAre(Pair(k1, ""), Pair(k2, "")));
  node = t.extract("Not there!");
  EXPECT_THAT(t, UnorderedElementsAre(Pair(k1, ""), Pair(k2, "")));
  EXPECT_FALSE(node);

  // Inserting nothing.
  res = t2.insert(std::move(node));
  EXPECT_FALSE(res.inserted);
  EXPECT_EQ(res.position, t2.end());
  EXPECT_FALSE(res.node);
  EXPECT_THAT(t2, UnorderedElementsAre(Pair(k0, "")));

  t.emplace(k0, "1");
  node = t.extract(k0);

  // Insert duplicate.
  res = t2.insert(std::move(node));
  EXPECT_FALSE(res.inserted);
  EXPECT_THAT(*res.position, Pair(k0, ""));
  EXPECT_TRUE(res.node);
  EXPECT_FALSE(node);  // NOLINT(bugprone-use-after-move)
}

TYPED_TEST(SooTest, HintInsert) {
  TypeParam t = {1, 2, 3};
  auto node = t.extract(1);
  EXPECT_THAT(t, UnorderedElementsAre(2, 3));
  auto it = t.insert(t.begin(), std::move(node));
  EXPECT_THAT(t, UnorderedElementsAre(1, 2, 3));
  EXPECT_EQ(*it, 1);
  EXPECT_FALSE(node);  // NOLINT(bugprone-use-after-move)

  node = t.extract(2);
  EXPECT_THAT(t, UnorderedElementsAre(1, 3));
  // reinsert 2 to make the next insert fail.
  t.insert(2);
  EXPECT_THAT(t, UnorderedElementsAre(1, 2, 3));
  it = t.insert(t.begin(), std::move(node));
  EXPECT_EQ(*it, 2);
  // The node was not emptied by the insert call.
  EXPECT_TRUE(node);  // NOLINT(bugprone-use-after-move)
}

template <typename T>
T MakeSimpleTable(size_t size) {
  T t;
  while (t.size() < size) t.insert(t.size());
  return t;
}

template <typename T>
std::vector<int> OrderOfIteration(const T& t) {
  std::vector<int> res;
  for (auto i : t) res.push_back(static_cast<int>(i));
  return res;
}

// These IterationOrderChanges tests depend on non-deterministic behavior.
// We are injecting non-determinism from the pointer of the table, but do so in
// a way that only the page matters. We have to retry enough times to make sure
// we are touching different memory pages to cause the ordering to change.
// We also need to keep the old tables around to avoid getting the same memory
// blocks over and over.
TYPED_TEST(SooTest, IterationOrderChangesByInstance) {
  for (size_t size : {2, 6, 12, 20}) {
    const auto reference_table = MakeSimpleTable<TypeParam>(size);
    const auto reference = OrderOfIteration(reference_table);

    std::vector<TypeParam> tables;
    bool found_difference = false;
    for (int i = 0; !found_difference && i < 5000; ++i) {
      tables.push_back(MakeSimpleTable<TypeParam>(size));
      found_difference = OrderOfIteration(tables.back()) != reference;
    }
    if (!found_difference) {
      FAIL()
          << "Iteration order remained the same across many attempts with size "
          << size;
    }
  }
}

TYPED_TEST(SooTest, IterationOrderChangesOnRehash) {
  // We test different sizes with many small numbers, because small table
  // resize has a different codepath.
  // Note: iteration order for size() <= 1 is always the same.
  for (size_t size : std::vector<size_t>{2, 3, 6, 7, 12, 15, 20, 50}) {
    for (size_t rehash_size : {
             size_t{0},  // Force rehash is guaranteed.
             size * 10   // Rehash to the larger capacity is guaranteed.
         }) {
      std::vector<TypeParam> garbage;
      bool ok = false;
      for (int i = 0; i < 5000; ++i) {
        auto t = MakeSimpleTable<TypeParam>(size);
        const auto reference = OrderOfIteration(t);
        // Force rehash.
        t.rehash(rehash_size);
        auto trial = OrderOfIteration(t);
        if (trial != reference) {
          // We are done.
          ok = true;
          break;
        }
        garbage.push_back(std::move(t));
      }
      EXPECT_TRUE(ok)
          << "Iteration order remained the same across many attempts " << size
          << "->" << rehash_size << ".";
    }
  }
}

// Verify that pointers are invalidated as soon as a second element is inserted.
// This prevents dependency on pointer stability on small tables.
TYPED_TEST(SooTest, UnstablePointers) {
  // We need to disable hashtablez to avoid issues related to SOO and sampling.
  SetHashtablezEnabled(false);

  TypeParam table;

  const auto addr = [&](int i) {
    return reinterpret_cast<uintptr_t>(&*table.find(i));
  };

  table.insert(0);
  const uintptr_t old_ptr = addr(0);

  // This causes a rehash.
  table.insert(1);

  EXPECT_NE(old_ptr, addr(0));
}

TEST(TableDeathTest, InvalidIteratorAsserts) {
  if (!IsAssertEnabled() && !SwisstableGenerationsEnabled())
    GTEST_SKIP() << "Assertions not enabled.";

  NonSooIntTable t;
  // Extra simple "regexp" as regexp support is highly varied across platforms.
  EXPECT_DEATH_IF_SUPPORTED(t.erase(t.end()),
                            "erase.* called on end.. iterator.");
  typename NonSooIntTable::iterator iter;
  EXPECT_DEATH_IF_SUPPORTED(
      ++iter, "operator.* called on default-constructed iterator.");
  t.insert(0);
  iter = t.begin();
  t.erase(iter);
  const char* const kErasedDeathMessage =
      SwisstableGenerationsEnabled()
          ? "operator.* called on invalid iterator.*was likely erased"
          : "operator.* called on invalid iterator.*might have been "
            "erased.*config=asan";
  EXPECT_DEATH_IF_SUPPORTED(++iter, kErasedDeathMessage);
}

TEST(TableDeathTest, InvalidIteratorAssertsSoo) {
  if (!IsAssertEnabled() && !SwisstableGenerationsEnabled())
    GTEST_SKIP() << "Assertions not enabled.";

  SooIntTable t;
  // Extra simple "regexp" as regexp support is highly varied across platforms.
  EXPECT_DEATH_IF_SUPPORTED(t.erase(t.end()),
                            "erase.* called on end.. iterator.");
  typename SooIntTable::iterator iter;
  EXPECT_DEATH_IF_SUPPORTED(
      ++iter, "operator.* called on default-constructed iterator.");

  // We can't detect the erased iterator case as invalid in SOO mode because
  // the control is static constant.
}

// Invalid iterator use can trigger use-after-free in asan/hwasan,
// use-of-uninitialized-value in msan, or invalidated iterator assertions.
constexpr const char* kInvalidIteratorDeathMessage =
    "use-after-free|use-of-uninitialized-value|invalidated "
    "iterator|Invalid iterator|invalid iterator";

// MSVC doesn't support | in regex.
#if defined(_MSC_VER)
constexpr bool kMsvc = true;
#else
constexpr bool kMsvc = false;
#endif

TYPED_TEST(SooTest, IteratorInvalidAssertsEqualityOperator) {
  if (!IsAssertEnabled() && !SwisstableGenerationsEnabled())
    GTEST_SKIP() << "Assertions not enabled.";

  TypeParam t;
  t.insert(1);
  t.insert(2);
  t.insert(3);
  auto iter1 = t.begin();
  auto iter2 = std::next(iter1);
  ASSERT_NE(iter1, t.end());
  ASSERT_NE(iter2, t.end());
  t.erase(iter1);
  // Extra simple "regexp" as regexp support is highly varied across platforms.
  const char* const kErasedDeathMessage =
      SwisstableGenerationsEnabled()
          ? "Invalid iterator comparison.*was likely erased"
          : "Invalid iterator comparison.*might have been erased.*config=asan";
  EXPECT_DEATH_IF_SUPPORTED(void(iter1 == iter2), kErasedDeathMessage);
  EXPECT_DEATH_IF_SUPPORTED(void(iter2 != iter1), kErasedDeathMessage);
  t.erase(iter2);
  EXPECT_DEATH_IF_SUPPORTED(void(iter1 == iter2), kErasedDeathMessage);

  TypeParam t1, t2;
  t1.insert(0);
  t2.insert(0);
  iter1 = t1.begin();
  iter2 = t2.begin();
  const char* const kContainerDiffDeathMessage =
      SwisstableGenerationsEnabled()
          ? "Invalid iterator comparison.*iterators from different.* hashtables"
          : "Invalid iterator comparison.*may be from different "
            ".*containers.*config=asan";
  EXPECT_DEATH_IF_SUPPORTED(void(iter1 == iter2), kContainerDiffDeathMessage);
  EXPECT_DEATH_IF_SUPPORTED(void(iter2 == iter1), kContainerDiffDeathMessage);
}

TYPED_TEST(SooTest, IteratorInvalidAssertsEqualityOperatorRehash) {
  if (!IsAssertEnabled() && !SwisstableGenerationsEnabled())
    GTEST_SKIP() << "Assertions not enabled.";
  if (kMsvc) GTEST_SKIP() << "MSVC doesn't support | in regex.";
#ifdef ABSL_HAVE_THREAD_SANITIZER
  GTEST_SKIP() << "ThreadSanitizer test runs fail on use-after-free even in "
                  "EXPECT_DEATH.";
#endif

  TypeParam t;
  t.insert(0);
  auto iter = t.begin();

  // Trigger a rehash in t.
  for (int i = 0; i < 10; ++i) t.insert(i);

  const char* const kRehashedDeathMessage =
      SwisstableGenerationsEnabled()
          ? kInvalidIteratorDeathMessage
          : "Invalid iterator comparison.*might have rehashed.*config=asan";
  EXPECT_DEATH_IF_SUPPORTED(void(iter == t.begin()), kRehashedDeathMessage);
}

#if defined(ABSL_INTERNAL_HASHTABLEZ_SAMPLE)
template <typename T>
class RawHashSamplerTest : public testing::Test {};

using RawHashSamplerTestTypes = ::testing::Types<SooIntTable, NonSooIntTable>;
TYPED_TEST_SUITE(RawHashSamplerTest, RawHashSamplerTestTypes);

TYPED_TEST(RawHashSamplerTest, Sample) {
  constexpr bool soo_enabled = std::is_same<SooIntTable, TypeParam>::value;
  // Enable the feature even if the prod default is off.
  SetHashtablezEnabled(true);
  SetHashtablezSampleParameter(100);  // Sample ~1% of tables.

  auto& sampler = GlobalHashtablezSampler();
  size_t start_size = 0;
  absl::flat_hash_set<const HashtablezInfo*> preexisting_info;
  start_size += sampler.Iterate([&](const HashtablezInfo& info) {
    preexisting_info.insert(&info);
    ++start_size;
  });

  std::vector<TypeParam> tables;
  for (int i = 0; i < 1000000; ++i) {
    tables.emplace_back();

    const bool do_reserve = (i % 10 > 5);
    const bool do_rehash = !do_reserve && (i % 10 > 0);

    if (do_reserve) {
      // Don't reserve on all tables.
      tables.back().reserve(10 * (i % 10));
    }

    tables.back().insert(1);
    tables.back().insert(i % 5);

    if (do_rehash) {
      // Rehash some other tables.
      tables.back().rehash(10 * (i % 10));
    }
  }
  size_t end_size = 0;
  absl::flat_hash_map<size_t, int> observed_checksums;
  absl::flat_hash_map<ssize_t, int> reservations;
  end_size += sampler.Iterate([&](const HashtablezInfo& info) {
    ++end_size;
    if (preexisting_info.contains(&info)) return;
    observed_checksums[info.hashes_bitwise_xor.load(
        std::memory_order_relaxed)]++;
    reservations[info.max_reserve.load(std::memory_order_relaxed)]++;
    EXPECT_EQ(info.inline_element_size, sizeof(typename TypeParam::value_type));
    EXPECT_EQ(info.key_size, sizeof(typename TypeParam::key_type));
    EXPECT_EQ(info.value_size, sizeof(typename TypeParam::value_type));

    if (soo_enabled) {
      EXPECT_EQ(info.soo_capacity, SooCapacity());
    } else {
      EXPECT_EQ(info.soo_capacity, 0);
    }
  });

  // Expect that we sampled at the requested sampling rate of ~1%.
  EXPECT_NEAR((end_size - start_size) / static_cast<double>(tables.size()),
              0.01, 0.005);
  EXPECT_EQ(observed_checksums.size(), 5);
  for (const auto& [_, count] : observed_checksums) {
    EXPECT_NEAR((100 * count) / static_cast<double>(tables.size()), 0.2, 0.05);
  }

  EXPECT_EQ(reservations.size(), 10);
  for (const auto& [reservation, count] : reservations) {
    EXPECT_GE(reservation, 0);
    EXPECT_LT(reservation, 100);

    EXPECT_NEAR((100 * count) / static_cast<double>(tables.size()), 0.1, 0.05)
        << reservation;
  }
}

std::vector<const HashtablezInfo*> SampleSooMutation(
    absl::FunctionRef<void(SooIntTable&)> mutate_table) {
  // Enable the feature even if the prod default is off.
  SetHashtablezEnabled(true);
  SetHashtablezSampleParameter(100);  // Sample ~1% of tables.

  auto& sampler = GlobalHashtablezSampler();
  size_t start_size = 0;
  absl::flat_hash_set<const HashtablezInfo*> preexisting_info;
  start_size += sampler.Iterate([&](const HashtablezInfo& info) {
    preexisting_info.insert(&info);
    ++start_size;
  });

  std::vector<SooIntTable> tables;
  for (int i = 0; i < 1000000; ++i) {
    tables.emplace_back();
    mutate_table(tables.back());
  }
  size_t end_size = 0;
  std::vector<const HashtablezInfo*> infos;
  end_size += sampler.Iterate([&](const HashtablezInfo& info) {
    ++end_size;
    if (preexisting_info.contains(&info)) return;
    infos.push_back(&info);
  });

  // Expect that we sampled at the requested sampling rate of ~1%.
  EXPECT_NEAR((end_size - start_size) / static_cast<double>(tables.size()),
              0.01, 0.005);
  return infos;
}

TEST(RawHashSamplerTest, SooTableInsertToEmpty) {
  if (SooIntTable().capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }
  std::vector<const HashtablezInfo*> infos =
      SampleSooMutation([](SooIntTable& t) { t.insert(1); });

  for (const HashtablezInfo* info : infos) {
    ASSERT_EQ(info->inline_element_size,
              sizeof(typename SooIntTable::value_type));
    ASSERT_EQ(info->soo_capacity, SooCapacity());
    ASSERT_EQ(info->capacity, NextCapacity(SooCapacity()));
    ASSERT_EQ(info->size, 1);
    ASSERT_EQ(info->max_reserve, 0);
    ASSERT_EQ(info->num_erases, 0);
    ASSERT_EQ(info->max_probe_length, 0);
    ASSERT_EQ(info->total_probe_length, 0);
  }
}

TEST(RawHashSamplerTest, SooTableReserveToEmpty) {
  if (SooIntTable().capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }
  std::vector<const HashtablezInfo*> infos =
      SampleSooMutation([](SooIntTable& t) { t.reserve(100); });

  for (const HashtablezInfo* info : infos) {
    ASSERT_EQ(info->inline_element_size,
              sizeof(typename SooIntTable::value_type));
    ASSERT_EQ(info->soo_capacity, SooCapacity());
    ASSERT_GE(info->capacity, 100);
    ASSERT_EQ(info->size, 0);
    ASSERT_EQ(info->max_reserve, 100);
    ASSERT_EQ(info->num_erases, 0);
    ASSERT_EQ(info->max_probe_length, 0);
    ASSERT_EQ(info->total_probe_length, 0);
  }
}

// This tests that reserve on a full SOO table doesn't incorrectly result in new
// (over-)sampling.
TEST(RawHashSamplerTest, SooTableReserveToFullSoo) {
  if (SooIntTable().capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }
  std::vector<const HashtablezInfo*> infos =
      SampleSooMutation([](SooIntTable& t) {
        t.insert(1);
        t.reserve(100);
      });

  for (const HashtablezInfo* info : infos) {
    ASSERT_EQ(info->inline_element_size,
              sizeof(typename SooIntTable::value_type));
    ASSERT_EQ(info->soo_capacity, SooCapacity());
    ASSERT_GE(info->capacity, 100);
    ASSERT_EQ(info->size, 1);
    ASSERT_EQ(info->max_reserve, 100);
    ASSERT_EQ(info->num_erases, 0);
    ASSERT_EQ(info->max_probe_length, 0);
    ASSERT_EQ(info->total_probe_length, 0);
  }
}

// This tests that rehash(0) on a sampled table with size that fits in SOO
// doesn't incorrectly result in losing sampling.
TEST(RawHashSamplerTest, SooTableRehashShrinkWhenSizeFitsInSoo) {
  if (SooIntTable().capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }
  std::vector<const HashtablezInfo*> infos =
      SampleSooMutation([](SooIntTable& t) {
        t.reserve(100);
        t.insert(1);
        EXPECT_GE(t.capacity(), 100);
        t.rehash(0);
      });

  for (const HashtablezInfo* info : infos) {
    ASSERT_EQ(info->inline_element_size,
              sizeof(typename SooIntTable::value_type));
    ASSERT_EQ(info->soo_capacity, SooCapacity());
    ASSERT_EQ(info->capacity, NextCapacity(SooCapacity()));
    ASSERT_EQ(info->size, 1);
    ASSERT_EQ(info->max_reserve, 100);
    ASSERT_EQ(info->num_erases, 0);
    ASSERT_EQ(info->max_probe_length, 0);
    ASSERT_EQ(info->total_probe_length, 0);
  }
}
#endif  // ABSL_INTERNAL_HASHTABLEZ_SAMPLE

TEST(RawHashSamplerTest, DoNotSampleCustomAllocators) {
  // Enable the feature even if the prod default is off.
  SetHashtablezEnabled(true);
  SetHashtablezSampleParameter(100);  // Sample ~1% of tables.

  auto& sampler = GlobalHashtablezSampler();
  size_t start_size = 0;
  start_size += sampler.Iterate([&](const HashtablezInfo&) { ++start_size; });

  std::vector<CustomAllocIntTable> tables;
  for (int i = 0; i < 1000000; ++i) {
    tables.emplace_back();
    tables.back().insert(1);
  }
  size_t end_size = 0;
  end_size += sampler.Iterate([&](const HashtablezInfo&) { ++end_size; });

  EXPECT_NEAR((end_size - start_size) / static_cast<double>(tables.size()),
              0.00, 0.001);
}

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
template <class TableType>
class SanitizerTest : public testing::Test {};

using SanitizerTableTypes = ::testing::Types<IntTable, TransferableIntTable>;
TYPED_TEST_SUITE(SanitizerTest, SanitizerTableTypes);

TYPED_TEST(SanitizerTest, PoisoningUnused) {
  TypeParam t;
  for (size_t reserve_size = 2; reserve_size < 1024;
       reserve_size = reserve_size * 3 / 2) {
    t.reserve(reserve_size);
    // Insert something to force an allocation.
    int64_t& v = *t.insert(0).first;

    // Make sure there is something to test.
    ASSERT_GT(t.capacity(), 1);

    int64_t* slots = RawHashSetTestOnlyAccess::GetSlots(t);
    for (size_t i = 0; i < t.capacity(); ++i) {
      EXPECT_EQ(slots + i != &v, __asan_address_is_poisoned(slots + i)) << i;
    }
  }
}

// TODO(b/289225379): poison inline space when empty SOO.
TEST(Sanitizer, PoisoningOnErase) {
  NonSooIntTable t;
  auto& v = *t.insert(0).first;

  EXPECT_FALSE(__asan_address_is_poisoned(&v));
  t.erase(0);
  EXPECT_TRUE(__asan_address_is_poisoned(&v));
}
#endif  // ABSL_HAVE_ADDRESS_SANITIZER

template <typename T>
class AlignOneTest : public ::testing::Test {};
using AlignOneTestTypes =
    ::testing::Types<Uint8Table, MinimumAlignmentUint8Table>;
TYPED_TEST_SUITE(AlignOneTest, AlignOneTestTypes);

TYPED_TEST(AlignOneTest, AlignOne) {
  // We previously had a bug in which we were copying a control byte over the
  // first slot when alignof(value_type) is 1. We test repeated
  // insertions/erases and verify that the behavior is correct.
  TypeParam t;
  std::unordered_set<uint8_t> verifier;  // NOLINT

  // Do repeated insertions/erases from the table.
  for (int64_t i = 0; i < 100000; ++i) {
    SCOPED_TRACE(i);
    const uint8_t u = (i * -i) & 0xFF;
    auto it = t.find(u);
    auto verifier_it = verifier.find(u);
    if (it == t.end()) {
      ASSERT_EQ(verifier_it, verifier.end());
      t.insert(u);
      verifier.insert(u);
    } else {
      ASSERT_NE(verifier_it, verifier.end());
      t.erase(it);
      verifier.erase(verifier_it);
    }
  }

  EXPECT_EQ(t.size(), verifier.size());
  for (uint8_t u : t) {
    EXPECT_EQ(verifier.count(u), 1);
  }
}

TEST(Iterator, InvalidUseCrashesWithSanitizers) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";
  if (kMsvc) GTEST_SKIP() << "MSVC doesn't support | in regexp.";

  NonSooIntTable t;
  // Start with 1 element so that `it` is never an end iterator.
  t.insert(-1);
  for (int i = 0; i < 10; ++i) {
    auto it = t.begin();
    t.insert(i);
    EXPECT_DEATH_IF_SUPPORTED(*it, kInvalidIteratorDeathMessage);
    EXPECT_DEATH_IF_SUPPORTED(void(it == t.begin()),
                              kInvalidIteratorDeathMessage);
  }
}

TEST(Iterator, InvalidUseWithReserveCrashesWithSanitizers) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";
  if (kMsvc) GTEST_SKIP() << "MSVC doesn't support | in regexp.";

  IntTable t;
  t.reserve(10);
  t.insert(0);
  auto it = t.begin();
  // Reserved growth can't rehash.
  for (int i = 1; i < 10; ++i) {
    t.insert(i);
    EXPECT_EQ(*it, 0);
  }
  // ptr will become invalidated on rehash.
  const int64_t* ptr = &*it;
  (void)ptr;

  // erase decreases size but does not decrease reserved growth so the next
  // insertion still invalidates iterators.
  t.erase(0);
  // The first insert after reserved growth is 0 is guaranteed to rehash when
  // generations are enabled.
  t.insert(10);
  EXPECT_DEATH_IF_SUPPORTED(*it, kInvalidIteratorDeathMessage);
  EXPECT_DEATH_IF_SUPPORTED(void(it == t.begin()),
                            kInvalidIteratorDeathMessage);
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH_IF_SUPPORTED(std::cout << *ptr, "heap-use-after-free");
#endif
}

TEST(Iterator, InvalidUseWithMoveCrashesWithSanitizers) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";
  if (kMsvc) GTEST_SKIP() << "MSVC doesn't support | in regexp.";

  NonSooIntTable t1, t2;
  t1.insert(1);
  auto it = t1.begin();
  // ptr will become invalidated on rehash.
  const auto* ptr = &*it;
  (void)ptr;

  t2 = std::move(t1);
  EXPECT_DEATH_IF_SUPPORTED(*it, kInvalidIteratorDeathMessage);
  EXPECT_DEATH_IF_SUPPORTED(void(it == t2.begin()),
                            kInvalidIteratorDeathMessage);
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  EXPECT_DEATH_IF_SUPPORTED(std::cout << **ptr, "heap-use-after-free");
#endif
}

TYPED_TEST(SooTest, ReservedGrowthUpdatesWhenTableDoesntGrow) {
  TypeParam t;
  for (int i = 0; i < 8; ++i) t.insert(i);
  // Want to insert twice without invalidating iterators so reserve.
  const size_t cap = t.capacity();
  t.reserve(t.size() + 2);
  // We want to be testing the case in which the reserve doesn't grow the table.
  ASSERT_EQ(cap, t.capacity());
  auto it = t.find(0);
  t.insert(100);
  t.insert(200);
  // `it` shouldn't have been invalidated.
  EXPECT_EQ(*it, 0);
}

template <class TableType>
class InstanceTrackerTest : public testing::Test {};

using ::absl::test_internal::CopyableMovableInstance;
using ::absl::test_internal::InstanceTracker;

struct InstanceTrackerHash {
  size_t operator()(const CopyableMovableInstance& t) const {
    return absl::HashOf(t.value());
  }
};

using InstanceTrackerTableTypes = ::testing::Types<
    absl::node_hash_set<CopyableMovableInstance, InstanceTrackerHash>,
    absl::flat_hash_set<CopyableMovableInstance, InstanceTrackerHash>>;
TYPED_TEST_SUITE(InstanceTrackerTest, InstanceTrackerTableTypes);

TYPED_TEST(InstanceTrackerTest, EraseIfAll) {
  using Table = TypeParam;
  InstanceTracker tracker;
  for (int size = 0; size < 100; ++size) {
    Table t;
    for (int i = 0; i < size; ++i) {
      t.emplace(i);
    }
    absl::erase_if(t, [](const auto&) { return true; });
    ASSERT_EQ(t.size(), 0);
  }
  EXPECT_EQ(tracker.live_instances(), 0);
}

TYPED_TEST(InstanceTrackerTest, EraseIfNone) {
  using Table = TypeParam;
  InstanceTracker tracker;
  {
    Table t;
    for (size_t size = 0; size < 100; ++size) {
      absl::erase_if(t, [](const auto&) { return false; });
      ASSERT_EQ(t.size(), size);
      t.emplace(size);
    }
  }
  EXPECT_EQ(tracker.live_instances(), 0);
}

TYPED_TEST(InstanceTrackerTest, EraseIfPartial) {
  using Table = TypeParam;
  InstanceTracker tracker;
  for (int mod : {0, 1}) {
    for (int size = 0; size < 100; ++size) {
      SCOPED_TRACE(absl::StrCat(mod, " ", size));
      Table t;
      std::vector<CopyableMovableInstance> expected;
      for (int i = 0; i < size; ++i) {
        t.emplace(i);
        if (i % 2 != mod) {
          expected.emplace_back(i);
        }
      }
      absl::erase_if(t, [mod](const auto& x) { return x.value() % 2 == mod; });
      ASSERT_THAT(t, testing::UnorderedElementsAreArray(expected));
    }
  }
  EXPECT_EQ(tracker.live_instances(), 0);
}

TYPED_TEST(SooTest, EraseIfAll) {
  auto pred = [](const auto&) { return true; };
  for (int size = 0; size < 100; ++size) {
    TypeParam t;
    for (int i = 0; i < size; ++i) t.insert(i);
    absl::container_internal::EraseIf(pred, &t);
    ASSERT_EQ(t.size(), 0);
  }
}

TYPED_TEST(SooTest, EraseIfNone) {
  auto pred = [](const auto&) { return false; };
  TypeParam t;
  for (size_t size = 0; size < 100; ++size) {
    absl::container_internal::EraseIf(pred, &t);
    ASSERT_EQ(t.size(), size);
    t.insert(size);
  }
}

TYPED_TEST(SooTest, EraseIfPartial) {
  for (int mod : {0, 1}) {
    auto pred = [&](const auto& x) {
      return static_cast<int64_t>(x) % 2 == mod;
    };
    for (int size = 0; size < 100; ++size) {
      SCOPED_TRACE(absl::StrCat(mod, " ", size));
      TypeParam t;
      std::vector<int64_t> expected;
      for (int i = 0; i < size; ++i) {
        t.insert(i);
        if (i % 2 != mod) {
          expected.push_back(i);
        }
      }
      absl::container_internal::EraseIf(pred, &t);
      ASSERT_THAT(t, testing::UnorderedElementsAreArray(expected));
    }
  }
}

TYPED_TEST(SooTest, ForEach) {
  TypeParam t;
  std::vector<int64_t> expected;
  for (int size = 0; size < 100; ++size) {
    SCOPED_TRACE(size);
    {
      SCOPED_TRACE("mutable iteration");
      std::vector<int64_t> actual;
      auto f = [&](auto& x) { actual.push_back(static_cast<int64_t>(x)); };
      absl::container_internal::ForEach(f, &t);
      ASSERT_THAT(actual, testing::UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("const iteration");
      std::vector<int64_t> actual;
      auto f = [&](auto& x) {
        static_assert(
            std::is_const<std::remove_reference_t<decltype(x)>>::value,
            "no mutable values should be passed to const ForEach");
        actual.push_back(static_cast<int64_t>(x));
      };
      const auto& ct = t;
      absl::container_internal::ForEach(f, &ct);
      ASSERT_THAT(actual, testing::UnorderedElementsAreArray(expected));
    }
    t.insert(size);
    expected.push_back(size);
  }
}

TEST(Table, ForEachMutate) {
  StringTable t;
  using ValueType = StringTable::value_type;
  std::vector<ValueType> expected;
  for (int size = 0; size < 100; ++size) {
    SCOPED_TRACE(size);
    std::vector<ValueType> actual;
    auto f = [&](ValueType& x) {
      actual.push_back(x);
      x.second += "a";
    };
    absl::container_internal::ForEach(f, &t);
    ASSERT_THAT(actual, testing::UnorderedElementsAreArray(expected));
    for (ValueType& v : expected) {
      v.second += "a";
    }
    ASSERT_THAT(t, testing::UnorderedElementsAreArray(expected));
    t.emplace(std::to_string(size), std::to_string(size));
    expected.emplace_back(std::to_string(size), std::to_string(size));
  }
}

TYPED_TEST(SooTest, EraseIfReentryDeath) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  auto erase_if_with_removal_reentrance = [](size_t reserve_size) {
    TypeParam t;
    t.reserve(reserve_size);
    int64_t first_value = -1;
    t.insert(1024);
    t.insert(5078);
    auto pred = [&](const auto& x) {
      if (first_value == -1) {
        first_value = static_cast<int64_t>(x);
        return false;
      }
      // We erase on second call to `pred` to reduce the chance that assertion
      // will happen in IterateOverFullSlots.
      t.erase(first_value);
      return true;
    };
    absl::container_internal::EraseIf(pred, &t);
  };
  // Removal will likely happen in a different group.
  EXPECT_DEATH_IF_SUPPORTED(erase_if_with_removal_reentrance(1024 * 16),
                            "hash table was modified unexpectedly");
  // Removal will happen in the same group.
  EXPECT_DEATH_IF_SUPPORTED(
      erase_if_with_removal_reentrance(CapacityToGrowth(Group::kWidth - 1)),
      "hash table was modified unexpectedly");
}

// This test is useful to test soo branch.
TYPED_TEST(SooTest, EraseIfReentrySingleElementDeath) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  auto erase_if_with_removal_reentrance = []() {
    TypeParam t;
    t.insert(1024);
    auto pred = [&](const auto& x) {
      // We erase ourselves in order to confuse the erase_if.
      t.erase(static_cast<int64_t>(x));
      return false;
    };
    absl::container_internal::EraseIf(pred, &t);
  };
  EXPECT_DEATH_IF_SUPPORTED(erase_if_with_removal_reentrance(),
                            "hash table was modified unexpectedly");
}

TEST(Table, EraseBeginEndResetsReservedGrowth) {
  bool frozen = false;
  BadHashFreezableIntTable t{FreezableAlloc<int64_t>(&frozen)};
  t.reserve(100);
  const size_t cap = t.capacity();
  frozen = true;  // no further allocs allowed

  for (int i = 0; i < 10; ++i) {
    // Create a long run (hash function returns constant).
    for (int j = 0; j < 100; ++j) t.insert(j);
    // Erase elements from the middle of the long run, which creates
    // tombstones.
    for (int j = 30; j < 60; ++j) t.erase(j);
    EXPECT_EQ(t.size(), 70);
    EXPECT_EQ(t.capacity(), cap);
    ASSERT_EQ(RawHashSetTestOnlyAccess::CountTombstones(t), 30);

    t.erase(t.begin(), t.end());

    EXPECT_EQ(t.size(), 0);
    EXPECT_EQ(t.capacity(), cap);
    ASSERT_EQ(RawHashSetTestOnlyAccess::CountTombstones(t), 0);
  }
}

TEST(Table, GenerationInfoResetsOnClear) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";
  if (kMsvc) GTEST_SKIP() << "MSVC doesn't support | in regexp.";

  NonSooIntTable t;
  for (int i = 0; i < 1000; ++i) t.insert(i);
  t.reserve(t.size() + 100);

  t.clear();

  t.insert(0);
  auto it = t.begin();
  t.insert(1);
  EXPECT_DEATH_IF_SUPPORTED(*it, kInvalidIteratorDeathMessage);
}

TEST(Table, InvalidReferenceUseCrashesWithSanitizers) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  GTEST_SKIP() << "MSan fails to detect some of these rehashes.";
#endif

  NonSooIntTable t;
  t.insert(0);
  // Rehashing is guaranteed on every insertion while capacity is less than
  // RehashProbabilityConstant().
  int i = 0;
  while (t.capacity() <= RehashProbabilityConstant()) {
    // ptr will become invalidated on rehash.
    const auto* ptr = &*t.begin();
    t.insert(++i);
    EXPECT_DEATH_IF_SUPPORTED(std::cout << **ptr, "use-after-free") << i;
  }
}

TEST(Iterator, InvalidComparisonDifferentTables) {
  if (!SwisstableGenerationsEnabled()) GTEST_SKIP() << "Generations disabled.";

  NonSooIntTable t1, t2;
  NonSooIntTable::iterator default_constructed_iter;
  // We randomly use one of N empty generations for generations from empty
  // hashtables. In general, we won't always detect when iterators from
  // different empty hashtables are compared, but in this test case, we
  // should deterministically detect the error due to our randomness yielding
  // consecutive random generations.
  EXPECT_DEATH_IF_SUPPORTED(void(t1.end() == t2.end()),
                            "Invalid iterator comparison.*empty hashtables");
  EXPECT_DEATH_IF_SUPPORTED(void(t1.end() == default_constructed_iter),
                            "Invalid iterator comparison.*default-constructed");
  t1.insert(0);
  EXPECT_DEATH_IF_SUPPORTED(void(t1.begin() == t2.end()),
                            "Invalid iterator comparison.*empty hashtable");
  EXPECT_DEATH_IF_SUPPORTED(void(t1.begin() == default_constructed_iter),
                            "Invalid iterator comparison.*default-constructed");
  t2.insert(0);
  EXPECT_DEATH_IF_SUPPORTED(void(t1.begin() == t2.end()),
                            "Invalid iterator comparison.*end.. iterator");
  EXPECT_DEATH_IF_SUPPORTED(void(t1.begin() == t2.begin()),
                            "Invalid iterator comparison.*non-end");
}

template <typename Alloc>
using RawHashSetAlloc = raw_hash_set<IntPolicy, hash_default_hash<int64_t>,
                                     std::equal_to<int64_t>, Alloc>;

TEST(Table, AllocatorPropagation) { TestAllocPropagation<RawHashSetAlloc>(); }

struct CountedHash {
  size_t operator()(int64_t value) const {
    ++count;
    return static_cast<size_t>(value);
  }
  mutable int count = 0;
};

struct CountedHashIntTable
    : raw_hash_set<IntPolicy, CountedHash, std::equal_to<int>,
                   std::allocator<int>> {
  using Base = typename CountedHashIntTable::raw_hash_set;
  using Base::Base;
};

TEST(Table, CountedHash) {
  // Verify that raw_hash_set does not compute redundant hashes.
#ifdef NDEBUG
  constexpr bool kExpectMinimumHashes = true;
#else
  constexpr bool kExpectMinimumHashes = false;
#endif
  if (!kExpectMinimumHashes) {
    GTEST_SKIP() << "Only run under NDEBUG: `assert` statements may cause "
                    "redundant hashing.";
  }

  using Table = CountedHashIntTable;
  auto HashCount = [](const Table& t) { return t.hash_function().count; };
  {
    Table t;
    EXPECT_EQ(HashCount(t), 0);
  }
  {
    Table t;
    t.insert(1);
    EXPECT_EQ(HashCount(t), 1);
    t.erase(1);
    EXPECT_EQ(HashCount(t), 2);
  }
  {
    Table t;
    t.insert(3);
    EXPECT_EQ(HashCount(t), 1);
    auto node = t.extract(3);
    EXPECT_EQ(HashCount(t), 2);
    t.insert(std::move(node));
    EXPECT_EQ(HashCount(t), 3);
  }
  {
    Table t;
    t.emplace(5);
    EXPECT_EQ(HashCount(t), 1);
  }
  {
    Table src;
    src.insert(7);
    Table dst;
    dst.merge(src);
    EXPECT_EQ(HashCount(dst), 1);
  }
}

// IterateOverFullSlots doesn't support SOO.
TEST(Table, IterateOverFullSlotsEmpty) {
  NonSooIntTable t;
  auto fail_if_any = [](const ctrl_t*, auto* i) {
    FAIL() << "expected no slots " << **i;
  };
  container_internal::IterateOverFullSlots(
      RawHashSetTestOnlyAccess::GetCommon(t),
      RawHashSetTestOnlyAccess::GetSlots(t), fail_if_any);
  for (size_t i = 0; i < 256; ++i) {
    t.reserve(i);
    container_internal::IterateOverFullSlots(
        RawHashSetTestOnlyAccess::GetCommon(t),
        RawHashSetTestOnlyAccess::GetSlots(t), fail_if_any);
  }
}

TEST(Table, IterateOverFullSlotsFull) {
  NonSooIntTable t;

  std::vector<int64_t> expected_slots;
  for (int64_t idx = 0; idx < 128; ++idx) {
    t.insert(idx);
    expected_slots.push_back(idx);

    std::vector<int64_t> slots;
    container_internal::IterateOverFullSlots(
        RawHashSetTestOnlyAccess::GetCommon(t),
        RawHashSetTestOnlyAccess::GetSlots(t),
        [&t, &slots](const ctrl_t* ctrl, auto* i) {
          ptrdiff_t ctrl_offset =
              ctrl - RawHashSetTestOnlyAccess::GetCommon(t).control();
          ptrdiff_t slot_offset = i - RawHashSetTestOnlyAccess::GetSlots(t);
          ASSERT_EQ(ctrl_offset, slot_offset);
          slots.push_back(**i);
        });
    EXPECT_THAT(slots, testing::UnorderedElementsAreArray(expected_slots));
  }
}

TEST(Table, IterateOverFullSlotsDeathOnRemoval) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  auto iterate_with_reentrant_removal = [](int64_t size,
                                           int64_t reserve_size = -1) {
    if (reserve_size == -1) reserve_size = size;
    for (int64_t idx = 0; idx < size; ++idx) {
      NonSooIntTable t;
      t.reserve(static_cast<size_t>(reserve_size));
      for (int val = 0; val <= idx; ++val) {
        t.insert(val);
      }

      container_internal::IterateOverFullSlots(
          RawHashSetTestOnlyAccess::GetCommon(t),
          RawHashSetTestOnlyAccess::GetSlots(t),
          [&t](const ctrl_t*, auto* i) {
            int64_t value = **i;
            // Erase the other element from 2*k and 2*k+1 pair.
            t.erase(value ^ 1);
          });
    }
  };

  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_removal(128),
                            "hash table was modified unexpectedly");
  // Removal will likely happen in a different group.
  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_removal(14, 1024 * 16),
                            "hash table was modified unexpectedly");
  // Removal will happen in the same group.
  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_removal(static_cast<int64_t>(
                                CapacityToGrowth(Group::kWidth - 1))),
                            "hash table was modified unexpectedly");
}

TEST(Table, IterateOverFullSlotsDeathOnInsert) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  auto iterate_with_reentrant_insert = [](int64_t reserve_size,
                                          int64_t size_divisor = 2) {
    int64_t size = reserve_size / size_divisor;
    for (int64_t idx = 1; idx <= size; ++idx) {
      NonSooIntTable t;
      t.reserve(static_cast<size_t>(reserve_size));
      for (int val = 1; val <= idx; ++val) {
        t.insert(val);
      }

      container_internal::IterateOverFullSlots(
          RawHashSetTestOnlyAccess::GetCommon(t),
          RawHashSetTestOnlyAccess::GetSlots(t),
          [&t](const ctrl_t*, auto* i) {
            int64_t value = **i;
            t.insert(-value);
          });
    }
  };

  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_insert(128),
                            "hash table was modified unexpectedly");
  // Insert will likely happen in a different group.
  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_insert(1024 * 16, 1024 * 2),
                            "hash table was modified unexpectedly");
  // Insert will happen in the same group.
  EXPECT_DEATH_IF_SUPPORTED(iterate_with_reentrant_insert(static_cast<int64_t>(
                                CapacityToGrowth(Group::kWidth - 1))),
                            "hash table was modified unexpectedly");
}

template <typename T>
class SooTable : public testing::Test {};
using FreezableSooTableTypes =
    ::testing::Types<FreezableSizedValueSooTable<8>,
                     FreezableSizedValueSooTable<16>>;
TYPED_TEST_SUITE(SooTable, FreezableSooTableTypes);

TYPED_TEST(SooTable, Basic) {
  bool frozen = true;
  TypeParam t{FreezableAlloc<typename TypeParam::value_type>(&frozen)};
  if (t.capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }

  t.insert(0);
  EXPECT_EQ(t.capacity(), 1);
  auto it = t.find(0);
  EXPECT_EQ(it, t.begin());
  ASSERT_NE(it, t.end());
  EXPECT_EQ(*it, 0);
  EXPECT_EQ(++it, t.end());
  EXPECT_EQ(t.find(1), t.end());
  EXPECT_EQ(t.size(), 1);

  t.erase(0);
  EXPECT_EQ(t.size(), 0);
  t.insert(1);
  it = t.find(1);
  EXPECT_EQ(it, t.begin());
  ASSERT_NE(it, t.end());
  EXPECT_EQ(*it, 1);

  t.clear();
  EXPECT_EQ(t.size(), 0);
}

TEST(Table, RehashToSooUnsampled) {
  SooIntTable t;
  if (t.capacity() != SooCapacity()) {
    CHECK_LT(sizeof(void*), 8) << "missing SOO coverage";
    GTEST_SKIP() << "not SOO on this platform";
  }

  // We disable hashtablez sampling for this test to ensure that the table isn't
  // sampled. When the table is sampled, it won't rehash down to SOO.
  SetHashtablezEnabled(false);

  t.reserve(100);
  t.insert(0);
  EXPECT_EQ(*t.begin(), 0);

  t.rehash(0);  // Rehash back down to SOO table.

  EXPECT_EQ(t.capacity(), SooCapacity());
  EXPECT_EQ(t.size(), 1);
  EXPECT_EQ(*t.begin(), 0);
  EXPECT_EQ(t.find(0), t.begin());
  EXPECT_EQ(t.find(1), t.end());
}

TEST(Table, ReserveToNonSoo) {
  for (int reserve_capacity : {8, 100000}) {
    SooIntTable t;
    t.insert(0);

    t.reserve(reserve_capacity);

    EXPECT_EQ(t.find(0), t.begin());
    EXPECT_EQ(t.size(), 1);
    EXPECT_EQ(*t.begin(), 0);
    EXPECT_EQ(t.find(1), t.end());
  }
}

struct InconsistentHashEqType {
  InconsistentHashEqType(int v1, int v2) : v1(v1), v2(v2) {}
  template <typename H>
  friend H AbslHashValue(H h, InconsistentHashEqType t) {
    return H::combine(std::move(h), t.v1);
  }
  bool operator==(InconsistentHashEqType t) const { return v2 == t.v2; }
  int v1, v2;
};

TEST(Iterator, InconsistentHashEqFunctorsValidation) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  ValueTable<InconsistentHashEqType> t;
  for (int i = 0; i < 10; ++i) t.insert({i, i});
  // We need to find/insert multiple times to guarantee that we get the
  // assertion because it's possible for the hash to collide with the inserted
  // element that has v2==0. In those cases, the new element won't be inserted.
  auto find_conflicting_elems = [&] {
    for (int i = 100; i < 20000; ++i) {
      EXPECT_EQ(t.find({i, 0}), t.end());
    }
  };
  EXPECT_DEATH_IF_SUPPORTED(find_conflicting_elems(),
                            "hash/eq functors are inconsistent.");
  auto insert_conflicting_elems = [&] {
    for (int i = 100; i < 20000; ++i) {
      EXPECT_EQ(t.insert({i, 0}).second, false);
    }
  };
  EXPECT_DEATH_IF_SUPPORTED(insert_conflicting_elems(),
                            "hash/eq functors are inconsistent.");
}

struct ConstructCaller {
  explicit ConstructCaller(int v) : val(v) {}
  ConstructCaller(int v, absl::FunctionRef<void()> func) : val(v) { func(); }
  template <typename H>
  friend H AbslHashValue(H h, const ConstructCaller& d) {
    return H::combine(std::move(h), d.val);
  }
  bool operator==(const ConstructCaller& c) const { return val == c.val; }

  int val;
};

struct DestroyCaller {
  explicit DestroyCaller(int v) : val(v) {}
  DestroyCaller(int v, absl::FunctionRef<void()> func)
      : val(v), destroy_func(func) {}
  DestroyCaller(DestroyCaller&& that)
      : val(that.val), destroy_func(std::move(that.destroy_func)) {
    that.Deactivate();
  }
  ~DestroyCaller() {
    if (destroy_func) (*destroy_func)();
  }
  void Deactivate() { destroy_func = absl::nullopt; }

  template <typename H>
  friend H AbslHashValue(H h, const DestroyCaller& d) {
    return H::combine(std::move(h), d.val);
  }
  bool operator==(const DestroyCaller& d) const { return val == d.val; }

  int val;
  absl::optional<absl::FunctionRef<void()>> destroy_func;
};

TEST(Table, ReentrantCallsFail) {
#ifdef NDEBUG
  GTEST_SKIP() << "Reentrant checks only enabled in debug mode.";
#else
  {
    ValueTable<ConstructCaller> t;
    t.insert(ConstructCaller{0});
    auto erase_begin = [&] { t.erase(t.begin()); };
    EXPECT_DEATH_IF_SUPPORTED(t.emplace(1, erase_begin), "");
  }
  {
    ValueTable<DestroyCaller> t;
    t.insert(DestroyCaller{0});
    auto find_0 = [&] { t.find(DestroyCaller{0}); };
    t.insert(DestroyCaller{1, find_0});
    for (int i = 10; i < 20; ++i) t.insert(DestroyCaller{i});
    EXPECT_DEATH_IF_SUPPORTED(t.clear(), "");
    for (auto& elem : t) elem.Deactivate();
  }
  {
    ValueTable<DestroyCaller> t;
    t.insert(DestroyCaller{0});
    auto insert_1 = [&] { t.insert(DestroyCaller{1}); };
    t.insert(DestroyCaller{1, insert_1});
    for (int i = 10; i < 20; ++i) t.insert(DestroyCaller{i});
    EXPECT_DEATH_IF_SUPPORTED(t.clear(), "");
    for (auto& elem : t) elem.Deactivate();
  }
#endif
}

TEST(Table, DestroyedCallsFail) {
#ifdef NDEBUG
  GTEST_SKIP() << "Destroyed checks only enabled in debug mode.";
#else
  absl::optional<IntTable> t;
  t.emplace({1});
  IntTable* t_ptr = &*t;
  EXPECT_TRUE(t_ptr->contains(1));
  t.reset();
  EXPECT_DEATH_IF_SUPPORTED(t_ptr->contains(1), "");
#endif
}

TEST(Table, MovedFromCallsFail) {
  if (!SwisstableGenerationsEnabled()) {
    GTEST_SKIP() << "Moved-from checks only enabled in sanitizer mode.";
    return;
  }

  {
    ABSL_ATTRIBUTE_UNUSED IntTable t1, t2;
    t1.insert(1);
    t2 = std::move(t1);
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_DEATH_IF_SUPPORTED(t1.contains(1), "moved-from");
  }
  {
    ABSL_ATTRIBUTE_UNUSED IntTable t1;
    t1.insert(1);
    ABSL_ATTRIBUTE_UNUSED IntTable t2(std::move(t1));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_DEATH_IF_SUPPORTED(t1.contains(1), "moved-from");
    t1.clear();  // Clearing a moved-from table is allowed.
  }
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
