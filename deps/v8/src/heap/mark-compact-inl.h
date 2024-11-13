// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARK_COMPACT_INL_H_
#define V8_HEAP_MARK_COMPACT_INL_H_

#include "src/common/globals.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-visitor-inl.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/marking-worklist.h"
#include "src/heap/marking.h"
#include "src/heap/objects-visiting-inl.h"
#include "src/heap/remembered-set-inl.h"
#include "src/objects/js-collection-inl.h"
#include "src/objects/transitions.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

void MarkCompactCollector::MarkObject(
    Tagged<HeapObject> host, Tagged<HeapObject> obj,
    MarkingHelper::WorklistTarget target_worklist) {
  DCHECK(ReadOnlyHeap::Contains(obj) || heap_->Contains(obj));
  MarkingHelper::TryMarkAndPush(heap_, local_marking_worklists_.get(),
                                marking_state_, target_worklist, obj);
}

void MarkCompactCollector::MarkRootObject(
    Root root, Tagged<HeapObject> obj,
    MarkingHelper::WorklistTarget target_worklist) {
  DCHECK(ReadOnlyHeap::Contains(obj) || heap_->Contains(obj));
  MarkingHelper::TryMarkAndPush(heap_, local_marking_worklists_.get(),
                                marking_state_, target_worklist, obj);
}

// static
template <typename THeapObjectSlot>
void MarkCompactCollector::RecordSlot(Tagged<HeapObject> object,
                                      THeapObjectSlot slot,
                                      Tagged<HeapObject> target) {
  MemoryChunk* source_page = MemoryChunk::FromHeapObject(object);
  if (!source_page->ShouldSkipEvacuationSlotRecording()) {
    RecordSlot(source_page, slot, target);
  }
}

// static
template <typename THeapObjectSlot>
void MarkCompactCollector::RecordSlot(MemoryChunk* source_chunk,
                                      THeapObjectSlot slot,
                                      Tagged<HeapObject> target) {
  MemoryChunk* target_chunk = MemoryChunk::FromHeapObject(target);
  if (target_chunk->IsEvacuationCandidate()) {
    MutablePageMetadata* source_page =
        MutablePageMetadata::cast(source_chunk->Metadata());
    if (target_chunk->IsFlagSet(MemoryChunk::IS_EXECUTABLE)) {
      RememberedSet<OLD_TO_CODE>::Insert<AccessMode::ATOMIC>(
          source_page, source_chunk->Offset(slot.address()));
    } else if (source_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED) &&
               target_chunk->IsFlagSet(MemoryChunk::IS_TRUSTED)) {
      RememberedSet<TRUSTED_TO_TRUSTED>::Insert<AccessMode::ATOMIC>(
          source_page, source_chunk->Offset(slot.address()));
    } else if (V8_LIKELY(!target_chunk->InWritableSharedSpace()) ||
               source_page->heap()->isolate()->is_shared_space_isolate()) {
      DCHECK_EQ(source_page->heap(), target_chunk->GetHeap());
      RememberedSet<OLD_TO_OLD>::Insert<AccessMode::ATOMIC>(
          source_page, source_chunk->Offset(slot.address()));
    } else {
      // DCHECK here that we only don't record in case of local->shared
      // references in a client GC.
      DCHECK(!source_page->heap()->isolate()->is_shared_space_isolate());
      DCHECK(target_chunk->GetHeap()->isolate()->is_shared_space_isolate());
      DCHECK(target_chunk->InWritableSharedSpace());
    }
  }
}

void MarkCompactCollector::AddTransitionArray(Tagged<TransitionArray> array) {
  local_weak_objects()->transition_arrays_local.Push(array);
}

void RootMarkingVisitor::VisitRootPointer(Root root, const char* description,
                                          FullObjectSlot p) {
  DCHECK(!MapWord::IsPacked(p.Relaxed_Load().ptr()));
  MarkObjectByPointer(root, p);
}

void RootMarkingVisitor::VisitRootPointers(Root root, const char* description,
                                           FullObjectSlot start,
                                           FullObjectSlot end) {
  for (FullObjectSlot p = start; p < end; ++p) {
    MarkObjectByPointer(root, p);
  }
}

void RootMarkingVisitor::MarkObjectByPointer(Root root, FullObjectSlot p) {
  Tagged<Object> object = *p;
#ifdef V8_ENABLE_DIRECT_HANDLE
  if (object.ptr() == kTaggedNullAddress) return;
#endif
  if (!IsHeapObject(object)) return;
  Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
  const auto target_worklist =
      MarkingHelper::ShouldMarkObject(collector_->heap(), heap_object);
  if (!target_worklist) {
    return;
  }
  collector_->MarkRootObject(root, heap_object, target_worklist.value());
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARK_COMPACT_INL_H_
