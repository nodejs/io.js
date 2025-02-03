// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SIMPLIFIED_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_SIMPLIFIED_LOWERING_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/utils.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename Next>
class SimplifiedLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(SimplifiedLowering)

  using Rep = RegisterRepresentation;

  enum class CheckKind {
    kNone,
    kSigned32,
  };

  OpIndex REDUCE_INPUT_GRAPH(SpeculativeNumberBinop)(
      OpIndex ig_index, const SpeculativeNumberBinopOp& op) {
    DCHECK_EQ(op.kind, SpeculativeNumberBinopOp::Kind::kSafeIntegerAdd);

    OpIndex frame_state = Map(op.frame_state());
    V<Word32> left = ProcessInput(Map(op.left()), Rep::Word32(),
                                  CheckKind::kSigned32, frame_state);
    V<Word32> right = ProcessInput(Map(op.right()), Rep::Word32(),
                                   CheckKind::kSigned32, frame_state);

    V<Tuple<Word32, Word32>> result = __ Int32AddCheckOverflow(left, right);

    V<Word32> overflow = __ template Projection<1>(result);
    __ DeoptimizeIf(overflow, Map(op.frame_state()),
                    DeoptimizeReason::kOverflow, FeedbackSource{});
    return __ template Projection<0>(result);
  }

  OpIndex REDUCE_INPUT_GRAPH(Return)(OpIndex ig_index, const ReturnOp& ret) {
    base::SmallVector<OpIndex, 8> return_values;
    for (OpIndex input : ret.return_values()) {
      return_values.push_back(
          ProcessInput(Map(input), Rep::Tagged(), CheckKind::kNone, {}));
    }

    __ Return(Map(ret.pop_count()), base::VectorOf(return_values));
    return OpIndex::Invalid();
  }

  OpIndex ProcessInput(OpIndex input_index, RegisterRepresentation target_rep,
                       CheckKind type_check, OptionalOpIndex frame_state) {
    const Operation& input_op = __ output_graph().Get(input_index);
    DCHECK_EQ(input_op.outputs_rep().size(), 1);

    RegisterRepresentation output_rep = input_op.outputs_rep()[0];
    if (output_rep == target_rep && type_check == CheckKind::kNone) {
      return input_index;
    }

    switch (multi(output_rep, target_rep)) {
      case multi(Rep::Tagged(), Rep::Word32()): {
        // TODO(nicohartmann@): More cases.
        DCHECK_EQ(type_check, CheckKind::kSigned32);
        return __ ConvertJSPrimitiveToUntaggedOrDeopt(
            input_index, frame_state.value(),
            ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber,
            ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
            CheckForMinusZeroMode::kCheckForMinusZero, {});
      }
      case multi(Rep::Word32(), Rep::Tagged()): {
        // TODO(nicohartmann@): More cases.
        return __ ConvertUntaggedToJSPrimitive(
            input_index,
            ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber,
            Rep::Word32(),
            ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
            CheckForMinusZeroMode::kDontCheckForMinusZero);
      }

      default:
        UNIMPLEMENTED();
    }
  }

  inline OpIndex Map(OpIndex ig_index) { return __ MapToNewGraph(ig_index); }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SIMPLIFIED_LOWERING_REDUCER_H_
