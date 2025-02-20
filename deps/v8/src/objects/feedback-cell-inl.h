// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FEEDBACK_CELL_INL_H_
#define V8_OBJECTS_FEEDBACK_CELL_INL_H_

#include <optional>

#include "src/execution/tiering-manager.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/feedback-cell.h"
#include "src/objects/feedback-vector-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/struct-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/feedback-cell-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(FeedbackCell)

RELEASE_ACQUIRE_ACCESSORS(FeedbackCell, value, Tagged<HeapObject>, kValueOffset)

void FeedbackCell::clear_padding() {
  if (FeedbackCell::kAlignedSize == FeedbackCell::kUnalignedSize) return;
  DCHECK_GE(FeedbackCell::kAlignedSize, FeedbackCell::kUnalignedSize);
  memset(reinterpret_cast<uint8_t*>(address() + FeedbackCell::kUnalignedSize),
         0, FeedbackCell::kAlignedSize - FeedbackCell::kUnalignedSize);
}

void FeedbackCell::reset_feedback_vector(
    std::optional<std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                                     Tagged<HeapObject> target)>>
        gc_notify_updated_slot) {
  clear_interrupt_budget();
  if (IsUndefined(value()) || IsClosureFeedbackCellArray(value())) return;

  CHECK(IsFeedbackVector(value()));
  Tagged<ClosureFeedbackCellArray> closure_feedback_cell_array =
      Cast<FeedbackVector>(value())->closure_feedback_cell_array();
  set_value(closure_feedback_cell_array, kReleaseStore);
  if (gc_notify_updated_slot) {
    (*gc_notify_updated_slot)(*this, RawField(FeedbackCell::kValueOffset),
                              closure_feedback_cell_array);
  }
}

void FeedbackCell::clear_interrupt_budget() {
  // This value is always reset to a proper budget before it's used.
  set_interrupt_budget(0);
}

void FeedbackCell::clear_dispatch_handle() {
  WriteField<JSDispatchHandle::underlying_type>(kDispatchHandleOffset,
                                                kNullJSDispatchHandle.value());
}

#ifdef V8_ENABLE_LEAPTIERING
void FeedbackCell::allocate_dispatch_handle(Isolate* isolate,
                                            uint16_t parameter_count,
                                            Tagged<Code> code,
                                            WriteBarrierMode mode) {
  DCHECK_EQ(dispatch_handle(), kNullJSDispatchHandle);
  AllocateAndInstallJSDispatchHandle(kDispatchHandleOffset, isolate,
                                     parameter_count, code, mode);
}

JSDispatchHandle FeedbackCell::dispatch_handle() const {
  return JSDispatchHandle(
      ReadField<JSDispatchHandle::underlying_type>(kDispatchHandleOffset));
}

void FeedbackCell::set_dispatch_handle(JSDispatchHandle new_handle) {
  DCHECK_EQ(dispatch_handle(), kNullJSDispatchHandle);
  WriteField<JSDispatchHandle::underlying_type>(kDispatchHandleOffset,
                                                new_handle.value());
  JS_DISPATCH_HANDLE_WRITE_BARRIER(*this, new_handle);
}
#endif  // V8_ENABLE_LEAPTIERING

FeedbackCell::ClosureCountTransition FeedbackCell::IncrementClosureCount(
    Isolate* isolate) {
  ReadOnlyRoots r(isolate);
  if (map() == r.no_closures_cell_map()) {
    set_map(isolate, r.one_closure_cell_map());
    return kNoneToOne;
  } else if (map() == r.one_closure_cell_map()) {
    set_map(isolate, r.many_closures_cell_map());
    return kOneToMany;
  } else {
    DCHECK(map() == r.many_closures_cell_map());
    return kMany;
  }
}

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FEEDBACK_CELL_INL_H_
