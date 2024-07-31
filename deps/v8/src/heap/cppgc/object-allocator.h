// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_
#define V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_

#include "include/cppgc/allocation.h"
#include "include/cppgc/internal/gc-info.h"
#include "include/cppgc/macros.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/object-start-bitmap.h"
#include "src/heap/cppgc/raw-heap.h"

namespace cppgc {

namespace internal {
class ObjectAllocator;
class PreFinalizerHandler;
}  // namespace internal

class V8_EXPORT AllocationHandle {
 private:
  AllocationHandle() = default;
  friend class internal::ObjectAllocator;
};

namespace internal {

class StatsCollector;
class PageBackend;
class GarbageCollector;

class V8_EXPORT_PRIVATE ObjectAllocator final : public cppgc::AllocationHandle {
 public:
  static constexpr size_t kSmallestSpaceSize = 32;

  ObjectAllocator(RawHeap&, PageBackend&, StatsCollector&, PreFinalizerHandler&,
                  FatalOutOfMemoryHandler&, GarbageCollector&);

  inline void* AllocateObject(size_t size, GCInfoIndex gcinfo);
  inline void* AllocateObject(size_t size, AlignVal alignment,
                              GCInfoIndex gcinfo);
  inline void* AllocateObject(size_t size, GCInfoIndex gcinfo,
                              CustomSpaceIndex space_index);
  inline void* AllocateObject(size_t size, AlignVal alignment,
                              GCInfoIndex gcinfo, CustomSpaceIndex space_index);

  void ResetLinearAllocationBuffers();
  void MarkAllPagesAsYoung();

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  void UpdateAllocationTimeout();
  int get_allocation_timeout_for_testing() const {
    return *allocation_timeout_;
  }
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

 private:
  bool in_disallow_gc_scope() const;

  // Returns the initially tried SpaceType to allocate an object of |size| bytes
  // on. Returns the largest regular object size bucket for large objects.
  inline static RawHeap::RegularSpaceType GetInitialSpaceIndexForSize(
      size_t size);

  inline void* AllocateObjectOnSpace(NormalPageSpace&, size_t, GCInfoIndex);
  inline void* AllocateObjectOnSpace(NormalPageSpace&, size_t, AlignVal,
                                     GCInfoIndex);
  inline void* OutOfLineAllocate(NormalPageSpace&, size_t, AlignVal,
                                 GCInfoIndex);

  // Called from the fast path LAB allocation when the LAB capacity cannot fit
  // the allocation or a large object is requested. Use out parameter as
  // `V8_PRESERVE_MOST` cannot handle non-void return values.
  //
  // Prefer using `OutOfLineAllocate()`.
  void V8_PRESERVE_MOST OutOfLineAllocateGCSafePoint(NormalPageSpace&, size_t,
                                                     AlignVal, GCInfoIndex,
                                                     void**);
  // Raw allocation, does not emit safepoint for conservative GC.
  void* OutOfLineAllocateImpl(NormalPageSpace&, size_t, AlignVal, GCInfoIndex);

  bool TryRefillLinearAllocationBuffer(NormalPageSpace&, size_t);
  bool TryRefillLinearAllocationBufferFromFreeList(NormalPageSpace&, size_t);
  bool TryExpandAndRefillLinearAllocationBuffer(NormalPageSpace&);

#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  void TriggerGCOnAllocationTimeoutIfNeeded();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT

  RawHeap& raw_heap_;
  PageBackend& page_backend_;
  StatsCollector& stats_collector_;
  PreFinalizerHandler& prefinalizer_handler_;
  FatalOutOfMemoryHandler& oom_handler_;
  GarbageCollector& garbage_collector_;
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  // Specifies how many allocations should be performed until triggering a
  // garbage collection.
  v8::base::Optional<int> allocation_timeout_;
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
};

void* ObjectAllocator::AllocateObject(size_t size, GCInfoIndex gcinfo) {
  DCHECK(!in_disallow_gc_scope());
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  TriggerGCOnAllocationTimeoutIfNeeded();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
  const size_t allocation_size =
      RoundUp<kAllocationGranularity>(size + sizeof(HeapObjectHeader));
  const RawHeap::RegularSpaceType type =
      GetInitialSpaceIndexForSize(allocation_size);
  return AllocateObjectOnSpace(NormalPageSpace::From(*raw_heap_.Space(type)),
                               allocation_size, gcinfo);
}

void* ObjectAllocator::AllocateObject(size_t size, AlignVal alignment,
                                      GCInfoIndex gcinfo) {
  DCHECK(!in_disallow_gc_scope());
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  TriggerGCOnAllocationTimeoutIfNeeded();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
  const size_t allocation_size =
      RoundUp<kAllocationGranularity>(size + sizeof(HeapObjectHeader));
  const RawHeap::RegularSpaceType type =
      GetInitialSpaceIndexForSize(allocation_size);
  return AllocateObjectOnSpace(NormalPageSpace::From(*raw_heap_.Space(type)),
                               allocation_size, alignment, gcinfo);
}

void* ObjectAllocator::AllocateObject(size_t size, GCInfoIndex gcinfo,
                                      CustomSpaceIndex space_index) {
  DCHECK(!in_disallow_gc_scope());
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  TriggerGCOnAllocationTimeoutIfNeeded();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
  const size_t allocation_size =
      RoundUp<kAllocationGranularity>(size + sizeof(HeapObjectHeader));
  return AllocateObjectOnSpace(
      NormalPageSpace::From(*raw_heap_.CustomSpace(space_index)),
      allocation_size, gcinfo);
}

void* ObjectAllocator::AllocateObject(size_t size, AlignVal alignment,
                                      GCInfoIndex gcinfo,
                                      CustomSpaceIndex space_index) {
  DCHECK(!in_disallow_gc_scope());
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  TriggerGCOnAllocationTimeoutIfNeeded();
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
  const size_t allocation_size =
      RoundUp<kAllocationGranularity>(size + sizeof(HeapObjectHeader));
  return AllocateObjectOnSpace(
      NormalPageSpace::From(*raw_heap_.CustomSpace(space_index)),
      allocation_size, alignment, gcinfo);
}

// static
RawHeap::RegularSpaceType ObjectAllocator::GetInitialSpaceIndexForSize(
    size_t size) {
  static_assert(kSmallestSpaceSize == 32,
                "should be half the next larger size");
  if (size < 64) {
    if (size < kSmallestSpaceSize) return RawHeap::RegularSpaceType::kNormal1;
    return RawHeap::RegularSpaceType::kNormal2;
  }
  if (size < 128) return RawHeap::RegularSpaceType::kNormal3;
  return RawHeap::RegularSpaceType::kNormal4;
}

void* ObjectAllocator::OutOfLineAllocate(NormalPageSpace& space, size_t size,
                                         AlignVal alignment,
                                         GCInfoIndex gcinfo) {
  void* object;
  OutOfLineAllocateGCSafePoint(space, size, alignment, gcinfo, &object);
  return object;
}

void* ObjectAllocator::AllocateObjectOnSpace(NormalPageSpace& space,
                                             size_t size, AlignVal alignment,
                                             GCInfoIndex gcinfo) {
  // The APIs are set up to support general alignment. Since we want to keep
  // track of the actual usage there the alignment support currently only covers
  // double-world alignment (8 bytes on 32bit and 16 bytes on 64bit
  // architectures). This is enforced on the public API via static_asserts
  // against alignof(T).
  static_assert(2 * kAllocationGranularity ==
                api_constants::kMaxSupportedAlignment);
  static_assert(kAllocationGranularity == sizeof(HeapObjectHeader));
  static_assert(kAllocationGranularity ==
                api_constants::kAllocationGranularity);
  DCHECK_EQ(2 * sizeof(HeapObjectHeader), static_cast<size_t>(alignment));
  constexpr size_t kAlignment = 2 * kAllocationGranularity;
  constexpr size_t kAlignmentMask = kAlignment - 1;
  constexpr size_t kPaddingSize = kAlignment - sizeof(HeapObjectHeader);

  NormalPageSpace::LinearAllocationBuffer& current_lab =
      space.linear_allocation_buffer();
  const size_t current_lab_size = current_lab.size();
  // Case 1: The LAB fits the request and the LAB start is already properly
  // aligned.
  bool lab_allocation_will_succeed =
      current_lab_size >= size &&
      (reinterpret_cast<uintptr_t>(current_lab.start() +
                                   sizeof(HeapObjectHeader)) &
       kAlignmentMask) == 0;
  // Case 2: The LAB fits an extended request to manually align the second
  // allocation.
  if (!lab_allocation_will_succeed &&
      (current_lab_size >= (size + kPaddingSize))) {
    void* filler_memory = current_lab.Allocate(kPaddingSize);
    auto& filler = Filler::CreateAt(filler_memory, kPaddingSize);
    NormalPage::From(BasePage::FromPayload(&filler))
        ->object_start_bitmap()
        .SetBit<AccessMode::kAtomic>(reinterpret_cast<ConstAddress>(&filler));
    lab_allocation_will_succeed = true;
  }
  if (V8_UNLIKELY(!lab_allocation_will_succeed)) {
    return OutOfLineAllocate(space, size, alignment, gcinfo);
  }
  void* object = AllocateObjectOnSpace(space, size, gcinfo);
  DCHECK_NOT_NULL(object);
  DCHECK_EQ(0u, reinterpret_cast<uintptr_t>(object) & kAlignmentMask);
  return object;
}

void* ObjectAllocator::AllocateObjectOnSpace(NormalPageSpace& space,
                                             size_t size, GCInfoIndex gcinfo) {
  DCHECK_LT(0u, gcinfo);

  NormalPageSpace::LinearAllocationBuffer& current_lab =
      space.linear_allocation_buffer();
  if (V8_UNLIKELY(current_lab.size() < size)) {
    return OutOfLineAllocate(
        space, size, static_cast<AlignVal>(kAllocationGranularity), gcinfo);
  }

  void* raw = current_lab.Allocate(size);
#if !defined(V8_USE_MEMORY_SANITIZER) && !defined(V8_USE_ADDRESS_SANITIZER) && \
    DEBUG
  // For debug builds, unzap only the payload.
  SetMemoryAccessible(static_cast<char*>(raw) + sizeof(HeapObjectHeader),
                      size - sizeof(HeapObjectHeader));
#else
  SetMemoryAccessible(raw, size);
#endif
  auto* header = new (raw) HeapObjectHeader(size, gcinfo);

  // The marker needs to find the object start concurrently.
  NormalPage::From(BasePage::FromPayload(header))
      ->object_start_bitmap()
      .SetBit<AccessMode::kAtomic>(reinterpret_cast<ConstAddress>(header));

  return header->ObjectStart();
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_OBJECT_ALLOCATOR_H_
