// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PAGE_METADATA_INL_H_
#define V8_HEAP_PAGE_METADATA_INL_H_

#include "src/heap/memory-chunk-inl.h"
#include "src/heap/page-metadata.h"
#include "src/heap/paged-spaces.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

// static
PageMetadata* PageMetadata::FromAddress(Address addr) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return reinterpret_cast<PageMetadata*>(
      MemoryChunk::FromAddress(addr)->Metadata());
}

// static
PageMetadata* PageMetadata::FromHeapObject(Tagged<HeapObject> o) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return FromAddress(o.ptr());
}

// static
PageMetadata* PageMetadata::FromAllocationAreaAddress(Address address) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return PageMetadata::FromAddress(address - kTaggedSize);
}

template <typename Callback>
void PageMetadata::ForAllFreeListCategories(Callback callback) {
  for (int i = kFirstCategory; i < owner()->free_list()->number_of_categories();
       i++) {
    callback(categories_[i]);
  }
}

void PageMetadata::MarkEvacuationCandidate() {
  DCHECK(!Chunk()->IsFlagSet(MemoryChunk::NEVER_EVACUATE));
  DCHECK_NULL(slot_set<OLD_TO_OLD>());
  DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  Chunk()->SetFlagSlow(MemoryChunk::EVACUATION_CANDIDATE);
  reinterpret_cast<PagedSpace*>(owner())->free_list()->EvictFreeListItems(this);
}

void PageMetadata::ClearEvacuationCandidate() {
  MemoryChunk* chunk = Chunk();
  if (!chunk->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED)) {
    DCHECK_NULL(slot_set<OLD_TO_OLD>());
    DCHECK_NULL(typed_slot_set<OLD_TO_OLD>());
  }
  chunk->ClearFlagSlow(MemoryChunk::EVACUATION_CANDIDATE);
  InitializeFreeListCategories();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_PAGE_METADATA_INL_H_
