// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/heap-layout-inl.h"
#include "src/heap/marking-inl.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/objects-inl.h"

namespace v8::internal {

// TODO(333906585): Due to cyclic dependency, we cannot pull in marking-inl.h
// here. Fix it and make the call inlined.
//
// static
bool HeapLayout::InYoungGenerationForStickyMarkbits(const MemoryChunk* chunk,
                                                    Tagged<HeapObject> object) {
  CHECK(v8_flags.sticky_mark_bits.value());
  return !chunk->IsOnlyOldOrMajorMarkingOn() &&
         !MarkingBitmap::MarkBitFromAddress(object.address())
              .template Get<AccessMode::ATOMIC>();
}

// static
void HeapLayout::CheckYoungGenerationConsistency(const MemoryChunk* chunk) {
  // Young generation objects should only be found in to space when the GC is
  // not currently running.

  // If the object is in the young generation, then it is safe to get to the
  // containing Heap.
#ifdef DEBUG
  const MemoryChunkMetadata* metadata = chunk->Metadata();
  SLOW_DCHECK(metadata->IsWritable());
  Heap* heap = metadata->heap();
  SLOW_DCHECK(heap != nullptr);
  DCHECK_IMPLIES(heap->gc_state() == Heap::NOT_IN_GC,
                 chunk->IsFlagSet(MemoryChunk::TO_PAGE));
#endif  // DEBUG
}

// static
bool HeapLayout::IsSelfForwarded(Tagged<HeapObject> object) {
  return IsSelfForwarded(object, GetPtrComprCageBase(object));
}

// static
bool HeapLayout::IsSelfForwarded(Tagged<HeapObject> object,
                                 PtrComprCageBase cage_base) {
  return IsSelfForwarded(object, object->map_word(cage_base, kRelaxedLoad));
}

// static
bool HeapLayout::IsSelfForwarded(Tagged<HeapObject> object, MapWord map_word) {
  return map_word == MapWord::FromForwardingAddress(object, object);
}

}  // namespace v8::internal
