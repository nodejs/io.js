// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_JS_GENERIC_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_JS_GENERIC_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// JSGenericLowering lowers JS operators to generic builtin calls (possibly with
// some small inlined fast paths).
//
// It should run after SimplifiedLowering, which should have already replaced
// most of the JS operations with lower levels (Simplified or Machine) more
// specialized operations. However, SimplifiedLowering won't be able to remove
// all JS operators; the remaining JS operations will thus be replaced by
// builtin calls here in JSGenericLowering.

template <class Next>
class JSGenericLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(JSGenericLowering)

  OpIndex REDUCE(GenericBinop)(V<Object> left, V<Object> right,
                               OpIndex frame_state, V<Context> context,
                               GenericBinopOp::Kind kind) {
    // Note that we're **not** calling the __WithFeedback variants of the
    // generic builtins, on purpose. There have been several experiments with
    // this in the past, and we always concluded that it wasn't worth it. The
    // latest experiment was ended with this commit:
    // https://crrev.com/c/4110858.
    switch (kind) {
#define CASE(Name)                    \
  case GenericBinopOp::Kind::k##Name: \
    return __ CallBuiltin_##Name(isolate_, frame_state, context, left, right);
      GENERIC_BINOP_LIST(CASE)
#undef CASE
    }
  }

  OpIndex REDUCE(GenericUnop)(V<Object> input, OpIndex frame_state,
                              V<Context> context, GenericUnopOp::Kind kind) {
    switch (kind) {
#define CASE(Name)                   \
  case GenericUnopOp::Kind::k##Name: \
    return __ CallBuiltin_##Name(isolate_, frame_state, context, input);
      GENERIC_UNOP_LIST(CASE)
#undef CASE
    }
  }

  OpIndex REDUCE(ToNumberOrNumeric)(V<Object> input, OpIndex frame_state,
                                    V<Context> context,
                                    Object::Conversion kind) {
    Label<Object> done(this);
    // Avoid builtin call for Smis and HeapNumbers.
    GOTO_IF(__ ObjectIs(input, ObjectIsOp::Kind::kNumber,
                        ObjectIsOp::InputAssumptions::kNone),
            done, input);
    switch (kind) {
      case Object::Conversion::kToNumber:
        GOTO(done,
             __ CallBuiltin_ToNumber(isolate_, frame_state, context, input));
        break;
      case Object::Conversion::kToNumeric:
        GOTO(done,
             __ CallBuiltin_ToNumeric(isolate_, frame_state, context, input));
        break;
    }
    BIND(done, result);
    return result;
  }

 private:
  Isolate* isolate_ = PipelineData::Get().isolate();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_JS_GENERIC_LOWERING_REDUCER_H_
