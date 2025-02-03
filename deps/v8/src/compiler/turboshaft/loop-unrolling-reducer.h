// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_

#include <optional>

#include "src/base/logging.h"
#include "src/compiler/globals.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// OVERVIEW:
//
// LoopUnrollingReducer fully unrolls small inner loops with a small
// statically-computable number of iterations, partially unrolls other small
// inner loops, and remove loops that we detect as always having 0 iterations.

class IterationCount {
  enum class Kind { kExact, kApprox, kUnknown };

 public:
  // Loops with an exact number of iteration could be unrolled.
  static IterationCount Exact(size_t count) {
    return IterationCount(Kind::kExact, count);
  }
  // We can remove stack checks from loops with a small number of iterations.
  static IterationCount Approx(size_t count) {
    return IterationCount(Kind::kApprox, count);
  }
  static IterationCount Unknown() { return IterationCount(Kind::kUnknown); }

  IterationCount() : kind_(Kind::kUnknown) {}
  explicit IterationCount(Kind kind) : kind_(kind) {
    DCHECK_NE(kind, Kind::kExact);
  }
  IterationCount(Kind kind, size_t count) : kind_(kind), count_(count) {
    DCHECK_EQ(kind, any_of(Kind::kExact, Kind::kApprox));
  }

  size_t exact_count() const {
    DCHECK_EQ(kind_, Kind::kExact);
    return count_;
  }

  bool IsExact() const { return kind_ == Kind::kExact; }
  bool IsApprox() const { return kind_ == Kind::kApprox; }
  bool IsUnknown() const { return kind_ == Kind::kUnknown; }

  bool IsSmallerThan(size_t max) {
    return (IsExact() || IsApprox()) && count_ < max;
  }

 private:
  Kind kind_;
  size_t count_;
};
std::ostream& operator<<(std::ostream& os, const IterationCount& count);

class V8_EXPORT_PRIVATE StaticCanonicalForLoopMatcher {
  // In the context of this class, a "static canonical for-loop" is one of the
  // form `for (let i = cst; i cmp cst; i = i binop cst)`. That is, a fairly
  // simple for-loop, for which we can statically compute the number of
  // iterations.
  //
  // There is an added constraint that this class can only match loops with few
  // iterations (controlled by the `max_iter_` parameter), for performance
  // reasons (because it's a bit tricky to compute how many iterations a loop
  // has, see the `HasFewerIterationsThan` method).
  //
  // This class and its methods are not in OperationMatcher, even though they
  // could fit there, because they seemed a bit too loop-unrolling specific.
  // However, if they can ever be useful for something else, any of the
  // "MatchXXX" method of this class could be moved to OperationMatcher.
 public:
  explicit StaticCanonicalForLoopMatcher(const OperationMatcher& matcher)
      : matcher_(matcher) {}

  IterationCount GetIterCountIfStaticCanonicalForLoop(
      const Block* header, OpIndex cond_idx, bool loop_if_cond_is) const;

  enum class CmpOp {
    kEqual,
    kSignedLessThan,
    kSignedLessThanOrEqual,
    kUnsignedLessThan,
    kUnsignedLessThanOrEqual,
    kSignedGreaterThan,
    kSignedGreaterThanOrEqual,
    kUnsignedGreaterThan,
    kUnsignedGreaterThanOrEqual,
  };
  static constexpr CmpOp ComparisonKindToCmpOp(ComparisonOp::Kind kind);
  static constexpr CmpOp InvertComparisonOp(CmpOp op);
  enum class BinOp {
    kAdd,
    kMul,
    kSub,
    kBitwiseAnd,
    kBitwiseOr,
    kBitwiseXor,
    kOverflowCheckedAdd,
    kOverflowCheckedMul,
    kOverflowCheckedSub
  };
  static constexpr BinOp BinopFromWordBinopKind(WordBinopOp::Kind kind);
  static constexpr BinOp BinopFromOverflowCheckedBinopKind(
      OverflowCheckedBinopOp::Kind kind);
  static constexpr bool BinopKindIsSupported(WordBinopOp::Kind binop_kind);

 private:
  bool MatchPhiCompareCst(OpIndex cond_idx,
                          StaticCanonicalForLoopMatcher::CmpOp* cmp_op,
                          OpIndex* phi, uint64_t* cst) const;
  bool MatchCheckedOverflowBinop(OpIndex idx, V<Word>* left, V<Word>* right,
                                 BinOp* binop_op,
                                 WordRepresentation* binop_rep) const;
  bool MatchWordBinop(OpIndex idx, V<Word>* left, V<Word>* right,
                      BinOp* binop_op, WordRepresentation* binop_rep) const;
  IterationCount CountIterations(uint64_t equal_cst, CmpOp cmp_op,
                                 uint64_t initial_input, uint64_t binop_cst,
                                 BinOp binop_op, WordRepresentation binop_rep,
                                 bool loop_if_cond_is) const;
  template <class Int>
  IterationCount CountIterationsImpl(
      Int init, Int max, CmpOp cmp_op, Int binop_cst,
      StaticCanonicalForLoopMatcher::BinOp binop_op,
      WordRepresentation binop_rep, bool loop_if_cond_is) const;

  const OperationMatcher& matcher_;

  // When trying to compute the number of iterations of a loop, we simulate the
  // first {kMaxExactIter} iterations of the loop, and check if the loop ends
  // during these first few iterations. This is slightly inneficient, hence the
  // small value for {kMaxExactIter}, but it's simpler than using a formula to
  // compute the number of iterations (in particular because of overflows).
  static constexpr size_t kMaxExactIter = 5;
};
std::ostream& operator<<(std::ostream& os,
                         const StaticCanonicalForLoopMatcher::CmpOp& cmp);
std::ostream& operator<<(std::ostream& os,
                         const StaticCanonicalForLoopMatcher::BinOp& binop);

class V8_EXPORT_PRIVATE LoopUnrollingAnalyzer {
  // LoopUnrollingAnalyzer analyzes the loops of the graph, and in particular
  // tries to figure out if some inner loops have a fixed (and known) number of
  // iterations. In particular, it tries to pattern match loops like
  //
  //    for (let i = 0; i < 4; i++) { ... }
  //
  // where `i++` could alternatively be pretty much any WordBinopOp or
  // OverflowCheckedBinopOp, and `i < 4` could be any ComparisonOp.
  // Such loops, if small enough, could be fully unrolled.
  //
  // Loops that don't have statically-known bounds could still be partially
  // unrolled if they are small enough.
 public:
  LoopUnrollingAnalyzer(Zone* phase_zone, Graph* input_graph, bool is_wasm)
      : input_graph_(input_graph),
        matcher_(*input_graph),
        loop_finder_(phase_zone, input_graph),
        loop_iteration_count_(phase_zone),
        canonical_loop_matcher_(matcher_),
        is_wasm_(is_wasm),
        stack_checks_to_remove_(input_graph->stack_checks_to_remove()) {
    DetectUnrollableLoops();
  }

  bool ShouldFullyUnrollLoop(const Block* loop_header) const {
    DCHECK(loop_header->IsLoop());

    LoopFinder::LoopInfo header_info = loop_finder_.GetLoopInfo(loop_header);
    if (header_info.has_inner_loops) return false;
    if (header_info.op_count > kMaxLoopSizeForFullUnrolling) return false;

    auto iter_count = GetIterationCount(loop_header);
    return iter_count.IsExact() &&
           iter_count.exact_count() < kMaxLoopIterationsForFullUnrolling;
  }

  bool ShouldPartiallyUnrollLoop(const Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto info = loop_finder_.GetLoopInfo(loop_header);
    return !info.has_inner_loops &&
           info.op_count < kMaxLoopSizeForPartialUnrolling;
  }

  bool ShouldRemoveLoop(const Block* loop_header) const {
    auto iter_count = GetIterationCount(loop_header);
    return iter_count.IsExact() && iter_count.exact_count() == 0;
  }

  IterationCount GetIterationCount(const Block* loop_header) const {
    DCHECK(loop_header->IsLoop());
    auto it = loop_iteration_count_.find(loop_header);
    if (it == loop_iteration_count_.end()) return IterationCount::Unknown();
    return it->second;
  }

  ZoneSet<const Block*, LoopFinder::BlockCmp> GetLoopBody(
      const Block* loop_header) {
    return loop_finder_.GetLoopBody(loop_header);
  }

  const Block* GetLoopHeader(const Block* block) {
    return loop_finder_.GetLoopHeader(block);
  }

  bool CanUnrollAtLeastOneLoop() const { return can_unroll_at_least_one_loop_; }

  // TODO(dmercadier): consider tweaking these value for a better size-speed
  // trade-off. In particular, having the number of iterations to unroll be a
  // function of the loop's size and a MaxLoopSize could make sense.
  static constexpr size_t kMaxLoopSizeForFullUnrolling = 150;
  static constexpr size_t kJSMaxLoopSizeForPartialUnrolling = 50;
  static constexpr size_t kWasmMaxLoopSizeForPartialUnrolling = 80;
  static constexpr size_t kMaxLoopIterationsForFullUnrolling = 4;
  static constexpr size_t kPartialUnrollingCount = 4;
  static constexpr size_t kMaxIterForStackCheckRemoval = 5000;

 private:
  void DetectUnrollableLoops();
  IterationCount GetLoopIterationCount(const LoopFinder::LoopInfo& info) const;

  Graph* input_graph_;
  OperationMatcher matcher_;
  LoopFinder loop_finder_;
  // {loop_iteration_count_} maps loop headers to number of iterations. It
  // doesn't contain entries for loops for which we don't know the number of
  // iterations.
  ZoneUnorderedMap<const Block*, IterationCount> loop_iteration_count_;
  const StaticCanonicalForLoopMatcher canonical_loop_matcher_;
  const bool is_wasm_;
  const size_t kMaxLoopSizeForPartialUnrolling =
      is_wasm_ ? kWasmMaxLoopSizeForPartialUnrolling
               : kJSMaxLoopSizeForPartialUnrolling;
  bool can_unroll_at_least_one_loop_ = false;

  ZoneAbslFlatHashSet<uint32_t>& stack_checks_to_remove_;
};

template <class Next>
class LoopPeelingReducer;

template <class Next>
class LoopStackCheckElisionReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LoopStackCheckElision)

  void Bind(Block* new_block) {
    Next::Bind(new_block);
    if (!remove_stack_checks_) return;

    if (new_block->IsLoop()) {
      const Block* origin = new_block->OriginForBlockEnd();
      if (origin) {
        if (stack_checks_to_remove_.contains(origin->index().id())) {
          skip_next_stack_check_ = true;
        }
      }
    }
  }

  V<AnyOrNone> REDUCE_INPUT_GRAPH(Call)(V<AnyOrNone> ig_idx,
                                        const CallOp& call) {
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphCall(ig_idx, call); }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (skip_next_stack_check_ &&
        call.IsStackCheck(__ input_graph(), broker_,
                          StackCheckKind::kJSIterationBody)) {
      skip_next_stack_check_ = false;
      return {};
    }

    goto no_change;
  }

  V<None> REDUCE_INPUT_GRAPH(JSStackCheck)(V<None> ig_idx,
                                           const JSStackCheckOp& stack_check) {
    if (skip_next_stack_check_ &&
        stack_check.kind == JSStackCheckOp::Kind::kLoop) {
      skip_next_stack_check_ = false;
      return {};
    }
    return Next::ReduceInputGraphJSStackCheck(ig_idx, stack_check);
  }

#if V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE_INPUT_GRAPH(WasmStackCheck)(
      V<None> ig_idx, const WasmStackCheckOp& stack_check) {
    if (skip_next_stack_check_ &&
        stack_check.kind == WasmStackCheckOp::Kind::kLoop) {
      skip_next_stack_check_ = false;
      return {};
    }
    return Next::ReduceInputGraphWasmStackCheck(ig_idx, stack_check);
  }
#endif

 private:
  bool skip_next_stack_check_ = false;

  // The analysis should have ran before the CopyingPhase starts, and stored in
  // `PipelineData::Get().stack_checks_to_remove()` the loops whose stack checks
  // should be removed.
  const ZoneAbslFlatHashSet<uint32_t>& stack_checks_to_remove_ =
      __ input_graph().stack_checks_to_remove();
  bool remove_stack_checks_ = !stack_checks_to_remove_.empty();

  JSHeapBroker* broker_ = __ data() -> broker();
};

template <class Next>
class LoopUnrollingReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LoopUnrolling)

#if defined(__clang__)
  // LoopUnrolling and LoopPeeling shouldn't be performed in the same phase, see
  // the comment in pipeline.cc where LoopUnrolling is triggered.
  static_assert(!reducer_list_contains<ReducerList, LoopPeelingReducer>::value);

  // TODO(dmercadier): Add static_assert that this is ran as part of a
  // CopyingPhase.
#endif

  V<None> REDUCE_INPUT_GRAPH(Goto)(V<None> ig_idx, const GotoOp& gto) {
    // Note that the "ShouldSkipOptimizationStep" are placed in the parts of
    // this Reduce method triggering the unrolling rather than at the begining.
    // This is because the backedge skipping is not an optimization but a
    // mandatory lowering when unrolling is being performed.
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphGoto(ig_idx, gto); }

    const Block* dst = gto.destination;
    if (unrolling_ == UnrollingStatus::kNotUnrolling && dst->IsLoop() &&
        !gto.is_backedge) {
      // We trigger unrolling when reaching the GotoOp that jumps to the loop
      // header (note that loop headers only have 2 predecessor, including the
      // backedge), and that isn't the backedge.
      if (ShouldSkipOptimizationStep()) goto no_change;
      if (analyzer_.ShouldRemoveLoop(dst)) {
        RemoveLoop(dst);
        return {};
      } else if (analyzer_.ShouldFullyUnrollLoop(dst)) {
        FullyUnrollLoop(dst);
        return {};
      } else if (analyzer_.ShouldPartiallyUnrollLoop(dst)) {
        PartiallyUnrollLoop(dst);
        return {};
      }
    } else if ((unrolling_ == UnrollingStatus::kUnrolling) &&
               dst == current_loop_header_) {
      // Skipping the backedge of the loop: FullyUnrollLoop and
      // PartiallyUnrollLoop will emit a Goto to the next unrolled iteration.
      return {};
    }
    goto no_change;
  }

  OpIndex REDUCE_INPUT_GRAPH(Branch)(OpIndex ig_idx, const BranchOp& branch) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphBranch(ig_idx, branch);
    }

    if (unrolling_ == UnrollingStatus::kRemoveLoop) {
      // We know that the branch of the final inlined header of a fully unrolled
      // loop never actually goes to the loop, so we can replace it by a Goto
      // (so that the non-unrolled loop doesn't get emitted). We still need to
      // figure out if we should Goto to the true or false side of the BranchOp.
      const Block* header = __ current_block()->OriginForBlockEnd();
      bool is_true_in_loop = analyzer_.GetLoopHeader(branch.if_true) == header;
      bool is_false_in_loop =
          analyzer_.GetLoopHeader(branch.if_false) == header;

      if (is_true_in_loop && !is_false_in_loop) {
        __ Goto(__ MapToNewGraph(branch.if_false));
        return OpIndex::Invalid();
      } else if (is_false_in_loop && !is_true_in_loop) {
        __ Goto(__ MapToNewGraph(branch.if_true));
        return OpIndex::Invalid();
      } else {
        // Both the true and false destinations of this block are in the loop,
        // which means that the exit of the loop is later down the graph. We
        // thus still emit the branch, which will lead to the loop being emitted
        // (unless some other reducers in the stack manage to get rid of the
        // loop).
        DCHECK(is_true_in_loop && is_false_in_loop);
      }
    }
    goto no_change;
  }

  V<AnyOrNone> REDUCE_INPUT_GRAPH(Call)(V<AnyOrNone> ig_idx,
                                        const CallOp& call) {
    LABEL_BLOCK(no_change) { return Next::ReduceInputGraphCall(ig_idx, call); }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (V8_LIKELY(!IsRunningBuiltinPipeline())) {
      if (skip_next_stack_check_ &&
          call.IsStackCheck(__ input_graph(), broker_,
                            StackCheckKind::kJSIterationBody)) {
        // When we unroll a loop, we get rid of its stack checks. (note that
        // we don't do this for the last folded body of partially unrolled
        // loops so that the loop keeps one stack check).
        return {};
      }
    }

    goto no_change;
  }

  V<None> REDUCE_INPUT_GRAPH(JSStackCheck)(V<None> ig_idx,
                                           const JSStackCheckOp& check) {
    if (ShouldSkipOptimizationStep() || !skip_next_stack_check_) {
      return Next::ReduceInputGraphJSStackCheck(ig_idx, check);
    }
    return V<None>::Invalid();
  }

#if V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE_INPUT_GRAPH(WasmStackCheck)(V<None> ig_idx,
                                             const WasmStackCheckOp& check) {
    if (ShouldSkipOptimizationStep() || !skip_next_stack_check_) {
      return Next::ReduceInputGraphWasmStackCheck(ig_idx, check);
    }
    return V<None>::Invalid();
  }
#endif

 private:
  enum class UnrollingStatus {
    // Not currently unrolling a loop.
    kNotUnrolling,
    // Currently unrolling a loop.
    kUnrolling,
    // We use kRemoveLoop in 2 cases:
    //   - When unrolling is finished and we are currently emitting the header
    //     one last time, and should change its final branch into a Goto.
    //   - We decided to remove a loop and will just emit its header.
    // Both cases are fairly similar: we are currently emitting a loop header,
    // and would like to not emit the loop body that follows.
    kRemoveLoop,
  };
  void RemoveLoop(const Block* header);
  void FullyUnrollLoop(const Block* header);
  void PartiallyUnrollLoop(const Block* header);
  void FixLoopPhis(const Block* input_graph_loop, Block* output_graph_loop,
                   const Block* backedge_block);
  bool IsRunningBuiltinPipeline() {
    return __ data() -> pipeline_kind() == TurboshaftPipelineKind::kCSA;
  }
  bool StopUnrollingIfUnreachable(
      std::optional<Block*> output_graph_header = std::nullopt) {
    if (__ generating_unreachable_operations()) {
      // By unrolling the loop, we realized that it was actually exiting early
      // (probably because a Branch inside the loop was using a loop Phi in a
      // condition, and unrolling showed that this loop Phi became true or
      // false), and that lasts iterations were unreachable. We thus don't both
      // unrolling the next iterations of the loop.
      unrolling_ = UnrollingStatus::kNotUnrolling;
      if (output_graph_header.has_value()) {
        // The loop that we're unrolling has a header (which means that we're
        // only partially unrolling), which needs to be turned into a Merge (and
        // its PendingLoopPhis into regular Phis).
        __ FinalizeLoop(*output_graph_header);
      }
      return true;
    }
    return false;
  }

  // The analysis should be ran ahead of time so that the LoopUnrollingPhase
  // doesn't trigger the CopyingPhase if there are no loops to unroll.
  LoopUnrollingAnalyzer& analyzer_ =
      *__ input_graph().loop_unrolling_analyzer();
  // {unrolling_} is true if a loop is currently being unrolled.
  UnrollingStatus unrolling_ = UnrollingStatus::kNotUnrolling;
  bool skip_next_stack_check_ = false;

  const Block* current_loop_header_ = nullptr;
  JSHeapBroker* broker_ = __ data() -> broker();
};

template <class Next>
void LoopUnrollingReducer<Next>::PartiallyUnrollLoop(const Block* header) {
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  DCHECK(!skip_next_stack_check_);
  unrolling_ = UnrollingStatus::kUnrolling;

  auto loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  int unroll_count = LoopUnrollingAnalyzer::kPartialUnrollingCount;

  ScopedModification<bool> set_true(__ turn_loop_without_backedge_into_merge(),
                                    false);

  // We remove the stack check of all iterations but the last one.
  // Emitting the 1st iteration of the loop (with a proper loop header). We
  // remove the stack check of all iterations except the last one.
  ScopedModification<bool> skip_stack_checks(&skip_next_stack_check_, true);
  Block* output_graph_header =
      __ CloneSubGraph(loop_body, /* keep_loop_kinds */ true);
  if (StopUnrollingIfUnreachable(output_graph_header)) return;

  // Emitting the subsequent folded iterations. We set `unrolling_` to
  // kUnrolling so that stack checks are skipped.
  unrolling_ = UnrollingStatus::kUnrolling;
  for (int i = 0; i < unroll_count - 1; i++) {
    // We remove the stack check of all iterations but the last one.
    bool is_last_iteration = i == unroll_count - 2;
    ScopedModification<bool> skip_stack_checks(&skip_next_stack_check_,
                                               !is_last_iteration);

    __ CloneSubGraph(loop_body, /* keep_loop_kinds */ false);
    if (StopUnrollingIfUnreachable(output_graph_header)) return;
  }

  // ReduceInputGraphGoto ignores backedge Gotos while kUnrolling is true, which
  // means that we are still missing the loop's backedge, which we thus emit
  // now.
  DCHECK(output_graph_header->IsLoop());
  Block* backedge_block = __ current_block();
  __ Goto(output_graph_header);
  // We use a custom `FixLoopPhis` because the mapping from old->new is a bit
  // "messed up" by having emitted multiple times the same block. See the
  // comments in `FixLoopPhis` for more details.
  FixLoopPhis(header, output_graph_header, backedge_block);

  unrolling_ = UnrollingStatus::kNotUnrolling;
}

template <class Next>
void LoopUnrollingReducer<Next>::FixLoopPhis(const Block* input_graph_loop,
                                             Block* output_graph_loop,
                                             const Block* backedge_block) {
  // FixLoopPhis for partially unrolled loops is a bit tricky: the mapping from
  // input Loop Phis to output Loop Phis is in the Variable Snapshot of the
  // header (`output_graph_loop`), but the mapping from the 2nd input of the
  // input graph loop phis to the 2nd input of the output graph loop phis is in
  // the snapshot of the backedge (`backedge_block`).
  // VariableReducer::ReduceGotoOp (which was called right before this function
  // because we emitted the backedge Goto) already set the current snapshot to
  // be at the loop header. So, we start by computing the mapping input loop
  // phis -> output loop phis (using the loop header's snapshot). Then, we
  // restore the backedge snapshot to compute the mapping input graph 2nd phi
  // input to output graph 2nd phi input.
  DCHECK(input_graph_loop->IsLoop());
  DCHECK(output_graph_loop->IsLoop());

  // The mapping InputGraphPhi -> OutputGraphPendingPhi should be retrieved from
  // `output_graph_loop`'s snapshot (the current mapping is for the latest
  // folded loop iteration, not for the loop header).
  __ SealAndSaveVariableSnapshot();
  __ RestoreTemporaryVariableSnapshotAfter(output_graph_loop);
  base::SmallVector<std::pair<const PhiOp*, const OpIndex>, 16> phis;
  for (const Operation& op : __ input_graph().operations(
           input_graph_loop->begin(), input_graph_loop->end())) {
    if (auto* input_phi = op.TryCast<PhiOp>()) {
      OpIndex phi_index =
          __ template MapToNewGraph<true>(__ input_graph().Index(*input_phi));
      if (!phi_index.valid() || !output_graph_loop->Contains(phi_index)) {
        // Unused phis are skipped, so they are not be mapped to anything in
        // the new graph. If the phi is reduced to an operation from a
        // different block, then there is no loop phi in the current loop
        // header to take care of.
        continue;
      }
      phis.push_back({input_phi, phi_index});
    }
  }

  // The mapping for the InputGraphPhi 2nd input should however be retrieved
  // from the last block of the loop.
  __ CloseTemporaryVariableSnapshot();
  __ RestoreTemporaryVariableSnapshotAfter(backedge_block);

  for (auto [input_phi, output_phi_index] : phis) {
    __ FixLoopPhi(*input_phi, output_phi_index, output_graph_loop);
  }

  __ CloseTemporaryVariableSnapshot();
}

template <class Next>
void LoopUnrollingReducer<Next>::RemoveLoop(const Block* header) {
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  DCHECK(!skip_next_stack_check_);
  // When removing a loop, we still need to emit the header (since it has to
  // always be executed before the 1st iteration anyways), but by setting
  // {unrolling_} to `kRemoveLoop`, the final Branch of the loop will become a
  // Goto to outside the loop.
  unrolling_ = UnrollingStatus::kRemoveLoop;
  __ CloneAndInlineBlock(header);
  unrolling_ = UnrollingStatus::kNotUnrolling;
}

template <class Next>
void LoopUnrollingReducer<Next>::FullyUnrollLoop(const Block* header) {
  DCHECK_EQ(unrolling_, UnrollingStatus::kNotUnrolling);
  DCHECK(!skip_next_stack_check_);
  ScopedModification<bool> skip_stack_checks(&skip_next_stack_check_, true);

  size_t iter_count = analyzer_.GetIterationCount(header).exact_count();

  auto loop_body = analyzer_.GetLoopBody(header);
  current_loop_header_ = header;

  unrolling_ = UnrollingStatus::kUnrolling;
  for (size_t i = 0; i < iter_count; i++) {
    __ CloneSubGraph(loop_body, /* keep_loop_kinds */ false);
    if (StopUnrollingIfUnreachable()) return;
  }

  // The loop actually finishes on the header rather than its last block. We
  // thus inline the header, and we'll replace its final BranchOp by a GotoOp to
  // outside of the loop.
  unrolling_ = UnrollingStatus::kRemoveLoop;
  __ CloneAndInlineBlock(header);

  unrolling_ = UnrollingStatus::kNotUnrolling;
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_UNROLLING_REDUCER_H_
