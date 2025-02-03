// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-statistics-collector.h"

#include "include/cppgc/heap-statistics.h"
#include "include/cppgc/persistent.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/heap/cppgc/globals.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

class HeapStatisticsCollectorTest : public testing::TestWithHeap {};

TEST_F(HeapStatisticsCollectorTest, EmptyHeapBriefStatisitcs) {
  HeapStatistics brief_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kBrief);
  EXPECT_EQ(HeapStatistics::DetailLevel::kBrief, brief_stats.detail_level);
  EXPECT_EQ(0u, brief_stats.used_size_bytes);
  EXPECT_EQ(0u, brief_stats.used_size_bytes);
  EXPECT_EQ(0u, brief_stats.pooled_memory_size_bytes);
  EXPECT_TRUE(brief_stats.space_stats.empty());
}

TEST_F(HeapStatisticsCollectorTest, EmptyHeapDetailedStatisitcs) {
  HeapStatistics detailed_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kDetailed);
  EXPECT_EQ(HeapStatistics::DetailLevel::kDetailed,
            detailed_stats.detail_level);
  EXPECT_EQ(0u, detailed_stats.used_size_bytes);
  EXPECT_EQ(0u, detailed_stats.used_size_bytes);
  EXPECT_EQ(0u, detailed_stats.pooled_memory_size_bytes);
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces, detailed_stats.space_stats.size());
  for (HeapStatistics::SpaceStatistics& space_stats :
       detailed_stats.space_stats) {
    EXPECT_EQ(0u, space_stats.used_size_bytes);
    EXPECT_EQ(0u, space_stats.used_size_bytes);
    EXPECT_TRUE(space_stats.page_stats.empty());
    if (space_stats.name == "LargePageSpace") {
      // Large page space has no free list.
      EXPECT_TRUE(space_stats.free_list_stats.bucket_size.empty());
      EXPECT_TRUE(space_stats.free_list_stats.free_count.empty());
      EXPECT_TRUE(space_stats.free_list_stats.free_size.empty());
    } else {
      EXPECT_EQ(kPageSizeLog2, space_stats.free_list_stats.bucket_size.size());
      EXPECT_EQ(kPageSizeLog2, space_stats.free_list_stats.free_count.size());
      EXPECT_EQ(kPageSizeLog2, space_stats.free_list_stats.free_size.size());
    }
  }
}

namespace {
template <size_t Size>
class GCed : public GarbageCollected<GCed<Size>> {
 public:
  void Trace(Visitor*) const {}

 private:
  char array_[Size];
};
}  // namespace

TEST_F(HeapStatisticsCollectorTest, NonEmptyNormalPage) {
  MakeGarbageCollected<GCed<1>>(GetHeap()->GetAllocationHandle());
  static constexpr size_t used_size =
      RoundUp<kAllocationGranularity>(1 + sizeof(HeapObjectHeader));
  HeapStatistics detailed_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kDetailed);
  EXPECT_EQ(HeapStatistics::DetailLevel::kDetailed,
            detailed_stats.detail_level);
  EXPECT_EQ(kPageSize, detailed_stats.committed_size_bytes);
  EXPECT_EQ(kPageSize, detailed_stats.resident_size_bytes);
  EXPECT_EQ(used_size, detailed_stats.used_size_bytes);
  EXPECT_EQ(0u, detailed_stats.pooled_memory_size_bytes);
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces, detailed_stats.space_stats.size());
  bool found_non_empty_space = false;
  for (const HeapStatistics::SpaceStatistics& space_stats :
       detailed_stats.space_stats) {
    if (space_stats.page_stats.empty()) {
      EXPECT_EQ(0u, space_stats.committed_size_bytes);
      EXPECT_EQ(0u, space_stats.resident_size_bytes);
      EXPECT_EQ(0u, space_stats.used_size_bytes);
      continue;
    }
    EXPECT_NE("LargePageSpace", space_stats.name);
    EXPECT_FALSE(found_non_empty_space);
    found_non_empty_space = true;
    EXPECT_EQ(kPageSize, space_stats.committed_size_bytes);
    EXPECT_EQ(kPageSize, space_stats.resident_size_bytes);
    EXPECT_EQ(used_size, space_stats.used_size_bytes);
    EXPECT_EQ(1u, space_stats.page_stats.size());
    EXPECT_EQ(kPageSize, space_stats.page_stats.back().committed_size_bytes);
    EXPECT_EQ(kPageSize, space_stats.page_stats.back().resident_size_bytes);
    EXPECT_EQ(used_size, space_stats.page_stats.back().used_size_bytes);
  }
  EXPECT_TRUE(found_non_empty_space);
}

TEST_F(HeapStatisticsCollectorTest, NonEmptyLargePage) {
  MakeGarbageCollected<GCed<kLargeObjectSizeThreshold>>(
      GetHeap()->GetAllocationHandle());
  static constexpr size_t used_size = RoundUp<kAllocationGranularity>(
      kLargeObjectSizeThreshold + sizeof(HeapObjectHeader));
  static constexpr size_t committed_size =
      RoundUp<kAllocationGranularity>(used_size + LargePage::PageHeaderSize());
  HeapStatistics detailed_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kDetailed);
  EXPECT_EQ(HeapStatistics::DetailLevel::kDetailed,
            detailed_stats.detail_level);
  EXPECT_EQ(committed_size, detailed_stats.committed_size_bytes);
  EXPECT_EQ(committed_size, detailed_stats.resident_size_bytes);
  EXPECT_EQ(used_size, detailed_stats.used_size_bytes);
  EXPECT_EQ(0u, detailed_stats.pooled_memory_size_bytes);
  EXPECT_EQ(RawHeap::kNumberOfRegularSpaces, detailed_stats.space_stats.size());
  bool found_non_empty_space = false;
  for (const HeapStatistics::SpaceStatistics& space_stats :
       detailed_stats.space_stats) {
    if (space_stats.page_stats.empty()) {
      EXPECT_EQ(0u, space_stats.committed_size_bytes);
      EXPECT_EQ(0u, space_stats.used_size_bytes);
      continue;
    }
    EXPECT_EQ("LargePageSpace", space_stats.name);
    EXPECT_FALSE(found_non_empty_space);
    found_non_empty_space = true;
    EXPECT_EQ(committed_size, space_stats.committed_size_bytes);
    EXPECT_EQ(committed_size, space_stats.resident_size_bytes);
    EXPECT_EQ(used_size, space_stats.used_size_bytes);
    EXPECT_EQ(1u, space_stats.page_stats.size());
    EXPECT_EQ(committed_size,
              space_stats.page_stats.back().committed_size_bytes);
    EXPECT_EQ(committed_size,
              space_stats.page_stats.back().resident_size_bytes);
    EXPECT_EQ(used_size, space_stats.page_stats.back().used_size_bytes);
  }
  EXPECT_TRUE(found_non_empty_space);
}

TEST_F(HeapStatisticsCollectorTest, BriefStatisticsWithDiscardingOnNormalPage) {
  if (!Sweeper::CanDiscardMemory()) return;

  Persistent<GCed<1>> holder =
      MakeGarbageCollected<GCed<1>>(GetHeap()->GetAllocationHandle());
  ConservativeMemoryDiscardingGC();
  HeapStatistics brief_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kBrief);
  // Do not enforce exact resident_size_bytes here as this is an implementation
  // detail of the sweeper.
  EXPECT_GT(brief_stats.committed_size_bytes, brief_stats.resident_size_bytes);
  EXPECT_EQ(0u, brief_stats.pooled_memory_size_bytes);
}

TEST_F(HeapStatisticsCollectorTest,
       BriefStatisticsWithoutDiscardingOnNormalPage) {
  if (!Sweeper::CanDiscardMemory()) return;

  MakeGarbageCollected<GCed<1>>(GetHeap()->GetAllocationHandle());

  // kNoHeapPointers: make the test deterministic, not depend on what the
  // compiler does with the stack.
  internal::Heap::From(GetHeap())->CollectGarbage(
      {CollectionType::kMinor, Heap::StackState::kNoHeapPointers,
       cppgc::Heap::MarkingType::kAtomic, cppgc::Heap::SweepingType::kAtomic,
       GCConfig::FreeMemoryHandling::kDoNotDiscard});

  HeapStatistics brief_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kBrief);
  // Pooled memory, since it wasn't discarded by the sweeper.
  EXPECT_NE(brief_stats.pooled_memory_size_bytes, 0u);
  // Pooled memory is committed and resident.
  EXPECT_EQ(brief_stats.pooled_memory_size_bytes,
            brief_stats.resident_size_bytes);
  EXPECT_EQ(brief_stats.pooled_memory_size_bytes,
            brief_stats.committed_size_bytes);
  // But not allocated.
  EXPECT_EQ(brief_stats.used_size_bytes, 0u);

  // Pooled memory goes away when discarding, and is not accounted for once
  // discarded.
  internal::Heap::From(GetHeap())->CollectGarbage(
      {CollectionType::kMinor, Heap::StackState::kMayContainHeapPointers,
       cppgc::Heap::MarkingType::kAtomic, cppgc::Heap::SweepingType::kAtomic,
       GCConfig::FreeMemoryHandling::kDiscardWherePossible});
  brief_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kBrief);
  EXPECT_EQ(0u, brief_stats.pooled_memory_size_bytes);
  EXPECT_EQ(0u, brief_stats.resident_size_bytes);
  EXPECT_EQ(0u, brief_stats.committed_size_bytes);
  EXPECT_EQ(0u, brief_stats.used_size_bytes);
}

TEST_F(HeapStatisticsCollectorTest,
       DetailedStatisticsWithDiscardingOnNormalPage) {
  if (!Sweeper::CanDiscardMemory()) return;

  Persistent<GCed<1>> holder =
      MakeGarbageCollected<GCed<1>>(GetHeap()->GetAllocationHandle());
  ConservativeMemoryDiscardingGC();
  HeapStatistics detailed_stats = Heap::From(GetHeap())->CollectStatistics(
      HeapStatistics::DetailLevel::kDetailed);
  // Do not enforce exact resident_size_bytes here as this is an implementation
  // detail of the sweeper.
  EXPECT_GT(detailed_stats.committed_size_bytes,
            detailed_stats.resident_size_bytes);
  EXPECT_EQ(0u, detailed_stats.pooled_memory_size_bytes);
  bool found_page = false;
  for (const auto& space_stats : detailed_stats.space_stats) {
    if (space_stats.committed_size_bytes == 0) continue;

    // We should find a single page here that contains memory that was
    // discarded.
    EXPECT_EQ(1u, space_stats.page_stats.size());
    const auto& page_stats = space_stats.page_stats[0];
    EXPECT_GT(page_stats.committed_size_bytes, page_stats.resident_size_bytes);
    found_page = true;
  }
  EXPECT_TRUE(found_page);
}

}  // namespace internal
}  // namespace cppgc
