// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_INL_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_INL_H_

#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/codegen/external-reference.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/globals.h"
#include "src/compiler/linkage.h"
#include "src/compiler/operator.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/execution/frame-constants.h"
#include "src/objects/bigint.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type-checker.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/instance-type.h"
#include "src/objects/oddball.h"
#include "src/runtime/runtime.h"
#include "src/utils/utils.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// MachineLoweringReducer, formerly known as EffectControlLinearizer, lowers
// simplified operations to machine operations.
template <typename Next>
class MachineLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(MachineLowering)

  bool NeedsHeapObjectCheck(ObjectIsOp::InputAssumptions input_assumptions) {
    // TODO(nicohartmann@): Consider type information once we have that.
    switch (input_assumptions) {
      case ObjectIsOp::InputAssumptions::kNone:
        return true;
      case ObjectIsOp::InputAssumptions::kHeapObject:
      case ObjectIsOp::InputAssumptions::kBigInt:
        return false;
    }
  }

  OpIndex REDUCE(ChangeOrDeopt)(OpIndex input, OpIndex frame_state,
                                ChangeOrDeoptOp::Kind kind,
                                CheckForMinusZeroMode minus_zero_mode,
                                const FeedbackSource& feedback) {
    switch (kind) {
      case ChangeOrDeoptOp::Kind::kUint32ToInt32: {
        __ DeoptimizeIf(__ Int32LessThan(input, 0), frame_state,
                        DeoptimizeReason::kLostPrecision, feedback);
        return input;
      }
      case ChangeOrDeoptOp::Kind::kInt64ToInt32: {
        V<Word32> i32 = __ TruncateWord64ToWord32(input);
        __ DeoptimizeIfNot(__ Word64Equal(__ ChangeInt32ToInt64(i32), input),
                           frame_state, DeoptimizeReason::kLostPrecision,
                           feedback);
        return i32;
      }
      case ChangeOrDeoptOp::Kind::kUint64ToInt32: {
        __ DeoptimizeIfNot(
            __ Uint64LessThanOrEqual(input, static_cast<uint64_t>(kMaxInt)),
            frame_state, DeoptimizeReason::kLostPrecision, feedback);
        return __ TruncateWord64ToWord32(input);
      }
      case ChangeOrDeoptOp::Kind::kUint64ToInt64: {
        __ DeoptimizeIfNot(__ Uint64LessThanOrEqual(
                               input, std::numeric_limits<int64_t>::max()),
                           frame_state, DeoptimizeReason::kLostPrecision,
                           feedback);
        return input;
      }
      case ChangeOrDeoptOp::Kind::kFloat64ToInt32: {
        V<Word32> i32 = __ TruncateFloat64ToInt32OverflowUndefined(input);
        __ DeoptimizeIfNot(__ Float64Equal(__ ChangeInt32ToFloat64(i32), input),
                           frame_state, DeoptimizeReason::kLostPrecisionOrNaN,
                           feedback);

        if (minus_zero_mode == CheckForMinusZeroMode::kCheckForMinusZero) {
          // Check if {value} is -0.
          IF (UNLIKELY(__ Word32Equal(i32, 0))) {
            // In case of 0, we need to check the high bits for the IEEE -0
            // pattern.
            V<Word32> check_negative =
                __ Int32LessThan(__ Float64ExtractHighWord32(input), 0);
            __ DeoptimizeIf(check_negative, frame_state,
                            DeoptimizeReason::kMinusZero, feedback);
          }
        }

        return i32;
      }
      case ChangeOrDeoptOp::Kind::kFloat64ToInt64: {
        V<Word64> i64 = __ TruncateFloat64ToInt64OverflowToMin(input);
        __ DeoptimizeIfNot(__ Float64Equal(__ ChangeInt64ToFloat64(i64), input),
                           frame_state, DeoptimizeReason::kLostPrecisionOrNaN,
                           feedback);

        if (minus_zero_mode == CheckForMinusZeroMode::kCheckForMinusZero) {
          // Check if {value} is -0.
          IF (UNLIKELY(__ Word64Equal(i64, 0))) {
            // In case of 0, we need to check the high bits for the IEEE -0
            // pattern.
            V<Word32> check_negative =
                __ Int32LessThan(__ Float64ExtractHighWord32(input), 0);
            __ DeoptimizeIf(check_negative, frame_state,
                            DeoptimizeReason::kMinusZero, feedback);
          }
        }

        return i64;
      }
      case ChangeOrDeoptOp::Kind::kFloat64NotHole: {
        // First check whether {value} is a NaN at all...
        IF_NOT (LIKELY(__ Float64Equal(input, input))) {
          // ...and only if {value} is a NaN, perform the expensive bit
          // check. See http://crbug.com/v8/8264 for details.
          __ DeoptimizeIf(__ Word32Equal(__ Float64ExtractHighWord32(input),
                                         kHoleNanUpper32),
                          frame_state, DeoptimizeReason::kHole, feedback);
        }

        return input;
      }
    }
    UNREACHABLE();
  }

  OpIndex REDUCE(DeoptimizeIf)(OpIndex condition, OpIndex frame_state,
                               bool negated,
                               const DeoptimizeParameters* parameters) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    // Block cloning only works for branches, but not for `DeoptimizeIf`. On the
    // other hand, explicit control flow makes the overall pipeline and
    // escpecially the register allocator slower. So we only switch a
    // `DeoptiomizeIf` to a branch if it has a phi input, which indicates that
    // block cloning could be helpful.
    if (__ Get(condition).template Is<PhiOp>()) {
      if (negated) {
        IF_NOT (LIKELY(condition)) {
          __ Deoptimize(frame_state, parameters);
        }

      } else {
        IF (UNLIKELY(condition)) {
          __ Deoptimize(frame_state, parameters);
        }
      }
      return OpIndex::Invalid();
    }
    goto no_change;
  }

  V<Word32> REDUCE(ObjectIs)(V<Object> input, ObjectIsOp::Kind kind,
                             ObjectIsOp::InputAssumptions input_assumptions) {
    switch (kind) {
      case ObjectIsOp::Kind::kBigInt:
      case ObjectIsOp::Kind::kBigInt64: {
        DCHECK_IMPLIES(kind == ObjectIsOp::Kind::kBigInt64, Is64());

        Label<Word32> done(this);

        if (input_assumptions != ObjectIsOp::InputAssumptions::kBigInt) {
          if (NeedsHeapObjectCheck(input_assumptions)) {
            // Check for Smi.
            GOTO_IF(__ IsSmi(input), done, 0);
          }

          // Check for BigInt.
          V<Map> map = __ LoadMapField(input);
          V<Word32> is_bigint_map =
              __ TaggedEqual(map, __ HeapConstant(factory_->bigint_map()));
          GOTO_IF_NOT(is_bigint_map, done, 0);
        }

        if (kind == ObjectIsOp::Kind::kBigInt) {
          GOTO(done, 1);
        } else {
          DCHECK_EQ(kind, ObjectIsOp::Kind::kBigInt64);
          // We have to perform check for BigInt64 range.
          V<Word32> bitfield = __ template LoadField<Word32>(
              input, AccessBuilder::ForBigIntBitfield());
          GOTO_IF(__ Word32Equal(bitfield, 0), done, 1);

          // Length must be 1.
          V<Word32> length_field =
              __ Word32BitwiseAnd(bitfield, BigInt::LengthBits::kMask);
          GOTO_IF_NOT(__ Word32Equal(length_field,
                                     uint32_t{1} << BigInt::LengthBits::kShift),
                      done, 0);

          // Check if it fits in 64 bit signed int.
          V<Word64> lsd = __ template LoadField<Word64>(
              input, AccessBuilder::ForBigIntLeastSignificantDigit64());
          V<Word32> magnitude_check = __ Uint64LessThanOrEqual(
              lsd, std::numeric_limits<int64_t>::max());
          GOTO_IF(magnitude_check, done, 1);

          // The BigInt probably doesn't fit into signed int64. The only
          // exception is int64_t::min. We check for this.
          V<Word32> sign =
              __ Word32BitwiseAnd(bitfield, BigInt::SignBits::kMask);
          V<Word32> sign_check = __ Word32Equal(sign, BigInt::SignBits::kMask);
          GOTO_IF_NOT(sign_check, done, 0);

          V<Word32> min_check =
              __ Word64Equal(lsd, std::numeric_limits<int64_t>::min());
          GOTO_IF(min_check, done, 1);

          GOTO(done, 0);
        }

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kUndetectable:
        if (DependOnNoUndetectableObjectsProtector()) {
          V<Word32> is_undefined = __ TaggedEqual(
              input, __ HeapConstant(factory_->undefined_value()));
          V<Word32> is_null =
              __ TaggedEqual(input, __ HeapConstant(factory_->null_value()));
          return __ Word32BitwiseOr(is_undefined, is_null);
        }
        [[fallthrough]];
      case ObjectIsOp::Kind::kCallable:
      case ObjectIsOp::Kind::kConstructor:
      case ObjectIsOp::Kind::kDetectableCallable:
      case ObjectIsOp::Kind::kNonCallable:
      case ObjectIsOp::Kind::kReceiver:
      case ObjectIsOp::Kind::kReceiverOrNullOrUndefined: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(UNLIKELY(__ IsSmi(input)), done, 0);
        }

        // Load bitfield from map.
        V<Map> map = __ LoadMapField(input);
        V<Word32> bitfield =
            __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField());

        V<Word32> check;
        switch (kind) {
          case ObjectIsOp::Kind::kCallable:
            check =
                __ Word32Equal(Map::Bits1::IsCallableBit::kMask,
                               __ Word32BitwiseAnd(
                                   bitfield, Map::Bits1::IsCallableBit::kMask));
            break;
          case ObjectIsOp::Kind::kConstructor:
            check = __ Word32Equal(
                Map::Bits1::IsConstructorBit::kMask,
                __ Word32BitwiseAnd(bitfield,
                                    Map::Bits1::IsConstructorBit::kMask));
            break;
          case ObjectIsOp::Kind::kDetectableCallable:
            check = __ Word32Equal(
                Map::Bits1::IsCallableBit::kMask,
                __ Word32BitwiseAnd(
                    bitfield, (Map::Bits1::IsCallableBit::kMask) |
                                  (Map::Bits1::IsUndetectableBit::kMask)));
            break;
          case ObjectIsOp::Kind::kNonCallable:
            check = __ Word32Equal(
                0, __ Word32BitwiseAnd(bitfield,
                                       Map::Bits1::IsCallableBit::kMask));
            GOTO_IF_NOT(check, done, 0);
            // Fallthrough into receiver check.
            [[fallthrough]];
          case ObjectIsOp::Kind::kReceiver:
            check = JSAnyIsNotPrimitiveHeapObject(input, map);
            break;
          case ObjectIsOp::Kind::kReceiverOrNullOrUndefined: {
#if V8_STATIC_ROOTS_BOOL
            V<Word32> check0 = JSAnyIsNotPrimitiveHeapObject(input, map);
            V<Word32> check1 = __ TaggedEqual(
                input, __ HeapConstant(factory_->undefined_value()));
            V<Word32> check2 =
                __ TaggedEqual(input, __ HeapConstant(factory_->null_value()));
            check =
                __ Word32BitwiseOr(check0, __ Word32BitwiseOr(check1, check2));
#else
            static_assert(LAST_PRIMITIVE_HEAP_OBJECT_TYPE == ODDBALL_TYPE);
            static_assert(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
            // Rule out all primitives except oddballs (true, false, undefined,
            // null).
            V<Word32> instance_type = __ LoadInstanceTypeField(map);
            GOTO_IF_NOT(__ Uint32LessThanOrEqual(ODDBALL_TYPE, instance_type),
                        done, 0);

            // Rule out booleans.
            check = __ Word32Equal(
                0,
                __ TaggedEqual(map, __ HeapConstant(factory_->boolean_map())));
#endif  // V8_STATIC_ROOTS_BOOL
            break;
          }
          case ObjectIsOp::Kind::kUndetectable:
            check = __ Word32Equal(
                Map::Bits1::IsUndetectableBit::kMask,
                __ Word32BitwiseAnd(bitfield,
                                    Map::Bits1::IsUndetectableBit::kMask));
            break;
          default:
            UNREACHABLE();
        }
        GOTO(done, check);

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kSmi: {
        // If we statically know that this is a heap object, it cannot be a Smi.
        if (!NeedsHeapObjectCheck(input_assumptions)) {
          return __ Word32Constant(0);
        }
        return __ IsSmi(input);
      }
      case ObjectIsOp::Kind::kNumber: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(__ IsSmi(input), done, 1);
        }

        V<Map> map = __ LoadMapField(input);
        GOTO(done,
             __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map())));

        BIND(done, result);
        return result;
      }

#if V8_STATIC_ROOTS_BOOL
      case ObjectIsOp::Kind::kString: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(__ IsSmi(input), done, 0);
        }

        V<Map> map = __ LoadMapField(input);
        GOTO(done,
             __ Uint32LessThanOrEqual(
                 __ TruncateWordPtrToWord32(__ BitcastHeapObjectToWordPtr(map)),
                 __ Word32Constant(InstanceTypeChecker::kLastStringMap)));

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kSymbol: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(__ IsSmi(input), done, 0);
        }

        V<Map> map = __ LoadMapField(input);
        GOTO(done,
             __ Word32Equal(
                 __ TruncateWordPtrToWord32(__ BitcastHeapObjectToWordPtr(map)),
                 __ Word32Constant(StaticReadOnlyRoot::kSymbolMap)));

        BIND(done, result);
        return result;
      }
#else
      case ObjectIsOp::Kind::kString:
      case ObjectIsOp::Kind::kSymbol:
#endif
      case ObjectIsOp::Kind::kArrayBufferView: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(__ IsSmi(input), done, 0);
        }

        // Load instance type from map.
        V<Map> map = __ LoadMapField(input);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);

        V<Word32> check;
        switch (kind) {
#if !V8_STATIC_ROOTS_BOOL
          case ObjectIsOp::Kind::kSymbol:
            check = __ Word32Equal(instance_type, SYMBOL_TYPE);
            break;
          case ObjectIsOp::Kind::kString:
            check = __ Uint32LessThan(instance_type, FIRST_NONSTRING_TYPE);
            break;
#endif
          case ObjectIsOp::Kind::kArrayBufferView:
            check = __ Uint32LessThan(
                __ Word32Sub(instance_type, FIRST_JS_ARRAY_BUFFER_VIEW_TYPE),
                LAST_JS_ARRAY_BUFFER_VIEW_TYPE -
                    FIRST_JS_ARRAY_BUFFER_VIEW_TYPE + 1);
            break;
          default:
            UNREACHABLE();
        }
        GOTO(done, check);

        BIND(done, result);
        return result;
      }
      case ObjectIsOp::Kind::kInternalizedString: {
        DCHECK_EQ(input_assumptions, ObjectIsOp::InputAssumptions::kHeapObject);
        // Load instance type from map.
        V<Map> map = __ LoadMapField(input);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);

        return __ Word32Equal(
            __ Word32BitwiseAnd(instance_type,
                                (kIsNotStringMask | kIsNotInternalizedMask)),
            kInternalizedTag);
      }
      case ObjectIsOp::Kind::kStringOrStringWrapper: {
        Label<Word32> done(this);

        // Check for Smi if necessary.
        if (NeedsHeapObjectCheck(input_assumptions)) {
          GOTO_IF(__ IsSmi(input), done, 0);
        }

        // Load instance type from map.
        V<Map> map = __ LoadMapField(input);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);

        GOTO_IF(__ Uint32LessThan(instance_type, FIRST_NONSTRING_TYPE), done,
                1);
        GOTO_IF_NOT(__ Word32Equal(instance_type, JS_PRIMITIVE_WRAPPER_TYPE),
                    done, 0);

        V<Word32> bitfield2 = __ template LoadField<Word32>(
            map, AccessBuilder::ForMapBitField2());

        V<Word32> elements_kind =
            __ Word32BitwiseAnd(bitfield2, Map::Bits2::ElementsKindBits::kMask);

        GOTO_IF(__ Word32Equal(FAST_STRING_WRAPPER_ELEMENTS
                                   << Map::Bits2::ElementsKindBits::kShift,
                               elements_kind),
                done, 1);

        V<Word32> check =
            __ Word32Equal(SLOW_STRING_WRAPPER_ELEMENTS
                               << Map::Bits2::ElementsKindBits::kShift,
                           elements_kind);
        GOTO(done, check);

        BIND(done, result);
        return result;
      }
    }
    UNREACHABLE();
  }

  V<Word32> REDUCE(FloatIs)(V<Float64> value, NumericKind kind,
                            FloatRepresentation input_rep) {
    DCHECK_EQ(input_rep, FloatRepresentation::Float64());
    switch (kind) {
      case NumericKind::kFloat64Hole: {
        Label<Word32> done(this);
        // First check whether {value} is a NaN at all...
        GOTO_IF(LIKELY(__ Float64Equal(value, value)), done, 0);
        // ...and only if {value} is a NaN, perform the expensive bit
        // check. See http://crbug.com/v8/8264 for details.
        GOTO(done, __ Word32Equal(__ Float64ExtractHighWord32(value),
                                  kHoleNanUpper32));
        BIND(done, result);
        return result;
      }
      case NumericKind::kFinite: {
        V<Float64> diff = __ Float64Sub(value, value);
        return __ Float64Equal(diff, diff);
      }
      case NumericKind::kInteger: {
        V<Float64> trunc = __ Float64RoundToZero(value);
        V<Float64> diff = __ Float64Sub(value, trunc);
        return __ Float64Equal(diff, 0.0);
      }
      case NumericKind::kSafeInteger: {
        Label<Word32> done(this);
        V<Float64> trunc = __ Float64RoundToZero(value);
        V<Float64> diff = __ Float64Sub(value, trunc);
        GOTO_IF_NOT(__ Float64Equal(diff, 0), done, 0);
        V<Word32> in_range =
            __ Float64LessThanOrEqual(__ Float64Abs(trunc), kMaxSafeInteger);
        GOTO(done, in_range);

        BIND(done, result);
        return result;
      }
      case NumericKind::kSmi: {
        Label<Word32> done(this);
        V<Word32> v32 = __ TruncateFloat64ToInt32OverflowUndefined(value);
        GOTO_IF_NOT(__ Float64Equal(value, __ ChangeInt32ToFloat64(v32)), done,
                    0);
        IF (__ Word32Equal(v32, 0)) {
          // Checking -0.
          GOTO_IF(__ Int32LessThan(__ Float64ExtractHighWord32(value), 0), done,
                  0);
        }

        if constexpr (SmiValuesAre32Bits()) {
          GOTO(done, 1);
        } else {
          OpIndex add = __ Int32AddCheckOverflow(v32, v32);
          V<Word32> overflow = __ template Projection<Word32>(add, 1);
          GOTO_IF(overflow, done, 0);
          GOTO(done, 1);
        }

        BIND(done, result);
        return result;
      }
      case NumericKind::kMinusZero: {
        if (Is64()) {
          V<Word64> value64 = __ BitcastFloat64ToWord64(value);
          return __ Word64Equal(value64, kMinusZeroBits);
        } else {
          Label<Word32> done(this);
          V<Word32> value_lo = __ Float64ExtractLowWord32(value);
          GOTO_IF_NOT(__ Word32Equal(value_lo, kMinusZeroLoBits), done, 0);
          V<Word32> value_hi = __ Float64ExtractHighWord32(value);
          GOTO(done, __ Word32Equal(value_hi, kMinusZeroHiBits));

          BIND(done, result);
          return result;
        }
      }
      case NumericKind::kNaN: {
        V<Word32> diff = __ Float64Equal(value, value);
        return __ Word32Equal(diff, 0);
      }
    }

    UNREACHABLE();
  }

  V<Word32> REDUCE(ObjectIsNumericValue)(V<Object> input, NumericKind kind,
                                         FloatRepresentation input_rep) {
    DCHECK_EQ(input_rep, FloatRepresentation::Float64());
    Label<Word32> done(this);

    switch (kind) {
      case NumericKind::kFinite:
      case NumericKind::kInteger:
      case NumericKind::kSafeInteger:
      case NumericKind::kSmi:
        GOTO_IF(__ IsSmi(input), done, 1);
        break;
      case NumericKind::kMinusZero:
      case NumericKind::kNaN:
        GOTO_IF(__ IsSmi(input), done, 0);
        break;
      case NumericKind::kFloat64Hole:
        // ObjectIsFloat64Hole is not used, but can be implemented when needed.
        UNREACHABLE();
    }

    V<Map> map = __ LoadMapField(input);
    GOTO_IF_NOT(
        __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map())), done,
        0);

    V<Float64> value = __ template LoadField<Float64>(
        input, AccessBuilder::ForHeapNumberValue());
    GOTO(done, __ FloatIs(value, kind, input_rep));

    BIND(done, result);
    return result;
  }

  V<Object> REDUCE(Convert)(V<Object> input, ConvertOp::Kind from,
                            ConvertOp::Kind to) {
    switch (to) {
      case ConvertOp::Kind::kNumber: {
        if (from == ConvertOp::Kind::kPlainPrimitive) {
          return __ CallBuiltin_PlainPrimitiveToNumber(
              isolate_, V<PlainPrimitive>::Cast(input));
        } else {
          DCHECK_EQ(from, ConvertOp::Kind::kString);
          return __ CallBuiltin_StringToNumber(isolate_,
                                               V<String>::Cast(input));
        }
      }
      case ConvertOp::Kind::kBoolean: {
        DCHECK_EQ(from, ConvertOp::Kind::kObject);
        return __ CallBuiltin_ToBoolean(isolate_, input);
      }
      case ConvertOp::Kind::kString: {
        DCHECK_EQ(from, ConvertOp::Kind::kNumber);
        return __ CallBuiltin_NumberToString(isolate_, V<Number>::Cast(input));
      }
      case ConvertOp::Kind::kSmi: {
        DCHECK_EQ(from, ConvertOp::Kind::kNumberOrOddball);
        Label<Smi> done(this);
        GOTO_IF(LIKELY(__ ObjectIsSmi(input)), done, V<Smi>::Cast(input));

        V<Float64> value = __ template LoadField<Float64>(
            input, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
        GOTO(done, __ TagSmi(__ ReversibleFloat64ToInt32(value)));

        BIND(done, result);
        return result;
      }
      default:
        UNREACHABLE();
    }
  }

  V<Object> REDUCE(ConvertUntaggedToJSPrimitive)(
      OpIndex input, ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      ConvertUntaggedToJSPrimitiveOp::InputInterpretation input_interpretation,
      CheckForMinusZeroMode minus_zero_mode) {
    switch (kind) {
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBigInt: {
        DCHECK(Is64());
        DCHECK_EQ(input_rep, RegisterRepresentation::Word64());
        Label<Object> done(this);

        // BigInts with value 0 must be of size 0 (canonical form).
        GOTO_IF(__ Word64Equal(input, int64_t{0}), done,
                AllocateBigInt(OpIndex::Invalid(), OpIndex::Invalid()));

        // The GOTO_IF above could have been changed to an unconditional GOTO,
        // in which case we are now in unreachable code, so we can skip the
        // following step and return.
        if (input_interpretation ==
            ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned) {
          // Shift sign bit into BigInt's sign bit position.
          V<Word32> bitfield = __ Word32BitwiseOr(
              BigInt::LengthBits::encode(1),
              __ TruncateWord64ToWord32(__ Word64ShiftRightLogical(
                  input, static_cast<int32_t>(63 - BigInt::SignBits::kShift))));

          // We use (value XOR (value >> 63)) - (value >> 63) to compute the
          // absolute value, in a branchless fashion.
          V<Word64> sign_mask =
              __ Word64ShiftRightArithmetic(input, int32_t{63});
          V<Word64> absolute_value =
              __ Word64Sub(__ Word64BitwiseXor(input, sign_mask), sign_mask);
          GOTO(done, AllocateBigInt(bitfield, absolute_value));
        } else {
          DCHECK_EQ(
              input_interpretation,
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kUnsigned);
          const auto bitfield = BigInt::LengthBits::encode(1);
          GOTO(done, AllocateBigInt(__ Word32Constant(bitfield), input));
        }

        BIND(done, result);
        return result;
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber: {
        if (input_rep == RegisterRepresentation::Word32()) {
          switch (input_interpretation) {
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned: {
              if (SmiValuesAre32Bits()) {
                return __ TagSmi(input);
              }
              DCHECK(SmiValuesAre31Bits());

              Label<Object> done(this);
              Label<> overflow(this);

              TagSmiOrOverflow(input, &overflow, &done);

              if (BIND(overflow)) {
                GOTO(done, AllocateHeapNumberWithValue(
                               __ ChangeInt32ToFloat64(input)));
              }

              BIND(done, result);
              return result;
            }
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::
                kUnsigned: {
              Label<Object> done(this);

              GOTO_IF(__ Uint32LessThanOrEqual(input, Smi::kMaxValue), done,
                      __ TagSmi(input));
              GOTO(done, AllocateHeapNumberWithValue(
                             __ ChangeUint32ToFloat64(input)));

              BIND(done, result);
              return result;
            }
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCharCode:
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::
                kCodePoint:
              UNREACHABLE();
          }
        } else if (input_rep == RegisterRepresentation::Word64()) {
          switch (input_interpretation) {
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned: {
              Label<Object> done(this);
              Label<> outside_smi_range(this);

              V<Word32> v32 = __ TruncateWord64ToWord32(input);
              V<Word64> v64 = __ ChangeInt32ToInt64(v32);
              GOTO_IF_NOT(__ Word64Equal(v64, input), outside_smi_range);

              if constexpr (SmiValuesAre32Bits()) {
                GOTO(done, __ TagSmi(input));
              } else {
                TagSmiOrOverflow(v32, &outside_smi_range, &done);
              }

              if (BIND(outside_smi_range)) {
                GOTO(done, AllocateHeapNumberWithValue(
                               __ ChangeInt64ToFloat64(input)));
              }

              BIND(done, result);
              return result;
            }
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::
                kUnsigned: {
              Label<Object> done(this);

              GOTO_IF(__ Uint64LessThanOrEqual(input, Smi::kMaxValue), done,
                      __ TagSmi(__ TruncateWord64ToWord32(input)));
              GOTO(done,
                   AllocateHeapNumberWithValue(__ ChangeInt64ToFloat64(input)));

              BIND(done, result);
              return result;
            }
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCharCode:
            case ConvertUntaggedToJSPrimitiveOp::InputInterpretation::
                kCodePoint:
              UNREACHABLE();
          }
        } else {
          DCHECK_EQ(input_rep, RegisterRepresentation::Float64());
          Label<Object> done(this);
          Label<> outside_smi_range(this);

          V<Word32> v32 = __ TruncateFloat64ToInt32OverflowUndefined(input);
          GOTO_IF_NOT(__ Float64Equal(input, __ ChangeInt32ToFloat64(v32)),
                      outside_smi_range);

          if (minus_zero_mode == CheckForMinusZeroMode::kCheckForMinusZero) {
            // In case of 0, we need to check the high bits for the IEEE -0
            // pattern.
            IF (__ Word32Equal(v32, 0)) {
              GOTO_IF(__ Int32LessThan(__ Float64ExtractHighWord32(input), 0),
                      outside_smi_range);
            }
          }

          if constexpr (SmiValuesAre32Bits()) {
            GOTO(done, __ TagSmi(v32));
          } else {
            TagSmiOrOverflow(v32, &outside_smi_range, &done);
          }

          if (BIND(outside_smi_range)) {
            GOTO(done, AllocateHeapNumberWithValue(input));
          }

          BIND(done, result);
          return result;
        }
        UNREACHABLE();
        break;
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kHeapNumber: {
        DCHECK_EQ(input_rep, RegisterRepresentation::Float64());
        DCHECK_EQ(input_interpretation,
                  ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned);
        return AllocateHeapNumberWithValue(input);
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::
          kHeapNumberOrUndefined: {
        DCHECK_EQ(input_rep, RegisterRepresentation::Float64());
        DCHECK_EQ(input_interpretation,
                  ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned);
        Label<Object> done(this);
        Label<> allocate_heap_number(this);

        // First check whether {input} is a NaN at all...
        IF (UNLIKELY(__ Float64IsNaN(input))) {
          // ...and only if {input} is a NaN, perform the expensive signaling
          // NaN bit check. See http://crbug.com/v8/8264 for details.
          GOTO_IF_NOT(__ Word32Equal(__ Float64ExtractHighWord32(input),
                                     kHoleNanUpper32),
                      allocate_heap_number);
          GOTO(done, __ HeapConstant(factory_->undefined_value()));
        } ELSE {
          GOTO(allocate_heap_number);
        }

        if (BIND(allocate_heap_number)) {
          GOTO(done, AllocateHeapNumberWithValue(input));
        }

        BIND(done, result);
        return result;
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kSmi: {
        DCHECK_EQ(input_rep, RegisterRepresentation::Word32());
        DCHECK_EQ(input_interpretation,
                  ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned);
        return __ TagSmi(input);
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kBoolean: {
        DCHECK_EQ(input_rep, RegisterRepresentation::Word32());
        DCHECK_EQ(input_interpretation,
                  ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned);
        Label<Object> done(this);

        IF (input) {
          GOTO(done, __ HeapConstant(factory_->true_value()));
        } ELSE {
          GOTO(done, __ HeapConstant(factory_->false_value()));
        }

        BIND(done, result);
        return result;
      }
      case ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kString: {
        Label<Word32> single_code(this);
        Label<Object> done(this);

        if (input_interpretation ==
            ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCharCode) {
          GOTO(single_code, __ Word32BitwiseAnd(input, 0xFFFF));
        } else {
          DCHECK_EQ(
              input_interpretation,
              ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kCodePoint);
          // Check if the input is a single code unit.
          GOTO_IF(LIKELY(__ Uint32LessThanOrEqual(input, 0xFFFF)), single_code,
                  input);

          // Generate surrogate pair string.

          // Convert UTF32 to UTF16 code units and store as a 32 bit word.
          V<Word32> lead_offset = __ Word32Constant(0xD800 - (0x10000 >> 10));

          // lead = (codepoint >> 10) + LEAD_OFFSET
          V<Word32> lead =
              __ Word32Add(__ Word32ShiftRightLogical(input, 10), lead_offset);

          // trail = (codepoint & 0x3FF) + 0xDC00
          V<Word32> trail =
              __ Word32Add(__ Word32BitwiseAnd(input, 0x3FF), 0xDC00);

          // codepoint = (trail << 16) | lead
#if V8_TARGET_BIG_ENDIAN
          V<Word32> code =
              __ Word32BitwiseOr(__ Word32ShiftLeft(lead, 16), trail);
#else
          V<Word32> code =
              __ Word32BitwiseOr(__ Word32ShiftLeft(trail, 16), lead);
#endif

          // Allocate a new SeqTwoByteString for {code}.
          auto string = __ template Allocate<String>(
              __ IntPtrConstant(SeqTwoByteString::SizeFor(2)),
              AllocationType::kYoung);
          // Set padding to 0.
          __ Initialize(string, __ IntPtrConstant(0),
                        MemoryRepresentation::TaggedSigned(),
                        WriteBarrierKind::kNoWriteBarrier,
                        SeqTwoByteString::SizeFor(2) - kObjectAlignment);
          __ InitializeField(
              string, AccessBuilder::ForMap(),
              __ HeapConstant(factory_->seq_two_byte_string_map()));
          __ InitializeField(string, AccessBuilder::ForNameRawHashField(),
                             __ Word32Constant(Name::kEmptyHashField));
          __ InitializeField(string, AccessBuilder::ForStringLength(),
                             __ Word32Constant(2));
          // Write the code as a single 32-bit value by adapting the elements
          // access to SeqTwoByteString characters.
          ElementAccess char_access =
              AccessBuilder::ForSeqTwoByteStringCharacter();
          char_access.machine_type = MachineType::Uint32();
          __ InitializeNonArrayBufferElement(string, char_access,
                                             __ IntPtrConstant(0), code);
          GOTO(done, __ FinishInitialization(std::move(string)));
        }

        if (BIND(single_code, code)) {
          // Check if the {code} is a one byte character.
          IF (LIKELY(__ Uint32LessThanOrEqual(code,
                                              String::kMaxOneByteCharCode))) {
            // Load the isolate wide single character string table.
            V<Object> table =
                __ HeapConstant(factory_->single_character_string_table());

            // Compute the {table} index for {code}.
            V<WordPtr> index = __ ChangeUint32ToUintPtr(code);

            // Load the string for the {code} from the single character string
            // table.
            OpIndex entry = __ LoadNonArrayBufferElement(
                table, AccessBuilder::ForFixedArrayElement(), index);

            // Use the {entry} from the {table}.
            GOTO(done, entry);
          } ELSE {
            // Allocate a new SeqTwoBytesString for {code}.
            auto string = __ template Allocate<String>(
                __ IntPtrConstant(SeqTwoByteString::SizeFor(1)),
                AllocationType::kYoung);

            // Set padding to 0.
            __ Initialize(string, __ IntPtrConstant(0),
                          MemoryRepresentation::TaggedSigned(),
                          WriteBarrierKind::kNoWriteBarrier,
                          SeqTwoByteString::SizeFor(1) - kObjectAlignment);
            __ InitializeField(
                string, AccessBuilder::ForMap(),
                __ HeapConstant(factory_->seq_two_byte_string_map()));
            __ InitializeField(string, AccessBuilder::ForNameRawHashField(),
                               __ Word32Constant(Name::kEmptyHashField));
            __ InitializeField(string, AccessBuilder::ForStringLength(),
                               __ Word32Constant(1));
            __ InitializeNonArrayBufferElement(
                string, AccessBuilder::ForSeqTwoByteStringCharacter(),
                __ IntPtrConstant(0), code);
            GOTO(done, __ FinishInitialization(std::move(string)));
          }
        }

        BIND(done, result);
        return result;
      }
    }

    UNREACHABLE();
  }

  OpIndex REDUCE(ConvertUntaggedToJSPrimitiveOrDeopt)(
      OpIndex input, OpIndex frame_state,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind kind,
      RegisterRepresentation input_rep,
      ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation
          input_interpretation,
      const FeedbackSource& feedback) {
    DCHECK_EQ(kind,
              ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::kSmi);
    if (input_rep == RegisterRepresentation::Word32()) {
      if (input_interpretation ==
          ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kSigned) {
        if constexpr (SmiValuesAre32Bits()) {
          return __ TagSmi(input);
        } else {
          OpIndex test = __ Int32AddCheckOverflow(input, input);
          __ DeoptimizeIf(__ template Projection<Word32>(test, 1), frame_state,
                          DeoptimizeReason::kLostPrecision, feedback);
          return __ BitcastWord32ToSmi(__ template Projection<Word32>(test, 0));
        }
      } else {
        DCHECK_EQ(input_interpretation, ConvertUntaggedToJSPrimitiveOrDeoptOp::
                                            InputInterpretation::kUnsigned);
        V<Word32> check = __ Uint32LessThanOrEqual(input, Smi::kMaxValue);
        __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kLostPrecision,
                           feedback);
        return __ TagSmi(input);
      }
    } else {
      DCHECK_EQ(input_rep, RegisterRepresentation::Word64());
      if (input_interpretation ==
          ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::kSigned) {
        V<Word32> i32 = __ TruncateWord64ToWord32(input);
        V<Word32> check = __ Word64Equal(__ ChangeInt32ToInt64(i32), input);
        __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kLostPrecision,
                           feedback);
        if constexpr (SmiValuesAre32Bits()) {
          return __ TagSmi(input);
        } else {
          OpIndex test = __ Int32AddCheckOverflow(i32, i32);
          __ DeoptimizeIf(__ template Projection<Word32>(test, 1), frame_state,
                          DeoptimizeReason::kLostPrecision, feedback);
          return __ BitcastWord32ToSmi(__ template Projection<Word32>(test, 0));
        }
      } else {
        DCHECK_EQ(input_interpretation, ConvertUntaggedToJSPrimitiveOrDeoptOp::
                                            InputInterpretation::kUnsigned);
        V<Word32> check = __ Uint64LessThanOrEqual(
            input, static_cast<uint64_t>(Smi::kMaxValue));
        __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kLostPrecision,
                           feedback);
        return __ TagSmi(input);
      }
    }

    UNREACHABLE();
  }

  OpIndex REDUCE(ConvertJSPrimitiveToUntagged)(
      V<Object> object, ConvertJSPrimitiveToUntaggedOp::UntaggedKind kind,
      ConvertJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    switch (kind) {
      case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kInt32:
        if (input_assumptions ==
            ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kSmi) {
          return __ UntagSmi(V<Smi>::Cast(object));
        } else if (input_assumptions ==
                   ConvertJSPrimitiveToUntaggedOp::InputAssumptions::
                       kNumberOrOddball) {
          Label<Word32> done(this);

          IF (LIKELY(__ ObjectIsSmi(object))) {
            GOTO(done, __ UntagSmi(V<Smi>::Cast(object)));
          } ELSE {
            V<Float64> value = __ template LoadField<Float64>(
                object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
            GOTO(done, __ ReversibleFloat64ToInt32(value));
          }

          BIND(done, result);
          return result;
        } else {
          DCHECK_EQ(input_assumptions, ConvertJSPrimitiveToUntaggedOp::
                                           InputAssumptions::kPlainPrimitive);
          Label<Word32> done(this);
          GOTO_IF(LIKELY(__ ObjectIsSmi(object)), done,
                  __ UntagSmi(V<Smi>::Cast(object)));
          V<Number> number =
              __ ConvertPlainPrimitiveToNumber(V<PlainPrimitive>::Cast(object));
          GOTO_IF(__ ObjectIsSmi(number), done, __ UntagSmi(number));
          V<Float64> f64 = __ template LoadField<Float64>(
              V<HeapNumber>::Cast(number), AccessBuilder::ForHeapNumberValue());
          GOTO(done, __ JSTruncateFloat64ToWord32(f64));
          BIND(done, result);
          return result;
        }
        UNREACHABLE();
      case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kInt64:
        if (input_assumptions ==
            ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kSmi) {
          return __ ChangeInt32ToInt64(__ UntagSmi(V<Smi>::Cast(object)));
        } else {
          DCHECK_EQ(input_assumptions, ConvertJSPrimitiveToUntaggedOp::
                                           InputAssumptions::kNumberOrOddball);
          Label<Word64> done(this);

          IF (LIKELY(__ ObjectIsSmi(object))) {
            GOTO(done,
                 __ ChangeInt32ToInt64(__ UntagSmi(V<Smi>::Cast(object))));
          } ELSE {
            V<Float64> value = __ template LoadField<Float64>(
                object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
            GOTO(done, __ ReversibleFloat64ToInt64(value));
          }

          BIND(done, result);
          return result;
        }
        UNREACHABLE();
      case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kUint32: {
        DCHECK_EQ(
            input_assumptions,
            ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kNumberOrOddball);
        Label<Word32> done(this);

        IF (LIKELY(__ ObjectIsSmi(object))) {
          GOTO(done, __ UntagSmi(V<Smi>::Cast(object)));
        } ELSE {
          V<Float64> value = __ template LoadField<Float64>(
              object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
          GOTO(done, __ ReversibleFloat64ToUint32(value));
        }

        BIND(done, result);
        return result;
      }
      case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kBit:
        DCHECK_EQ(input_assumptions,
                  ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kBoolean);
        return __ TaggedEqual(object, __ HeapConstant(factory_->true_value()));
      case ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kFloat64: {
        if (input_assumptions == ConvertJSPrimitiveToUntaggedOp::
                                     InputAssumptions::kNumberOrOddball) {
          Label<Float64> done(this);

          IF (LIKELY(__ ObjectIsSmi(object))) {
            GOTO(done,
                 __ ChangeInt32ToFloat64(__ UntagSmi(V<Smi>::Cast(object))));
          } ELSE {
            V<Float64> value = __ template LoadField<Float64>(
                object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
            GOTO(done, value);
          }

          BIND(done, result);
          return result;
        } else {
          DCHECK_EQ(input_assumptions, ConvertJSPrimitiveToUntaggedOp::
                                           InputAssumptions::kPlainPrimitive);
          Label<Float64> done(this);
          GOTO_IF(LIKELY(__ ObjectIsSmi(object)), done,
                  __ ChangeInt32ToFloat64(__ UntagSmi(V<Smi>::Cast(object))));
          V<Number> number =
              __ ConvertPlainPrimitiveToNumber(V<PlainPrimitive>::Cast(object));
          GOTO_IF(__ ObjectIsSmi(number), done,
                  __ ChangeInt32ToFloat64(__ UntagSmi(number)));
          V<Float64> f64 = __ template LoadField<Float64>(
              V<HeapNumber>::Cast(number), AccessBuilder::ForHeapNumberValue());
          GOTO(done, f64);
          BIND(done, result);
          return result;
        }
      }
    }
    UNREACHABLE();
  }

  OpIndex REDUCE(ConvertJSPrimitiveToUntaggedOrDeopt)(
      V<Object> object, OpIndex frame_state,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind from_kind,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind to_kind,
      CheckForMinusZeroMode minus_zero_mode, const FeedbackSource& feedback) {
    switch (to_kind) {
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32: {
        if (from_kind ==
            ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi) {
          __ DeoptimizeIfNot(__ ObjectIsSmi(object), frame_state,
                             DeoptimizeReason::kNotASmi, feedback);
          return __ UntagSmi(V<Smi>::Cast(object));
        } else {
          DCHECK_EQ(
              from_kind,
              ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber);
          Label<Word32> done(this);

          IF (LIKELY(__ ObjectIsSmi(object))) {
            GOTO(done, __ UntagSmi(V<Smi>::Cast(object)));
          } ELSE {
            V<Map> map = __ LoadMapField(object);
            __ DeoptimizeIfNot(
                __ TaggedEqual(map,
                               __ HeapConstant(factory_->heap_number_map())),
                frame_state, DeoptimizeReason::kNotAHeapNumber, feedback);
            V<Float64> heap_number_value = __ template LoadField<Float64>(
                object, AccessBuilder::ForHeapNumberValue());

            GOTO(done,
                 __ ChangeFloat64ToInt32OrDeopt(heap_number_value, frame_state,
                                                minus_zero_mode, feedback));
          }

          BIND(done, result);
          return result;
        }
      }
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt64: {
        DCHECK_EQ(
            from_kind,
            ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber);
        Label<Word64> done(this);

        IF (LIKELY(__ ObjectIsSmi(object))) {
          GOTO(done, __ ChangeInt32ToInt64(__ UntagSmi(V<Smi>::Cast(object))));
        } ELSE {
          V<Map> map = __ LoadMapField(object);
          __ DeoptimizeIfNot(
              __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map())),
              frame_state, DeoptimizeReason::kNotAHeapNumber, feedback);
          V<Float64> heap_number_value = __ template LoadField<Float64>(
              object, AccessBuilder::ForHeapNumberValue());
          GOTO(done,
               __ ChangeFloat64ToInt64OrDeopt(heap_number_value, frame_state,
                                              minus_zero_mode, feedback));
        }

        BIND(done, result);
        return result;
      }
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kFloat64: {
        Label<Float64> done(this);

        // In the Smi case, just convert to int32 and then float64.
        // Otherwise, check heap numberness and load the number.
        IF (__ ObjectIsSmi(object)) {
          GOTO(done,
               __ ChangeInt32ToFloat64(__ UntagSmi(V<Smi>::Cast(object))));
        } ELSE {
          GOTO(done, ConvertHeapObjectToFloat64OrDeopt(object, frame_state,
                                                       from_kind, feedback));
        }

        BIND(done, result);
        return result;
      }
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kArrayIndex: {
        DCHECK_EQ(from_kind, ConvertJSPrimitiveToUntaggedOrDeoptOp::
                                 JSPrimitiveKind::kNumberOrString);
        Label<WordPtr> done(this);

        IF (LIKELY(__ ObjectIsSmi(object))) {
          // In the Smi case, just convert to intptr_t.
          GOTO(done, __ ChangeInt32ToIntPtr(__ UntagSmi(V<Smi>::Cast(object))));
        } ELSE {
          V<Map> map = __ LoadMapField(object);
          IF (LIKELY(__ TaggedEqual(
                  map, __ HeapConstant(factory_->heap_number_map())))) {
            V<Float64> heap_number_value = __ template LoadField<Float64>(
                object, AccessBuilder::ForHeapNumberValue());
            // Perform Turbofan's "CheckedFloat64ToIndex"
            {
              if constexpr (Is64()) {
                V<Word64> i64 = __ TruncateFloat64ToInt64OverflowUndefined(
                    heap_number_value);
                // The TruncateKind above means there will be a precision loss
                // in case INT64_MAX input is passed, but that precision loss
                // would not be detected and would not lead to a deoptimization
                // from the first check. But in this case, we'll deopt anyway
                // because of the following checks.
                __ DeoptimizeIfNot(__ Float64Equal(__ ChangeInt64ToFloat64(i64),
                                                   heap_number_value),
                                   frame_state,
                                   DeoptimizeReason::kLostPrecisionOrNaN,
                                   feedback);
                __ DeoptimizeIfNot(
                    __ IntPtrLessThan(i64, kMaxSafeIntegerUint64), frame_state,
                    DeoptimizeReason::kNotAnArrayIndex, feedback);
                __ DeoptimizeIfNot(
                    __ IntPtrLessThan(-kMaxSafeIntegerUint64, i64), frame_state,
                    DeoptimizeReason::kNotAnArrayIndex, feedback);
                GOTO(done, i64);
              } else {
                V<Word32> i32 = __ TruncateFloat64ToInt32OverflowUndefined(
                    heap_number_value);
                __ DeoptimizeIfNot(__ Float64Equal(__ ChangeInt32ToFloat64(i32),
                                                   heap_number_value),
                                   frame_state,
                                   DeoptimizeReason::kLostPrecisionOrNaN,
                                   feedback);
                GOTO(done, i32);
              }
            }
          } ELSE {
#if V8_STATIC_ROOTS_BOOL
            V<Word32> is_string_map = __ Uint32LessThanOrEqual(
                __ TruncateWordPtrToWord32(__ BitcastHeapObjectToWordPtr(map)),
                __ Word32Constant(InstanceTypeChecker::kLastStringMap));
#else
            V<Word32> instance_type = __ LoadInstanceTypeField(map);
            V<Word32> is_string_map =
                __ Uint32LessThan(instance_type, FIRST_NONSTRING_TYPE);
#endif
            __ DeoptimizeIfNot(is_string_map, frame_state,
                               DeoptimizeReason::kNotAString, feedback);

            // TODO(nicohartmann@): We might introduce a Turboshaft way for
            // constructing call descriptors.
            MachineSignature::Builder builder(__ graph_zone(), 1, 1);
            builder.AddReturn(MachineType::Int32());
            builder.AddParam(MachineType::TaggedPointer());
            auto desc = Linkage::GetSimplifiedCDescriptor(__ graph_zone(),
                                                          builder.Build());
            auto ts_desc =
                TSCallDescriptor::Create(desc, CanThrow::kNo, __ graph_zone());
            OpIndex callee = __ ExternalConstant(
                ExternalReference::string_to_array_index_function());
            // NOTE: String::ToArrayIndex() currently returns int32_t.
            V<WordPtr> index =
                __ ChangeInt32ToIntPtr(__ Call(callee, {object}, ts_desc));
            __ DeoptimizeIf(__ WordPtrEqual(index, -1), frame_state,
                            DeoptimizeReason::kNotAnArrayIndex, feedback);
            GOTO(done, index);
          }
        }

        BIND(done, result);
        return result;
      }
    }
    UNREACHABLE();
  }

  OpIndex REDUCE(TruncateJSPrimitiveToUntagged)(
      V<Object> object, TruncateJSPrimitiveToUntaggedOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOp::InputAssumptions input_assumptions) {
    switch (kind) {
      case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt32: {
        DCHECK_EQ(input_assumptions, TruncateJSPrimitiveToUntaggedOp::
                                         InputAssumptions::kNumberOrOddball);
        Label<Word32> done(this);

        IF (LIKELY(__ ObjectIsSmi(object))) {
          GOTO(done, __ UntagSmi(V<Smi>::Cast(object)));
        } ELSE {
          V<Float64> number_value = __ template LoadField<Float64>(
              object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
          GOTO(done, __ JSTruncateFloat64ToWord32(number_value));
        }

        BIND(done, result);
        return result;
      }
      case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kInt64: {
        DCHECK_EQ(input_assumptions,
                  TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kBigInt);
        DCHECK(Is64());
        Label<Word64> done(this);

        V<Word32> bitfield = __ template LoadField<Word32>(
            object, AccessBuilder::ForBigIntBitfield());
        IF (__ Word32Equal(bitfield, 0)) {
          GOTO(done, 0);
        } ELSE {
          V<Word64> lsd = __ template LoadField<Word64>(
              object, AccessBuilder::ForBigIntLeastSignificantDigit64());
          V<Word32> sign =
              __ Word32BitwiseAnd(bitfield, BigInt::SignBits::kMask);
          IF (__ Word32Equal(sign, 1)) {
            GOTO(done, __ Word64Sub(0, lsd));
          }

          GOTO(done, lsd);
        }

        BIND(done, result);
        return result;
      }
      case TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kBit: {
        Label<Word32> done(this);

        if (input_assumptions ==
            TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject) {
          // Perform Smi check.
          IF (UNLIKELY(__ ObjectIsSmi(object))) {
            GOTO(done, __ Word32Equal(__ TaggedEqual(object, __ TagSmi(0)), 0));
          }

          // Otherwise fall through into HeapObject case.
        } else {
          DCHECK_EQ(
              input_assumptions,
              TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject);
        }

#if V8_STATIC_ROOTS_BOOL
        // Check if {object} is a falsey root or the true value.
        // Undefined is the first root, so it's the smallest possible pointer
        // value, which means we don't have to subtract it for the range check.
        ReadOnlyRoots roots(isolate_);
        static_assert(StaticReadOnlyRoot::kUndefinedValue + sizeof(Undefined) ==
                      StaticReadOnlyRoot::kNullValue);
        static_assert(StaticReadOnlyRoot::kNullValue + sizeof(Null) ==
                      StaticReadOnlyRoot::kempty_string);
        static_assert(StaticReadOnlyRoot::kempty_string +
                          SeqOneByteString::SizeFor(0) ==
                      StaticReadOnlyRoot::kFalseValue);
        static_assert(StaticReadOnlyRoot::kFalseValue + sizeof(False) ==
                      StaticReadOnlyRoot::kTrueValue);
        V<Word32> object_as_word32 = __ TruncateWordPtrToWord32(
            __ BitcastHeapObjectToWordPtr(V<HeapObject>::Cast(object)));
        V<Word32> true_as_word32 =
            __ Word32Constant(StaticReadOnlyRoot::kTrueValue);
        GOTO_IF(__ Uint32LessThan(object_as_word32, true_as_word32), done, 0);
        GOTO_IF(__ Word32Equal(object_as_word32, true_as_word32), done, 1);
#else
        // Check if {object} is false.
        GOTO_IF(
            __ TaggedEqual(object, __ HeapConstant(factory_->false_value())),
            done, 0);

        // Check if {object} is true.
        GOTO_IF(__ TaggedEqual(object, __ HeapConstant(factory_->true_value())),
                done, 1);

        // Check if {object} is the empty string.
        GOTO_IF(
            __ TaggedEqual(object, __ HeapConstant(factory_->empty_string())),
            done, 0);

        // Only check null and undefined if we're not going to check the
        // undetectable bit.
        if (DependOnNoUndetectableObjectsProtector()) {
          // Check if {object} is the null value.
          GOTO_IF(
              __ TaggedEqual(object, __ HeapConstant(factory_->null_value())),
              done, 0);

          // Check if {object} is the undefined value.
          GOTO_IF(__ TaggedEqual(object,
                                 __ HeapConstant(factory_->undefined_value())),
                  done, 0);
        }
#endif

        // Load the map of {object}.
        V<Map> map = __ LoadMapField(object);

        if (!DependOnNoUndetectableObjectsProtector()) {
          // Check if the {object} is undetectable and immediately return false.
          V<Word32> bitfield = __ template LoadField<Word32>(
              map, AccessBuilder::ForMapBitField());
          GOTO_IF(__ Word32BitwiseAnd(bitfield,
                                      Map::Bits1::IsUndetectableBit::kMask),
                  done, 0);
        }

        // Check if {object} is a HeapNumber.
        IF (UNLIKELY(__ TaggedEqual(
                map, __ HeapConstant(factory_->heap_number_map())))) {
          // For HeapNumber {object}, just check that its value is not 0.0, -0.0
          // or NaN.
          V<Float64> number_value = __ template LoadField<Float64>(
              object, AccessBuilder::ForHeapNumberValue());
          GOTO(done, __ Float64LessThan(0.0, __ Float64Abs(number_value)));
        }

        // Check if {object} is a BigInt.
        IF (UNLIKELY(
                __ TaggedEqual(map, __ HeapConstant(factory_->bigint_map())))) {
          V<Word32> bitfield = __ template LoadField<Word32>(
              object, AccessBuilder::ForBigIntBitfield());
          GOTO(done, IsNonZero(__ Word32BitwiseAnd(bitfield,
                                                   BigInt::LengthBits::kMask)));
        }

        // All other values that reach here are true.
        GOTO(done, 1);

        BIND(done, result);
        return result;
      }
    }
    UNREACHABLE();
  }

  OpIndex REDUCE(TruncateJSPrimitiveToUntaggedOrDeopt)(
      V<Object> input, OpIndex frame_state,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind kind,
      TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement
          input_requirement,
      const FeedbackSource& feedback) {
    DCHECK_EQ(kind,
              TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32);
    Label<Word32> done(this);
    // In the Smi case, just convert to int32.
    GOTO_IF(LIKELY(__ ObjectIsSmi(input)), done,
            __ UntagSmi(V<Smi>::Cast(input)));

    // Otherwise, check that it's a heap number or oddball and truncate the
    // value to int32.
    V<Float64> number_value = ConvertHeapObjectToFloat64OrDeopt(
        input, frame_state, input_requirement, feedback);
    GOTO(done, __ JSTruncateFloat64ToWord32(number_value));

    BIND(done, result);
    return result;
  }

  V<Word32> JSAnyIsNotPrimitiveHeapObject(V<Object> value,
                                          V<Map> value_map = OpIndex{}) {
    if (!value_map.valid()) {
      value_map = __ LoadMapField(value);
    }
#if V8_STATIC_ROOTS_BOOL
    // Assumes only primitive objects and JS_RECEIVER's are passed here. All
    // primitive object's maps are in RO space and are allocated before all
    // JS_RECEIVER maps. Thus primitive object maps have smaller (compressed)
    // addresses.
    return __ Uint32LessThan(
        InstanceTypeChecker::kNonJsReceiverMapLimit,
        __ TruncateWordPtrToWord32(__ BitcastHeapObjectToWordPtr(value_map)));
#else
    static_assert(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
    V<Word32> value_instance_type = __ LoadInstanceTypeField(value_map);
    return __ Uint32LessThanOrEqual(FIRST_JS_RECEIVER_TYPE,
                                    value_instance_type);
#endif
  }

  OpIndex REDUCE(ConvertJSPrimitiveToObject)(V<Object> value,
                                             V<Object> native_context,
                                             OptionalV<Object> global_proxy,
                                             ConvertReceiverMode mode) {
    switch (mode) {
      case ConvertReceiverMode::kNullOrUndefined:
        DCHECK(global_proxy.valid());
        return global_proxy.value();
      case ConvertReceiverMode::kNotNullOrUndefined:
      case ConvertReceiverMode::kAny: {
        Label<Object> done(this);

        // Check if {value} is already a JSReceiver (or null/undefined).
        Label<> convert_to_object(this);
        GOTO_IF(UNLIKELY(__ ObjectIsSmi(value)), convert_to_object);
        GOTO_IF_NOT(LIKELY(__ JSAnyIsNotPrimitiveHeapObject(value)),
                    convert_to_object);
        GOTO(done, value);

        // Wrap the primitive {value} into a JSPrimitiveWrapper.
        if (BIND(convert_to_object)) {
          if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
            DCHECK(global_proxy.valid());
            // Replace the {value} with the {global_proxy}.
            GOTO_IF(UNLIKELY(__ TaggedEqual(
                        value, __ HeapConstant(factory_->undefined_value()))),
                    done, global_proxy.value());
            GOTO_IF(UNLIKELY(__ TaggedEqual(
                        value, __ HeapConstant(factory_->null_value()))),
                    done, global_proxy.value());
          }
          GOTO(done, __ CallBuiltin_ToObject(
                         isolate_, V<Context>::Cast(native_context), value));
        }

        BIND(done, result);
        return result;
      }
    }
    UNREACHABLE();
  }

  OpIndex REDUCE(NewConsString)(OpIndex length, OpIndex first, OpIndex second) {
    // Determine the instance types of {first} and {second}.
    V<Map> first_map = __ LoadMapField(first);
    V<Word32> first_type = __ LoadInstanceTypeField(first_map);
    V<Map> second_map = __ LoadMapField(second);
    V<Word32> second_type = __ LoadInstanceTypeField(second_map);

    Label<Object> allocate_string(this);
    // Determine the proper map for the resulting ConsString.
    // If both {first} and {second} are one-byte strings, we
    // create a new ConsOneByteString, otherwise we create a
    // new ConsString instead.
    static_assert(kOneByteStringTag != 0);
    static_assert(kTwoByteStringTag == 0);
    V<Word32> instance_type = __ Word32BitwiseAnd(first_type, second_type);
    V<Word32> encoding =
        __ Word32BitwiseAnd(instance_type, kStringEncodingMask);
    IF (__ Word32Equal(encoding, kTwoByteStringTag)) {
      GOTO(allocate_string,
           __ HeapConstant(factory_->cons_two_byte_string_map()));
    } ELSE {
      GOTO(allocate_string,
           __ HeapConstant(factory_->cons_one_byte_string_map()));
    }

    // Allocate the resulting ConsString.
    BIND(allocate_string, map);
    auto string = __ template Allocate<String>(
        __ IntPtrConstant(sizeof(ConsString)), AllocationType::kYoung);
    __ InitializeField(string, AccessBuilder::ForMap(), map);
    __ InitializeField(string, AccessBuilder::ForNameRawHashField(),
                       __ Word32Constant(Name::kEmptyHashField));
    __ InitializeField(string, AccessBuilder::ForStringLength(), length);
    __ InitializeField(string, AccessBuilder::ForConsStringFirst(), first);
    __ InitializeField(string, AccessBuilder::ForConsStringSecond(), second);
    return __ FinishInitialization(std::move(string));
  }

  OpIndex REDUCE(NewArray)(V<WordPtr> length, NewArrayOp::Kind kind,
                           AllocationType allocation_type) {
    Label<Object> done(this);

    GOTO_IF(__ WordPtrEqual(length, 0), done,
            __ HeapConstant(factory_->empty_fixed_array()));

    // Compute the effective size of the backing store.
    intptr_t size_log2;
    Handle<Map> array_map;
    // TODO(nicohartmann@): Replace ElementAccess by a Turboshaft replacement.
    ElementAccess access;
    V<Any> the_hole_value;
    switch (kind) {
      case NewArrayOp::Kind::kDouble: {
        size_log2 = kDoubleSizeLog2;
        array_map = factory_->fixed_double_array_map();
        access = {kTaggedBase, FixedDoubleArray::kHeaderSize,
                  compiler::Type::NumberOrHole(), MachineType::Float64(),
                  kNoWriteBarrier};
        the_hole_value = __ template LoadField<Float64>(
            __ HeapConstant(factory_->the_hole_value()),
            AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
        break;
      }
      case NewArrayOp::Kind::kObject: {
        size_log2 = kTaggedSizeLog2;
        array_map = factory_->fixed_array_map();
        access = {kTaggedBase, FixedArray::kHeaderSize, compiler::Type::Any(),
                  MachineType::AnyTagged(), kNoWriteBarrier};
        the_hole_value = __ HeapConstant(factory_->the_hole_value());
        break;
      }
    }
    V<WordPtr> size =
        __ WordPtrAdd(__ WordPtrShiftLeft(length, static_cast<int>(size_log2)),
                      access.header_size);

    // Allocate the result and initialize the header.
    auto uninitialized_array =
        __ template Allocate<FixedArray>(size, allocation_type);
    __ InitializeField(uninitialized_array, AccessBuilder::ForMap(),
                       __ HeapConstant(array_map));
    __ InitializeField(uninitialized_array,
                       AccessBuilder::ForFixedArrayLength(),
                       __ TagSmi(__ TruncateWordPtrToWord32(length)));
    // TODO(nicohartmann@): Should finish initialization only after all elements
    // have been initialized.
    auto array = __ FinishInitialization(std::move(uninitialized_array));

    ScopedVar<WordPtr> index(this, 0);

    WHILE(__ UintPtrLessThan(index, length)) {
      __ StoreNonArrayBufferElement(array, access, index, the_hole_value);
      // Advance the {index}.
      index = __ WordPtrAdd(index, 1);
    }

    GOTO(done, array);

    BIND(done, result);
    return result;
  }

  OpIndex REDUCE(DoubleArrayMinMax)(V<Object> array,
                                    DoubleArrayMinMaxOp::Kind kind) {
    DCHECK(kind == DoubleArrayMinMaxOp::Kind::kMin ||
           kind == DoubleArrayMinMaxOp::Kind::kMax);
    const bool is_max = kind == DoubleArrayMinMaxOp::Kind::kMax;

    // Iterate the elements and find the result.
    V<WordPtr> array_length =
        __ ChangeInt32ToIntPtr(__ UntagSmi(__ template LoadField<Smi>(
            array, AccessBuilder::ForJSArrayLength(
                       ElementsKind::PACKED_DOUBLE_ELEMENTS))));
    V<Object> elements = __ template LoadField<Object>(
        array, AccessBuilder::ForJSObjectElements());

    ScopedVar<Float64> result(this, is_max ? -V8_INFINITY : V8_INFINITY);
    ScopedVar<WordPtr> index(this, 0);

    WHILE(__ UintPtrLessThan(index, array_length)) {
      V<Float64> element = __ template LoadNonArrayBufferElement<Float64>(
          elements, AccessBuilder::ForFixedDoubleArrayElement(), index);

      result = is_max ? __ Float64Max(result, element)
                      : __ Float64Min(result, element);
      index = __ WordPtrAdd(index, 1);
    }

    return __ ConvertFloat64ToNumber(result,
                                     CheckForMinusZeroMode::kCheckForMinusZero);
  }

  OpIndex REDUCE(LoadFieldByIndex)(V<Object> object, V<Word32> field_index) {
    // Index encoding (see `src/objects/field-index-inl.h`):
    // For efficiency, the LoadByFieldIndex instruction takes an index that is
    // optimized for quick access. If the property is inline, the index is
    // positive. If it's out-of-line, the encoded index is -raw_index - 1 to
    // disambiguate the zero out-of-line index from the zero inobject case.
    // The index itself is shifted up by one bit, the lower-most bit
    // signifying if the field is a mutable double box (1) or not (0).
    V<WordPtr> index = __ ChangeInt32ToIntPtr(field_index);

    Label<> double_field(this);
    Label<Object> done(this);

    // Check if field is a mutable double field.
    GOTO_IF(
        UNLIKELY(__ Word32Equal(
            __ Word32BitwiseAnd(__ TruncateWordPtrToWord32(index), 0x1), 0x1)),
        double_field);

    {
      // The field is a proper Tagged field on {object}. The {index} is
      // shifted to the left by one in the code below.

      // Check if field is in-object or out-of-object.
      IF (__ IntPtrLessThan(index, 0)) {
        // The field is located in the properties backing store of {object}.
        // The {index} is equal to the negated out of property index plus 1.
        V<Object> properties = __ template LoadField<Object>(
            object, AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer());

        V<WordPtr> out_of_object_index = __ WordPtrSub(0, index);
        V<Object> result =
            __ Load(properties, out_of_object_index,
                    LoadOp::Kind::Aligned(BaseTaggedness::kTaggedBase),
                    MemoryRepresentation::AnyTagged(),
                    FixedArray::kHeaderSize - kTaggedSize, kTaggedSizeLog2 - 1);
        GOTO(done, result);
      } ELSE {
        // This field is located in the {object} itself.
        V<Object> result = __ Load(
            object, index, LoadOp::Kind::Aligned(BaseTaggedness::kTaggedBase),
            MemoryRepresentation::AnyTagged(), JSObject::kHeaderSize,
            kTaggedSizeLog2 - 1);
        GOTO(done, result);
      }
    }

    if (BIND(double_field)) {
      // If field is a Double field, either unboxed in the object on 64 bit
      // architectures, or a mutable HeapNumber.
      V<WordPtr> double_index = __ WordPtrShiftRightArithmetic(index, 1);
      Label<Object> loaded_field(this);

      // Check if field is in-object or out-of-object.
      IF (__ IntPtrLessThan(double_index, 0)) {
        V<Object> properties = __ template LoadField<Object>(
            object, AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer());

        V<WordPtr> out_of_object_index = __ WordPtrSub(0, double_index);
        V<Object> result =
            __ Load(properties, out_of_object_index,
                    LoadOp::Kind::Aligned(BaseTaggedness::kTaggedBase),
                    MemoryRepresentation::AnyTagged(),
                    FixedArray::kHeaderSize - kTaggedSize, kTaggedSizeLog2);
        GOTO(loaded_field, result);
      } ELSE {
        // The field is located in the {object} itself.
        V<Object> result =
            __ Load(object, double_index,
                    LoadOp::Kind::Aligned(BaseTaggedness::kTaggedBase),
                    MemoryRepresentation::AnyTagged(), JSObject::kHeaderSize,
                    kTaggedSizeLog2);
        GOTO(loaded_field, result);
      }

      if (BIND(loaded_field, field)) {
        // We may have transitioned in-place away from double, so check that
        // this is a HeapNumber -- otherwise the load is fine and we don't need
        // to copy anything anyway.
        GOTO_IF(__ ObjectIsSmi(field), done, field);
        V<Map> map = __ LoadMapField(field);
        GOTO_IF_NOT(
            __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map())),
            done, field);

        V<Float64> value = __ template LoadField<Float64>(
            field, AccessBuilder::ForHeapNumberValue());
        GOTO(done, AllocateHeapNumberWithValue(value));
      }
    }

    BIND(done, result);
    return result;
  }

  OpIndex REDUCE(WordBinopDeoptOnOverflow)(
      OpIndex left, OpIndex right, OpIndex frame_state,
      WordBinopDeoptOnOverflowOp::Kind kind, WordRepresentation rep,
      FeedbackSource feedback, CheckForMinusZeroMode mode) {
    switch (kind) {
      case WordBinopDeoptOnOverflowOp::Kind::kSignedAdd: {
        DCHECK_EQ(mode, CheckForMinusZeroMode::kDontCheckForMinusZero);
        OpIndex result = __ IntAddCheckOverflow(left, right, rep);

        V<Word32> overflow =
            __ Projection(result, 1, RegisterRepresentation::Word32());
        __ DeoptimizeIf(overflow, frame_state, DeoptimizeReason::kOverflow,
                        feedback);
        return __ Projection(result, 0, rep);
      }
      case WordBinopDeoptOnOverflowOp::Kind::kSignedSub: {
        DCHECK_EQ(mode, CheckForMinusZeroMode::kDontCheckForMinusZero);
        OpIndex result = __ IntSubCheckOverflow(left, right, rep);

        V<Word32> overflow =
            __ Projection(result, 1, RegisterRepresentation::Word32());
        __ DeoptimizeIf(overflow, frame_state, DeoptimizeReason::kOverflow,
                        feedback);
        return __ Projection(result, 0, rep);
      }
      case WordBinopDeoptOnOverflowOp::Kind::kSignedMul:
        if (rep == WordRepresentation::Word32()) {
          OpIndex result = __ Int32MulCheckOverflow(left, right);
          V<Word32> overflow =
              __ Projection(result, 1, RegisterRepresentation::Word32());
          __ DeoptimizeIf(overflow, frame_state, DeoptimizeReason::kOverflow,
                          feedback);
          V<Word32> value =
              __ Projection(result, 0, RegisterRepresentation::Word32());

          if (mode == CheckForMinusZeroMode::kCheckForMinusZero) {
            IF (__ Word32Equal(value, 0)) {
              __ DeoptimizeIf(
                  __ Int32LessThan(__ Word32BitwiseOr(left, right), 0),
                  frame_state, DeoptimizeReason::kMinusZero, feedback);
            }
          }

          return value;
        } else {
          DCHECK_EQ(rep, WordRepresentation::Word64());
          DCHECK_EQ(mode, CheckForMinusZeroMode::kDontCheckForMinusZero);
          OpIndex result = __ Int64MulCheckOverflow(left, right);

          V<Word32> overflow =
              __ Projection(result, 1, RegisterRepresentation::Word32());
          __ DeoptimizeIf(overflow, frame_state, DeoptimizeReason::kOverflow,
                          feedback);
          return __ Projection(result, 0, RegisterRepresentation::Word64());
        }
      case WordBinopDeoptOnOverflowOp::Kind::kSignedDiv:
        if (rep == WordRepresentation::Word32()) {
          // Check if the {rhs} is a known power of two.
          int32_t divisor;
          if (__ matcher().MatchPowerOfTwoWord32Constant(right, &divisor)) {
            // Since we know that {rhs} is a power of two, we can perform a fast
            // check to see if the relevant least significant bits of the {lhs}
            // are all zero, and if so we know that we can perform a division
            // safely (and fast by doing an arithmetic - aka sign preserving -
            // right shift on {lhs}).
            V<Word32> check =
                __ Word32Equal(__ Word32BitwiseAnd(left, divisor - 1), 0);
            __ DeoptimizeIfNot(check, frame_state,
                               DeoptimizeReason::kLostPrecision, feedback);
            return __ Word32ShiftRightArithmeticShiftOutZeros(
                left, base::bits::WhichPowerOfTwo(divisor));
          } else {
            Label<Word32> done(this);

            // Check if {rhs} is positive (and not zero).
            IF (__ Int32LessThan(0, right)) {
              GOTO(done, __ Int32Div(left, right));
            } ELSE {
              // Check if {rhs} is zero.
              __ DeoptimizeIf(__ Word32Equal(right, 0), frame_state,
                              DeoptimizeReason::kDivisionByZero, feedback);

              // Check if {lhs} is zero, as that would produce minus zero.
              __ DeoptimizeIf(__ Word32Equal(left, 0), frame_state,
                              DeoptimizeReason::kMinusZero, feedback);

              // Check if {lhs} is kMinInt and {rhs} is -1, in which case we'd
              // have to return -kMinInt, which is not representable as Word32.
              IF (UNLIKELY(__ Word32Equal(left, kMinInt))) {
                __ DeoptimizeIf(__ Word32Equal(right, -1), frame_state,
                                DeoptimizeReason::kOverflow, feedback);
              }

              GOTO(done, __ Int32Div(left, right));
            }

            BIND(done, value);
            V<Word32> lossless =
                __ Word32Equal(left, __ Word32Mul(value, right));
            __ DeoptimizeIfNot(lossless, frame_state,
                               DeoptimizeReason::kLostPrecision, feedback);
            return value;
          }
        } else {
          DCHECK_EQ(rep, WordRepresentation::Word64());
          DCHECK(Is64());

          __ DeoptimizeIf(__ Word64Equal(right, 0), frame_state,
                          DeoptimizeReason::kDivisionByZero, feedback);
          // Check if {lhs} is kMinInt64 and {rhs} is -1, in which case we'd
          // have to return -kMinInt64, which is not representable as Word64.
          IF (UNLIKELY(
                  __ Word64Equal(left, std::numeric_limits<int64_t>::min()))) {
            __ DeoptimizeIf(__ Word64Equal(right, int64_t{-1}), frame_state,
                            DeoptimizeReason::kOverflow, feedback);
          }

          return __ Int64Div(left, right);
        }
      case WordBinopDeoptOnOverflowOp::Kind::kSignedMod:
        if (rep == WordRepresentation::Word32()) {
          // General case for signed integer modulus, with optimization for
          // (unknown) power of 2 right hand side.
          //
          //   if rhs <= 0 then
          //     rhs = -rhs
          //     deopt if rhs == 0
          //   if lhs < 0 then
          //     let lhs_abs = -lhs in
          //     let res = lhs_abs % rhs in
          //     deopt if res == 0
          //     -res
          //   else
          //     let msk = rhs - 1 in
          //     if rhs & msk == 0 then
          //       lhs & msk
          //     else
          //       lhs % rhs
          //
          Label<Word32> rhs_checked(this);
          Label<Word32> done(this);

          // Check if {rhs} is not strictly positive.
          IF (__ Int32LessThanOrEqual(right, 0)) {
            // Negate {rhs}, might still produce a negative result in case of
            // -2^31, but that is handled safely below.
            V<Word32> temp = __ Word32Sub(0, right);

            // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
            __ DeoptimizeIfNot(temp, frame_state,
                               DeoptimizeReason::kDivisionByZero, feedback);
            GOTO(rhs_checked, temp);
          } ELSE {
            GOTO(rhs_checked, right);
          }

          BIND(rhs_checked, rhs_value);

          IF (__ Int32LessThan(left, 0)) {
            // The {lhs} is a negative integer. This is very unlikely and
            // we intentionally don't use the BuildUint32Mod() here, which
            // would try to figure out whether {rhs} is a power of two,
            // since this is intended to be a slow-path.
            V<Word32> temp = __ Uint32Mod(__ Word32Sub(0, left), rhs_value);

            // Check if we would have to return -0.
            __ DeoptimizeIf(__ Word32Equal(temp, 0), frame_state,
                            DeoptimizeReason::kMinusZero, feedback);
            GOTO(done, __ Word32Sub(0, temp));
          } ELSE {
            // The {lhs} is a non-negative integer.
            GOTO(done, BuildUint32Mod(left, rhs_value));
          }

          BIND(done, result);
          return result;
        } else {
          DCHECK_EQ(rep, WordRepresentation::Word64());
          DCHECK(Is64());

          __ DeoptimizeIf(__ Word64Equal(right, 0), frame_state,
                          DeoptimizeReason::kDivisionByZero, feedback);

          // While the mod-result cannot overflow, the underlying instruction is
          // `idiv` and will trap when the accompanying div-result overflows.
          IF (UNLIKELY(
                  __ Word64Equal(left, std::numeric_limits<int64_t>::min()))) {
            __ DeoptimizeIf(__ Word64Equal(right, int64_t{-1}), frame_state,
                            DeoptimizeReason::kOverflow, feedback);
          }

          return __ Int64Mod(left, right);
        }
      case WordBinopDeoptOnOverflowOp::Kind::kUnsignedDiv: {
        DCHECK_EQ(rep, WordRepresentation::Word32());

        // Check if the {rhs} is a known power of two.
        int32_t divisor;
        if (__ matcher().MatchPowerOfTwoWord32Constant(right, &divisor)) {
          // Since we know that {rhs} is a power of two, we can perform a fast
          // check to see if the relevant least significant bits of the {lhs}
          // are all zero, and if so we know that we can perform a division
          // safely (and fast by doing a logical - aka zero extending - right
          // shift on {lhs}).
          V<Word32> check =
              __ Word32Equal(__ Word32BitwiseAnd(left, divisor - 1), 0);
          __ DeoptimizeIfNot(check, frame_state,
                             DeoptimizeReason::kLostPrecision, feedback);
          return __ Word32ShiftRightLogical(
              left, base::bits::WhichPowerOfTwo(divisor));
        } else {
          // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
          __ DeoptimizeIf(__ Word32Equal(right, 0), frame_state,
                          DeoptimizeReason::kDivisionByZero, feedback);

          // Perform the actual unsigned integer division.
          V<Word32> value = __ Uint32Div(left, right);

          // Check if the remainder is non-zero.
          V<Word32> lossless = __ Word32Equal(left, __ Word32Mul(right, value));
          __ DeoptimizeIfNot(lossless, frame_state,
                             DeoptimizeReason::kLostPrecision, feedback);
          return value;
        }
      }
      case WordBinopDeoptOnOverflowOp::Kind::kUnsignedMod: {
        DCHECK_EQ(rep, WordRepresentation::Word32());
        // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
        __ DeoptimizeIf(__ Word32Equal(right, 0), frame_state,
                        DeoptimizeReason::kDivisionByZero, feedback);

        return BuildUint32Mod(left, right);
      }
    }
  }

  OpIndex REDUCE(BigIntBinop)(V<Object> left, V<Object> right,
                              OpIndex frame_state, BigIntBinopOp::Kind kind) {
    const Builtin builtin = GetBuiltinForBigIntBinop(kind);
    switch (kind) {
      case BigIntBinopOp::Kind::kAdd:
      case BigIntBinopOp::Kind::kSub:
      case BigIntBinopOp::Kind::kBitwiseAnd:
      case BigIntBinopOp::Kind::kBitwiseXor:
      case BigIntBinopOp::Kind::kShiftLeft:
      case BigIntBinopOp::Kind::kShiftRightArithmetic: {
        V<Object> result = CallBuiltinForBigIntOp(builtin, {left, right});

        // Check for exception sentinel: Smi 0 is returned to signal
        // BigIntTooBig.
        __ DeoptimizeIf(__ ObjectIsSmi(result), frame_state,
                        DeoptimizeReason::kBigIntTooBig, FeedbackSource{});
        return result;
      }
      case BigIntBinopOp::Kind::kMul:
      case BigIntBinopOp::Kind::kDiv:
      case BigIntBinopOp::Kind::kMod: {
        V<Object> result = CallBuiltinForBigIntOp(builtin, {left, right});

        // Check for exception sentinel: Smi 1 is returned to signal
        // TerminationRequested.
        IF (UNLIKELY(__ TaggedEqual(result, __ TagSmi(1)))) {
          __ CallRuntime_TerminateExecution(isolate_, frame_state,
                                            __ NoContextConstant());
        }

        // Check for exception sentinel: Smi 0 is returned to signal
        // BigIntTooBig or DivisionByZero.
        __ DeoptimizeIf(__ ObjectIsSmi(result), frame_state,
                        kind == BigIntBinopOp::Kind::kMul
                            ? DeoptimizeReason::kBigIntTooBig
                            : DeoptimizeReason::kDivisionByZero,
                        FeedbackSource{});
        return result;
      }
      case BigIntBinopOp::Kind::kBitwiseOr: {
        return CallBuiltinForBigIntOp(builtin, {left, right});
      }
      default:
        UNIMPLEMENTED();
    }
    UNREACHABLE();
  }

  V<Boolean> REDUCE(BigIntComparison)(V<Object> left, V<Object> right,
                                      BigIntComparisonOp::Kind kind) {
    switch (kind) {
      case BigIntComparisonOp::Kind::kEqual:
        return CallBuiltinForBigIntOp(Builtin::kBigIntEqual, {left, right});
      case BigIntComparisonOp::Kind::kLessThan:
        return CallBuiltinForBigIntOp(Builtin::kBigIntLessThan, {left, right});
      case BigIntComparisonOp::Kind::kLessThanOrEqual:
        return CallBuiltinForBigIntOp(Builtin::kBigIntLessThanOrEqual,
                                      {left, right});
    }
  }

  V<Object> REDUCE(BigIntUnary)(V<Object> input, BigIntUnaryOp::Kind kind) {
    DCHECK_EQ(kind, BigIntUnaryOp::Kind::kNegate);
    return CallBuiltinForBigIntOp(Builtin::kBigIntUnaryMinus, {input});
  }

  V<Word32> REDUCE(StringAt)(V<String> string, V<WordPtr> pos,
                             StringAtOp::Kind kind) {
    if (kind == StringAtOp::Kind::kCharCode) {
      Label<Word32> done(this);

      if (const ConstantOp* cst =
              __ matcher().template TryCast<ConstantOp>(string);
          cst && cst->kind == ConstantOp::Kind::kHeapObject) {
        // For constant SeqString, we have a fast-path that doesn't run through
        // the loop. It requires fewer loads (we only load the map once, but not
        // the instance type), uses static 1/2-byte, and only uses a single
        // comparison to check that the string has indeed the correct SeqString
        // map.
        UnparkedScopeIfNeeded unpark(broker_);
        HeapObjectRef ref = MakeRef(broker_, cst->handle());
        if (ref.IsString()) {
          StringRef str = ref.AsString();
          if (str.IsSeqString()) {
            V<Map> dynamic_map = __ LoadMapField(string);
            Handle<Map> expected_map = str.map(broker_).object();
            IF (__ TaggedEqual(dynamic_map, __ HeapConstant(expected_map))) {
              bool one_byte = str.IsOneByteRepresentation();
              GOTO(done,
                   LoadFromSeqString(string, pos, __ Word32Constant(one_byte)));
            }
          }
        }
      }

      // TODO(dmercadier): the runtime label should be deferred, and because
      // Labels/Blocks don't have deferred annotation, we achieve this by
      // marking all branches to this Label as UNLIKELY, but 1) it's easy to
      // forget one, and 2) it makes the code less clear: `if(x) {} else
      // if(likely(y)) {} else {}` looks like `y` is more likely than `x`, but
      // it just means that `y` is more likely than `!y`.
      Label<> runtime(this);
      // We need a loop here to properly deal with indirect strings
      // (SlicedString, ConsString and ThinString).
      LoopLabel<> loop(this);
      ScopedVar<String> receiver(this, string);
      ScopedVar<WordPtr> position(this, pos);
      GOTO(loop);

      BIND_LOOP(loop) {
        V<Map> map = __ LoadMapField(receiver);
        V<Word32> instance_type = __ LoadInstanceTypeField(map);
        V<Word32> representation =
            __ Word32BitwiseAnd(instance_type, kStringRepresentationMask);

        IF (__ Int32LessThanOrEqual(representation, kConsStringTag)) {
          {
            // if_lessthanoreq_cons
            IF (__ Word32Equal(representation, kConsStringTag)) {
              // if_consstring
              V<String> second = __ template LoadField<String>(
                  receiver, AccessBuilder::ForConsStringSecond());
              GOTO_IF_NOT(
                  LIKELY(__ TaggedEqual(
                      second, __ HeapConstant(factory_->empty_string()))),
                  runtime);
              receiver = __ template LoadField<String>(
                  receiver, AccessBuilder::ForConsStringFirst());
              GOTO(loop);
            } ELSE {
              // if_seqstring
              V<Word32> onebyte = __ Word32Equal(
                  __ Word32BitwiseAnd(instance_type, kStringEncodingMask),
                  kOneByteStringTag);
              GOTO(done, LoadFromSeqString(receiver, position, onebyte));
            }
          }
        } ELSE {
          // if_greaterthan_cons
          {
            IF (__ Word32Equal(representation, kThinStringTag)) {
              // if_thinstring
              receiver = __ template LoadField<String>(
                  receiver, AccessBuilder::ForThinStringActual());
              GOTO(loop);
            } ELSE IF (__ Word32Equal(representation, kExternalStringTag)) {
              // if_externalstring
              // We need to bailout to the runtime for uncached external
              // strings.
              GOTO_IF(UNLIKELY(__ Word32Equal(
                          __ Word32BitwiseAnd(instance_type,
                                              kUncachedExternalStringMask),
                          kUncachedExternalStringTag)),
                      runtime);

              OpIndex data = __ LoadField(
                  receiver, AccessBuilder::ForExternalStringResourceData());
              IF (__ Word32Equal(
                      __ Word32BitwiseAnd(instance_type, kStringEncodingMask),
                      kTwoByteStringTag)) {
                // if_twobyte
                constexpr uint8_t twobyte_size_log2 = 1;
                V<Word32> value = __ Load(
                    data, position,
                    LoadOp::Kind::Aligned(BaseTaggedness::kUntaggedBase),
                    MemoryRepresentation::Uint16(), 0, twobyte_size_log2);
                GOTO(done, value);
              } ELSE {
                // if_onebyte
                constexpr uint8_t onebyte_size_log2 = 0;
                V<Word32> value = __ Load(
                    data, position,
                    LoadOp::Kind::Aligned(BaseTaggedness::kUntaggedBase),
                    MemoryRepresentation::Uint8(), 0, onebyte_size_log2);
                GOTO(done, value);
              }
            } ELSE IF (LIKELY(
                          __ Word32Equal(representation, kSlicedStringTag))) {
              // if_slicedstring
              V<Smi> offset = __ template LoadField<Smi>(
                  receiver, AccessBuilder::ForSlicedStringOffset());
              receiver = __ template LoadField<String>(
                  receiver, AccessBuilder::ForSlicedStringParent());
              position = __ WordPtrAdd(
                  position, __ ChangeInt32ToIntPtr(__ UntagSmi(offset)));
              GOTO(loop);
            } ELSE {
              GOTO(runtime);
            }
          }
        }

        if (BIND(runtime)) {
          V<Word32> value =
              __ UntagSmi(V<Smi>::Cast(__ CallRuntime_StringCharCodeAt(
                  isolate_, __ NoContextConstant(), receiver,
                  __ TagSmi(__ TruncateWordPtrToWord32(position)))));
          GOTO(done, value);
        }
      }

      BIND(done, result);
      return result;
    } else {
      DCHECK_EQ(kind, StringAtOp::Kind::kCodePoint);
      Label<Word32> done(this);

      V<Word32> first_code_unit = __ StringCharCodeAt(string, pos);
      GOTO_IF_NOT(UNLIKELY(__ Word32Equal(
                      __ Word32BitwiseAnd(first_code_unit, 0xFC00), 0xD800)),
                  done, first_code_unit);
      V<WordPtr> length =
          __ ChangeUint32ToUintPtr(__ template LoadField<Word32>(
              string, AccessBuilder::ForStringLength()));
      V<WordPtr> next_index = __ WordPtrAdd(pos, 1);
      GOTO_IF_NOT(__ IntPtrLessThan(next_index, length), done, first_code_unit);

      V<Word32> second_code_unit = __ StringCharCodeAt(string, next_index);
      GOTO_IF_NOT(
          __ Word32Equal(__ Word32BitwiseAnd(second_code_unit, 0xFC00), 0xDC00),
          done, first_code_unit);

      const int32_t surrogate_offset = 0x10000 - (0xD800 << 10) - 0xDC00;
      V<Word32> value =
          __ Word32Add(__ Word32ShiftLeft(first_code_unit, 10),
                       __ Word32Add(second_code_unit, surrogate_offset));
      GOTO(done, value);

      BIND(done, result);
      return result;
    }

    UNREACHABLE();
  }

  V<Word32> REDUCE(StringLength)(V<String> string) {
    return __ template LoadField<Word32>(string,
                                         AccessBuilder::ForStringLength());
  }

  V<Smi> REDUCE(StringIndexOf)(V<String> string, V<String> search,
                               V<Smi> position) {
    return __ CallBuiltin_StringIndexOf(isolate_, string, search, position);
  }

  V<String> REDUCE(StringFromCodePointAt)(V<String> string, V<WordPtr> index) {
    return __ CallBuiltin_StringFromCodePointAt(isolate_, string, index);
  }

#ifdef V8_INTL_SUPPORT
  V<String> REDUCE(StringToCaseIntl)(V<String> string,
                                     StringToCaseIntlOp::Kind kind) {
    if (kind == StringToCaseIntlOp::Kind::kLower) {
      return __ CallBuiltin_StringToLowerCaseIntl(
          isolate_, __ NoContextConstant(), string);
    } else {
      DCHECK_EQ(kind, StringToCaseIntlOp::Kind::kUpper);
      return __ CallRuntime_StringToUpperCaseIntl(
          isolate_, __ NoContextConstant(), string);
    }
  }
#endif  // V8_INTL_SUPPORT

  V<String> REDUCE(StringSubstring)(V<String> string, V<Word32> start,
                                    V<Word32> end) {
    V<WordPtr> s = __ ChangeInt32ToIntPtr(start);
    V<WordPtr> e = __ ChangeInt32ToIntPtr(end);
    return __ CallBuiltin_StringSubstring(isolate_, string, s, e);
  }

  V<String> REDUCE(StringConcat)(V<String> left, V<String> right) {
    // TODO(nicohartmann@): Port StringBuilder once it is stable.
    return __ CallBuiltin_StringAdd_CheckNone(isolate_, __ NoContextConstant(),
                                              left, right);
  }

  V<Boolean> REDUCE(StringComparison)(V<String> left, V<String> right,
                                      StringComparisonOp::Kind kind) {
    switch (kind) {
      case StringComparisonOp::Kind::kEqual: {
        Label<Boolean> done(this);

        GOTO_IF(__ TaggedEqual(left, right), done,
                __ HeapConstant(factory_->true_value()));

        V<Word32> left_length = __ template LoadField<Word32>(
            left, AccessBuilder::ForStringLength());
        V<Word32> right_length = __ template LoadField<Word32>(
            right, AccessBuilder::ForStringLength());
        IF (__ Word32Equal(left_length, right_length)) {
          GOTO(done,
               __ CallBuiltin_StringEqual(isolate_, left, right,
                                          __ ChangeInt32ToIntPtr(left_length)));
        } ELSE {
          GOTO(done, __ HeapConstant(factory_->false_value()));
        }

        BIND(done, result);
        return result;
      }
      case StringComparisonOp::Kind::kLessThan:
        return __ CallBuiltin_StringLessThan(isolate_, left, right);
      case StringComparisonOp::Kind::kLessThanOrEqual:
        return __ CallBuiltin_StringLessThanOrEqual(isolate_, left, right);
    }
  }

  V<Smi> REDUCE(ArgumentsLength)(ArgumentsLengthOp::Kind kind,
                                 int formal_parameter_count) {
    V<WordPtr> count =
        __ LoadOffHeap(__ FramePointer(), StandardFrameConstants::kArgCOffset,
                       MemoryRepresentation::UintPtr());
    V<WordPtr> arguments_length = __ WordPtrSub(count, kJSArgcReceiverSlots);

    if (kind == ArgumentsLengthOp::Kind::kArguments) {
      return __ TagSmi(__ TruncateWordPtrToWord32(arguments_length));
    } else {
      DCHECK_EQ(kind, ArgumentsLengthOp::Kind::kRest);
      V<WordPtr> rest_length =
          __ WordPtrSub(arguments_length, formal_parameter_count);
      Label<WordPtr> done(this);
      IF (__ IntPtrLessThan(rest_length, 0)) {
        GOTO(done, 0);
      } ELSE {
        GOTO(done, rest_length);
      }

      BIND(done, value);
      return __ TagSmi(__ TruncateWordPtrToWord32(value));
    }
  }

  V<Object> REDUCE(NewArgumentsElements)(V<Smi> arguments_count,
                                         CreateArgumentsType type,
                                         int formal_parameter_count) {
    V<WordPtr> frame = __ FramePointer();
    V<WordPtr> p_count = __ IntPtrConstant(formal_parameter_count);
    switch (type) {
      case CreateArgumentsType::kMappedArguments:
        return __ CallBuiltin_NewSloppyArgumentsElements(
            isolate_, frame, p_count, arguments_count);
      case CreateArgumentsType::kUnmappedArguments:
        return __ CallBuiltin_NewStrictArgumentsElements(
            isolate_, frame, p_count, arguments_count);
      case CreateArgumentsType::kRestParameter:
        return __ CallBuiltin_NewRestArgumentsElements(isolate_, frame, p_count,
                                                       arguments_count);
    }
  }

  OpIndex REDUCE(LoadTypedElement)(OpIndex buffer, V<Object> base,
                                   V<WordPtr> external, V<WordPtr> index,
                                   ExternalArrayType array_type) {
    V<WordPtr> data_ptr = BuildTypedArrayDataPointer(base, external);

    // Perform the actual typed element access.
    OpIndex result = __ LoadArrayBufferElement(
        data_ptr, AccessBuilder::ForTypedArrayElement(array_type, true), index);

    // We need to keep the {buffer} alive so that the GC will not release the
    // ArrayBuffer (if there's any) as long as we are still operating on it.
    __ Retain(buffer);
    return result;
  }

  V<Object> REDUCE(LoadStackArgument)(V<WordPtr> base, V<WordPtr> index) {
    V<WordPtr> argument = __ template LoadNonArrayBufferElement<WordPtr>(
        base, AccessBuilder::ForStackArgument(), index);
    return __ BitcastWordPtrToTagged(argument);
  }

  OpIndex REDUCE(StoreTypedElement)(OpIndex buffer, V<Object> base,
                                    V<WordPtr> external, V<WordPtr> index,
                                    OpIndex value,
                                    ExternalArrayType array_type) {
    V<WordPtr> data_ptr = BuildTypedArrayDataPointer(base, external);

    // Perform the actual typed element access.
    __ StoreArrayBufferElement(
        data_ptr, AccessBuilder::ForTypedArrayElement(array_type, true), index,
        value);

    // We need to keep the {buffer} alive so that the GC will not release the
    // ArrayBuffer (if there's any) as long as we are still operating on it.
    __ Retain(buffer);
    return {};
  }

  OpIndex REDUCE(TransitionAndStoreArrayElement)(
      V<JSArray> array, V<WordPtr> index, OpIndex value,
      TransitionAndStoreArrayElementOp::Kind kind, MaybeHandle<Map> fast_map,
      MaybeHandle<Map> double_map) {
    V<Map> map = __ LoadMapField(array);
    V<Word32> bitfield2 =
        __ template LoadField<Word32>(map, AccessBuilder::ForMapBitField2());
    V<Word32> elements_kind = __ Word32ShiftRightLogical(
        __ Word32BitwiseAnd(bitfield2, Map::Bits2::ElementsKindBits::kMask),
        Map::Bits2::ElementsKindBits::kShift);

    switch (kind) {
      case TransitionAndStoreArrayElementOp::Kind::kElement: {
        // Possibly transition array based on input and store.
        //
        //   -- TRANSITION PHASE -----------------
        //   kind = ElementsKind(array)
        //   if value is not smi {
        //     if kind == HOLEY_SMI_ELEMENTS {
        //       if value is heap number {
        //         Transition array to HOLEY_DOUBLE_ELEMENTS
        //         kind = HOLEY_DOUBLE_ELEMENTS
        //       } else {
        //         Transition array to HOLEY_ELEMENTS
        //         kind = HOLEY_ELEMENTS
        //       }
        //     } else if kind == HOLEY_DOUBLE_ELEMENTS {
        //       if value is not heap number {
        //         Transition array to HOLEY_ELEMENTS
        //         kind = HOLEY_ELEMENTS
        //       }
        //     }
        //   }
        //
        //   -- STORE PHASE ----------------------
        //   [make sure {kind} is up-to-date]
        //   if kind == HOLEY_DOUBLE_ELEMENTS {
        //     if value is smi {
        //       float_value = convert smi to float
        //       Store array[index] = float_value
        //     } else {
        //       float_value = value
        //       Store array[index] = float_value
        //     }
        //   } else {
        //     // kind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS
        //     Store array[index] = value
        //   }
        //
        Label<Word32> do_store(this);
        // We can store a smi anywhere.
        GOTO_IF(__ ObjectIsSmi(value), do_store, elements_kind);

        // {value} is a HeapObject.
        IF_NOT (LIKELY(__ Int32LessThan(HOLEY_SMI_ELEMENTS, elements_kind))) {
          // Transition {array} from HOLEY_SMI_ELEMENTS to HOLEY_DOUBLE_ELEMENTS
          // or to HOLEY_ELEMENTS.
          V<Map> value_map = __ LoadMapField(value);
          IF (__ TaggedEqual(value_map,
                             __ HeapConstant(factory_->heap_number_map()))) {
            // {value} is a HeapNumber.
            TransitionElementsTo(array, HOLEY_SMI_ELEMENTS,
                                 HOLEY_DOUBLE_ELEMENTS,
                                 double_map.ToHandleChecked());
            GOTO(do_store, HOLEY_DOUBLE_ELEMENTS);
          } ELSE {
            TransitionElementsTo(array, HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS,
                                 fast_map.ToHandleChecked());
            GOTO(do_store, HOLEY_ELEMENTS);
          }
        }

        GOTO_IF_NOT(LIKELY(__ Int32LessThan(HOLEY_ELEMENTS, elements_kind)),
                    do_store, elements_kind);

        // We have double elements kind. Only a HeapNumber can be stored
        // without effecting a transition.
        V<Map> value_map = __ LoadMapField(value);
        IF_NOT (UNLIKELY(__ TaggedEqual(
                    value_map, __ HeapConstant(factory_->heap_number_map())))) {
          TransitionElementsTo(array, HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS,
                               fast_map.ToHandleChecked());
          GOTO(do_store, HOLEY_ELEMENTS);
        }

        GOTO(do_store, elements_kind);

        BIND(do_store, store_kind);
        V<Object> elements = __ template LoadField<Object>(
            array, AccessBuilder::ForJSObjectElements());
        IF (__ Int32LessThan(HOLEY_ELEMENTS, store_kind)) {
          // Our ElementsKind is HOLEY_DOUBLE_ELEMENTS.
          IF (__ ObjectIsSmi(value)) {
            V<Float64> float_value =
                __ ChangeInt32ToFloat64(__ UntagSmi(value));
            __ StoreNonArrayBufferElement(
                elements, AccessBuilder::ForFixedDoubleArrayElement(), index,
                float_value);
          } ELSE {
            V<Float64> float_value = __ template LoadField<Float64>(
                V<HeapObject>::Cast(value),
                AccessBuilder::ForHeapNumberValue());
            __ StoreNonArrayBufferElement(
                elements, AccessBuilder::ForFixedDoubleArrayElement(), index,
                __ Float64SilenceNaN(float_value));
          }
        } ELSE {
          // Our ElementsKind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS.
          __ StoreNonArrayBufferElement(
              elements, AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS),
              index, value);
        }

        break;
      }
      case TransitionAndStoreArrayElementOp::Kind::kNumberElement: {
        Label<> done(this);
        // Possibly transition array based on input and store.
        //
        //   -- TRANSITION PHASE -----------------
        //   kind = ElementsKind(array)
        //   if kind == HOLEY_SMI_ELEMENTS {
        //     Transition array to HOLEY_DOUBLE_ELEMENTS
        //   } else if kind != HOLEY_DOUBLE_ELEMENTS {
        //     if kind == HOLEY_ELEMENTS {
        //       Store value as a HeapNumber in array[index].
        //     } else {
        //       This is UNREACHABLE, execute a debug break.
        //     }
        //   }
        //
        //   -- STORE PHASE ----------------------
        //   Store array[index] = value (it's a float)
        //
        // {value} is a float64.
        IF_NOT (LIKELY(__ Int32LessThan(HOLEY_SMI_ELEMENTS, elements_kind))) {
          // Transition {array} from HOLEY_SMI_ELEMENTS to
          // HOLEY_DOUBLE_ELEMENTS.
          TransitionElementsTo(array, HOLEY_SMI_ELEMENTS, HOLEY_DOUBLE_ELEMENTS,
                               double_map.ToHandleChecked());
        } ELSE {
          // We expect that our input array started at HOLEY_SMI_ELEMENTS, and
          // climbs the lattice up to HOLEY_DOUBLE_ELEMENTS. However, loop
          // peeling can break this assumption, because in the peeled iteration,
          // the array might have transitioned to HOLEY_ELEMENTS kind, so we
          // handle this as well.
          IF_NOT (LIKELY(
                      __ Word32Equal(elements_kind, HOLEY_DOUBLE_ELEMENTS))) {
            IF (__ Word32Equal(elements_kind, HOLEY_ELEMENTS)) {
              V<Object> elements = __ template LoadField<Object>(
                  array, AccessBuilder::ForJSObjectElements());
              // Our ElementsKind is HOLEY_ELEMENTS.
              __ StoreNonArrayBufferElement(
                  elements, AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS),
                  index, AllocateHeapNumberWithValue(value));
              GOTO(done);
            }

            __ Unreachable();
          }
        }

        V<Object> elements = __ template LoadField<Object>(
            array, AccessBuilder::ForJSObjectElements());
        __ StoreNonArrayBufferElement(
            elements, AccessBuilder::ForFixedDoubleArrayElement(), index,
            __ Float64SilenceNaN(value));
        GOTO(done);

        BIND(done);
        break;
      }
      case TransitionAndStoreArrayElementOp::Kind::kOddballElement:
      case TransitionAndStoreArrayElementOp::Kind::kNonNumberElement: {
        // Possibly transition array based on input and store.
        //
        //   -- TRANSITION PHASE -----------------
        //   kind = ElementsKind(array)
        //   if kind == HOLEY_SMI_ELEMENTS {
        //     Transition array to HOLEY_ELEMENTS
        //   } else if kind == HOLEY_DOUBLE_ELEMENTS {
        //     Transition array to HOLEY_ELEMENTS
        //   }
        //
        //   -- STORE PHASE ----------------------
        //   // kind is HOLEY_ELEMENTS
        //   Store array[index] = value
        //
        IF_NOT (LIKELY(__ Int32LessThan(HOLEY_SMI_ELEMENTS, elements_kind))) {
          // Transition {array} from HOLEY_SMI_ELEMENTS to HOLEY_ELEMENTS.
          TransitionElementsTo(array, HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS,
                               fast_map.ToHandleChecked());
        } ELSE IF (UNLIKELY(__ Int32LessThan(HOLEY_ELEMENTS, elements_kind))) {
          TransitionElementsTo(array, HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS,
                               fast_map.ToHandleChecked());
        }

        V<Object> elements = __ template LoadField<Object>(
            array, AccessBuilder::ForJSObjectElements());
        ElementAccess access =
            AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS);
        if (kind == TransitionAndStoreArrayElementOp::Kind::kOddballElement) {
          access.type = compiler::Type::BooleanOrNullOrUndefined();
          access.write_barrier_kind = kNoWriteBarrier;
        }
        __ StoreNonArrayBufferElement(elements, access, index, value);
        break;
      }
      case TransitionAndStoreArrayElementOp::Kind::kSignedSmallElement: {
        // Store a signed small in an output array.
        //
        //   kind = ElementsKind(array)
        //
        //   -- STORE PHASE ----------------------
        //   if kind == HOLEY_DOUBLE_ELEMENTS {
        //     float_value = convert int32 to float
        //     Store array[index] = float_value
        //   } else {
        //     // kind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS
        //     smi_value = convert int32 to smi
        //     Store array[index] = smi_value
        //   }
        //
        V<Object> elements = __ template LoadField<Object>(
            array, AccessBuilder::ForJSObjectElements());
        IF (__ Int32LessThan(HOLEY_ELEMENTS, elements_kind)) {
          // Our ElementsKind is HOLEY_DOUBLE_ELEMENTS.
          V<Float64> f64 = __ ChangeInt32ToFloat64(value);
          __ StoreNonArrayBufferElement(
              elements, AccessBuilder::ForFixedDoubleArrayElement(), index,
              f64);
        } ELSE {
          // Our ElementsKind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS.
          // In this case, we know our value is a signed small, and we can
          // optimize the ElementAccess information.
          ElementAccess access = AccessBuilder::ForFixedArrayElement();
          access.type = compiler::Type::SignedSmall();
          access.machine_type = MachineType::TaggedSigned();
          access.write_barrier_kind = kNoWriteBarrier;
          __ StoreNonArrayBufferElement(elements, access, index,
                                        __ TagSmi(value));
        }

        break;
      }
    }

    return OpIndex::Invalid();
  }

  V<Word32> REDUCE(CompareMaps)(V<HeapObject> heap_object,
                                const ZoneRefSet<Map>& maps) {
    return CompareMapAgainstMultipleMaps(__ LoadMapField(heap_object), maps);
  }

  OpIndex REDUCE(CheckMaps)(V<HeapObject> heap_object, OpIndex frame_state,
                            const ZoneRefSet<Map>& maps, CheckMapsFlags flags,
                            const FeedbackSource& feedback) {
    if (maps.is_empty()) {
      __ Deoptimize(frame_state, DeoptimizeReason::kWrongMap, feedback);
      return OpIndex::Invalid();
    }

    if (flags & CheckMapsFlag::kTryMigrateInstance) {
      V<Map> heap_object_map = __ LoadMapField(heap_object);
      IF_NOT (LIKELY(CompareMapAgainstMultipleMaps(heap_object_map, maps))) {
        // Reloading the map slightly reduces register pressure, and we are on a
        // slow path here anyway.
        MigrateInstanceOrDeopt(heap_object, __ LoadMapField(heap_object),
                               frame_state, feedback);
        __ DeoptimizeIfNot(__ CompareMaps(heap_object, maps), frame_state,
                           DeoptimizeReason::kWrongMap, feedback);
      }
    } else {
      __ DeoptimizeIfNot(__ CompareMaps(heap_object, maps), frame_state,
                         DeoptimizeReason::kWrongMap, feedback);
    }
    // Inserting a AssumeMap so that subsequent optimizations know the map of
    // this object.
    __ AssumeMap(heap_object, maps);
    return OpIndex::Invalid();
  }

  OpIndex REDUCE(FloatUnary)(OpIndex input, FloatUnaryOp::Kind kind,
                             FloatRepresentation rep) {
    LABEL_BLOCK(no_change) { return Next::ReduceFloatUnary(input, kind, rep); }
    switch (kind) {
      case FloatUnaryOp::Kind::kRoundUp:
      case FloatUnaryOp::Kind::kRoundDown:
      case FloatUnaryOp::Kind::kRoundTiesEven:
      case FloatUnaryOp::Kind::kRoundToZero: {
        // TODO(14108): Implement for Float32.
        if (rep == FloatRepresentation::Float32()) {
          goto no_change;
        }
        DCHECK_EQ(rep, FloatRepresentation::Float64());
        if (FloatUnaryOp::IsSupported(kind, rep)) {
          // If we have a fast machine operation for this, we can just keep it.
          goto no_change;
        }
        // Otherwise we have to lower it.
        V<Float64> two_52 = __ Float64Constant(4503599627370496.0E0);
        V<Float64> minus_two_52 = __ Float64Constant(-4503599627370496.0E0);

        if (kind == FloatUnaryOp::Kind::kRoundUp) {
          // General case for ceil.
          //
          //   if 0.0 < input then
          //     if 2^52 <= input then
          //       input
          //     else
          //       let temp1 = (2^52 + input) - 2^52 in
          //       if temp1 < input then
          //         temp1 + 1
          //       else
          //         temp1
          //   else
          //     if input == 0 then
          //       input
          //     else
          //       if input <= -2^52 then
          //         input
          //       else
          //         let temp1 = -0 - input in
          //         let temp2 = (2^52 + temp1) - 2^52 in
          //         if temp1 < temp2 then -0 - (temp2 - 1) else -0 - temp2

          Label<Float64> done(this);

          IF (LIKELY(__ Float64LessThan(0.0, input))) {
            GOTO_IF(UNLIKELY(__ Float64LessThanOrEqual(two_52, input)), done,
                    input);
            V<Float64> temp1 =
                __ Float64Sub(__ Float64Add(two_52, input), two_52);
            GOTO_IF_NOT(__ Float64LessThan(temp1, input), done, temp1);
            GOTO(done, __ Float64Add(temp1, 1.0));
          } ELSE IF (UNLIKELY(__ Float64Equal(input, 0.0))) {
            GOTO(done, input);
          } ELSE IF (UNLIKELY(__ Float64LessThanOrEqual(input, minus_two_52))) {
            GOTO(done, input);
          } ELSE {
            V<Float64> temp1 = __ Float64Sub(-0.0, input);
            V<Float64> temp2 =
                __ Float64Sub(__ Float64Add(two_52, temp1), two_52);
            GOTO_IF_NOT(__ Float64LessThan(temp1, temp2), done,
                        __ Float64Sub(-0.0, temp2));
            GOTO(done, __ Float64Sub(-0.0, __ Float64Sub(temp2, 1.0)));
          }

          BIND(done, result);
          return result;
        } else if (kind == FloatUnaryOp::Kind::kRoundDown) {
          // General case for floor.
          //
          //   if 0.0 < input then
          //     if 2^52 <= input then
          //       input
          //     else
          //       let temp1 = (2^52 + input) - 2^52 in
          //       if input < temp1 then
          //         temp1 - 1
          //       else
          //         temp1
          //   else
          //     if input == 0 then
          //       input
          //     else
          //       if input <= -2^52 then
          //         input
          //       else
          //         let temp1 = -0 - input in
          //         let temp2 = (2^52 + temp1) - 2^52 in
          //         if temp2 < temp1 then
          //           -1 - temp2
          //         else
          //           -0 - temp2

          Label<Float64> done(this);

          IF (LIKELY(__ Float64LessThan(0.0, input))) {
            GOTO_IF(UNLIKELY(__ Float64LessThanOrEqual(two_52, input)), done,
                    input);
            V<Float64> temp1 =
                __ Float64Sub(__ Float64Add(two_52, input), two_52);
            GOTO_IF_NOT(__ Float64LessThan(input, temp1), done, temp1);
            GOTO(done, __ Float64Sub(temp1, 1.0));
          } ELSE IF (UNLIKELY(__ Float64Equal(input, 0.0))) {
            GOTO(done, input);
          } ELSE IF (UNLIKELY(__ Float64LessThanOrEqual(input, minus_two_52))) {
            GOTO(done, input);
          } ELSE {
            V<Float64> temp1 = __ Float64Sub(-0.0, input);
            V<Float64> temp2 =
                __ Float64Sub(__ Float64Add(two_52, temp1), two_52);
            GOTO_IF_NOT(__ Float64LessThan(temp2, temp1), done,
                        __ Float64Sub(-0.0, temp2));
            GOTO(done, __ Float64Sub(-1.0, temp2));
          }

          BIND(done, result);
          return result;
        } else if (kind == FloatUnaryOp::Kind::kRoundTiesEven) {
          // Generate case for round ties to even:
          //
          //   let value = floor(input) in
          //   let temp1 = input - value in
          //   if temp1 < 0.5 then
          //     value
          //   else if 0.5 < temp1 then
          //     value + 1.0
          //   else
          //     let temp2 = value % 2.0 in
          //     if temp2 == 0.0 then
          //       value
          //     else
          //       value + 1.0

          Label<Float64> done(this);

          V<Float64> value = __ Float64RoundDown(input);
          V<Float64> temp1 = __ Float64Sub(input, value);
          GOTO_IF(__ Float64LessThan(temp1, 0.5), done, value);
          GOTO_IF(__ Float64LessThan(0.5, temp1), done,
                  __ Float64Add(value, 1.0));

          V<Float64> temp2 = __ Float64Mod(value, 2.0);
          GOTO_IF(__ Float64Equal(temp2, 0.0), done, value);
          GOTO(done, __ Float64Add(value, 1.0));

          BIND(done, result);
          return result;
        } else if (kind == FloatUnaryOp::Kind::kRoundToZero) {
          // General case for trunc.
          //
          //   if 0.0 < input then
          //     if 2^52 <= input then
          //       input
          //     else
          //       let temp1 = (2^52 + input) - 2^52 in
          //       if input < temp1 then
          //         temp1 - 1
          //       else
          //         temp1
          //   else
          //     if input == 0 then
          //        input
          //     if input <= -2^52 then
          //       input
          //     else
          //       let temp1 = -0 - input in
          //       let temp2 = (2^52 + temp1) - 2^52 in
          //       if temp1 < temp2 then
          //          -0 - (temp2 - 1)
          //       else
          //          -0 - temp2

          Label<Float64> done(this);

          IF (__ Float64LessThan(0.0, input)) {
            GOTO_IF(UNLIKELY(__ Float64LessThanOrEqual(two_52, input)), done,
                    input);

            V<Float64> temp1 =
                __ Float64Sub(__ Float64Add(two_52, input), two_52);
            GOTO_IF(__ Float64LessThan(input, temp1), done,
                    __ Float64Sub(temp1, 1.0));
            GOTO(done, temp1);
          } ELSE {
            GOTO_IF(UNLIKELY(__ Float64Equal(input, 0.0)), done, input);
            GOTO_IF(UNLIKELY(__ Float64LessThanOrEqual(input, minus_two_52)),
                    done, input);

            V<Float64> temp1 = __ Float64Sub(-0.0, input);
            V<Float64> temp2 =
                __ Float64Sub(__ Float64Add(two_52, temp1), two_52);

            IF (__ Float64LessThan(temp1, temp2)) {
              GOTO(done, __ Float64Sub(-0.0, __ Float64Sub(temp2, 1.0)));
            } ELSE {
              GOTO(done, __ Float64Sub(-0.0, temp2));
            }
          }

          BIND(done, result);
          return result;
        }
        UNREACHABLE();
      }
      default:
        DCHECK(FloatUnaryOp::IsSupported(kind, rep));
        goto no_change;
    }
    UNREACHABLE();
  }

  V<Object> REDUCE(CheckedClosure)(V<Object> input, OpIndex frame_state,
                                   Handle<FeedbackCell> feedback_cell) {
    // Check that {input} is actually a JSFunction.
    V<Map> map = __ LoadMapField(input);
    V<Word32> instance_type = __ LoadInstanceTypeField(map);
    V<Word32> is_function_type = __ Uint32LessThanOrEqual(
        __ Word32Sub(instance_type, FIRST_JS_FUNCTION_TYPE),
        (LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));
    __ DeoptimizeIfNot(is_function_type, frame_state,
                       DeoptimizeReason::kWrongCallTarget, FeedbackSource{});

    // Check that the {input}s feedback vector cell matches the one
    // we recorded before.
    V<HeapObject> cell = __ template LoadField<HeapObject>(
        input, AccessBuilder::ForJSFunctionFeedbackCell());
    __ DeoptimizeIfNot(__ TaggedEqual(cell, __ HeapConstant(feedback_cell)),
                       frame_state, DeoptimizeReason::kWrongFeedbackCell,
                       FeedbackSource{});
    return input;
  }

  OpIndex REDUCE(CheckEqualsInternalizedString)(V<Object> expected,
                                                V<Object> value,
                                                OpIndex frame_state) {
    Label<> done(this);
    // Check if {expected} and {value} are the same, which is the likely case.
    GOTO_IF(LIKELY(__ TaggedEqual(expected, value)), done);

    // Now {value} could still be a non-internalized String that matches
    // {expected}.
    __ DeoptimizeIf(__ ObjectIsSmi(value), frame_state,
                    DeoptimizeReason::kWrongName, FeedbackSource{});
    V<Map> value_map = __ LoadMapField(value);
    V<Word32> value_instance_type = __ LoadInstanceTypeField(value_map);
    V<Word32> value_representation =
        __ Word32BitwiseAnd(value_instance_type, kStringRepresentationMask);
    // ThinString
    IF (__ Word32Equal(value_representation, kThinStringTag)) {
      // The {value} is a ThinString, let's check the actual value.
      V<String> value_actual = __ template LoadField<String>(
          value, AccessBuilder::ForThinStringActual());
      __ DeoptimizeIfNot(__ TaggedEqual(expected, value_actual), frame_state,
                         DeoptimizeReason::kWrongName, FeedbackSource{});
    } ELSE {
      // Check that the {value} is a non-internalized String, if it's anything
      // else it cannot match the recorded feedback {expected} anyways.
      __ DeoptimizeIfNot(
          __ Word32Equal(
              __ Word32BitwiseAnd(value_instance_type,
                                  kIsNotStringMask | kIsNotInternalizedMask),
              kStringTag | kNotInternalizedTag),
          frame_state, DeoptimizeReason::kWrongName, FeedbackSource{});

      // Try to find the {value} in the string table.
      MachineSignature::Builder builder(__ graph_zone(), 1, 2);
      builder.AddReturn(MachineType::AnyTagged());
      builder.AddParam(MachineType::Pointer());
      builder.AddParam(MachineType::AnyTagged());
      OpIndex try_string_to_index_or_lookup_existing = __ ExternalConstant(
          ExternalReference::try_string_to_index_or_lookup_existing());
      OpIndex isolate_ptr =
          __ ExternalConstant(ExternalReference::isolate_address(isolate_));
      V<String> value_internalized = __ Call(
          try_string_to_index_or_lookup_existing, {isolate_ptr, value},
          TSCallDescriptor::Create(Linkage::GetSimplifiedCDescriptor(
                                       __ graph_zone(), builder.Build()),
                                   CanThrow::kNo, __ graph_zone()));

      // Now see if the results match.
      __ DeoptimizeIfNot(__ TaggedEqual(expected, value_internalized),
                         frame_state, DeoptimizeReason::kWrongName,
                         FeedbackSource{});
    }

    GOTO(done);

    BIND(done);
    return OpIndex::Invalid();
  }

  V<Object> REDUCE(LoadMessage)(V<WordPtr> offset) {
    return __ BitcastWordPtrToTagged(__ template LoadField<WordPtr>(
        offset, AccessBuilder::ForExternalIntPtr()));
  }

  OpIndex REDUCE(StoreMessage)(V<WordPtr> offset, V<Object> object) {
    __ StoreField(offset, AccessBuilder::ForExternalIntPtr(),
                  __ BitcastTaggedToWordPtr(object));
    return OpIndex::Invalid();
  }

  V<Boolean> REDUCE(SameValue)(OpIndex left, OpIndex right,
                               SameValueOp::Mode mode) {
    switch (mode) {
      case SameValueOp::Mode::kSameValue:
        return __ CallBuiltin_SameValue(isolate_, left, right);
      case SameValueOp::Mode::kSameValueNumbersOnly:
        return __ CallBuiltin_SameValueNumbersOnly(isolate_, left, right);
    }
  }

  V<Word32> REDUCE(Float64SameValue)(OpIndex left, OpIndex right) {
    Label<Word32> done(this);

    IF (__ Float64Equal(left, right)) {
      // Even if the values are float64-equal, we still need to distinguish
      // zero and minus zero.
      V<Word32> left_hi = __ Float64ExtractHighWord32(left);
      V<Word32> right_hi = __ Float64ExtractHighWord32(right);
      GOTO(done, __ Word32Equal(left_hi, right_hi));
    } ELSE {
      // Return true iff both {lhs} and {rhs} are NaN.
      GOTO_IF(__ Float64Equal(left, left), done, 0);
      GOTO_IF(__ Float64Equal(right, right), done, 0);
      GOTO(done, 1);
    }

    BIND(done, result);
    return result;
  }

  OpIndex REDUCE(RuntimeAbort)(AbortReason reason) {
    __ CallRuntime_Abort(isolate_, __ NoContextConstant(),
                         __ TagSmi(static_cast<int>(reason)));
    return OpIndex::Invalid();
  }

  V<Object> REDUCE(EnsureWritableFastElements)(V<Object> object,
                                               V<Object> elements) {
    Label<Object> done(this);
    // Load the current map of {elements}.
    V<Map> map = __ LoadMapField(elements);

    // Check if {elements} is not a copy-on-write FixedArray.
    // Nothing to do if the {elements} are not copy-on-write.
    GOTO_IF(LIKELY(__ TaggedEqual(
                map, __ HeapConstant(factory_->fixed_array_map()))),
            done, elements);

    // We need to take a copy of the {elements} and set them up for {object}.
    V<Object> copy =
        __ CallBuiltin_CopyFastSmiOrObjectElements(isolate_, object);
    GOTO(done, copy);

    BIND(done, result);
    return result;
  }

  V<Object> REDUCE(MaybeGrowFastElements)(V<Object> object, V<Object> elements,
                                          V<Word32> index,
                                          V<Word32> elements_length,
                                          OpIndex frame_state,
                                          GrowFastElementsMode mode,
                                          const FeedbackSource& feedback) {
    Label<Object> done(this);
    // Check if we need to grow the {elements} backing store.
    GOTO_IF(LIKELY(__ Uint32LessThan(index, elements_length)), done, elements);
    // We need to grow the {elements} for {object}.
    V<Object> new_elements;
    switch (mode) {
      case GrowFastElementsMode::kDoubleElements:
        new_elements = __ CallBuiltin_GrowFastDoubleElements(isolate_, object,
                                                             __ TagSmi(index));
        break;
      case GrowFastElementsMode::kSmiOrObjectElements:
        new_elements = __ CallBuiltin_GrowFastSmiOrObjectElements(
            isolate_, object, __ TagSmi(index));
        break;
    }

    // Ensure that we were able to grow the {elements}.
    __ DeoptimizeIf(__ ObjectIsSmi(new_elements), frame_state,
                    DeoptimizeReason::kCouldNotGrowElements, feedback);
    GOTO(done, new_elements);

    BIND(done, result);
    return result;
  }

  OpIndex REDUCE(TransitionElementsKind)(V<HeapObject> object,
                                         const ElementsTransition& transition) {
    V<Map> source_map = __ HeapConstant(transition.source().object());
    V<Map> target_map = __ HeapConstant(transition.target().object());

    // Load the current map of {object}.
    V<Map> map = __ LoadMapField(object);

    // Check if {map} is the same as {source_map}.
    IF (UNLIKELY(__ TaggedEqual(map, source_map))) {
      switch (transition.mode()) {
        case ElementsTransition::kFastTransition:
          // In-place migration of {object}, just store the {target_map}.
          __ StoreField(object, AccessBuilder::ForMap(), target_map);
          break;
        case ElementsTransition::kSlowTransition:
          // Instance migration, call out to the runtime for {object}.
          __ CallRuntime_TransitionElementsKind(
              isolate_, __ NoContextConstant(), object, target_map);
          break;
      }
    }

    return OpIndex::Invalid();
  }

  OpIndex REDUCE(FindOrderedHashEntry)(V<Object> data_structure, OpIndex key,
                                       FindOrderedHashEntryOp::Kind kind) {
    switch (kind) {
      case FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntry:
        return __ CallBuiltin_FindOrderedHashMapEntry(
            isolate_, __ NoContextConstant(), data_structure, key);
      case FindOrderedHashEntryOp::Kind::kFindOrderedHashMapEntryForInt32Key: {
        // Compute the integer hash code.
        V<WordPtr> hash = __ ChangeUint32ToUintPtr(ComputeUnseededHash(key));

        V<WordPtr> number_of_buckets =
            __ ChangeInt32ToIntPtr(__ UntagSmi(__ template LoadField<Smi>(
                data_structure,
                AccessBuilder::ForOrderedHashMapOrSetNumberOfBuckets())));
        hash = __ WordPtrBitwiseAnd(hash, __ WordPtrSub(number_of_buckets, 1));
        V<WordPtr> first_entry = __ ChangeInt32ToIntPtr(__ UntagSmi(__ Load(
            data_structure,
            __ WordPtrAdd(__ WordPtrShiftLeft(hash, kTaggedSizeLog2),
                          OrderedHashMap::HashTableStartOffset()),
            LoadOp::Kind::TaggedBase(), MemoryRepresentation::TaggedSigned())));

        Label<WordPtr> done(this);
        LoopLabel<WordPtr> loop(this);
        GOTO(loop, first_entry);

        BIND_LOOP(loop, entry) {
          GOTO_IF(__ WordPtrEqual(entry, OrderedHashMap::kNotFound), done,
                  entry);
          V<WordPtr> candidate =
              __ WordPtrAdd(__ WordPtrMul(entry, OrderedHashMap::kEntrySize),
                            number_of_buckets);
          V<Object> candidate_key = __ Load(
              data_structure,
              __ WordPtrAdd(__ WordPtrShiftLeft(candidate, kTaggedSizeLog2),
                            OrderedHashMap::HashTableStartOffset()),
              LoadOp::Kind::TaggedBase(), MemoryRepresentation::AnyTagged());

          IF (LIKELY(__ ObjectIsSmi(candidate_key))) {
            GOTO_IF(
                __ Word32Equal(__ UntagSmi(V<Smi>::Cast(candidate_key)), key),
                done, candidate);
          } ELSE IF (__ TaggedEqual(
                        __ LoadMapField(candidate_key),
                        __ HeapConstant(factory_->heap_number_map()))) {
            GOTO_IF(__ Float64Equal(
                        __ template LoadField<Float64>(
                            candidate_key, AccessBuilder::ForHeapNumberValue()),
                        __ ChangeInt32ToFloat64(key)),
                    done, candidate);
          }

          V<WordPtr> next_entry = __ ChangeInt32ToIntPtr(__ UntagSmi(__ Load(
              data_structure,
              __ WordPtrAdd(__ WordPtrShiftLeft(candidate, kTaggedSizeLog2),
                            (OrderedHashMap::HashTableStartOffset() +
                             OrderedHashMap::kChainOffset * kTaggedSize)),
              LoadOp::Kind::TaggedBase(),
              MemoryRepresentation::TaggedSigned())));
          GOTO(loop, next_entry);
        }

        BIND(done, result);
        return result;
      }
      case FindOrderedHashEntryOp::Kind::kFindOrderedHashSetEntry:
        return __ CallBuiltin_FindOrderedHashSetEntry(
            isolate_, __ NoContextConstant(), data_structure, key);
    }
  }

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  V<Object> REDUCE(GetContinuationPreservedEmbedderData)() {
    return __ Load(
        __ ExternalConstant(
            ExternalReference::continuation_preserved_embedder_data(isolate_)),
        LoadOp::Kind::RawAligned(), MemoryRepresentation::TaggedPointer());
  }

  OpIndex REDUCE(SetContinuationPreservedEmbedderData)(V<Object> data) {
    __ Store(
        __ ExternalConstant(
            ExternalReference::continuation_preserved_embedder_data(isolate_)),
        data, StoreOp::Kind::RawAligned(),
        MemoryRepresentation::TaggedPointer(),
        WriteBarrierKind::kNoWriteBarrier);
    return OpIndex::Invalid();
  }
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

 private:
  V<Word32> BuildUint32Mod(V<Word32> left, V<Word32> right) {
    Label<Word32> done(this);

    // Compute the mask for the {rhs}.
    V<Word32> msk = __ Word32Sub(right, 1);

    // Check if the {rhs} is a power of two.
    IF (__ Word32Equal(__ Word32BitwiseAnd(right, msk), 0)) {
      // The {rhs} is a power of two, just do a fast bit masking.
      GOTO(done, __ Word32BitwiseAnd(left, msk));
    } ELSE {
      // The {rhs} is not a power of two, do a generic Uint32Mod.
      GOTO(done, __ Uint32Mod(left, right));
    }

    BIND(done, result);
    return result;
  }

  // Pass {bitfield} = {digit} = OpIndex::Invalid() to construct the canonical
  // 0n BigInt.
  V<BigInt> AllocateBigInt(V<Word32> bitfield, V<Word64> digit) {
    if (Asm().generating_unreachable_operations()) return OpIndex::Invalid();

    DCHECK(Is64());
    DCHECK_EQ(bitfield.valid(), digit.valid());
    static constexpr auto zero_bitfield =
        BigInt::SignBits::update(BigInt::LengthBits::encode(0), false);

    V<Map> map = __ HeapConstant(factory_->bigint_map());
    auto bigint = __ template Allocate<FreshlyAllocatedBigInt>(
        __ IntPtrConstant(BigInt::SizeFor(digit.valid() ? 1 : 0)),
        AllocationType::kYoung);
    __ InitializeField(bigint, AccessBuilder::ForMap(), map);
    __ InitializeField(
        bigint, AccessBuilder::ForBigIntBitfield(),
        bitfield.valid() ? bitfield : __ Word32Constant(zero_bitfield));

    // BigInts have no padding on 64 bit architectures with pointer compression.
#ifdef BIGINT_NEEDS_PADDING
    __ InitializeField(bigint, AccessBuilder::ForBigIntOptionalPadding(),
                       __ IntPtrConstant(0));
#endif
    if (digit.valid()) {
      __ InitializeField(
          bigint, AccessBuilder::ForBigIntLeastSignificantDigit64(), digit);
    }
    return V<BigInt>::Cast(__ FinishInitialization(std::move(bigint)));
  }

  void TagSmiOrOverflow(V<Word32> input, Label<>* overflow,
                        Label<Object>* done) {
    DCHECK(SmiValuesAre31Bits());

    // Check for overflow at the same time that we are smi tagging.
    // Since smi tagging shifts left by one, it's the same as adding value
    // twice.
    OpIndex add = __ Int32AddCheckOverflow(input, input);
    V<Word32> check = __ template Projection<Word32>(add, 1);
    GOTO_IF(UNLIKELY(check), *overflow);
    GOTO(*done, __ BitcastWord32ToSmi(__ template Projection<Word32>(add, 0)));
  }

  // `IsNonZero` converts any non-0 value into 1.
  V<Word32> IsNonZero(V<Word32> value) {
    return __ Word32Equal(__ Word32Equal(value, 0), 0);
  }

  V<Object> AllocateHeapNumberWithValue(V<Float64> value) {
    auto result = __ template Allocate<HeapNumber>(
        __ IntPtrConstant(sizeof(HeapNumber)), AllocationType::kYoung);
    __ InitializeField(result, AccessBuilder::ForMap(),
                       __ HeapConstant(factory_->heap_number_map()));
    __ InitializeField(result, AccessBuilder::ForHeapNumberValue(), value);
    return __ FinishInitialization(std::move(result));
  }

  V<Float64> ConvertHeapObjectToFloat64OrDeopt(
      V<Object> heap_object, OpIndex frame_state,
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind input_kind,
      const FeedbackSource& feedback) {
    V<Map> map = __ LoadMapField(heap_object);
    V<Word32> check_number =
        __ TaggedEqual(map, __ HeapConstant(factory_->heap_number_map()));
    switch (input_kind) {
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi:
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
          kNumberOrString:
        UNREACHABLE();
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber: {
        __ DeoptimizeIfNot(check_number, frame_state,
                           DeoptimizeReason::kNotAHeapNumber, feedback);
        break;
      }
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
          kNumberOrBoolean: {
        IF_NOT(check_number) {
          __ DeoptimizeIfNot(
              __ TaggedEqual(map, __ HeapConstant(factory_->boolean_map())),
              frame_state, DeoptimizeReason::kNotANumberOrBoolean, feedback);
        }

        break;
      }
      case ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
          kNumberOrOddball: {
        IF_NOT(check_number) {
          // For oddballs also contain the numeric value, let us just check that
          // we have an oddball here.
          V<Word32> instance_type = __ LoadInstanceTypeField(map);
          __ DeoptimizeIfNot(__ Word32Equal(instance_type, ODDBALL_TYPE),
                             frame_state,
                             DeoptimizeReason::kNotANumberOrOddball, feedback);
        }

        break;
      }
    }
    return __ template LoadField<Float64>(
        heap_object, AccessBuilder::ForHeapNumberOrOddballOrHoleValue());
  }

  OpIndex LoadFromSeqString(V<Object> receiver, V<WordPtr> position,
                            V<Word32> onebyte) {
    Label<Word32> done(this);

    IF (onebyte) {
      GOTO(done, __ template LoadNonArrayBufferElement<Word32>(
                     receiver, AccessBuilder::ForSeqOneByteStringCharacter(),
                     position));
    } ELSE {
      GOTO(done, __ template LoadNonArrayBufferElement<Word32>(
                     receiver, AccessBuilder::ForSeqTwoByteStringCharacter(),
                     position));
    }

    BIND(done, result);
    return result;
  }

  void MigrateInstanceOrDeopt(V<HeapObject> heap_object, V<Map> heap_object_map,
                              OpIndex frame_state,
                              const FeedbackSource& feedback) {
    // If {heap_object_map} is not deprecated, the migration attempt does not
    // make sense.
    V<Word32> bitfield3 = __ template LoadField<Word32>(
        heap_object_map, AccessBuilder::ForMapBitField3());
    V<Word32> deprecated =
        __ Word32BitwiseAnd(bitfield3, Map::Bits3::IsDeprecatedBit::kMask);
    __ DeoptimizeIfNot(deprecated, frame_state, DeoptimizeReason::kWrongMap,
                       feedback);
    V<Object> result = __ CallRuntime_TryMigrateInstance(
        isolate_, __ NoContextConstant(), heap_object);
    // TryMigrateInstance returns a Smi value to signal failure.
    __ DeoptimizeIf(__ ObjectIsSmi(result), frame_state,
                    DeoptimizeReason::kInstanceMigrationFailed, feedback);
  }

  // TODO(nicohartmann@): Might use the CallBuiltinDescriptors here.
  OpIndex CallBuiltinForBigIntOp(Builtin builtin,
                                 std::initializer_list<OpIndex> arguments) {
    DCHECK_IMPLIES(builtin == Builtin::kBigIntUnaryMinus,
                   arguments.size() == 1);
    DCHECK_IMPLIES(builtin != Builtin::kBigIntUnaryMinus,
                   arguments.size() == 2);
    base::SmallVector<OpIndex, 4> args(arguments);
    args.push_back(__ NoContextConstant());

    Callable callable = Builtins::CallableFor(isolate_, builtin);
    auto descriptor = Linkage::GetStubCallDescriptor(
        __ graph_zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(),
        CallDescriptor::kNoFlags, Operator::kFoldable | Operator::kNoThrow);
    auto ts_descriptor =
        TSCallDescriptor::Create(descriptor, CanThrow::kNo, __ graph_zone());
    return __ Call(__ HeapConstant(callable.code()), OpIndex::Invalid(),
                   base::VectorOf(args), ts_descriptor);
  }

  Builtin GetBuiltinForBigIntBinop(BigIntBinopOp::Kind kind) {
    switch (kind) {
      case BigIntBinopOp::Kind::kAdd:
        return Builtin::kBigIntAddNoThrow;
      case BigIntBinopOp::Kind::kSub:
        return Builtin::kBigIntSubtractNoThrow;
      case BigIntBinopOp::Kind::kMul:
        return Builtin::kBigIntMultiplyNoThrow;
      case BigIntBinopOp::Kind::kDiv:
        return Builtin::kBigIntDivideNoThrow;
      case BigIntBinopOp::Kind::kMod:
        return Builtin::kBigIntModulusNoThrow;
      case BigIntBinopOp::Kind::kBitwiseAnd:
        return Builtin::kBigIntBitwiseAndNoThrow;
      case BigIntBinopOp::Kind::kBitwiseOr:
        return Builtin::kBigIntBitwiseOrNoThrow;
      case BigIntBinopOp::Kind::kBitwiseXor:
        return Builtin::kBigIntBitwiseXorNoThrow;
      case BigIntBinopOp::Kind::kShiftLeft:
        return Builtin::kBigIntShiftLeftNoThrow;
      case BigIntBinopOp::Kind::kShiftRightArithmetic:
        return Builtin::kBigIntShiftRightNoThrow;
    }
  }

  V<WordPtr> BuildTypedArrayDataPointer(V<Object> base, V<WordPtr> external) {
    if (__ matcher().MatchZero(base)) return external;
    V<WordPtr> untagged_base = __ BitcastTaggedToWordPtr(base);
    if (COMPRESS_POINTERS_BOOL) {
      // Zero-extend Tagged_t to UintPtr according to current compression
      // scheme so that the addition with |external_pointer| (which already
      // contains compensated offset value) will decompress the tagged value.
      // See JSTypedArray::ExternalPointerCompensationForOnHeapArray() for
      // details.
      untagged_base =
          __ ChangeUint32ToUintPtr(__ TruncateWordPtrToWord32(untagged_base));
    }
    return __ WordPtrAdd(untagged_base, external);
  }

  V<Word32> ComputeUnseededHash(V<Word32> value) {
    // See v8::internal::ComputeUnseededHash()
    value = __ Word32Add(__ Word32BitwiseXor(value, 0xFFFFFFFF),
                         __ Word32ShiftLeft(value, 15));
    value = __ Word32BitwiseXor(value, __ Word32ShiftRightLogical(value, 12));
    value = __ Word32Add(value, __ Word32ShiftLeft(value, 2));
    value = __ Word32BitwiseXor(value, __ Word32ShiftRightLogical(value, 4));
    value = __ Word32Mul(value, 2057);
    value = __ Word32BitwiseXor(value, __ Word32ShiftRightLogical(value, 16));
    value = __ Word32BitwiseAnd(value, 0x3FFFFFFF);
    return value;
  }

  void TransitionElementsTo(V<JSArray> array, ElementsKind from,
                            ElementsKind to, Handle<Map> target_map) {
    DCHECK(IsMoreGeneralElementsKindTransition(from, to));
    DCHECK(to == HOLEY_ELEMENTS || to == HOLEY_DOUBLE_ELEMENTS);

    if (IsSimpleMapChangeTransition(from, to)) {
      __ StoreField(array, AccessBuilder::ForMap(),
                    __ HeapConstant(target_map));
    } else {
      // Instance migration, call out to the runtime for {array}.
      __ CallRuntime_TransitionElementsKind(isolate_, __ NoContextConstant(),
                                            array, __ HeapConstant(target_map));
    }
  }

  V<Word32> CompareMapAgainstMultipleMaps(V<Map> heap_object_map,
                                          const ZoneRefSet<Map>& maps) {
    if (maps.is_empty()) {
      return __ Word32Constant(0);
    }
    V<Word32> result;
    for (size_t i = 0; i < maps.size(); ++i) {
      V<Map> map = __ HeapConstant(maps[i].object());
      if (i == 0) {
        result = __ TaggedEqual(heap_object_map, map);
      } else {
        result =
            __ Word32BitwiseOr(result, __ TaggedEqual(heap_object_map, map));
      }
    }
    return result;
  }

  bool DependOnNoUndetectableObjectsProtector() {
    if (!undetectable_objects_protector_) {
      UnparkedScopeIfNeeded unpark(broker_);
      undetectable_objects_protector_ =
          broker_->dependencies()->DependOnNoUndetectableObjectsProtector();
    }
    return *undetectable_objects_protector_;
  }

  Isolate* isolate_ = PipelineData::Get().isolate();
  Factory* factory_ = isolate_ ? isolate_->factory() : nullptr;
  JSHeapBroker* broker_ = PipelineData::Get().broker();
  base::Optional<bool> undetectable_objects_protector_ = {};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_LOWERING_REDUCER_INL_H_
