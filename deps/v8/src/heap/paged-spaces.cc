// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/paged-spaces.h"

#include <atomic>

#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/execution/vm-state-inl.h"
#include "src/flags/flags.h"
#include "src/heap/allocation-observer.h"
#include "src/heap/array-buffer-sweeper.h"
#include "src/heap/free-list-inl.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/paged-spaces-inl.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/safepoint.h"
#include "src/heap/spaces.h"
#include "src/heap/sweeper.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/objects/string.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

// ----------------------------------------------------------------------------
// PagedSpaceObjectIterator

PagedSpaceObjectIterator::PagedSpaceObjectIterator(Heap* heap,
                                                   const PagedSpaceBase* space)
    : space_(space),
      page_range_(space->first_page(), nullptr),
      current_page_(page_range_.begin()) {
  heap->MakeHeapIterable();
  USE(space_);
}

// We have hit the end of the page and should advance to the next block of
// objects.  This happens at the end of the page.
bool PagedSpaceObjectIterator::AdvanceToNextPage() {
  if (current_page_ == page_range_.end()) return false;
  const PageMetadata* cur_page = *(current_page_++);
  HeapObjectRange heap_objects(cur_page);
  cur_ = heap_objects.begin();
  end_ = heap_objects.end();
  return true;
}

// ----------------------------------------------------------------------------
// PagedSpaceBase implementation

PagedSpaceBase::PagedSpaceBase(Heap* heap, AllocationSpace space,
                               Executability executable,
                               std::unique_ptr<FreeList> free_list,
                               CompactionSpaceKind compaction_space_kind)
    : SpaceWithLinearArea(heap, space, std::move(free_list)),
      executable_(executable),
      compaction_space_kind_(compaction_space_kind) {
  area_size_ = MemoryChunkLayout::AllocatableMemoryInMemoryChunk(space);
  accounting_stats_.Clear();
}

PageMetadata* PagedSpaceBase::InitializePage(
    MutablePageMetadata* mutable_page_metadata) {
  MemoryChunk* chunk = mutable_page_metadata->Chunk();
  PageMetadata* page = PageMetadata::cast(mutable_page_metadata);
  DCHECK_EQ(
      MemoryChunkLayout::AllocatableMemoryInMemoryChunk(page->owner_identity()),
      page->area_size());
  // Make sure that categories are initialized before freeing the area.
  page->ResetAllocationStatistics();
  page->AllocateFreeListCategories();
  page->InitializeFreeListCategories();
  page->list_node().Initialize();
  chunk->InitializationMemoryFence();
  return page;
}

void PagedSpaceBase::TearDown() {
  while (!memory_chunk_list_.Empty()) {
    MutablePageMetadata* chunk = memory_chunk_list_.front();
    memory_chunk_list_.Remove(chunk);
    heap()->memory_allocator()->Free(MemoryAllocator::FreeMode::kImmediately,
                                     chunk);
  }
  accounting_stats_.Clear();
}

void PagedSpaceBase::MergeCompactionSpace(CompactionSpace* other) {
  base::MutexGuard guard(mutex());

  DCHECK_NE(NEW_SPACE, identity());
  DCHECK_NE(NEW_SPACE, other->identity());

  // Move over pages.
  for (auto it = other->begin(); it != other->end();) {
    PageMetadata* p = *(it++);

    // Ensure that pages are initialized before objects on it are discovered by
    // concurrent markers.
    p->Chunk()->InitializationMemoryFence();

    // Relinking requires the category to be unlinked.
    other->RemovePage(p);
    AddPage(p);
    DCHECK_IMPLIES(
        !p->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE),
        p->AvailableInFreeList() == p->AvailableInFreeListFromAllocatedBytes());

    // TODO(leszeks): Here we should allocation step, but:
    //   1. Allocation groups are currently not handled properly by the sampling
    //      allocation profiler, and
    //   2. Observers might try to take the space lock, which isn't reentrant.
    // We'll have to come up with a better solution for allocation stepping
    // before shipping, which will likely be using LocalHeap.
  }
  for (auto p : other->GetNewPages()) {
    heap()->NotifyOldGenerationExpansion(heap()->main_thread_local_heap(),
                                         identity(), p);
  }

  DCHECK_EQ(0u, other->Size());
  DCHECK_EQ(0u, other->Capacity());
}

size_t PagedSpaceBase::CommittedPhysicalMemory() const {
  if (!base::OS::HasLazyCommits()) {
    DCHECK_EQ(0, committed_physical_memory());
    return CommittedMemory();
  }
  return committed_physical_memory();
}

void PagedSpaceBase::IncrementCommittedPhysicalMemory(size_t increment_value) {
  if (!base::OS::HasLazyCommits() || increment_value == 0) return;
  size_t old_value = committed_physical_memory_.fetch_add(
      increment_value, std::memory_order_relaxed);
  USE(old_value);
  DCHECK_LT(old_value, old_value + increment_value);
}

void PagedSpaceBase::DecrementCommittedPhysicalMemory(size_t decrement_value) {
  if (!base::OS::HasLazyCommits() || decrement_value == 0) return;
  size_t old_value = committed_physical_memory_.fetch_sub(
      decrement_value, std::memory_order_relaxed);
  USE(old_value);
  DCHECK_GT(old_value, old_value - decrement_value);
}

#if DEBUG
void PagedSpaceBase::VerifyCommittedPhysicalMemory() const {
  heap()->safepoint()->AssertActive();
  size_t size = 0;
  for (const PageMetadata* page : *this) {
    DCHECK(page->SweepingDone());
    size += page->CommittedPhysicalMemory();
  }
  // Ensure that the space's counter matches the sum of all page counters.
  DCHECK_EQ(size, CommittedPhysicalMemory());
}
#endif  // DEBUG

bool PagedSpaceBase::ContainsSlow(Address addr) const {
  PageMetadata* p = PageMetadata::FromAddress(addr);
  for (const PageMetadata* page : *this) {
    if (page == p) return true;
  }
  return false;
}

void PagedSpaceBase::RefineAllocatedBytesAfterSweeping(PageMetadata* page) {
  CHECK(page->SweepingDone());
  // The live_byte on the page was accounted in the space allocated
  // bytes counter. After sweeping allocated_bytes() contains the
  // accurate live byte count on the page.
  size_t old_counter = page->live_bytes();
  size_t new_counter = page->allocated_bytes();
  DCHECK_GE(old_counter, new_counter);
  if (old_counter > new_counter) {
    size_t counter_diff = old_counter - new_counter;
    if (identity() == NEW_SPACE) size_at_last_gc_ -= counter_diff;
    DecreaseAllocatedBytes(counter_diff, page);
    DCHECK_EQ(new_counter, accounting_stats_.AllocatedOnPage(page));
    AdjustDifferenceInAllocatedBytes(counter_diff);
  }
  if (!v8_flags.sticky_mark_bits) {
    // With sticky mark-bits the counter is reset on unmarking.
    page->SetLiveBytes(0);
  }
}

PageMetadata* PagedSpaceBase::RemovePageSafe(int size_in_bytes) {
  base::MutexGuard guard(mutex());
  PageMetadata* page = free_list()->GetPageForSize(size_in_bytes);
  if (!page) return nullptr;
  RemovePage(page);
  return page;
}

void PagedSpaceBase::AddPageImpl(PageMetadata* page) {
  DCHECK_NOT_NULL(page);
  CHECK(page->SweepingDone());
  page->set_owner(this);
  DCHECK_IMPLIES(identity() == NEW_SPACE,
                 page->Chunk()->IsFlagSet(MemoryChunk::TO_PAGE));
  DCHECK_IMPLIES(identity() != NEW_SPACE,
                 !page->Chunk()->IsFlagSet(MemoryChunk::TO_PAGE));
  memory_chunk_list_.PushBack(page);
  AccountCommitted(page->size());
  IncreaseCapacity(page->area_size());
  IncreaseAllocatedBytes(page->allocated_bytes(), page);
  ForAll<ExternalBackingStoreType>(
      [this, page](ExternalBackingStoreType type, int index) {
        IncrementExternalBackingStoreBytes(
            type, page->ExternalBackingStoreBytes(type));
      });
  IncrementCommittedPhysicalMemory(page->CommittedPhysicalMemory());
}

size_t PagedSpaceBase::AddPage(PageMetadata* page) {
  AddPageImpl(page);
  return RelinkFreeListCategories(page);
}

void PagedSpaceBase::RemovePage(PageMetadata* page) {
  CHECK(page->SweepingDone());
  DCHECK_IMPLIES(identity() == NEW_SPACE,
                 page->Chunk()->IsFlagSet(MemoryChunk::TO_PAGE));
  memory_chunk_list_.Remove(page);
  UnlinkFreeListCategories(page);
  // Pages are only removed from new space when they are promoted to old space
  // during a GC. This happens after sweeping as started and the allocation
  // counters have been reset.
  DCHECK_IMPLIES(identity() == NEW_SPACE,
                 heap()->gc_state() != Heap::NOT_IN_GC);
  if (identity() == NEW_SPACE) {
    page->ReleaseFreeListCategories();
  } else {
    DecreaseAllocatedBytes(page->allocated_bytes(), page);
    free_list()->decrease_wasted_bytes(page->wasted_memory());
  }
  DecreaseCapacity(page->area_size());
  AccountUncommitted(page->size());
  ForAll<ExternalBackingStoreType>(
      [this, page](ExternalBackingStoreType type, int index) {
        DecrementExternalBackingStoreBytes(
            type, page->ExternalBackingStoreBytes(type));
      });
  DecrementCommittedPhysicalMemory(page->CommittedPhysicalMemory());
}

size_t PagedSpaceBase::ShrinkPageToHighWaterMark(PageMetadata* page) {
  size_t unused = page->ShrinkToHighWaterMark();
  accounting_stats_.DecreaseCapacity(static_cast<intptr_t>(unused));
  AccountUncommitted(unused);
  return unused;
}

void PagedSpaceBase::ResetFreeList() {
  for (PageMetadata* page : *this) {
    free_list_->EvictFreeListItems(page);
  }
  DCHECK(free_list_->IsEmpty());
  DCHECK_EQ(0, free_list_->Available());
}

void PagedSpaceBase::ShrinkImmortalImmovablePages() {
  DCHECK(!heap()->deserialization_complete());
  ResetFreeList();
  for (PageMetadata* page : *this) {
    DCHECK(page->Chunk()->IsFlagSet(MemoryChunk::NEVER_EVACUATE));
    ShrinkPageToHighWaterMark(page);
  }
}

bool PagedSpaceBase::TryExpand(LocalHeap* local_heap, AllocationOrigin origin) {
  DCHECK_EQ(!local_heap, origin == AllocationOrigin::kGC);
  const size_t accounted_size =
      MemoryChunkLayout::AllocatableMemoryInMemoryChunk(identity());
  if (origin != AllocationOrigin::kGC && identity() != NEW_SPACE) {
    base::MutexGuard expansion_guard(heap_->heap_expansion_mutex());
    if (!heap()->IsOldGenerationExpansionAllowed(accounted_size,
                                                 expansion_guard)) {
      return false;
    }
  }
  const MemoryAllocator::AllocationMode allocation_mode =
      (identity() == NEW_SPACE || identity() == OLD_SPACE)
          ? MemoryAllocator::AllocationMode::kUsePool
          : MemoryAllocator::AllocationMode::kRegular;
  PageMetadata* page = heap()->memory_allocator()->AllocatePage(
      allocation_mode, this, executable());
  if (page == nullptr) return false;
  DCHECK_EQ(page->area_size(), accounted_size);
  ConcurrentAllocationMutex guard(this);
  AddPage(page);
  if (origin != AllocationOrigin::kGC && identity() != NEW_SPACE) {
    heap()->NotifyOldGenerationExpansion(local_heap, identity(), page);
  }
  Free(page->area_start(), page->area_size());
  NotifyNewPage(page);
  return true;
}

int PagedSpaceBase::CountTotalPages() const {
  int count = 0;
  for (const PageMetadata* page : *this) {
    count++;
    USE(page);
  }
  return count;
}

size_t PagedSpaceBase::Available() const {
  ConcurrentAllocationMutex guard(this);
  return free_list_->Available();
}

size_t PagedSpaceBase::Waste() const {
  return free_list_->wasted_bytes();
}

void PagedSpaceBase::ReleasePage(PageMetadata* page) {
  ReleasePageImpl(page, MemoryAllocator::FreeMode::kImmediately);
}

void PagedSpaceBase::ReleasePageImpl(PageMetadata* page,
                                     MemoryAllocator::FreeMode free_mode) {
  DCHECK(page->SweepingDone());
  DCHECK_EQ(0, page->live_bytes());
  DCHECK_EQ(page->owner(), this);

  DCHECK_IMPLIES(identity() == NEW_SPACE,
                 page->Chunk()->IsFlagSet(MemoryChunk::TO_PAGE));

  memory_chunk_list().Remove(page);

  free_list_->EvictFreeListItems(page);

  if (identity() == CODE_SPACE) {
    heap()->isolate()->RemoveCodeMemoryChunk(page);
  }

  AccountUncommitted(page->size());
  DecrementCommittedPhysicalMemory(page->CommittedPhysicalMemory());
  accounting_stats_.DecreaseCapacity(page->area_size());
  heap()->memory_allocator()->Free(free_mode, page);
}

std::unique_ptr<ObjectIterator> PagedSpaceBase::GetObjectIterator(Heap* heap) {
  return std::unique_ptr<ObjectIterator>(
      new PagedSpaceObjectIterator(heap, this));
}

#ifdef DEBUG
void PagedSpaceBase::Print() {}
#endif

#ifdef VERIFY_HEAP
void PagedSpaceBase::Verify(Isolate* isolate,
                            SpaceVerificationVisitor* visitor) const {
  CHECK_IMPLIES(identity() != NEW_SPACE, size_at_last_gc_ == 0);

  size_t external_space_bytes[static_cast<int>(
      ExternalBackingStoreType::kNumValues)] = {0};
  PtrComprCageBase cage_base(isolate);
  for (const PageMetadata* page : *this) {
    size_t external_page_bytes[static_cast<int>(
        ExternalBackingStoreType::kNumValues)] = {0};

    CHECK_EQ(page->owner(), this);
    CHECK_IMPLIES(identity() != NEW_SPACE, page->AllocatedLabSize() == 0);
    visitor->VerifyPage(page);

    CHECK(page->SweepingDone());
    Address end_of_previous_object = page->area_start();
    Address top = page->area_end();

    for (Tagged<HeapObject> object : HeapObjectRange(page)) {
      CHECK(end_of_previous_object <= object.address());

      // Invoke verification method for each object.
      visitor->VerifyObject(object);

      // All the interior pointers should be contained in the heap.
      int size = object->Size(cage_base);
      CHECK(object.address() + size <= top);
      end_of_previous_object = object.address() + size;

      if (IsExternalString(object, cage_base)) {
        Tagged<ExternalString> external_string = Cast<ExternalString>(object);
        size_t payload_size = external_string->ExternalPayloadSize();
        external_page_bytes[static_cast<int>(
            ExternalBackingStoreType::kExternalString)] += payload_size;
      }
    }
    ForAll<ExternalBackingStoreType>(
        [page, external_page_bytes, &external_space_bytes](
            ExternalBackingStoreType type, int index) {
          CHECK_EQ(external_page_bytes[index],
                   page->ExternalBackingStoreBytes(type));
          external_space_bytes[index] += external_page_bytes[index];
        });

    visitor->VerifyPageDone(page);
  }
  ForAll<ExternalBackingStoreType>(
      [this, external_space_bytes](ExternalBackingStoreType type, int index) {
        if (type == ExternalBackingStoreType::kArrayBuffer) {
          return;
        }
        CHECK_EQ(external_space_bytes[index], ExternalBackingStoreBytes(type));
      });

  if (!v8_flags.concurrent_array_buffer_sweeping) {
    if (identity() == OLD_SPACE) {
      size_t bytes = heap()->array_buffer_sweeper()->old().BytesSlow();
      CHECK_EQ(bytes, ExternalBackingStoreBytes(
                          ExternalBackingStoreType::kArrayBuffer));
    } else if (identity() == NEW_SPACE) {
      DCHECK(v8_flags.minor_ms);
      size_t bytes = heap()->array_buffer_sweeper()->young().BytesSlow();
      CHECK_EQ(bytes, ExternalBackingStoreBytes(
                          ExternalBackingStoreType::kArrayBuffer));
    }
  }

#ifdef DEBUG
  VerifyCountersAfterSweeping(isolate->heap());
#endif
}

void PagedSpaceBase::VerifyLiveBytes() const {
  MarkingState* marking_state = heap()->marking_state();
  PtrComprCageBase cage_base(heap()->isolate());
  for (const PageMetadata* page : *this) {
    CHECK(page->SweepingDone());
    int black_size = 0;
    for (Tagged<HeapObject> object : HeapObjectRange(page)) {
      // All the interior pointers should be contained in the heap.
      if (marking_state->IsMarked(object)) {
        black_size += object->Size(cage_base);
      }
    }
    CHECK_LE(black_size, page->live_bytes());
  }
}
#endif  // VERIFY_HEAP

#ifdef DEBUG
void PagedSpaceBase::VerifyCountersAfterSweeping(Heap* heap) const {
  size_t total_capacity = 0;
  size_t total_allocated = 0;
  PtrComprCageBase cage_base(heap->isolate());
  for (const PageMetadata* page : *this) {
    DCHECK(page->SweepingDone());
    total_capacity += page->area_size();
    size_t real_allocated = 0;
    for (Tagged<HeapObject> object : HeapObjectRange(page)) {
      if (!IsFreeSpaceOrFiller(object)) {
        real_allocated +=
            ALIGN_TO_ALLOCATION_ALIGNMENT(object->Size(cage_base));
      }
    }
    total_allocated += page->allocated_bytes();
    // The real size can be smaller than the accounted size if array trimming,
    // object slack tracking happened after sweeping.
    DCHECK_LE(real_allocated, accounting_stats_.AllocatedOnPage(page));
    DCHECK_EQ(page->allocated_bytes(), accounting_stats_.AllocatedOnPage(page));
  }
  DCHECK_EQ(total_capacity, accounting_stats_.Capacity());
  DCHECK_EQ(total_allocated, accounting_stats_.Size());
}

void PagedSpaceBase::VerifyCountersBeforeConcurrentSweeping() const {
  size_t total_capacity = 0;
  size_t total_allocated = 0;
  for (const PageMetadata* page : *this) {
    size_t page_allocated =
        page->SweepingDone() ? page->allocated_bytes() : page->live_bytes();
    total_capacity += page->area_size();
    total_allocated += page_allocated;
    DCHECK_EQ(page_allocated, accounting_stats_.AllocatedOnPage(page));
  }
  DCHECK_EQ(total_capacity, accounting_stats_.Capacity());
  DCHECK_EQ(total_allocated, accounting_stats_.Size());
}
#endif

void PagedSpaceBase::AddRangeToActiveSystemPages(PageMetadata* page,
                                                 Address start, Address end) {
  MemoryChunk* chunk = page->Chunk();
  DCHECK_LE(chunk->address(), start);
  DCHECK_LT(start, end);
  DCHECK_LE(end, chunk->address() + PageMetadata::kPageSize);

  const size_t added_pages = page->active_system_pages()->Add(
      chunk->Offset(start), chunk->Offset(end),
      MemoryAllocator::GetCommitPageSizeBits());

  IncrementCommittedPhysicalMemory(added_pages *
                                   MemoryAllocator::GetCommitPageSize());
}

void PagedSpaceBase::ReduceActiveSystemPages(
    PageMetadata* page, ActiveSystemPages active_system_pages) {
  const size_t reduced_pages =
      page->active_system_pages()->Reduce(active_system_pages);
  DecrementCommittedPhysicalMemory(reduced_pages *
                                   MemoryAllocator::GetCommitPageSize());
}

void PagedSpaceBase::UnlinkFreeListCategories(PageMetadata* page) {
  DCHECK_EQ(this, page->owner());
  page->ForAllFreeListCategories([this](FreeListCategory* category) {
    free_list()->RemoveCategory(category);
  });
}

size_t PagedSpaceBase::RelinkFreeListCategories(PageMetadata* page) {
  DCHECK_EQ(this, page->owner());
  size_t added = 0;
  page->ForAllFreeListCategories([this, &added](FreeListCategory* category) {
    added += category->available();
    category->Relink(free_list());
  });
  free_list()->increase_wasted_bytes(page->wasted_memory());

  DCHECK_IMPLIES(!page->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE),
                 page->AvailableInFreeList() ==
                     page->AvailableInFreeListFromAllocatedBytes());
  return added;
}

namespace {

void DropFreeListCategories(PageMetadata* page, FreeList* free_list) {
  size_t previously_available = 0;
  page->ForAllFreeListCategories(
      [free_list, &previously_available](FreeListCategory* category) {
        previously_available += category->available();
        category->Reset(free_list);
      });
  page->add_wasted_memory(previously_available);
}

}  // namespace

void PagedSpaceBase::RefillFreeList() {
  // Any PagedSpace might invoke RefillFreeList.
  DCHECK(identity() == OLD_SPACE || identity() == CODE_SPACE ||
         identity() == SHARED_SPACE || identity() == NEW_SPACE ||
         identity() == TRUSTED_SPACE);
  DCHECK_IMPLIES(identity() == NEW_SPACE, heap_->IsMainThread());
  DCHECK(!is_compaction_space());

  for (PageMetadata* p : heap()->sweeper()->GetAllSweptPagesSafe(this)) {
    // We regularly sweep NEVER_ALLOCATE_ON_PAGE pages. We drop the freelist
    // entries here to make them unavailable for allocations.
    if (p->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE)) {
      DropFreeListCategories(p, free_list());
    }

    ConcurrentAllocationMutex guard(this);
    DCHECK_EQ(this, p->owner());
    RefineAllocatedBytesAfterSweeping(p);
    RelinkFreeListCategories(p);
  }
}

AllocatorPolicy* PagedSpace::CreateAllocatorPolicy(MainAllocator* allocator) {
  return new PagedSpaceAllocatorPolicy(this, allocator);
}

// -----------------------------------------------------------------------------
// CompactionSpace implementation

void CompactionSpace::NotifyNewPage(PageMetadata* page) {
  new_pages_.push_back(page);
}

void CompactionSpace::RefillFreeList() {
  DCHECK_NE(NEW_SPACE, identity());

  Sweeper* sweeper = heap()->sweeper();
  size_t added = 0;
  PageMetadata* p = nullptr;
  while ((added <= kCompactionMemoryWanted) &&
         (p = sweeper->GetSweptPageSafe(this))) {
    // We regularly sweep NEVER_ALLOCATE_ON_PAGE pages. We drop the freelist
    // entries here to make them unavailable for allocations.
    if (p->Chunk()->IsFlagSet(MemoryChunk::NEVER_ALLOCATE_ON_PAGE)) {
      DropFreeListCategories(p, free_list());
    }

    // Only during compaction pages can actually change ownership. This is
    // safe because there exists no other competing action on the page links
    // during compaction.
    DCHECK_NE(this, p->owner());
    PagedSpace* owner = static_cast<PagedSpace*>(p->owner());
    base::MutexGuard guard(owner->mutex());
    owner->RefineAllocatedBytesAfterSweeping(p);
    owner->RemovePage(p);
    added += AddPage(p);
    added += p->wasted_memory();
  }
}

CompactionSpaceCollection::CompactionSpaceCollection(
    Heap* heap, CompactionSpaceKind compaction_space_kind)
    : old_space_(heap, OLD_SPACE, Executability::NOT_EXECUTABLE,
                 compaction_space_kind),
      code_space_(heap, CODE_SPACE, Executability::EXECUTABLE,
                  compaction_space_kind),
      trusted_space_(heap, TRUSTED_SPACE, Executability::NOT_EXECUTABLE,
                     compaction_space_kind) {
  if (heap->isolate()->has_shared_space()) {
    shared_space_.emplace(heap->isolate()->shared_space_isolate()->heap(),
                          SHARED_SPACE, Executability::NOT_EXECUTABLE,
                          compaction_space_kind);
  }
}

// -----------------------------------------------------------------------------
// OldSpace implementation

void OldSpace::AddPromotedPage(PageMetadata* page) {
  if (v8_flags.minor_ms) {
    // Reset the page's allocated bytes. The page will be swept and the
    // allocated bytes will be updated to match the live bytes.
    DCHECK_EQ(page->area_size(), page->allocated_bytes());
    page->DecreaseAllocatedBytes(page->area_size());
  }
  AddPageImpl(page);
  if (!v8_flags.minor_ms) {
    RelinkFreeListCategories(page);
  }
}

void OldSpace::ReleasePage(PageMetadata* page) {
  ReleasePageImpl(page, MemoryAllocator::FreeMode::kPool);
}

// -----------------------------------------------------------------------------
// StickySpace implementation

void StickySpace::AdjustDifferenceInAllocatedBytes(size_t diff) {
  DCHECK_GE(allocated_old_size_, diff);
  allocated_old_size_ -= diff;
}

// -----------------------------------------------------------------------------
// SharedSpace implementation

void SharedSpace::ReleasePage(PageMetadata* page) {
  // Old-to-new slots in old objects may be overwritten with references to
  // shared objects. Postpone releasing empty pages so that updating old-to-new
  // slots in dead old objects may access the dead shared objects.
  ReleasePageImpl(page, MemoryAllocator::FreeMode::kPostpone);
}

}  // namespace internal
}  // namespace v8
