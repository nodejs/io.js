// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/spill-placer.h"

#include "src/base/bits-iterator.h"
#include "src/compiler/backend/register-allocator.h"

namespace v8 {
namespace internal {
namespace compiler {

SpillPlacer::SpillPlacer(RegisterAllocationData* data, Zone* zone)
    : data_(data), zone_(zone) {}

SpillPlacer::~SpillPlacer() {
  if (assigned_indices_ > 0) {
    CommitSpills();
  }
}

void SpillPlacer::Add(TopLevelLiveRange* range) {
  DCHECK(range->HasGeneralSpillRange());
  InstructionOperand spill_operand = range->GetSpillRangeOperand();
  range->FilterSpillMoves(data(), spill_operand);

  InstructionSequence* code = data_->code();
  InstructionBlock* top_start_block =
      code->GetInstructionBlock(range->Start().ToInstructionIndex());
  RpoNumber top_start_block_number = top_start_block->rpo_number();

  // Check for several cases where spilling at the definition is best.
  // - The value is already moved on-stack somehow so the list of insertion
  //   locations for spilling at the definition is empty.
  // - If the first LiveRange is spilled, then there's no sense in doing
  //   anything other than spilling at the definition.
  // - If the value is defined in a deferred block, then the logic to select
  //   the earliest deferred block as the insertion point would cause
  //   incorrect behavior, so the value must be spilled at the definition.
  // - We haven't seen any indication of performance improvements from seeking
  //   optimal spilling positions except on loop-top phi values, so spill
  //   any value that isn't a loop-top phi at the definition to avoid
  //   increasing the code size for no benefit.
  if (range->GetSpillMoveInsertionLocations(data()) == nullptr ||
      range->spilled() || top_start_block->IsDeferred() ||
      (!v8_flags.stress_turbo_late_spilling && !range->is_loop_phi())) {
    range->CommitSpillMoves(data(), spill_operand);
    return;
  }

  // Iterate through the range and mark every block that needs the value to be
  // spilled.
  for (const LiveRange* child = range; child != nullptr;
       child = child->next()) {
    if (child->spilled()) {
      // Add every block that contains part of this live range.
      for (const UseInterval& interval : child->intervals()) {
        RpoNumber start_block =
            code->GetInstructionBlock(interval.start().ToInstructionIndex())
                ->rpo_number();
        if (start_block == top_start_block_number) {
          // Can't do late spilling if the first spill is within the
          // definition block.
          range->CommitSpillMoves(data(), spill_operand);
          // Verify that we never added any data for this range to the table.
          DCHECK(!IsLatestVreg(range->vreg()));
          return;
        }
        LifetimePosition end = interval.end();
        int end_instruction = end.ToInstructionIndex();
        // The end position is exclusive, so an end position exactly on a block
        // boundary indicates that the range applies only to the prior block.
        if (data()->IsBlockBoundary(end)) {
          --end_instruction;
        }
        RpoNumber end_block =
            code->GetInstructionBlock(end_instruction)->rpo_number();
        while (start_block <= end_block) {
          SetSpillRequired(code->InstructionBlockAt(start_block), range->vreg(),
                           top_start_block_number);
          start_block = start_block.Next();
        }
      }
    } else {
      // Add every block that contains a use which requires the on-stack value.
      for (const UsePosition* pos : child->positions()) {
        if (pos->type() != UsePositionType::kRequiresSlot) continue;
        InstructionBlock* block =
            code->GetInstructionBlock(pos->pos().ToInstructionIndex());
        RpoNumber block_number = block->rpo_number();
        if (block_number == top_start_block_number) {
          // Can't do late spilling if the first spill is within the
          // definition block.
          range->CommitSpillMoves(data(), spill_operand);
          // Verify that we never added any data for this range to the table.
          DCHECK(!IsLatestVreg(range->vreg()));
          return;
        }
        SetSpillRequired(block, range->vreg(), top_start_block_number);
      }
    }
  }

  // If we haven't yet marked anything for this range, then it never needs to
  // spill at all.
  if (!IsLatestVreg(range->vreg())) {
    range->SetLateSpillingSelected(true);
    return;
  }

  SetDefinition(top_start_block_number, range->vreg());
}

class SpillPlacer::Entry {
 public:
  // Functions operating on single values (during setup):

  void SetSpillRequiredSingleValue(int value_index) {
    DCHECK_LT(value_index, kValueIndicesPerEntry);
    uint64_t bit = uint64_t{1} << value_index;
    SetSpillRequired(bit);
  }
  void SetDefinitionSingleValue(int value_index) {
    DCHECK_LT(value_index, kValueIndicesPerEntry);
    uint64_t bit = uint64_t{1} << value_index;
    SetDefinition(bit);
  }

  // Functions operating on all values simultaneously, as bitfields:

  uint64_t SpillRequired() const { return GetValuesInState<kSpillRequired>(); }
  void SetSpillRequired(uint64_t mask) {
    UpdateValuesToState<kSpillRequired>(mask);
  }
  uint64_t SpillRequiredInNonDeferredSuccessor() const {
    return GetValuesInState<kSpillRequiredInNonDeferredSuccessor>();
  }
  void SetSpillRequiredInNonDeferredSuccessor(uint64_t mask) {
    UpdateValuesToState<kSpillRequiredInNonDeferredSuccessor>(mask);
  }
  uint64_t SpillRequiredInDeferredSuccessor() const {
    return GetValuesInState<kSpillRequiredInDeferredSuccessor>();
  }
  void SetSpillRequiredInDeferredSuccessor(uint64_t mask) {
    UpdateValuesToState<kSpillRequiredInDeferredSuccessor>(mask);
  }
  uint64_t Definition() const { return GetValuesInState<kDefinition>(); }
  void SetDefinition(uint64_t mask) { UpdateValuesToState<kDefinition>(mask); }

 private:
  // Possible states for every value, at every block.
  enum State {
    // This block is not (yet) known to require the on-stack value.
    kUnmarked,

    // The value must be on the stack in this block.
    kSpillRequired,

    // The value doesn't need to be on-stack in this block, but some
    // non-deferred successor needs it.
    kSpillRequiredInNonDeferredSuccessor,

    // The value doesn't need to be on-stack in this block, but some
    // deferred successor needs it.
    kSpillRequiredInDeferredSuccessor,

    // The value is defined in this block.
    kDefinition,
  };

  template <State state>
  uint64_t GetValuesInState() const {
    static_assert(state < 8);
    return ((state & 1) ? first_bit_ : ~first_bit_) &
           ((state & 2) ? second_bit_ : ~second_bit_) &
           ((state & 4) ? third_bit_ : ~third_bit_);
  }

  template <State state>
  void UpdateValuesToState(uint64_t mask) {
    static_assert(state < 8);
    first_bit_ =
        Entry::UpdateBitDataWithMask<(state & 1) != 0>(first_bit_, mask);
    second_bit_ =
        Entry::UpdateBitDataWithMask<(state & 2) != 0>(second_bit_, mask);
    third_bit_ =
        Entry::UpdateBitDataWithMask<(state & 4) != 0>(third_bit_, mask);
  }

  template <bool set_ones>
  static uint64_t UpdateBitDataWithMask(uint64_t data, uint64_t mask) {
    return set_ones ? data | mask : data & ~mask;
  }

  // Storage for the states of up to 64 live ranges.
  uint64_t first_bit_ = 0;
  uint64_t second_bit_ = 0;
  uint64_t third_bit_ = 0;
};

int SpillPlacer::GetOrCreateIndexForLatestVreg(int vreg) {
  DCHECK_LE(assigned_indices_, kValueIndicesPerEntry);
  // If this vreg isn't yet the last one in the list, then add it.
  if (!IsLatestVreg(vreg)) {
    if (vreg_numbers_ == nullptr) {
      DCHECK_EQ(assigned_indices_, 0);
      DCHECK_EQ(entries_, nullptr);
      // We lazily allocate these arrays because many functions don't have any
      // values that use SpillPlacer.
      entries_ = zone_->AllocateArray<Entry>(
          data()->code()->instruction_blocks().size());
      for (size_t i = 0; i < data()->code()->instruction_blocks().size(); ++i) {
        new (&entries_[i]) Entry();
      }
      vreg_numbers_ = zone_->AllocateArray<int>(kValueIndicesPerEntry);
    }

    if (assigned_indices_ == kValueIndicesPerEntry) {
      // The table is full; commit the current set of values and clear it.
      CommitSpills();
      ClearData();
    }

    vreg_numbers_[assigned_indices_] = vreg;
    ++assigned_indices_;
  }
  return assigned_indices_ - 1;
}

void SpillPlacer::CommitSpills() {
  FirstBackwardPass();
  ForwardPass();
  SecondBackwardPass();
}

void SpillPlacer::ClearData() {
  assigned_indices_ = 0;
  for (int i = 0; i < data()->code()->InstructionBlockCount(); ++i) {
    new (&entries_[i]) Entry();
  }
  first_block_ = RpoNumber::Invalid();
  last_block_ = RpoNumber::Invalid();
}

void SpillPlacer::ExpandBoundsToInclude(RpoNumber block) {
  if (!first_block_.IsValid()) {
    DCHECK(!last_block_.IsValid());
    first_block_ = block;
    last_block_ = block;
  } else {
    if (first_block_ > block) {
      first_block_ = block;
    }
    if (last_block_ < block) {
      last_block_ = block;
    }
  }
}

void SpillPlacer::SetSpillRequired(InstructionBlock* block, int vreg,
                                   RpoNumber top_start_block) {
  // Spilling in loops is bad, so if the block is non-deferred and nested
  // within a loop, and the definition is before that loop, then mark the loop
  // top instead. Of course we must find the outermost such loop.
  if (!block->IsDeferred()) {
    while (block->loop_header().IsValid() &&
           block->loop_header() > top_start_block) {
      block = data()->code()->InstructionBlockAt(block->loop_header());
    }
  }

  int value_index = GetOrCreateIndexForLatestVreg(vreg);
  entries_[block->rpo_number().ToSize()].SetSpillRequiredSingleValue(
      value_index);
  ExpandBoundsToInclude(block->rpo_number());
}

void SpillPlacer::SetDefinition(RpoNumber block, int vreg) {
  int value_index = GetOrCreateIndexForLatestVreg(vreg);
  entries_[block.ToSize()].SetDefinitionSingleValue(value_index);
  ExpandBoundsToInclude(block);
}

void SpillPlacer::FirstBackwardPass() {
  InstructionSequence* code = data()->code();

  for (int i = last_block_.ToInt(); i >= first_block_.ToInt(); --i) {
    RpoNumber block_id = RpoNumber::FromInt(i);
    InstructionBlock* block = code->instruction_blocks()[i];

    Entry& entry = entries_[i];

    // State that will be accumulated from successors.
    uint64_t spill_required_in_non_deferred_successor = 0;
    uint64_t spill_required_in_deferred_successor = 0;

    for (RpoNumber successor_id : block->successors()) {
      // Ignore loop back-edges.
      if (successor_id <= block_id) continue;

      InstructionBlock* successor = code->InstructionBlockAt(successor_id);
      const Entry& successor_entry = entries_[successor_id.ToSize()];
      if (successor->IsDeferred()) {
        spill_required_in_deferred_successor |= successor_entry.SpillRequired();
      } else {
        spill_required_in_non_deferred_successor |=
            successor_entry.SpillRequired();
      }
      spill_required_in_deferred_successor |=
          successor_entry.SpillRequiredInDeferredSuccessor();
      spill_required_in_non_deferred_successor |=
          successor_entry.SpillRequiredInNonDeferredSuccessor();
    }

    // Starting state of the current block.
    uint64_t defs = entry.Definition();
    uint64_t needs_spill = entry.SpillRequired();

    // Info about successors doesn't get to override existing info about
    // definitions and spills required by this block itself.
    spill_required_in_deferred_successor &= ~(defs | needs_spill);
    spill_required_in_non_deferred_successor &= ~(defs | needs_spill);

    entry.SetSpillRequiredInDeferredSuccessor(
        spill_required_in_deferred_successor);
    entry.SetSpillRequiredInNonDeferredSuccessor(
        spill_required_in_non_deferred_successor);
  }
}

void SpillPlacer::ForwardPass() {
  InstructionSequence* code = data()->code();
  for (int i = first_block_.ToInt(); i <= last_block_.ToInt(); ++i) {
    RpoNumber block_id = RpoNumber::FromInt(i);
    InstructionBlock* block = code->instruction_blocks()[i];

    // Deferred blocks don't need to participate in the forward pass, because
    // their spills all get pulled forward to the earliest possible deferred
    // block (where a non-deferred block jumps to a deferred block), and
    // decisions about spill requirements for non-deferred blocks don't take
    // deferred blocks into account.
    if (block->IsDeferred()) continue;

    Entry& entry = entries_[i];

    // State that will be accumulated from predecessors.
    uint64_t spill_required_in_non_deferred_predecessor = 0;
    uint64_t spill_required_in_all_non_deferred_predecessors =
        static_cast<uint64_t>(int64_t{-1});

    for (RpoNumber predecessor_id : block->predecessors()) {
      // Ignore loop back-edges.
      if (predecessor_id >= block_id) continue;

      InstructionBlock* predecessor = code->InstructionBlockAt(predecessor_id);
      if (predecessor->IsDeferred()) continue;
      const Entry& predecessor_entry = entries_[predecessor_id.ToSize()];
      spill_required_in_non_deferred_predecessor |=
          predecessor_entry.SpillRequired();
      spill_required_in_all_non_deferred_predecessors &=
          predecessor_entry.SpillRequired();
    }

    // Starting state of the current block.
    uint64_t spill_required_in_non_deferred_successor =
        entry.SpillRequiredInNonDeferredSuccessor();
    uint64_t spill_required_in_any_successor =
        spill_required_in_non_deferred_successor |
        entry.SpillRequiredInDeferredSuccessor();

    // If all of the predecessors agree that a spill is required, then a
    // spill is required. Note that we don't set anything for values that
    // currently have no markings in this block, to avoid pushing data too
    // far down the graph and confusing the next backward pass.
    entry.SetSpillRequired(spill_required_in_any_successor &
                           spill_required_in_non_deferred_predecessor &
                           spill_required_in_all_non_deferred_predecessors);

    // If only some of the predecessors require a spill, but some successor
    // of this block also requires a spill, then this merge point requires a
    // spill. This ensures that no control-flow path through non-deferred
    // blocks ever has to spill twice.
    entry.SetSpillRequired(spill_required_in_non_deferred_successor &
                           spill_required_in_non_deferred_predecessor);
  }
}

void SpillPlacer::SecondBackwardPass() {
  InstructionSequence* code = data()->code();
  for (int i = last_block_.ToInt(); i >= first_block_.ToInt(); --i) {
    RpoNumber block_id = RpoNumber::FromInt(i);
    InstructionBlock* block = code->instruction_blocks()[i];

    Entry& entry = entries_[i];

    // State that will be accumulated from successors.
    uint64_t spill_required_in_non_deferred_successor = 0;
    uint64_t spill_required_in_deferred_successor = 0;
    uint64_t spill_required_in_all_non_deferred_successors =
        static_cast<uint64_t>(int64_t{-1});

    for (RpoNumber successor_id : block->successors()) {
      // Ignore loop back-edges.
      if (successor_id <= block_id) continue;

      InstructionBlock* successor = code->InstructionBlockAt(successor_id);
      const Entry& successor_entry = entries_[successor_id.ToSize()];
      if (successor->IsDeferred()) {
        spill_required_in_deferred_successor |= successor_entry.SpillRequired();
      } else {
        spill_required_in_non_deferred_successor |=
            successor_entry.SpillRequired();
        spill_required_in_all_non_deferred_successors &=
            successor_entry.SpillRequired();
      }
    }

    // Starting state of the current block.
    uint64_t defs = entry.Definition();

    // If all of the successors of a definition need the value to be
    // spilled, then the value should be spilled at the definition.
    uint64_t spill_at_def = defs & spill_required_in_non_deferred_successor &
                            spill_required_in_all_non_deferred_successors;
    for (int index_to_spill : base::bits::IterateBits(spill_at_def)) {
      int vreg_to_spill = vreg_numbers_[index_to_spill];
      TopLevelLiveRange* top = data()->live_ranges()[vreg_to_spill];
      top->CommitSpillMoves(data(), top->GetSpillRangeOperand());
    }

    if (block->IsDeferred()) {
      DCHECK_EQ(defs, 0);
      // Any deferred successor needing a spill is sufficient to make the
      // current block need a spill.
      entry.SetSpillRequired(spill_required_in_deferred_successor);
    }

    // Propagate data upward if there are non-deferred successors and they
    // all need a spill, regardless of whether the current block is
    // deferred.
    entry.SetSpillRequired(~defs & spill_required_in_non_deferred_successor &
                           spill_required_in_all_non_deferred_successors);

    // Iterate the successors again to find out which ones require spills at
    // their beginnings, and insert those spills.
    for (RpoNumber successor_id : block->successors()) {
      // Ignore loop back-edges.
      if (successor_id <= block_id) continue;

      InstructionBlock* successor = code->InstructionBlockAt(successor_id);
      const Entry& successor_entry = entries_[successor_id.ToSize()];
      for (int index_to_spill :
           base::bits::IterateBits(successor_entry.SpillRequired() &
                                   ~entry.SpillRequired() & ~spill_at_def)) {
        CommitSpill(vreg_numbers_[index_to_spill], block, successor);
      }
    }
  }
}

void SpillPlacer::CommitSpill(int vreg, InstructionBlock* predecessor,
                              InstructionBlock* successor) {
  TopLevelLiveRange* live_range = data()->live_ranges()[vreg];
  LifetimePosition pred_end = LifetimePosition::InstructionFromInstructionIndex(
      predecessor->last_instruction_index());
  LiveRange* child_range = live_range->GetChildCovers(pred_end);
  DCHECK_NOT_NULL(child_range);
  InstructionOperand pred_op = child_range->GetAssignedOperand();
  DCHECK(pred_op.IsAnyRegister());
  DCHECK_EQ(successor->PredecessorCount(), 1);
  data()->AddGapMove(successor->first_instruction_index(),
                     Instruction::GapPosition::START, pred_op,
                     live_range->GetSpillRangeOperand());
  successor->mark_needs_frame();
  live_range->SetLateSpillingSelected(true);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
