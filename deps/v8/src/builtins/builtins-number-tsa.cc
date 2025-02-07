// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/number-builtins-reducer-inl.h"
#include "src/codegen/turboshaft-builtins-assembler-inl.h"

namespace v8::internal {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using namespace compiler::turboshaft;  // NOLINT(build/namespaces)

class NumberBuiltinsAssemblerTS
    : public TurboshaftBuiltinsAssembler<NumberBuiltinsReducer,
                                         FeedbackCollectorReducer> {
 public:
  using Base = TurboshaftBuiltinsAssembler;

  using Base::Asm;
  using Base::Base;
};

#ifdef V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS

TS_BUILTIN(BitwiseNot_WithFeedback, NumberBuiltinsAssemblerTS) {
  // TODO(nicohartmann): It would be great to deduce the parameter type from the
  // Descriptor directly.
  V<Object> value = Parameter<Object>(Descriptor::kValue);
  V<Context> context = Parameter<Context>(Descriptor::kContext);
  V<FeedbackVector> feedback_vector =
      Parameter<FeedbackVector>(Descriptor::kFeedbackVector);
  V<WordPtr> slot = Parameter<WordPtr>(Descriptor::kSlot);

  SetFeedbackSlot(slot);
  SetFeedbackVector(feedback_vector);

  V<Object> result = BitwiseNot(context, value);
  Return(result);
}

#endif  // V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal
