// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_CASTING_INL_H_
#define V8_OBJECTS_CASTING_INL_H_

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-layout.h"
#include "src/heap/heap.h"
#include "src/objects/casting.h"
#include "src/objects/heap-object.h"

namespace v8::internal {

#ifdef DEBUG
template <typename T>
bool GCAwareObjectTypeCheck(Tagged<Object> object, const Heap* heap) {
  Tagged<HeapObject> heap_object = UncheckedCast<HeapObject>(object);
  // `heap_object` must be of type `T`. One of 4 cases can apply:
  // 1) A scavenger is ongoing and `object` is a promoted large object.
  if ((heap->gc_state() == Heap::SCAVENGE) &&
      MemoryChunk::FromHeapObject(heap_object)->IsLargePage()) {
    return true;
  }
  // 2) A conservative scavenge is ongoing and `object` was pinned by
  // conservative stack scanning.
  MapWord map_word = heap_object->map_word(
      PtrComprCageBase(heap->isolate()->cage_base()), kRelaxedLoad);
  if ((heap->gc_state() == Heap::SCAVENGE) &&
      HeapLayout::InYoungGeneration(heap_object) &&
      (v8_flags.scavenger_pinning_objects ||
       v8_flags.scavenger_precise_pinning_objects) &&
      map_word.IsForwardingAddress() &&
      HeapLayout::IsSelfForwarded(heap_object)) {
    return true;
  }
  // 3) A GC is ongoing, `object` was evacuated, and the new instance is a
  // `T`.
  if (heap->IsInGC() && map_word.IsForwardingAddress() &&
      Is<T>(map_word.ToForwardingAddress(heap_object))) {
    return true;
  }
  // 4) If none of the above special cases apply, `heap_object` must be a `T`.
  return Is<T>(object);
}
#endif  // DEBUG

}  // namespace v8::internal

#endif  // V8_OBJECTS_CASTING_INL_H_
