// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SCAVENGER_INL_H_
#define V8_HEAP_SCAVENGER_INL_H_

#include "src/codegen/assembler-inl.h"
#include "src/heap/evacuation-allocator-inl.h"
#include "src/heap/incremental-marking-inl.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/new-spaces.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/pretenuring-handler-inl.h"
#include "src/heap/scavenger.h"
#include "src/objects/js-objects.h"
#include "src/objects/map.h"
#include "src/objects/objects-body-descriptors-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/slots-inl.h"

namespace v8 {
namespace internal {

void Scavenger::PromotionList::Local::PushRegularObject(
    Tagged<HeapObject> object, int size) {
  regular_object_promotion_list_local_.Push({object, size});
}

void Scavenger::PromotionList::Local::PushLargeObject(Tagged<HeapObject> object,
                                                      Tagged<Map> map,
                                                      int size) {
  large_object_promotion_list_local_.Push({object, map, size});
}

size_t Scavenger::PromotionList::Local::LocalPushSegmentSize() const {
  return regular_object_promotion_list_local_.PushSegmentSize() +
         large_object_promotion_list_local_.PushSegmentSize();
}

bool Scavenger::PromotionList::Local::Pop(struct PromotionListEntry* entry) {
  ObjectAndSize regular_object;
  if (regular_object_promotion_list_local_.Pop(&regular_object)) {
    entry->heap_object = regular_object.first;
    entry->size = regular_object.second;
    entry->map = entry->heap_object->map();
    return true;
  }
  return large_object_promotion_list_local_.Pop(entry);
}

void Scavenger::PromotionList::Local::Publish() {
  regular_object_promotion_list_local_.Publish();
  large_object_promotion_list_local_.Publish();
}

bool Scavenger::PromotionList::Local::IsGlobalPoolEmpty() const {
  return regular_object_promotion_list_local_.IsGlobalEmpty() &&
         large_object_promotion_list_local_.IsGlobalEmpty();
}

bool Scavenger::PromotionList::Local::ShouldEagerlyProcessPromotionList()
    const {
  // Threshold when to prioritize processing of the promotion list. Right
  // now we only look into the regular object list.
  const int kProcessPromotionListThreshold =
      kRegularObjectPromotionListSegmentSize / 2;
  return LocalPushSegmentSize() >= kProcessPromotionListThreshold;
}

bool Scavenger::PromotionList::IsEmpty() const {
  return regular_object_promotion_list_.IsEmpty() &&
         large_object_promotion_list_.IsEmpty();
}

size_t Scavenger::PromotionList::Size() const {
  return regular_object_promotion_list_.Size() +
         large_object_promotion_list_.Size();
}

void Scavenger::PageMemoryFence(Tagged<MaybeObject> object) {
#ifdef THREAD_SANITIZER
  // Perform a dummy acquire load to tell TSAN that there is no data race
  // with  page initialization.
  Tagged<HeapObject> heap_object;
  if (object.GetHeapObject(&heap_object)) {
    MemoryChunk::FromHeapObject(heap_object)->SynchronizedLoad();
  }
#endif
}

bool Scavenger::MigrateObject(Tagged<Map> map, Tagged<HeapObject> source,
                              Tagged<HeapObject> target, int size,
                              PromotionHeapChoice promotion_heap_choice) {
  // Copy the content of source to target.
  target->set_map_word(map, kRelaxedStore);
  heap()->CopyBlock(target.address() + kTaggedSize,
                    source.address() + kTaggedSize, size - kTaggedSize);

  // This release CAS is paired with the load acquire in ScavengeObject.
  if (!source->release_compare_and_swap_map_word_forwarded(
          MapWord::FromMap(map), target)) {
    // Other task migrated the object.
    return false;
  }

  if (V8_UNLIKELY(is_logging_)) {
    heap()->OnMoveEvent(source, target, size);
  }

  if (is_incremental_marking_ &&
      (promotion_heap_choice != kPromoteIntoSharedHeap || mark_shared_heap_)) {
    heap()->incremental_marking()->TransferColor(source, target);
  }
  pretenuring_handler_->UpdateAllocationSite(map, source,
                                             &local_pretenuring_feedback_);

  return true;
}

template <typename THeapObjectSlot>
CopyAndForwardResult Scavenger::SemiSpaceCopyObject(
    Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
    int object_size, ObjectFields object_fields) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  DCHECK(heap()->AllowedToBeMigrated(map, object, NEW_SPACE));
  AllocationAlignment alignment = HeapObject::RequiredAlignment(map);
  AllocationResult allocation =
      allocator_.Allocate(NEW_SPACE, object_size, alignment);

  Tagged<HeapObject> target;
  if (allocation.To(&target)) {
    DCHECK(heap()->marking_state()->IsUnmarked(target));
    const bool self_success =
        MigrateObject(map, object, target, object_size, kPromoteIntoLocalHeap);
    if (!self_success) {
      allocator_.FreeLast(NEW_SPACE, target, object_size);
      MapWord map_word = object->map_word(kAcquireLoad);
      UpdateHeapObjectReferenceSlot(slot, map_word.ToForwardingAddress(object));
      DCHECK(!Heap::InFromPage(*slot));
      return Heap::InToPage(*slot)
                 ? CopyAndForwardResult::SUCCESS_YOUNG_GENERATION
                 : CopyAndForwardResult::SUCCESS_OLD_GENERATION;
    }
    UpdateHeapObjectReferenceSlot(slot, target);
    if (object_fields == ObjectFields::kMaybePointers) {
      copied_list_local_.Push(ObjectAndSize(target, object_size));
    }
    copied_size_ += object_size;
    return CopyAndForwardResult::SUCCESS_YOUNG_GENERATION;
  }
  return CopyAndForwardResult::FAILURE;
}

template <typename THeapObjectSlot,
          Scavenger::PromotionHeapChoice promotion_heap_choice>
CopyAndForwardResult Scavenger::PromoteObject(Tagged<Map> map,
                                              THeapObjectSlot slot,
                                              Tagged<HeapObject> object,
                                              int object_size,
                                              ObjectFields object_fields) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  DCHECK_GE(object_size, Heap::kMinObjectSizeInTaggedWords * kTaggedSize);
  AllocationAlignment alignment = HeapObject::RequiredAlignment(map);
  AllocationResult allocation = allocator_.Allocate(
      promotion_heap_choice == kPromoteIntoLocalHeap ? OLD_SPACE : SHARED_SPACE,
      object_size, alignment);

  Tagged<HeapObject> target;
  if (allocation.To(&target)) {
    DCHECK(heap()->non_atomic_marking_state()->IsUnmarked(target));
    const bool self_success =
        MigrateObject(map, object, target, object_size, promotion_heap_choice);
    if (!self_success) {
      allocator_.FreeLast(promotion_heap_choice == kPromoteIntoLocalHeap
                              ? OLD_SPACE
                              : SHARED_SPACE,
                          target, object_size);

      MapWord map_word = object->map_word(kAcquireLoad);
      UpdateHeapObjectReferenceSlot(slot, map_word.ToForwardingAddress(object));
      DCHECK(!Heap::InFromPage(*slot));
      return Heap::InToPage(*slot)
                 ? CopyAndForwardResult::SUCCESS_YOUNG_GENERATION
                 : CopyAndForwardResult::SUCCESS_OLD_GENERATION;
    }
    UpdateHeapObjectReferenceSlot(slot, target);

    // During incremental marking we want to push every object in order to
    // record slots for map words. Necessary for map space compaction.
    if (object_fields == ObjectFields::kMaybePointers || is_compacting_) {
      promotion_list_local_.PushRegularObject(target, object_size);
    }
    promoted_size_ += object_size;
    return CopyAndForwardResult::SUCCESS_OLD_GENERATION;
  }
  return CopyAndForwardResult::FAILURE;
}

SlotCallbackResult Scavenger::RememberedSetEntryNeeded(
    CopyAndForwardResult result) {
  DCHECK_NE(CopyAndForwardResult::FAILURE, result);
  return result == CopyAndForwardResult::SUCCESS_YOUNG_GENERATION ? KEEP_SLOT
                                                                  : REMOVE_SLOT;
}

bool Scavenger::HandleLargeObject(Tagged<Map> map, Tagged<HeapObject> object,
                                  int object_size, ObjectFields object_fields) {
  if (NEW_LO_SPACE ==
      MutablePageMetadata::FromHeapObject(object)->owner_identity()) {
    DCHECK(MemoryChunk::FromHeapObject(object)->InNewLargeObjectSpace());
    if (object->release_compare_and_swap_map_word_forwarded(
            MapWord::FromMap(map), object)) {
      surviving_new_large_objects_.insert({object, map});
      promoted_size_ += object_size;
      if (object_fields == ObjectFields::kMaybePointers) {
        promotion_list_local_.PushLargeObject(object, map, object_size);
      }
    }
    return true;
  }
  return false;
}

template <typename THeapObjectSlot,
          Scavenger::PromotionHeapChoice promotion_heap_choice>
SlotCallbackResult Scavenger::EvacuateObjectDefault(
    Tagged<Map> map, THeapObjectSlot slot, Tagged<HeapObject> object,
    int object_size, ObjectFields object_fields) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  SLOW_DCHECK(object->SizeFromMap(map) == object_size);
  CopyAndForwardResult result;

  if (HandleLargeObject(map, object, object_size, object_fields)) {
    return KEEP_SLOT;
  }

  SLOW_DCHECK(static_cast<size_t>(object_size) <=
              MemoryChunkLayout::AllocatableMemoryInDataPage());

  if (!SemiSpaceNewSpace::From(heap()->new_space())
           ->ShouldBePromoted(object.address())) {
    // A semi-space copy may fail due to fragmentation. In that case, we
    // try to promote the object.
    result = SemiSpaceCopyObject(map, slot, object, object_size, object_fields);
    if (result != CopyAndForwardResult::FAILURE) {
      return RememberedSetEntryNeeded(result);
    }
  }

  // We may want to promote this object if the object was already semi-space
  // copied in a previous young generation GC or if the semi-space copy above
  // failed.
  result = PromoteObject<THeapObjectSlot, promotion_heap_choice>(
      map, slot, object, object_size, object_fields);
  if (result != CopyAndForwardResult::FAILURE) {
    return RememberedSetEntryNeeded(result);
  }

  // If promotion failed, we try to copy the object to the other semi-space.
  result = SemiSpaceCopyObject(map, slot, object, object_size, object_fields);
  if (result != CopyAndForwardResult::FAILURE) {
    return RememberedSetEntryNeeded(result);
  }

  heap()->FatalProcessOutOfMemory("Scavenger: semi-space copy");
  UNREACHABLE();
}

template <typename THeapObjectSlot>
SlotCallbackResult Scavenger::EvacuateThinString(Tagged<Map> map,
                                                 THeapObjectSlot slot,
                                                 Tagged<ThinString> object,
                                                 int object_size) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  if (shortcut_strings_) {
    // The ThinString should die after Scavenge, so avoid writing the proper
    // forwarding pointer and instead just signal the actual object as forwarded
    // reference.
    Tagged<String> actual = object->actual();
    // ThinStrings always refer to internalized strings, which are always in old
    // space.
    DCHECK(!Heap::InYoungGeneration(actual));
    UpdateHeapObjectReferenceSlot(slot, actual);
    return REMOVE_SLOT;
  }

  DCHECK_EQ(ObjectFields::kMaybePointers,
            Map::ObjectFieldsFrom(map->visitor_id()));
  return EvacuateObjectDefault(map, slot, object, object_size,
                               ObjectFields::kMaybePointers);
}

template <typename THeapObjectSlot>
SlotCallbackResult Scavenger::EvacuateShortcutCandidate(
    Tagged<Map> map, THeapObjectSlot slot, Tagged<ConsString> object,
    int object_size) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  DCHECK(IsShortcutCandidate(map->instance_type()));

  if (shortcut_strings_ &&
      object->unchecked_second() == ReadOnlyRoots(heap()).empty_string()) {
    Tagged<HeapObject> first = Cast<HeapObject>(object->unchecked_first());

    UpdateHeapObjectReferenceSlot(slot, first);

    if (!Heap::InYoungGeneration(first)) {
      object->set_map_word_forwarded(first, kReleaseStore);
      return REMOVE_SLOT;
    }

    MapWord first_word = first->map_word(kAcquireLoad);
    if (first_word.IsForwardingAddress()) {
      Tagged<HeapObject> target = first_word.ToForwardingAddress(first);

      UpdateHeapObjectReferenceSlot(slot, target);
      object->set_map_word_forwarded(target, kReleaseStore);
      return Heap::InYoungGeneration(target) ? KEEP_SLOT : REMOVE_SLOT;
    }
    Tagged<Map> first_map = first_word.ToMap();
    SlotCallbackResult result = EvacuateObjectDefault(
        first_map, slot, first, first->SizeFromMap(first_map),
        Map::ObjectFieldsFrom(first_map->visitor_id()));
    object->set_map_word_forwarded(slot.ToHeapObject(), kReleaseStore);
    return result;
  }
  DCHECK_EQ(ObjectFields::kMaybePointers,
            Map::ObjectFieldsFrom(map->visitor_id()));
  return EvacuateObjectDefault(map, slot, object, object_size,
                               ObjectFields::kMaybePointers);
}

template <typename THeapObjectSlot>
SlotCallbackResult Scavenger::EvacuateInPlaceInternalizableString(
    Tagged<Map> map, THeapObjectSlot slot, Tagged<String> object,
    int object_size, ObjectFields object_fields) {
  DCHECK(String::IsInPlaceInternalizable(map->instance_type()));
  DCHECK_EQ(object_fields, Map::ObjectFieldsFrom(map->visitor_id()));
  if (shared_string_table_) {
    return EvacuateObjectDefault<THeapObjectSlot, kPromoteIntoSharedHeap>(
        map, slot, object, object_size, object_fields);
  }
  return EvacuateObjectDefault(map, slot, object, object_size, object_fields);
}

template <typename THeapObjectSlot>
SlotCallbackResult Scavenger::EvacuateObject(THeapObjectSlot slot,
                                             Tagged<Map> map,
                                             Tagged<HeapObject> source) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  SLOW_DCHECK(Heap::InFromPage(source));
  SLOW_DCHECK(!MapWord::FromMap(map).IsForwardingAddress());
  int size = source->SizeFromMap(map);
  // Cannot use ::cast() below because that would add checks in debug mode
  // that require re-reading the map.
  VisitorId visitor_id = map->visitor_id();
  switch (visitor_id) {
    case kVisitThinString:
      // At the moment we don't allow weak pointers to thin strings.
      DCHECK(!(*slot).IsWeak());
      return EvacuateThinString(map, slot, UncheckedCast<ThinString>(source),
                                size);
    case kVisitShortcutCandidate:
      DCHECK(!(*slot).IsWeak());
      // At the moment we don't allow weak pointers to cons strings.
      return EvacuateShortcutCandidate(map, slot,
                                       UncheckedCast<ConsString>(source), size);
    case kVisitSeqOneByteString:
    case kVisitSeqTwoByteString:
      DCHECK(String::IsInPlaceInternalizable(map->instance_type()));
      static_assert(Map::ObjectFieldsFrom(kVisitSeqOneByteString) ==
                    Map::ObjectFieldsFrom(kVisitSeqTwoByteString));
      return EvacuateInPlaceInternalizableString(
          map, slot, UncheckedCast<String>(source), size,
          Map::ObjectFieldsFrom(kVisitSeqOneByteString));
    default:
      return EvacuateObjectDefault(map, slot, source, size,
                                   Map::ObjectFieldsFrom(visitor_id));
  }
}

template <typename THeapObjectSlot>
SlotCallbackResult Scavenger::ScavengeObject(THeapObjectSlot p,
                                             Tagged<HeapObject> object) {
  static_assert(std::is_same<THeapObjectSlot, FullHeapObjectSlot>::value ||
                    std::is_same<THeapObjectSlot, HeapObjectSlot>::value,
                "Only FullHeapObjectSlot and HeapObjectSlot are expected here");
  DCHECK(Heap::InFromPage(object));

  // Synchronized load that consumes the publishing CAS of MigrateObject. We
  // need memory ordering in order to read the page header of the forwarded
  // object (using Heap::InYoungGeneration).
  MapWord first_word = object->map_word(kAcquireLoad);

  // If the first word is a forwarding address, the object has already been
  // copied.
  if (first_word.IsForwardingAddress()) {
    Tagged<HeapObject> dest = first_word.ToForwardingAddress(object);
    UpdateHeapObjectReferenceSlot(p, dest);
    DCHECK_IMPLIES(Heap::InYoungGeneration(dest),
                   Heap::InToPage(dest) || Heap::IsLargeObject(dest));

    // This load forces us to have memory ordering for the map load above. We
    // need to have the page header properly initialized.
    return Heap::InYoungGeneration(dest) ? KEEP_SLOT : REMOVE_SLOT;
  }

  Tagged<Map> map = first_word.ToMap();
  // AllocationMementos are unrooted and shouldn't survive a scavenge
  DCHECK_NE(ReadOnlyRoots(heap()).allocation_memento_map(), map);
  // Call the slow part of scavenge object.
  return EvacuateObject(p, map, object);
}

template <typename TSlot>
SlotCallbackResult Scavenger::CheckAndScavengeObject(Heap* heap, TSlot slot) {
  static_assert(
      std::is_same<TSlot, FullMaybeObjectSlot>::value ||
          std::is_same<TSlot, MaybeObjectSlot>::value,
      "Only FullMaybeObjectSlot and MaybeObjectSlot are expected here");
  using THeapObjectSlot = typename TSlot::THeapObjectSlot;
  Tagged<MaybeObject> object = *slot;
  if (Heap::InFromPage(object)) {
    Tagged<HeapObject> heap_object = object.GetHeapObject();

    SlotCallbackResult result =
        ScavengeObject(THeapObjectSlot(slot), heap_object);
    DCHECK_IMPLIES(result == REMOVE_SLOT,
                   !heap->InYoungGeneration((*slot).GetHeapObject()));
    return result;
  } else if (Heap::InToPage(object)) {
    // Already updated slot. This can happen when processing of the work list
    // is interleaved with processing roots.
    return KEEP_SLOT;
  }
  // Slots can point to "to" space if the slot has been recorded multiple
  // times in the remembered set. We remove the redundant slot now.
  return REMOVE_SLOT;
}

class ScavengeVisitor final : public NewSpaceVisitor<ScavengeVisitor> {
 public:
  explicit ScavengeVisitor(Scavenger* scavenger);

  V8_INLINE void VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                               ObjectSlot end) final;

  V8_INLINE void VisitPointers(Tagged<HeapObject> host, MaybeObjectSlot start,
                               MaybeObjectSlot end) final;
  V8_INLINE int VisitEphemeronHashTable(Tagged<Map> map,
                                        Tagged<EphemeronHashTable> object);
  V8_INLINE int VisitJSArrayBuffer(Tagged<Map> map,
                                   Tagged<JSArrayBuffer> object);
  V8_INLINE int VisitJSApiObject(Tagged<Map> map, Tagged<JSObject> object);
  V8_INLINE void VisitExternalPointer(Tagged<HeapObject> host,
                                      ExternalPointerSlot slot);

  V8_INLINE static constexpr bool CanEncounterFillerOrFreeSpace() {
    return false;
  }

 private:
  template <typename TSlot>
  V8_INLINE void VisitHeapObjectImpl(TSlot slot,
                                     Tagged<HeapObject> heap_object);

  template <typename TSlot>
  V8_INLINE void VisitPointersImpl(Tagged<HeapObject> host, TSlot start,
                                   TSlot end);

  Scavenger* const scavenger_;
};

void ScavengeVisitor::VisitPointers(Tagged<HeapObject> host, ObjectSlot start,
                                    ObjectSlot end) {
  return VisitPointersImpl(host, start, end);
}

void ScavengeVisitor::VisitPointers(Tagged<HeapObject> host,
                                    MaybeObjectSlot start,
                                    MaybeObjectSlot end) {
  return VisitPointersImpl(host, start, end);
}

template <typename TSlot>
void ScavengeVisitor::VisitHeapObjectImpl(TSlot slot,
                                          Tagged<HeapObject> heap_object) {
  if (Heap::InYoungGeneration(heap_object)) {
    using THeapObjectSlot = typename TSlot::THeapObjectSlot;
    scavenger_->ScavengeObject(THeapObjectSlot(slot), heap_object);
  }
}

template <typename TSlot>
void ScavengeVisitor::VisitPointersImpl(Tagged<HeapObject> host, TSlot start,
                                        TSlot end) {
  for (TSlot slot = start; slot < end; ++slot) {
    const std::optional<Tagged<Object>> optional_object =
        this->GetObjectFilterReadOnlyAndSmiFast(slot);
    if (!optional_object) {
      continue;
    }
    typename TSlot::TObject object = *optional_object;
    Tagged<HeapObject> heap_object;
    // Treat weak references as strong.
    if (object.GetHeapObject(&heap_object)) {
      VisitHeapObjectImpl(slot, heap_object);
    }
  }
}

int ScavengeVisitor::VisitJSArrayBuffer(Tagged<Map> map,
                                        Tagged<JSArrayBuffer> object) {
  object->YoungMarkExtension();
  int size = JSArrayBuffer::BodyDescriptor::SizeOf(map, object);
  JSArrayBuffer::BodyDescriptor::IterateBody(map, object, size, this);
  return size;
}

int ScavengeVisitor::VisitJSApiObject(Tagged<Map> map,
                                      Tagged<JSObject> object) {
  int size = JSAPIObjectWithEmbedderSlots::BodyDescriptor::SizeOf(map, object);
  JSAPIObjectWithEmbedderSlots::BodyDescriptor::IterateBody(map, object, size,
                                                            this);
  return size;
}

void ScavengeVisitor::VisitExternalPointer(Tagged<HeapObject> host,
                                           ExternalPointerSlot slot) {
#ifdef V8_COMPRESS_POINTERS
  DCHECK_NE(slot.tag(), kExternalPointerNullTag);
  DCHECK(!IsSharedExternalPointerType(slot.tag()));
  DCHECK(Heap::InYoungGeneration(host));

  // If an incremental mark is in progress, there is already a whole-heap trace
  // running that will mark live EPT entries, and the scavenger won't sweep the
  // young EPT space.  So, leave the tracing and sweeping work to the impending
  // major GC.
  //
  // The EPT entry may or may not be marked already by the incremental marker.
  if (scavenger_->is_incremental_marking_) return;

  // TODO(chromium:337580006): Remove when pointer compression always uses
  // EPT.
  if (!slot.HasExternalPointerHandle()) return;

  ExternalPointerHandle handle = slot.Relaxed_LoadHandle();
  Heap* heap = scavenger_->heap();
  ExternalPointerTable& table = heap->isolate()->external_pointer_table();
  table.Mark(heap->young_external_pointer_space(), handle, slot.address());
#endif  // V8_COMPRESS_POINTERS
}

int ScavengeVisitor::VisitEphemeronHashTable(Tagged<Map> map,
                                             Tagged<EphemeronHashTable> table) {
  // Register table with the scavenger, so it can take care of the weak keys
  // later. This allows to only iterate the tables' values, which are treated
  // as strong independently of whether the key is live.
  scavenger_->AddEphemeronHashTable(table);
  for (InternalIndex i : table->IterateEntries()) {
    ObjectSlot value_slot =
        table->RawFieldOfElementAt(EphemeronHashTable::EntryToValueIndex(i));
    VisitPointer(table, value_slot);
  }

  return table->SizeFromMap(map);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SCAVENGER_INL_H_
