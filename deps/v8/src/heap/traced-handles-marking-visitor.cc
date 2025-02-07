// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/traced-handles-marking-visitor.h"

#include <algorithm>
#include <iterator>

#include "src/heap/marking-state-inl.h"
#include "src/heap/marking-worklist-inl.h"
#include "src/heap/marking.h"

namespace v8 {
namespace internal {

ConservativeTracedHandlesMarkingVisitor::
    ConservativeTracedHandlesMarkingVisitor(
        Heap& heap, MarkingWorklists::Local& local_marking_worklist,
        cppgc::internal::CollectionType collection_type)
    : heap_(heap),
      marking_state_(*heap_.marking_state()),
      local_marking_worklist_(local_marking_worklist),
      traced_node_bounds_(heap.isolate()->traced_handles()->GetNodeBounds()),
      mark_mode_(collection_type == cppgc::internal::CollectionType::kMinor
                     ? TracedHandles::MarkMode::kOnlyYoung
                     : TracedHandles::MarkMode::kAll) {}

void ConservativeTracedHandlesMarkingVisitor::VisitPointer(
    const void* address) {
  const auto upper_it = std::upper_bound(
      traced_node_bounds_.begin(), traced_node_bounds_.end(), address,
      [](const void* needle, const auto& pair) { return needle < pair.first; });
  // Also checks emptiness as begin() == end() on empty bounds.
  if (upper_it == traced_node_bounds_.begin()) return;

  const auto bounds = std::next(upper_it, -1);
  if (address < bounds->second) {
    auto object = TracedHandles::MarkConservatively(
        const_cast<Address*>(reinterpret_cast<const Address*>(address)),
        const_cast<Address*>(reinterpret_cast<const Address*>(bounds->first)),
        mark_mode_);
    if (!IsHeapObject(object)) {
      // The embedder is not aware of whether numbers are materialized as heap
      // objects are just passed around as Smis. This branch also filters out
      // intentionally passed `Smi::zero()` that indicate that there's no
      // object to mark.
      return;
    }
    Tagged<HeapObject> heap_object = Cast<HeapObject>(object);
    const auto target_worklist =
        MarkingHelper::ShouldMarkObject(&heap_, heap_object);
    if (target_worklist) {
      MarkingHelper::TryMarkAndPush(&heap_, &local_marking_worklist_,
                                    &marking_state_, target_worklist.value(),
                                    heap_object);
    }
  }
}

}  // namespace internal
}  // namespace v8
