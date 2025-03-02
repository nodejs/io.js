// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/basic-slot-set.h"

#include <limits>
#include <map>

#include "testing/gtest/include/gtest/gtest.h"

namespace heap {
namespace base {

static constexpr size_t kTestGranularity = sizeof(void*);
using TestSlotSet = ::heap::base::BasicSlotSet<kTestGranularity>;
static constexpr size_t kTestPageSize = 1 << 17;
static constexpr size_t kBucketsTestPage =
    TestSlotSet::BucketsForSize(kTestPageSize);

TEST(BasicSlotSet, InsertAndLookup1) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);
  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    EXPECT_FALSE(set->Lookup(i));
  }
  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
  }
  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    EXPECT_TRUE(set->Lookup(i));
  }
  TestSlotSet::Delete(set);
}

TEST(BasicSlotSet, InsertAndLookup2) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);
  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 7 == 0) {
      set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
    }
  }
  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 7 == 0) {
      EXPECT_TRUE(set->Lookup(i));
    } else {
      EXPECT_FALSE(set->Lookup(i));
    }
  }
  TestSlotSet::Delete(set);
}

TEST(BasicSlotSet, Iterate) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 7 == 0) {
      set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
    }
  }

  set->Iterate(
      0, 0, kBucketsTestPage,
      [](uintptr_t slot) {
        if (slot % 3 == 0) {
          return KEEP_SLOT;
        } else {
          return REMOVE_SLOT;
        }
      },
      TestSlotSet::KEEP_EMPTY_BUCKETS);

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 21 == 0) {
      EXPECT_TRUE(set->Lookup(i));
    } else {
      EXPECT_FALSE(set->Lookup(i));
    }
  }

  TestSlotSet::Delete(set);
}

TEST(BasicSlotSet, IterateFromHalfway) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 7 == 0) {
      set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
    }
  }

  set->Iterate(
      0, kBucketsTestPage / 2, kBucketsTestPage,
      [](uintptr_t slot) {
        if (slot % 3 == 0) {
          return KEEP_SLOT;
        } else {
          return REMOVE_SLOT;
        }
      },
      TestSlotSet::KEEP_EMPTY_BUCKETS);

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i < kTestPageSize / 2 && i % 7 == 0) {
      EXPECT_TRUE(set->Lookup(i));
    } else if (i >= kTestPageSize / 2 && i % 21 == 0) {
      EXPECT_TRUE(set->Lookup(i));
    } else {
      EXPECT_FALSE(set->Lookup(i));
    }
  }

  TestSlotSet::Delete(set);
}

TEST(BasicSlotSet, Remove) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 7 == 0) {
      set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
    }
  }

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 3 != 0) {
      set->Remove(i);
    }
  }

  for (size_t i = 0; i < kTestPageSize; i += kTestGranularity) {
    if (i % 21 == 0) {
      EXPECT_TRUE(set->Lookup(i));
    } else {
      EXPECT_FALSE(set->Lookup(i));
    }
  }

  TestSlotSet::Delete(set);
}

namespace {
void CheckRemoveRangeOn(uint32_t start, uint32_t end) {
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);
  uint32_t first = start == 0 ? 0 : start - kTestGranularity;
  uint32_t last = end == kTestPageSize ? end - kTestGranularity : end;
  for (const auto mode :
       {TestSlotSet::FREE_EMPTY_BUCKETS, TestSlotSet::KEEP_EMPTY_BUCKETS}) {
    for (uint32_t i = first; i <= last; i += kTestGranularity) {
      set->Insert<TestSlotSet::AccessMode::ATOMIC>(i);
    }
    set->RemoveRange(start, end, kBucketsTestPage, mode);
    if (first != start) {
      EXPECT_TRUE(set->Lookup(first));
    }
    if (last == end) {
      EXPECT_TRUE(set->Lookup(last));
    }
    for (size_t i = start; i < end; i += kTestGranularity) {
      EXPECT_FALSE(set->Lookup(i));
    }
  }
  TestSlotSet::Delete(set);
}
}  // namespace

TEST(BasicSlotSet, RemoveRange) {
  CheckRemoveRangeOn(0, kTestPageSize);
  CheckRemoveRangeOn(1 * kTestGranularity, 1023 * kTestGranularity);
  for (uint32_t start = 0; start <= 32; start++) {
    CheckRemoveRangeOn(start * kTestGranularity,
                       (start + 1) * kTestGranularity);
    CheckRemoveRangeOn(start * kTestGranularity,
                       (start + 2) * kTestGranularity);
    const uint32_t kEnds[] = {32, 64, 100, 128, 1024, 1500, 2048};
    for (size_t i = 0; i < sizeof(kEnds) / sizeof(uint32_t); i++) {
      for (int k = -3; k <= 3; k++) {
        uint32_t end = (kEnds[i] + k);
        if (start < end) {
          CheckRemoveRangeOn(start * kTestGranularity, end * kTestGranularity);
        }
      }
    }
  }
  TestSlotSet* set = TestSlotSet::Allocate(kBucketsTestPage);
  for (const auto mode :
       {TestSlotSet::FREE_EMPTY_BUCKETS, TestSlotSet::KEEP_EMPTY_BUCKETS}) {
    set->Insert<TestSlotSet::AccessMode::ATOMIC>(kTestPageSize / 2);
    set->RemoveRange(0, kTestPageSize, kBucketsTestPage, mode);
    for (uint32_t i = 0; i < kTestPageSize; i += kTestGranularity) {
      EXPECT_FALSE(set->Lookup(i));
    }
  }
  TestSlotSet::Delete(set);
}

}  // namespace base
}  // namespace heap
