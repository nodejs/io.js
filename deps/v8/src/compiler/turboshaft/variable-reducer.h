// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_

#include <algorithm>
#include <optional>

#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

// When cloning a Block or duplicating an Operation, we end up with some
// Operations of the old graph mapping to multiple Operations in the new graph.
// When using those Operations in subsequent Operations, we need to know which
// of the new-Operation to use, and, in particular, if a Block has 2
// predecessors that have a mapping for the same old-Operation, we need to
// merge them in a Phi node. All of this is handled by the VariableAssembler.
//
// The typical workflow when working with the VariableAssembler would be:
//    - At some point, you need to introduce a Variable (for instance
//      because you cloned a block or an Operation) and call NewVariable or
//      NewLoopInvariantVariable to get a fresh Variable. A loop invariant
//      variable must not need loop phis, that is, not change its value
//      depending on loop iteration while being visible across loop iterations.
//    - You can then Set the new-OpIndex associated with this Variable in the
//      current Block with the Set method.
//    - If you later need to set an OpIndex for this Variable in another Block,
//      call Set again.
//    - At any time, you can call Get to get the new-Operation associated to
//      this Variable. Get will return:
//         * if the current block is dominated by a block who did a Set on the
//           Variable, then the Operation that was Set then.
//         * otherwise, the current block must be dominated by a Merge whose
//           predecessors have all Set this Variable. In that case, the
//           VariableAssembler introduced a Phi in this merge, and will return
//           this Phi.
//
// Note that the VariableAssembler does not do "old-OpIndex => Variable"
// book-keeping: the users of the Variable should do that themselves (which
// is what CopyingPhase does for instance).

// VariableReducer always adds a RequiredOptimizationReducer, because phis
// with constant inputs introduced by `VariableReducer` need to be eliminated.
template <class AfterNext>
class VariableReducer : public RequiredOptimizationReducer<AfterNext> {
  using Next = RequiredOptimizationReducer<AfterNext>;
  using Snapshot = SnapshotTable<OpIndex, VariableData>::Snapshot;

  struct GetActiveLoopVariablesIndex {
    IntrusiveSetIndex& operator()(Variable var) const {
      return var.data().active_loop_variables_index;
    }
  };

  struct VariableTable
      : ChangeTrackingSnapshotTable<VariableTable, OpIndex, VariableData> {
    explicit VariableTable(Zone* zone)
        : ChangeTrackingSnapshotTable<VariableTable, OpIndex, VariableData>(
              zone),
          active_loop_variables(zone) {}

    ZoneIntrusiveSet<Variable, GetActiveLoopVariablesIndex>
        active_loop_variables;

    void OnNewKey(Variable var, OpIndex value) { DCHECK(!value.valid()); }
    void OnValueChange(Variable var, OpIndex old_value, OpIndex new_value) {
      if (var.data().loop_invariant) {
        return;
      }
      if (old_value.valid() && !new_value.valid()) {
        active_loop_variables.Remove(var);
      } else if (!old_value.valid() && new_value.valid()) {
        active_loop_variables.Add(var);
      }
    }
  };

 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(VariableReducer)

  void Bind(Block* new_block) {
    Next::Bind(new_block);

    SealAndSaveVariableSnapshot();

    predecessors_.clear();
    for (const Block* pred : new_block->PredecessorsIterable()) {
      std::optional<Snapshot> pred_snapshot =
          block_to_snapshot_mapping_[pred->index()];
      DCHECK(pred_snapshot.has_value());
      predecessors_.push_back(pred_snapshot.value());
    }
    std::reverse(predecessors_.begin(), predecessors_.end());

    auto merge_variables =
        [&](Variable var, base::Vector<const OpIndex> predecessors) -> OpIndex {
      for (OpIndex idx : predecessors) {
        if (!idx.valid()) {
          // If any of the predecessors' value is Invalid, then we shouldn't
          // merge {var}.
          return OpIndex::Invalid();
        } else if (__ output_graph()
                       .Get(idx)
                       .template Is<LoadRootRegisterOp>()) {
          // Variables that once contain the root register never contain another
          // value.
          return __ LoadRootRegister();
        }
      }
      return MergeOpIndices(predecessors, var.data().rep);
    };

    table_.StartNewSnapshot(base::VectorOf(predecessors_), merge_variables);
    current_block_ = new_block;
    if (new_block->IsLoop()) {
      // When starting a loop, we need to create a PendingLoopPhi for each
      // currently active variable (except those that are marked as
      // loop-invariant).
      auto active_loop_variables_begin = table_.active_loop_variables.begin();
      auto active_loop_variables_end = table_.active_loop_variables.end();
      if (active_loop_variables_begin != active_loop_variables_end) {
        ZoneVector<std::pair<Variable, OpIndex>> pending_phis(__ phase_zone());
        for (Variable var : table_.active_loop_variables) {
          MaybeRegisterRepresentation rep = var.data().rep;
          DCHECK_NE(rep, MaybeRegisterRepresentation::None());
          V<Any> pending_loop_phi =
              __ PendingLoopPhi(table_.Get(var), RegisterRepresentation(rep));
          SetVariable(var, pending_loop_phi);
          pending_phis.push_back({var, pending_loop_phi});
        }
        loop_pending_phis_[new_block->index()].emplace(pending_phis);
      }
    }
  }

  void RestoreTemporaryVariableSnapshotAfter(const Block* block) {
    DCHECK(table_.IsSealed());
    DCHECK(block_to_snapshot_mapping_[block->index()].has_value());
    table_.StartNewSnapshot(*block_to_snapshot_mapping_[block->index()]);
    is_temporary_ = true;
  }
  void CloseTemporaryVariableSnapshot() {
    DCHECK(is_temporary_);
    table_.Seal();
    is_temporary_ = false;
  }

  V<None> REDUCE(Goto)(Block* destination, bool is_backedge) {
    V<None> result = Next::ReduceGoto(destination, is_backedge);
    if (!destination->IsBound()) {
      return result;
    }

    // For loops, we have to "fix" the PendingLoopPhis (= replace them with
    // regular loop phis).
    DCHECK(destination->IsLoop());
    DCHECK_EQ(destination->PredecessorCount(), 2);

    if (loop_pending_phis_.contains(destination->index())) {
      for (auto [var, pending_phi_idx] :
           loop_pending_phis_[destination->index()].value()) {
        const PendingLoopPhiOp& pending_phi =
            __ Get(pending_phi_idx).template Cast<PendingLoopPhiOp>();
        __ output_graph().template Replace<PhiOp>(
            pending_phi_idx,
            base::VectorOf({pending_phi.first(), GetVariable(var)}),
            pending_phi.rep);
      }
    }

    return result;
  }

  OpIndex GetVariable(Variable var) { return table_.Get(var); }

  OpIndex GetPredecessorValue(Variable var, int predecessor_index) {
    return table_.GetPredecessorValue(var, predecessor_index);
  }

  void SetVariable(Variable var, OpIndex new_index) {
    DCHECK(!is_temporary_);
    if (V8_UNLIKELY(__ generating_unreachable_operations())) return;
    table_.Set(var, new_index);
  }
  template <typename Rep>
  void Set(Variable var, V<Rep> value) {
    DCHECK(!is_temporary_);
    if (V8_UNLIKELY(__ generating_unreachable_operations())) return;
    DCHECK(Rep::allows_representation(RegisterRepresentation(var.data().rep)));
    table_.Set(var, value);
  }

  Variable NewLoopInvariantVariable(MaybeRegisterRepresentation rep) {
    DCHECK(!is_temporary_);
    return table_.NewKey(VariableData{rep, true}, OpIndex::Invalid());
  }
  Variable NewVariable(MaybeRegisterRepresentation rep) {
    DCHECK(!is_temporary_);
    return table_.NewKey(VariableData{rep, false}, OpIndex::Invalid());
  }

  // SealAndSaveVariableSnapshot seals the current snapshot, and stores it in
  // {block_to_snapshot_mapping_}, so that it can be used for later merging.
  void SealAndSaveVariableSnapshot() {
    if (table_.IsSealed()) {
      DCHECK_EQ(current_block_, nullptr);
      return;
    }

    DCHECK_NOT_NULL(current_block_);
    block_to_snapshot_mapping_[current_block_->index()] = table_.Seal();
    current_block_ = nullptr;
  }

 private:
  OpIndex MergeOpIndices(base::Vector<const OpIndex> inputs,
                         MaybeRegisterRepresentation maybe_rep) {
    if (maybe_rep != MaybeRegisterRepresentation::None()) {
      // Every Operation that has a RegisterRepresentation can be merged with a
      // simple Phi.
      return __ Phi(base::VectorOf(inputs), RegisterRepresentation(maybe_rep));
    } else if (__ output_graph().Get(inputs[0]).template Is<FrameStateOp>()) {
      // Frame states need be be merged recursively, because they represent
      // multiple scalar values that will lead to multiple phi nodes.
      return MergeFrameState(inputs);
    } else {
      return OpIndex::Invalid();
    }
  }

  OpIndex MergeFrameState(base::Vector<const OpIndex> frame_states_indices) {
    base::SmallVector<const FrameStateOp*, 32> frame_states;
    for (OpIndex idx : frame_states_indices) {
      frame_states.push_back(
          &__ output_graph().Get(idx).template Cast<FrameStateOp>());
    }
    const FrameStateOp* first_frame = frame_states[0];

#if DEBUG
    // Making sure that all frame states have the same number of inputs, the
    // same "inlined" field, and the same data.
    for (auto frame_state : frame_states) {
      DCHECK_EQ(first_frame->input_count, frame_state->input_count);
      DCHECK_EQ(first_frame->inlined, frame_state->inlined);
      DCHECK_EQ(first_frame->data, frame_state->data);
    }
#endif

    base::SmallVector<OpIndex, 32> new_inputs;

    // Merging the parent frame states.
    if (first_frame->inlined) {
      ZoneVector<OpIndex> indices_to_merge(__ phase_zone());
      bool all_parent_frame_states_are_the_same = true;
      for (auto frame_state : frame_states) {
        indices_to_merge.push_back(frame_state->parent_frame_state());
        all_parent_frame_states_are_the_same =
            all_parent_frame_states_are_the_same &&
            first_frame->parent_frame_state() ==
                frame_state->parent_frame_state();
      }
      if (all_parent_frame_states_are_the_same) {
        new_inputs.push_back(first_frame->parent_frame_state());
      } else {
        OpIndex merged_parent_frame_state =
            MergeFrameState(base::VectorOf(indices_to_merge));
        new_inputs.push_back(merged_parent_frame_state);
      }
    }

    // Merging the state values.
    for (int i = 0; i < first_frame->state_values_count(); i++) {
      ZoneVector<OpIndex> indices_to_merge(__ phase_zone());
      bool all_inputs_are_the_same = true;
      for (auto frame_state : frame_states) {
        indices_to_merge.push_back(frame_state->state_value(i));
        all_inputs_are_the_same =
            all_inputs_are_the_same &&
            first_frame->state_value(i) == frame_state->state_value(i);
      }
      if (all_inputs_are_the_same) {
        // This input does not need to be merged, since its identical for all of
        // the frame states.
        new_inputs.push_back(first_frame->state_value(i));
      } else {
        RegisterRepresentation rep = first_frame->state_value_rep(i);
        OpIndex new_input =
            MergeOpIndices(base::VectorOf(indices_to_merge), rep);
        new_inputs.push_back(new_input);
      }
    }

    return __ FrameState(base::VectorOf(new_inputs), first_frame->inlined,
                         first_frame->data);
  }

  VariableTable table_{__ phase_zone()};
  const Block* current_block_ = nullptr;
  GrowingBlockSidetable<std::optional<Snapshot>> block_to_snapshot_mapping_{
      __ input_graph().block_count(), std::nullopt, __ phase_zone()};
  bool is_temporary_ = false;

  // {predecessors_} is used during merging, but we use an instance variable for
  // it, in order to save memory and not reallocate it for each merge.
  ZoneVector<Snapshot> predecessors_{__ phase_zone()};

  // Map from loop headers to the pending loop phis in these headers which have
  // to be patched on backedges.
  ZoneAbslFlatHashMap<BlockIndex,
                      std::optional<ZoneVector<std::pair<Variable, OpIndex>>>>
      loop_pending_phis_{__ phase_zone()};
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_VARIABLE_REDUCER_H_
