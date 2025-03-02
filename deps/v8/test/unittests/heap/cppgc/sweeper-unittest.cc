// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/sweeper.h"

#include <algorithm>

#include "include/cppgc/allocation.h"
#include "include/cppgc/cross-thread-persistent.h"
#include "include/cppgc/persistent.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-visitor.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/object-view.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/stats-collector.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

namespace {

size_t g_destructor_callcount;

template <size_t Size>
class GCed : public GarbageCollected<GCed<Size>> {
 public:
  virtual ~GCed() { ++g_destructor_callcount; }

  virtual void Trace(cppgc::Visitor*) const {}

 private:
  char array[Size];
};

class SweeperTest : public testing::TestWithHeap {
 public:
  SweeperTest() { g_destructor_callcount = 0; }

  void Sweep() {
    Heap* heap = Heap::From(GetHeap());
    ResetLinearAllocationBuffers();
    Sweeper& sweeper = heap->sweeper();
    // Pretend do finish marking as StatsCollector verifies that Notify*
    // methods are called in the right order.
    heap->stats_collector()->NotifyMarkingStarted(
        CollectionType::kMajor, GCConfig::MarkingType::kAtomic,
        GCConfig::IsForcedGC::kNotForced);
    heap->stats_collector()->NotifyMarkingCompleted(0);
    const SweepingConfig sweeping_config{
        SweepingConfig::SweepingType::kAtomic,
        SweepingConfig::CompactableSpaceHandling::kSweep};
    sweeper.Start(sweeping_config);
    sweeper.FinishIfRunning();
  }

  void MarkObject(void* payload) {
    HeapObjectHeader& header = HeapObjectHeader::FromObject(payload);
    header.TryMarkAtomic();
    BasePage* page = BasePage::FromPayload(&header);
    page->IncrementMarkedBytes(page->is_large()
                                   ? LargePage::From(page)->PayloadSize()
                                   : header.AllocatedSize());
  }

  PageBackend* GetBackend() { return Heap::From(GetHeap())->page_backend(); }
};

}  // namespace

TEST_F(SweeperTest, SweepUnmarkedNormalObject) {
  constexpr size_t kObjectSize = 8;
  using Type = GCed<kObjectSize>;

  MakeGarbageCollected<Type>(GetAllocationHandle());

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(1u, g_destructor_callcount);
}

TEST_F(SweeperTest, DontSweepMarkedNormalObject) {
  constexpr size_t kObjectSize = 8;
  using Type = GCed<kObjectSize>;

  auto* object = MakeGarbageCollected<Type>(GetAllocationHandle());
  MarkObject(object);
  BasePage* page = BasePage::FromPayload(object);
  BaseSpace& space = page->space();

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(0u, g_destructor_callcount);
  // Check that page is returned back to the space.
  EXPECT_NE(space.end(), std::find(space.begin(), space.end(), page));
  EXPECT_NE(nullptr, GetBackend()->Lookup(reinterpret_cast<Address>(object)));
}

TEST_F(SweeperTest, SweepUnmarkedLargeObject) {
  constexpr size_t kObjectSize = kLargeObjectSizeThreshold * 2;
  using Type = GCed<kObjectSize>;

  auto* object = MakeGarbageCollected<Type>(GetAllocationHandle());
  BasePage* page = BasePage::FromPayload(object);
  BaseSpace& space = page->space();

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(1u, g_destructor_callcount);
  // Check that page is gone.
  EXPECT_EQ(space.end(), std::find(space.begin(), space.end(), page));
  EXPECT_EQ(nullptr, GetBackend()->Lookup(reinterpret_cast<Address>(object)));
}

TEST_F(SweeperTest, DontSweepMarkedLargeObject) {
  constexpr size_t kObjectSize = kLargeObjectSizeThreshold * 2;
  using Type = GCed<kObjectSize>;

  auto* object = MakeGarbageCollected<Type>(GetAllocationHandle());
  MarkObject(object);
  BasePage* page = BasePage::FromPayload(object);
  BaseSpace& space = page->space();

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(0u, g_destructor_callcount);
  // Check that page is returned back to the space.
  EXPECT_NE(space.end(), std::find(space.begin(), space.end(), page));
  EXPECT_NE(nullptr, GetBackend()->Lookup(reinterpret_cast<Address>(object)));
}

TEST_F(SweeperTest, SweepMultipleObjectsOnPage) {
  constexpr size_t kObjectSize = 8;
  using Type = GCed<kObjectSize>;
  const size_t kNumberOfObjects =
      NormalPage::PayloadSize() / (sizeof(Type) + sizeof(HeapObjectHeader));

  for (size_t i = 0; i < kNumberOfObjects; ++i) {
    MakeGarbageCollected<Type>(GetAllocationHandle());
  }

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(kNumberOfObjects, g_destructor_callcount);
}

TEST_F(SweeperTest, SweepObjectsOnAllArenas) {
  MakeGarbageCollected<GCed<1>>(GetAllocationHandle());
  MakeGarbageCollected<GCed<32>>(GetAllocationHandle());
  MakeGarbageCollected<GCed<64>>(GetAllocationHandle());
  MakeGarbageCollected<GCed<128>>(GetAllocationHandle());
  MakeGarbageCollected<GCed<2 * kLargeObjectSizeThreshold>>(
      GetAllocationHandle());

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(5u, g_destructor_callcount);
}

TEST_F(SweeperTest, SweepMultiplePagesInSingleSpace) {
  MakeGarbageCollected<GCed<2 * kLargeObjectSizeThreshold>>(
      GetAllocationHandle());
  MakeGarbageCollected<GCed<2 * kLargeObjectSizeThreshold>>(
      GetAllocationHandle());
  MakeGarbageCollected<GCed<2 * kLargeObjectSizeThreshold>>(
      GetAllocationHandle());

  EXPECT_EQ(0u, g_destructor_callcount);

  Sweep();

  EXPECT_EQ(3u, g_destructor_callcount);
}

TEST_F(SweeperTest, CoalesceFreeListEntries) {
  constexpr size_t kObjectSize = 32;
  using Type = GCed<kObjectSize>;

  auto* object1 = MakeGarbageCollected<Type>(GetAllocationHandle());
  auto* object2 = MakeGarbageCollected<Type>(GetAllocationHandle());
  auto* object3 = MakeGarbageCollected<Type>(GetAllocationHandle());
  auto* object4 = MakeGarbageCollected<Type>(GetAllocationHandle());

  MarkObject(object1);
  MarkObject(object4);

  Address object2_start =
      reinterpret_cast<Address>(&HeapObjectHeader::FromObject(object2));
  Address object3_end =
      reinterpret_cast<Address>(&HeapObjectHeader::FromObject(object3)) +
      HeapObjectHeader::FromObject(object3).AllocatedSize();

  const BasePage* page = BasePage::FromPayload(object2);
  const FreeList& freelist = NormalPageSpace::From(page->space()).free_list();

  const FreeList::Block coalesced_block = {
      object2_start, static_cast<size_t>(object3_end - object2_start)};

  EXPECT_EQ(0u, g_destructor_callcount);
  EXPECT_FALSE(freelist.ContainsForTesting(coalesced_block));

  Sweep();

  EXPECT_EQ(2u, g_destructor_callcount);
  EXPECT_TRUE(freelist.ContainsForTesting(coalesced_block));
}

namespace {

class GCInDestructor final : public GarbageCollected<GCInDestructor> {
 public:
  explicit GCInDestructor(Heap* heap) : heap_(heap) {}
  ~GCInDestructor() {
    // Instead of directly calling GC, allocations should be supported here as
    // well.
    heap_->CollectGarbage(internal::GCConfig::ConservativeAtomicConfig());
  }
  void Trace(Visitor*) const {}

 private:
  Heap* heap_;
};

}  // namespace

TEST_F(SweeperTest, SweepDoesNotTriggerRecursiveGC) {
  auto* internal_heap = internal::Heap::From(GetHeap());
  size_t saved_epoch = internal_heap->epoch();
  MakeGarbageCollected<GCInDestructor>(GetAllocationHandle(), internal_heap);
  PreciseGC();
  EXPECT_EQ(saved_epoch + 1, internal_heap->epoch());
}

TEST_F(SweeperTest, UnmarkObjects) {
  auto* normal_object = MakeGarbageCollected<GCed<32>>(GetAllocationHandle());
  auto* large_object =
      MakeGarbageCollected<GCed<kLargeObjectSizeThreshold * 2>>(
          GetAllocationHandle());

  auto& normal_object_header = HeapObjectHeader::FromObject(normal_object);
  auto& large_object_header = HeapObjectHeader::FromObject(large_object);

  MarkObject(normal_object);
  MarkObject(large_object);

  EXPECT_TRUE(normal_object_header.IsMarked());
  EXPECT_TRUE(large_object_header.IsMarked());

  Sweep();

  if (Heap::From(GetHeap())->generational_gc_supported()) {
    EXPECT_TRUE(normal_object_header.IsMarked());
    EXPECT_TRUE(large_object_header.IsMarked());
  } else {
    EXPECT_FALSE(normal_object_header.IsMarked());
    EXPECT_FALSE(large_object_header.IsMarked());
  }
}

TEST_F(SweeperTest, LazySweepingDuringAllocation) {
  // The test allocates objects in such a way that the object with its header is
  // power of two. This is to make sure that if there is some padding at the end
  // of the page, it will go to a different freelist bucket. To get that,
  // subtract vptr and object-header-size from a power-of-two.
  static constexpr size_t kGCObjectSize =
      256 - sizeof(void*) - sizeof(HeapObjectHeader);
  using GCedObject = GCed<kGCObjectSize>;
  static_assert(v8::base::bits::IsPowerOfTwo(sizeof(GCedObject) +
                                             sizeof(HeapObjectHeader)));

  static const size_t kObjectsPerPage =
      NormalPage::PayloadSize() /
      (sizeof(GCedObject) + sizeof(HeapObjectHeader));
  // This test expects each page contain at least 2 objects.
  DCHECK_LT(2u, kObjectsPerPage);
  PreciseGC();
  std::vector<Persistent<GCedObject>> first_page;
  first_page.push_back(MakeGarbageCollected<GCedObject>(GetAllocationHandle()));
  GCedObject* expected_address_on_first_page =
      MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  for (size_t i = 2; i < kObjectsPerPage; ++i) {
    first_page.push_back(
        MakeGarbageCollected<GCedObject>(GetAllocationHandle()));
  }
  std::vector<Persistent<GCedObject>> second_page;
  second_page.push_back(
      MakeGarbageCollected<GCedObject>(GetAllocationHandle()));
  GCedObject* expected_address_on_second_page =
      MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  for (size_t i = 2; i < kObjectsPerPage; ++i) {
    second_page.push_back(
        MakeGarbageCollected<GCedObject>(GetAllocationHandle()));
  }
  testing::TestPlatform::DisableBackgroundTasksScope no_concurrent_sweep_scope(
      GetPlatformHandle().get());
  g_destructor_callcount = 0;
  static constexpr GCConfig config = {
      CollectionType::kMajor, StackState::kNoHeapPointers,
      GCConfig::MarkingType::kAtomic,
      GCConfig::SweepingType::kIncrementalAndConcurrent};
  Heap::From(GetHeap())->CollectGarbage(config);
  // Incremental sweeping is active and the space should have two pages with
  // no room for an additional GCedObject. Allocating a new GCedObject should
  // trigger sweeping. All objects other than the 2nd object on each page are
  // marked. Lazy sweeping on allocation should reclaim the object on one of
  // the pages and reuse its memory. The object on the other page should remain
  // un-reclaimed. To confirm: the newly object will be allcoated at one of the
  // expected addresses and the GCedObject destructor is only called once.
  GCedObject* new_object1 =
      MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  EXPECT_EQ(1u, g_destructor_callcount);
  EXPECT_TRUE((new_object1 == expected_address_on_first_page) ||
              (new_object1 == expected_address_on_second_page));
  // Allocating again should reclaim the other unmarked object and reuse its
  // memory. The destructor will be called again and the new object will be
  // allocated in one of the expected addresses but not the same one as before.
  GCedObject* new_object2 =
      MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  EXPECT_EQ(2u, g_destructor_callcount);
  EXPECT_TRUE((new_object2 == expected_address_on_first_page) ||
              (new_object2 == expected_address_on_second_page));
  EXPECT_NE(new_object1, new_object2);
}

TEST_F(SweeperTest, LazySweepingNormalPages) {
  using GCedObject = GCed<sizeof(size_t)>;
  EXPECT_EQ(0u, g_destructor_callcount);
  PreciseGC();
  EXPECT_EQ(0u, g_destructor_callcount);
  MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  static constexpr GCConfig config = {
      CollectionType::kMajor, StackState::kNoHeapPointers,
      GCConfig::MarkingType::kAtomic,
      // Sweeping type must not include concurrent as that could lead to the
      // concurrent sweeper holding onto pages in rare cases which delays
      // reclamation of objects.
      GCConfig::SweepingType::kIncremental};
  Heap::From(GetHeap())->CollectGarbage(config);
  EXPECT_EQ(0u, g_destructor_callcount);
  MakeGarbageCollected<GCedObject>(GetAllocationHandle());
  EXPECT_EQ(1u, g_destructor_callcount);
  PreciseGC();
  EXPECT_EQ(2u, g_destructor_callcount);
}

namespace {
class AllocatingFinalizer : public GarbageCollected<AllocatingFinalizer> {
 public:
  static size_t destructor_callcount_;
  explicit AllocatingFinalizer(AllocationHandle& allocation_handle)
      : allocation_handle_(allocation_handle) {}
  ~AllocatingFinalizer() {
    MakeGarbageCollected<GCed<sizeof(size_t)>>(allocation_handle_);
    ++destructor_callcount_;
  }
  void Trace(Visitor*) const {}

 private:
  AllocationHandle& allocation_handle_;
};
size_t AllocatingFinalizer::destructor_callcount_ = 0;
}  // namespace

TEST_F(SweeperTest, AllocationDuringFinalizationIsNotSwept) {
  AllocatingFinalizer::destructor_callcount_ = 0;
  g_destructor_callcount = 0;
  MakeGarbageCollected<AllocatingFinalizer>(GetAllocationHandle(),
                                            GetAllocationHandle());
  PreciseGC();
  EXPECT_LT(0u, AllocatingFinalizer::destructor_callcount_);
  EXPECT_EQ(0u, g_destructor_callcount);
}

TEST_F(SweeperTest, DiscardingNormalPageMemory) {
  if (!Sweeper::CanDiscardMemory()) return;

  // Test ensures that free list payload is discarded and accounted for on page
  // level.
  auto* holder = MakeGarbageCollected<GCed<1>>(GetAllocationHandle());
  ConservativeMemoryDiscardingGC();
  auto* page = NormalPage::FromPayload(holder);
  // Assume the `holder` object is the first on the page for simplifying exact
  // discarded count.
  ASSERT_EQ(static_cast<void*>(page->PayloadStart() + sizeof(HeapObjectHeader)),
            holder);
  // No other object on the page is live.
  Address free_list_payload_start =
      page->PayloadStart() +
      HeapObjectHeader::FromObject(holder).AllocatedSize() +
      sizeof(kFreeListEntrySize);
  uintptr_t start =
      RoundUp(reinterpret_cast<uintptr_t>(free_list_payload_start),
              GetPlatform().GetPageAllocator()->CommitPageSize());
  uintptr_t end = RoundDown(reinterpret_cast<uintptr_t>(page->PayloadEnd()),
                            GetPlatform().GetPageAllocator()->CommitPageSize());
  EXPECT_GT(end, start);
  EXPECT_EQ(page->discarded_memory(), end - start);
  USE(holder);
}

namespace {

class Holder final : public GarbageCollected<Holder> {
 public:
  static size_t destructor_callcount;

  void Trace(Visitor*) const {}

  ~Holder() {
    EXPECT_FALSE(ref);
    EXPECT_FALSE(weak_ref);
    destructor_callcount++;
  }

  cppgc::subtle::CrossThreadPersistent<GCed<1>> ref;
  cppgc::subtle::WeakCrossThreadPersistent<GCed<1>> weak_ref;
};

// static
size_t Holder::destructor_callcount;

}  // namespace

TEST_F(SweeperTest, CrossThreadPersistentCanBeClearedFromOtherThread) {
  Holder::destructor_callcount = 0;
  auto* holder = MakeGarbageCollected<Holder>(GetAllocationHandle());

  auto remote_heap = cppgc::Heap::Create(GetPlatformHandle());
  // The case below must be able to clear both, the CTP and WCTP.
  holder->ref =
      MakeGarbageCollected<GCed<1>>(remote_heap->GetAllocationHandle());
  holder->weak_ref =
      MakeGarbageCollected<GCed<1>>(remote_heap->GetAllocationHandle());

  testing::TestPlatform::DisableBackgroundTasksScope no_concurrent_sweep_scope(
      GetPlatformHandle().get());
  Heap::From(GetHeap())->CollectGarbage(
      {CollectionType::kMajor, StackState::kNoHeapPointers,
       GCConfig::MarkingType::kAtomic,
       GCConfig::SweepingType::kIncrementalAndConcurrent});
  // `holder` is unreachable (as the stack is not scanned) and will be
  // reclaimed. Its payload memory is generally poisoned at this point. The
  // CrossThreadPersistent slot should be unpoisoned.

  // Terminate the remote heap which should also clear `holder->ref`. The slot
  // for `ref` should have been unpoisoned by the GC.
  Heap::From(remote_heap.get())->Terminate();

  // Finish the sweeper which will find the CrossThreadPersistent in cleared
  // state.
  Heap::From(GetHeap())->sweeper().FinishIfRunning();
  EXPECT_EQ(1u, Holder::destructor_callcount);
}

TEST_F(SweeperTest, WeakCrossThreadPersistentCanBeClearedFromOtherThread) {
  Holder::destructor_callcount = 0;
  auto* holder = MakeGarbageCollected<Holder>(GetAllocationHandle());

  auto remote_heap = cppgc::Heap::Create(GetPlatformHandle());
  holder->weak_ref =
      MakeGarbageCollected<GCed<1>>(remote_heap->GetAllocationHandle());

  testing::TestPlatform::DisableBackgroundTasksScope no_concurrent_sweep_scope(
      GetPlatformHandle().get());
  static constexpr GCConfig config = {
      CollectionType::kMajor, StackState::kNoHeapPointers,
      GCConfig::MarkingType::kAtomic,
      GCConfig::SweepingType::kIncrementalAndConcurrent};
  Heap::From(GetHeap())->CollectGarbage(config);
  // `holder` is unreachable (as the stack is not scanned) and will be
  // reclaimed. Its payload memory is generally poisoned at this point. The
  // WeakCrossThreadPersistent slot should be unpoisoned during clearing.

  // GC in the remote heap should also clear `holder->weak_ref`. The slot for
  // `weak_ref` should be unpoisoned by the GC.
  Heap::From(remote_heap.get())
      ->CollectGarbage({CollectionType::kMajor, StackState::kNoHeapPointers,
                        GCConfig::MarkingType::kAtomic,
                        GCConfig::SweepingType::kAtomic});

  // Finish the sweeper which will find the CrossThreadPersistent in cleared
  // state.
  Heap::From(GetHeap())->sweeper().FinishIfRunning();
  EXPECT_EQ(1u, Holder::destructor_callcount);
}

TEST_F(SweeperTest, SweepOnAllocationTakeLastFreeListEntry) {
  // The test allocates the following layout:
  // |--object-A--|-object-B-|--object-A--|---free-space---|
  // Objects A are reachable, whereas object B is not. sizeof(B) is smaller than
  // that of A. The test starts garbage-collection with lazy sweeping, then
  // tries to allocate object A, expecting the allocation to end up on the same
  // page at the free-space.
  using GCedA = GCed<256>;
  using GCedB = GCed<240>;

  PreciseGC();

  // Allocate the layout.
  Persistent<GCedA> a1 = MakeGarbageCollected<GCedA>(GetAllocationHandle());
  MakeGarbageCollected<GCedB>(GetAllocationHandle());
  Persistent<GCedA> a2 = MakeGarbageCollected<GCedA>(GetAllocationHandle());
  ConstAddress free_space_start =
      ObjectView<>(HeapObjectHeader::FromObject(a2.Get())).End();

  // Start the GC without sweeping.
  testing::TestPlatform::DisableBackgroundTasksScope no_concurrent_sweep_scope(
      GetPlatformHandle().get());
  static constexpr GCConfig config = {
      CollectionType::kMajor, StackState::kNoHeapPointers,
      GCConfig::MarkingType::kAtomic,
      GCConfig::SweepingType::kIncrementalAndConcurrent};
  Heap::From(GetHeap())->CollectGarbage(config);

  // Allocate and sweep.
  const GCedA* allocated_after_sweeping =
      MakeGarbageCollected<GCedA>(GetAllocationHandle());
  EXPECT_EQ(free_space_start,
            reinterpret_cast<ConstAddress>(
                &HeapObjectHeader::FromObject(allocated_after_sweeping)));
}

}  // namespace internal
}  // namespace cppgc
