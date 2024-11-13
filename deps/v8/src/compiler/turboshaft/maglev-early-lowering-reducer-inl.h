// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_

#include <optional>

#include "src/compiler/feedback-source.h"
#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/objects/contexts.h"
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

  void CheckInstanceType(V<Object> input, V<FrameState> frame_state,
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
        std::optional<RootIndex> expected_index =
            InstanceTypeChecker::UniqueMapOfInstanceType(first_instance_type);
        CHECK(expected_index.has_value());
        Handle<HeapObject> expected_map =
            Cast<HeapObject>(isolate_->root_handle(expected_index.value()));
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
      __ DeoptimizeIfNot(CheckInstanceTypeIsInRange(map, first_instance_type,
                                                    last_instance_type),
                         frame_state, DeoptimizeReason::kWrongInstanceType,
                         feedback);
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

  void CheckValueEqualsString(V<Object> object, InternalizedStringRef value,
                              V<FrameState> frame_state,
                              const FeedbackSource& feedback) {
    IF_NOT (LIKELY(__ TaggedEqual(object, __ HeapConstant(value.object())))) {
      __ DeoptimizeIfNot(__ ObjectIsString(object), frame_state,
                         DeoptimizeReason::kNotAString, feedback);
      V<Boolean> is_same_string_bool =
          __ StringEqual(V<String>::Cast(object),
                         __ template HeapConstant<String>(value.object()));
      __ DeoptimizeIf(
          __ RootEqual(is_same_string_bool, RootIndex::kFalseValue, isolate_),
          frame_state, DeoptimizeReason::kWrongValue, feedback);
    }
  }

  V<Object> CheckConstructResult(V<Object> construct_result,
                                 V<Object> implicit_receiver) {
    // If the result is an object (in the ECMA sense), we should get rid
    // of the receiver and use the result; see ECMA-262 version 5.1
    // section 13.2.2-7 on page 74.
    Label<Object> done(this);

    GOTO_IF(
        __ RootEqual(construct_result, RootIndex::kUndefinedValue, isolate_),
        done, implicit_receiver);

    // If the result is a smi, it is *not* an object in the ECMA sense.
    GOTO_IF(__ IsSmi(construct_result), done, implicit_receiver);

    // Check if the type of the result is not an object in the ECMA sense.
    GOTO_IF(JSAnyIsNotPrimitive(V<HeapObject>::Cast(construct_result)), done,
            construct_result);

    // Throw away the result of the constructor invocation and use the
    // implicit receiver as the result.
    GOTO(done, implicit_receiver);

    BIND(done, result);
    return result;
  }

  void CheckDerivedConstructResult(V<Object> construct_result,
                                   V<FrameState> frame_state,
                                   V<NativeContext> native_context,
                                   LazyDeoptOnThrow lazy_deopt_on_throw) {
    // The result of a derived construct should be an object (in the ECMA
    // sense).
    Label<> do_throw(this);

    // If the result is a smi, it is *not* an object in the ECMA sense.
    GOTO_IF(__ IsSmi(construct_result), do_throw);

    // Check if the type of the result is not an object done the ECMA sense.
    IF_NOT (JSAnyIsNotPrimitive(V<HeapObject>::Cast(construct_result))) {
      GOTO(do_throw);
      BIND(do_throw);
      __ CallRuntime_ThrowConstructorReturnedNonObject(
          isolate_, frame_state, native_context, lazy_deopt_on_throw);
      // ThrowConstructorReturnedNonObject should not return.
      __ Unreachable();
    }
  }

  void CheckConstTrackingLetCellTagged(V<Context> context, V<Object> value,
                                       int index, V<FrameState> frame_state,
                                       const FeedbackSource& feedback) {
    V<Object> old_value =
        __ LoadTaggedField(context, Context::OffsetOfElementAt(index));
    IF_NOT (__ TaggedEqual(old_value, value)) {
      CheckConstTrackingLetCell(context, index, frame_state, feedback);
    }
  }

  void CheckConstTrackingLetCell(V<Context> context, int index,
                                 V<FrameState> frame_state,
                                 const FeedbackSource& feedback) {
    // Load the const tracking let side data.
    V<Object> side_data = __ LoadTaggedField(
        context, Context::OffsetOfElementAt(
                     Context::CONST_TRACKING_LET_SIDE_DATA_INDEX));
    V<Object> index_data = __ LoadTaggedField(
        side_data, FixedArray::OffsetOfElementAt(
                       index - Context::MIN_CONTEXT_EXTENDED_SLOTS));
    // If the field is already marked as "not a constant", storing a
    // different value is fine. But if it's anything else (including the hole,
    // which means no value was stored yet), deopt this code. The lower tier
    // code will update the side data and invalidate DependentCode if needed.
    V<Word32> is_const = __ TaggedEqual(
        index_data, __ SmiConstant(ConstTrackingLetCell::kNonConstMarker));
    __ DeoptimizeIfNot(is_const, frame_state,
                       DeoptimizeReason::kConstTrackingLet, feedback);
  }

  V<Smi> UpdateJSArrayLength(V<Word32> length_raw, V<JSArray> object,
                             V<Word32> index) {
    Label<Smi> done(this);
    IF (__ Uint32LessThan(index, length_raw)) {
      GOTO(done, __ TagSmi(length_raw));
    } ELSE {
      V<Word32> new_length_raw =
          __ Word32Add(index, 1);  // This cannot overflow.
      V<Smi> new_length_tagged = __ TagSmi(new_length_raw);
      __ Store(object, new_length_tagged, StoreOp::Kind::TaggedBase(),
               MemoryRepresentation::TaggedSigned(),
               WriteBarrierKind::kNoWriteBarrier, JSArray::kLengthOffset);
      GOTO(done, new_length_tagged);
    }

    BIND(done, length_tagged);
    return length_tagged;
  }

  void TransitionElementsKindOrCheckMap(
      V<Object> object, V<FrameState> frame_state, bool check_heap_object,
      const ZoneVector<compiler::MapRef>& transition_sources,
      const MapRef transition_target, const FeedbackSource& feedback) {
    Label<> end(this);
    Label<> if_smi(this);

    TransitionElementsKind(object, transition_sources, transition_target,
                           check_heap_object, if_smi, end);

    __ DeoptimizeIfNot(
        __ TaggedEqual(__ LoadMapField(object),
                       __ HeapConstant(transition_target.object())),
        frame_state, DeoptimizeReason::kWrongMap, feedback);
    GOTO(end);

    if (check_heap_object && if_smi.has_incoming_jump()) {
      BIND(if_smi);
      __ Deoptimize(frame_state, DeoptimizeReason::kSmi, feedback);
    } else {
      DCHECK(!if_smi.has_incoming_jump());
    }

    BIND(end);
  }

  void TransitionMultipleElementsKind(
      V<Object> object, const ZoneVector<compiler::MapRef>& transition_sources,
      const MapRef transition_target) {
    Label<> end(this);

    TransitionElementsKind(object, transition_sources, transition_target,
                           /* check_heap_object */ true, end, end);

    GOTO(end);
    BIND(end);
  }

  void TransitionElementsKind(
      V<Object> object, const ZoneVector<compiler::MapRef>& transition_sources,
      const MapRef transition_target, bool check_heap_object, Label<>& if_smi,
      Label<>& end) {
    if (check_heap_object) {
      GOTO_IF(__ ObjectIsSmi(object), if_smi);
    }

    // Turboshaft's TransitionElementsKind operation loads the map everytime, so
    // we don't call it to have a single map load (in practice,
    // LateLoadElimination should probably eliminate the subsequent map loads,
    // but let's not risk it).
    V<Map> map = __ LoadMapField(object);
    V<Map> target_map = __ HeapConstant(transition_target.object());

    for (const compiler::MapRef transition_source : transition_sources) {
      bool is_simple = IsSimpleMapChangeTransition(
          transition_source.elements_kind(), transition_target.elements_kind());
      IF (__ TaggedEqual(map, __ HeapConstant(transition_source.object()))) {
        if (is_simple) {
          __ StoreField(object, AccessBuilder::ForMap(), target_map);
        } else {
          __ CallRuntime_TransitionElementsKind(
              isolate_, __ NoContextConstant(), V<HeapObject>::Cast(object),
              target_map);
        }
        GOTO(end);
      }
    }
  }

  V<Word32> JSAnyIsNotPrimitive(V<HeapObject> heap_object) {
    V<Map> map = __ LoadMapField(heap_object);
    if (V8_STATIC_ROOTS_BOOL) {
      // All primitive object's maps are allocated at the start of the read only
      // heap. Thus JS_RECEIVER's must have maps with larger (compressed)
      // addresses.
      return __ Uint32LessThanOrEqual(
          InstanceTypeChecker::kNonJsReceiverMapLimit,
          __ TruncateWordPtrToWord32(__ BitcastTaggedToWordPtr(map)));
    } else {
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      return __ Uint32LessThanOrEqual(FIRST_JS_RECEIVER_TYPE,
                                      __ LoadInstanceTypeField(map));
    }
  }

  V<Boolean> HasInPrototypeChain(V<Object> object, HeapObjectRef prototype,
                                 V<FrameState> frame_state,
                                 V<NativeContext> native_context,
                                 LazyDeoptOnThrow lazy_deopt_on_throw) {
    Label<Boolean> done(this);

    V<Boolean> true_bool = __ HeapConstant(factory_->true_value());
    V<Boolean> false_bool = __ HeapConstant(factory_->false_value());
    V<HeapObject> target_proto = __ HeapConstant(prototype.object());

    GOTO_IF(__ IsSmi(object), done, false_bool);

    LoopLabel<Map> loop(this);
    GOTO(loop, __ LoadMapField(object));

    BIND_LOOP(loop, map) {
      Label<> object_is_direct(this);

      IF (UNLIKELY(CheckInstanceTypeIsInRange(map, FIRST_TYPE,
                                              LAST_SPECIAL_RECEIVER_TYPE))) {
        Label<> call_runtime(this);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);

        GOTO_IF(__ Word32Equal(instance_type, JS_PROXY_TYPE), call_runtime);

        V<Word32> bitfield =
            __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField());
        int mask = Map::Bits1::HasNamedInterceptorBit::kMask |
                   Map::Bits1::IsAccessCheckNeededBit::kMask;
        GOTO_IF_NOT(__ Word32BitwiseAnd(bitfield, mask), object_is_direct);
        GOTO(call_runtime);

        BIND(call_runtime);
        GOTO(done, __ CallRuntime_HasInPrototypeChain(
                       isolate_, frame_state, native_context,
                       lazy_deopt_on_throw, object, target_proto));
      }
      GOTO(object_is_direct);

      BIND(object_is_direct);
      V<HeapObject> proto = __ template LoadField<HeapObject>(
          map, AccessBuilder::ForMapPrototype());
      GOTO_IF(__ RootEqual(proto, RootIndex::kNullValue, isolate_), done,
              false_bool);
      GOTO_IF(__ TaggedEqual(proto, target_proto), done, true_bool);

      GOTO(loop, __ LoadMapField(proto));
    }

    BIND(done, result);
    return result;
  }

  V<Map> MigrateMapIfNeeded(V<HeapObject> object, V<Map> map,
                            V<FrameState> frame_state,
                            const FeedbackSource& feedback) {
    ScopedVar<Map> result(this, map);

    V<Word32> bitfield3 =
        __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField3());
    IF (UNLIKELY(__ Word32BitwiseAnd(bitfield3,
                                     Map::Bits3::IsDeprecatedBit::kMask))) {
      V<Object> result = __ CallRuntime_TryMigrateInstance(
          isolate_, __ NoContextConstant(), object);
      __ DeoptimizeIf(__ ObjectIsSmi(result), frame_state,
                      DeoptimizeReason::kInstanceMigrationFailed, feedback);
      // Reload the map since TryMigrateInstance might have changed it.
      result = __ LoadMapField(V<HeapObject>::Cast(result));
    }

    return result;
  }

  V<PropertyArray> ExtendPropertiesBackingStore(
      V<PropertyArray> old_property_array, V<JSObject> object, int old_length,
      V<FrameState> frame_state, const FeedbackSource& feedback) {
    // Allocate new PropertyArray.
    int new_length = old_length + JSObject::kFieldsAdded;
    Uninitialized<PropertyArray> new_property_array =
        __ template Allocate<PropertyArray>(
            __ IntPtrConstant(PropertyArray::SizeFor(new_length)),
            AllocationType::kYoung);
    __ InitializeField(new_property_array, AccessBuilder::ForMap(),
                       __ HeapConstant(factory_->property_array_map()));

    // Copy existing properties over.
    for (int i = 0; i < old_length; i++) {
      V<Object> old_value = __ template LoadField<Object>(
          old_property_array, AccessBuilder::ForPropertyArraySlot(i));
      __ InitializeField(new_property_array,
                         AccessBuilder::ForPropertyArraySlot(i), old_value);
    }

    // Initialize new properties to undefined.
    V<Undefined> undefined = __ HeapConstant(factory_->undefined_value());
    for (int i = 0; i < JSObject::kFieldsAdded; ++i) {
      __ InitializeField(new_property_array,
                         AccessBuilder::ForPropertyArraySlot(old_length + i),
                         undefined);
    }

    // Read the hash.
    ScopedVar<Word32> hash(this);
    if (old_length == 0) {
      // The object might still have a hash, stored in properties_or_hash. If
      // properties_or_hash is a SMI, then it's the hash. It can also be an
      // empty PropertyArray.
      V<Object> hash_obj = __ template LoadField<Object>(
          object, AccessBuilder::ForJSObjectPropertiesOrHash());
      IF (__ IsSmi(hash_obj)) {
        hash = __ Word32ShiftLeft(__ UntagSmi(V<Smi>::Cast(hash_obj)),
                                  PropertyArray::HashField::kShift);
      } ELSE {
        hash = __ Word32Constant(PropertyArray::kNoHashSentinel);
      }
    } else {
      V<Smi> hash_smi = __ template LoadField<Smi>(
          old_property_array, AccessBuilder::ForPropertyArrayLengthAndHash());
      hash = __ Word32BitwiseAnd(__ UntagSmi(hash_smi),
                                 PropertyArray::HashField::kMask);
    }

    // Add the new length and write the length-and-hash field.
    static_assert(PropertyArray::LengthField::kShift == 0);
    V<Word32> length_and_hash = __ Word32BitwiseOr(hash, new_length);
    __ InitializeField(new_property_array,
                       AccessBuilder::ForPropertyArrayLengthAndHash(),
                       __ TagSmi(length_and_hash));

    V<PropertyArray> initialized_new_property_array =
        __ FinishInitialization(std::move(new_property_array));

    // Replace the old property array in {object}.
    __ StoreField(object, AccessBuilder::ForJSObjectPropertiesOrHash(),
                  initialized_new_property_array);

    return initialized_new_property_array;
  }

  void GeneratorStore(V<Context> context, V<JSGeneratorObject> generator,
                      base::SmallVector<OpIndex, 32> parameters_and_registers,
                      int suspend_id, int bytecode_offset) {
    V<FixedArray> array = __ template LoadTaggedField<FixedArray>(
        generator, JSGeneratorObject::kParametersAndRegistersOffset);
    for (int i = 0; static_cast<size_t>(i) < parameters_and_registers.size();
         i++) {
      __ Store(array, parameters_and_registers[i], StoreOp::Kind::TaggedBase(),
               MemoryRepresentation::AnyTagged(),
               WriteBarrierKind::kFullWriteBarrier,
               FixedArray::OffsetOfElementAt(i));
    }
    __ Store(generator, __ SmiConstant(Smi::FromInt(suspend_id)),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::TaggedSigned(),
             WriteBarrierKind::kNoWriteBarrier,
             JSGeneratorObject::kContinuationOffset);
    __ Store(generator, __ SmiConstant(Smi::FromInt(bytecode_offset)),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::TaggedSigned(),
             WriteBarrierKind::kNoWriteBarrier,
             JSGeneratorObject::kInputOrDebugPosOffset);

    __ Store(generator, context, StoreOp::Kind::TaggedBase(),
             MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kFullWriteBarrier,
             JSGeneratorObject::kContextOffset);
  }

 private:
  V<Word32> CheckInstanceTypeIsInRange(V<Map> map,
                                       InstanceType first_instance_type,
                                       InstanceType last_instance_type) {
    V<Word32> instance_type = __ LoadInstanceTypeField(map);

    if (first_instance_type == 0) {
      return __ Uint32LessThanOrEqual(instance_type, last_instance_type);
    } else {
      return __ Uint32LessThanOrEqual(
          __ Word32Sub(instance_type, first_instance_type),
          last_instance_type - first_instance_type);
    }
  }

  Isolate* isolate_ = __ data() -> isolate();
  LocalIsolate* local_isolate_ = isolate_->AsLocalIsolate();
  JSHeapBroker* broker_ = __ data() -> broker();
  LocalFactory* factory_ = local_isolate_->factory();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MAGLEV_EARLY_LOWERING_REDUCER_INL_H_
