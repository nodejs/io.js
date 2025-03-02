// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_GC_TYPED_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_GC_TYPED_OPTIMIZATION_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/snapshot-table-opindex.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8::internal::compiler::turboshaft {

// The WasmGCTypedOptimizationReducer infers type information based on the input
// graph and reduces type checks and casts based on that information.
//
// This is done in two steps:
// 1) The WasmGCTypeAnalyzer infers the types based on the input graph, e.g.:
//    func (param anyref) (result i32)
//      local.get 0
//      ref.test $MyType
//      if                     // local 0 is known to be (a subtype of) $MyType
//        local.get 0
//        ref.cast $MyType     // the input of this cast is a subtype of $MyType
//                             // it can be removed during reduction
//        struct.get $MyType 0
//        return
//      end                    // local 0 is still anyref
//        i32.const 0
//
// 2) The WasmGCTypedOptimizationReducer reduces the graph to a new graph
//    potentially removing, simplifying (e.g. replacing a cast with a null
//    check) or refining (setting the from type to a more specific type) type
//    operations.

class WasmGCTypeAnalyzer {
 public:
  WasmGCTypeAnalyzer(PipelineData* data, Graph& graph, Zone* zone)
      : data_(data), graph_(graph), phase_zone_(zone) {
    // If we ever want to run this analyzer for Wasm wrappers, we'll need
    // to make it handle their {CanonicalSig} signatures.
    DCHECK_NOT_NULL(signature_);
  }

  void Run();

  // Returns the input type for the operation or bottom if the operation shall
  // always trap.
  wasm::ValueType GetInputTypeOrSentinelType(OpIndex op) const {
    auto iter = input_type_map_.find(op);
    DCHECK_NE(iter, input_type_map_.end());
    return iter->second;
  }

 private:
  using TypeSnapshotTable = SparseOpIndexSnapshotTable<wasm::ValueType>;
  using Snapshot = TypeSnapshotTable::Snapshot;
  using MaybeSnapshot = TypeSnapshotTable::MaybeSnapshot;

  void StartNewSnapshotFor(const Block& block);
  void ProcessOperations(const Block& block);
  void ProcessBlock(const Block& block);
  void ProcessBranchOnTarget(const BranchOp& branch, const Block& target);

  void ProcessTypeCast(const WasmTypeCastOp& type_cast);
  void ProcessTypeCheck(const WasmTypeCheckOp& type_check);
  void ProcessAssertNotNull(const AssertNotNullOp& type_cast);
  void ProcessNull(const NullOp& null);
  void ProcessIsNull(const IsNullOp& is_null);
  void ProcessParameter(const ParameterOp& parameter);
  void ProcessStructGet(const StructGetOp& struct_get);
  void ProcessStructSet(const StructSetOp& struct_set);
  void ProcessArrayGet(const ArrayGetOp& array_get);
  void ProcessArrayLength(const ArrayLengthOp& array_length);
  void ProcessGlobalGet(const GlobalGetOp& global_get);
  void ProcessRefFunc(const WasmRefFuncOp& ref_func);
  void ProcessAllocateArray(const WasmAllocateArrayOp& allocate_array);
  void ProcessAllocateStruct(const WasmAllocateStructOp& allocate_struct);
  void ProcessPhi(const PhiOp& phi);
  void ProcessTypeAnnotation(const WasmTypeAnnotationOp& type_annotation);

  wasm::ValueType GetTypeForPhiInput(const PhiOp& phi, int input_index);

  void CreateMergeSnapshot(const Block& block);
  bool CreateMergeSnapshot(base::Vector<const Snapshot> predecessors,
                           base::Vector<const bool> reachable);

  // Updates the knowledge in the side table about the type of {object},
  // returning the previous known type. Returns bottom if the refined type is
  // uninhabited. In this case the operation shall always trap.
  wasm::ValueType RefineTypeKnowledge(OpIndex object, wasm::ValueType new_type,
                                      const Operation& op);
  // Updates the knowledge in the side table to be a non-nullable type for
  // {object}, returning the previous known type. Returns bottom if the refined
  // type is uninhabited. In this case the operation shall always trap.
  wasm::ValueType RefineTypeKnowledgeNotNull(OpIndex object,
                                             const Operation& op);

  OpIndex ResolveAliases(OpIndex object) const;
  wasm::ValueType GetResolvedType(OpIndex object) const;

  // Returns the reachability status of a block. For any predecessor, this marks
  // whether the *end* of the block is reachable, for the current block it marks
  // whether the current instruction is reachable. (For successors the
  // reachability is unknown.)
  bool IsReachable(const Block& block) const;

  PipelineData* data_;
  Graph& graph_;
  Zone* phase_zone_;
  const wasm::WasmModule* module_ = data_->wasm_module();
  const wasm::FunctionSig* signature_ = data_->wasm_module_sig();
  // Contains the snapshots for all blocks in the CFG.
  TypeSnapshotTable types_table_{phase_zone_};
  // Maps the block id to a snapshot in the table defining the type knowledge
  // at the end of the block.
  FixedBlockSidetable<MaybeSnapshot> block_to_snapshot_{graph_.block_count(),
                                                        phase_zone_};

  // Tracks reachability of blocks throughout the analysis. Marking a block as
  // unreachable means that the block in question is unreachable from the
  // current "point of view" of the analysis, e.g. marking the current block as
  // "unreachable" means that from "now on" all succeeding statements can treat
  // it as unreachable, not that the beginning of the block was unreachable.
  BitVector block_is_unreachable_{static_cast<int>(graph_.block_count()),
                                  phase_zone_};

  const Block* current_block_ = nullptr;
  // For any operation that could potentially refined, this map stores an entry
  // to the inferred input type based on the analysis.
  ZoneUnorderedMap<OpIndex, wasm::ValueType> input_type_map_{phase_zone_};
  // Marker wheteher it is the first time visiting a loop header. In that case,
  // loop phis can only use type information based on the forward edge of the
  // loop. The value is false outside of loop headers.
  bool is_first_loop_header_evaluation_ = false;
};

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <class Next>
class WasmGCTypedOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(WasmGCTypedOptimization)

  void Analyze() {
    analyzer_.Run();
    Next::Analyze();
  }

  V<Object> REDUCE_INPUT_GRAPH(WasmTypeCast)(V<Object> op_idx,
                                             const WasmTypeCastOp& cast_op) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphWasmTypeCast(op_idx, cast_op);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      // We are either already in unreachable code (then this instruction isn't
      // even emitted) or the type analyzer inferred that this instruction will
      // always trap. In either case emitting an unconditional trap to increase
      // the chances of logic errors just leading to wrong behaviors but not
      // resulting in security issues.
      __ TrapIf(1, TrapId::kTrapIllegalCast);
      __ Unreachable();
      return OpIndex::Invalid();
    }
    if (type != wasm::ValueType()) {
      CHECK(!type.is_uninhabited());
      CHECK(wasm::IsSameTypeHierarchy(type.heap_type(),
                                      cast_op.config.to.heap_type(), module_));
      bool to_nullable = cast_op.config.to.is_nullable();
      if (wasm::IsHeapSubtypeOf(type.heap_type(), cast_op.config.to.heap_type(),
                                module_, module_)) {
        if (to_nullable || type.is_non_nullable()) {
          // The inferred type is already as specific as the cast target, the
          // cast is guaranteed to always succeed and can therefore be removed.
          return __ MapToNewGraph(cast_op.object());
        } else {
          // The inferred heap type is already as specific as the cast target,
          // but the source can be nullable and the target cannot be, so a null
          // check is still required.
          return __ AssertNotNull(__ MapToNewGraph(cast_op.object()), type,
                                  TrapId::kTrapIllegalCast);
        }
      }
      if (wasm::HeapTypesUnrelated(type.heap_type(),
                                   cast_op.config.to.heap_type(), module_,
                                   module_)) {
        // A cast between unrelated types can only succeed if the argument is
        // null. Otherwise, it always fails.
        V<Word32> non_trapping_condition =
            type.is_nullable() && to_nullable ? __ IsNull(__ MapToNewGraph(
                                                              cast_op.object()),
                                                          type)
                                              : __ Word32Constant(0);
        __ TrapIfNot(non_trapping_condition, TrapId::kTrapIllegalCast);
        if (!to_nullable) {
          __ Unreachable();
        }
        return __ MapToNewGraph(cast_op.object());
      }

      // If the cast resulted in an uninhabitable type, the analyzer should have
      // returned a sentinel (bottom) type as {type}.
      CHECK(!wasm::Intersection(type, cast_op.config.to, module_, module_)
                 .type.is_uninhabited());

      // The cast cannot be replaced. Still, we can refine the source type, so
      // that the lowering could potentially skip null or smi checks.
      wasm::ValueType from_type =
          wasm::Intersection(type, cast_op.config.from, module_, module_).type;
      DCHECK(!from_type.is_uninhabited());
      WasmTypeCheckConfig config{from_type, cast_op.config.to};
      return __ WasmTypeCast(__ MapToNewGraph(cast_op.object()),
                             __ MapToNewGraph(cast_op.rtt()), config);
    }
    goto no_change;
  }

  V<Word32> REDUCE_INPUT_GRAPH(WasmTypeCheck)(
      V<Word32> op_idx, const WasmTypeCheckOp& type_check) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphWasmTypeCheck(op_idx, type_check);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      __ Unreachable();
      return OpIndex::Invalid();
    }
    if (type != wasm::ValueType()) {
      CHECK(!type.is_uninhabited());
      CHECK(wasm::IsSameTypeHierarchy(
          type.heap_type(), type_check.config.to.heap_type(), module_));
      bool to_nullable = type_check.config.to.is_nullable();
      if (wasm::IsHeapSubtypeOf(type.heap_type(),
                                type_check.config.to.heap_type(), module_,
                                module_)) {
        if (to_nullable || type.is_non_nullable()) {
          // The inferred type is guaranteed to be a subtype of the checked
          // type.
          return __ Word32Constant(1);
        } else {
          // The inferred type is guaranteed to be a subtype of the checked
          // type if it is not null.
          return __ Word32Equal(
              __ IsNull(__ MapToNewGraph(type_check.object()), type), 0);
        }
      }
      if (wasm::HeapTypesUnrelated(type.heap_type(),
                                   type_check.config.to.heap_type(), module_,
                                   module_)) {
        if (to_nullable && type.is_nullable()) {
          return __ IsNull(__ MapToNewGraph(type_check.object()), type);
        } else {
          return __ Word32Constant(0);
        }
      }

      // If there isn't a type that matches our known input type and the
      // type_check.config.to type, the type check always fails.
      wasm::ValueType true_type =
          wasm::Intersection(type, type_check.config.to, module_, module_).type;
      if (true_type.is_uninhabited()) {
        return __ Word32Constant(0);
      }

      // The check cannot be replaced. Still, we can refine the source type, so
      // that the lowering could potentially skip null or smi checks.
      wasm::ValueType from_type =
          wasm::Intersection(type, type_check.config.from, module_, module_)
              .type;
      DCHECK(!from_type.is_uninhabited());
      WasmTypeCheckConfig config{from_type, type_check.config.to};
      return __ WasmTypeCheck(__ MapToNewGraph(type_check.object()),
                              __ MapToNewGraph(type_check.rtt()), config);
    }
    goto no_change;
  }

  V<Object> REDUCE_INPUT_GRAPH(AssertNotNull)(
      V<Object> op_idx, const AssertNotNullOp& assert_not_null) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphAssertNotNull(op_idx, assert_not_null);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      // We are either already in unreachable code (then this instruction isn't
      // even emitted) or the type analyzer inferred that this instruction will
      // always trap. In either case emitting an unconditional trap to increase
      // the chances of logic errors just leading to wrong behaviors but not
      // resulting in security issues.
      __ TrapIf(1, assert_not_null.trap_id);
      __ Unreachable();
      return OpIndex::Invalid();
    }
    if (type.is_non_nullable()) {
      return __ MapToNewGraph(assert_not_null.object());
    }
    goto no_change;
  }

  V<Word32> REDUCE_INPUT_GRAPH(IsNull)(V<Word32> op_idx,
                                       const IsNullOp& is_null) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphIsNull(op_idx, is_null);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      __ Unreachable();
      return OpIndex::Invalid();
    }
    if (type.is_non_nullable()) {
      return __ Word32Constant(0);
    }
    if (type != wasm::ValueType() &&
        wasm::ToNullSentinel({type, module_}) == type) {
      return __ Word32Constant(1);
    }
    goto no_change;
  }

  V<Object> REDUCE_INPUT_GRAPH(WasmTypeAnnotation)(
      V<Object> op_idx, const WasmTypeAnnotationOp& type_annotation) {
    // Remove type annotation operations as they are not needed any more.
    return __ MapToNewGraph(type_annotation.value());
  }

  V<Any> REDUCE_INPUT_GRAPH(StructGet)(V<Any> op_idx,
                                       const StructGetOp& struct_get) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphStructGet(op_idx, struct_get);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      // We are either already in unreachable code (then this instruction isn't
      // even emitted) or the type analyzer inferred that this instruction will
      // always trap. In either case emitting an unconditional trap to increase
      // the chances of logic errors just leading to wrong behaviors but not
      // resulting in security issues.
      __ TrapIf(1, TrapId::kTrapNullDereference);
      __ Unreachable();
      return OpIndex::Invalid();
    }
    // Remove the null check if it is known to be not null.
    if (struct_get.null_check == kWithNullCheck && type.is_non_nullable()) {
      return __ StructGet(__ MapToNewGraph(struct_get.object()),
                          struct_get.type, struct_get.type_index,
                          struct_get.field_index, struct_get.is_signed,
                          kWithoutNullCheck);
    }
    goto no_change;
  }

  V<None> REDUCE_INPUT_GRAPH(StructSet)(V<None> op_idx,
                                        const StructSetOp& struct_set) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphStructSet(op_idx, struct_set);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    if (type.is_uninhabited()) {
      // We are either already in unreachable code (then this instruction isn't
      // even emitted) or the type analyzer inferred that this instruction will
      // always trap. In either case emitting an unconditional trap to increase
      // the chances of logic errors just leading to wrong behaviors but not
      // resulting in security issues.
      __ TrapIf(1, TrapId::kTrapNullDereference);
      __ Unreachable();
      return OpIndex::Invalid();
    }
    // Remove the null check if it is known to be not null.
    if (struct_set.null_check == kWithNullCheck && type.is_non_nullable()) {
      __ StructSet(__ MapToNewGraph(struct_set.object()),
                   __ MapToNewGraph(struct_set.value()), struct_set.type,
                   struct_set.type_index, struct_set.field_index,
                   kWithoutNullCheck);
      return OpIndex::Invalid();
    }
    goto no_change;
  }

  V<Word32> REDUCE_INPUT_GRAPH(ArrayLength)(V<Word32> op_idx,
                                            const ArrayLengthOp& array_length) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceInputGraphArrayLength(op_idx, array_length);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    const wasm::ValueType type = analyzer_.GetInputTypeOrSentinelType(op_idx);
    // Remove the null check if it is known to be not null.
    if (array_length.null_check == kWithNullCheck && type.is_non_nullable()) {
      return __ ArrayLength(__ MapToNewGraph(array_length.array()),
                            kWithoutNullCheck);
    }
    goto no_change;
  }

  // TODO(14108): This isn't a type optimization and doesn't fit well into this
  // reducer.
  V<Object> REDUCE(AnyConvertExtern)(V<Object> object) {
    LABEL_BLOCK(no_change) { return Next::ReduceAnyConvertExtern(object); }
    if (ShouldSkipOptimizationStep()) goto no_change;

    if (object.valid()) {
      const ExternConvertAnyOp* externalize =
          __ output_graph().Get(object).template TryCast<ExternConvertAnyOp>();
      if (externalize != nullptr) {
        // Directly return the object as
        // any.convert_extern(extern.convert_any(x)) == x.
        return externalize->object();
      }
    }
    goto no_change;
  }

 private:
  Graph& graph_ = __ modifiable_input_graph();
  const wasm::WasmModule* module_ = __ data() -> wasm_module();
  WasmGCTypeAnalyzer analyzer_{__ data(), graph_, __ phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_WASM_GC_TYPED_OPTIMIZATION_REDUCER_H_
