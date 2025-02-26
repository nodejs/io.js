// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/write-barrier.h"

#include "include/cppgc/heap-consistency.h"
#include "include/cppgc/internal/member-storage.h"
#include "include/cppgc/internal/pointer-policies.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marker.h"
#include "src/heap/cppgc/marking-visitor.h"

#if defined(CPPGC_CAGED_HEAP)
#include "include/cppgc/internal/caged-heap-local-data.h"
#endif

namespace cppgc {
namespace internal {

// static
AtomicEntryFlag WriteBarrier::write_barrier_enabled_;

// static
void WriteBarrier::DijkstraMarkingBarrierSlowWithSentinelCheck(
    const void* value) {
  if (!value || value == kSentinelPointer) return;

  DijkstraMarkingBarrierSlow(value);
}

// static
void WriteBarrier::DijkstraMarkingBarrierSlow(const void* value) {
  const BasePage* page = BasePage::FromPayload(value);
  const auto& heap = page->heap();

  // GetWriteBarrierType() checks marking state.
  DCHECK(heap.marker());
  // No write barriers should be executed from atomic pause marking.
  DCHECK(!heap.in_atomic_pause());
  DCHECK(heap.is_incremental_marking_in_progress());

  auto& header =
      const_cast<HeapObjectHeader&>(page->ObjectHeaderFromInnerAddress(value));
  heap.marker()->WriteBarrierForObject<MarkerBase::WriteBarrierType::kDijkstra>(
      header);
}

// static
void WriteBarrier::DijkstraMarkingBarrierRangeSlow(
    HeapHandle& heap_handle, const void* first_element, size_t element_size,
    size_t number_of_elements, TraceCallback trace_callback) {
  auto& heap_base = HeapBase::From(heap_handle);

  // GetWriteBarrierType() checks marking state.
  DCHECK(heap_base.marker());
  // No write barriers should be executed from atomic pause marking.
  DCHECK(!heap_base.in_atomic_pause());

  cppgc::subtle::DisallowGarbageCollectionScope disallow_gc_scope(heap_base);
  const char* array = static_cast<const char*>(first_element);
  while (number_of_elements-- > 0) {
    trace_callback(&heap_base.marker()->Visitor(), array);
    array += element_size;
  }
}

// static
void WriteBarrier::SteeleMarkingBarrierSlowWithSentinelCheck(
    const void* value) {
  if (!value || value == kSentinelPointer) return;

  SteeleMarkingBarrierSlow(value);
}

// static
void WriteBarrier::SteeleMarkingBarrierSlow(const void* value) {
  const BasePage* page = BasePage::FromPayload(value);
  const auto& heap = page->heap();

  // GetWriteBarrierType() checks marking state.
  DCHECK(heap.marker());
  // No write barriers should be executed from atomic pause marking.
  DCHECK(!heap.in_atomic_pause());
  DCHECK(heap.is_incremental_marking_in_progress());

  auto& header =
      const_cast<HeapObjectHeader&>(page->ObjectHeaderFromInnerAddress(value));
  heap.marker()->WriteBarrierForObject<MarkerBase::WriteBarrierType::kSteele>(
      header);
}

#if defined(CPPGC_YOUNG_GENERATION)
// static
void WriteBarrier::GenerationalBarrierSlow(const CagedHeapLocalData& local_data,
                                           const AgeTable& age_table,
                                           const void* slot,
                                           uintptr_t value_offset,
                                           HeapHandle* heap_handle) {
  DCHECK(slot);
  DCHECK(heap_handle);
  DCHECK_GT(api_constants::kCagedHeapMaxReservationSize, value_offset);
  // A write during atomic pause (e.g. pre-finalizer) may trigger the slow path
  // of the barrier. This is a result of the order of bailouts where not marking
  // results in applying the generational barrier.
  auto& heap = HeapBase::From(*heap_handle);
  if (heap.in_atomic_pause()) return;

  if (value_offset > 0 && age_table.GetAge(value_offset) == AgeTable::Age::kOld)
    return;

  // Record slot.
  heap.remembered_set().AddSlot((const_cast<void*>(slot)));
}

// static
void WriteBarrier::GenerationalBarrierForUncompressedSlotSlow(
    const CagedHeapLocalData& local_data, const AgeTable& age_table,
    const void* slot, uintptr_t value_offset, HeapHandle* heap_handle) {
  DCHECK(slot);
  DCHECK(heap_handle);
  DCHECK_GT(api_constants::kCagedHeapMaxReservationSize, value_offset);
  // A write during atomic pause (e.g. pre-finalizer) may trigger the slow path
  // of the barrier. This is a result of the order of bailouts where not marking
  // results in applying the generational barrier.
  auto& heap = HeapBase::From(*heap_handle);
  if (heap.in_atomic_pause()) return;

  if (value_offset > 0 && age_table.GetAge(value_offset) == AgeTable::Age::kOld)
    return;

  // Record slot.
  heap.remembered_set().AddUncompressedSlot((const_cast<void*>(slot)));
}

// static
void WriteBarrier::GenerationalBarrierForSourceObjectSlow(
    const CagedHeapLocalData& local_data, const void* inner_pointer,
    HeapHandle* heap_handle) {
  DCHECK(inner_pointer);
  DCHECK(heap_handle);

  auto& heap = HeapBase::From(*heap_handle);

  auto& object_header =
      BasePage::FromInnerAddress(&heap, inner_pointer)
          ->ObjectHeaderFromInnerAddress<AccessMode::kAtomic>(inner_pointer);

  // Record the source object.
  heap.remembered_set().AddSourceObject(
      const_cast<HeapObjectHeader&>(object_header));
}
#endif  // CPPGC_YOUNG_GENERATION

#if V8_ENABLE_CHECKS
// static
void WriteBarrier::CheckParams(Type expected_type, const Params& params) {
  CHECK_EQ(expected_type, params.type);
}
#endif  // V8_ENABLE_CHECKS

#if defined(CPPGC_YOUNG_GENERATION)

// static
YoungGenerationEnabler& YoungGenerationEnabler::Instance() {
  static v8::base::LeakyObject<YoungGenerationEnabler> instance;
  return *instance.get();
}

void YoungGenerationEnabler::Enable() {
  auto& instance = Instance();
  v8::base::SpinningMutexGuard _(&instance.mutex_);
  if (++instance.is_enabled_ == 1) {
    // Enter the flag so that the check in the write barrier will always trigger
    // when young generation is enabled.
    WriteBarrier::FlagUpdater::Enter();
  }
}

void YoungGenerationEnabler::Disable() {
  auto& instance = Instance();
  v8::base::SpinningMutexGuard _(&instance.mutex_);
  DCHECK_LT(0, instance.is_enabled_);
  if (--instance.is_enabled_ == 0) {
    WriteBarrier::FlagUpdater::Exit();
  }
}

bool YoungGenerationEnabler::IsEnabled() {
  auto& instance = Instance();
  v8::base::SpinningMutexGuard _(&instance.mutex_);
  return instance.is_enabled_;
}

#endif  // defined(CPPGC_YOUNG_GENERATION)

#ifdef CPPGC_SLIM_WRITE_BARRIER

// static
template <WriteBarrierSlotType SlotType>
void WriteBarrier::CombinedWriteBarrierSlow(const void* slot) {
  DCHECK_NOT_NULL(slot);

  const void* value = nullptr;
#if defined(CPPGC_POINTER_COMPRESSION)
  if constexpr (SlotType == WriteBarrierSlotType::kCompressed) {
    value = CompressedPointer::Decompress(
        *static_cast<const CompressedPointer::IntegralType*>(slot));
  } else {
    value = *reinterpret_cast<const void* const*>(slot);
  }
#else
  static_assert(SlotType == WriteBarrierSlotType::kUncompressed);
  value = *reinterpret_cast<const void* const*>(slot);
#endif

  WriteBarrier::Params params;
  const WriteBarrier::Type type =
      WriteBarrier::GetWriteBarrierType(slot, value, params);
  switch (type) {
    case WriteBarrier::Type::kGenerational:
      WriteBarrier::GenerationalBarrier<
          WriteBarrier::GenerationalBarrierType::kPreciseSlot>(params, slot);
      break;
    case WriteBarrier::Type::kMarking:
      WriteBarrier::DijkstraMarkingBarrier(params, value);
      break;
    case WriteBarrier::Type::kNone:
      // The fast checks are approximate and may trigger spuriously if any heap
      // has marking in progress. `GetWriteBarrierType()` above is exact which
      // is the reason we could still observe a bailout here.
      break;
  }
}

template V8_EXPORT_PRIVATE void WriteBarrier::CombinedWriteBarrierSlow<
    WriteBarrierSlotType::kUncompressed>(const void* slot);
#if defined(CPPGC_POINTER_COMPRESSION)
template V8_EXPORT_PRIVATE void WriteBarrier::CombinedWriteBarrierSlow<
    WriteBarrierSlotType::kCompressed>(const void* slot);
#endif  // defined(CPPGC_POINTER_COMPRESSION)

#endif  // CPPGC_SLIM_WRITE_BARRIER

}  // namespace internal
}  // namespace cppgc
