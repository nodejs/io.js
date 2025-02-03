// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/heap/heap-utils.h"

#include <algorithm>

#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/gc-tracer-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/mark-compact.h"
#include "src/heap/new-spaces.h"
#include "src/heap/page-metadata-inl.h"
#include "src/heap/safepoint.h"
#include "src/objects/free-space-inl.h"

namespace v8 {
namespace internal {

void HeapInternalsBase::SimulateIncrementalMarking(Heap* heap,
                                                   bool force_completion) {
  static constexpr auto kStepSize = v8::base::TimeDelta::FromMilliseconds(100);
  CHECK(v8_flags.incremental_marking);
  i::IncrementalMarking* marking = heap->incremental_marking();

  if (heap->sweeping_in_progress()) {
    IsolateSafepointScope scope(heap);
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }

  if (marking->IsStopped()) {
    heap->StartIncrementalMarking(i::GCFlag::kNoFlags,
                                  i::GarbageCollectionReason::kTesting);
  }
  CHECK(marking->IsMajorMarking());
  if (!force_completion) return;

  while (!marking->IsMajorMarkingComplete()) {
    marking->AdvanceForTesting(kStepSize);
  }
}

namespace {

int FixedArrayLenFromSize(int size) {
  return std::min({(size - FixedArray::kHeaderSize) / kTaggedSize,
                   FixedArray::kMaxRegularLength});
}

void FillPageInPagedSpace(PageMetadata* page,
                          std::vector<Handle<FixedArray>>* out_handles) {
  Heap* heap = page->heap();
  ManualGCScope manual_gc_scope(heap->isolate());
  DCHECK(page->SweepingDone());
  PagedSpaceBase* paged_space = static_cast<PagedSpaceBase*>(page->owner());
  heap->FreeLinearAllocationAreas();

  PauseAllocationObserversScope no_observers_scope(heap);

  CollectionEpoch full_epoch =
      heap->tracer()->CurrentEpoch(GCTracer::Scope::ScopeId::MARK_COMPACTOR);
  CollectionEpoch young_epoch = heap->tracer()->CurrentEpoch(
      GCTracer::Scope::ScopeId::MINOR_MARK_SWEEPER);

  for (PageMetadata* p : *paged_space) {
    if (p != page) paged_space->UnlinkFreeListCategories(p);
  }

  // If min_block_size is larger than FixedArray::kHeaderSize, all blocks in the
  // free list can be used to allocate a fixed array. This guarantees that we
  // can fill the whole page.
  DCHECK_LT(FixedArray::kHeaderSize,
            paged_space->free_list()->min_block_size());

  std::vector<int> available_sizes;
  // Collect all free list block sizes
  page->ForAllFreeListCategories(
      [&available_sizes](FreeListCategory* category) {
        category->IterateNodesForTesting(
            [&available_sizes](Tagged<FreeSpace> node) {
              int node_size = node->Size();
              if (node_size >= kMaxRegularHeapObjectSize) {
                available_sizes.push_back(node_size);
              }
            });
      });

  Isolate* isolate = heap->isolate();

  // Allocate as many max size arrays as possible, while making sure not to
  // leave behind a block too small to fit a FixedArray.
  const int max_array_length = FixedArrayLenFromSize(kMaxRegularHeapObjectSize);
  for (size_t i = 0; i < available_sizes.size(); ++i) {
    int available_size = available_sizes[i];
    while (available_size > kMaxRegularHeapObjectSize) {
      Handle<FixedArray> fixed_array = isolate->factory()->NewFixedArray(
          max_array_length, AllocationType::kYoung);
      if (out_handles) out_handles->push_back(fixed_array);
      available_size -= kMaxRegularHeapObjectSize;
    }
  }

  heap->FreeLinearAllocationAreas();

  // Allocate FixedArrays in remaining free list blocks, from largest
  // category to smallest.
  std::vector<std::vector<int>> remaining_sizes;
  page->ForAllFreeListCategories(
      [&remaining_sizes](FreeListCategory* category) {
        remaining_sizes.push_back({});
        std::vector<int>& sizes_in_category =
            remaining_sizes[remaining_sizes.size() - 1];
        category->IterateNodesForTesting(
            [&sizes_in_category](Tagged<FreeSpace> node) {
              int node_size = node->Size();
              DCHECK_LT(0, FixedArrayLenFromSize(node_size));
              sizes_in_category.push_back(node_size);
            });
      });
  for (auto it = remaining_sizes.rbegin(); it != remaining_sizes.rend(); ++it) {
    std::vector<int> sizes_in_category = *it;
    for (int size : sizes_in_category) {
      DCHECK_LE(size, kMaxRegularHeapObjectSize);
      int array_length = FixedArrayLenFromSize(size);
      DCHECK_LT(0, array_length);
      Handle<FixedArray> fixed_array = isolate->factory()->NewFixedArray(
          array_length, AllocationType::kYoung);
      if (out_handles) out_handles->push_back(fixed_array);
    }
  }

  DCHECK_EQ(0, page->AvailableInFreeList());
  DCHECK_EQ(0, page->AvailableInFreeListFromAllocatedBytes());

  for (PageMetadata* p : *paged_space) {
    if (p != page) paged_space->RelinkFreeListCategories(p);
  }

  // Allocations in this method should not require a GC.
  CHECK_EQ(full_epoch, heap->tracer()->CurrentEpoch(
                           GCTracer::Scope::ScopeId::MARK_COMPACTOR));
  CHECK_EQ(young_epoch, heap->tracer()->CurrentEpoch(
                            GCTracer::Scope::ScopeId::MINOR_MARK_SWEEPER));
  heap->FreeLinearAllocationAreas();
}

}  // namespace

void HeapInternalsBase::SimulateFullSpace(
    v8::internal::NewSpace* space,
    std::vector<Handle<FixedArray>>* out_handles) {
  Heap* heap = space->heap();
  IsolateSafepointScope safepoint_scope(heap);
  heap->FreeLinearAllocationAreas();
  // If you see this check failing, disable the flag at the start of your test:
  // v8_flags.stress_concurrent_allocation = false;
  // Background thread allocating concurrently interferes with this function.
  CHECK(!v8_flags.stress_concurrent_allocation);
  space->heap()->EnsureSweepingCompleted(
      Heap::SweepingForcedFinalizationMode::kV8Only);
  if (v8_flags.minor_ms) {
    auto* space = heap->paged_new_space()->paged_space();
    space->AllocatePageUpToCapacityForTesting();
    for (PageMetadata* page : *space) {
      FillPageInPagedSpace(page, out_handles);
    }
    DCHECK_IMPLIES(space->free_list(), space->free_list()->Available() == 0);
  } else {
    SemiSpaceNewSpace* space = SemiSpaceNewSpace::From(heap->new_space());
    do {
      FillCurrentPage(space, out_handles);
    } while (space->AddFreshPage());
  }
}

void HeapInternalsBase::SimulateFullSpace(v8::internal::PagedSpace* space) {
  Heap* heap = space->heap();
  IsolateSafepointScope safepoint_scope(heap);
  heap->FreeLinearAllocationAreas();
  // If you see this check failing, disable the flag at the start of your test:
  // v8_flags.stress_concurrent_allocation = false;
  // Background thread allocating concurrently interferes with this function.
  CHECK(!v8_flags.stress_concurrent_allocation);
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
  }
  space->ResetFreeList();
}

namespace {
std::vector<Handle<FixedArray>> CreatePadding(Heap* heap, int padding_size,
                                              AllocationType allocation) {
  std::vector<Handle<FixedArray>> handles;
  Isolate* isolate = heap->isolate();
  int allocate_memory;
  int length;
  int free_memory = padding_size;
  heap->FreeMainThreadLinearAllocationAreas();
  if (allocation == i::AllocationType::kOld) {
    int overall_free_memory = static_cast<int>(heap->old_space()->Available());
    CHECK(padding_size <= overall_free_memory || overall_free_memory == 0);
  } else {
    int overall_free_memory = static_cast<int>(heap->new_space()->Available());
    CHECK(padding_size <= overall_free_memory || overall_free_memory == 0);
  }
  while (free_memory > 0) {
    if (free_memory > kMaxRegularHeapObjectSize) {
      allocate_memory = kMaxRegularHeapObjectSize;
      length = FixedArrayLenFromSize(allocate_memory);
    } else {
      allocate_memory = free_memory;
      length = FixedArrayLenFromSize(allocate_memory);
      if (length <= 0) {
        // Not enough room to create another FixedArray, so create a filler.
        if (allocation == i::AllocationType::kOld) {
          heap->CreateFillerObjectAt(*heap->OldSpaceAllocationTopAddress(),
                                     free_memory);
        } else {
          heap->CreateFillerObjectAt(*heap->NewSpaceAllocationTopAddress(),
                                     free_memory);
        }
        break;
      }
    }
    handles.push_back(isolate->factory()->NewFixedArray(length, allocation));
    CHECK((allocation == AllocationType::kYoung &&
           heap->new_space()->Contains(*handles.back())) ||
          (allocation == AllocationType::kOld &&
           heap->InOldSpace(*handles.back())) ||
          v8_flags.single_generation);
    free_memory -= handles.back()->Size();
  }
  return handles;
}

void FillCurrentSemiSpacePage(v8::internal::SemiSpaceNewSpace* space,
                              std::vector<Handle<FixedArray>>* out_handles) {
  // We cannot rely on `space->limit()` to point to the end of the current page
  // in the case where inline allocations are disabled, it actually points to
  // the current allocation pointer.
  DCHECK_IMPLIES(
      !space->heap()->IsInlineAllocationEnabled(),
      space->heap()->NewSpaceTop() == space->heap()->NewSpaceLimit());

  int space_remaining = space->GetSpaceRemainingOnCurrentPageForTesting();
  if (space_remaining == 0) return;
  std::vector<Handle<FixedArray>> handles =
      CreatePadding(space->heap(), space_remaining, i::AllocationType::kYoung);
  if (out_handles != nullptr) {
    out_handles->insert(out_handles->end(), handles.begin(), handles.end());
  }
}

void FillCurrentPagedSpacePage(v8::internal::NewSpace* space,
                               std::vector<Handle<FixedArray>>* out_handles) {
  const Address top = space->heap()->NewSpaceTop();
  if (top == kNullAddress) return;
  PageMetadata* page = PageMetadata::FromAllocationAreaAddress(top);
  space->heap()->EnsureSweepingCompleted(
      Heap::SweepingForcedFinalizationMode::kV8Only);
  FillPageInPagedSpace(page, out_handles);
}

}  // namespace

void HeapInternalsBase::FillCurrentPage(
    v8::internal::NewSpace* space,
    std::vector<Handle<FixedArray>>* out_handles) {
  PauseAllocationObserversScope pause_observers(space->heap());
  MainAllocator* allocator = space->heap()->allocator()->new_space_allocator();
  allocator->FreeLinearAllocationArea();
  if (v8_flags.minor_ms) {
    FillCurrentPagedSpacePage(space, out_handles);
  } else {
    FillCurrentSemiSpacePage(SemiSpaceNewSpace::From(space), out_handles);
  }
  allocator->FreeLinearAllocationArea();
}

bool IsNewObjectInCorrectGeneration(Tagged<HeapObject> object) {
  return v8_flags.single_generation ? !i::Heap::InYoungGeneration(object)
                                    : i::Heap::InYoungGeneration(object);
}

ManualGCScope::ManualGCScope(Isolate* isolate)
    : isolate_(isolate),
      flag_concurrent_marking_(v8_flags.concurrent_marking),
      flag_concurrent_sweeping_(v8_flags.concurrent_sweeping),
      flag_concurrent_minor_ms_marking_(v8_flags.concurrent_minor_ms_marking),
      flag_stress_concurrent_allocation_(v8_flags.stress_concurrent_allocation),
      flag_stress_incremental_marking_(v8_flags.stress_incremental_marking),
      flag_parallel_marking_(v8_flags.parallel_marking),
      flag_detect_ineffective_gcs_near_heap_limit_(
          v8_flags.detect_ineffective_gcs_near_heap_limit),
      flag_cppheap_concurrent_marking_(v8_flags.cppheap_concurrent_marking) {
  // Some tests run threaded (back-to-back) and thus the GC may already be
  // running by the time a ManualGCScope is created. Finalizing existing marking
  // prevents any undefined/unexpected behavior.
  if (isolate) {
    auto* heap = isolate->heap();
    if (heap->incremental_marking()->IsMarking()) {
      InvokeAtomicMajorGC(isolate);
    }
  }

  v8_flags.concurrent_marking = false;
  v8_flags.concurrent_sweeping = false;
  v8_flags.concurrent_minor_ms_marking = false;
  v8_flags.stress_incremental_marking = false;
  v8_flags.stress_concurrent_allocation = false;
  // Parallel marking has a dependency on concurrent marking.
  v8_flags.parallel_marking = false;
  v8_flags.detect_ineffective_gcs_near_heap_limit = false;
  // CppHeap concurrent marking has a dependency on concurrent marking.
  v8_flags.cppheap_concurrent_marking = false;

  if (isolate_ && isolate_->heap()->cpp_heap()) {
    CppHeap::From(isolate_->heap()->cpp_heap())
        ->UpdateGCCapabilitiesFromFlagsForTesting();
  }
}

ManualGCScope::~ManualGCScope() {
  v8_flags.concurrent_marking = flag_concurrent_marking_;
  v8_flags.concurrent_sweeping = flag_concurrent_sweeping_;
  v8_flags.concurrent_minor_ms_marking = flag_concurrent_minor_ms_marking_;
  v8_flags.stress_concurrent_allocation = flag_stress_concurrent_allocation_;
  v8_flags.stress_incremental_marking = flag_stress_incremental_marking_;
  v8_flags.parallel_marking = flag_parallel_marking_;
  v8_flags.detect_ineffective_gcs_near_heap_limit =
      flag_detect_ineffective_gcs_near_heap_limit_;
  v8_flags.cppheap_concurrent_marking = flag_cppheap_concurrent_marking_;

  if (isolate_ && isolate_->heap()->cpp_heap()) {
    CppHeap::From(isolate_->heap()->cpp_heap())
        ->UpdateGCCapabilitiesFromFlagsForTesting();
  }
}

}  // namespace internal
}  // namespace v8
