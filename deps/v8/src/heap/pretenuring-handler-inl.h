// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PRETENURING_HANDLER_INL_H_
#define V8_HEAP_PRETENURING_HANDLER_INL_H_

#include "src/base/sanitizer/msan.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/new-spaces.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/spaces.h"
#include "src/objects/allocation-site-inl.h"
#include "src/objects/allocation-site.h"

namespace v8 {
namespace internal {

void PretenuringHandler::UpdateAllocationSite(
    Tagged<Map> map, Tagged<HeapObject> object,
    PretenuringFeedbackMap* pretenuring_feedback) {
  DCHECK_NE(pretenuring_feedback, &global_pretenuring_feedback_);
#ifdef DEBUG
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  // MemoryChunk::IsToPage() is not available with sticky mark-bits.
  DCHECK_IMPLIES(v8_flags.sticky_mark_bits || chunk->IsToPage(),
                 v8_flags.minor_ms);
  DCHECK_IMPLIES(!v8_flags.minor_ms && !Heap::InYoungGeneration(object),
                 chunk->IsFlagSet(MemoryChunk::PAGE_NEW_OLD_PROMOTION));
#endif
  if (!v8_flags.allocation_site_pretenuring ||
      !AllocationSite::CanTrack(map->instance_type())) {
    return;
  }
  Tagged<AllocationMemento> memento_candidate =
      FindAllocationMemento<kForGC>(map, object);
  if (memento_candidate.is_null()) return;
  DCHECK(IsJSObjectMap(map));

  // Entering cached feedback is used in the parallel case. We are not allowed
  // to dereference the allocation site and rather have to postpone all checks
  // till actually merging the data.
  Address key = memento_candidate->GetAllocationSiteUnchecked();
  (*pretenuring_feedback)[UncheckedCast<AllocationSite>(Tagged<Object>(key))]++;
}

template <PretenuringHandler::FindMementoMode mode>
Tagged<AllocationMemento> PretenuringHandler::FindAllocationMemento(
    Tagged<Map> map, Tagged<HeapObject> object) {
  Address object_address = object.address();
  Address memento_address =
      object_address + ALIGN_TO_ALLOCATION_ALIGNMENT(object->SizeFromMap(map));
  Address last_memento_word_address = memento_address + kTaggedSize;
  // If the memento would be on another page, bail out immediately.
  if (!PageMetadata::OnSamePage(object_address, last_memento_word_address)) {
    return AllocationMemento();
  }

  MemoryChunk* object_chunk = MemoryChunk::FromAddress(object_address);
  PageMetadata* object_page = PageMetadata::cast(object_chunk->Metadata());
  // If the page is being swept, treat it as if the memento was already swept
  // and bail out.
  if (mode != FindMementoMode::kForGC && !object_page->SweepingDone())
    return AllocationMemento();

  Tagged<HeapObject> candidate = HeapObject::FromAddress(memento_address);
  ObjectSlot candidate_map_slot = candidate->map_slot();
  // This fast check may peek at an uninitialized word. However, the slow check
  // below (memento_address == top) ensures that this is safe. Mark the word as
  // initialized to silence MemorySanitizer warnings.
  MSAN_MEMORY_IS_INITIALIZED(candidate_map_slot.address(), kTaggedSize);
  if (!candidate_map_slot.Relaxed_ContainsMapValue(
          ReadOnlyRoots(heap_).allocation_memento_map().ptr())) {
    return AllocationMemento();
  }

  // Bail out if the memento is below the age mark, which can happen when
  // mementos survived because a page got moved within new space.
  if (object_chunk->IsFlagSet(MemoryChunk::NEW_SPACE_BELOW_AGE_MARK)) {
    Address age_mark =
        reinterpret_cast<SemiSpace*>(object_page->owner())->age_mark();
    if (!object_page->Contains(age_mark)) {
      return AllocationMemento();
    }
    // Do an exact check in the case where the age mark is on the same page.
    if (object_address < age_mark) {
      return AllocationMemento();
    }
  }

  Tagged<AllocationMemento> memento_candidate =
      Cast<AllocationMemento>(candidate);

  // Depending on what the memento is used for, we might need to perform
  // additional checks.
  Address top;
  switch (mode) {
    case kForGC:
      return memento_candidate;
    case kForRuntime:
      if (memento_candidate.is_null()) return AllocationMemento();
      // Either the object is the last object in the new space, or there is
      // another object of at least word size (the header map word) following
      // it, so suffices to compare ptr and top here.
      top = heap_->NewSpaceTop();
      DCHECK(memento_address >= heap_->NewSpaceLimit() ||
             memento_address +
                     ALIGN_TO_ALLOCATION_ALIGNMENT(AllocationMemento::kSize) <=
                 top);
      if ((memento_address != top) && memento_candidate->IsValid()) {
        return memento_candidate;
      }
      return AllocationMemento();
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PRETENURING_HANDLER_INL_H_
