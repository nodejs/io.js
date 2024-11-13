// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ic/unary-op-assembler.h"

#include "src/common/globals.h"
#include "torque-generated/src/objects/oddball-tq-csa.h"

namespace v8 {
namespace internal {

namespace {

class UnaryOpAssemblerImpl final : public CodeStubAssembler {
 public:
  explicit UnaryOpAssemblerImpl(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  TNode<Object> BitwiseNot(TNode<Context> context, TNode<Object> value,
                           TNode<UintPtrT> slot,
                           TNode<HeapObject> maybe_feedback_vector,
                           UpdateFeedbackMode update_feedback_mode) {
    // TODO(jgruber): Make this implementation more consistent with other unary
    // ops (i.e. have them all use UnaryOpWithFeedback or some other common
    // mechanism).
    TVARIABLE(Word32T, var_word32);
    TVARIABLE(Smi, var_feedback);
    TVARIABLE(BigInt, var_bigint);
    TVARIABLE(Object, var_result);
    Label if_number(this), if_bigint(this, Label::kDeferred), out(this);
    LazyNode<HeapObject> get_vector = [&]() { return maybe_feedback_vector; };
    FeedbackValues feedback = {&var_feedback, &get_vector, &slot,
                               update_feedback_mode};
    TaggedToWord32OrBigIntWithFeedback(context, value, &if_number, &var_word32,
                                       &if_bigint, nullptr, &var_bigint,
                                       feedback);

    // Number case.
    BIND(&if_number);
    var_result =
        ChangeInt32ToTagged(Signed(Word32BitwiseNot(var_word32.value())));
    TNode<Smi> result_type = SelectSmiConstant(
        TaggedIsSmi(var_result.value()), BinaryOperationFeedback::kSignedSmall,
        BinaryOperationFeedback::kNumber);
    UpdateFeedback(SmiOr(result_type, var_feedback.value()),
                   maybe_feedback_vector, slot, update_feedback_mode);
    Goto(&out);

    // BigInt case.
    BIND(&if_bigint);
    UpdateFeedback(SmiConstant(BinaryOperationFeedback::kBigInt),
                   maybe_feedback_vector, slot, update_feedback_mode);
    var_result =
        CallRuntime(Runtime::kBigIntUnaryOp, context, var_bigint.value(),
                    SmiConstant(Operation::kBitwiseNot));
    Goto(&out);

    BIND(&out);
    return var_result.value();
  }

  TNode<Object> Decrement(TNode<Context> context, TNode<Object> value,
                          TNode<UintPtrT> slot,
                          TNode<HeapObject> maybe_feedback_vector,
                          UpdateFeedbackMode update_feedback_mode) {
    return IncrementOrDecrement<Operation::kDecrement>(
        context, value, slot, maybe_feedback_vector, update_feedback_mode);
  }

  TNode<Object> Increment(TNode<Context> context, TNode<Object> value,
                          TNode<UintPtrT> slot,
                          TNode<HeapObject> maybe_feedback_vector,
                          UpdateFeedbackMode update_feedback_mode) {
    return IncrementOrDecrement<Operation::kIncrement>(
        context, value, slot, maybe_feedback_vector, update_feedback_mode);
  }

  TNode<Object> Negate(TNode<Context> context, TNode<Object> value,
                       TNode<UintPtrT> slot,
                       TNode<HeapObject> maybe_feedback_vector,
                       UpdateFeedbackMode update_feedback_mode) {
    SmiOperation smi_op =
        [=, this](TNode<Smi> smi_value, TVariable<Smi>* var_feedback,
                  Label* do_float_op, TVariable<Float64T>* var_float) {
          TVARIABLE(Number, var_result);
          Label if_zero(this), if_min_smi(this), end(this);
          // Return -0 if operand is 0.
          GotoIf(SmiEqual(smi_value, SmiConstant(0)), &if_zero);

          // Special-case the minimum Smi to avoid overflow.
          GotoIf(SmiEqual(smi_value, SmiConstant(Smi::kMinValue)), &if_min_smi);

          // Else simply subtract operand from 0.
          CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
          var_result = SmiSub(SmiConstant(0), smi_value);
          Goto(&end);

          BIND(&if_zero);
          CombineFeedback(var_feedback, BinaryOperationFeedback::kNumber);
          var_result = MinusZeroConstant();
          Goto(&end);

          BIND(&if_min_smi);
          *var_float = SmiToFloat64(smi_value);
          Goto(do_float_op);

          BIND(&end);
          return var_result.value();
        };
    FloatOperation float_op = [=, this](TNode<Float64T> float_value) {
      return Float64Neg(float_value);
    };
    BigIntOperation bigint_op = [=, this](TNode<Context> context,
                                          TNode<HeapObject> bigint_value) {
      return CAST(CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                              SmiConstant(Operation::kNegate)));
    };
    return UnaryOpWithFeedback(context, value, slot, maybe_feedback_vector,
                               smi_op, float_op, bigint_op,
                               update_feedback_mode);
  }

 private:
  using SmiOperation = std::function<TNode<Number>(
      TNode<Smi> /* smi_value */, TVariable<Smi>* /* var_feedback */,
      Label* /* do_float_op */, TVariable<Float64T>* /* var_float */)>;
  using FloatOperation =
      std::function<TNode<Float64T>(TNode<Float64T> /* float_value */)>;
  using BigIntOperation = std::function<TNode<HeapObject>(
      TNode<Context> /* context */, TNode<HeapObject> /* bigint_value */)>;

  TNode<Object> UnaryOpWithFeedback(TNode<Context> context, TNode<Object> value,
                                    TNode<UintPtrT> slot,
                                    TNode<HeapObject> maybe_feedback_vector,
                                    const SmiOperation& smi_op,
                                    const FloatOperation& float_op,
                                    const BigIntOperation& bigint_op,
                                    UpdateFeedbackMode update_feedback_mode) {
    TVARIABLE(Object, var_value, value);
    TVARIABLE(Object, var_result);
    TVARIABLE(Float64T, var_float_value);
    TVARIABLE(Smi, var_feedback, SmiConstant(BinaryOperationFeedback::kNone));
    TVARIABLE(Object, var_exception);
    Label start(this, {&var_value, &var_feedback}), end(this);
    Label do_float_op(this, &var_float_value);
    Label if_exception(this, Label::kDeferred);
    Goto(&start);
    // We might have to try again after ToNumeric conversion.
    BIND(&start);
    {
      Label if_smi(this), if_heapnumber(this), if_oddball(this);
      Label if_bigint(this, Label::kDeferred);
      Label if_other(this, Label::kDeferred);
      value = var_value.value();
      GotoIf(TaggedIsSmi(value), &if_smi);

      TNode<HeapObject> value_heap_object = CAST(value);
      TNode<Map> map = LoadMap(value_heap_object);
      GotoIf(IsHeapNumberMap(map), &if_heapnumber);
      TNode<Uint16T> instance_type = LoadMapInstanceType(map);
      GotoIf(IsBigIntInstanceType(instance_type), &if_bigint);
      Branch(InstanceTypeEqual(instance_type, ODDBALL_TYPE), &if_oddball,
             &if_other);

      BIND(&if_smi);
      {
        var_result =
            smi_op(CAST(value), &var_feedback, &do_float_op, &var_float_value);
        Goto(&end);
      }

      BIND(&if_heapnumber);
      {
        var_float_value = LoadHeapNumberValue(value_heap_object);
        Goto(&do_float_op);
      }

      BIND(&if_bigint);
      {
        var_result = bigint_op(context, value_heap_object);
        CombineFeedback(&var_feedback, BinaryOperationFeedback::kBigInt);
        Goto(&end);
      }

      BIND(&if_oddball);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_DCHECK(this, SmiEqual(var_feedback.value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
        OverwriteFeedback(&var_feedback,
                          BinaryOperationFeedback::kNumberOrOddball);
        var_value = LoadOddballToNumber(CAST(value_heap_object));
        Goto(&start);
      }

      BIND(&if_other);
      {
        // We do not require an Or with earlier feedback here because once we
        // convert the value to a number, we cannot reach this path. We can
        // only reach this path on the first pass when the feedback is kNone.
        CSA_DCHECK(this, SmiEqual(var_feedback.value(),
                                  SmiConstant(BinaryOperationFeedback::kNone)));
        OverwriteFeedback(&var_feedback, BinaryOperationFeedback::kAny);
        {
          ScopedExceptionHandler handler(this, &if_exception, &var_exception);
          var_value = CallBuiltin(Builtin::kNonNumberToNumeric, context,
                                  value_heap_object);
        }
        Goto(&start);
      }
    }

    BIND(&if_exception);
    {
      UpdateFeedback(var_feedback.value(), maybe_feedback_vector, slot,
                     update_feedback_mode);
      CallRuntime(Runtime::kReThrow, context, var_exception.value());
      Unreachable();
    }

    BIND(&do_float_op);
    {
      CombineFeedback(&var_feedback, BinaryOperationFeedback::kNumber);
      var_result =
          AllocateHeapNumberWithValue(float_op(var_float_value.value()));
      Goto(&end);
    }

    BIND(&end);
    UpdateFeedback(var_feedback.value(), maybe_feedback_vector, slot,
                   update_feedback_mode);
    return var_result.value();
  }

  template <Operation kOperation>
  TNode<Object> IncrementOrDecrement(TNode<Context> context,
                                     TNode<Object> value, TNode<UintPtrT> slot,
                                     TNode<HeapObject> maybe_feedback_vector,
                                     UpdateFeedbackMode update_feedback_mode) {
    static_assert(kOperation == Operation::kIncrement ||
                  kOperation == Operation::kDecrement);
    static constexpr int kAddValue =
        (kOperation == Operation::kIncrement) ? 1 : -1;

    SmiOperation smi_op = [=, this](TNode<Smi> smi_value,
                                    TVariable<Smi>* var_feedback,
                                    Label* do_float_op,
                                    TVariable<Float64T>* var_float) {
      Label if_overflow(this), out(this);
      TNode<Smi> result =
          TrySmiAdd(smi_value, SmiConstant(kAddValue), &if_overflow);
      CombineFeedback(var_feedback, BinaryOperationFeedback::kSignedSmall);
      Goto(&out);

      BIND(&if_overflow);
      *var_float = SmiToFloat64(smi_value);
      Goto(do_float_op);

      BIND(&out);
      return result;
    };
    FloatOperation float_op = [=, this](TNode<Float64T> float_value) {
      return Float64Add(float_value, Float64Constant(kAddValue));
    };
    BigIntOperation bigint_op = [=, this](TNode<Context> context,
                                          TNode<HeapObject> bigint_value) {
      return CAST(CallRuntime(Runtime::kBigIntUnaryOp, context, bigint_value,
                              SmiConstant(kOperation)));
    };
    return UnaryOpWithFeedback(context, value, slot, maybe_feedback_vector,
                               smi_op, float_op, bigint_op,
                               update_feedback_mode);
  }
};

}  // namespace

TNode<Object> UnaryOpAssembler::Generate_BitwiseNotWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  UnaryOpAssemblerImpl a(state_);
  return a.BitwiseNot(context, value, slot, maybe_feedback_vector,
                      update_feedback_mode);
}

TNode<Object> UnaryOpAssembler::Generate_DecrementWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  UnaryOpAssemblerImpl a(state_);
  return a.Decrement(context, value, slot, maybe_feedback_vector,
                     update_feedback_mode);
}

TNode<Object> UnaryOpAssembler::Generate_IncrementWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  UnaryOpAssemblerImpl a(state_);
  return a.Increment(context, value, slot, maybe_feedback_vector,
                     update_feedback_mode);
}

TNode<Object> UnaryOpAssembler::Generate_NegateWithFeedback(
    TNode<Context> context, TNode<Object> value, TNode<UintPtrT> slot,
    TNode<HeapObject> maybe_feedback_vector,
    UpdateFeedbackMode update_feedback_mode) {
  UnaryOpAssemblerImpl a(state_);
  return a.Negate(context, value, slot, maybe_feedback_vector,
                  update_feedback_mode);
}

}  // namespace internal
}  // namespace v8
