// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FEEDBACK_CELL_H_
#define V8_OBJECTS_FEEDBACK_CELL_H_

#include <optional>

#include "src/objects/struct.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

class Undefined;

#include "torque-generated/src/objects/feedback-cell-tq.inc"

// This is a special cell used to maintain both the link between a
// closure and its feedback vector, as well as a way to count the
// number of closures created for a certain function per native
// context. There's at most one FeedbackCell for each function in
// a native context.
class FeedbackCell : public TorqueGeneratedFeedbackCell<FeedbackCell, Struct> {
 public:
  // Dispatched behavior.
  DECL_PRINTER(FeedbackCell)

  static const int kUnalignedSize = kSize;
  static const int kAlignedSize = RoundUp<kObjectAlignment>(int{kSize});

  using TorqueGeneratedFeedbackCell<FeedbackCell, Struct>::value;
  using TorqueGeneratedFeedbackCell<FeedbackCell, Struct>::set_value;

  DECL_RELEASE_ACQUIRE_ACCESSORS(value, Tagged<HeapObject>)

  inline void clear_interrupt_budget();

#ifdef V8_ENABLE_LEAPTIERING
  inline void allocate_dispatch_handle(
      IsolateForSandbox isolate, uint16_t parameter_count, Tagged<Code> code,
      WriteBarrierMode mode = WriteBarrierMode::UPDATE_WRITE_BARRIER);
  inline void clear_dispatch_handle();
  inline JSDispatchHandle dispatch_handle() const;
  inline void set_dispatch_handle(JSDispatchHandle new_handle);
#endif  // V8_ENABLE_LEAPTIERING

  inline void clear_padding();
  inline void reset_feedback_vector(
      std::optional<
          std::function<void(Tagged<HeapObject> object, ObjectSlot slot,
                             Tagged<HeapObject> target)>>
          gc_notify_updated_slot = std::nullopt);

  // The closure count is encoded in the cell's map, which distinguishes
  // between zero, one, or many closures. This function records a new closure
  // creation by updating the map.
  inline void IncrementClosureCount(Isolate* isolate);

  DECL_VERIFIER(FeedbackCell)

  class BodyDescriptor;

  TQ_OBJECT_CONSTRUCTORS(FeedbackCell)
};

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FEEDBACK_CELL_H_
