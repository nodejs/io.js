// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_BRANCH_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_BRANCH_ELIMINATION_REDUCER_H_

#include <optional>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/layered-hash-map.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/utils/utils.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename>
class VariableReducer;

template <class Next>
class BranchEliminationReducer : public Next {
  // # General overview
  //
  // BranchEliminationAssembler optimizes branches in a few ways:
  //
  //   1- When a branch is nested in another branch and uses the same condition,
  //     then we can get rid of this branch and keep only the correct target.
  //     For instance:
  //
  //         if (cond) {
  //              if (cond) print("B1");
  //              else print("B2");
  //         } else {
  //              if (cond) print("B3");
  //              else print("B4");
  //         }
  //
  //     Will be simplified to:
  //
  //         if (cond) {
  //              print("B1");
  //         } else {
  //              print("B4");
  //         }
  //
  //     Because the 1st nested "if (cond)" is always true, and the 2nd is
  //     always false.
  //
  //     Or, if you prefer a more graph-oriented visual representation:
  //
  //           condition                             condition
  //           |   |   |                                 |
  //       -----   |   ------                            |
  //       |       |        |                            |
  //       |       v        |                            v
  //       |     branch     |                         branch
  //       |     /     \    |                          /   \
  //       |    /       \   |                         /     \
  //       v   /         \  v         becomes        v       v
  //       branch      branch         ======>       B1       B4
  //        /  \        /  \
  //       /    \      /    \
  //      B1     B2   B3     B4
  //
  //
  //   2- When 2 consecutive branches (where the 2nd one is after the merging of
  //     the 1st one) have the same condition, we can pull up the 2nd branch to
  //     get rid of the merge of the 1st branch and the branch of the 2nd
  //     branch. For instance:
  //
  //         if (cond) {
  //             B1;
  //         } else {
  //             B2;
  //         }
  //         B3;
  //         if (cond) {
  //             B4;
  //         } else {
  //             B5;
  //         }
  //
  //     Will be simplified to:
  //
  //         if (cond) {
  //             B1;
  //             B3;
  //             B4;
  //         } else {
  //             B2;
  //             B3;
  //             B5;
  //         }
  //
  //     Or, if you prefer a more graph-oriented visual representation:
  //
  //           condition                           condition
  //           |     |                                 |
  //     -------     |                                 |
  //     |           v                                 v
  //     |        branch                            branch
  //     |         /  \                              /  \
  //     |        /    \                            /    \
  //     |       B1    B2                          B1    B2
  //     |        \    /                           |     |
  //     |         \  /         becomes            |     |
  //     |        merge1        ======>            B3    B3
  //     |          B3                             |     |
  //     -------> branch                           |     |
  //               /  \                            B4    B5
  //              /    \                            \    /
  //             B4    B5                            \  /
  //              \    /                             merge
  //               \  /
  //              merge2
  //
  //   2bis- In the 2nd optimization, if `cond` is a Phi of 2 values that come
  //   from B1 and B2, then the same optimization can be applied for a similar
  //   result. For instance:
  //
  //     if (cond) {                                if (cond) {
  //       x = 1                                        x = 1;
  //     } else {                becomes                B1
  //       x = 0                 ======>            } else {
  //     }                                              x = 0;
  //     if (x) B1 else B2                              B2;
  //                                                }
  //
  //   If `x` is more complex than a simple boolean, then the 2nd branch will
  //   remain, except that it will be on `x`'s value directly rather than on a
  //   Phi (so, it avoids creating a Phi, and it will probably be better for
  //   branch prediction).
  //
  //
  //   3- Optimizing {Return} nodes through merges. It checks that
  //    the return value is actually a {Phi} and the Return is dominated
  //    only by the Phi.
  //
  //    if (c) {                         if (c) {
  //       v = 42;            ====>         v = 42;
  //    } else {                            return v;
  //       v = 5;                        } else {
  //    }                                   v = 5;
  //    return v;                           return v;
  //                                     }
  //
  //    And here's the graph representation:
  //
  //    +----B1----+    <Some other           +----B1'----+     +----B2'----+
  //    | p1 = ... |      block(s):           | p1 = ...  |     | p2 = ...  |
  //    | <...>    |      B2,...>             | <...>     |     | <...>     |
  //    +----------+        /                 | return p1 |     | return p2 |
  //         \             /                  +-----------+     +-----------+
  //          \           /          =====>
  //           \         /
  //            \       |
  //        +--------B3-------+
  //        | p = Phi(p1,...) |
  //        | <...>           |
  //        | return p        |
  //        +-----------------+
  //
  //
  //    4- Eliminating merges: if the 2 merged branches are empty,
  //    and the merge block doesn't have a Phi (which is either the first
  //    operation or is only preceded by FrameState operations),
  //    we can remove the merge and instead Goto the block from the new graph.
  //
  //    5- Eliminating unneeded control flow edges: if a block has only one
  //    successor and the successor only has one predecessor, we can merge these
  //    blocks.
  //
  // # Technical overview of the implementation
  //
  // We iterate the graph in dominator order, and maintain a hash map of
  // conditions with a resolved value along the current path. For instance, if
  // we have:
  //     if (c) { B1 } else { B2 }
  // when iterating B1, we'll know that |c| is true, while when iterating
  // over B2, we'll know that |c| is false.
  // When reaching a Branch, we'll insert the condition in the hash map, while
  // when reaching a Merge, we'll remove it.
  //
  // Then, the 1st optimization (nested branches with the same condition) is
  // trivial: we just look in the hashmap if the condition is known, and only
  // generate the right branch target without generating the branch itself.
  //
  // For the 2nd optimization, when generating a Goto, we check if the
  // destination block ends with a branch whose condition is already known. If
  // that's the case, then we copy the destination block, and the 1st
  // optimization will replace its final Branch by a Goto when reaching it.
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(BranchElimination)
  // TODO(dmercadier): Add static_assert that this is ran as part of a
  // CopyingPhase.

  void Bind(Block* new_block) {
    Next::Bind(new_block);

    if (ShouldSkipOptimizationStep()) {
      // It's important to have a ShouldSkipOptimizationStep here, because
      // {known_conditions_} assumes that we perform all branch elimination
      // possible (which implies that we don't ever insert twice the same thing
      // in {known_conditions_}). If we stop doing ReduceBranch because of
      // ShouldSkipOptimizationStep, then this assumption doesn't hold anymore,
      // and we should thus stop updating {known_conditions_} to not trigger
      // some DCHECKs.
      return;
    }

    // Update {known_conditions_} based on where {new_block} is in the dominator
    // tree.
    ResetToBlock(new_block);
    ReplayMissingPredecessors(new_block);
    StartLayer(new_block);

    if (new_block->IsBranchTarget()) {
      // The current block is a branch target, so we add the branch condition
      // along with its value in {known_conditions_}.
      DCHECK_EQ(new_block->PredecessorCount(), 1);
      const Operation& op =
          new_block->LastPredecessor()->LastOperation(__ output_graph());
      if (const BranchOp* branch = op.TryCast<BranchOp>()) {
        DCHECK_EQ(new_block, any_of(branch->if_true, branch->if_false));
        bool condition_value = branch->if_true == new_block;
        if (!known_conditions_.Contains(branch->condition())) {
          known_conditions_.InsertNewKey(branch->condition(), condition_value);
        }
      }
    }
  }

  V<None> REDUCE(Branch)(V<Word32> cond, Block* if_true, Block* if_false,
                         BranchHint hint) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceBranch(cond, if_true, if_false, hint);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (const Block* if_true_origin = __ OriginForBlockStart(if_true)) {
      if (const Block* if_false_origin = __ OriginForBlockStart(if_false)) {
        const Operation& first_op_true =
            if_true_origin->FirstOperation(__ input_graph());
        const Operation& first_op_false =
            if_false_origin->FirstOperation(__ input_graph());
        const GotoOp* true_goto = first_op_true.template TryCast<GotoOp>();
        const GotoOp* false_goto = first_op_false.template TryCast<GotoOp>();
        // We apply the fourth optimization, replacing empty braches with a
        // Goto to their destination (if it's the same block).
        if (true_goto && false_goto &&
            true_goto->destination == false_goto->destination) {
          Block* merge_block = true_goto->destination;
          if (!merge_block->HasPhis(__ input_graph())) {
            // Using `ReduceInputGraphGoto()` here enables more optimizations.
            __ Goto(__ MapToNewGraph(merge_block));
            return V<None>::Invalid();
          }
        }
      }
    }

    if (auto cond_value = known_conditions_.Get(cond)) {
      // We already know the value of {cond}. We thus remove the branch (this is
      // the "first" optimization in the documentation at the top of this
      // module).
      __ Goto(*cond_value ? if_true : if_false);
      return V<None>::Invalid();
    }
    // We can't optimize this branch.
    goto no_change;
  }

  V<Any> REDUCE(Select)(V<Word32> cond, V<Any> vtrue, V<Any> vfalse,
                        RegisterRepresentation rep, BranchHint hint,
                        SelectOp::Implementation implem) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (auto cond_value = known_conditions_.Get(cond)) {
      if (*cond_value) {
        return vtrue;
      } else {
        return vfalse;
      }
    }
    goto no_change;
  }

  V<None> REDUCE(Goto)(Block* destination, bool is_backedge) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceGoto(destination, is_backedge);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const Block* destination_origin = __ OriginForBlockStart(destination);
    if (!destination_origin || !destination_origin->IsMerge()) {
      goto no_change;
    }

    // Maximum size up to which we allow cloning a block. Cloning too large
    // blocks will lead to increasing the size of the graph too much, which will
    // lead to slower compile time, and larger generated code.
    // TODO(dmercadier): we might want to exclude Phis from this, since they are
    // typically removed when we clone a block. However, computing the number of
    // operations in a block excluding Phis is more costly (because we'd have to
    // iterate all of the operations one by one).
    // TODO(dmercadier): this "13" was selected fairly arbitrarily (= it sounded
    // reasonable). It could be useful to run a few benchmarks to see if we can
    // find a more optimal number.
    static constexpr int kMaxOpCountForCloning = 13;

    const Operation& last_op =
        destination_origin->LastOperation(__ input_graph());

    if (destination_origin->OpCountUpperBound() > kMaxOpCountForCloning) {
      goto no_change;
    }

    if (const BranchOp* branch = last_op.template TryCast<BranchOp>()) {
      V<Word32> condition =
          __ template MapToNewGraph<true>(branch->condition());
      if (condition.valid()) {
        std::optional<bool> condition_value = known_conditions_.Get(condition);
        if (!condition_value.has_value()) {
          // We've already visited the subsequent block's Branch condition, but
          // we don't know its value right now.
          goto no_change;
        }

        // The next block {new_dst} is a Merge, and ends with a Branch whose
        // condition is already known. As per the 2nd optimization, we'll
        // process {new_dst} right away, and we'll end it with a Goto instead of
        // its current Branch.
        __ CloneBlockAndGoto(destination_origin);
        return {};
      } else {
        // Optimization 2bis:
        // {condition} hasn't been visited yet, and thus it doesn't have a
        // mapping to the new graph. However, if it's the result of a Phi whose
        // input is coming from the current block, then it still makes sense to
        // inline {destination_origin}: the condition will then be known.
        if (destination_origin->Contains(branch->condition())) {
          if (__ input_graph().Get(branch->condition()).template Is<PhiOp>()) {
            __ CloneBlockAndGoto(destination_origin);
            return {};
          } else if (CanBeConstantFolded(branch->condition(),
                                         destination_origin)) {
            // If the {cond} only uses constant Phis that come from the current
            // block, it's probably worth it to clone the block in order to
            // constant-fold away the Branch.
            __ CloneBlockAndGoto(destination_origin);
            return {};
          } else {
            goto no_change;
          }
        }
      }
    } else if (last_op.template Is<ReturnOp>()) {
      // In case of the following pattern, the `Goto` is most likely going to be
      // folded into a jump table, so duplicating Block 5 will only increase the
      // amount of different targets within the jump table.
      //
      // Block 1:
      // [...]
      // SwitchOp()[2, 3, 4]
      //
      // Block 2:    Block 3:    Block 4:
      // Goto  5     Goto  5     Goto  6
      //
      // Block 5:                Block 6:
      // [...]                   [...]
      // ReturnOp
      if (Asm().current_block()->PredecessorCount() == 1 &&
          Asm().current_block()->begin() ==
              __ output_graph().next_operation_index()) {
        const Block* prev_block = Asm().current_block()->LastPredecessor();
        if (prev_block->LastOperation(__ output_graph())
                .template Is<SwitchOp>()) {
          goto no_change;
        }
      }
      // The destination block in the old graph ends with a Return
      // and the old destination is a merge block, so we can directly
      // inline the destination block in place of the Goto.
      Asm().CloneAndInlineBlock(destination_origin);
      return {};
    }

    goto no_change;
  }

  V<None> REDUCE(DeoptimizeIf)(V<Word32> condition, V<FrameState> frame_state,
                               bool negated,
                               const DeoptimizeParameters* parameters) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    std::optional<bool> condition_value = known_conditions_.Get(condition);
    if (!condition_value.has_value()) {
      known_conditions_.InsertNewKey(condition, negated);
      goto no_change;
    }

    if ((*condition_value && !negated) || (!*condition_value && negated)) {
      // The condition is true, so we always deoptimize.
      return Next::ReduceDeoptimize(frame_state, parameters);
    } else {
      // The condition is false, so we never deoptimize.
      return V<None>::Invalid();
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE(TrapIf)(V<Word32> condition, OptionalV<FrameState> frame_state,
                         bool negated, const TrapId trap_id) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceTrapIf(condition, frame_state, negated, trap_id);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    std::optional<bool> condition_value = known_conditions_.Get(condition);
    if (!condition_value.has_value()) {
      known_conditions_.InsertNewKey(condition, negated);
      goto no_change;
    }

    if (__ matcher().template Is<ConstantOp>(condition)) {
      goto no_change;
    }

    V<Word32> static_condition = __ Word32Constant(*condition_value);
    if (negated) {
      __ TrapIfNot(static_condition, frame_state, trap_id);
    } else {
      __ TrapIf(static_condition, frame_state, trap_id);
    }
    return V<None>::Invalid();
  }
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  // Resets {known_conditions_} and {dominator_path_} up to the 1st dominator of
  // {block} that they contain.
  void ResetToBlock(Block* block) {
    Block* target = block->GetDominator();
    while (!dominator_path_.empty() && target != nullptr &&
           dominator_path_.back() != target) {
      if (dominator_path_.back()->Depth() > target->Depth()) {
        ClearCurrentEntries();
      } else if (dominator_path_.back()->Depth() < target->Depth()) {
        target = target->GetDominator();
      } else {
        // {target} and {dominator_path.back} have the same depth but are not
        // equal, so we go one level up for both.
        ClearCurrentEntries();
        target = target->GetDominator();
      }
    }
  }

  // Removes the latest entry in {known_conditions_} and {dominator_path_}.
  void ClearCurrentEntries() {
    known_conditions_.DropLastLayer();
    dominator_path_.pop_back();
  }

  void StartLayer(Block* block) {
    known_conditions_.StartLayer();
    dominator_path_.push_back(block);
  }

  // ReplayMissingPredecessors adds to {known_conditions_} and {dominator_path_}
  // the conditions/blocks that related to the dominators of {block} that are
  // not already present. This can happen when control-flow changes during the
  // CopyingPhase, which results in a block being visited not right after
  // its dominator. For instance, when optimizing a double-diamond like:
  //
  //                  B0
  //                 /  \
  //                /    \
  //               B1    B2
  //                \    /
  //                 \  /
  //                  B3
  //                 /  \
  //                /    \
  //               B4    B5
  //                \    /
  //                 \  /
  //                  B6
  //                 /  \
  //                /    \
  //               B7    B8
  //                \    /
  //                 \  /
  //                  B9
  //
  // In this example, where B0, B3 and B6 branch on the same condition, the
  // blocks are actually visited in the following order: B0 - B1 - B3/1 - B2 -
  // B3/2 - B4 - B5 - ... (note how B3 is duplicated and visited twice because
  // from B1/B2 its branch condition is already known; I've noted the duplicated
  // blocks as B3/1 and B3/2). In the new graph, the dominator of B4 is B3/1 and
  // the dominator of B5 is B3/2. Except that upon visiting B4, the last visited
  // block is not B3/1 but rather B3/2, so, we have to reset {known_conditions_}
  // to B0, and thus miss that we actually know branch condition of B0/B3/B6 and
  // we thus won't optimize the 3rd diamond.
  //
  // To overcome this issue, ReplayMissingPredecessors will add the information
  // of the missing predecessors of the current block to {known_conditions_}. In
  // the example above, this means that when visiting B4,
  // ReplayMissingPredecessors will add the information of B3/1 to
  // {known_conditions_}.
  void ReplayMissingPredecessors(Block* new_block) {
    // Collect blocks that need to be replayed.
    base::SmallVector<Block*, 32> missing_blocks;
    for (Block* dom = new_block->GetDominator();
         dom != nullptr && dom != dominator_path_.back();
         dom = dom->GetDominator()) {
      missing_blocks.push_back(dom);
    }
    // Actually does the replaying, starting from the oldest block and finishing
    // with the newest one (so that they will later be removed in the correct
    // order).
    for (auto it = missing_blocks.rbegin(); it != missing_blocks.rend(); ++it) {
      Block* block = *it;
      StartLayer(block);

      if (block->IsBranchTarget()) {
        const Operation& op =
            block->LastPredecessor()->LastOperation(__ output_graph());
        if (const BranchOp* branch = op.TryCast<BranchOp>()) {
          DCHECK(branch->if_true->index() == block->index() ||
                 branch->if_false->index() == block->index());
          bool condition_value =
              branch->if_true->index().valid()
                  ? branch->if_true->index() == block->index()
                  : branch->if_false->index() != block->index();
          known_conditions_.InsertNewKey(branch->condition(), condition_value);
        }
      }
    }
  }

  // Checks that {idx} only depends on only on Constants or on Phi whose input
  // from the current block is a Constant, and on a least one Phi (whose input
  // from the current block is a Constant). If it is the case and {idx} is used
  // in a Branch, then the Branch's block could be cloned in the current block,
  // and {idx} could then be constant-folded away such that the Branch becomes a
  // Goto.
  bool CanBeConstantFolded(OpIndex idx, const Block* cond_input_block,
                           bool has_phi = false, int depth = 0) {
    // We limit the depth of the search to {kMaxDepth} in order to avoid
    // potentially visiting a lot of nodes.
    static constexpr int kMaxDepth = 4;
    if (depth > kMaxDepth) return false;
    const Operation& op = __ input_graph().Get(idx);
    if (!cond_input_block->Contains(idx)) {
      // If we reach a ConstantOp without having gone through a Phi, then the
      // condition can be constant-folded without performing block cloning.
      return has_phi && op.Is<ConstantOp>();
    }
    if (op.Is<PhiOp>()) {
      int curr_block_pred_idx = cond_input_block->GetPredecessorIndex(
          __ current_block()->OriginForBlockEnd());
      // There is no need to increment {depth} on this recursive call, because
      // it will anyways exit early because {idx} won't be in
      // {cond_input_block}.
      return CanBeConstantFolded(op.input(curr_block_pred_idx),
                                 cond_input_block, /*has_phi*/ true, depth);
    } else if (op.Is<ConstantOp>()) {
      return true;
    } else if (op.input_count == 0) {
      // Any operation that has no input but is not a ConstantOp probably won't
      // be able to be constant-folded away (eg, LoadRootRegister).
      return false;
    } else if (!op.Effects().can_be_constant_folded()) {
      // Operations with side-effects won't be able to be constant-folded.
      return false;
    }

    for (int i = 0; i < op.input_count; i++) {
      if (!CanBeConstantFolded(op.input(i), cond_input_block, has_phi,
                               depth + 1)) {
        return false;
      }
    }

    return has_phi;
  }

  // TODO(dmercadier): use the SnapshotTable to replace {dominator_path_} and
  // {known_conditions_}, and to reuse the existing merging/replay logic of the
  // SnapshotTable.
  ZoneVector<Block*> dominator_path_{__ phase_zone()};
  LayeredHashMap<V<Word32>, bool> known_conditions_{
      __ phase_zone(), __ input_graph().DominatorTreeDepth() * 2};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_BRANCH_ELIMINATION_REDUCER_H_
