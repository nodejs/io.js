// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-allocator.h"

#include "src/base/logging.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-allocator-inl.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"

namespace v8 {
namespace internal {

class Heap;

HeapAllocator::HeapAllocator(LocalHeap* local_heap)
    : local_heap_(local_heap), heap_(local_heap->heap()) {}

void HeapAllocator::Setup(LinearAllocationArea* new_allocation_info,
                          LinearAllocationArea* old_allocation_info) {
  for (int i = FIRST_SPACE; i <= LAST_SPACE; ++i) {
    spaces_[i] = heap_->space(i);
  }

  if (heap_->new_space() && local_heap_->is_main_thread()) {
    new_space_allocator_.emplace(local_heap_, heap_->new_space(),
                                 new_allocation_info);
  }

  old_space_allocator_.emplace(local_heap_, heap_->old_space(),
                               old_allocation_info);

  trusted_space_allocator_.emplace(local_heap_, heap_->trusted_space());
  code_space_allocator_.emplace(local_heap_, heap_->code_space());

  if (heap_->isolate()->has_shared_space()) {
    shared_space_allocator_.emplace(local_heap_,
                                    heap_->shared_allocation_space());
    shared_lo_space_ = heap_->shared_lo_allocation_space();
  }
}

void HeapAllocator::SetReadOnlySpace(ReadOnlySpace* read_only_space) {
  read_only_space_ = read_only_space;
}

AllocationResult HeapAllocator::AllocateRawLargeInternal(
    int size_in_bytes, AllocationType allocation, AllocationOrigin origin,
    AllocationAlignment alignment) {
  DCHECK_GT(size_in_bytes, heap_->MaxRegularHeapObjectSize(allocation));
  switch (allocation) {
    case AllocationType::kYoung:
      return new_lo_space()->AllocateRaw(local_heap_, size_in_bytes);
    case AllocationType::kOld:
      return lo_space()->AllocateRaw(local_heap_, size_in_bytes);
    case AllocationType::kCode:
      return code_lo_space()->AllocateRaw(local_heap_, size_in_bytes);
    case AllocationType::kSharedOld:
      return shared_lo_space()->AllocateRaw(local_heap_, size_in_bytes);
    case AllocationType::kTrusted:
      return trusted_lo_space()->AllocateRaw(local_heap_, size_in_bytes);
    case AllocationType::kMap:
    case AllocationType::kReadOnly:
    case AllocationType::kSharedMap:
      UNREACHABLE();
  }
}

namespace {

constexpr AllocationSpace AllocationTypeToGCSpace(AllocationType type) {
  switch (type) {
    case AllocationType::kYoung:
      return NEW_SPACE;
    case AllocationType::kOld:
    case AllocationType::kCode:
    case AllocationType::kMap:
    case AllocationType::kTrusted:
      // OLD_SPACE indicates full GC.
      return OLD_SPACE;
    case AllocationType::kReadOnly:
    case AllocationType::kSharedMap:
    case AllocationType::kSharedOld:
      UNREACHABLE();
  }
}

}  // namespace

AllocationResult HeapAllocator::AllocateRawWithLightRetrySlowPath(
    int size, AllocationType allocation, AllocationOrigin origin,
    AllocationAlignment alignment) {
  AllocationResult result = AllocateRaw(size, allocation, origin, alignment);
  if (!result.IsFailure()) {
    return result;
  }

  // Two GCs before returning failure.
  for (int i = 0; i < 2; i++) {
    if (IsSharedAllocationType(allocation)) {
      heap_->CollectGarbageShared(heap_->main_thread_local_heap(),
                                  GarbageCollectionReason::kAllocationFailure);
    } else {
      AllocationSpace space_to_gc = AllocationTypeToGCSpace(allocation);
      heap_->CollectGarbage(space_to_gc,
                            GarbageCollectionReason::kAllocationFailure);
    }
    result = AllocateRaw(size, allocation, origin, alignment);
    if (!result.IsFailure()) {
      return result;
    }
  }
  return result;
}

AllocationResult HeapAllocator::AllocateRawWithRetryOrFailSlowPath(
    int size, AllocationType allocation, AllocationOrigin origin,
    AllocationAlignment alignment) {
  AllocationResult result =
      AllocateRawWithLightRetrySlowPath(size, allocation, origin, alignment);
  if (!result.IsFailure()) return result;

  if (IsSharedAllocationType(allocation)) {
    heap_->CollectGarbageShared(heap_->main_thread_local_heap(),
                                GarbageCollectionReason::kLastResort);

    // We need always_allocate() to be true both on the client- and
    // server-isolate. It is used in both code paths.
    AlwaysAllocateScope shared_scope(
        heap_->isolate()->shared_space_isolate()->heap());
    AlwaysAllocateScope client_scope(heap_);
    result = AllocateRaw(size, allocation, origin, alignment);
  } else {
    heap_->CollectAllAvailableGarbage(GarbageCollectionReason::kLastResort);

    AlwaysAllocateScope scope(heap_);
    result = AllocateRaw(size, allocation, origin, alignment);
  }

  if (!result.IsFailure()) {
    return result;
  }

  V8::FatalProcessOutOfMemory(heap_->isolate(), "CALL_AND_RETRY_LAST",
                              V8::kHeapOOM);
}

void HeapAllocator::MakeLinearAllocationAreasIterable() {
  if (new_space_allocator_) {
    new_space_allocator_->MakeLinearAllocationAreaIterable();
  }
  old_space_allocator_->MakeLinearAllocationAreaIterable();
  trusted_space_allocator_->MakeLinearAllocationAreaIterable();
  code_space_allocator_->MakeLinearAllocationAreaIterable();

  if (shared_space_allocator_) {
    shared_space_allocator_->MakeLinearAllocationAreaIterable();
  }
}

#if DEBUG
void HeapAllocator::VerifyLinearAllocationAreas() const {
  if (new_space_allocator_) {
    new_space_allocator_->Verify();
  }
  old_space_allocator_->Verify();
  trusted_space_allocator_->Verify();
  code_space_allocator_->Verify();

  if (shared_space_allocator_) {
    shared_space_allocator_->Verify();
  }
}
#endif  // DEBUG

void HeapAllocator::MarkLinearAllocationAreasBlack() {
  old_space_allocator_->MarkLinearAllocationAreaBlack();
  trusted_space_allocator_->MarkLinearAllocationAreaBlack();

  {
    CodePageHeaderModificationScope rwx_write_scope(
        "Marking Code objects requires write access to the Code page header");
    code_space_allocator_->MarkLinearAllocationAreaBlack();
  }
}

void HeapAllocator::UnmarkLinearAllocationsArea() {
  old_space_allocator_->UnmarkLinearAllocationArea();
  trusted_space_allocator_->UnmarkLinearAllocationArea();

  {
    CodePageHeaderModificationScope rwx_write_scope(
        "Marking Code objects requires write access to the Code page header");
    code_space_allocator_->UnmarkLinearAllocationArea();
  }
}

void HeapAllocator::MarkSharedLinearAllocationAreasBlack() {
  if (shared_space_allocator_) {
    shared_space_allocator_->MarkLinearAllocationAreaBlack();
  }
}

void HeapAllocator::UnmarkSharedLinearAllocationAreas() {
  if (shared_space_allocator_) {
    shared_space_allocator_->UnmarkLinearAllocationArea();
  }
}

void HeapAllocator::FreeLinearAllocationAreas() {
  if (new_space_allocator_) {
    new_space_allocator_->FreeLinearAllocationArea();
  }
  old_space_allocator_->FreeLinearAllocationArea();
  trusted_space_allocator_->FreeLinearAllocationArea();

  {
    CodePageHeaderModificationScope rwx_write_scope(
        "Setting the high water mark requires write access to the Code page "
        "header");
    code_space_allocator_->FreeLinearAllocationArea();
  }

  if (shared_space_allocator_) {
    shared_space_allocator_->FreeLinearAllocationArea();
  }
}

void HeapAllocator::PublishPendingAllocations() {
  if (new_space_allocator_) {
    new_space_allocator_->MoveOriginalTopForward();
  }

  old_space_allocator_->MoveOriginalTopForward();
  trusted_space_allocator_->MoveOriginalTopForward();
  code_space_allocator_->MoveOriginalTopForward();

  lo_space()->ResetPendingObject();
  if (new_lo_space()) new_lo_space()->ResetPendingObject();
  code_lo_space()->ResetPendingObject();
  trusted_lo_space()->ResetPendingObject();
}

void HeapAllocator::AddAllocationObserver(
    AllocationObserver* observer, AllocationObserver* new_space_observer) {
  if (new_space_allocator_) {
    new_space_allocator_->AddAllocationObserver(new_space_observer);
  }
  if (new_lo_space()) {
    new_lo_space()->AddAllocationObserver(new_space_observer);
  }
  old_space_allocator_->AddAllocationObserver(observer);
  lo_space()->AddAllocationObserver(observer);
  trusted_space_allocator_->AddAllocationObserver(observer);
  trusted_lo_space()->AddAllocationObserver(observer);
  code_space_allocator_->AddAllocationObserver(observer);
  code_lo_space()->AddAllocationObserver(observer);
}

void HeapAllocator::RemoveAllocationObserver(
    AllocationObserver* observer, AllocationObserver* new_space_observer) {
  if (new_space_allocator_) {
    new_space_allocator_->RemoveAllocationObserver(new_space_observer);
  }
  if (new_lo_space()) {
    new_lo_space()->RemoveAllocationObserver(new_space_observer);
  }
  old_space_allocator_->RemoveAllocationObserver(observer);
  lo_space()->RemoveAllocationObserver(observer);
  trusted_space_allocator_->RemoveAllocationObserver(observer);
  trusted_lo_space()->RemoveAllocationObserver(observer);
  code_space_allocator_->RemoveAllocationObserver(observer);
  code_lo_space()->RemoveAllocationObserver(observer);
}

void HeapAllocator::PauseAllocationObservers() {
  if (new_space_allocator_) {
    new_space_allocator_->PauseAllocationObservers();
  }
  old_space_allocator_->PauseAllocationObservers();
  trusted_space_allocator_->PauseAllocationObservers();
  code_space_allocator_->PauseAllocationObservers();
}

void HeapAllocator::ResumeAllocationObservers() {
  if (new_space_allocator_) {
    new_space_allocator_->ResumeAllocationObservers();
  }
  old_space_allocator_->ResumeAllocationObservers();
  trusted_space_allocator_->ResumeAllocationObservers();
  code_space_allocator_->ResumeAllocationObservers();
}

#ifdef DEBUG

void HeapAllocator::IncrementObjectCounters() {
  heap_->isolate()->counters()->objs_since_last_full()->Increment();
  heap_->isolate()->counters()->objs_since_last_young()->Increment();
}

#endif  // DEBUG

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
// static
void HeapAllocator::InitializeOncePerProcess() {
  SetAllocationGcInterval(v8_flags.gc_interval);
}

// static
void HeapAllocator::SetAllocationGcInterval(int allocation_gc_interval) {
  allocation_gc_interval_.store(allocation_gc_interval,
                                std::memory_order_relaxed);
}

// static
std::atomic<int> HeapAllocator::allocation_gc_interval_{-1};

void HeapAllocator::SetAllocationTimeout(int allocation_timeout) {
  // See `allocation_timeout_` for description. We map negative values to 0 to
  // avoid underflows as allocation decrements this value as well.
  allocation_timeout_ = std::max(0, allocation_timeout);
}

void HeapAllocator::UpdateAllocationTimeout() {
  if (v8_flags.random_gc_interval > 0) {
    const int new_timeout = allocation_timeout_ <= 0
                                ? heap_->isolate()->fuzzer_rng()->NextInt(
                                      v8_flags.random_gc_interval + 1)
                                : allocation_timeout_;
    // Reset the allocation timeout, but make sure to allow at least a few
    // allocations after a collection. The reason for this is that we have a lot
    // of allocation sequences and we assume that a garbage collection will
    // allow the subsequent allocation attempts to go through.
    constexpr int kFewAllocationsHeadroom = 6;
    allocation_timeout_ = std::max(kFewAllocationsHeadroom, new_timeout);
    return;
  }

  int interval = allocation_gc_interval_.load(std::memory_order_relaxed);
  allocation_timeout_ = std::max(0, interval);
}

#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

}  // namespace internal
}  // namespace v8
