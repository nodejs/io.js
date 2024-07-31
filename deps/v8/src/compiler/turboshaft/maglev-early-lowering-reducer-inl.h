// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_

#include "src/compiler/feedback-source.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/objects/instance-type-inl.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class MaglevEarlyLoweringReducer : public Next {
  // This Reducer provides some helpers that are used during
  // MaglevGraphBuildingPhase to lower some Maglev operators. Depending on what
  // we decide going forward (regarding SimplifiedLowering for instance), we
  // could introduce new Simplified or JS operations instead of using these
  // helpers to lower, and turn the helpers into regular REDUCE methods in the
  // new simplified lowering or in MachineLoweringReducer.

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(MaglevEarlyLowering)

  void CheckInstanceType(V<Object> input, OpIndex frame_state,
                         const FeedbackSource& feedback,
                         InstanceType first_instance_type,
                         InstanceType last_instance_type, bool check_smi) {
    if (check_smi) {
      __ DeoptimizeIf(__ IsSmi(input), frame_state,
                      DeoptimizeReason::kWrongInstanceType, feedback);
    }

    V<i::Map> map = __ LoadMapField(input);

    if (first_instance_type == last_instance_type) {
#if V8_STATIC_ROOTS_BOOL
      if (InstanceTypeChecker::UniqueMapOfInstanceType(first_instance_type)) {
        base::Optional<RootIndex> expected_index =
            InstanceTypeChecker::UniqueMapOfInstanceType(first_instance_type);
        CHECK(expected_index.has_value());
        Handle<HeapObject> expected_map = Handle<HeapObject>::cast(
            isolate_->root_handle(expected_index.value()));
        __ DeoptimizeIfNot(__ TaggedEqual(map, __ HeapConstant(expected_map)),
                           frame_state, DeoptimizeReason::kWrongInstanceType,
                           feedback);
        return;
      }
#endif  // V8_STATIC_ROOTS_BOOL
      V<Word32> instance_type = __ LoadInstanceTypeField(map);
      __ DeoptimizeIfNot(__ Word32Equal(instance_type, first_instance_type),
                         frame_state, DeoptimizeReason::kWrongInstanceType,
                         feedback);
    } else {
      V<Word32> instance_type = __ LoadInstanceTypeField(map);

      V<Word32> cond;
      if (first_instance_type == 0) {
        cond = __ Uint32LessThanOrEqual(instance_type, last_instance_type);
      } else {
        cond = __ Uint32LessThanOrEqual(
            __ Word32Sub(instance_type, first_instance_type),
            last_instance_type - first_instance_type);
      }
      __ DeoptimizeIfNot(cond, frame_state,
                         DeoptimizeReason::kWrongInstanceType, feedback);
    }
  }

  V<InternalizedString> CheckedInternalizedString(
      V<Object> object, OpIndex frame_state, bool check_smi,
      const FeedbackSource& feedback) {
    if (check_smi) {
      __ DeoptimizeIf(__ IsSmi(object), frame_state, DeoptimizeReason::kSmi,
                      feedback);
    }

    Label<InternalizedString> done(this);
    V<Map> map = __ LoadMapField(object);
    V<Word32> instance_type = __ LoadInstanceTypeField(map);

    // Go to the slow path if this is a non-string, or a non-internalised
    // string.
    static_assert((kStringTag | kInternalizedTag) == 0);
    IF (UNLIKELY(__ Word32BitwiseAnd(
            instance_type, kIsNotStringMask | kIsNotInternalizedMask))) {
      // Deopt if this isn't a string.
      __ DeoptimizeIf(__ Word32BitwiseAnd(instance_type, kIsNotStringMask),
                      frame_state, DeoptimizeReason::kWrongMap, feedback);
      // Deopt if this isn't a thin string.
      static_assert(base::bits::CountPopulation(kThinStringTagBit) == 1);
      __ DeoptimizeIfNot(__ Word32BitwiseAnd(instance_type, kThinStringTagBit),
                         frame_state, DeoptimizeReason::kWrongMap, feedback);
      // Load internalized string from thin string.
      V<InternalizedString> intern_string =
          __ template LoadField<InternalizedString>(
              object, AccessBuilder::ForThinStringActual());
      GOTO(done, intern_string);
    } ELSE {
      GOTO(done, V<InternalizedString>::Cast(object));
    }

    BIND(done, result);
    return result;
  }

  LocalIsolate* isolate_ = PipelineData::Get().isolate()->AsLocalIsolate();
  JSHeapBroker* broker_ = PipelineData::Get().broker();
  LocalFactory* factory_ = isolate_->factory();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
