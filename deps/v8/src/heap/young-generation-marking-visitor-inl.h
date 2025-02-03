// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_INL_H_
#define V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_INL_H_

#include "src/common/globals.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/minor-mark-sweep.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/objects-visiting.h"
#include "src/heap/pretenuring-handler-inl.h"
#include "src/heap/remembered-set-inl.h"
#include "src/heap/young-generation-marking-visitor.h"

namespace v8 {
namespace internal {

template <YoungGenerationMarkingVisitationMode marking_mode>
YoungGenerationMarkingVisitor<marking_mode>::YoungGenerationMarkingVisitor(
    Heap* heap,
    PretenuringHandler::PretenuringFeedbackMap* local_pretenuring_feedback)
    : Base(heap->isolate()),
      isolate_(heap->isolate()),
      marking_worklists_local_(
          heap->minor_mark_sweep_collector()->marking_worklists(),
          heap->cpp_heap()
              ? CppHeap::From(heap->cpp_heap())->CreateCppMarkingState()
              : MarkingWorklists::Local::kNoCppMarkingState),
      ephemeron_table_list_local_(
          *heap->minor_mark_sweep_collector()->ephemeron_table_list()),
      pretenuring_handler_(heap->pretenuring_handler()),
      local_pretenuring_feedback_(local_pretenuring_feedback),
      shortcut_strings_(heap->CanShortcutStringsDuringGC(
          GarbageCollector::MINOR_MARK_SWEEPER)) {}

template <YoungGenerationMarkingVisitationMode marking_mode>
YoungGenerationMarkingVisitor<marking_mode>::~YoungGenerationMarkingVisitor() {
  PublishWorklists();

  // Flush memory chunk live bytes. Atomics are used for incrementing the live
  // bytes counter of the page, so there is no need to defer flushing to the
  // main thread.
  for (auto& pair : live_bytes_data_) {
    if (pair.first) {
      pair.first->IncrementLiveBytesAtomically(pair.second);
    }
  }
}

template <YoungGenerationMarkingVisitationMode marking_mode>
void YoungGenerationMarkingVisitor<marking_mode>::VisitCppHeapPointer(
    Tagged<HeapObject> host, CppHeapPointerSlot slot) {
  if (!marking_worklists_local_.cpp_marking_state()) return;

  // The table is not reclaimed in the young generation, so we only need to mark
  // through to the C++ pointer.

  if (auto cpp_heap_pointer = slot.try_load(isolate_, kAnyCppHeapPointer)) {
    marking_worklists_local_.cpp_marking_state()->MarkAndPush(
        reinterpret_cast<void*>(cpp_heap_pointer));
  }
}

template <YoungGenerationMarkingVisitationMode marking_mode>
int YoungGenerationMarkingVisitor<marking_mode>::VisitJSArrayBuffer(
    Tagged<Map> map, Tagged<JSArrayBuffer> object) {
  object->YoungMarkExtension();
  return Base::VisitJSArrayBuffer(map, object);
}

template <YoungGenerationMarkingVisitationMode marking_mode>
template <typename T, typename TBodyDescriptor>
int YoungGenerationMarkingVisitor<marking_mode>::VisitJSObjectSubclass(
    Tagged<Map> map, Tagged<T> object) {
  pretenuring_handler_->UpdateAllocationSite(map, object,
                                             local_pretenuring_feedback_);
  return Base::template VisitJSObjectSubclass<T, TBodyDescriptor>(map, object);
}

template <YoungGenerationMarkingVisitationMode marking_mode>
int YoungGenerationMarkingVisitor<marking_mode>::VisitEphemeronHashTable(
    Tagged<Map> map, Tagged<EphemeronHashTable> table) {
  // Register table with Minor MC, so it can take care of the weak keys later.
  // This allows to only iterate the tables' values, which are treated as strong
  // independently of whether the key is live.
  ephemeron_table_list_local_.Push(table);
  for (InternalIndex i : table->IterateEntries()) {
    ObjectSlot value_slot =
        table->RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));
    VisitPointer(table, value_slot);
  }
  return EphemeronHashTable::BodyDescriptor::SizeOf(map, table);
}

#ifdef V8_COMPRESS_POINTERS
template <YoungGenerationMarkingVisitationMode marking_mode>
void YoungGenerationMarkingVisitor<marking_mode>::VisitExternalPointer(
    Tagged<HeapObject> host, ExternalPointerSlot slot) {
  // With sticky mark-bits the host object was already marked (old).
  DCHECK_IMPLIES(!v8_flags.sticky_mark_bits, Heap::InYoungGeneration(host));
  DCHECK_NE(slot.tag(), kExternalPointerNullTag);
  DCHECK(!IsSharedExternalPointerType(slot.tag()));

  // TODO(chromium:337580006): Remove when pointer compression always uses
  // EPT.
  if (!slot.HasExternalPointerHandle()) return;

  ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
  if (handle != kNullExternalPointerHandle) {
    ExternalPointerTable& table = isolate_->external_pointer_table();
    auto* space = isolate_->heap()->young_external_pointer_space();
    table.Mark(space, handle, slot.address());
  }

  // Add to the remset whether the handle is null or not, as the slot could be
  // set to a non-null value before the marking pause.
  // TODO(342905179): Avoid adding null handle locations to the remset, and
  // instead make external pointer writes invoke a marking barrier.
  auto slot_chunk = MutablePageMetadata::FromHeapObject(host);
  RememberedSet<SURVIVOR_TO_EXTERNAL_POINTER>::template Insert<
      AccessMode::ATOMIC>(slot_chunk, slot_chunk->Offset(slot.address()));
}
#endif  // V8_COMPRESS_POINTERS

template <YoungGenerationMarkingVisitationMode marking_mode>
template <typename TSlot>
void YoungGenerationMarkingVisitor<marking_mode>::VisitPointersImpl(
    Tagged<HeapObject> host, TSlot start, TSlot end) {
  for (TSlot slot = start; slot < end; ++slot) {
    if constexpr (marking_mode ==
                  YoungGenerationMarkingVisitationMode::kConcurrent) {
      VisitObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                         SlotTreatmentMode::kReadOnly>(slot);
    } else {
      VisitObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                         SlotTreatmentMode::kReadWrite>(slot);
    }
  }
}

template <YoungGenerationMarkingVisitationMode marking_mode>
template <typename TSlot>
V8_INLINE bool
YoungGenerationMarkingVisitor<marking_mode>::VisitObjectViaSlotInRememberedSet(
    TSlot slot) {
  if constexpr (marking_mode ==
                YoungGenerationMarkingVisitationMode::kConcurrent) {
    return VisitObjectViaSlot<ObjectVisitationMode::kPushToWorklist,
                              SlotTreatmentMode::kReadOnly>(slot);
  } else {
    return VisitObjectViaSlot<ObjectVisitationMode::kVisitDirectly,
                              SlotTreatmentMode::kReadWrite>(slot);
  }
}

template <YoungGenerationMarkingVisitationMode marking_mode>
template <typename YoungGenerationMarkingVisitor<
              marking_mode>::ObjectVisitationMode visitation_mode,
          typename YoungGenerationMarkingVisitor<
              marking_mode>::SlotTreatmentMode slot_treatment_mode,
          typename TSlot>
V8_INLINE bool YoungGenerationMarkingVisitor<marking_mode>::VisitObjectViaSlot(
    TSlot slot) {
  const std::optional<Tagged<Object>> optional_object =
      this->GetObjectFilterReadOnlyAndSmiFast(slot);
  if (!optional_object) {
    return false;
  }
  typename TSlot::TObject target = *optional_object;
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (target.ptr() == kTaggedNullAddress) return false;
#endif
  Tagged<HeapObject> heap_object;
  // Treat weak references as strong.
  if (!target.GetHeapObject(&heap_object)) {
    return false;
  }

#ifdef THREAD_SANITIZER
  MemoryChunk::FromHeapObject(heap_object)->SynchronizedLoad();
#endif  // THREAD_SANITIZER

  if (!Heap::InYoungGeneration(heap_object)) {
    return false;
  }

#ifdef V8_MINORMS_STRING_SHORTCUTTING
  if (slot_treatment_mode == SlotTreatmentMode::kReadWrite &&
      !ShortCutStrings(reinterpret_cast<HeapObjectSlot&>(slot), &heap_object)) {
    return false;
  }
#endif  // V8_MINORMS_STRING_SHORTCUTTING

  if (!TryMark(heap_object)) return true;

  // Maps won't change in the atomic pause, so the map can be read without
  // atomics.
  if constexpr (visitation_mode == ObjectVisitationMode::kVisitDirectly) {
    Tagged<Map> map = heap_object->map(isolate_);
    const int visited_size = Base::Visit(map, heap_object);
    if (visited_size) {
      IncrementLiveBytesCached(
          MutablePageMetadata::cast(
              MemoryChunkMetadata::FromHeapObject(heap_object)),
          ALIGN_TO_ALLOCATION_ALIGNMENT(visited_size));
    }
    return true;
  }
  // Default case: Visit via worklist.
  marking_worklists_local_.Push(heap_object);

  return true;
}

#ifdef V8_MINORMS_STRING_SHORTCUTTING
template <YoungGenerationMarkingVisitationMode marking_mode>
V8_INLINE bool YoungGenerationMarkingVisitor<marking_mode>::ShortCutStrings(
    HeapObjectSlot slot, Tagged<HeapObject>* heap_object) {
  DCHECK_EQ(YoungGenerationMarkingVisitationMode::kParallel, marking_mode);
  if (shortcut_strings_) {
    DCHECK(V8_STATIC_ROOTS_BOOL);
#if V8_STATIC_ROOTS_BOOL
    ObjectSlot map_slot = (*heap_object)->map_slot();
    Address map_address = map_slot.load_map().ptr();
    if (map_address == StaticReadOnlyRoot::kThinOneByteStringMap ||
        map_address == StaticReadOnlyRoot::kThinTwoByteStringMap) {
      DCHECK_EQ((*heap_object)
                    ->map(ObjectVisitorWithCageBases::cage_base())
                    ->visitor_id(),
                VisitorId::kVisitThinString);
      *heap_object = Cast<ThinString>(*heap_object)->actual();
      // ThinStrings always refer to internalized strings, which are always
      // in old space.
      DCHECK(!Heap::InYoungGeneration(*heap_object));
      slot.StoreHeapObject(*heap_object);
      return false;
    } else if (map_address == StaticReadOnlyRoot::kConsOneByteStringMap ||
               map_address == StaticReadOnlyRoot::kConsTwoByteStringMap) {
      // Not all ConsString are short cut candidates.
      const VisitorId visitor_id =
          (*heap_object)
              ->map(ObjectVisitorWithCageBases::cage_base())
              ->visitor_id();
      if (visitor_id == VisitorId::kVisitShortcutCandidate) {
        Tagged<ConsString> string = Cast<ConsString>(*heap_object);
        if (static_cast<Tagged_t>(string->second().ptr()) ==
            StaticReadOnlyRoot::kempty_string) {
          *heap_object = string->first();
          slot.StoreHeapObject(*heap_object);
          if (!Heap::InYoungGeneration(*heap_object)) {
            return false;
          }
        }
      }
    }
#endif  // V8_STATIC_ROOTS_BOOL
  }
  return true;
}
#endif  // V8_MINORMS_STRING_SHORTCUTTING

template <YoungGenerationMarkingVisitationMode marking_mode>
V8_INLINE void
YoungGenerationMarkingVisitor<marking_mode>::IncrementLiveBytesCached(
    MutablePageMetadata* chunk, intptr_t by) {
  DCHECK_IMPLIES(V8_COMPRESS_POINTERS_8GB_BOOL,
                 IsAligned(by, kObjectAlignment8GbHeap));
  const size_t hash = base::hash<MutablePageMetadata*>()(chunk) & kEntriesMask;
  auto& entry = live_bytes_data_[hash];
  if (entry.first && entry.first != chunk) {
    entry.first->IncrementLiveBytesAtomically(entry.second);
    entry.first = chunk;
    entry.second = 0;
  } else {
    entry.first = chunk;
  }
  entry.second += by;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_YOUNG_GENERATION_MARKING_VISITOR_INL_H_
