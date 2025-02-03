// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-write-barrier.h"

#include "src/heap/heap-write-barrier-inl.h"
#include "src/heap/heap.h"
#include "src/heap/marking-barrier-inl.h"
#include "src/heap/memory-chunk-layout.h"
#include "src/heap/memory-chunk.h"
#include "src/heap/remembered-set.h"
#include "src/objects/code-inl.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/js-objects.h"
#include "src/objects/maybe-object.h"
#include "src/objects/slots-inl.h"
#include "src/sandbox/js-dispatch-table-inl.h"

namespace v8::internal {
namespace {
thread_local MarkingBarrier* current_marking_barrier = nullptr;
}  // namespace

bool HeapObjectInYoungGenerationSticky(MemoryChunk* chunk,
                                       Tagged<HeapObject> object) {
  DCHECK(v8_flags.sticky_mark_bits);
  return !chunk->IsOnlyOldOrMajorMarkingOn() &&
         !MarkingBitmap::MarkBitFromAddress(object.address())
              .template Get<AccessMode::ATOMIC>();
}

MarkingBarrier* WriteBarrier::CurrentMarkingBarrier(
    Tagged<HeapObject> verification_candidate) {
  MarkingBarrier* marking_barrier = current_marking_barrier;
  DCHECK_NOT_NULL(marking_barrier);
#if DEBUG
  if (!verification_candidate.is_null() &&
      !InAnySharedSpace(verification_candidate)) {
    Heap* host_heap =
        MutablePageMetadata::FromHeapObject(verification_candidate)->heap();
    LocalHeap* local_heap = LocalHeap::Current();
    if (!local_heap) local_heap = host_heap->main_thread_local_heap();
    DCHECK_EQ(marking_barrier, local_heap->marking_barrier());
  }
#endif  // DEBUG
  return marking_barrier;
}

MarkingBarrier* WriteBarrier::SetForThread(MarkingBarrier* marking_barrier) {
  MarkingBarrier* existing = current_marking_barrier;
  current_marking_barrier = marking_barrier;
  return existing;
}

void WriteBarrier::MarkingSlow(Tagged<HeapObject> host, HeapObjectSlot slot,
                               Tagged<HeapObject> value) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);
  marking_barrier->Write(host, slot, value);
}

// static
void WriteBarrier::MarkingSlowFromTracedHandle(Tagged<HeapObject> value) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(value);
  marking_barrier->WriteWithoutHost(value);
}

// static
void WriteBarrier::MarkingSlowFromCppHeapWrappable(Heap* heap, void* object) {
  if (auto* cpp_heap = heap->cpp_heap()) {
    CppHeap::From(cpp_heap)->WriteBarrier(object);
  }
}

void WriteBarrier::MarkingSlow(Tagged<InstructionStream> host,
                               RelocInfo* reloc_info,
                               Tagged<HeapObject> value) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);
  marking_barrier->Write(host, reloc_info, value);
}

// static
void WriteBarrier::SharedSlow(Tagged<InstructionStream> host,
                              RelocInfo* reloc_info, Tagged<HeapObject> value) {
  MarkCompactCollector::RecordRelocSlotInfo info =
      MarkCompactCollector::ProcessRelocInfo(host, reloc_info, value);

  base::MutexGuard write_scope(info.page_metadata->mutex());
  RememberedSet<OLD_TO_SHARED>::InsertTyped(info.page_metadata, info.slot_type,
                                            info.offset);
}

// static
void WriteBarrier::SharedHeapBarrierSlow(Tagged<HeapObject> object,
                                         Address slot) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  DCHECK(!chunk->InWritableSharedSpace());
  RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
      MutablePageMetadata::cast(chunk->Metadata()), chunk->Offset(slot));
}

// static
void WriteBarrier::SharedSlow(Tagged<TrustedObject> host,
                              ProtectedPointerSlot slot,
                              Tagged<TrustedObject> value) {
  DCHECK(MemoryChunk::FromHeapObject(value)->InWritableSharedSpace());
  if (!MemoryChunk::FromHeapObject(host)->InWritableSharedSpace()) {
    MutablePageMetadata* host_chunk_metadata =
        MutablePageMetadata::FromHeapObject(host);
    RememberedSet<TRUSTED_TO_SHARED_TRUSTED>::Insert<AccessMode::NON_ATOMIC>(
        host_chunk_metadata, host_chunk_metadata->Offset(slot.address()));
  }
}

void WriteBarrier::MarkingSlow(Tagged<JSArrayBuffer> host,
                               ArrayBufferExtension* extension) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);
  marking_barrier->Write(host, extension);
}

void WriteBarrier::MarkingSlow(Tagged<DescriptorArray> descriptor_array,
                               int number_of_own_descriptors) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(descriptor_array);
  marking_barrier->Write(descriptor_array, number_of_own_descriptors);
}

void WriteBarrier::MarkingSlow(Tagged<HeapObject> host,
                               IndirectPointerSlot slot) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);
  marking_barrier->Write(host, slot);
}

void WriteBarrier::MarkingSlow(Tagged<TrustedObject> host,
                               ProtectedPointerSlot slot,
                               Tagged<TrustedObject> value) {
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);
  marking_barrier->Write(host, slot, value);
}

void WriteBarrier::MarkingSlow(Tagged<HeapObject> host,
                               JSDispatchHandle handle) {
#ifdef V8_ENABLE_LEAPTIERING
  MarkingBarrier* marking_barrier = CurrentMarkingBarrier(host);

  // The JSDispatchTable is only marked during major GC so we can skip the
  // barrier if we're only doing a minor GC.
  // This is mostly an optimization, but it does help avoid scenarios where a
  // minor GC marking barrier marks a table entry as alive but not the Code
  // object contained in it (because it's not a young-gen object).
  if (marking_barrier->is_minor()) return;

  // Mark both the table entry and its content.
  JSDispatchTable* jdt = GetProcessWideJSDispatchTable();
  jdt->Mark(handle);
  marking_barrier->MarkValue(host, jdt->GetCode(handle));

  // We don't need to record a slot here because the entries in the
  // JSDispatchTable are not compacted and because the pointers stored in the
  // table entries are updated after compacting GC.
  static_assert(!JSDispatchTable::kSupportsCompaction);
#else
  UNREACHABLE();
#endif
}

int WriteBarrier::MarkingFromCode(Address raw_host, Address raw_slot) {
  Tagged<HeapObject> host = Cast<HeapObject>(Tagged<Object>(raw_host));
  MaybeObjectSlot slot(raw_slot);
  Address value = (*slot).ptr();

#ifdef V8_MAP_PACKING
  if (slot.address() == host.address()) {
    // Clear metadata bits and fix object tag.
    value = (value & ~Internals::kMapWordMetadataMask &
             ~Internals::kMapWordXorMask) |
            (uint64_t)kHeapObjectTag;
  }
#endif

#if DEBUG
  Heap* heap = MutablePageMetadata::FromHeapObject(host)->heap();
  DCHECK(heap->incremental_marking()->IsMarking());

  // We will only reach local objects here while incremental marking in the
  // current isolate is enabled. However, we might still reach objects in the
  // shared space but only from the shared space isolate (= the main isolate).
  MarkingBarrier* barrier = CurrentMarkingBarrier(host);
  DCHECK_IMPLIES(InWritableSharedSpace(host),
                 barrier->heap()->isolate()->is_shared_space_isolate());
  barrier->AssertMarkingIsActivated();
#endif  // DEBUG

  Marking(host, slot, Tagged<MaybeObject>(value));
  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

int WriteBarrier::IndirectPointerMarkingFromCode(Address raw_host,
                                                 Address raw_slot,
                                                 Address raw_tag) {
  Tagged<HeapObject> host = Cast<HeapObject>(Tagged<Object>(raw_host));
  IndirectPointerTag tag = static_cast<IndirectPointerTag>(raw_tag);
  DCHECK(IsValidIndirectPointerTag(tag));
  IndirectPointerSlot slot(raw_slot, tag);

#if DEBUG
  DCHECK(!InWritableSharedSpace(host));
  MarkingBarrier* barrier = CurrentMarkingBarrier(host);
  DCHECK(barrier->heap()->isolate()->isolate_data()->is_marking());

  DCHECK(IsExposedTrustedObject(slot.load(barrier->heap()->isolate())));
#endif

  Marking(host, slot);
  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

int WriteBarrier::SharedMarkingFromCode(Address raw_host, Address raw_slot) {
  Tagged<HeapObject> host = Cast<HeapObject>(Tagged<Object>(raw_host));
  MaybeObjectSlot slot(raw_slot);
  Address raw_value = (*slot).ptr();
  Tagged<MaybeObject> value(raw_value);

  DCHECK(InWritableSharedSpace(host));

#if DEBUG
  Heap* heap = MutablePageMetadata::FromHeapObject(host)->heap();
  DCHECK(heap->incremental_marking()->IsMajorMarking());
  Isolate* isolate = heap->isolate();
  DCHECK(isolate->is_shared_space_isolate());

  // The shared marking barrier will only be reached from client isolates (=
  // worker isolates).
  MarkingBarrier* barrier = CurrentMarkingBarrier(host);
  DCHECK(!barrier->heap()->isolate()->is_shared_space_isolate());
  barrier->AssertSharedMarkingIsActivated();
#endif  // DEBUG

  Marking(host, slot, Tagged<MaybeObject>(value));

  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

int WriteBarrier::SharedFromCode(Address raw_host, Address raw_slot) {
  Tagged<HeapObject> host = Cast<HeapObject>(Tagged<Object>(raw_host));

  if (!InWritableSharedSpace(host)) {
    SharedHeapBarrierSlow(host, raw_slot);
  }

  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

// static
bool WriteBarrier::PageFlagsAreConsistent(Tagged<HeapObject> object) {
  MemoryChunkMetadata* metadata = MemoryChunkMetadata::FromHeapObject(object);
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);

  // Slim chunk flags consistency.
  CHECK_EQ(chunk->IsFlagSet(MemoryChunk::INCREMENTAL_MARKING),
           chunk->IsMarking());

  if (!v8_flags.sticky_mark_bits) {
    AllocationSpace identity = metadata->owner()->identity();

    // Generation consistency.
    CHECK_EQ(identity == NEW_SPACE || identity == NEW_LO_SPACE,
             chunk->InYoungGeneration());
  }

  // Marking consistency.
  if (metadata->IsWritable()) {
    // RO_SPACE can be shared between heaps, so we can't use RO_SPACE objects to
    // find a heap. The exception is when the ReadOnlySpace is writeable, during
    // bootstrapping, so explicitly allow this case.
    Heap* heap = Heap::FromWritableHeapObject(object);
    if (chunk->InWritableSharedSpace()) {
      // The marking bit is not set for chunks in shared spaces during MinorMS
      // concurrent marking.
      CHECK_EQ(chunk->IsMarking(),
               heap->incremental_marking()->IsMajorMarking());
    } else {
      CHECK_EQ(chunk->IsMarking(), heap->incremental_marking()->IsMarking());
    }
  } else {
    // Non-writable RO_SPACE must never have marking flag set.
    CHECK(!chunk->IsMarking());
  }
  return true;
}

// static
void WriteBarrier::GenerationalBarrierForCodeSlow(
    Tagged<InstructionStream> host, RelocInfo* rinfo,
    Tagged<HeapObject> value) {
  DCHECK(Heap::InYoungGeneration(value));
  const MarkCompactCollector::RecordRelocSlotInfo info =
      MarkCompactCollector::ProcessRelocInfo(host, rinfo, value);

  base::MutexGuard write_scope(info.page_metadata->mutex());
  RememberedSet<OLD_TO_NEW>::InsertTyped(info.page_metadata, info.slot_type,
                                         info.offset);
}

// static
void WriteBarrier::CombinedGenerationalAndSharedEphemeronBarrierSlow(
    Tagged<EphemeronHashTable> table, Address slot, Tagged<HeapObject> value) {
  if (HeapObjectInYoungGeneration(value)) {
    MutablePageMetadata* table_chunk =
        MutablePageMetadata::FromHeapObject(table);
    table_chunk->heap()->ephemeron_remembered_set()->RecordEphemeronKeyWrite(
        table, slot);
  } else {
    DCHECK(MemoryChunk::FromHeapObject(value)->InWritableSharedSpace());
    DCHECK(!InWritableSharedSpace(table));
    SharedHeapBarrierSlow(table, slot);
  }
}

// static
void WriteBarrier::CombinedGenerationalAndSharedBarrierSlow(
    Tagged<HeapObject> object, Address slot, Tagged<HeapObject> value) {
  if (HeapObjectInYoungGeneration(value)) {
    GenerationalBarrierSlow(object, slot, value);

  } else {
    DCHECK(MemoryChunk::FromHeapObject(value)->InWritableSharedSpace());
    DCHECK(!InWritableSharedSpace(object));
    SharedHeapBarrierSlow(object, slot);
  }
}

//  static
void WriteBarrier::GenerationalBarrierSlow(Tagged<HeapObject> object,
                                           Address slot,
                                           Tagged<HeapObject> value) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  MutablePageMetadata* metadata = MutablePageMetadata::cast(chunk->Metadata());
  if (LocalHeap::Current() == nullptr) {
    RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(
        metadata, chunk->Offset(slot));
  } else {
    RememberedSet<OLD_TO_NEW_BACKGROUND>::Insert<AccessMode::ATOMIC>(
        metadata, chunk->Offset(slot));
  }
}

// static
void WriteBarrier::EphemeronKeyWriteBarrierFromCode(Address raw_object,
                                                    Address key_slot_address,
                                                    Isolate* isolate) {
  Tagged<EphemeronHashTable> table =
      Cast<EphemeronHashTable>(Tagged<Object>(raw_object));
  ObjectSlot key_slot(key_slot_address);
  ForEphemeronHashTable(table, key_slot, *key_slot, UPDATE_WRITE_BARRIER);
}

namespace {

enum RangeWriteBarrierMode {
  kDoGenerationalOrShared = 1 << 0,
  kDoMarking = 1 << 1,
  kDoEvacuationSlotRecording = 1 << 2,
};

template <int kModeMask, typename TSlot>
void ForRangeImpl(Heap* heap, MemoryChunk* source_chunk,
                  Tagged<HeapObject> object, TSlot start_slot, TSlot end_slot) {
  // At least one of generational or marking write barrier should be requested.
  static_assert(kModeMask & (kDoGenerationalOrShared | kDoMarking));
  // kDoEvacuationSlotRecording implies kDoMarking.
  static_assert(!(kModeMask & kDoEvacuationSlotRecording) ||
                (kModeMask & kDoMarking));

  MarkingBarrier* marking_barrier = nullptr;
  static constexpr Tagged_t kPageMask =
      ~static_cast<Tagged_t>(PageMetadata::kPageSize - 1);
  Tagged_t cached_uninteresting_page =
      static_cast<Tagged_t>(heap->read_only_space()->FirstPageAddress()) &
      kPageMask;

  if (kModeMask & kDoMarking) {
    marking_barrier = WriteBarrier::CurrentMarkingBarrier(object);
  }

  MarkCompactCollector* collector = heap->mark_compact_collector();
  MutablePageMetadata* source_page_metadata =
      MutablePageMetadata::cast(source_chunk->Metadata());

  for (TSlot slot = start_slot; slot < end_slot; ++slot) {
    // If we *only* need the generational or shared WB, we can skip objects
    // residing on uninteresting pages.
    Tagged_t compressed_page;
    if (kModeMask == kDoGenerationalOrShared) {
      Tagged_t tagged_value = *slot.location();
      if (HAS_SMI_TAG(tagged_value)) continue;
      compressed_page = tagged_value & kPageMask;
      if (compressed_page == cached_uninteresting_page) {
#if DEBUG
        typename TSlot::TObject value = *slot;
        Tagged<HeapObject> value_heap_object;
        if (value.GetHeapObject(&value_heap_object)) {
          CHECK(!Heap::InYoungGeneration(value_heap_object));
          CHECK(!InWritableSharedSpace(value_heap_object));
        }
#endif  // DEBUG
        continue;
      }
      // Fall through to decompressing the pointer and fetching its actual
      // page header flags.
    }
    typename TSlot::TObject value = *slot;
    Tagged<HeapObject> value_heap_object;
    if (!value.GetHeapObject(&value_heap_object)) continue;

    if (kModeMask & kDoGenerationalOrShared) {
      if (Heap::InYoungGeneration(value_heap_object)) {
        RememberedSet<OLD_TO_NEW>::Insert<AccessMode::NON_ATOMIC>(
            source_page_metadata, source_chunk->Offset(slot.address()));
      } else if (InWritableSharedSpace(value_heap_object)) {
        RememberedSet<OLD_TO_SHARED>::Insert<AccessMode::ATOMIC>(
            source_page_metadata, source_chunk->Offset(slot.address()));
      } else if (kModeMask == kDoGenerationalOrShared) {
        cached_uninteresting_page = compressed_page;
      }
    }

    if (kModeMask & kDoMarking) {
      marking_barrier->MarkValue(object, value_heap_object);
      if (kModeMask & kDoEvacuationSlotRecording) {
        collector->RecordSlot(source_chunk, HeapObjectSlot(slot),
                              value_heap_object);
      }
    }
  }
}

}  // namespace

// Instantiate `WriteBarrier::WriteBarrierForRange()` for `ObjectSlot` and
// `MaybeObjectSlot`.
template void WriteBarrier::ForRange<ObjectSlot>(Heap* heap,
                                                 Tagged<HeapObject> object,
                                                 ObjectSlot start_slot,
                                                 ObjectSlot end_slot);
template void WriteBarrier::ForRange<MaybeObjectSlot>(
    Heap* heap, Tagged<HeapObject> object, MaybeObjectSlot start_slot,
    MaybeObjectSlot end_slot);

template <typename TSlot>
// static
void WriteBarrier::ForRange(Heap* heap, Tagged<HeapObject> object,
                            TSlot start_slot, TSlot end_slot) {
  if (v8_flags.disable_write_barriers) return;
  MemoryChunk* source_chunk = MemoryChunk::FromHeapObject(object);
  base::Flags<RangeWriteBarrierMode> mode;

  if (!HeapObjectInYoungGeneration(object) &&
      !source_chunk->InWritableSharedSpace()) {
    mode |= kDoGenerationalOrShared;
  }

  if (heap->incremental_marking()->IsMarking()) {
    mode |= kDoMarking;
    if (!source_chunk->ShouldSkipEvacuationSlotRecording()) {
      mode |= kDoEvacuationSlotRecording;
    }
  }

  switch (mode) {
    // Nothing to be done.
    case 0:
      return;
    // Generational only.
    case kDoGenerationalOrShared:
      return ForRangeImpl<kDoGenerationalOrShared>(heap, source_chunk, object,
                                                   start_slot, end_slot);
    // Marking, no evacuation slot recording.
    case kDoMarking:
      return ForRangeImpl<kDoMarking>(heap, source_chunk, object, start_slot,
                                      end_slot);
    // Marking with evacuation slot recording.
    case kDoMarking | kDoEvacuationSlotRecording:
      return ForRangeImpl<kDoMarking | kDoEvacuationSlotRecording>(
          heap, source_chunk, object, start_slot, end_slot);
    // Generational and marking, no evacuation slot recording.
    case kDoGenerationalOrShared | kDoMarking:
      return ForRangeImpl<kDoGenerationalOrShared | kDoMarking>(
          heap, source_chunk, object, start_slot, end_slot);
    // Generational and marking with evacuation slot recording.
    case kDoGenerationalOrShared | kDoMarking | kDoEvacuationSlotRecording:
      return ForRangeImpl<kDoGenerationalOrShared | kDoMarking |
                          kDoEvacuationSlotRecording>(
          heap, source_chunk, object, start_slot, end_slot);
    default:
      UNREACHABLE();
  }
}

}  // namespace v8::internal
