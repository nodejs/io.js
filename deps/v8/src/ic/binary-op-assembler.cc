// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/binary-op-assembler.h"

#include "src/common/globals.h"
#include "src/execution/protectors.h"
#include "src/objects/property-cell.h"

namespace v8 {
namespace internal {

namespace {

inline bool IsBigInt64OpSupported(BinaryOpAssembler* assembler, Operation op) {
  return assembler->Is64() && op != Operation::kExponentiate &&
         op != Operation::kShiftLeft && op != Operation::kShiftRight &&
         op != Operation::kShiftRightLogical;
}

}  // namespace

TNode<Object> BinaryOpAssembler::Generate_AddWithFeedback(
    const LazyNode<Context>& context, TNode<Object> lhs, TNode<Object> rhs,
    TNode<UintPtrT> slot_id, const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  // Shared entry for floating point addition.
  Label do_fadd(this), if_lhsisnotnumber(this, Label::kDeferred),
      check_rhsisoddball(this, Label::kDeferred),
      call_with_oddball_feedback(this), call_with_any_feedback(this),
      call_add_stub(this), end(this), bigint(this, Label::kDeferred),
      bigint64(this);
  TVARIABLE(Float64T, var_fadd_lhs);
  TVARIABLE(Float64T, var_fadd_rhs);
  TVARIABLE(Smi, var_type_feedback);
  TVARIABLE(Object, var_result);

  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(this);
  // If rhs is known to be an Smi we want to fast path Smi operation. This is
  // for AddSmi operation. For the normal Add operation, we want to fast path
  // both Smi and Number operations, so this path should not be marked as
  // Deferred.
  Label if_lhsisnotsmi(this,
                       rhs_known_smi ? Label::kDeferred : Label::kNonDeferred);
  Branch(TaggedIsNotSmi(lhs), &if_lhsisnotsmi, &if_lhsissmi);

  BIND(&if_lhsissmi);
  {
    Comment("lhs is Smi");
    TNode<Smi> lhs_smi = CAST(lhs);
    if (!rhs_known_smi) {
      // Check if the {rhs} is also a Smi.
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsisnotsmi);
      {
        // Check if the {rhs} is a HeapNumber.
        TNode<HeapObject> rhs_heap_object = CAST(rhs);
        GotoIfNot(IsHeapNumber(rhs_heap_object), &check_rhsisoddball);

        var_fadd_lhs = SmiToFloat64(lhs_smi);
        var_fadd_rhs = LoadHeapNumberValue(rhs_heap_object);
        Goto(&do_fadd);
      }

      BIND(&if_rhsissmi);
    }

    {
      Comment("perform smi operation");
      // If rhs is known to be an Smi we want to fast path Smi operation. This
      // is for AddSmi operation. For the normal Add operation, we want to fast
      // path both Smi and Number operations, so this path should not be marked
      // as Deferred.
      TNode<Smi> rhs_smi = CAST(rhs);
      Label if_overflow(this,
                        rhs_known_smi ? Label::kDeferred : Label::kNonDeferred);
      TNode<Smi> smi_result = TrySmiAdd(lhs_smi, rhs_smi, &if_overflow);
      // Not overflowed.
      {
        var_type_feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
        UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(),
                       slot_id, update_feedback_mode);
        var_result = smi_result;
        Goto(&end);
      }

      BIND(&if_overflow);
      {
        var_fadd_lhs = SmiToFloat64(lhs_smi);
        var_fadd_rhs = SmiToFloat64(rhs_smi);
        Goto(&do_fadd);
      }
    }
  }

  BIND(&if_lhsisnotsmi);
  {
    // Check if {lhs} is a HeapNumber.
    TNode<HeapObject> lhs_heap_object = CAST(lhs);
    GotoIfNot(IsHeapNumber(lhs_heap_object), &if_lhsisnotnumber);

    if (!rhs_known_smi) {
      // Check if the {rhs} is Smi.
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsisnotsmi);
      {
        // Check if the {rhs} is a HeapNumber.
        TNode<HeapObject> rhs_heap_object = CAST(rhs);
        GotoIfNot(IsHeapNumber(rhs_heap_object), &check_rhsisoddball);

        var_fadd_lhs = LoadHeapNumberValue(lhs_heap_object);
        var_fadd_rhs = LoadHeapNumberValue(rhs_heap_object);
        Goto(&do_fadd);
      }

      BIND(&if_rhsissmi);
    }
    {
      var_fadd_lhs = LoadHeapNumberValue(lhs_heap_object);
      var_fadd_rhs = SmiToFloat64(CAST(rhs));
      Goto(&do_fadd);
    }
  }

  BIND(&do_fadd);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    TNode<Float64T> value =
        Float64Add(var_fadd_lhs.value(), var_fadd_rhs.value());
    TNode<HeapNumber> result = AllocateHeapNumberWithValue(value);
    var_result = result;
    Goto(&end);
  }

  BIND(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    Label if_lhsisoddball(this), if_lhsisnotoddball(this);
    TNode<Uint16T> lhs_instance_type = LoadInstanceType(CAST(lhs));
    TNode<BoolT> lhs_is_oddball =
        InstanceTypeEqual(lhs_instance_type, ODDBALL_TYPE);
    Branch(lhs_is_oddball, &if_lhsisoddball, &if_lhsisnotoddball);

    BIND(&if_lhsisoddball);
    {
      GotoIf(TaggedIsSmi(rhs), &call_with_oddball_feedback);

      // Check if {rhs} is a HeapNumber.
      Branch(IsHeapNumber(CAST(rhs)), &call_with_oddball_feedback,
             &check_rhsisoddball);
    }

    BIND(&if_lhsisnotoddball);
    {
      // Check if the {rhs} is a smi, and exit the string and bigint check early
      // if it is.
      GotoIf(TaggedIsSmi(rhs), &call_with_any_feedback);
      TNode<HeapObject> rhs_heap_object = CAST(rhs);

      Label lhs_is_string(this), lhs_is_bigint(this);
      GotoIf(IsStringInstanceType(lhs_instance_type), &lhs_is_string);
      GotoIf(IsBigIntInstanceType(lhs_instance_type), &lhs_is_bigint);
      Goto(&call_with_any_feedback);

      BIND(&lhs_is_bigint);
      {
        GotoIfNot(IsBigInt(rhs_heap_object), &call_with_any_feedback);
        if (Is64()) {
          GotoIfLargeBigInt(CAST(lhs), &bigint);
          GotoIfLargeBigInt(CAST(rhs), &bigint);
          Goto(&bigint64);
        } else {
          Goto(&bigint);
        }
      }

      BIND(&lhs_is_string);
      {
        Label lhs_is_string_rhs_is_not_string(this);

        TNode<Uint16T> rhs_instance_type = LoadInstanceType(rhs_heap_object);

        // Fast path where both {lhs} and {rhs} are strings. Since {lhs} is a
        // string we no longer need an Oddball check.
        GotoIfNot(IsStringInstanceType(rhs_instance_type),
                  &lhs_is_string_rhs_is_not_string);

        var_type_feedback = SmiConstant(BinaryOperationFeedback::kString);
        UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(),
                       slot_id, update_feedback_mode);
        var_result =
            CallBuiltin(Builtin::kStringAdd_CheckNone, context(), lhs, rhs);

        Goto(&end);

        BIND(&lhs_is_string_rhs_is_not_string);

        GotoIfNot(IsStringWrapper(rhs_heap_object), &call_with_any_feedback);

        // lhs is a string and rhs is a string wrapper.

        TNode<PropertyCell> to_primitive_protector =
            StringWrapperToPrimitiveProtectorConstant();
        GotoIf(TaggedEqual(LoadObjectField(to_primitive_protector,
                                           PropertyCell::kValueOffset),
                           SmiConstant(Protectors::kProtectorInvalid)),
               &call_with_any_feedback);

        var_type_feedback =
            SmiConstant(BinaryOperationFeedback::kStringOrStringWrapper);
        UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(),
                       slot_id, update_feedback_mode);
        TNode<String> rhs_string =
            CAST(LoadJSPrimitiveWrapperValue(CAST(rhs_heap_object)));
        var_result = CallBuiltin(Builtin::kStringAdd_CheckNone, context(), lhs,
                                 rhs_string);
        Goto(&end);
      }
    }
  }
  // TODO(v8:12199): Support "string wrapper + string" concatenation too.

  BIND(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    TNode<Uint16T> rhs_instance_type = LoadInstanceType(CAST(rhs));
    TNode<BoolT> rhs_is_oddball =
        InstanceTypeEqual(rhs_instance_type, ODDBALL_TYPE);
    GotoIf(rhs_is_oddball, &call_with_oddball_feedback);
    Goto(&call_with_any_feedback);
  }

  if (Is64()) {
    BIND(&bigint64);
    {
      // Both {lhs} and {rhs} are of BigInt type and can fit in 64-bit
      // registers.
      Label if_overflow(this);
      TVARIABLE(UintPtrT, lhs_raw);
      TVARIABLE(UintPtrT, rhs_raw);
      BigIntToRawBytes(CAST(lhs), &lhs_raw, &lhs_raw);
      BigIntToRawBytes(CAST(rhs), &rhs_raw, &rhs_raw);
      var_result = BigIntFromInt64(
          TryIntPtrAdd(UncheckedCast<IntPtrT>(lhs_raw.value()),
                       UncheckedCast<IntPtrT>(rhs_raw.value()), &if_overflow));

      var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt64);
      UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(),
                     slot_id, update_feedback_mode);
      Goto(&end);

      BIND(&if_overflow);
      Goto(&bigint);
    }
  }

  BIND(&bigint);
  {
    // Both {lhs} and {rhs} are of BigInt type.
    Label bigint_too_big(this);
    var_result = CallBuiltin(Builtin::kBigIntAddNoThrow, context(), lhs, rhs);
    // Check for sentinel that signals BigIntTooBig exception.
    GotoIf(TaggedIsSmi(var_result.value()), &bigint_too_big);

    var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt);
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    Goto(&end);

    BIND(&bigint_too_big);
    {
      // Update feedback to prevent deopt loop.
      UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                     maybe_feedback_vector(), slot_id, update_feedback_mode);
      ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
    }
  }

  BIND(&call_with_oddball_feedback);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumberOrOddball);
    Goto(&call_add_stub);
  }

  BIND(&call_with_any_feedback);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kAny);
    Goto(&call_add_stub);
  }

  BIND(&call_add_stub);
  {
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    var_result = CallBuiltin(Builtin::kAdd, context(), lhs, rhs);
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Object> BinaryOpAssembler::Generate_BinaryOperationWithFeedback(
    const LazyNode<Context>& context, TNode<Object> lhs, TNode<Object> rhs,
    TNode<UintPtrT> slot_id, const LazyNode<HeapObject>& maybe_feedback_vector,
    const SmiOperation& smiOperation, const FloatOperation& floatOperation,
    Operation op, UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  Label do_float_operation(this), end(this), call_stub(this),
      check_rhsisoddball(this, Label::kDeferred), call_with_any_feedback(this),
      if_lhsisnotnumber(this, Label::kDeferred),
      if_both_bigint(this, Label::kDeferred), if_both_bigint64(this);
  TVARIABLE(Float64T, var_float_lhs);
  TVARIABLE(Float64T, var_float_rhs);
  TVARIABLE(Smi, var_type_feedback);
  TVARIABLE(Object, var_result);

  Label if_lhsissmi(this);
  // If rhs is known to be an Smi (in the SubSmi, MulSmi, DivSmi, ModSmi
  // bytecode handlers) we want to fast path Smi operation. For the normal
  // operation, we want to fast path both Smi and Number operations, so this
  // path should not be marked as Deferred.
  Label if_lhsisnotsmi(this,
                       rhs_known_smi ? Label::kDeferred : Label::kNonDeferred);
  Branch(TaggedIsNotSmi(lhs), &if_lhsisnotsmi, &if_lhsissmi);

  // Check if the {lhs} is a Smi or a HeapObject.
  BIND(&if_lhsissmi);
  {
    Comment("lhs is Smi");
    TNode<Smi> lhs_smi = CAST(lhs);
    if (!rhs_known_smi) {
      // Check if the {rhs} is also a Smi.
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsisnotsmi);
      {
        // Check if {rhs} is a HeapNumber.
        TNode<HeapObject> rhs_heap_object = CAST(rhs);
        GotoIfNot(IsHeapNumber(rhs_heap_object), &check_rhsisoddball);

        // Perform a floating point operation.
        var_float_lhs = SmiToFloat64(lhs_smi);
        var_float_rhs = LoadHeapNumberValue(rhs_heap_object);
        Goto(&do_float_operation);
      }

      BIND(&if_rhsissmi);
    }

    {
      Comment("perform smi operation");
      var_result = smiOperation(lhs_smi, CAST(rhs), &var_type_feedback);
      UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(),
                     slot_id, update_feedback_mode);
      Goto(&end);
    }
  }

  BIND(&if_lhsisnotsmi);
  {
    Comment("lhs is not Smi");
    // Check if the {lhs} is a HeapNumber.
    TNode<HeapObject> lhs_heap_object = CAST(lhs);
    GotoIfNot(IsHeapNumber(lhs_heap_object), &if_lhsisnotnumber);

    if (!rhs_known_smi) {
      // Check if the {rhs} is a Smi.
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsisnotsmi);
      {
        // Check if the {rhs} is a HeapNumber.
        TNode<HeapObject> rhs_heap_object = CAST(rhs);
        GotoIfNot(IsHeapNumber(rhs_heap_object), &check_rhsisoddball);

        // Perform a floating point operation.
        var_float_lhs = LoadHeapNumberValue(lhs_heap_object);
        var_float_rhs = LoadHeapNumberValue(rhs_heap_object);
        Goto(&do_float_operation);
      }

      BIND(&if_rhsissmi);
    }

    {
      // Perform floating point operation.
      var_float_lhs = LoadHeapNumberValue(lhs_heap_object);
      var_float_rhs = SmiToFloat64(CAST(rhs));
      Goto(&do_float_operation);
    }
  }

  BIND(&do_float_operation);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    TNode<Float64T> lhs_value = var_float_lhs.value();
    TNode<Float64T> rhs_value = var_float_rhs.value();
    TNode<Float64T> value = floatOperation(lhs_value, rhs_value);
    var_result = AllocateHeapNumberWithValue(value);
    Goto(&end);
  }

  BIND(&if_lhsisnotnumber);
  {
    // No checks on rhs are done yet. We just know lhs is not a number or Smi.
    Label if_left_bigint(this), if_left_oddball(this);
    TNode<Uint16T> lhs_instance_type = LoadInstanceType(CAST(lhs));
    GotoIf(IsBigIntInstanceType(lhs_instance_type), &if_left_bigint);
    TNode<BoolT> lhs_is_oddball =
        InstanceTypeEqual(lhs_instance_type, ODDBALL_TYPE);
    Branch(lhs_is_oddball, &if_left_oddball, &call_with_any_feedback);

    BIND(&if_left_oddball);
    {
      Label if_rhsissmi(this), if_rhsisnotsmi(this);
      Branch(TaggedIsSmi(rhs), &if_rhsissmi, &if_rhsisnotsmi);

      BIND(&if_rhsissmi);
      {
        var_type_feedback =
            SmiConstant(BinaryOperationFeedback::kNumberOrOddball);
        Goto(&call_stub);
      }

      BIND(&if_rhsisnotsmi);
      {
        // Check if {rhs} is a HeapNumber.
        GotoIfNot(IsHeapNumber(CAST(rhs)), &check_rhsisoddball);

        var_type_feedback =
            SmiConstant(BinaryOperationFeedback::kNumberOrOddball);
        Goto(&call_stub);
      }
    }

    BIND(&if_left_bigint);
    {
      GotoIf(TaggedIsSmi(rhs), &call_with_any_feedback);
      GotoIfNot(IsBigInt(CAST(rhs)), &call_with_any_feedback);
      if (IsBigInt64OpSupported(this, op)) {
        GotoIfLargeBigInt(CAST(lhs), &if_both_bigint);
        GotoIfLargeBigInt(CAST(rhs), &if_both_bigint);
        Goto(&if_both_bigint64);
      } else {
        Goto(&if_both_bigint);
      }
    }
  }

  BIND(&check_rhsisoddball);
  {
    // Check if rhs is an oddball. At this point we know lhs is either a
    // Smi or number or oddball and rhs is not a number or Smi.
    TNode<Uint16T> rhs_instance_type = LoadInstanceType(CAST(rhs));
    TNode<BoolT> rhs_is_oddball =
        InstanceTypeEqual(rhs_instance_type, ODDBALL_TYPE);
    GotoIfNot(rhs_is_oddball, &call_with_any_feedback);

    var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumberOrOddball);
    Goto(&call_stub);
  }

  if (IsBigInt64OpSupported(this, op)) {
    BIND(&if_both_bigint64);
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt64);
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);

    TVARIABLE(UintPtrT, lhs_raw);
    TVARIABLE(UintPtrT, rhs_raw);
    BigIntToRawBytes(CAST(lhs), &lhs_raw, &lhs_raw);
    BigIntToRawBytes(CAST(rhs), &rhs_raw, &rhs_raw);

    switch (op) {
      case Operation::kSubtract: {
        var_result = BigIntFromInt64(TryIntPtrSub(
            UncheckedCast<IntPtrT>(lhs_raw.value()),
            UncheckedCast<IntPtrT>(rhs_raw.value()), &if_both_bigint));
        Goto(&end);
        break;
      }
      case Operation::kMultiply: {
        var_result = BigIntFromInt64(TryIntPtrMul(
            UncheckedCast<IntPtrT>(lhs_raw.value()),
            UncheckedCast<IntPtrT>(rhs_raw.value()), &if_both_bigint));
        Goto(&end);
        break;
      }
      case Operation::kDivide: {
        // No need to check overflow because INT_MIN is excluded
        // from the range of small BigInts.
        Label if_div_zero(this);
        var_result = BigIntFromInt64(TryIntPtrDiv(
            UncheckedCast<IntPtrT>(lhs_raw.value()),
            UncheckedCast<IntPtrT>(rhs_raw.value()), &if_div_zero));
        Goto(&end);

        BIND(&if_div_zero);
        {
          // Update feedback to prevent deopt loop.
          UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                         maybe_feedback_vector(), slot_id,
                         update_feedback_mode);
          ThrowRangeError(context(), MessageTemplate::kBigIntDivZero);
        }
        break;
      }
      case Operation::kModulus: {
        Label if_div_zero(this);
        var_result = BigIntFromInt64(TryIntPtrMod(
            UncheckedCast<IntPtrT>(lhs_raw.value()),
            UncheckedCast<IntPtrT>(rhs_raw.value()), &if_div_zero));
        Goto(&end);

        BIND(&if_div_zero);
        {
          // Update feedback to prevent deopt loop.
          UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                         maybe_feedback_vector(), slot_id,
                         update_feedback_mode);
          ThrowRangeError(context(), MessageTemplate::kBigIntDivZero);
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  BIND(&if_both_bigint);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kBigInt);
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    switch (op) {
      case Operation::kSubtract: {
        var_result =
            CallBuiltin(Builtin::kBigIntSubtractNoThrow, context(), lhs, rhs);

        // Check for sentinel that signals BigIntTooBig exception.
        GotoIfNot(TaggedIsSmi(var_result.value()), &end);

        // Update feedback to prevent deopt loop.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       maybe_feedback_vector(), slot_id, update_feedback_mode);
        ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
        break;
      }
      case Operation::kMultiply: {
        Label termination_requested(this, Label::kDeferred);
        var_result =
            CallBuiltin(Builtin::kBigIntMultiplyNoThrow, context(), lhs, rhs);

        GotoIfNot(TaggedIsSmi(var_result.value()), &end);

        // Check for sentinel that signals TerminationRequested exception.
        GotoIf(TaggedEqual(var_result.value(), SmiConstant(1)),
               &termination_requested);

        // Handles BigIntTooBig exception.
        // Update feedback to prevent deopt loop.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       maybe_feedback_vector(), slot_id, update_feedback_mode);
        ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);

        BIND(&termination_requested);
        TerminateExecution(context());
        break;
      }
      case Operation::kDivide: {
        Label termination_requested(this, Label::kDeferred);
        var_result =
            CallBuiltin(Builtin::kBigIntDivideNoThrow, context(), lhs, rhs);

        GotoIfNot(TaggedIsSmi(var_result.value()), &end);

        // Check for sentinel that signals TerminationRequested exception.
        GotoIf(TaggedEqual(var_result.value(), SmiConstant(1)),
               &termination_requested);

        // Handles BigIntDivZero exception.
        // Update feedback to prevent deopt loop.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       maybe_feedback_vector(), slot_id, update_feedback_mode);
        ThrowRangeError(context(), MessageTemplate::kBigIntDivZero);

        BIND(&termination_requested);
        TerminateExecution(context());
        break;
      }
      case Operation::kModulus: {
        Label termination_requested(this, Label::kDeferred);
        var_result =
            CallBuiltin(Builtin::kBigIntModulusNoThrow, context(), lhs, rhs);

        GotoIfNot(TaggedIsSmi(var_result.value()), &end);

        // Check for sentinel that signals TerminationRequested exception.
        GotoIf(TaggedEqual(var_result.value(), SmiConstant(1)),
               &termination_requested);

        // Handles BigIntDivZero exception.
        // Update feedback to prevent deopt loop.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       maybe_feedback_vector(), slot_id, update_feedback_mode);
        ThrowRangeError(context(), MessageTemplate::kBigIntDivZero);

        BIND(&termination_requested);
        TerminateExecution(context());
        break;
      }
      case Operation::kExponentiate: {
        // TODO(panq): replace the runtime with builtin once it is implemented.
        var_result = CallRuntime(Runtime::kBigIntBinaryOp, context(), lhs, rhs,
                                 SmiConstant(op));
        Goto(&end);
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  BIND(&call_with_any_feedback);
  {
    var_type_feedback = SmiConstant(BinaryOperationFeedback::kAny);
    Goto(&call_stub);
  }

  BIND(&call_stub);
  {
    UpdateFeedback(var_type_feedback.value(), maybe_feedback_vector(), slot_id,
                   update_feedback_mode);
    TNode<Object> result;
    switch (op) {
      case Operation::kSubtract:
        result = CallBuiltin(Builtin::kSubtract, context(), lhs, rhs);
        break;
      case Operation::kMultiply:
        result = CallBuiltin(Builtin::kMultiply, context(), lhs, rhs);
        break;
      case Operation::kDivide:
        result = CallBuiltin(Builtin::kDivide, context(), lhs, rhs);
        break;
      case Operation::kModulus:
        result = CallBuiltin(Builtin::kModulus, context(), lhs, rhs);
        break;
      case Operation::kExponentiate:
        result = CallBuiltin(Builtin::kExponentiate, context(), lhs, rhs);
        break;
      default:
        UNREACHABLE();
    }
    var_result = result;
    Goto(&end);
  }

  BIND(&end);
  return var_result.value();
}

TNode<Object> BinaryOpAssembler::Generate_SubtractWithFeedback(
    const LazyNode<Context>& context, TNode<Object> lhs, TNode<Object> rhs,
    TNode<UintPtrT> slot_id, const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  auto smiFunction = [=](TNode<Smi> lhs, TNode<Smi> rhs,
                         TVariable<Smi>* var_type_feedback) {
    Label end(this);
    TVARIABLE(Number, var_result);
    // If rhs is known to be an Smi (for SubSmi) we want to fast path Smi
    // operation. For the normal Sub operation, we want to fast path both
    // Smi and Number operations, so this path should not be marked as Deferred.
    Label if_overflow(this,
                      rhs_known_smi ? Label::kDeferred : Label::kNonDeferred);
    var_result = TrySmiSub(lhs, rhs, &if_overflow);
    *var_type_feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
    Goto(&end);

    BIND(&if_overflow);
    {
      *var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
      TNode<Float64T> value = Float64Sub(SmiToFloat64(lhs), SmiToFloat64(rhs));
      var_result = AllocateHeapNumberWithValue(value);
      Goto(&end);
    }

    BIND(&end);
    return var_result.value();
  };
  auto floatFunction = [=](TNode<Float64T> lhs, TNode<Float64T> rhs) {
    return Float64Sub(lhs, rhs);
  };
  return Generate_BinaryOperationWithFeedback(
      context, lhs, rhs, slot_id, maybe_feedback_vector, smiFunction,
      floatFunction, Operation::kSubtract, update_feedback_mode, rhs_known_smi);
}

TNode<Object> BinaryOpAssembler::Generate_MultiplyWithFeedback(
    const LazyNode<Context>& context, TNode<Object> lhs, TNode<Object> rhs,
    TNode<UintPtrT> slot_id, const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  auto smiFunction = [=](TNode<Smi> lhs, TNode<Smi> rhs,
                         TVariable<Smi>* var_type_feedback) {
    TNode<Number> result = SmiMul(lhs, rhs);
    *var_type_feedback = SelectSmiConstant(
        TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
        BinaryOperationFeedback::kNumber);
    return result;
  };
  auto floatFunction = [=](TNode<Float64T> lhs, TNode<Float64T> rhs) {
    return Float64Mul(lhs, rhs);
  };
  return Generate_BinaryOperationWithFeedback(
      context, lhs, rhs, slot_id, maybe_feedback_vector, smiFunction,
      floatFunction, Operation::kMultiply, update_feedback_mode, rhs_known_smi);
}

TNode<Object> BinaryOpAssembler::Generate_DivideWithFeedback(
    const LazyNode<Context>& context, TNode<Object> dividend,
    TNode<Object> divisor, TNode<UintPtrT> slot_id,
    const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  auto smiFunction = [=](TNode<Smi> lhs, TNode<Smi> rhs,
                         TVariable<Smi>* var_type_feedback) {
    TVARIABLE(Object, var_result);
    // If rhs is known to be an Smi (for DivSmi) we want to fast path Smi
    // operation. For the normal Div operation, we want to fast path both
    // Smi and Number operations, so this path should not be marked as Deferred.
    Label bailout(this, rhs_known_smi ? Label::kDeferred : Label::kNonDeferred),
        end(this);
    var_result = TrySmiDiv(lhs, rhs, &bailout);
    *var_type_feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
    Goto(&end);

    BIND(&bailout);
    {
      *var_type_feedback =
          SmiConstant(BinaryOperationFeedback::kSignedSmallInputs);
      TNode<Float64T> value = Float64Div(SmiToFloat64(lhs), SmiToFloat64(rhs));
      var_result = AllocateHeapNumberWithValue(value);
      Goto(&end);
    }

    BIND(&end);
    return var_result.value();
  };
  auto floatFunction = [=](TNode<Float64T> lhs, TNode<Float64T> rhs) {
    return Float64Div(lhs, rhs);
  };
  return Generate_BinaryOperationWithFeedback(
      context, dividend, divisor, slot_id, maybe_feedback_vector, smiFunction,
      floatFunction, Operation::kDivide, update_feedback_mode, rhs_known_smi);
}

TNode<Object> BinaryOpAssembler::Generate_ModulusWithFeedback(
    const LazyNode<Context>& context, TNode<Object> dividend,
    TNode<Object> divisor, TNode<UintPtrT> slot_id,
    const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  auto smiFunction = [=](TNode<Smi> lhs, TNode<Smi> rhs,
                         TVariable<Smi>* var_type_feedback) {
    TNode<Number> result = SmiMod(lhs, rhs);
    *var_type_feedback = SelectSmiConstant(
        TaggedIsSmi(result), BinaryOperationFeedback::kSignedSmall,
        BinaryOperationFeedback::kNumber);
    return result;
  };
  auto floatFunction = [=](TNode<Float64T> lhs, TNode<Float64T> rhs) {
    return Float64Mod(lhs, rhs);
  };
  return Generate_BinaryOperationWithFeedback(
      context, dividend, divisor, slot_id, maybe_feedback_vector, smiFunction,
      floatFunction, Operation::kModulus, update_feedback_mode, rhs_known_smi);
}

TNode<Object> BinaryOpAssembler::Generate_ExponentiateWithFeedback(
    const LazyNode<Context>& context, TNode<Object> base,
    TNode<Object> exponent, TNode<UintPtrT> slot_id,
    const LazyNode<HeapObject>& maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode, bool rhs_known_smi) {
  auto smiFunction = [=](TNode<Smi> base, TNode<Smi> exponent,
                         TVariable<Smi>* var_type_feedback) {
    *var_type_feedback = SmiConstant(BinaryOperationFeedback::kNumber);
    return AllocateHeapNumberWithValue(
        Float64Pow(SmiToFloat64(base), SmiToFloat64(exponent)));
  };
  auto floatFunction = [=](TNode<Float64T> base, TNode<Float64T> exponent) {
    return Float64Pow(base, exponent);
  };
  return Generate_BinaryOperationWithFeedback(
      context, base, exponent, slot_id, maybe_feedback_vector, smiFunction,
      floatFunction, Operation::kExponentiate, update_feedback_mode,
      rhs_known_smi);
}

TNode<Object> BinaryOpAssembler::Generate_BitwiseBinaryOpWithOptionalFeedback(
    Operation bitwise_op, TNode<Object> left, TNode<Object> right,
    const LazyNode<Context>& context, TNode<UintPtrT>* slot,
    const LazyNode<HeapObject>* maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  TVARIABLE(Object, result);
  TVARIABLE(Smi, var_left_feedback);
  TVARIABLE(Smi, var_right_feedback);
  TVARIABLE(Word32T, var_left_word32);
  TVARIABLE(Word32T, var_right_word32);
  TVARIABLE(BigInt, var_left_bigint);
  Label done(this);
  Label if_left_number(this), do_number_op(this);
  Label if_left_bigint(this), if_left_bigint64(this);
  Label if_left_number_right_bigint(this, Label::kDeferred);

  FeedbackValues feedback =
      slot ? FeedbackValues{&var_left_feedback, maybe_feedback_vector, slot,
                            update_feedback_mode}
           : FeedbackValues();

  TaggedToWord32OrBigIntWithFeedback(
      context(), left, &if_left_number, &var_left_word32, &if_left_bigint,
      IsBigInt64OpSupported(this, bitwise_op) ? &if_left_bigint64 : nullptr,
      &var_left_bigint, feedback);

  BIND(&if_left_number);
  feedback.var_feedback = slot ? &var_right_feedback : nullptr;
  TaggedToWord32OrBigIntWithFeedback(
      context(), right, &do_number_op, &var_right_word32,
      &if_left_number_right_bigint, nullptr, nullptr, feedback);

  BIND(&if_left_number_right_bigint);
  {
    if (slot) {
      // Ensure that the feedback is updated before we throw.
      UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                     (*maybe_feedback_vector)(), *slot, update_feedback_mode);
    }
    ThrowTypeError(context(), MessageTemplate::kBigIntMixedTypes);
  }

  BIND(&do_number_op);
  {
    result = BitwiseOp(var_left_word32.value(), var_right_word32.value(),
                       bitwise_op);

    if (slot) {
      TNode<Smi> result_type = SelectSmiConstant(
          TaggedIsSmi(result.value()), BinaryOperationFeedback::kSignedSmall,
          BinaryOperationFeedback::kNumber);
      TNode<Smi> input_feedback =
          SmiOr(var_left_feedback.value(), var_right_feedback.value());
      TNode<Smi> feedback = SmiOr(result_type, input_feedback);
      UpdateFeedback(feedback, (*maybe_feedback_vector)(), *slot,
                     update_feedback_mode);
    }
    Goto(&done);
  }

  // BigInt cases.
  {
    TVARIABLE(BigInt, var_right_bigint);
    Label if_both_bigint(this), if_both_bigint64(this);
    Label if_bigint_mix(this, Label::kDeferred);

    BIND(&if_left_bigint);
    TaggedToBigInt(context(), right, &if_bigint_mix, &if_both_bigint, nullptr,
                   &var_right_bigint, slot ? &var_right_feedback : nullptr);

    if (IsBigInt64OpSupported(this, bitwise_op)) {
      BIND(&if_left_bigint64);
      TaggedToBigInt(context(), right, &if_bigint_mix, &if_both_bigint,
                     &if_both_bigint64, &var_right_bigint,
                     slot ? &var_right_feedback : nullptr);

      BIND(&if_both_bigint64);
      if (slot) {
        // {feedback} is Any if {left} or {right} is non-number.
        TNode<Smi> feedback =
            SmiOr(var_left_feedback.value(), var_right_feedback.value());
        UpdateFeedback(feedback, (*maybe_feedback_vector)(), *slot,
                       update_feedback_mode);
      }

      TVARIABLE(UintPtrT, left_raw);
      TVARIABLE(UintPtrT, right_raw);
      BigIntToRawBytes(var_left_bigint.value(), &left_raw, &left_raw);
      BigIntToRawBytes(var_right_bigint.value(), &right_raw, &right_raw);

      switch (bitwise_op) {
        case Operation::kBitwiseAnd: {
          result = BigIntFromInt64(UncheckedCast<IntPtrT>(
              WordAnd(left_raw.value(), right_raw.value())));
          Goto(&done);
          break;
        }
        case Operation::kBitwiseOr: {
          result = BigIntFromInt64(UncheckedCast<IntPtrT>(
              WordOr(left_raw.value(), right_raw.value())));
          Goto(&done);
          break;
        }
        case Operation::kBitwiseXor: {
          result = BigIntFromInt64(UncheckedCast<IntPtrT>(
              WordXor(left_raw.value(), right_raw.value())));
          Goto(&done);
          break;
        }
        default:
          UNREACHABLE();
      }
    }

    BIND(&if_both_bigint);
    {
      if (slot) {
        // Ensure that the feedback is updated even if the runtime call below
        // would throw.
        TNode<Smi> feedback =
            SmiOr(var_left_feedback.value(), var_right_feedback.value());
        UpdateFeedback(feedback, (*maybe_feedback_vector)(), *slot,
                       update_feedback_mode);
      }

      switch (bitwise_op) {
        case Operation::kBitwiseAnd: {
          result =
              CallBuiltin(Builtin::kBigIntBitwiseAndNoThrow, context(),
                          var_left_bigint.value(), var_right_bigint.value());
          // Check for sentinel that signals BigIntTooBig exception.
          GotoIfNot(TaggedIsSmi(result.value()), &done);

          if (slot) {
            // Update feedback to prevent deopt loop.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
          break;
        }
        case Operation::kBitwiseOr: {
          result =
              CallBuiltin(Builtin::kBigIntBitwiseOrNoThrow, context(),
                          var_left_bigint.value(), var_right_bigint.value());
          // Check for sentinel that signals BigIntTooBig exception.
          GotoIfNot(TaggedIsSmi(result.value()), &done);

          if (slot) {
            // Update feedback to prevent deopt loop.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
          break;
        }
        case Operation::kBitwiseXor: {
          result =
              CallBuiltin(Builtin::kBigIntBitwiseXorNoThrow, context(),
                          var_left_bigint.value(), var_right_bigint.value());
          // Check for sentinel that signals BigIntTooBig exception.
          GotoIfNot(TaggedIsSmi(result.value()), &done);

          if (slot) {
            // Update feedback to prevent deopt loop.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
          break;
        }
        case Operation::kShiftLeft: {
          result =
              CallBuiltin(Builtin::kBigIntShiftLeftNoThrow, context(),
                          var_left_bigint.value(), var_right_bigint.value());
          // Check for sentinel that signals BigIntTooBig exception.
          GotoIfNot(TaggedIsSmi(result.value()), &done);

          if (slot) {
            // Update feedback to prevent deopt loop.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
          break;
        }
        case Operation::kShiftRight: {
          result =
              CallBuiltin(Builtin::kBigIntShiftRightNoThrow, context(),
                          var_left_bigint.value(), var_right_bigint.value());
          // Check for sentinel that signals BigIntTooBig exception.
          GotoIfNot(TaggedIsSmi(result.value()), &done);

          if (slot) {
            // Update feedback to prevent deopt loop.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          ThrowRangeError(context(), MessageTemplate::kBigIntTooBig);
          break;
        }
        case Operation::kShiftRightLogical: {
          if (slot) {
            // Ensure that the feedback is updated before we throw.
            UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                           (*maybe_feedback_vector)(), *slot,
                           update_feedback_mode);
          }
          // BigInt does not support logical right shift.
          ThrowTypeError(context(), MessageTemplate::kBigIntShr);
          break;
        }
        default:
          UNREACHABLE();
      }
    }

    BIND(&if_bigint_mix);
    {
      if (slot) {
        // Ensure that the feedback is updated before we throw.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       (*maybe_feedback_vector)(), *slot, update_feedback_mode);
      }
      ThrowTypeError(context(), MessageTemplate::kBigIntMixedTypes);
    }
  }

  BIND(&done);
  return result.value();
}

TNode<Object>
BinaryOpAssembler::Generate_BitwiseBinaryOpWithSmiOperandAndOptionalFeedback(
    Operation bitwise_op, TNode<Object> left, TNode<Object> right,
    const LazyNode<Context>& context, TNode<UintPtrT>* slot,
    const LazyNode<HeapObject>* maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  TNode<Smi> right_smi = CAST(right);
  TVARIABLE(Object, result);
  TVARIABLE(Smi, var_left_feedback);
  TVARIABLE(Word32T, var_left_word32);
  TVARIABLE(BigInt, var_left_bigint);
  TVARIABLE(Smi, feedback);
  // Check if the {lhs} is a Smi or a HeapObject.
  Label if_lhsissmi(this), if_lhsisnotsmi(this, Label::kDeferred);
  Label do_number_op(this), if_bigint_mix(this), done(this);

  Branch(TaggedIsSmi(left), &if_lhsissmi, &if_lhsisnotsmi);

  BIND(&if_lhsissmi);
  {
    TNode<Smi> left_smi = CAST(left);
    result = BitwiseSmiOp(left_smi, right_smi, bitwise_op);
    if (slot) {
      if (IsBitwiseOutputKnownSmi(bitwise_op)) {
        feedback = SmiConstant(BinaryOperationFeedback::kSignedSmall);
      } else {
        feedback = SelectSmiConstant(TaggedIsSmi(result.value()),
                                     BinaryOperationFeedback::kSignedSmall,
                                     BinaryOperationFeedback::kNumber);
      }
    }
    Goto(&done);
  }

  BIND(&if_lhsisnotsmi);
  {
    TNode<HeapObject> left_pointer = CAST(left);
    FeedbackValues feedback_values{&var_left_feedback, maybe_feedback_vector,
                                   slot, update_feedback_mode};
    TaggedPointerToWord32OrBigIntWithFeedback(
        context(), left_pointer, &do_number_op, &var_left_word32,
        &if_bigint_mix, nullptr, &var_left_bigint, feedback_values);
    BIND(&do_number_op);
    {
      result =
          BitwiseOp(var_left_word32.value(), SmiToInt32(right_smi), bitwise_op);
      if (slot) {
        TNode<Smi> result_type = SelectSmiConstant(
            TaggedIsSmi(result.value()), BinaryOperationFeedback::kSignedSmall,
            BinaryOperationFeedback::kNumber);
        feedback = SmiOr(result_type, var_left_feedback.value());
      }
      Goto(&done);
    }

    BIND(&if_bigint_mix);
    {
      if (slot) {
        // Ensure that the feedback is updated before we throw.
        UpdateFeedback(SmiConstant(BinaryOperationFeedback::kAny),
                       (*maybe_feedback_vector)(), *slot, update_feedback_mode);
      }
      ThrowTypeError(context(), MessageTemplate::kBigIntMixedTypes);
    }
  }

  BIND(&done);
  if (slot) {
    UpdateFeedback(feedback.value(), (*maybe_feedback_vector)(), *slot,
                   update_feedback_mode);
  }
  return result.value();
}

}  // namespace internal
}  // namespace v8
