// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_

#include <iomanip>
#include <optional>

#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// General overview
//
// DeadCodeAnalysis iterates the graph backwards to propagate liveness
// information. This information consists of the ControlState and the
// OperationState.
//
// OperationState reflects the liveness of operations. An operation is live if
//
//   1) The operation has the `IsRequiredWhenUnused()` property.
//   2) Any of its outputs is live (is used in a live operation).
//
// If the operation is not live, it is dead and can be eliminated.
//
// ControlState describes to which block we could jump immediately without
// changing the program semantics. That is missing any side effects, required
// control flow or any live operations. This information is then used
// at BranchOps to rewrite them to a GotoOp towards the corresponding block.
// From the output control state(s) c after an operation, the control state c'
// before the operation is computed as follows:
//
//                           | Bi               if ct, cf are Bi or Unreachable
//   c' = [Branch](ct, cf) = {
//                           | NotEliminatable  otherwise
//
// And if c' = Bi, then the BranchOp can be rewritten into GotoOp(Bi).
//
//                           | NotEliminatable  if Op is live
//            c' = [Op](c) = {
//                           | c                otherwise
//
//                           | Bk               if c = Bk
//       c' = [Merge i](c) = { Bi               if Merge i has no live phis
//                           | NotEliminatable  otherwise
//
// Where Merge is an imaginary operation at the start of every merge block. This
// is the important part for the analysis. If block `Merge i` does not have any
// live phi operations, then we don't necessarily need to distinguish the
// control flow paths going into that block and if we further don't encounter
// any live operations along any of the paths leading to `Merge i`
// starting at some BranchOp, we can skip both branches and eliminate the
// control flow entirely by rewriting the BranchOp into a GotoOp(Bi). Notice
// that if the control state already describes a potential Goto-target Bk, then
// we do not replace that in order to track the farthest block we can jump to.

struct ControlState {
  // Lattice:
  //
  //  NotEliminatable
  //     /  |  \
  //    B1 ... Bn
  //     \  |  /
  //    Unreachable
  //
  // We use ControlState to propagate information during the analysis about how
  // branches can be rewritten. Read the values like this:
  // - NotEliminatable: We cannot rewrite a branch, because we need the control
  // flow (e.g. because we have seen live operations on either branch or need
  // the phi at the merge).
  // - Bj: Control can be rewritten to go directly to Block Bj, because all
  // paths to that block are free of live operations.
  // - Unreachable: This is the bottom element and it represents that we haven't
  // seen anything live yet and are free to rewrite branches to any block
  // reachable from the current block.
  enum Kind {
    kUnreachable,
    kBlock,
    kNotEliminatable,
  };

  static ControlState NotEliminatable() {
    return ControlState{kNotEliminatable};
  }
  static ControlState Block(BlockIndex block) {
    return ControlState{kBlock, block};
  }
  static ControlState Unreachable() { return ControlState{kUnreachable}; }

  explicit ControlState(Kind kind, BlockIndex block = BlockIndex::Invalid())
      : kind(kind), block(block) {}

  static ControlState LeastUpperBound(const ControlState& lhs,
                                      const ControlState& rhs) {
    switch (lhs.kind) {
      case Kind::kUnreachable:
        return rhs;
      case Kind::kBlock: {
        if (rhs.kind == Kind::kUnreachable) return lhs;
        if (rhs.kind == Kind::kNotEliminatable) return rhs;
        if (lhs.block == rhs.block) return lhs;
        return NotEliminatable();
      }
      case Kind::kNotEliminatable:
        return lhs;
    }
  }

  Kind kind;
  BlockIndex block;
};

inline std::ostream& operator<<(std::ostream& stream,
                                const ControlState& state) {
  switch (state.kind) {
    case ControlState::kNotEliminatable:
      return stream << "NotEliminatable";
    case ControlState::kBlock:
      return stream << "Block(" << state.block << ")";
    case ControlState::kUnreachable:
      return stream << "Unreachable";
  }
}

inline bool operator==(const ControlState& lhs, const ControlState& rhs) {
  if (lhs.kind != rhs.kind) return false;
  if (lhs.kind == ControlState::kBlock) {
    DCHECK_EQ(rhs.kind, ControlState::kBlock);
    return lhs.block == rhs.block;
  }
  return true;
}

inline bool operator!=(const ControlState& lhs, const ControlState& rhs) {
  return !(lhs == rhs);
}

struct OperationState {
  // Lattice:
  //
  //   Live
  //    |
  //   Dead
  //
  // Describes the liveness state of an operation.
  enum Liveness : uint8_t {
    kDead,
    kLive,
  };

  static Liveness LeastUpperBound(Liveness lhs, Liveness rhs) {
    static_assert(kDead == 0 && kLive == 1);
    return static_cast<Liveness>(lhs | rhs);
  }
};

inline std::ostream& operator<<(std::ostream& stream,
                                OperationState::Liveness liveness) {
  switch (liveness) {
    case OperationState::kDead:
      return stream << "Dead";
    case OperationState::kLive:
      return stream << "Live";
  }
  UNREACHABLE();
}

class DeadCodeAnalysis {
 public:
  explicit DeadCodeAnalysis(Graph& graph, Zone* phase_zone)
      : graph_(graph),
        liveness_(graph.op_id_count(), OperationState::kDead, phase_zone,
                  &graph),
        entry_control_state_(graph.block_count(), ControlState::Unreachable(),
                             phase_zone),
        rewritable_branch_targets_(phase_zone, &graph) {}

  template <bool trace_analysis>
  std::pair<FixedOpIndexSidetable<OperationState::Liveness>,
            SparseOpIndexSideTable<BlockIndex>>
  Run() {
    if constexpr (trace_analysis) {
      std::cout << "===== Running Dead Code Analysis =====\n";
    }
    for (uint32_t unprocessed_count = graph_.block_count();
         unprocessed_count > 0;) {
      BlockIndex block_index = static_cast<BlockIndex>(unprocessed_count - 1);
      --unprocessed_count;

      const Block& block = graph_.Get(block_index);
      ProcessBlock<trace_analysis>(block, &unprocessed_count);
    }

    if constexpr (trace_analysis) {
      std::cout << "===== Results =====\n== Operation State ==\n";
      for (Block b : graph_.blocks()) {
        std::cout << PrintAsBlockHeader{b} << ":\n";
        for (OpIndex index : graph_.OperationIndices(b)) {
          std::cout << " " << std::setw(8) << liveness_[index] << " "
                    << std::setw(3) << index.id() << ": " << graph_.Get(index)
                    << "\n";
        }
      }

      std::cout << "== Rewritable Branches ==\n";
      for (auto [branch_id, target] : rewritable_branch_targets_) {
        DCHECK(target.valid());
        std::cout << " " << std::setw(3) << branch_id << ": Branch ==> Goto "
                  << target.id() << "\n";
      }
      std::cout << "==========\n";
    }

    return {std::move(liveness_), std::move(rewritable_branch_targets_)};
  }

  template <bool trace_analysis>
  void ProcessBlock(const Block& block, uint32_t* unprocessed_count) {
    if constexpr (trace_analysis) {
      std::cout << "\n==========\n=== Processing " << PrintAsBlockHeader{block}
                << ":\n==========\nEXIT CONTROL STATE\n";
    }
    auto successors = SuccessorBlocks(block.LastOperation(graph_));
    ControlState control_state = ControlState::Unreachable();
    for (size_t i = 0; i < successors.size(); ++i) {
      const auto& r = entry_control_state_[successors[i]->index()];
      if constexpr (trace_analysis) {
        std::cout << " Successor " << successors[i]->index() << ": " << r
                  << "\n";
      }
      control_state = ControlState::LeastUpperBound(control_state, r);
    }
    if constexpr (trace_analysis)
      std::cout << "Combined: " << control_state << "\n";

    // If control_state == ControlState::Block(b), then the merge block b is
    // reachable through every path starting at the current block without any
    // live operations.

    if constexpr (trace_analysis) std::cout << "OPERATION STATE\n";
    auto op_range = graph_.OperationIndices(block);
    bool has_live_phis = false;
    for (auto it = op_range.end(); it != op_range.begin();) {
      --it;
      OpIndex index = *it;
      const Operation& op = graph_.Get(index);
      if constexpr (trace_analysis) std::cout << index << ":" << op << "\n";
      OperationState::Liveness op_state = liveness_[index];

      if (op.Is<DeadOp>()) {
        // Operation is already recognized as dead by a previous analysis.
        DCHECK_EQ(op_state, OperationState::kDead);
      } else if (op.Is<CallOp>()) {
        // The function contains a call, so it's not a leaf function.
        is_leaf_function_ = false;
      } else if (op.Is<BranchOp>() || op.Is<GotoOp>()) {
        if (control_state != ControlState::NotEliminatable()) {
          // Branch is still dead.
          DCHECK_EQ(op_state, OperationState::kDead);
          // If we know a target block we can rewrite into a goto.
          if (control_state.kind == ControlState::kBlock) {
            BlockIndex target = control_state.block;
            DCHECK(target.valid());
            rewritable_branch_targets_[index] = target;
          }
        } else {
          // Branch is live. We cannot rewrite it.
          op_state = OperationState::kLive;
          rewritable_branch_targets_.remove(index);
        }
      } else if (op.IsRequiredWhenUnused()) {
        op_state = OperationState::kLive;
      } else if (op.Is<PhiOp>()) {
        has_live_phis = has_live_phis || (op_state == OperationState::kLive);

        if (block.IsLoop()) {
          const PhiOp& phi = op.Cast<PhiOp>();
          // Check if the operation state of the input coming from the backedge
          // changes the liveness of the phi. In that case, trigger a revisit of
          // the loop.
          if (liveness_[phi.inputs()[PhiOp::kLoopPhiBackEdgeIndex]] <
              op_state) {
            if constexpr (trace_analysis) {
              std::cout
                  << "Operation state has changed. Need to revisit loop.\n";
            }
            Block* backedge = block.LastPredecessor();
            // Revisit the loop by increasing the {unprocessed_count} to include
            // all blocks of the loop.
            *unprocessed_count =
                std::max(*unprocessed_count, backedge->index().id() + 1);
          }
        }
      }

      // TODO(nicohartmann@): Handle Stack Guards to allow elimination of
      // otherwise empty loops.
      //
      // if(const CallOp* call = op.TryCast<CallOp>()) {
      //   if(std::string(call->descriptor->descriptor->debug_name())
      //     == "StackGuard") {
      //       DCHECK_EQ(op_state, OperationState::kLive);
      //       op_state = OperationState::kWeakLive;
      //     }
      // }

      DCHECK_LE(liveness_[index], op_state);
      // If everything is still dead. We don't need to update anything.
      if (op_state == OperationState::kDead) continue;

      // We have a live operation.
      if constexpr (trace_analysis) {
        std::cout << " " << op_state << " <== " << liveness_[index] << "\n";
      }
      liveness_[index] = op_state;

      if constexpr (trace_analysis) {
        if (op.input_count > 0) std::cout << " Updating inputs:\n";
      }
      for (OpIndex input : op.inputs()) {
        auto old_input_state = liveness_[input];
        auto new_input_state =
            OperationState::LeastUpperBound(old_input_state, op_state);
        if constexpr (trace_analysis) {
          std::cout << "  " << input << ": " << new_input_state
                    << " <== " << old_input_state << " || " << op_state << "\n";
        }
        liveness_[input] = new_input_state;
      }

      if (op_state == OperationState::kLive &&
          control_state != ControlState::NotEliminatable()) {
        // This block has live operations, which means that we can't skip it.
        // Reset the ControlState to NotEliminatable.
        if constexpr (trace_analysis) {
          std::cout << "Block has live operations. New control state: "
                    << ControlState::NotEliminatable() << "\n";
        }
        control_state = ControlState::NotEliminatable();
      }
    }

    if constexpr (trace_analysis) {
      std::cout << "ENTRY CONTROL STATE\nAfter operations: " << control_state
                << "\n";
    }

    // If this block is a merge and we don't have any live phis, it is a
    // potential target for branch redirection.
    if (block.IsMerge()) {
      if (!has_live_phis) {
        if (control_state.kind != ControlState::kBlock) {
          control_state = ControlState::Block(block.index());
          if constexpr (trace_analysis) {
            std::cout
                << "Block is loop or merge and has no live phi operations.\n";
          }
        } else if constexpr (trace_analysis) {
          std::cout << "Block is loop or merge and has no live phi "
                       "operations.\nControl state already has a goto block: "
                    << control_state << "\n";
        }
      }
    } else if (block.IsLoop()) {
      // If this is a loop, we reset the control state to avoid jumps into the
      // middle of the loop. In particular, this is required to prevent
      // introducing new backedges when blocks towards the end of the loop body
      // want to jump to a block at the beginning (past the header).
      control_state = ControlState::NotEliminatable();
      if constexpr (trace_analysis) {
        std::cout << "Block is loop header. Resetting control state: "
                  << control_state << "\n";
      }

      if (entry_control_state_[block.index()] != control_state) {
        if constexpr (trace_analysis) {
          std::cout << "Control state has changed. Need to revisit loop.\n";
        }
        Block* backedge = block.LastPredecessor();
        DCHECK_NOT_NULL(backedge);
        // Revisit the loop by increasing the {unprocessed_count} to include
        // all blocks of the loop.
        *unprocessed_count =
            std::max(*unprocessed_count, backedge->index().id() + 1);
      }
    }

    if constexpr (trace_analysis) {
      std::cout << "Final: " << control_state << "\n";
    }
    entry_control_state_[block.index()] = control_state;
  }

  bool is_leaf_function() const { return is_leaf_function_; }

 private:
  Graph& graph_;
  FixedOpIndexSidetable<OperationState::Liveness> liveness_;
  FixedBlockSidetable<ControlState> entry_control_state_;
  SparseOpIndexSideTable<BlockIndex> rewritable_branch_targets_;
  // The stack check at function entry of leaf functions can be eliminated, as
  // it is guaranteed that another stack check will be hit eventually. This flag
  // records if the current function is a leaf function.
  bool is_leaf_function_ = true;
};

template <class Next>
class DeadCodeEliminationReducer
    : public UniformReducerAdapter<DeadCodeEliminationReducer, Next> {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(DeadCodeElimination)

  using Adapter = UniformReducerAdapter<DeadCodeEliminationReducer, Next>;

  // DeadCodeElimination can change the control flow in somewhat unexpected ways
  // (ie, a block with a single predecessor in the input graph can end up with
  // multiple predecessors in the output graph), so we prevent the CopyingPhase
  // from automatically inlining blocks with a single predecessor when we run
  // the DeadCodeEliminationReducer.
  bool CanAutoInlineBlocksWithSinglePredecessor() const { return false; }

  void Analyze() {
    // TODO(nicohartmann@): We might want to make this a flag.
    constexpr bool trace_analysis = false;
    std::tie(liveness_, branch_rewrite_targets_) =
        analyzer_.Run<trace_analysis>();
    Next::Analyze();
  }

  V<None> REDUCE_INPUT_GRAPH(Branch)(V<None> ig_index, const BranchOp& branch) {
    if (TryRewriteBranch(ig_index)) return V<None>::Invalid();
    return Next::ReduceInputGraphBranch(ig_index, branch);
  }

  V<None> REDUCE_INPUT_GRAPH(Goto)(V<None> ig_index, const GotoOp& gto) {
    if (TryRewriteBranch(ig_index)) return {};
    return Next::ReduceInputGraphGoto(ig_index, gto);
  }

  template <typename Op, typename Continuation>
  OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& op) {
    if ((*liveness_)[ig_index] == OperationState::kDead) {
      return OpIndex::Invalid();
    }
    return Continuation{this}.ReduceInputGraph(ig_index, op);
  }

  bool IsLeafFunction() const { return analyzer_.is_leaf_function(); }

 private:
  bool TryRewriteBranch(OpIndex index) {
    const BlockIndex* goto_target;
    if (branch_rewrite_targets_.contains(index, &goto_target)) {
      Asm().Goto(Asm().MapToNewGraph(&Asm().input_graph().Get(*goto_target)));
      return true;
    }
    return false;
  }
  std::optional<FixedOpIndexSidetable<OperationState::Liveness>> liveness_;
  SparseOpIndexSideTable<BlockIndex> branch_rewrite_targets_{
      Asm().phase_zone(), &Asm().input_graph()};
  DeadCodeAnalysis analyzer_{Asm().modifiable_input_graph(),
                             Asm().phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_DEAD_CODE_ELIMINATION_REDUCER_H_
