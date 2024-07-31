// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/codegen/tnode.h"
#include "src/objects/objects-inl.h"
#include "src/objects/oddball.h"

namespace v8 {
namespace internal {

// ES6 section 7.1.3 ToNumber ( argument )
TF_BUILTIN(ToNumber, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto input = Parameter<Object>(Descriptor::kArgument);

  Return(ToNumber(context, input));
}

TF_BUILTIN(ToBigInt, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto input = Parameter<Object>(Descriptor::kArgument);

  Return(ToBigInt(context, input));
}

TF_BUILTIN(ToNumber_Baseline, CodeStubAssembler) {
  auto input = Parameter<Object>(Descriptor::kArgument);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);
  auto context = [this] { return LoadContextFromBaseline(); };

  TVARIABLE(Smi, var_type_feedback);
  TNode<Number> result = CAST(ToNumberOrNumeric(
      context, input, &var_type_feedback, Object::Conversion::kToNumber));

  auto feedback_vector = LoadFeedbackVectorFromBaseline();
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
  Return(result);
}

TF_BUILTIN(ToNumeric_Baseline, CodeStubAssembler) {
  auto input = Parameter<Object>(Descriptor::kArgument);
  auto slot = UncheckedParameter<UintPtrT>(Descriptor::kSlot);
  auto context = [this] { return LoadContextFromBaseline(); };

  TVARIABLE(Smi, var_type_feedback);
  TNode<Numeric> result = ToNumberOrNumeric(context, input, &var_type_feedback,
                                            Object::Conversion::kToNumeric);

  auto feedback_vector = LoadFeedbackVectorFromBaseline();
  UpdateFeedback(var_type_feedback.value(), feedback_vector, slot);
  Return(result);
}

TF_BUILTIN(PlainPrimitiveToNumber, CodeStubAssembler) {
  auto input = Parameter<Object>(Descriptor::kArgument);

  Return(PlainPrimitiveToNumber(input));
}

// Like ToNumber, but also converts BigInts.
TF_BUILTIN(ToNumberConvertBigInt, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto input = Parameter<Object>(Descriptor::kArgument);

  Return(ToNumber(context, input, BigIntHandling::kConvertToNumber));
}

TF_BUILTIN(ToBigIntConvertNumber, CodeStubAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto input = Parameter<Object>(Descriptor::kArgument);

  Return(ToBigIntConvertNumber(context, input));
}

// ES6 section 7.1.2 ToBoolean ( argument )
// Requires parameter on stack so that it can be used as a continuation from a
// LAZY deopt.
TF_BUILTIN(ToBooleanLazyDeoptContinuation, CodeStubAssembler) {
  auto value = Parameter<Object>(Descriptor::kArgument);

  Label return_true(this), return_false(this);
  BranchIfToBooleanIsTrue(value, &return_true, &return_false);

  BIND(&return_true);
  Return(TrueConstant());

  BIND(&return_false);
  Return(FalseConstant());
}

// Requires parameter on stack so that it can be used as a continuation from a
// LAZY deopt.
TF_BUILTIN(MathRoundContinuation, CodeStubAssembler) {
  auto value = Parameter<Number>(Descriptor::kArgument);
  Return(ChangeFloat64ToTagged(Float64Round(ChangeNumberToFloat64(value))));
}

// Requires parameter on stack so that it can be used as a continuation from a
// LAZY deopt.
TF_BUILTIN(MathFloorContinuation, CodeStubAssembler) {
  auto value = Parameter<Number>(Descriptor::kArgument);
  Return(ChangeFloat64ToTagged(Float64Floor(ChangeNumberToFloat64(value))));
}

// Requires parameter on stack so that it can be used as a continuation from a
// LAZY deopt.
TF_BUILTIN(MathCeilContinuation, CodeStubAssembler) {
  auto value = Parameter<Number>(Descriptor::kArgument);
  Return(ChangeFloat64ToTagged(Float64Ceil(ChangeNumberToFloat64(value))));
}

// ES6 section 12.5.5 typeof operator
TF_BUILTIN(Typeof, CodeStubAssembler) {
  auto object = Parameter<Object>(Descriptor::kObject);

  Return(Typeof(object));
}

}  // namespace internal
}  // namespace v8
