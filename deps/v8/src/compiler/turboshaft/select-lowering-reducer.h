// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_

#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// Lowers Select operations to diamonds.
//
// A Select is conceptually somewhat similar to a ternary if:
//
//       res = Select(cond, val_true, val_false)
//
// means:
//
//       res = cond ? val_true : val_false
//
// SelectLoweringReducer lowers such operations into:
//
//     if (cond) {
//         res = val_true
//     } else {
//         res = val_false
//     }

template <class Next>
class SelectLoweringReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(SelectLowering)

  V<Any> REDUCE(Select)(V<Word32> cond, V<Any> vtrue, V<Any> vfalse,
                        RegisterRepresentation rep, BranchHint hint,
                        SelectOp::Implementation implem) {
    if (implem == SelectOp::Implementation::kCMove) {
      // We do not lower Select operations that should be implemented with
      // CMove.
      return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    }

    Variable result = __ NewLoopInvariantVariable(rep);
    IF (cond) {
      __ SetVariable(result, vtrue);
    } ELSE {
      __ SetVariable(result, vfalse);
    }

    return __ GetVariable(result);
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_SELECT_LOWERING_REDUCER_H_
