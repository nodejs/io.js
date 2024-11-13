// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_
#define V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_

#include <atomic>

#include "include/v8-traced-handle.h"
#include "src/base/logging.h"
#include "src/handles/traced-handles.h"
#include "src/heap/cppgc-js/unified-heap-marking-state.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-inl.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class BasicTracedReferenceExtractor final {
 public:
  static Address* GetObjectSlotForMarking(const TracedReferenceBase& ref) {
    return const_cast<Address*>(
        reinterpret_cast<const Address*>(ref.GetSlotThreadSafe()));
  }
};

void UnifiedHeapMarkingState::MarkAndPush(
    const TracedReferenceBase& reference) {
  // The following code will crash with null pointer derefs when finding a
  // non-empty `TracedReferenceBase` when `CppHeap` is in detached mode.
  Address* traced_handle_location =
      BasicTracedReferenceExtractor::GetObjectSlotForMarking(reference);
  // We cannot assume that the reference is non-null as we may get here by
  // tracing an ephemeron which doesn't have early bailouts, see
  // `cppgc::Visitor::TraceEphemeron()` for non-Member values.
  if (!traced_handle_location) {
    return;
  }
  Tagged<Object> object =
      TracedHandles::Mark(traced_handle_location, mark_mode_);
  if (!IsHeapObject(object)) {
    // The embedder is not aware of whether numbers are materialized as heap
    // objects are just passed around as Smis.
    return;
  }
  Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
  const auto worklist_target =
      MarkingHelper::ShouldMarkObject(heap_, heap_object);
  if (worklist_target) {
    MarkingHelper::TryMarkAndPush(heap_, local_marking_worklist_,
                                  marking_state_, worklist_target.value(),
                                  heap_object);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_INL_H_
