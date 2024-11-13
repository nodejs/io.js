// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_INCREMENTAL_MARKING_INL_H_
#define V8_HEAP_INCREMENTAL_MARKING_INL_H_

#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/marking-state-inl.h"
#include "src/objects/descriptor-array.h"

namespace v8 {
namespace internal {

void IncrementalMarking::TransferColor(Tagged<HeapObject> from,
                                       Tagged<HeapObject> to) {
  DCHECK(marking_state()->IsUnmarked(to));
  DCHECK(!black_allocation());

  if (marking_state()->IsMarked(from)) {
    bool success = marking_state()->TryMark(to);
    DCHECK(success);
    USE(success);
    if (!IsDescriptorArray(to) ||
        (DescriptorArrayMarkingState::Marked::decode(
             Cast<DescriptorArray>(to)->raw_gc_state(kRelaxedLoad)) != 0)) {
      MutablePageMetadata::FromHeapObject(to)->IncrementLiveBytesAtomically(
          ALIGN_TO_ALLOCATION_ALIGNMENT(to->Size()));
    }
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_INCREMENTAL_MARKING_INL_H_
