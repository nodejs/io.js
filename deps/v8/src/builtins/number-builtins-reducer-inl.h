// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_NUMBER_BUILTINS_REDUCER_INL_H_
#define V8_BUILTINS_NUMBER_BUILTINS_REDUCER_INL_H_

#include "src/codegen/turboshaft-builtins-assembler-inl.h"

namespace v8::internal {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

using namespace compiler::turboshaft;  // NOLINT(build/namespaces)

template <typename Next>
class NumberBuiltinsReducer : public Next {
 public:
  BUILTIN_REDUCER(NumberBuiltins)

  V<Object> BitwiseNot(V<Context> context, V<Object> input) {
    Label<Object> done(this);
    Label<Word32> if_number(this);
    Label<BigInt> if_bigint(this);
    __ template TaggedToWord32OrBigIntImpl<Object::Conversion::kToNumeric>(
        context, input, IsKnownTaggedPointer::kNo, if_number, &if_bigint,
        nullptr);

    // Number case.
    {
      BIND(if_number, w32);
      V<Number> temp = __ ConvertInt32ToNumber(__ Word32BitwiseNot(w32));
      IF (__ IsSmi(temp)) {
        __ CombineFeedback(BinaryOperationFeedback::kSignedSmall);
      } ELSE {
        __ CombineFeedback(BinaryOperationFeedback::kNumber);
      }
      GOTO(done, temp);
    }

    // BigInt case.
    {
      BIND(if_bigint, bigint_value);
      if (__ HasFeedbackCollector()) {
        // Feedback has been set already in `TaggedToWord32OrBigIntImpl`.
        TSA_DCHECK(this, __ FeedbackIs(BinaryOperationFeedback::kBigInt));
      }
      GOTO(done, __ CallRuntime_BigIntUnaryOp(isolate_, context, bigint_value,
                                              ::Operation::kBitwiseNot));
    }

    BIND(done, result);
    return result;
  }

 private:
  Isolate* isolate_ = __ data() -> isolate();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal

#endif  // V8_BUILTINS_NUMBER_BUILTINS_REDUCER_INL_H_
