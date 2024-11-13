// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_ALLOCATOR_INL_H_
#define V8_HEAP_HEAP_ALLOCATOR_INL_H_

#include "src/base/logging.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/heap/heap-allocator.h"
#include "src/heap/large-spaces.h"
#include "src/heap/local-heap.h"
#include "src/heap/main-allocator-inl.h"
#include "src/heap/new-spaces.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/read-only-spaces.h"
#include "src/heap/third-party/heap-api.h"
#include "src/heap/zapping.h"

namespace v8 {
namespace internal {

PagedSpace* HeapAllocator::code_space() const {
  return static_cast<PagedSpace*>(spaces_[CODE_SPACE]);
}

CodeLargeObjectSpace* HeapAllocator::code_lo_space() const {
  return static_cast<CodeLargeObjectSpace*>(spaces_[CODE_LO_SPACE]);
}

OldLargeObjectSpace* HeapAllocator::lo_space() const {
  return static_cast<OldLargeObjectSpace*>(spaces_[LO_SPACE]);
}

OldLargeObjectSpace* HeapAllocator::shared_lo_space() const {
  return shared_lo_space_;
}

NewSpace* HeapAllocator::new_space() const {
  return static_cast<NewSpace*>(spaces_[NEW_SPACE]);
}

NewLargeObjectSpace* HeapAllocator::new_lo_space() const {
  return static_cast<NewLargeObjectSpace*>(spaces_[NEW_LO_SPACE]);
}

PagedSpace* HeapAllocator::old_space() const {
  return static_cast<PagedSpace*>(spaces_[OLD_SPACE]);
}

ReadOnlySpace* HeapAllocator::read_only_space() const {
  return read_only_space_;
}

PagedSpace* HeapAllocator::trusted_space() const {
  return static_cast<PagedSpace*>(spaces_[TRUSTED_SPACE]);
}

OldLargeObjectSpace* HeapAllocator::trusted_lo_space() const {
  return static_cast<OldLargeObjectSpace*>(spaces_[TRUSTED_LO_SPACE]);
}

OldLargeObjectSpace* HeapAllocator::shared_trusted_lo_space() const {
  return shared_trusted_lo_space_;
}

bool HeapAllocator::CanAllocateInReadOnlySpace() const {
  return read_only_space()->writable();
}

template <AllocationType type>
V8_WARN_UNUSED_RESULT V8_INLINE AllocationResult HeapAllocator::AllocateRaw(
    int size_in_bytes, AllocationOrigin origin, AllocationAlignment alignment) {
  DCHECK(!heap_->IsInGC());
  DCHECK(AllowHandleAllocation::IsAllowed());
  DCHECK(AllowHeapAllocation::IsAllowed());
  CHECK(AllowHeapAllocationInRelease::IsAllowed());
  DCHECK(local_heap_->IsRunning());
#if DEBUG
  local_heap_->VerifyCurrent();
#endif

  if (v8_flags.single_generation.value() && type == AllocationType::kYoung) {
    return AllocateRaw(size_in_bytes, AllocationType::kOld, origin, alignment);
  }

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  if (V8_UNLIKELY(allocation_timeout_.has_value()) &&
      ReachedAllocationTimeout()) {
    return AllocationResult::Failure();
  }
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

#ifdef DEBUG
  IncrementObjectCounters();
#endif  // DEBUG

  if (heap_->CanSafepoint()) {
    local_heap_->Safepoint();
  }

  const size_t large_object_threshold = heap_->MaxRegularHeapObjectSize(type);
  const bool large_object =
      static_cast<size_t>(size_in_bytes) > large_object_threshold;

  Tagged<HeapObject> object;
  AllocationResult allocation;

  if (V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
    allocation = heap_->tp_heap_->Allocate(size_in_bytes, type, alignment);
  } else {
    if (V8_UNLIKELY(large_object)) {
      allocation =
          AllocateRawLargeInternal(size_in_bytes, type, origin, alignment);
    } else {
      switch (type) {
        case AllocationType::kYoung:
          allocation = new_space_allocator_->AllocateRaw(size_in_bytes,
                                                         alignment, origin);
          break;
        case AllocationType::kMap:
        case AllocationType::kOld:
          allocation = old_space_allocator_->AllocateRaw(size_in_bytes,
                                                         alignment, origin);
          DCHECK_IMPLIES(
              v8_flags.sticky_mark_bits && !allocation.IsFailure(),
              heap_->marking_state()->IsMarked(allocation.ToObject()));
          break;
        case AllocationType::kCode: {
          DCHECK_EQ(alignment, AllocationAlignment::kTaggedAligned);
          DCHECK(AllowCodeAllocation::IsAllowed());
          allocation = code_space_allocator_->AllocateRaw(
              size_in_bytes, AllocationAlignment::kTaggedAligned, origin);
          break;
        }
        case AllocationType::kReadOnly:
          DCHECK(read_only_space()->writable());
          DCHECK_EQ(AllocationOrigin::kRuntime, origin);
          allocation = read_only_space()->AllocateRaw(size_in_bytes, alignment);
          break;
        case AllocationType::kSharedMap:
        case AllocationType::kSharedOld:
          allocation = shared_space_allocator_->AllocateRaw(size_in_bytes,
                                                            alignment, origin);
          break;
        case AllocationType::kTrusted:
          allocation = trusted_space_allocator_->AllocateRaw(size_in_bytes,
                                                             alignment, origin);
          break;
        case AllocationType::kSharedTrusted:
          allocation = shared_trusted_space_allocator_->AllocateRaw(
              size_in_bytes, alignment, origin);
          break;
      }
    }
  }

  if (allocation.To(&object)) {
    if (heap::ShouldZapGarbage() && AllocationType::kCode == type &&
        !V8_ENABLE_THIRD_PARTY_HEAP_BOOL) {
      heap::ZapCodeBlock(object.address(), size_in_bytes);
    }

    if (local_heap_->is_main_thread()) {
      for (auto& tracker : heap_->allocation_trackers_) {
        tracker->AllocationEvent(object.address(), size_in_bytes);
      }
    }
  }

  return allocation;
}

AllocationResult HeapAllocator::AllocateRaw(int size_in_bytes,
                                            AllocationType type,
                                            AllocationOrigin origin,
                                            AllocationAlignment alignment) {
  switch (type) {
    case AllocationType::kYoung:
      return AllocateRaw<AllocationType::kYoung>(size_in_bytes, origin,
                                                 alignment);
    case AllocationType::kOld:
      return AllocateRaw<AllocationType::kOld>(size_in_bytes, origin,
                                               alignment);
    case AllocationType::kCode:
      return AllocateRaw<AllocationType::kCode>(size_in_bytes, origin,
                                                alignment);
    case AllocationType::kMap:
      return AllocateRaw<AllocationType::kMap>(size_in_bytes, origin,
                                               alignment);
    case AllocationType::kReadOnly:
      return AllocateRaw<AllocationType::kReadOnly>(size_in_bytes, origin,
                                                    alignment);
    case AllocationType::kSharedMap:
      return AllocateRaw<AllocationType::kSharedMap>(size_in_bytes, origin,
                                                     alignment);
    case AllocationType::kSharedOld:
      return AllocateRaw<AllocationType::kSharedOld>(size_in_bytes, origin,
                                                     alignment);
    case AllocationType::kTrusted:
      return AllocateRaw<AllocationType::kTrusted>(size_in_bytes, origin,
                                                   alignment);
    case AllocationType::kSharedTrusted:
      return AllocateRaw<AllocationType::kSharedTrusted>(size_in_bytes, origin,
                                                         alignment);
  }
  UNREACHABLE();
}

AllocationResult HeapAllocator::AllocateRawData(int size_in_bytes,
                                                AllocationType type,
                                                AllocationOrigin origin,
                                                AllocationAlignment alignment) {
  switch (type) {
    case AllocationType::kYoung:
      return AllocateRaw<AllocationType::kYoung>(size_in_bytes, origin,
                                                 alignment);
    case AllocationType::kOld:
      return AllocateRaw<AllocationType::kOld>(size_in_bytes, origin,
                                               alignment);
    case AllocationType::kCode:
    case AllocationType::kMap:
    case AllocationType::kReadOnly:
    case AllocationType::kSharedMap:
    case AllocationType::kSharedOld:
    case AllocationType::kTrusted:
    case AllocationType::kSharedTrusted:
      UNREACHABLE();
  }
  UNREACHABLE();
}

template <HeapAllocator::AllocationRetryMode mode>
V8_WARN_UNUSED_RESULT V8_INLINE Tagged<HeapObject>
HeapAllocator::AllocateRawWith(int size, AllocationType allocation,
                               AllocationOrigin origin,
                               AllocationAlignment alignment) {
  AllocationResult result;
  Tagged<HeapObject> object;
  size = ALIGN_TO_ALLOCATION_ALIGNMENT(size);
  if (allocation == AllocationType::kYoung) {
    result = AllocateRaw<AllocationType::kYoung>(size, origin, alignment);
    if (result.To(&object)) {
      return object;
    }
  } else if (allocation == AllocationType::kOld) {
    result = AllocateRaw<AllocationType::kOld>(size, origin, alignment);
    if (result.To(&object)) {
      return object;
    }
  }
  switch (mode) {
    case kLightRetry:
      result = AllocateRawWithLightRetrySlowPath(size, allocation, origin,
                                                 alignment);
      break;
    case kRetryOrFail:
      result = AllocateRawWithRetryOrFailSlowPath(size, allocation, origin,
                                                  alignment);
      break;
  }
  if (result.To(&object)) {
    return object;
  }
  return HeapObject();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_HEAP_ALLOCATOR_INL_H_
