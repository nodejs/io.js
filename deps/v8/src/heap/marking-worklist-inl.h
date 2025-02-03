// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_HEAP_MARKING_WORKLIST_INL_H_
#define V8_HEAP_MARKING_WORKLIST_INL_H_

#include <unordered_map>

#include "src/heap/cppgc-js/cpp-marking-state-inl.h"
#include "src/heap/marking-worklist.h"
#include "src/objects/embedder-data-slot.h"
#include "src/objects/js-objects-inl.h"

namespace v8 {
namespace internal {

template <typename Callback>
void MarkingWorklists::Update(Callback callback) {
  shared_.Update(callback);
  on_hold_.Update(callback);
  other_.Update(callback);
  for (auto& cw : context_worklists_) {
    cw.worklist->Update(callback);
  }
}

void MarkingWorklists::Local::Push(Tagged<HeapObject> object) {
  active_->Push(object);
}

bool MarkingWorklists::Local::Pop(Tagged<HeapObject>* object) {
  if (active_->Pop(object)) return true;
  if (!is_per_context_mode_) return false;
  // The active worklist is empty. Find any other non-empty worklist and
  // switch the active worklist to it.
  return PopContext(object);
}

void MarkingWorklists::Local::PushOnHold(Tagged<HeapObject> object) {
  on_hold_.Push(object);
}

bool MarkingWorklists::Local::PopOnHold(Tagged<HeapObject>* object) {
  return on_hold_.Pop(object);
}

Address MarkingWorklists::Local::SwitchToContext(Address context) {
  if (context == active_context_) return context;
  return SwitchToContextSlow(context);
}

void MarkingWorklists::Local::SwitchToContextImpl(
    Address context, MarkingWorklist::Local* worklist) {
  active_ = worklist;
  active_context_ = context;
}

void MarkingWorklists::Local::PublishCppHeapObjects() {
  if (!cpp_marking_state_) {
    return;
  }
  cpp_marking_state_->Publish();
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_WORKLIST_INL_H_
