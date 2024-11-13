// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/spaces.h"

#include <memory>

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/heap.h"
#include "src/heap/large-spaces.h"
#include "src/heap/main-allocator.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces-inl.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

static Tagged<HeapObject> AllocateUnaligned(MainAllocator* allocator,
                                            SpaceWithLinearArea* space,
                                            int size) {
  AllocationResult allocation =
      allocator->AllocateRaw(size, kTaggedAligned, AllocationOrigin::kRuntime);
  CHECK(!allocation.IsFailure());
  Tagged<HeapObject> filler;
  CHECK(allocation.To(&filler));
  space->heap()->CreateFillerObjectAt(filler.address(), size);
  return filler;
}

static Tagged<HeapObject> AllocateUnaligned(OldLargeObjectSpace* allocator,
                                            OldLargeObjectSpace* space,
                                            int size) {
  AllocationResult allocation =
      allocator->AllocateRaw(space->heap()->main_thread_local_heap(), size);
  CHECK(!allocation.IsFailure());
  Tagged<HeapObject> filler;
  CHECK(allocation.To(&filler));
  space->heap()->CreateFillerObjectAt(filler.address(), size);
  return filler;
}

using SpacesTest = TestWithIsolate;

TEST_F(SpacesTest, CompactionSpaceMerge) {
  Heap* heap = i_isolate()->heap();
  OldSpace* old_space = heap->old_space();
  EXPECT_TRUE(old_space != nullptr);

  heap->SetGCState(Heap::MARK_COMPACT);

  CompactionSpace* compaction_space =
      new CompactionSpace(heap, OLD_SPACE, NOT_EXECUTABLE,
                          CompactionSpaceKind::kCompactionSpaceForMarkCompact);
  MainAllocator allocator(heap, compaction_space, MainAllocator::kInGC);
  EXPECT_TRUE(compaction_space != nullptr);

  for (PageMetadata* p : *old_space) {
    // Unlink free lists from the main space to avoid reusing the memory for
    // compaction spaces.
    old_space->UnlinkFreeListCategories(p);
  }

  // Cannot loop until "Available()" since we initially have 0 bytes available
  // and would thus neither grow, nor be able to allocate an object.
  const int kNumObjects = 10;
  const int kNumObjectsPerPage =
      compaction_space->AreaSize() / kMaxRegularHeapObjectSize;
  const int kExpectedPages =
      (kNumObjects + kNumObjectsPerPage - 1) / kNumObjectsPerPage;
  for (int i = 0; i < kNumObjects; i++) {
    Tagged<HeapObject> object =
        allocator
            .AllocateRaw(kMaxRegularHeapObjectSize, kTaggedAligned,
                         AllocationOrigin::kGC)
            .ToObjectChecked();
    heap->CreateFillerObjectAt(object.address(), kMaxRegularHeapObjectSize);
  }
  int pages_in_old_space = old_space->CountTotalPages();
  int pages_in_compaction_space = compaction_space->CountTotalPages();
  EXPECT_EQ(kExpectedPages, pages_in_compaction_space);
  allocator.FreeLinearAllocationArea();
  old_space->MergeCompactionSpace(compaction_space);
  EXPECT_EQ(pages_in_old_space + pages_in_compaction_space,
            old_space->CountTotalPages());

  delete compaction_space;

  heap->SetGCState(Heap::NOT_IN_GC);
}

TEST_F(SpacesTest, WriteBarrierIsMarking) {
  const size_t kSizeOfMemoryChunk = sizeof(MutablePageMetadata);
  char memory[kSizeOfMemoryChunk];
  memset(&memory, 0, kSizeOfMemoryChunk);
  MemoryChunk* chunk = reinterpret_cast<MemoryChunk*>(&memory);
  EXPECT_FALSE(chunk->IsFlagSet(MemoryChunk::INCREMENTAL_MARKING));
  EXPECT_FALSE(chunk->IsMarking());
  chunk->SetFlagNonExecutable(MemoryChunk::INCREMENTAL_MARKING);
  EXPECT_TRUE(chunk->IsFlagSet(MemoryChunk::INCREMENTAL_MARKING));
  EXPECT_TRUE(chunk->IsMarking());
  chunk->ClearFlagNonExecutable(MemoryChunk::INCREMENTAL_MARKING);
  EXPECT_FALSE(chunk->IsFlagSet(MemoryChunk::INCREMENTAL_MARKING));
  EXPECT_FALSE(chunk->IsMarking());
}

TEST_F(SpacesTest, WriteBarrierInYoungGenerationToSpace) {
  const size_t kSizeOfMemoryChunk = sizeof(MutablePageMetadata);
  char memory[kSizeOfMemoryChunk];
  memset(&memory, 0, kSizeOfMemoryChunk);
  MemoryChunk* chunk = reinterpret_cast<MemoryChunk*>(&memory);
  EXPECT_FALSE(chunk->InYoungGeneration());
  chunk->SetFlagNonExecutable(MemoryChunk::TO_PAGE);
  EXPECT_TRUE(chunk->InYoungGeneration());
  chunk->ClearFlagNonExecutable(MemoryChunk::TO_PAGE);
  EXPECT_FALSE(chunk->InYoungGeneration());
}

TEST_F(SpacesTest, WriteBarrierInYoungGenerationFromSpace) {
  const size_t kSizeOfMemoryChunk = sizeof(MutablePageMetadata);
  char memory[kSizeOfMemoryChunk];
  memset(&memory, 0, kSizeOfMemoryChunk);
  MemoryChunk* chunk = reinterpret_cast<MemoryChunk*>(&memory);
  EXPECT_FALSE(chunk->InYoungGeneration());
  chunk->SetFlagNonExecutable(MemoryChunk::FROM_PAGE);
  EXPECT_TRUE(chunk->InYoungGeneration());
  chunk->ClearFlagNonExecutable(MemoryChunk::FROM_PAGE);
  EXPECT_FALSE(chunk->InYoungGeneration());
}

TEST_F(SpacesTest, CodeRangeAddressReuse) {
  CodeRangeAddressHint hint;
  const size_t base_alignment = MutablePageMetadata::kPageSize;
  // Create code ranges.
  Address code_range1 = hint.GetAddressHint(100, base_alignment);
  CHECK(IsAligned(code_range1, base_alignment));
  Address code_range2 = hint.GetAddressHint(200, base_alignment);
  CHECK(IsAligned(code_range2, base_alignment));
  Address code_range3 = hint.GetAddressHint(100, base_alignment);
  CHECK(IsAligned(code_range3, base_alignment));

  // Since the addresses are random, we cannot check that they are different.

  // Free two code ranges.
  hint.NotifyFreedCodeRange(code_range1, 100);
  hint.NotifyFreedCodeRange(code_range2, 200);

  // The next two code ranges should reuse the freed addresses.
  Address code_range4 = hint.GetAddressHint(100, base_alignment);
  EXPECT_EQ(code_range4, code_range1);
  Address code_range5 = hint.GetAddressHint(200, base_alignment);
  EXPECT_EQ(code_range5, code_range2);

  // Free the third code range and check address reuse.
  hint.NotifyFreedCodeRange(code_range3, 100);
  Address code_range6 = hint.GetAddressHint(100, base_alignment);
  EXPECT_EQ(code_range6, code_range3);
}

// Tests that FreeListMany::SelectFreeListCategoryType returns what it should.
TEST_F(SpacesTest, FreeListManySelectFreeListCategoryType) {
  FreeListMany free_list;

  // Testing that all sizes below 256 bytes get assigned the correct category
  for (size_t size = 0; size <= FreeListMany::kPreciseCategoryMaxSize; size++) {
    FreeListCategoryType cat = free_list.SelectFreeListCategoryType(size);
    if (cat == 0) {
      // If cat == 0, then we make sure that |size| doesn't fit in the 2nd
      // category.
      EXPECT_LT(size, free_list.categories_min[1]);
    } else {
      // Otherwise, size should fit in |cat|, but not in |cat+1|.
      EXPECT_LE(free_list.categories_min[cat], size);
      EXPECT_LT(size, free_list.categories_min[cat + 1]);
    }
  }

  // Testing every size above 256 would take long time, so test only some
  // "interesting cases": picking some number in the middle of the categories,
  // as well as at the categories' bounds.
  for (int cat = kFirstCategory + 1; cat <= free_list.last_category_; cat++) {
    std::vector<size_t> sizes;
    // Adding size less than this category's minimum
    sizes.push_back(free_list.categories_min[cat] - 8);
    // Adding size equal to this category's minimum
    sizes.push_back(free_list.categories_min[cat]);
    // Adding size greater than this category's minimum
    sizes.push_back(free_list.categories_min[cat] + 8);
    // Adding size between this category's minimum and the next category
    if (cat != free_list.last_category_) {
      sizes.push_back(
          (free_list.categories_min[cat] + free_list.categories_min[cat + 1]) /
          2);
    }

    for (size_t size : sizes) {
      FreeListCategoryType selected =
          free_list.SelectFreeListCategoryType(size);
      if (selected == free_list.last_category_) {
        // If selected == last_category, then we make sure that |size| indeeds
        // fits in the last category.
        EXPECT_LE(free_list.categories_min[selected], size);
      } else {
        // Otherwise, size should fit in |selected|, but not in |selected+1|.
        EXPECT_LE(free_list.categories_min[selected], size);
        EXPECT_LT(size, free_list.categories_min[selected + 1]);
      }
    }
  }
}

// Tests that
// FreeListManyCachedFastPath::SelectFastAllocationFreeListCategoryType returns
// what it should.
TEST_F(SpacesTest,
       FreeListManyCachedFastPathSelectFastAllocationFreeListCategoryType) {
  FreeListManyCachedFastPath free_list;

  for (int cat = kFirstCategory; cat <= free_list.last_category_; cat++) {
    std::vector<size_t> sizes;
    // Adding size less than this category's minimum
    sizes.push_back(free_list.categories_min[cat] - 8);
    // Adding size equal to this category's minimum
    sizes.push_back(free_list.categories_min[cat]);
    // Adding size greater than this category's minimum
    sizes.push_back(free_list.categories_min[cat] + 8);
    // Adding size between this category's minimum and the next category
    if (cat != free_list.last_category_) {
      sizes.push_back(
          (free_list.categories_min[cat] + free_list.categories_min[cat + 1]) /
          2);
    }

    for (size_t size : sizes) {
      FreeListCategoryType selected =
          free_list.SelectFastAllocationFreeListCategoryType(size);
      if (size <= FreeListManyCachedFastPath::kTinyObjectMaxSize) {
        // For tiny objects, the first category of the fast path should be
        // chosen.
        EXPECT_TRUE(selected ==
                    FreeListManyCachedFastPath::kFastPathFirstCategory);
      } else if (size >= free_list.categories_min[free_list.last_category_] -
                             FreeListManyCachedFastPath::kFastPathOffset) {
        // For objects close to the minimum of the last category, the last
        // category is chosen.
        EXPECT_EQ(selected, free_list.last_category_);
      } else {
        // For other objects, the chosen category must satisfy that its minimum
        // is at least |size|+1.85k.
        EXPECT_GE(free_list.categories_min[selected],
                  size + FreeListManyCachedFastPath::kFastPathOffset);
        // And the smaller categoriy's minimum is less than |size|+1.85k
        // (otherwise it would have been chosen instead).
        EXPECT_LT(free_list.categories_min[selected - 1],
                  size + FreeListManyCachedFastPath::kFastPathOffset);
      }
    }
  }
}

class Observer : public AllocationObserver {
 public:
  explicit Observer(intptr_t step_size)
      : AllocationObserver(step_size), count_(0) {}

  void Step(int bytes_allocated, Address addr, size_t) override { count_++; }

  int count() const { return count_; }

 private:
  int count_;
};

template <typename T, typename A>
void testAllocationObserver(Isolate* i_isolate, T* space, A* allocator) {
  Observer observer1(128);
  i_isolate->heap()->FreeMainThreadLinearAllocationAreas();
  allocator->AddAllocationObserver(&observer1);

  // The observer should not get notified if we have only allocated less than
  // 128 bytes.
  AllocateUnaligned(allocator, space, 64);
  CHECK_EQ(observer1.count(), 0);

  // The observer should get called when we have allocated exactly 128 bytes.
  AllocateUnaligned(allocator, space, 64);
  CHECK_EQ(observer1.count(), 1);

  // Another >128 bytes should get another notification.
  AllocateUnaligned(allocator, space, 136);
  CHECK_EQ(observer1.count(), 2);

  // Allocating a large object should get only one notification.
  AllocateUnaligned(allocator, space, 1024);
  CHECK_EQ(observer1.count(), 3);

  // Allocating another 2048 bytes in small objects should get 16
  // notifications.
  for (int i = 0; i < 64; ++i) {
    AllocateUnaligned(allocator, space, 32);
  }
  CHECK_EQ(observer1.count(), 19);

  // Multiple observers should work.
  Observer observer2(96);
  i_isolate->heap()->FreeMainThreadLinearAllocationAreas();
  allocator->AddAllocationObserver(&observer2);

  AllocateUnaligned(allocator, space, 2048);
  CHECK_EQ(observer1.count(), 20);
  CHECK_EQ(observer2.count(), 1);

  AllocateUnaligned(allocator, space, 104);
  CHECK_EQ(observer1.count(), 20);
  CHECK_EQ(observer2.count(), 2);

  // Callback should stop getting called after an observer is removed.
  allocator->RemoveAllocationObserver(&observer1);

  AllocateUnaligned(allocator, space, 384);
  CHECK_EQ(observer1.count(), 20);  // no more notifications.
  CHECK_EQ(observer2.count(), 3);   // this one is still active.

  // Ensure that PauseInlineAllocationObserversScope work correctly.
  AllocateUnaligned(allocator, space, 48);
  CHECK_EQ(observer2.count(), 3);
  {
    i_isolate->heap()->FreeMainThreadLinearAllocationAreas();
    PauseAllocationObserversScope pause_observers(i_isolate->heap());
    CHECK_EQ(observer2.count(), 3);
    AllocateUnaligned(allocator, space, 384);
    CHECK_EQ(observer2.count(), 3);
    i_isolate->heap()->FreeMainThreadLinearAllocationAreas();
  }
  CHECK_EQ(observer2.count(), 3);
  // Coupled with the 48 bytes allocated before the pause, another 48 bytes
  // allocated here should trigger a notification.
  AllocateUnaligned(allocator, space, 48);
  CHECK_EQ(observer2.count(), 4);

  allocator->RemoveAllocationObserver(&observer2);
  AllocateUnaligned(allocator, space, 384);
  CHECK_EQ(observer1.count(), 20);
  CHECK_EQ(observer2.count(), 4);
}

TEST_F(SpacesTest, AllocationObserver) {
  if (v8_flags.single_generation) return;
  v8::Isolate::Scope isolate_scope(v8_isolate());
  v8::HandleScope handle_scope(v8_isolate());
  v8::Context::New(v8_isolate())->Enter();

  testAllocationObserver<NewSpace>(
      i_isolate(), i_isolate()->heap()->new_space(),
      i_isolate()->heap()->allocator()->new_space_allocator());
  // Old space is used but the code path is shared for all
  // classes inheriting from PagedSpace.
  testAllocationObserver<PagedSpace>(
      i_isolate(), i_isolate()->heap()->old_space(),
      i_isolate()->heap()->allocator()->old_space_allocator());
  testAllocationObserver<OldLargeObjectSpace>(i_isolate(),
                                              i_isolate()->heap()->lo_space(),
                                              i_isolate()->heap()->lo_space());
}

TEST_F(SpacesTest, InlineAllocationObserverCadence) {
  if (v8_flags.single_generation) return;
  v8::Isolate::Scope isolate_scope(v8_isolate());
  v8::HandleScope handle_scope(v8_isolate());
  v8::Context::New(v8_isolate())->Enter();

  // Clear out any pre-existing garbage to make the test consistent
  // across snapshot/no-snapshot builds.
  InvokeMajorGC(i_isolate());

  MainAllocator* new_space_allocator =
      i_isolate()->heap()->allocator()->new_space_allocator();

  Observer observer1(512);
  new_space_allocator->AddAllocationObserver(&observer1);
  Observer observer2(576);
  new_space_allocator->AddAllocationObserver(&observer2);

  for (int i = 0; i < 512; ++i) {
    AllocateUnaligned(new_space_allocator, i_isolate()->heap()->new_space(),
                      32);
  }

  new_space_allocator->RemoveAllocationObserver(&observer1);
  new_space_allocator->RemoveAllocationObserver(&observer2);

  CHECK_EQ(observer1.count(), 32);
  CHECK_EQ(observer2.count(), 28);
}

}  // namespace internal
}  // namespace v8
