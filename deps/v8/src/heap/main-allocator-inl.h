// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MAIN_ALLOCATOR_INL_H_
#define V8_HEAP_MAIN_ALLOCATOR_INL_H_

#include "src/base/sanitizer/msan.h"
#include "src/flags/flags.h"
#include "src/heap/heap-inl.h"
#include "src/heap/main-allocator.h"

namespace v8 {
namespace internal {

AllocationResult MainAllocator::AllocateRaw(int size_in_bytes,
                                            AllocationAlignment alignment,
                                            AllocationOrigin origin) {
  DCHECK(!v8_flags.enable_third_party_heap);
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);

  DCHECK_EQ(in_gc(), origin == AllocationOrigin::kGC);
  DCHECK_EQ(in_gc(), isolate_heap()->IsInGC());

  // We are not supposed to allocate in fast c calls.
  DCHECK_IMPLIES(is_main_thread(),
                 v8_flags.allow_allocation_in_fast_api_call ||
                     !isolate_heap()->isolate()->InFastCCall());

  AllocationResult result;

  if (USE_ALLOCATION_ALIGNMENT_BOOL && alignment != kTaggedAligned) {
    result = AllocateFastAligned(size_in_bytes, nullptr, alignment, origin);
  } else {
    result = AllocateFastUnaligned(size_in_bytes, origin);
  }

  return result.IsFailure() ? AllocateRawSlow(size_in_bytes, alignment, origin)
                            : result;
}

AllocationResult MainAllocator::AllocateFastUnaligned(int size_in_bytes,
                                                      AllocationOrigin origin) {
  size_in_bytes = ALIGN_TO_ALLOCATION_ALIGNMENT(size_in_bytes);
  if (!allocation_info().CanIncrementTop(size_in_bytes)) {
    return AllocationResult::Failure();
  }
  Tagged<HeapObject> obj =
      HeapObject::FromAddress(allocation_info().IncrementTop(size_in_bytes));

  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(obj.address(), size_in_bytes);

  DCHECK_IMPLIES(black_allocation_ == BlackAllocation::kAlwaysEnabled,
                 space_heap()->marking_state()->IsMarked(obj));

  return AllocationResult::FromObject(obj);
}

AllocationResult MainAllocator::AllocateFastAligned(
    int size_in_bytes, int* result_aligned_size_in_bytes,
    AllocationAlignment alignment, AllocationOrigin origin) {
  Address top = allocation_info().top();
  int filler_size = Heap::GetFillToAlign(top, alignment);
  int aligned_size_in_bytes = size_in_bytes + filler_size;

  if (!allocation_info().CanIncrementTop(aligned_size_in_bytes)) {
    return AllocationResult::Failure();
  }
  Tagged<HeapObject> obj = HeapObject::FromAddress(
      allocation_info().IncrementTop(aligned_size_in_bytes));
  if (result_aligned_size_in_bytes)
    *result_aligned_size_in_bytes = aligned_size_in_bytes;

  if (filler_size > 0) {
    obj = space_heap()->PrecedeWithFiller(obj, filler_size);
  }

  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(obj.address(), size_in_bytes);

  DCHECK_IMPLIES(black_allocation_ == BlackAllocation::kAlwaysEnabled,
                 space_heap()->marking_state()->IsMarked(obj));

  return AllocationResult::FromObject(obj);
}

bool MainAllocator::TryFreeLast(Address object_address, int object_size) {
  if (top() != kNullAddress) {
    return allocation_info().DecrementTopIfAdjacent(object_address,
                                                    object_size);
  }
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MAIN_ALLOCATOR_INL_H_
