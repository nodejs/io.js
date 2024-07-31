// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_STRUCTURAL_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_STRUCTURAL_OPTIMIZATION_REDUCER_H_

#include <cstdio>

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/zone/zone.h"

// The StructuralOptimizationReducer reducer is suitable for changing the
// graph in a way that doesn't reduce individual operations, rather changes
// the structure of the graph.
//
// We currently support a reduction which transforms if-else cascades
// that check if a given value is equal to a 32-bit constant from a given set
// into a switch with cases corresponding to the constants in the set.
//
// So for example code like:
//    [only pure ops 1]
//    if (x == 3) {
//      B1;
//    } else {
//      [only pure ops 2]
//      if (x == 5) {
//        B2;
//      } else {
//        B3;
//      }
//    }
//
// will be transformed to:
//    [only pure ops 1]
//    [only pure ops 2]
//    switch (x) {
//      case 3:
//        B1;
//      case 5:
//        B2;
//      default:
//        B3;
//    }
//
// Or represented graphically:
//                                                 [only pure ops 1]
//       [only pure ops 1]                         [only pure ops 2]
//           x == 3                                    Switch(x)
//           Branch                                    |    |   |
//           |    |                                -----    |   ------
//       -----    ------                    case 3 |        |        | default
//       |             |                           |        |        |
//     T |             | F                         v        |        |
//       v             v                           B1       |        v
//       B1      [only pure ops 2]    becomes               |        B3
//                   x == 5           ======>        case 5 |
//                   Branch                                 v
//                   |    |                                 B2
//               -----    ------
//               |             |
//             T |             | F
//               v             v
//              B2            B3
//

// TODO(mslekova): Introduce a flag and move to a common graph place.
// #define TRACE_REDUCTIONS
#ifdef TRACE_REDUCTIONS
#define TRACE(str, ...) \
  { PrintF(str, ##__VA_ARGS__); }
#else  // TRACE_REDUCTIONS
#define TRACE(str, ...)

#endif  // TRACE_REDUCTIONS

namespace v8::internal::compiler::turboshaft {

template <class Next>
class StructuralOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(StructuralOptimization)

  OpIndex ReduceInputGraphBranch(OpIndex input_index, const BranchOp& branch) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphBranch(input_index, branch);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    TRACE("[structural] Calling ReduceInputGraphBranch for index: %u\n",
          static_cast<unsigned int>(input_index.id()));

    base::SmallVector<SwitchOp::Case, 16> cases;
    base::SmallVector<const Block*, 16> false_blocks;

    Block* current_if_true;
    Block* current_if_false;
    const BranchOp* current_branch = &branch;
    BranchHint current_branch_hint;
    BranchHint next_hint = BranchHint::kNone;

    OpIndex switch_var = OpIndex::Invalid();
    uint32_t value;
    while (true) {
      // If we encounter a condition that is not equality, we can't turn it
      // into a switch case.
      const Operation& cond =
          Asm().input_graph().Get(current_branch->condition());

      if (!cond.template Is<ComparisonOp>()) {
        // 'if(x==0)' may be optimized to 'if(x)', we should take this into
        // consideration.

        // The "false" destination will be inlined before the switch is emitted,
        // so it should only contain pure operations.
        if (!ContainsOnlyPureOps(current_branch->if_true,
                                 Asm().input_graph())) {
          TRACE("\t [break] End of only-pure-ops cascade reached.\n");
          break;
        }

        OpIndex current_var = current_branch->condition();
        if (!switch_var.valid()) {
          switch_var = current_var;
        } else if (switch_var != current_var) {
          TRACE("\t [bailout] Not all branches compare the same variable.\n");
          break;
        }
        value = 0;
        // The true/false of 'if(x)' is reversed from 'if(x==0)'
        current_if_true = current_branch->if_false;
        current_if_false = current_branch->if_true;
        const BranchHint hint = current_branch->hint;
        current_branch_hint = hint == BranchHint::kNone   ? BranchHint::kNone
                              : hint == BranchHint::kTrue ? BranchHint::kFalse
                                                          : BranchHint::kTrue;
      } else {
        const ComparisonOp* equal =
            cond.template TryCast<Opmask::kWord32Equal>();
        if (!equal) {
          TRACE(
              "\t [bailout] Branch with different condition than Word32 "
              "Equal.\n");
          break;
        }
        // MachineOptimizationReducer should normalize equality to put constants
        // right.
        const Operation& right_op = Asm().input_graph().Get(equal->right());
        if (!right_op.Is<Opmask::kWord32Constant>()) {
          TRACE(
              "\t [bailout] No Word32 constant on the right side of Equal.\n");
          break;
        }

        // The "false" destination will be inlined before the switch is emitted,
        // so it should only contain pure operations.
        if (!ContainsOnlyPureOps(current_branch->if_false,
                                 Asm().input_graph())) {
          TRACE("\t [break] End of only-pure-ops cascade reached.\n");
          break;
        }
        const ConstantOp& const_op = right_op.Cast<ConstantOp>();
        value = const_op.word32();

        // If we encounter equal to a different value, we can't introduce
        // a switch.
        OpIndex current_var = equal->left();
        if (!switch_var.valid()) {
          switch_var = current_var;
        } else if (switch_var != current_var) {
          TRACE("\t [bailout] Not all branches compare the same variable.\n");
          break;
        }

        current_if_true = current_branch->if_true;
        current_if_false = current_branch->if_false;
        current_branch_hint = current_branch->hint;
      }

      DCHECK(current_if_true && current_if_false);

      // We can't just use `current_branch->hint` for every case. Consider:
      //
      //     if (a) { }
      //     else if (b) { }
      //     else if (likely(c)) { }
      //     else if (d) { }
      //     else { }
      //
      // The fact that `c` is Likely doesn't tell anything about the likelyness
      // of `a` and `b` compared to `c`, which means that `c` shouldn't have the
      // Likely hint in the switch. However, since `c` is likely here, it means
      // that `d` and "default" are both unlikely, even in the switch.
      //
      // So, for the 1st case, we use `current_branch->hint`.
      // Then, when we encounter a Likely hint, we mark all of the subsequent
      // cases are Unlikely, but don't mark the current one as Likely. This is
      // done with the `next_hint` variable, which is initially kNone, but
      // because kFalse when we encounter a Likely branch.
      // We never set `next_hint` as kTrue as it would only apply to subsequent
      // cases and not to already-emitted cases. The only case that could thus
      // have a kTrue annotation is the 1st one.
      DCHECK_NE(next_hint, BranchHint::kTrue);
      BranchHint hint = next_hint;
      if (cases.size() == 0) {
        // The 1st case gets its original hint.
        hint = current_branch_hint;
      } else if (current_branch_hint == BranchHint::kFalse) {
        // For other cases, if the branch has a kFalse hint, we do use it,
        // regardless of `next_hint`.
        hint = BranchHint::kNone;
      }
      if (current_branch_hint == BranchHint::kTrue) {
        // This branch is likely true, which means that all subsequent cases are
        // unlikely.
        next_hint = BranchHint::kFalse;
      }

      // The current_if_true block becomes the corresponding switch case block.
      cases.emplace_back(value, Asm().MapToNewGraph(current_if_true), hint);

      // All pure ops from the if_false block should be executed before
      // the switch, except the last Branch operation (which we drop).
      false_blocks.push_back(current_if_false);

      // If we encounter a if_false block that doesn't end with a Branch,
      // this means we've reached the end of the cascade.
      const Operation& maybe_branch =
          current_if_false->LastOperation(Asm().input_graph());
      if (!maybe_branch.Is<BranchOp>()) {
        TRACE("\t [break] Reached end of the if-else cascade.\n");
        break;
      }

      // Iterate to the next if_false block in the cascade.
      current_branch = &maybe_branch.template Cast<BranchOp>();
    }

    // Probably better to keep short if-else cascades as they are.
    if (cases.size() <= 2) {
      TRACE("\t [bailout] Cascade with less than 2 levels of nesting.\n");
      goto no_change;
    }
    CHECK_EQ(cases.size(), false_blocks.size());

    // We're skipping the last false block, as it becomes the default block.
    for (size_t i = 0; i < false_blocks.size() - 1; ++i) {
      const Block* block = false_blocks[i];
      InlineAllOperationsWithoutLast(block);
    }

    TRACE("[reduce] Successfully emit a Switch with %zu cases.", cases.size());

    // The last current_if_true block that ends the cascade becomes the default
    // case.
    Block* default_block = current_if_false;
    Asm().Switch(
        Asm().MapToNewGraph(switch_var),
        Asm().output_graph().graph_zone()->CloneVector(base::VectorOf(cases)),
        Asm().MapToNewGraph(default_block), next_hint);
    return OpIndex::Invalid();
  }

 private:
  static bool ContainsOnlyPureOps(const Block* block, const Graph& graph) {
    for (const auto& op : base::IterateWithoutLast(graph.operations(*block))) {
      // We are moving the block content to before the switch, effectively
      // moving it before the previously existing branches.
      if (!op.Effects().hoistable_before_a_branch()) {
        return false;
      }
    }
    return true;
  }

  // Visits and emits {input_block} right now (ie, in the current block)
  // until the one before the last operation is reached.
  void InlineAllOperationsWithoutLast(const Block* input_block) {
    base::iterator_range<Graph::OpIndexIterator> all_ops =
        Asm().input_graph().OperationIndices(*input_block);

    for (OpIndex op : base::IterateWithoutLast(all_ops)) {
      Asm().InlineOp(op, input_block);
    }
  }
};

}  // namespace v8::internal::compiler::turboshaft

#undef TRACE

#endif  // V8_COMPILER_TURBOSHAFT_STRUCTURAL_OPTIMIZATION_REDUCER_H_
