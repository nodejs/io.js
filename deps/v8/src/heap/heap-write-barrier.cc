// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-write-barrier.h"

#include "src/heap/heap-write-barrier-inl.h"
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

namespace v8 {
namespace internal {

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
void WriteBarrier::MarkingSlowFromGlobalHandle(Tagged<HeapObject> value) {
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

void WriteBarrier::SharedSlow(Tagged<InstructionStream> host,
                              RelocInfo* reloc_info, Tagged<HeapObject> value) {
  MarkCompactCollector::RecordRelocSlotInfo info =
      MarkCompactCollector::ProcessRelocInfo(host, reloc_info, value);

  base::MutexGuard write_scope(info.page_metadata->mutex());
  RememberedSet<OLD_TO_SHARED>::InsertTyped(info.page_metadata, info.slot_type,
                                            info.offset);
}

void WriteBarrier::Shared(Tagged<TrustedObject> host, ProtectedPointerSlot slot,
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
  GetProcessWideJSDispatchTable()->Mark(handle);

  // TODO(saelo): in the future, we'll probably need to mark the Code object in
  // the table as well here.
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

  WriteBarrier::Marking(host, slot, Tagged<MaybeObject>(value));
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

  WriteBarrier::Marking(host, slot);
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

  WriteBarrier::Marking(host, slot, Tagged<MaybeObject>(value));

  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

int WriteBarrier::SharedFromCode(Address raw_host, Address raw_slot) {
  Tagged<HeapObject> host = Cast<HeapObject>(Tagged<Object>(raw_host));

  if (!InWritableSharedSpace(host)) {
    Heap::SharedHeapBarrierSlow(host, raw_slot);
  }

  // Called by WriteBarrierCodeStubAssembler, which doesn't accept void type
  return 0;
}

#ifdef ENABLE_SLOW_DCHECKS
bool WriteBarrier::IsImmortalImmovableHeapObject(Tagged<HeapObject> object) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  // All objects in readonly space are immortal and immovable.
  return chunk->InReadOnlySpace();
}
#endif

}  // namespace internal
}  // namespace v8
