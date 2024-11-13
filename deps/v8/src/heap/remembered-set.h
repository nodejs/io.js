// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_REMEMBERED_SET_H_
#define V8_HEAP_REMEMBERED_SET_H_

#include <memory>

#include "src/base/bounds.h"
#include "src/base/memory.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/heap/base/worklist.h"
#include "src/heap/heap.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/slot-set.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

class RememberedSetOperations {
 public:
  // Given a page and a slot in that page, this function adds the slot to the
  // remembered set.
  template <AccessMode access_mode>
  static void Insert(SlotSet* slot_set, size_t slot_offset) {
    slot_set->Insert<access_mode == v8::internal::AccessMode::ATOMIC
                         ? v8::internal::SlotSet::AccessMode::ATOMIC
                         : v8::internal::SlotSet::AccessMode::NON_ATOMIC>(
        slot_offset);
  }

  template <AccessMode access_mode = AccessMode::ATOMIC, typename Callback>
  static int Iterate(SlotSet* slot_set, const MutablePageMetadata* chunk,
                     Callback callback, SlotSet::EmptyBucketMode mode) {
    int slots = 0;
    if (slot_set != nullptr) {
      slots += slot_set->Iterate<access_mode>(chunk->ChunkAddress(), 0,
                                              chunk->buckets(), callback, mode);
    }
    return slots;
  }

  static void Remove(SlotSet* slot_set, MutablePageMetadata* chunk,
                     Address slot_addr) {
    if (slot_set != nullptr) {
      uintptr_t offset = chunk->Offset(slot_addr);
      slot_set->Remove(offset);
    }
  }

  static void RemoveRange(SlotSet* slot_set, MutablePageMetadata* page,
                          Address start, Address end,
                          SlotSet::EmptyBucketMode mode) {
    if (slot_set != nullptr) {
      MemoryChunk* chunk = page->Chunk();
      uintptr_t start_offset = chunk->Offset(start);
      uintptr_t end_offset = chunk->OffsetMaybeOutOfRange(end);
      DCHECK_LE(start_offset, end_offset);
      slot_set->RemoveRange(static_cast<int>(start_offset),
                            static_cast<int>(end_offset), page->buckets(),
                            mode);
    }
  }

  static void CheckNoneInRange(SlotSet* slot_set, MemoryChunk* chunk,
                               Address start, Address end) {
    if (slot_set != nullptr) {
      size_t start_bucket = SlotSet::BucketForSlot(chunk->Offset(start));
      // Both 'end' and 'end_bucket' are exclusive limits, so do some index
      // juggling to make sure we get the right bucket even if the end address
      // is at the start of a bucket.
      size_t end_bucket = SlotSet::BucketForSlot(
                              chunk->OffsetMaybeOutOfRange(end) - kTaggedSize) +
                          1;
      slot_set->Iterate(
          chunk->address(), start_bucket, end_bucket,
          [start, end](MaybeObjectSlot slot) {
            CHECK(slot.address() < start || slot.address() >= end);
            return KEEP_SLOT;
          },
          SlotSet::KEEP_EMPTY_BUCKETS);
    }
  }
};

template <RememberedSetType type>
class RememberedSet : public AllStatic {
 public:
  // Given a page and a slot in that page, this function adds the slot to the
  // remembered set.
  template <AccessMode access_mode>
  static void Insert(MutablePageMetadata* page, size_t slot_offset) {
    SlotSet* slot_set = page->slot_set<type, access_mode>();
    if (slot_set == nullptr) {
      slot_set = page->AllocateSlotSet(type);
    }
    RememberedSetOperations::Insert<access_mode>(slot_set, slot_offset);
  }

  // Given a page and a slot set, this function merges the slot set to the set
  // of the page. |other_slot_set| should not be used after calling this method.
  static void MergeAndDelete(MutablePageMetadata* chunk,
                             SlotSet&& other_slot_set) {
    static_assert(type == RememberedSetType::OLD_TO_NEW ||
                  type == RememberedSetType::OLD_TO_NEW_BACKGROUND);
    SlotSet* slot_set = chunk->slot_set<type, AccessMode::NON_ATOMIC>();
    if (slot_set == nullptr) {
      chunk->set_slot_set<type, AccessMode::NON_ATOMIC>(&other_slot_set);
      return;
    }
    slot_set->Merge(&other_slot_set, chunk->buckets());
    SlotSet::Delete(&other_slot_set, chunk->buckets());
  }

  // Given a page and a slot set, this function merges the slot set to the set
  // of the page. |other_slot_set| should not be used after calling this method.
  static void MergeAndDeleteTyped(MutablePageMetadata* chunk,
                                  TypedSlotSet&& other_typed_slot_set) {
    static_assert(type == RememberedSetType::OLD_TO_NEW);
    TypedSlotSet* typed_slot_set =
        chunk->typed_slot_set<type, AccessMode::NON_ATOMIC>();
    if (typed_slot_set == nullptr) {
      chunk->set_typed_slot_set<RememberedSetType::OLD_TO_NEW,
                                AccessMode::NON_ATOMIC>(&other_typed_slot_set);
      return;
    }
    typed_slot_set->Merge(&other_typed_slot_set);
    delete &other_typed_slot_set;
  }

  static void DeleteTyped(TypedSlotSet&& other_typed_slot_set) {
    delete &other_typed_slot_set;
  }

  // Given a page and a slot in that page, this function returns true if
  // the remembered set contains the slot.
  static bool Contains(MutablePageMetadata* chunk, Address slot_addr) {
    DCHECK(chunk->Contains(slot_addr));
    SlotSet* slot_set = chunk->slot_set<type>();
    if (slot_set == nullptr) {
      return false;
    }
    uintptr_t offset = chunk->Offset(slot_addr);
    return slot_set->Contains(offset);
  }

  static void CheckNoneInRange(MutablePageMetadata* page, Address start,
                               Address end) {
    SlotSet* slot_set = page->slot_set<type>();
    RememberedSetOperations::CheckNoneInRange(slot_set, page->Chunk(), start,
                                              end);
  }

  // Given a page and a slot in that page, this function removes the slot from
  // the remembered set.
  // If the slot was never added, then the function does nothing.
  static void Remove(MutablePageMetadata* chunk, Address slot_addr) {
    DCHECK(chunk->Contains(slot_addr));
    SlotSet* slot_set = chunk->slot_set<type>();
    RememberedSetOperations::Remove(slot_set, chunk, slot_addr);
  }

  // Given a page and a range of slots in that page, this function removes the
  // slots from the remembered set.
  static void RemoveRange(MutablePageMetadata* chunk, Address start,
                          Address end, SlotSet::EmptyBucketMode mode) {
    SlotSet* slot_set = chunk->slot_set<type>();
    RememberedSetOperations::RemoveRange(slot_set, chunk, start, end, mode);
  }

  // Iterates over all memory chunks that contains non-empty slot sets.
  // The callback should take (MutablePageMetadata* chunk) and return void.
  template <typename Callback>
  static void IterateMemoryChunks(Heap* heap, Callback callback) {
    OldGenerationMemoryChunkIterator it(heap);
    MutablePageMetadata* chunk;
    while ((chunk = it.next()) != nullptr) {
      SlotSet* slot_set = chunk->slot_set<type>();
      TypedSlotSet* typed_slot_set = chunk->typed_slot_set<type>();
      if (slot_set != nullptr || typed_slot_set != nullptr) {
        callback(chunk);
      }
    }
  }

  // Iterates and filters the remembered set in the given memory chunk with
  // the given callback. The callback should take (Address slot) and return
  // SlotCallbackResult.
  //
  // Notice that |mode| can only be of FREE* or PREFREE* if there are no other
  // threads concurrently inserting slots.
  template <AccessMode access_mode = AccessMode::ATOMIC, typename Callback>
  static int Iterate(MutablePageMetadata* chunk, Callback callback,
                     SlotSet::EmptyBucketMode mode) {
    SlotSet* slot_set = chunk->slot_set<type>();
    return Iterate<access_mode>(slot_set, chunk, callback, mode);
  }

  template <AccessMode access_mode = AccessMode::ATOMIC, typename Callback>
  static int Iterate(SlotSet* slot_set, const MutablePageMetadata* chunk,
                     Callback callback, SlotSet::EmptyBucketMode mode) {
    return RememberedSetOperations::Iterate<access_mode>(slot_set, chunk,
                                                         callback, mode);
  }

  template <typename Callback>
  static int IterateAndTrackEmptyBuckets(
      MutablePageMetadata* chunk, Callback callback,
      ::heap::base::Worklist<MutablePageMetadata*, 64>::Local* empty_chunks) {
    SlotSet* slot_set = chunk->slot_set<type>();
    int slots = 0;
    if (slot_set != nullptr) {
      PossiblyEmptyBuckets* possibly_empty_buckets =
          chunk->possibly_empty_buckets();
      slots += slot_set->IterateAndTrackEmptyBuckets(chunk->ChunkAddress(), 0,
                                                     chunk->buckets(), callback,
                                                     possibly_empty_buckets);
      if (!possibly_empty_buckets->IsEmpty()) empty_chunks->Push(chunk);
    }
    return slots;
  }

  static bool CheckPossiblyEmptyBuckets(MutablePageMetadata* chunk) {
    DCHECK(type == OLD_TO_NEW || type == OLD_TO_NEW_BACKGROUND);
    SlotSet* slot_set = chunk->slot_set<type, AccessMode::NON_ATOMIC>();
    if (slot_set != nullptr &&
        slot_set->CheckPossiblyEmptyBuckets(chunk->buckets(),
                                            chunk->possibly_empty_buckets())) {
      chunk->ReleaseSlotSet(type);
      return true;
    }

    return false;
  }

  // Given a page and a typed slot in that page, this function adds the slot
  // to the remembered set.
  static void InsertTyped(MutablePageMetadata* memory_chunk, SlotType slot_type,
                          uint32_t offset) {
    TypedSlotSet* slot_set = memory_chunk->typed_slot_set<type>();
    if (slot_set == nullptr) {
      slot_set = memory_chunk->AllocateTypedSlotSet(type);
    }
    slot_set->Insert(slot_type, offset);
  }

  static void MergeTyped(MutablePageMetadata* page,
                         std::unique_ptr<TypedSlots> other) {
    TypedSlotSet* slot_set = page->typed_slot_set<type>();
    if (slot_set == nullptr) {
      slot_set = page->AllocateTypedSlotSet(type);
    }
    slot_set->Merge(other.get());
  }

  // Given a page and a range of typed slots in that page, this function removes
  // the slots from the remembered set.
  static void RemoveRangeTyped(MutablePageMetadata* page, Address start,
                               Address end) {
    TypedSlotSet* slot_set = page->typed_slot_set<type>();
    if (slot_set != nullptr) {
      slot_set->Iterate(
          [=](SlotType slot_type, Address slot_addr) {
            return start <= slot_addr && slot_addr < end ? REMOVE_SLOT
                                                         : KEEP_SLOT;
          },
          TypedSlotSet::FREE_EMPTY_CHUNKS);
    }
  }

  // Iterates and filters typed pointers in the given memory chunk with the
  // given callback. The callback should take (SlotType slot_type, Address addr)
  // and return SlotCallbackResult.
  template <typename Callback>
  static int IterateTyped(MutablePageMetadata* chunk, Callback callback) {
    TypedSlotSet* slot_set = chunk->typed_slot_set<type>();
    if (!slot_set) return 0;
    return IterateTyped(slot_set, callback);
  }

  template <typename Callback>
  static int IterateTyped(TypedSlotSet* slot_set, Callback callback) {
    DCHECK_NOT_NULL(slot_set);
    return slot_set->Iterate(callback, TypedSlotSet::KEEP_EMPTY_CHUNKS);
  }

  // Clear all old to old slots from the remembered set.
  static void ClearAll(Heap* heap) {
    static_assert(type == OLD_TO_OLD || type == OLD_TO_CODE);
    OldGenerationMemoryChunkIterator it(heap);
    MutablePageMetadata* chunk;
    while ((chunk = it.next()) != nullptr) {
      chunk->ReleaseSlotSet(OLD_TO_OLD);
      chunk->ReleaseSlotSet(OLD_TO_CODE);
      chunk->ReleaseTypedSlotSet(OLD_TO_OLD);
    }
  }
};

class UpdateTypedSlotHelper {
 public:
  // Updates a typed slot using an untyped slot callback where |addr| depending
  // on slot type represents either address for respective RelocInfo or address
  // of the uncompressed constant pool entry.
  // The callback accepts FullMaybeObjectSlot and returns SlotCallbackResult.
  template <typename Callback>
  static SlotCallbackResult UpdateTypedSlot(
      WritableJitAllocation& jit_allocation, Heap* heap, SlotType slot_type,
      Address addr, Callback callback);

  // Returns the HeapObject referenced by the given typed slot entry.
  inline static Tagged<HeapObject> GetTargetObject(Heap* heap,
                                                   SlotType slot_type,
                                                   Address addr);

 private:
  // Updates a code entry slot using an untyped slot callback.
  // The callback accepts FullMaybeObjectSlot and returns SlotCallbackResult.
  template <typename Callback>
  static SlotCallbackResult UpdateCodeEntry(Address entry_address,
                                            Callback callback) {
    Tagged<InstructionStream> code =
        InstructionStream::FromEntryAddress(entry_address);
    Tagged<InstructionStream> old_code = code;
    SlotCallbackResult result = callback(FullMaybeObjectSlot(&code));
    DCHECK(!HasWeakHeapObjectTag(code));
    if (code != old_code) {
      base::Memory<Address>(entry_address) = code->instruction_start();
    }
    return result;
  }

  // Updates a code target slot using an untyped slot callback.
  // The callback accepts FullMaybeObjectSlot and returns SlotCallbackResult.
  template <typename Callback>
  static SlotCallbackResult UpdateCodeTarget(WritableRelocInfo* rinfo,
                                             Callback callback) {
    DCHECK(RelocInfo::IsCodeTargetMode(rinfo->rmode()));
    Tagged<InstructionStream> old_target =
        InstructionStream::FromTargetAddress(rinfo->target_address());
    Tagged<InstructionStream> new_target = old_target;
    SlotCallbackResult result = callback(FullMaybeObjectSlot(&new_target));
    DCHECK(!HasWeakHeapObjectTag(new_target));
    if (new_target != old_target) {
      rinfo->set_target_address(
          Cast<InstructionStream>(new_target)->instruction_start());
    }
    return result;
  }

  // Updates an embedded pointer slot using an untyped slot callback.
  // The callback accepts FullMaybeObjectSlot and returns SlotCallbackResult.
  template <typename Callback>
  static SlotCallbackResult UpdateEmbeddedPointer(Heap* heap,
                                                  WritableRelocInfo* rinfo,
                                                  Callback callback) {
    DCHECK(RelocInfo::IsEmbeddedObjectMode(rinfo->rmode()));
    Tagged<HeapObject> old_target = rinfo->target_object(heap->isolate());
    Tagged<HeapObject> new_target = old_target;
    SlotCallbackResult result = callback(FullMaybeObjectSlot(&new_target));
    DCHECK(!HasWeakHeapObjectTag(new_target));
    if (new_target != old_target) {
      rinfo->set_target_object(Cast<HeapObject>(new_target));
    }
    return result;
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_REMEMBERED_SET_H_
