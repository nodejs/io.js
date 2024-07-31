// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_ANALYSIS_H_
#define V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_ANALYSIS_H_

#include <limits>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/typer.h"
#include "src/compiler/turboshaft/types.h"

namespace v8::internal::compiler::turboshaft {

// This analysis infers types for all operations. It does so by running a
// fixpoint analysis on the input graph in order to properly type PhiOps. The
// analysis visits blocks in order and computes operation types using
// Turboshaft's Typer. For Goto operations, the analysis checks if this is a
// back edge (the Goto's target is a loop block with an index less than the
// index of the current block). If this is the case, the analysis revisits the
// loop block (this is when ProcessBlock<true> is called). During this revisit,
// two things are different to the normal processing of a block:
//
// 1.) PhiOps are handled specially, which means applying proper
// widening/narrowing mechanics to accelerate termination while still computing
// somewhat precise types for Phis. 2.) If the type of any of the loop's Phis
// grows, we reset the index of unprocessed blocks to the block after the loop
// header, such that the entire loop body is revisited with the new type
// information.
class TypeInferenceAnalysis {
 public:
  explicit TypeInferenceAnalysis(const Graph& graph, Zone* phase_zone)
      : graph_(graph),
        // TODO(nicohartmann@): Might put types back into phase_zone once we
        // don't store them in the graph anymore.
        types_(graph.op_id_count(), Type{}, graph.graph_zone(), &graph),
        table_(phase_zone),
        op_to_key_mapping_(phase_zone, &graph),
        block_to_snapshot_mapping_(graph.block_count(), base::nullopt,
                                   phase_zone),
        predecessors_(phase_zone),
        graph_zone_(graph.graph_zone()) {}

  GrowingOpIndexSidetable<Type> Run(
      GrowingBlockSidetable<std::vector<std::pair<OpIndex, Type>>>*
          block_refinements = nullptr) {
#ifdef DEBUG
    block_refinements_ = block_refinements;
#endif  // DEBUG
    TURBOSHAFT_TRACE_TYPING("=== Running Type Inference Analysis ===\n");
    for (uint32_t unprocessed_index = 0;
         unprocessed_index < graph_.block_count();) {
      BlockIndex block_index = static_cast<BlockIndex>(unprocessed_index);
      ++unprocessed_index;
      const Block& block = graph_.Get(block_index);

#ifdef DEBUG
      if (V8_UNLIKELY(v8_flags.turboshaft_trace_typing)) {
        std::stringstream os;
        os << block.kind() << " " << block.index().id();
        TURBOSHAFT_TRACE_TYPING("=== %s ===\n", os.str().c_str());
      }
#endif  // DEBUG

      ProcessBlock<false>(block, &unprocessed_index);
    }
    TURBOSHAFT_TRACE_TYPING("=== Completed Type Inference Analysis ===\n");

    return std::move(types_);
  }

  template <bool revisit_loop_header>
  void ProcessBlock(const Block& block, uint32_t* unprocessed_index) {
    DCHECK_IMPLIES(revisit_loop_header, block.IsLoop());

    // Seal the current block first.
    if (table_.IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      // If we process a new block while the previous one is still unsealed, we
      // finalize it.
      DCHECK_NOT_NULL(current_block_);
      DCHECK(current_block_->index().valid());
      block_to_snapshot_mapping_[current_block_->index()] = table_.Seal();
      current_block_ = nullptr;
    }

    // Collect the snapshots of all predecessors.
    {
      predecessors_.clear();
      for (const Block* pred : block.PredecessorsIterable()) {
        base::Optional<table_t::Snapshot> pred_snapshot =
            block_to_snapshot_mapping_[pred->index()];
        if (pred_snapshot.has_value()) {
          predecessors_.push_back(pred_snapshot.value());
        } else {
          // The only case where we might not have a snapshot for the
          // predecessor is when we visit a loop header for the first time.
          DCHECK(block.IsLoop() && pred == block.LastPredecessor() &&
                 !revisit_loop_header);
        }
      }
      std::reverse(predecessors_.begin(), predecessors_.end());
    }

    // Start a new snapshot for this block by merging information from
    // predecessors.
    {
      auto MergeTypes = [&](table_t::Key,
                            base::Vector<const Type> predecessors) -> Type {
        DCHECK_GT(predecessors.size(), 0);
        Type result_type = predecessors[0];
        for (size_t i = 1; i < predecessors.size(); ++i) {
          result_type =
              Type::LeastUpperBound(result_type, predecessors[i], graph_zone_);
        }
        return result_type;
      };

      table_.StartNewSnapshot(base::VectorOf(predecessors_), MergeTypes);
    }

    // Check if the predecessor is a branch that allows us to refine a few
    // types.
    DCHECK_IMPLIES(revisit_loop_header, block.PredecessorCount() == 2);
    if (block.PredecessorCount() == 1) {
      Block* predecessor = block.LastPredecessor();
      const Operation& terminator = predecessor->LastOperation(graph_);
      if (const BranchOp* branch = terminator.TryCast<BranchOp>()) {
        DCHECK(branch->if_true == &block || branch->if_false == &block);
        RefineTypesAfterBranch(branch, &block, branch->if_true == &block);
      }
    }
    current_block_ = &block;

    bool loop_needs_revisit = false;
    auto op_range = graph_.OperationIndices(block);
    for (auto it = op_range.begin(); it != op_range.end(); ++it) {
      OpIndex index = *it;
      const Operation& op = graph_.Get(index);

      switch (op.opcode) {
        case Opcode::kBranch:
        case Opcode::kDeoptimize:
        case Opcode::kDeoptimizeIf:
        case Opcode::kFrameState:
        case Opcode::kReturn:
        case Opcode::kStore:
        case Opcode::kRetain:
        case Opcode::kUnreachable:
        case Opcode::kSwitch:
        case Opcode::kTuple:
        case Opcode::kStaticAssert:
        case Opcode::kDebugBreak:
        case Opcode::kDebugPrint:
#if V8_ENABLE_WEBASSEMBLY
        case Opcode::kGlobalSet:
        case Opcode::kTrapIf:
#endif
        case Opcode::kCheckException:
          // These operations do not produce any output that needs to be typed.
          DCHECK_EQ(0, op.outputs_rep().size());
          break;
        case Opcode::kCheckTurboshaftTypeOf:
          ProcessCheckTurboshaftTypeOf(index,
                                       op.Cast<CheckTurboshaftTypeOfOp>());
          break;
        case Opcode::kComparison:
          ProcessComparison(index, op.Cast<ComparisonOp>());
          break;
        case Opcode::kConstant:
          ProcessConstant(index, op.Cast<ConstantOp>());
          break;
        case Opcode::kFloatBinop:
          ProcessFloatBinop(index, op.Cast<FloatBinopOp>());
          break;
        case Opcode::kOverflowCheckedBinop:
          ProcessOverflowCheckedBinop(index, op.Cast<OverflowCheckedBinopOp>());
          break;
        case Opcode::kProjection:
          ProcessProjection(index, op.Cast<ProjectionOp>());
          break;
        case Opcode::kWordBinop:
          ProcessWordBinop(index, op.Cast<WordBinopOp>());
          break;
        case Opcode::kWord32PairBinop:
        case Opcode::kAtomicWord32Pair:
        case Opcode::kPendingLoopPhi:
          // Input graph must not contain these op codes.
          UNREACHABLE();
        case Opcode::kPhi:
          if constexpr (revisit_loop_header) {
            loop_needs_revisit =
                ProcessLoopPhi(index, op.Cast<PhiOp>()) || loop_needs_revisit;
          } else {
            ProcessPhi(index, op.Cast<PhiOp>());
          }
          break;
        case Opcode::kGoto: {
          const GotoOp& gto = op.Cast<GotoOp>();
          // Check if this is a backedge.
          if (gto.destination->IsLoop()) {
            if (gto.destination->index() < current_block_->index()) {
              ProcessBlock<true>(*gto.destination, unprocessed_index);
            } else if (gto.destination->index() == current_block_->index()) {
              // This is a single block loop. We must only revisit the current
              // header block if we actually need to, in order to prevent
              // infinite recursion.
              if (!revisit_loop_header || loop_needs_revisit) {
                ProcessBlock<true>(*gto.destination, unprocessed_index);
              }
            }
          }
          break;
        }

        default:
          // TODO(nicohartmann@): Support remaining operations. For now we
          // compute fallback types.
          if (op.outputs_rep().size() > 0) {
            constexpr bool allow_narrowing = false;
            constexpr bool is_fallback_for_unsupported_operation = true;
            SetType(index,
                    Typer::TypeForRepresentation(op.outputs_rep(), graph_zone_),
                    allow_narrowing, is_fallback_for_unsupported_operation);
          }
          break;
        case Opcode::kLoadRootRegister:
          SetType(index,
                  Typer::TypeForRepresentation(op.outputs_rep(), graph_zone_));
          break;
      }
    }

    if constexpr (revisit_loop_header) {
      if (loop_needs_revisit) {
        // This is a loop header and the loop body needs to be revisited. Reset
        // {unprocessed_index} to the loop header's successor.
        *unprocessed_index =
            std::min(*unprocessed_index, block.index().id() + 1);
      }
    }
  }

  void ProcessCheckTurboshaftTypeOf(OpIndex index,
                                    const CheckTurboshaftTypeOfOp& check) {
    Type input_type = GetType(check.input());

    if (input_type.IsSubtypeOf(check.type)) {
      TURBOSHAFT_TRACE_TYPING_OK(
          "CTOF %3d:%-40s\n  P: %3d:%-40s ~~> %s\n", index.id(),
          graph_.Get(index).ToString().substr(0, 40).c_str(),
          check.input().id(),
          graph_.Get(check.input()).ToString().substr(0, 40).c_str(),
          input_type.ToString().c_str());
    } else if (check.successful) {
      FATAL(
          "Checking type %s of operation %d:%s failed after it passed in a "
          "previous phase",
          check.type.ToString().c_str(), check.input().id(),
          graph_.Get(check.input()).ToString().c_str());
    } else {
      TURBOSHAFT_TRACE_TYPING_FAIL(
          "CTOF %3d:%-40s\n  F: %3d:%-40s ~~> %s\n", index.id(),
          graph_.Get(index).ToString().substr(0, 40).c_str(),
          check.input().id(),
          graph_.Get(check.input()).ToString().substr(0, 40).c_str(),
          input_type.ToString().c_str());
    }
  }

  void ProcessComparison(OpIndex index, const ComparisonOp& comparison) {
    Type left_type = GetType(comparison.left());
    Type right_type = GetType(comparison.right());

    Type result_type = Typer::TypeComparison(
        left_type, right_type, comparison.rep, comparison.kind, graph_zone_);
    SetType(index, result_type);
  }

  void ProcessConstant(OpIndex index, const ConstantOp& constant) {
    Type type = Typer::TypeConstant(constant.kind, constant.storage);
    SetType(index, type);
  }

  void ProcessFloatBinop(OpIndex index, const FloatBinopOp& binop) {
    Type left_type = GetType(binop.left());
    Type right_type = GetType(binop.right());

    Type result_type = Typer::TypeFloatBinop(left_type, right_type, binop.kind,
                                             binop.rep, graph_zone_);
    SetType(index, result_type);
  }

  bool ProcessLoopPhi(OpIndex index, const PhiOp& phi) {
    Type old_type = GetTypeAtDefinition(index);
    Type new_type = ComputeTypeForPhi(phi);

    if (old_type.IsInvalid()) {
      SetType(index, new_type);
      return true;
    }

    // If the new type is smaller, we narrow it without revisiting the loop.
    if (new_type.IsSubtypeOf(old_type)) {
      TURBOSHAFT_TRACE_TYPING_OK(
          "LOOP %3d:%-40s (FIXPOINT)\n  N:     %-40s ~~> %-40s\n", index.id(),
          graph_.Get(index).ToString().substr(0, 40).c_str(),
          old_type.ToString().c_str(), new_type.ToString().c_str());

      constexpr bool allow_narrowing = true;
      SetType(index, new_type, allow_narrowing);
      return false;
    }

    // Otherwise, the new type is larger and we widen and revisit the loop.
    TURBOSHAFT_TRACE_TYPING_OK(
        "LOOP %3d:%-40s (REVISIT)\n  W:     %-40s ~~> %-40s\n", index.id(),
        graph_.Get(index).ToString().substr(0, 40).c_str(),
        old_type.ToString().c_str(), new_type.ToString().c_str());

    if (!old_type.IsNone()) {
      new_type = Widen(old_type, new_type);
    }
    SetType(index, new_type);
    return true;
  }

  void ProcessOverflowCheckedBinop(OpIndex index,
                                   const OverflowCheckedBinopOp& binop) {
    Type left_type = GetType(binop.left());
    Type right_type = GetType(binop.right());

    Type result_type = Typer::TypeOverflowCheckedBinop(
        left_type, right_type, binop.kind, binop.rep, graph_zone_);
    SetType(index, result_type);
  }

  void ProcessPhi(OpIndex index, const PhiOp& phi) {
    Type result_type = ComputeTypeForPhi(phi);
    SetType(index, result_type);
  }

  void ProcessProjection(OpIndex index, const ProjectionOp& projection) {
    Type input_type = GetType(projection.input());

    Type result_type;
    if (input_type.IsNone()) {
      result_type = Type::None();
    } else if (input_type.IsTuple()) {
      const TupleType& tuple = input_type.AsTuple();
      DCHECK_LT(projection.index, tuple.size());
      result_type = tuple.element(projection.index);
      DCHECK(result_type.IsSubtypeOf(
          Typer::TypeForRepresentation(projection.rep)));
    } else {
      result_type = Typer::TypeForRepresentation(projection.rep);
    }

    SetType(index, result_type);
  }

  void ProcessWordBinop(OpIndex index, const WordBinopOp& binop) {
    Type left_type = GetType(binop.left());
    Type right_type = GetType(binop.right());

    Type result_type = Typer::TypeWordBinop(left_type, right_type, binop.kind,
                                            binop.rep, graph_zone_);
    SetType(index, result_type);
  }

  Type ComputeTypeForPhi(const PhiOp& phi) {
    // Word64 values are truncated to word32 implicitly, we need to handle this
    // here.
    auto MaybeTruncate = [&](Type t) -> Type {
      if (t.IsNone()) return t;
      if (phi.rep == RegisterRepresentation::Word32()) {
        return Typer::TruncateWord32Input(t, true, graph_zone_);
      }
      return t;
    };

    Type result_type =
        MaybeTruncate(GetTypeOrDefault(phi.inputs()[0], Type::None()));
    for (size_t i = 1; i < phi.inputs().size(); ++i) {
      Type input_type =
          MaybeTruncate(GetTypeOrDefault(phi.inputs()[i], Type::None()));
      result_type = Type::LeastUpperBound(result_type, input_type, graph_zone_);
    }
    return result_type;
  }

  void RefineTypesAfterBranch(const BranchOp* branch, const Block* new_block,
                              bool then_branch) {
    TURBOSHAFT_TRACE_TYPING_OK("Br   %3d:%-40s\n", graph_.Index(*branch).id(),
                               branch->ToString().substr(0, 40).c_str());

    Typer::BranchRefinements refinements(
        [this](OpIndex index) { return GetType(index); },
        [&](OpIndex index, const Type& refined_type) {
          RefineOperationType(new_block, index, refined_type,
                              then_branch ? 'T' : 'F');
        });

    // Inspect branch condition.
    const Operation& condition = graph_.Get(branch->condition());
    refinements.RefineTypes(condition, then_branch, graph_zone_);
  }

  void RefineOperationType(const Block* new_block, OpIndex op, const Type& type,
                           char case_for_tracing) {
    DCHECK(op.valid());
    DCHECK(!type.IsInvalid());

    TURBOSHAFT_TRACE_TYPING_OK("  %c: %3d:%-40s ~~> %s\n", case_for_tracing,
                               op.id(),
                               graph_.Get(op).ToString().substr(0, 40).c_str(),
                               type.ToString().c_str());

    auto key_opt = op_to_key_mapping_[op];
    DCHECK(key_opt.has_value());
    table_.Set(*key_opt, type);

#ifdef DEBUG
    if (block_refinements_) {
      (*block_refinements_)[new_block->index()].emplace_back(op, type);
    }
#endif

    // TODO(nicohartmann@): One could push the refined type deeper into the
    // operations.
  }

  void SetType(OpIndex index, Type result_type, bool allow_narrowing = false,
               bool is_fallback_for_unsupported_operation = false) {
    DCHECK(!result_type.IsInvalid());

    if (auto key_opt = op_to_key_mapping_[index]) {
      table_.Set(*key_opt, result_type);
      types_[index] = result_type;
    } else {
      auto key = table_.NewKey(Type::None());
      op_to_key_mapping_[index] = key;
      table_.Set(key, result_type);
      types_[index] = result_type;
    }

    if (!is_fallback_for_unsupported_operation) {
      TURBOSHAFT_TRACE_TYPING_OK(
          "Type %3d:%-40s ==> %s\n", index.id(),
          graph_.Get(index).ToString().substr(0, 40).c_str(),
          result_type.ToString().c_str());
    } else {
      // TODO(nicohartmann@): Remove the fallback case once all operations are
      // supported.
      TURBOSHAFT_TRACE_TYPING_FAIL(
          "TODO %3d:%-40s ==> %s\n", index.id(),
          graph_.Get(index).ToString().substr(0, 40).c_str(),
          result_type.ToString().c_str());
    }
  }

  Type GetTypeOrInvalid(const OpIndex index) {
    if (auto key = op_to_key_mapping_[index]) return table_.Get(*key);
    return Type::Invalid();
  }

  Type GetTypeOrDefault(OpIndex index, const Type& default_type) {
    Type t = GetTypeOrInvalid(index);
    if (t.IsInvalid()) return default_type;
    return t;
  }

  Type GetType(OpIndex index) {
    Type t = GetTypeOrInvalid(index);
    if (t.IsInvalid()) {
      // TODO(nicohartmann@): This is a fallback mechanism as long as not all
      // operations are properly typed. Remove this once typing is complete.
      const Operation& op = graph_.Get(index);
      return Typer::TypeForRepresentation(op.outputs_rep(), graph_zone_);
    }
    return t;
  }

  Type GetTypeAtDefinition(OpIndex index) const { return types_[index]; }

  Type Widen(const Type& old_type, const Type& new_type) {
    if (new_type.IsAny()) return new_type;
    // We might have to relax this eventually and widen different types.
    DCHECK_EQ(old_type.kind(), new_type.kind());

    switch (old_type.kind()) {
      case Type::Kind::kWord32:
        // TODO(nicohartmann@): Reevaluate whether exponential widening is
        // better here.
        //
        // return WordOperationTyper<32>::WidenExponential(old_type.AsWord32(),
        // new_type.AsWord32(), graph_zone_);
        return WordOperationTyper<32>::WidenMaximal(
            old_type.AsWord32(), new_type.AsWord32(), graph_zone_);
      case Type::Kind::kWord64:
        // TODO(nicohartmann@): Reevaluate whether exponential widening is
        // better here.
        //
        // return WordOperationTyper<64>::WidenExponential(old_type.AsWord64(),
        // new_type.AsWord64(), graph_zone_);
        return WordOperationTyper<64>::WidenMaximal(
            old_type.AsWord64(), new_type.AsWord64(), graph_zone_);
      case Type::Kind::kFloat32:
        // TODO(nicohartmann@): Implement proper widening.
        return Float32Type::Any();
      case Type::Kind::kFloat64:
        // TODO(nicohartmann@): Implement proper widening.
        return Float64Type::Any();
      default:
        // TODO(nicohartmann@): Handle remaining cases.
        UNREACHABLE();
    }
  }

 private:
  const Graph& graph_;
  GrowingOpIndexSidetable<Type> types_;
  using table_t = SnapshotTable<Type>;
  table_t table_;
  const Block* current_block_ = nullptr;
  GrowingOpIndexSidetable<base::Optional<table_t::Key>> op_to_key_mapping_;
  GrowingBlockSidetable<base::Optional<table_t::Snapshot>>
      block_to_snapshot_mapping_;
  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<table_t::Snapshot> predecessors_;
  Zone* graph_zone_;

#ifdef DEBUG
  // {block_refinements_} are only stored for tracing in Debug builds.
  GrowingBlockSidetable<std::vector<std::pair<OpIndex, Type>>>*
      block_refinements_ = nullptr;
#endif
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_ANALYSIS_H_
