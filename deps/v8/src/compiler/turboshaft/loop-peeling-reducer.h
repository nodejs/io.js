// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class LoopUnrollingReducer;

// LoopPeeling "peels" the first iteration of innermost loops (= it extracts the
// first iteration from the loop). The goal of this is mainly to hoist checks
// out of the loop (such as Smi-checks, type-checks, bound-checks, etc).

template <class Next>
class LoopPeelingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LoopPeeling)

#if defined(__clang__)
  // LoopUnrolling and LoopPeeling shouldn't be performed in the same phase, see
  // the comment in pipeline.cc where LoopUnrolling is triggered.
  static_assert(
      !reducer_list_contains<ReducerList, LoopUnrollingReducer>::value);
#endif

  V<None> REDUCE_INPUT_GRAPH(Goto)(V<None> ig_idx, const GotoOp& gto) {
    // Note that the "ShouldSkipOptimizationStep" is placed in the part of
    // this Reduce method triggering the peeling rather than at the begining.
    // This is because the backedge skipping is not an optimization but a
    // mandatory lowering when peeling is being performed.
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphGoto(ig_idx, gto); }

    const Block* dst = gto.destination;
    if (dst->IsLoop() && !gto.is_backedge && CanPeelLoop(dst)) {
      if (ShouldSkipOptimizationStep()) goto no_change;
      PeelFirstIteration(dst);
      return {};
    } else if (IsPeeling() && dst == current_loop_header_) {
      // We skip the backedge of the loop: PeelFirstIeration will instead emit a
      // forward edge to the non-peeled header.
      return {};
    }

    goto no_change;
  }

  // TODO(dmercadier): remove once StackCheckOp are kept in the pipeline until
  // the very end (which should happen when we have a SimplifiedLowering in
  // Turboshaft).
  V<AnyOrNone> REDUCE_INPUT_GRAPH(Call)(V<AnyOrNone> ig_idx,
                                        const CallOp& call) {
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphCall(ig_idx, call); }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (IsPeeling() && call.IsStackCheck(__ input_graph(), broker_,
                                         StackCheckKind::kJSIterationBody)) {
      // We remove the stack check of the peeled iteration.
      return {};
    }

    goto no_change;
  }

  V<None> REDUCE_INPUT_GRAPH(JSStackCheck)(V<None> ig_idx,
                                           const JSStackCheckOp& stack_check) {
    if (ShouldSkipOptimizationStep() || !IsPeeling()) {
      return Next::ReduceInputGraphJSStackCheck(ig_idx, stack_check);
    }

    // We remove the stack check of the peeled iteration.
    return V<None>::Invalid();
  }

#if V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE_INPUT_GRAPH(WasmStackCheck)(
      V<None> ig_idx, const WasmStackCheckOp& stack_check) {
    if (ShouldSkipOptimizationStep() || !IsPeeling()) {
      return Next::ReduceInputGraphWasmStackCheck(ig_idx, stack_check);
    }

    // We remove the stack check of the peeled iteration.
    return V<None>::Invalid();
  }
#endif

  OpIndex REDUCE_INPUT_GRAPH(Phi)(OpIndex ig_idx, const PhiOp& phi) {
    if (!IsEmittingUnpeeledBody() ||
        __ current_input_block() != current_loop_header_) {
      return Next::ReduceInputGraphPhi(ig_idx, phi);
    }

    // The 1st input of the loop phis of the unpeeled loop header should be the
    // 2nd input of the original loop phis, since with the peeling, they
    // actually come from the backedge of the peeled iteration.
    return __ PendingLoopPhi(
        __ MapToNewGraph(phi.input(PhiOp::kLoopPhiBackEdgeIndex)), phi.rep);
  }

 private:
  static constexpr int kMaxSizeForPeeling = 1000;
  enum class PeelingStatus {
    kNotPeeling,
    kEmittingPeeledLoop,
    kEmittingUnpeeledBody
  };

  void PeelFirstIteration(const Block* header) {
    DCHECK_EQ(peeling_, PeelingStatus::kNotPeeling);
    ScopedModification<PeelingStatus> scope(&peeling_,
                                            PeelingStatus::kEmittingPeeledLoop);
    current_loop_header_ = header;

    // Emitting the peeled iteration.
    auto loop_body = loop_finder_.GetLoopBody(header);
    // Note that this call to CloneSubGraph will not emit the backedge because
    // we'll skip it in ReduceInputGraphGoto (above). The next CloneSubGraph
    // call will start with a forward Goto to the header (like all
    // CloneSubGraphs do), and will end by emitting the backedge, because this
    // time {peeling_} won't be EmittingPeeledLoop, and the backedge Goto will
    // thus be emitted.
    __ CloneSubGraph(loop_body, /* keep_loop_kinds */ false);

    if (__ generating_unreachable_operations()) {
      // While peeling, we realized that the 2nd iteration of the loop is not
      // reachable.
      return;
    }

    // We now emit the regular unpeeled loop.
    peeling_ = PeelingStatus::kEmittingUnpeeledBody;
    __ CloneSubGraph(loop_body, /* keep_loop_kinds */ true,
                     /* is_loop_after_peeling */ true);
  }

  bool CanPeelLoop(const Block* header) {
    if (IsPeeling()) return false;
    auto info = loop_finder_.GetLoopInfo(header);
    return !info.has_inner_loops && info.op_count <= kMaxSizeForPeeling;
  }

  bool IsPeeling() const {
    return peeling_ == PeelingStatus::kEmittingPeeledLoop;
  }
  bool IsEmittingUnpeeledBody() const {
    return peeling_ == PeelingStatus::kEmittingUnpeeledBody;
  }

  PeelingStatus peeling_ = PeelingStatus::kNotPeeling;
  const Block* current_loop_header_ = nullptr;

  LoopFinder loop_finder_{__ phase_zone(), &__ modifiable_input_graph()};
  JSHeapBroker* broker_ = __ data() -> broker();
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_PEELING_REDUCER_H_
