// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if defined(CPPGC_CAGED_HEAP)

#include "src/heap/cppgc/caged-heap.h"

#include "include/cppgc/internal/caged-heap-local-data.h"
#include "src/base/page-allocator.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc::internal {

class CagedHeapDeathTest : public testing::TestWithHeap {};

TEST_F(CagedHeapDeathTest, AgeTableUncommittedBeforeGenerationalGCEnabled) {
  // Test cannot run if Generational GC was already enabled.
  ASSERT_FALSE(Heap::From(GetHeap())->generational_gc_supported());

  EXPECT_DEATH_IF_SUPPORTED(
      CagedHeapLocalData::Get().age_table.SetAge(0, AgeTable::Age::kOld), "");
}

class CagedHeapTest : public testing::TestWithHeap {};

TEST_F(CagedHeapTest, AgeTableCommittedAfterGenerationalGCEnabled) {
  // Test cannot run if Generational GC was already enabled.
  ASSERT_FALSE(Heap::From(GetHeap())->generational_gc_supported());

  CagedHeap::CommitAgeTable(*(GetPlatform().GetPageAllocator()));
  EXPECT_EQ(CagedHeapLocalData::Get().age_table.GetAge(0), AgeTable::Age::kOld);
}

}  // namespace cppgc::internal

#endif  // defined(CPPGC_CAGED_HEAP)
