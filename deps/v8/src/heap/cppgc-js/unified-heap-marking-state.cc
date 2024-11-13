// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/unified-heap-marking-state.h"

#include "src/base/logging.h"
#include "src/heap/heap-inl.h"
#include "src/heap/mark-compact.h"

namespace v8 {
namespace internal {

UnifiedHeapMarkingState::UnifiedHeapMarkingState(
    Heap* heap, MarkingWorklists::Local* local_marking_worklist,
    cppgc::internal::CollectionType collection_type)
    : heap_(heap),
      marking_state_(heap_ ? heap_->marking_state() : nullptr),
      local_marking_worklist_(local_marking_worklist),
      mark_mode_(collection_type == cppgc::internal::CollectionType::kMinor
                     ? TracedHandles::MarkMode::kOnlyYoung
                     : TracedHandles::MarkMode::kAll) {
  DCHECK_IMPLIES(heap_, marking_state_);
}

void UnifiedHeapMarkingState::Update(
    MarkingWorklists::Local* local_marking_worklist) {
  local_marking_worklist_ = local_marking_worklist;
  DCHECK_NOT_NULL(heap_);
}

}  // namespace internal
}  // namespace v8
