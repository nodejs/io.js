// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_

#include <limits>
#include <optional>

#include "src/base/logging.h"
#include "src/base/vector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/compiler/turboshaft/tracing.h"
#include "src/compiler/turboshaft/type-inference-analysis.h"
#include "src/compiler/turboshaft/typer.h"
#include "src/compiler/turboshaft/types.h"
#include "src/compiler/turboshaft/uniform-reducer-adapter.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename Op>
V8_INLINE bool CanBeTyped(const Op& operation) {
  return operation.outputs_rep().size() > 0;
}

struct TypeInferenceReducerArgs
    : base::ContextualClass<TypeInferenceReducerArgs> {
  enum class InputGraphTyping {
    kNone,     // Do not compute types for the input graph.
    kPrecise,  // Run a complete fixpoint analysis on the input graph.
  };
  enum class OutputGraphTyping {
    kNone,                    // Do not compute types for the output graph.
    kPreserveFromInputGraph,  // Reuse types of the input graph where
                              // possible.
    kRefineFromInputGraph,  // Reuse types of the input graph and compute types
                            // for new nodes and more precise types where
                            // possible.
  };
  InputGraphTyping input_graph_typing;
  OutputGraphTyping output_graph_typing;

  TypeInferenceReducerArgs(InputGraphTyping input_graph_typing,
                           OutputGraphTyping output_graph_typing)
      : input_graph_typing(input_graph_typing),
        output_graph_typing(output_graph_typing) {}
};

// TypeInferenceReducer is the central component to infer types for Turboshaft
// graphs. It comes with different options for how the input and output graph
// should be typed:
//
// - InputGraphTyping::kNone: No types are computed for the input graph.
// - InputGraphTyping::kPrecise: We run a full fixpoint analysis on the input
// graph to infer the most precise types possible (see TypeInferenceAnalysis).
//
// - OutputGraphTyping::kNone: No types will be set for the output graph.
// - OutputGraphTyping::kPreserveFromInputGraph: Types from the input graph will
// be preserved for the output graph. Where this is not possible (e.g. new
// operations introduced during lowering), the output operation will be untyped.
// - OutputGraphTyping::kRefineFromInputGraph: Types from the input graph will
// be used where they provide additional precision (e.g loop phis). For new
// operations, the reducer reruns the typer to make sure that the output graph
// is fully typed.
//
// NOTE: The TypeInferenceReducer has to be the last reducer in the stack!
template <class Next>
class TypeInferenceReducer
    : public UniformReducerAdapter<TypeInferenceReducer, Next> {
  static_assert(next_is_bottom_of_assembler_stack<Next>::value);
  using table_t = SnapshotTable<Type>;

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(TypeInference)

  using Adapter = UniformReducerAdapter<TypeInferenceReducer, Next>;
  using Args = TypeInferenceReducerArgs;

  TypeInferenceReducer() {
    // It is not reasonable to try to reuse input graph types if there are none.
    DCHECK_IMPLIES(args_.output_graph_typing ==
                       Args::OutputGraphTyping::kPreserveFromInputGraph,
                   args_.input_graph_typing != Args::InputGraphTyping::kNone);
  }

  void Analyze() {
    if (args_.input_graph_typing == Args::InputGraphTyping::kPrecise) {
#ifdef DEBUG
      GrowingBlockSidetable<std::vector<std::pair<OpIndex, Type>>>
          block_refinements(Asm().input_graph().block_count(), {},
                            Asm().phase_zone());
      input_graph_types_ = analyzer_.Run(&block_refinements);
      Tracing::Get().PrintPerBlockData(
          "Type Refinements", Asm().input_graph(),
          [&](std::ostream& stream, const turboshaft::Graph& graph,
              turboshaft::BlockIndex index) -> bool {
            const std::vector<std::pair<turboshaft::OpIndex, turboshaft::Type>>&
                refinements = block_refinements[index];
            if (refinements.empty()) return false;
            stream << "\\n";
            for (const auto& [op, type] : refinements) {
              stream << op << " : " << type << "\\n";
            }
            return true;
          });
#else
      input_graph_types_ = analyzer_.Run(nullptr);
#endif  // DEBUG
      Tracing::Get().PrintPerOperationData(
          "Types", Asm().input_graph(),
          [&](std::ostream& stream, const turboshaft::Graph& graph,
              turboshaft::OpIndex index) -> bool {
            turboshaft::Type type = input_graph_types_[index];
            if (!type.IsInvalid() && !type.IsNone()) {
              type.PrintTo(stream);
              return true;
            }
            return false;
          });
    }
    Next::Analyze();
  }

  Type GetInputGraphType(OpIndex ig_index) {
    return input_graph_types_[ig_index];
  }

  Type GetOutputGraphType(OpIndex og_index) { return GetType(og_index); }

  template <Opcode opcode, typename Continuation, typename... Ts>
  OpIndex ReduceOperation(Ts... args) {
    OpIndex index = Continuation{this}.Reduce(args...);
    if (!NeedsTyping(index)) return index;

    const Operation& op = Asm().output_graph().Get(index);
    if (CanBeTyped(op)) {
      Type type = Typer::TypeForRepresentation(
          Asm().output_graph().Get(index).outputs_rep(), Asm().graph_zone());
      SetType(index, type, true);
    }
    return index;
  }

  template <typename Op, typename Continuation>
  OpIndex ReduceInputGraphOperation(OpIndex ig_index, const Op& operation) {
    OpIndex og_index = Continuation{this}.ReduceInputGraph(ig_index, operation);
    if (!og_index.valid()) return og_index;
    if (args_.output_graph_typing == Args::OutputGraphTyping::kNone) {
      return og_index;
    }
    if (!CanBeTyped(operation)) return og_index;

    Type ig_type = GetInputGraphType(ig_index);
    DCHECK_IMPLIES(args_.input_graph_typing != Args::InputGraphTyping::kNone,
                   !ig_type.IsInvalid());
    if (!ig_type.IsInvalid()) {
      Type og_type = GetType(og_index);
      // If the type we have from the input graph is more precise, we keep it.
      if (og_type.IsInvalid() ||
          (ig_type.IsSubtypeOf(og_type) && !og_type.IsSubtypeOf(ig_type))) {
        RefineTypeFromInputGraph(og_index, og_type, ig_type);
      }
    }
    return og_index;
  }

  void Bind(Block* new_block) {
    Next::Bind(new_block);

    // Seal the current block first.
    if (table_.IsSealed()) {
      DCHECK_NULL(current_block_);
    } else {
      // If we bind a new block while the previous one is still unsealed, we
      // finalize it.
      DCHECK_NOT_NULL(current_block_);
      DCHECK(current_block_->index().valid());
      block_to_snapshot_mapping_[current_block_->index()] = table_.Seal();
      current_block_ = nullptr;
    }

    // Collect the snapshots of all predecessors.
    {
      predecessors_.clear();
      for (const Block* pred : new_block->PredecessorsIterable()) {
        std::optional<table_t::Snapshot> pred_snapshot =
            block_to_snapshot_mapping_[pred->index()];
        DCHECK(pred_snapshot.has_value());
        predecessors_.push_back(pred_snapshot.value());
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
          result_type = Type::LeastUpperBound(result_type, predecessors[i],
                                              Asm().graph_zone());
        }
        return result_type;
      };

      table_.StartNewSnapshot(base::VectorOf(predecessors_), MergeTypes);
    }

    // Check if the predecessor is a branch that allows us to refine a few
    // types.
    if (args_.output_graph_typing ==
        Args::OutputGraphTyping::kRefineFromInputGraph) {
      if (new_block->PredecessorCount() == 1) {
        Block* predecessor = new_block->LastPredecessor();
        const Operation& terminator =
            predecessor->LastOperation(Asm().output_graph());
        if (const BranchOp* branch = terminator.TryCast<BranchOp>()) {
          DCHECK(branch->if_true == new_block || branch->if_false == new_block);
          RefineTypesAfterBranch(branch, new_block,
                                 branch->if_true == new_block);
        }
      }
    }
    current_block_ = new_block;
  }

  void RefineTypesAfterBranch(const BranchOp* branch, Block* new_block,
                              bool then_branch) {
    const std::string branch_str = branch->ToString().substr(0, 40);
    USE(branch_str);
    TURBOSHAFT_TRACE_TYPING_OK("Br   %3d:%-40s\n",
                               Asm().output_graph().Index(*branch).id(),
                               branch_str.c_str());

    Typer::BranchRefinements refinements(
        [this](OpIndex index) { return GetType(index); },
        [&](OpIndex index, const Type& refined_type) {
          RefineOperationType(new_block, index, refined_type,
                              then_branch ? 'T' : 'F');
        });

    // Inspect branch condition.
    const Operation& condition = Asm().output_graph().Get(branch->condition());
    refinements.RefineTypes(condition, then_branch, Asm().graph_zone());
  }

  void RefineOperationType(Block* new_block, OpIndex op, const Type& type,
                           char case_for_tracing) {
    DCHECK(op.valid());
    DCHECK(!type.IsInvalid());

    TURBOSHAFT_TRACE_TYPING_OK(
        "  %c: %3d:%-40s ~~> %s\n", case_for_tracing, op.id(),
        Asm().output_graph().Get(op).ToString().substr(0, 40).c_str(),
        type.ToString().c_str());

    auto key_opt = op_to_key_mapping_[op];
    // We might not have a key for this value, because we are running in a mode
    // where we don't type all operations.
    if (key_opt.has_value()) {
      table_.Set(*key_opt, type);

#ifdef DEBUG
      std::vector<std::pair<OpIndex, Type>>& refinement =
          Asm().output_graph().block_type_refinement()[new_block->index()];
      refinement.push_back(std::make_pair(op, type));
#endif

      // TODO(nicohartmann@): One could push the refined type deeper into the
      // operations.
    }
  }

  OpIndex REDUCE(PendingLoopPhi)(OpIndex first, RegisterRepresentation rep) {
    OpIndex index = Next::ReducePendingLoopPhi(first, rep);
    if (!NeedsTyping(index)) return index;

    // There is not much we can do for pending loop phis, because we don't know
    // the type of the backedge yet, so we have to assume maximal type. If we
    // run with a typed input graph, we can refine this type using the input
    // graph's type (see ReduceInputGraphOperation).
    SetType(index, Typer::TypeForRepresentation(rep));
    return index;
  }

  OpIndex REDUCE(Phi)(base::Vector<const OpIndex> inputs,
                      RegisterRepresentation rep) {
    OpIndex index = Next::ReducePhi(inputs, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Type::None();
    for (const OpIndex input : inputs) {
      type = Type::LeastUpperBound(type, GetType(input), Asm().graph_zone());
    }
    SetType(index, type);
    return index;
  }

  OpIndex REDUCE(Constant)(ConstantOp::Kind kind, ConstantOp::Storage value) {
    OpIndex index = Next::ReduceConstant(kind, value);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeConstant(kind, value);
    SetType(index, type);
    return index;
  }

  V<Word32> REDUCE(Comparison)(V<Any> left, V<Any> right,
                               ComparisonOp::Kind kind,
                               RegisterRepresentation rep) {
    OpIndex index = Next::ReduceComparison(left, right, kind, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeComparison(GetType(left), GetType(right), rep, kind,
                                      Asm().graph_zone());
    SetType(index, type);
    return index;
  }

  V<Any> REDUCE(Projection)(V<Any> input, uint16_t idx,
                            RegisterRepresentation rep) {
    V<Any> index = Next::ReduceProjection(input, idx, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeProjection(GetType(input), idx);
    SetType(index, type);
    return index;
  }

  V<Word> REDUCE(WordBinop)(V<Word> left, V<Word> right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    V<Word> index = Next::ReduceWordBinop(left, right, kind, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeWordBinop(GetType(left), GetType(right), kind, rep,
                                     Asm().graph_zone());
    SetType(index, type);
    return index;
  }

  OpIndex REDUCE(OverflowCheckedBinop)(V<Word> left, V<Word> right,
                                       OverflowCheckedBinopOp::Kind kind,
                                       WordRepresentation rep) {
    OpIndex index = Next::ReduceOverflowCheckedBinop(left, right, kind, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeOverflowCheckedBinop(GetType(left), GetType(right),
                                                kind, rep, Asm().graph_zone());
    SetType(index, type);
    return index;
  }

  V<Float> REDUCE(FloatBinop)(V<Float> left, V<Float> right,
                              FloatBinopOp::Kind kind,
                              FloatRepresentation rep) {
    V<Float> index = Next::ReduceFloatBinop(left, right, kind, rep);
    if (!NeedsTyping(index)) return index;

    Type type = Typer::TypeFloatBinop(GetType(left), GetType(right), kind, rep,
                                      Asm().graph_zone());
    SetType(index, type);
    return index;
  }

  OpIndex REDUCE(CheckTurboshaftTypeOf)(OpIndex input,
                                        RegisterRepresentation rep, Type type,
                                        bool successful) {
    Type input_type = GetType(input);
    if (input_type.IsSubtypeOf(type)) {
      OpIndex index = Next::ReduceCheckTurboshaftTypeOf(input, rep, type, true);
      TURBOSHAFT_TRACE_TYPING_OK(
          "CTOF %3d:%-40s\n  P: %3d:%-40s ~~> %s\n", index.id(),
          Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
          input.id(),
          Asm().output_graph().Get(input).ToString().substr(0, 40).c_str(),
          input_type.ToString().c_str());
      return index;
    }
    if (successful) {
      FATAL(
          "Checking type %s of operation %d:%s failed after it passed in a "
          "previous phase",
          type.ToString().c_str(), input.id(),
          Asm().output_graph().Get(input).ToString().c_str());
    }
    OpIndex index =
        Next::ReduceCheckTurboshaftTypeOf(input, rep, type, successful);
    TURBOSHAFT_TRACE_TYPING_FAIL(
        "CTOF %3d:%-40s\n  F: %3d:%-40s ~~> %s\n", index.id(),
        Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
        input.id(),
        Asm().output_graph().Get(input).ToString().substr(0, 40).c_str(),
        input_type.ToString().c_str());
    return index;
  }

  void RemoveLast(OpIndex index_of_last_operation) {
    if (op_to_key_mapping_[index_of_last_operation]) {
      op_to_key_mapping_[index_of_last_operation] = std::nullopt;
      TURBOSHAFT_TRACE_TYPING_OK(
          "REM  %3d:%-40s %-40s\n", index_of_last_operation.id(),
          Asm()
              .output_graph()
              .Get(index_of_last_operation)
              .ToString()
              .substr(0, 40)
              .c_str(),
          GetType(index_of_last_operation).ToString().substr(0, 40).c_str());
      output_graph_types_[index_of_last_operation] = Type::Invalid();
    }
    Next::RemoveLast(index_of_last_operation);
  }

 private:
  void RefineTypeFromInputGraph(OpIndex index, const Type& og_type,
                                const Type& ig_type) {
    // Refinement should happen when we just lowered the corresponding
    // operation, so we should be at the point where the operation is defined
    // (e.g. not in a refinement after a branch). So the current block must
    // contain the operation.
    DCHECK(!ig_type.IsInvalid());

    TURBOSHAFT_TRACE_TYPING_OK(
        "Refi %3d:%-40s\n  I:     %-40s ~~> %-40s\n", index.id(),
        Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
        (og_type.IsInvalid() ? "invalid" : og_type.ToString().c_str()),
        ig_type.ToString().c_str());

    RefineOperationType(Asm().current_block(), index, ig_type, 'I');
  }

  Type GetTypeOrInvalid(OpIndex index) {
    if (auto key = op_to_key_mapping_[index]) return table_.Get(*key);
    return Type::Invalid();
  }

  Type GetTupleType(const TupleOp& tuple) {
    base::SmallVector<Type, 4> tuple_types;
    for (OpIndex input : tuple.inputs()) {
      tuple_types.push_back(GetType(input));
    }
    return TupleType::Tuple(base::VectorOf(tuple_types), Asm().graph_zone());
  }

  Type GetType(OpIndex index) {
    Type type = GetTypeOrInvalid(index);
    if (type.IsInvalid()) {
      const Operation& op = Asm().output_graph().Get(index);
      if (op.Is<TupleOp>()) {
        return GetTupleType(op.Cast<TupleOp>());
      } else {
        return Typer::TypeForRepresentation(op.outputs_rep(),
                                            Asm().graph_zone());
      }
    }
    return type;
  }

  void SetType(OpIndex index, const Type& result_type,
               bool is_fallback_for_unsupported_operation = false) {
    DCHECK(!result_type.IsInvalid());

    if (auto key_opt = op_to_key_mapping_[index]) {
      table_.Set(*key_opt, result_type);
      DCHECK(result_type.IsSubtypeOf(output_graph_types_[index]));
      output_graph_types_[index] = result_type;
      DCHECK(!output_graph_types_[index].IsInvalid());
    } else {
      auto key = table_.NewKey(Type::None());
      op_to_key_mapping_[index] = key;
      table_.Set(key, result_type);
      output_graph_types_[index] = result_type;
    }

    if (!is_fallback_for_unsupported_operation) {
      TURBOSHAFT_TRACE_TYPING_OK(
          "Type %3d:%-40s ==> %s\n", index.id(),
          Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
          result_type.ToString().c_str());
    } else {
      // TODO(nicohartmann@): Remove the fallback case once all operations are
      // supported.
      TURBOSHAFT_TRACE_TYPING_FAIL(
          "TODO %3d:%-40s ==> %s\n", index.id(),
          Asm().output_graph().Get(index).ToString().substr(0, 40).c_str(),
          result_type.ToString().c_str());
    }
  }

// Verification is more difficult, now that the output graph uses types from the
// input graph. It is generally not possible to verify that the output graph's
// type is a subtype of the input graph's type, because the typer might not
// support a precise typing of the operations after the lowering.
// TODO(nicohartmann@): Evaluate new strategies for verification.
#if 0
#ifdef DEBUG
  void Verify(OpIndex input_index, OpIndex output_index) {
    DCHECK(input_index.valid());
    DCHECK(output_index.valid());

    const auto& input_type = Asm().input_graph().operation_types()[input_index];
    const auto& output_type = types_[output_index];

    if (input_type.IsInvalid()) return;
    DCHECK(!output_type.IsInvalid());

    const bool is_okay = output_type.IsSubtypeOf(input_type);

    TURBOSHAFT_TRACE_TYPING(
        "\033[%s %3d:%-40s %-40s\n     %3d:%-40s %-40s\033[0m\n",
        is_okay ? "32mOK  " : "31mFAIL", input_index.id(),
        Asm().input_graph().Get(input_index).ToString().substr(0, 40).c_str(),
        input_type.ToString().substr(0, 40).c_str(), output_index.id(),
        Asm().output_graph().Get(output_index).ToString().substr(0, 40).c_str(),
        output_type.ToString().substr(0, 40).c_str());

    if (V8_UNLIKELY(!is_okay)) {
      FATAL(
          "\033[%s %3d:%-40s %-40s\n     %3d:%-40s %-40s\033[0m\n",
          is_okay ? "32mOK  " : "31mFAIL", input_index.id(),
          Asm().input_graph().Get(input_index).ToString().substr(0, 40).c_str(),
          input_type.ToString().substr(0, 40).c_str(), output_index.id(),
          Asm()
              .output_graph()
              .Get(output_index)
              .ToString()
              .substr(0, 40)
              .c_str(),
          output_type.ToString().substr(0, 40).c_str());
    }
  }
#endif
#endif

  bool NeedsTyping(OpIndex index) const {
    return index.valid() && args_.output_graph_typing ==
                                Args::OutputGraphTyping::kRefineFromInputGraph;
  }

  TypeInferenceReducerArgs args_{TypeInferenceReducerArgs::Get()};
  GrowingOpIndexSidetable<Type> input_graph_types_{Asm().graph_zone(),
                                                   &Asm().input_graph()};
  GrowingOpIndexSidetable<Type>& output_graph_types_{
      Asm().output_graph().operation_types()};
  table_t table_{Asm().phase_zone()};
  const Block* current_block_ = nullptr;
  GrowingOpIndexSidetable<std::optional<table_t::Key>> op_to_key_mapping_{
      Asm().phase_zone(), &Asm().output_graph()};
  GrowingBlockSidetable<std::optional<table_t::Snapshot>>
      block_to_snapshot_mapping_{Asm().input_graph().block_count(),
                                 std::nullopt, Asm().phase_zone()};
  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<table_t::Snapshot> predecessors_{Asm().phase_zone()};
  TypeInferenceAnalysis analyzer_{Asm().modifiable_input_graph(),
                                  Asm().phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_TYPE_INFERENCE_REDUCER_H_
