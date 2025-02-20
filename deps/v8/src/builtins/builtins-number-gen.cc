// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/ic/binary-op-assembler.h"
#include "src/ic/unary-op-assembler.h"

namespace v8 {
namespace internal {

#include "src/codegen/define-code-stub-assembler-macros.inc"

// -----------------------------------------------------------------------------
// ES6 section 20.1 Number Objects

#define DEF_BINOP(Name, Generator)                                           \
  TF_BUILTIN(Name, CodeStubAssembler) {                                      \
    auto lhs = Parameter<Object>(Descriptor::kLeft);                         \
    auto rhs = Parameter<Object>(Descriptor::kRight);                        \
    auto context = Parameter<Context>(Descriptor::kContext);                 \
    auto feedback_vector =                                                   \
        Parameter<FeedbackVector>(Descriptor::kFeedbackVector);              \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);             \
                                                                             \
    BinaryOpAssembler binop_asm(state());                                    \
    TNode<Object> result =                                                   \
        binop_asm.Generator([&]() { return context; }, lhs, rhs, slot,       \
                            [&]() { return feedback_vector; },               \
                            UpdateFeedbackMode::kGuaranteedFeedback, false); \
                                                                             \
    Return(result);                                                          \
  }
DEF_BINOP(Add_WithFeedback, Generate_AddWithFeedback)
DEF_BINOP(Subtract_WithFeedback, Generate_SubtractWithFeedback)
DEF_BINOP(Multiply_WithFeedback, Generate_MultiplyWithFeedback)
DEF_BINOP(Divide_WithFeedback, Generate_DivideWithFeedback)
DEF_BINOP(Modulus_WithFeedback, Generate_ModulusWithFeedback)
DEF_BINOP(Exponentiate_WithFeedback, Generate_ExponentiateWithFeedback)
DEF_BINOP(BitwiseOr_WithFeedback, Generate_BitwiseOrWithFeedback)
DEF_BINOP(BitwiseXor_WithFeedback, Generate_BitwiseXorWithFeedback)
DEF_BINOP(BitwiseAnd_WithFeedback, Generate_BitwiseAndWithFeedback)
DEF_BINOP(ShiftLeft_WithFeedback, Generate_ShiftLeftWithFeedback)
DEF_BINOP(ShiftRight_WithFeedback, Generate_ShiftRightWithFeedback)
DEF_BINOP(ShiftRightLogical_WithFeedback,
          Generate_ShiftRightLogicalWithFeedback)
#undef DEF_BINOP

#define DEF_BINOP(Name, Generator)                                   \
  TF_BUILTIN(Name, CodeStubAssembler) {                              \
    auto lhs = Parameter<Object>(Descriptor::kLeft);                 \
    auto rhs = Parameter<Object>(Descriptor::kRight);                \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);     \
                                                                     \
    BinaryOpAssembler binop_asm(state());                            \
    TNode<Object> result = binop_asm.Generator(                      \
        [&]() { return LoadContextFromBaseline(); }, lhs, rhs, slot, \
        [&]() { return LoadFeedbackVectorFromBaseline(); },          \
        UpdateFeedbackMode::kGuaranteedFeedback, false);             \
                                                                     \
    Return(result);                                                  \
  }
DEF_BINOP(Add_Baseline, Generate_AddWithFeedback)
DEF_BINOP(Subtract_Baseline, Generate_SubtractWithFeedback)
DEF_BINOP(Multiply_Baseline, Generate_MultiplyWithFeedback)
DEF_BINOP(Divide_Baseline, Generate_DivideWithFeedback)
DEF_BINOP(Modulus_Baseline, Generate_ModulusWithFeedback)
DEF_BINOP(Exponentiate_Baseline, Generate_ExponentiateWithFeedback)
DEF_BINOP(BitwiseOr_Baseline, Generate_BitwiseOrWithFeedback)
DEF_BINOP(BitwiseXor_Baseline, Generate_BitwiseXorWithFeedback)
DEF_BINOP(BitwiseAnd_Baseline, Generate_BitwiseAndWithFeedback)
DEF_BINOP(ShiftLeft_Baseline, Generate_ShiftLeftWithFeedback)
DEF_BINOP(ShiftRight_Baseline, Generate_ShiftRightWithFeedback)
DEF_BINOP(ShiftRightLogical_Baseline, Generate_ShiftRightLogicalWithFeedback)
#undef DEF_BINOP

#define DEF_BINOP_RHS_SMI(Name, Generator)                           \
  TF_BUILTIN(Name, CodeStubAssembler) {                              \
    auto lhs = Parameter<Object>(Descriptor::kLeft);                 \
    auto rhs = Parameter<Object>(Descriptor::kRight);                \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);     \
                                                                     \
    BinaryOpAssembler binop_asm(state());                            \
    TNode<Object> result = binop_asm.Generator(                      \
        [&]() { return LoadContextFromBaseline(); }, lhs, rhs, slot, \
        [&]() { return LoadFeedbackVectorFromBaseline(); },          \
        UpdateFeedbackMode::kGuaranteedFeedback, true);              \
                                                                     \
    Return(result);                                                  \
  }
DEF_BINOP_RHS_SMI(AddSmi_Baseline, Generate_AddWithFeedback)
DEF_BINOP_RHS_SMI(SubtractSmi_Baseline, Generate_SubtractWithFeedback)
DEF_BINOP_RHS_SMI(MultiplySmi_Baseline, Generate_MultiplyWithFeedback)
DEF_BINOP_RHS_SMI(DivideSmi_Baseline, Generate_DivideWithFeedback)
DEF_BINOP_RHS_SMI(ModulusSmi_Baseline, Generate_ModulusWithFeedback)
DEF_BINOP_RHS_SMI(ExponentiateSmi_Baseline, Generate_ExponentiateWithFeedback)
DEF_BINOP_RHS_SMI(BitwiseOrSmi_Baseline, Generate_BitwiseOrWithFeedback)
DEF_BINOP_RHS_SMI(BitwiseXorSmi_Baseline, Generate_BitwiseXorWithFeedback)
DEF_BINOP_RHS_SMI(BitwiseAndSmi_Baseline, Generate_BitwiseAndWithFeedback)
DEF_BINOP_RHS_SMI(ShiftLeftSmi_Baseline, Generate_ShiftLeftWithFeedback)
DEF_BINOP_RHS_SMI(ShiftRightSmi_Baseline, Generate_ShiftRightWithFeedback)
DEF_BINOP_RHS_SMI(ShiftRightLogicalSmi_Baseline,
                  Generate_ShiftRightLogicalWithFeedback)
#undef DEF_BINOP_RHS_SMI

#define DEF_UNOP(Name, Generator)                                \
  TF_BUILTIN(Name, CodeStubAssembler) {                          \
    auto value = Parameter<Object>(Descriptor::kValue);          \
    auto context = Parameter<Context>(Descriptor::kContext);     \
    auto feedback_vector =                                       \
        Parameter<FeedbackVector>(Descriptor::kFeedbackVector);  \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot); \
                                                                 \
    UnaryOpAssembler a(state());                                 \
    TNode<Object> result =                                       \
        a.Generator(context, value, slot, feedback_vector,       \
                    UpdateFeedbackMode::kGuaranteedFeedback);    \
                                                                 \
    Return(result);                                              \
  }
#ifndef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS
DEF_UNOP(BitwiseNot_WithFeedback, Generate_BitwiseNotWithFeedback)
#endif
DEF_UNOP(Decrement_WithFeedback, Generate_DecrementWithFeedback)
DEF_UNOP(Increment_WithFeedback, Generate_IncrementWithFeedback)
DEF_UNOP(Negate_WithFeedback, Generate_NegateWithFeedback)
#undef DEF_UNOP

#define DEF_UNOP(Name, Generator)                                \
  TF_BUILTIN(Name, CodeStubAssembler) {                          \
    auto value = Parameter<Object>(Descriptor::kValue);          \
    auto context = LoadContextFromBaseline();                    \
    auto feedback_vector = LoadFeedbackVectorFromBaseline();     \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot); \
                                                                 \
    UnaryOpAssembler a(state());                                 \
    TNode<Object> result =                                       \
        a.Generator(context, value, slot, feedback_vector,       \
                    UpdateFeedbackMode::kGuaranteedFeedback);    \
                                                                 \
    Return(result);                                              \
  }
DEF_UNOP(BitwiseNot_Baseline, Generate_BitwiseNotWithFeedback)
DEF_UNOP(Decrement_Baseline, Generate_DecrementWithFeedback)
DEF_UNOP(Increment_Baseline, Generate_IncrementWithFeedback)
DEF_UNOP(Negate_Baseline, Generate_NegateWithFeedback)
#undef DEF_UNOP

#define DEF_COMPARE(Name)                                                  \
  TF_BUILTIN(Name##_WithFeedback, CodeStubAssembler) {                     \
    auto lhs = Parameter<Object>(Descriptor::kLeft);                       \
    auto rhs = Parameter<Object>(Descriptor::kRight);                      \
    auto context = Parameter<Context>(Descriptor::kContext);               \
    auto feedback_vector =                                                 \
        Parameter<FeedbackVector>(Descriptor::kFeedbackVector);            \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);           \
                                                                           \
    TVARIABLE(Smi, var_type_feedback);                                     \
    TVARIABLE(Object, var_exception);                                      \
    Label if_exception(this, Label::kDeferred);                            \
    TNode<Boolean> result;                                                 \
    {                                                                      \
      ScopedExceptionHandler handler(this, &if_exception, &var_exception); \
      result = RelationalComparison(Operation::k##Name, lhs, rhs, context, \
                                    &var_type_feedback);                   \
    }                                                                      \
    UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);      \
                                                                           \
    Return(result);                                                        \
    BIND(&if_exception);                                                   \
    {                                                                      \
      UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);    \
      CallRuntime(Runtime::kReThrow, context, var_exception.value());      \
      Unreachable();                                                       \
    }                                                                      \
  }
DEF_COMPARE(LessThan)
DEF_COMPARE(LessThanOrEqual)
DEF_COMPARE(GreaterThan)
DEF_COMPARE(GreaterThanOrEqual)
#undef DEF_COMPARE

#define DEF_COMPARE(Name)                                                   \
  TF_BUILTIN(Name##_Baseline, CodeStubAssembler) {                          \
    auto lhs = Parameter<Object>(Descriptor::kLeft);                        \
    auto rhs = Parameter<Object>(Descriptor::kRight);                       \
    auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);            \
                                                                            \
    TVARIABLE(Smi, var_type_feedback);                                      \
    TVARIABLE(Object, var_exception);                                       \
    Label if_exception(this, Label::kDeferred);                             \
    TNode<Boolean> result;                                                  \
    {                                                                       \
      ScopedExceptionHandler handler(this, &if_exception, &var_exception);  \
      result = RelationalComparison(                                        \
          Operation::k##Name, lhs, rhs,                                     \
          [&]() { return LoadContextFromBaseline(); }, &var_type_feedback); \
    }                                                                       \
    auto feedback_vector = LoadFeedbackVectorFromBaseline();                \
    UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);       \
                                                                            \
    Return(result);                                                         \
    BIND(&if_exception);                                                    \
    {                                                                       \
      feedback_vector = LoadFeedbackVectorFromBaseline();                   \
      UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);     \
      CallRuntime(Runtime::kReThrow, LoadContextFromBaseline(),             \
                  var_exception.value());                                   \
      Unreachable();                                                        \
    }                                                                       \
  }
DEF_COMPARE(LessThan)
DEF_COMPARE(LessThanOrEqual)
DEF_COMPARE(GreaterThan)
DEF_COMPARE(GreaterThanOrEqual)
#undef DEF_COMPARE

TF_BUILTIN(Equal_WithFeedback, CodeStubAssembler) {
  auto lhs = Parameter<Object>(Descriptor::kLeft);
  auto rhs = Parameter<Object>(Descriptor::kRight);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto feedback_vector = Parameter<FeedbackVector>(Descriptor::kFeedbackVector);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  TVARIABLE(Smi, var_type_feedback);
  TVARIABLE(Object, var_exception);
  Label if_exception(this, Label::kDeferred);
  TNode<Boolean> result;
  {
    ScopedExceptionHandler handler(this, &if_exception, &var_exception);
    result = Equal(lhs, rhs, [&]() { return context; }, &var_type_feedback);
  }
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
  Return(result);

  BIND(&if_exception);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
  CallRuntime(Runtime::kReThrow, LoadContextFromBaseline(),
              var_exception.value());
  Unreachable();
}

TF_BUILTIN(StrictEqual_WithFeedback, CodeStubAssembler) {
  auto lhs = Parameter<Object>(Descriptor::kLeft);
  auto rhs = Parameter<Object>(Descriptor::kRight);
  auto feedback_vector = Parameter<FeedbackVector>(Descriptor::kFeedbackVector);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  TVARIABLE(Smi, var_type_feedback);
  TNode<Boolean> result = StrictEqual(lhs, rhs, &var_type_feedback);
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);

  Return(result);
}

TF_BUILTIN(Equal_Baseline, CodeStubAssembler) {
  auto lhs = Parameter<Object>(Descriptor::kLeft);
  auto rhs = Parameter<Object>(Descriptor::kRight);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  TVARIABLE(Smi, var_type_feedback);
  TVARIABLE(Object, var_exception);
  Label if_exception(this, Label::kDeferred);
  TNode<Boolean> result;
  {
    ScopedExceptionHandler handler(this, &if_exception, &var_exception);
    result = Equal(
        lhs, rhs, [&]() { return LoadContextFromBaseline(); },
        &var_type_feedback);
  }
  auto feedback_vector = LoadFeedbackVectorFromBaseline();
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
  Return(result);

  BIND(&if_exception);
  {
    feedback_vector = LoadFeedbackVectorFromBaseline();
    UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
    CallRuntime(Runtime::kReThrow, LoadContextFromBaseline(),
                var_exception.value());
    Unreachable();
  }
}

TF_BUILTIN(StrictEqual_Baseline, CodeStubAssembler) {
  auto lhs = Parameter<Object>(Descriptor::kLeft);
  auto rhs = Parameter<Object>(Descriptor::kRight);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);

  TVARIABLE(Smi, var_type_feedback);
  TNode<Boolean> result = StrictEqual(lhs, rhs, &var_type_feedback);
  auto feedback_vector = LoadFeedbackVectorFromBaseline();
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);

  Return(result);
}

#include "src/codegen/undef-code-stub-assembler-macros.inc"

}  // namespace internal
}  // namespace v8
