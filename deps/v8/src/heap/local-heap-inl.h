// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LOCAL_HEAP_INL_H_
#define V8_HEAP_LOCAL_HEAP_INL_H_

#include <atomic>

#include "src/common/assert-scope.h"
#include "src/handles/persistent-handles.h"
#include "src/heap/heap.h"
#include "src/heap/large-spaces.h"
#include "src/heap/local-heap.h"
#include "src/heap/main-allocator-inl.h"
#include "src/heap/parked-scope.h"
#include "src/heap/zapping.h"

namespace v8 {
namespace internal {

#define ROOT_ACCESSOR(type, name, CamelName) \
  inline Tagged<type> LocalHeap::name() { return heap()->name(); }
MUTABLE_ROOT_LIST(ROOT_ACCESSOR)
#undef ROOT_ACCESSOR

AllocationResult LocalHeap::AllocateRaw(int size_in_bytes, AllocationType type,
                                        AllocationOrigin origin,
                                        AllocationAlignment alignment) {
  return heap_allocator_.AllocateRaw(size_in_bytes, type, origin, alignment);
}

template <typename HeapAllocator::AllocationRetryMode mode>
Tagged<HeapObject> LocalHeap::AllocateRawWith(int object_size,
                                              AllocationType type,
                                              AllocationOrigin origin,
                                              AllocationAlignment alignment) {
  object_size = ALIGN_TO_ALLOCATION_ALIGNMENT(object_size);
  DCHECK(!v8_flags.enable_third_party_heap);
  return heap_allocator_.AllocateRawWith<mode>(object_size, type, origin,
                                               alignment);
}

Address LocalHeap::AllocateRawOrFail(int object_size, AllocationType type,
                                     AllocationOrigin origin,
                                     AllocationAlignment alignment) {
  return AllocateRawWith<HeapAllocator::kRetryOrFail>(object_size, type, origin,
                                                      alignment)
      .address();
}

template <typename Callback>
V8_INLINE void LocalHeap::ParkAndExecuteCallback(Callback callback) {
  // This method is given as a callback to the stack trampoline, when the stack
  // marker has just been set.
#if defined(V8_ENABLE_DIRECT_HANDLE) && defined(DEBUG)
  // Reset the number of direct handles that are below the stack marker.
  // It will be restored before the method returns.
  DirectHandleBase::ResetNumberOfHandlesScope scope;
#endif  // V8_ENABLE_DIRECT_HANDLE && DEBUG
  ParkedScope parked(this);
  // Provide the parked scope as a witness, if the callback expects it.
  if constexpr (std::is_invocable_v<Callback, const ParkedScope&>) {
    callback(parked);
  } else {
    callback();
  }
}

template <typename Callback>
V8_INLINE void LocalHeap::ExecuteWithStackMarker(Callback callback) {
  if (is_main_thread()) {
    heap()->stack().SetMarkerAndCallback(callback);
  } else {
    heap()->stack().SetMarkerForBackgroundThreadAndCallback(
        ThreadId::Current().ToInteger(), callback);
  }
}

template <typename Callback>
V8_INLINE void LocalHeap::ExecuteWhileParked(Callback callback) {
  ExecuteWithStackMarker(
      [this, callback]() { ParkAndExecuteCallback(callback); });
}

template <typename Callback>
V8_INLINE void LocalHeap::ExecuteMainThreadWhileParked(Callback callback) {
  DCHECK(is_main_thread());
  heap()->stack().SetMarkerAndCallback(
      [this, callback]() { ParkAndExecuteCallback(callback); });
}

template <typename Callback>
V8_INLINE void LocalHeap::ExecuteBackgroundThreadWhileParked(
    Callback callback) {
  DCHECK(!is_main_thread());
  heap()->stack().SetMarkerForBackgroundThreadAndCallback(
      ThreadId::Current().ToInteger(),
      [this, callback]() { ParkAndExecuteCallback(callback); });
}

V8_INLINE bool LocalHeap::is_in_trampoline() const {
  if (is_main_thread()) {
    return heap_->stack().IsMarkerSet();
  } else {
    return heap_->stack().IsMarkerSetForBackgroundThread(
        ThreadId::Current().ToInteger());
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LOCAL_HEAP_INL_H_
