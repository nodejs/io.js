// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REQUIRED_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_REQUIRED_OPTIMIZATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// The RequiredOptimizationReducer performs reductions that might be needed for
// correctness, because instruction selection or other reducers rely on it. In
// particular, we have the following dependencies:
//   - VariableReducer can introduce phi nodes for call target constants, which
//     have to be reduced in order for instruction selection to detect the call
//     target. So we have to run RequiredOptimizationReducer at least once after
//     every occurence of VariableReducer.
//   - Loop peeling/unrolling can introduce phi nodes for RttCanons, which have
//     to be reduced to aid `WasmGCTypedOptimizationReducer` resolve type
//     indices corresponding to RttCanons.
template <class Next>
class RequiredOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(RequiredOptimization)

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    LABEL_BLOCK(no_change) { return Next::ReducePhi(inputs, rep); }
    if (inputs.size() == 0) goto no_change;
    OpIndex first = inputs.first();
    bool same_inputs = true;
    for (const OpIndex& input : inputs.SubVectorFrom(1)) {
      if (input != first) {
        same_inputs = false;
        break;
      }
    }
    if (same_inputs) {
      return first;
    }
    if (const ConstantOp* first_constant =
            __ Get(first).template TryCast<ConstantOp>()) {
      for (const OpIndex& input : inputs.SubVectorFrom(1)) {
        const ConstantOp* maybe_constant =
            __ Get(input).template TryCast<ConstantOp>();
        if (!(maybe_constant && *maybe_constant == *first_constant)) {
          goto no_change;
        }
      }
      // If all of the predecessors are the same Constant, then we re-emit
      // this Constant rather than emitting a Phi. This is a good idea in
      // general, but is in particular needed for Constant that are used as
      // call target: if they were merged into a Phi, this would result in an
      // indirect call rather than a direct one, which:
      //   - is probably slower than a direct call in general
      //   - is probably not supported for builtins on 32-bit architectures.
      return __ ReduceConstant(first_constant->kind, first_constant->storage);
    }
#if V8_ENABLE_WEBASSEMBLY
    if (const RttCanonOp* first_rtt =
            __ Get(first).template TryCast<RttCanonOp>()) {
      for (const OpIndex& input : inputs.SubVectorFrom(1)) {
        const RttCanonOp* maybe_rtt =
            __ Get(input).template TryCast<RttCanonOp>();
        if (!(maybe_rtt && maybe_rtt->rtts() == first_rtt->rtts() &&
              maybe_rtt->type_index == first_rtt->type_index)) {
          goto no_change;
        }
      }
      // If all of the predecessors are the same RttCanon, then we re-emit this
      // RttCanon rather than emitting a Phi. This helps the subsequent
      // phases (in particular, `WasmGCTypedOptimizationReducer`) to resolve the
      // type index corresponding to an RttCanon.
      // Note: this relies on all RttCanons having the same `rtts()` input,
      // which is the case due to instance field caching during graph
      // generation.
      // TODO(manoskouk): Can we generalize these two (and possibly more) cases?
      return __ ReduceRttCanon(first_rtt->rtts(), first_rtt->type_index);
    }
#endif
    goto no_change;
  }
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_REQUIRED_OPTIMIZATION_REDUCER_H_
