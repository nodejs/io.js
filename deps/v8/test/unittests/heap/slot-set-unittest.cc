// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/slot-set.h"

#include <limits>
#include <map>

#include "src/common/globals.h"
#include "src/heap/spaces.h"
#include "src/objects/slots.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

TEST(PossiblyEmptyBucketsTest, WordsForBuckets) {
  EXPECT_EQ(
      PossiblyEmptyBuckets::WordsForBuckets(PossiblyEmptyBuckets::kBitsPerWord),
      1U);
  EXPECT_EQ(PossiblyEmptyBuckets::WordsForBuckets(
                PossiblyEmptyBuckets::kBitsPerWord - 1),
            1U);
  EXPECT_EQ(PossiblyEmptyBuckets::WordsForBuckets(
                PossiblyEmptyBuckets::kBitsPerWord + 1),
            2U);

  EXPECT_EQ(PossiblyEmptyBuckets::WordsForBuckets(
                5 * PossiblyEmptyBuckets::kBitsPerWord - 1),
            5U);
  EXPECT_EQ(PossiblyEmptyBuckets::WordsForBuckets(
                5 * PossiblyEmptyBuckets::kBitsPerWord),
            5U);
  EXPECT_EQ(PossiblyEmptyBuckets::WordsForBuckets(
                5 * PossiblyEmptyBuckets::kBitsPerWord + 1),
            6U);
}

TEST(SlotSet, BucketsForSize) {
  EXPECT_EQ(static_cast<size_t>(SlotSet::kBucketsRegularPage),
            SlotSet::BucketsForSize(PageMetadata::kPageSize));

  EXPECT_EQ(static_cast<size_t>(SlotSet::kBucketsRegularPage) * 2,
            SlotSet::BucketsForSize(PageMetadata::kPageSize * 2));
}

TEST(PossiblyEmptyBuckets, ContainsAndInsert) {
  static const int kBuckets = 100;
  PossiblyEmptyBuckets possibly_empty_buckets;
  possibly_empty_buckets.Insert(0, kBuckets);
  int last = sizeof(uintptr_t) * kBitsPerByte - 2;
  possibly_empty_buckets.Insert(last, kBuckets);
  EXPECT_TRUE(possibly_empty_buckets.Contains(0));
  EXPECT_TRUE(possibly_empty_buckets.Contains(last));
  possibly_empty_buckets.Insert(last + 1, kBuckets);
  EXPECT_TRUE(possibly_empty_buckets.Contains(0));
  EXPECT_TRUE(possibly_empty_buckets.Contains(last));
  EXPECT_TRUE(possibly_empty_buckets.Contains(last + 1));
}

TEST(TypedSlotSet, Iterate) {
  TypedSlotSet set(0);
  // These two constants must be static as a workaround
  // for a MSVC++ bug about lambda captures, see the discussion at
  // https://social.msdn.microsoft.com/Forums/SqlServer/4abf18bd-4ae4-4c72-ba3e-3b13e7909d5f
  static const int kDelta = 10000001;
  int added = 0;
  for (uint32_t i = 0; i < TypedSlotSet::kMaxOffset; i += kDelta) {
    SlotType type =
        static_cast<SlotType>(i % static_cast<uint8_t>(SlotType::kCleared));
    set.Insert(type, i);
    ++added;
  }
  int iterated = 0;
  set.Iterate(
      [&iterated](SlotType type, Address addr) {
        uint32_t i = static_cast<uint32_t>(addr);
        EXPECT_EQ(i % static_cast<uint8_t>(SlotType::kCleared),
                  static_cast<uint32_t>(type));
        EXPECT_EQ(0u, i % kDelta);
        ++iterated;
        return i % 2 == 0 ? KEEP_SLOT : REMOVE_SLOT;
      },
      TypedSlotSet::KEEP_EMPTY_CHUNKS);
  EXPECT_EQ(added, iterated);
  iterated = 0;
  set.Iterate(
      [&iterated](SlotType type, Address addr) {
        uint32_t i = static_cast<uint32_t>(addr);
        EXPECT_EQ(0u, i % 2);
        ++iterated;
        return KEEP_SLOT;
      },
      TypedSlotSet::KEEP_EMPTY_CHUNKS);
  EXPECT_EQ(added / 2, iterated);
}

TEST(TypedSlotSet, ClearInvalidSlots) {
  TypedSlotSet set(0);
  const int kHostDelta = 100;
  uint32_t entries = 10;
  for (uint32_t i = 0; i < entries; i++) {
    SlotType type =
        static_cast<SlotType>(i % static_cast<uint8_t>(SlotType::kCleared));
    set.Insert(type, i * kHostDelta);
  }

  TypedSlotSet::FreeRangesMap invalid_ranges;
  for (uint32_t i = 1; i < entries; i += 2) {
    invalid_ranges.insert(
        std::pair<uint32_t, uint32_t>(i * kHostDelta, i * kHostDelta + 1));
  }

  set.ClearInvalidSlots(invalid_ranges);
  for (TypedSlotSet::FreeRangesMap::iterator it = invalid_ranges.begin();
       it != invalid_ranges.end(); ++it) {
    uint32_t start = it->first;
    uint32_t end = it->second;
    set.Iterate(
        [=](SlotType slot_type, Address slot_addr) {
          CHECK(slot_addr < start || slot_addr >= end);
          return KEEP_SLOT;
        },
        TypedSlotSet::KEEP_EMPTY_CHUNKS);
  }
}

TEST(TypedSlotSet, Merge) {
  TypedSlotSet set0(0), set1(0);
  static const uint32_t kEntries = 10000;
  for (uint32_t i = 0; i < kEntries; i++) {
    set0.Insert(SlotType::kEmbeddedObjectFull, 2 * i);
    set1.Insert(SlotType::kEmbeddedObjectFull, 2 * i + 1);
  }
  uint32_t count = 0;
  set0.Merge(&set1);
  set0.Iterate(
      [&count](SlotType slot_type, Address slot_addr) {
        if (count < kEntries) {
          CHECK_EQ(slot_addr % 2, 0);
        } else {
          CHECK_EQ(slot_addr % 2, 1);
        }
        ++count;
        return KEEP_SLOT;
      },
      TypedSlotSet::KEEP_EMPTY_CHUNKS);
  CHECK_EQ(2 * kEntries, count);
  set1.Iterate(
      [](SlotType slot_type, Address slot_addr) {
        CHECK(false);  // Unreachable.
        return KEEP_SLOT;
      },
      TypedSlotSet::KEEP_EMPTY_CHUNKS);
}

}  // namespace internal
}  // namespace v8
