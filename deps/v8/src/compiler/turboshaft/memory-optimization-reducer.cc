// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/memory-optimization-reducer.h"

#include "src/codegen/interface-descriptors-inl.h"
#include "src/compiler/linkage.h"
#include "src/roots/roots-inl.h"

namespace v8::internal::compiler::turboshaft {

const TSCallDescriptor* CreateAllocateBuiltinDescriptor(Zone* zone,
                                                        Isolate* isolate) {
  return TSCallDescriptor::Create(
      Linkage::GetStubCallDescriptor(
          zone, AllocateDescriptor{},
          AllocateDescriptor{}.GetStackParameterCount(),
          CallDescriptor::kCanUseRoots, Operator::kNoThrow,
          isolate != nullptr ? StubCallMode::kCallCodeObject
                             : StubCallMode::kCallBuiltinPointer),
      CanThrow::kNo, zone);
}

void MemoryAnalyzer::Run() {
  block_states[current_block] = BlockState{};
  BlockIndex end = BlockIndex(input_graph.block_count());
  while (current_block < end) {
    state = *block_states[current_block];
    auto operations_range =
        input_graph.operations(input_graph.Get(current_block));
    // Set the next block index here already, to allow it to be changed if
    // needed.
    current_block = BlockIndex(current_block.id() + 1);
    for (const Operation& op : operations_range) {
      Process(op);
    }
  }
}

void MemoryAnalyzer::Process(const Operation& op) {
  if (ShouldSkipOperation(op)) {
    return;
  }

  if (auto* alloc = op.TryCast<AllocateOp>()) {
    ProcessAllocation(*alloc);
    return;
  }
  if (auto* store = op.TryCast<StoreOp>()) {
    ProcessStore(*store);
    return;
  }
  if (op.Effects().can_allocate) {
    state = BlockState();
  }
  if (op.IsBlockTerminator()) {
    ProcessBlockTerminator(op);
  }
}

// Update the successor block states based on the state of the current block.
// For loop backedges, we need to re-start the analysis from the loop header
// unless the backedge state is unchanged.
void MemoryAnalyzer::ProcessBlockTerminator(const Operation& op) {
  if (auto* goto_op = op.TryCast<GotoOp>()) {
    if (input_graph.IsLoopBackedge(*goto_op)) {
      base::Optional<BlockState>& target_state =
          block_states[goto_op->destination->index()];
      BlockState old_state = *target_state;
      MergeCurrentStateIntoSuccessor(goto_op->destination);
      if (old_state != *target_state) {
        // We can never fold allocations inside of the loop into an
        // allocation before the loop, since this leads to unbounded
        // allocation size. An unknown `reserved_size` will prevent adding
        // allocations inside of the loop.
        target_state->reserved_size = base::nullopt;
        // Redo the analysis from the beginning of the loop.
        current_block = goto_op->destination->index();
      }
      return;
    } else if (goto_op->destination->IsLoop()) {
      // Look ahead to detect allocating loops earlier, avoiding a wrong
      // speculation resulting in processing the loop twice.
      for (const Operation& op :
           input_graph.operations(*goto_op->destination)) {
        if (op.Effects().can_allocate && !ShouldSkipOperation(op)) {
          state = BlockState();
          break;
        }
      }
    }
  }
  for (Block* successor : SuccessorBlocks(op)) {
    MergeCurrentStateIntoSuccessor(successor);
  }
}

// We try to merge the new allocation into a previous dominating allocation.
// We also allow folding allocations across blocks, as long as there is a
// dominating relationship.
void MemoryAnalyzer::ProcessAllocation(const AllocateOp& alloc) {
  if (ShouldSkipOptimizationStep()) return;
  base::Optional<uint64_t> new_size;
  if (auto* size =
          input_graph.Get(alloc.size()).template TryCast<ConstantOp>()) {
    new_size = size->integral();
  }
  // If the new allocation has a static size and is of the same type, then we
  // can fold it into the previous allocation unless the folded allocation would
  // exceed `kMaxRegularHeapObjectSize`.
  if (allocation_folding == AllocationFolding::kDoAllocationFolding &&
      state.last_allocation && new_size.has_value() &&
      state.reserved_size.has_value() &&
      alloc.type == state.last_allocation->type &&
      *new_size <= kMaxRegularHeapObjectSize - *state.reserved_size) {
    state.reserved_size =
        static_cast<uint32_t>(*state.reserved_size + *new_size);
    folded_into[&alloc] = state.last_allocation;
    uint32_t& max_reserved_size = reserved_size[state.last_allocation];
    max_reserved_size = std::max(max_reserved_size, *state.reserved_size);
    return;
  }
  state.last_allocation = &alloc;
  state.reserved_size = base::nullopt;
  if (new_size.has_value() && *new_size <= kMaxRegularHeapObjectSize) {
    state.reserved_size = static_cast<uint32_t>(*new_size);
  }
  // We might be re-visiting the current block. In this case, we need to remove
  // an allocation that can no longer be folded.
  reserved_size.erase(&alloc);
  folded_into.erase(&alloc);
}

void MemoryAnalyzer::ProcessStore(const StoreOp& store) {
  OpIndex store_op_index = input_graph.Index(store);
  if (SkipWriteBarrier(store)) {
    skipped_write_barriers.insert(store_op_index);
  } else {
    // We might be re-visiting the current block. In this case, we need to
    // still update the information.
    DCHECK_NE(store.write_barrier, WriteBarrierKind::kAssertNoWriteBarrier);
    skipped_write_barriers.erase(store_op_index);
  }
}

void MemoryAnalyzer::MergeCurrentStateIntoSuccessor(const Block* successor) {
  base::Optional<BlockState>& target_state = block_states[successor->index()];
  if (!target_state.has_value()) {
    target_state = state;
    return;
  }
  // All predecessors need to have the same last allocation for us to continue
  // folding into it.
  if (target_state->last_allocation != state.last_allocation) {
    target_state = BlockState();
    return;
  }
  // We take the maximum allocation size of all predecessors. If the size is
  // unknown because it is dynamic, we remember the allocation to eliminate
  // write barriers.
  if (target_state->reserved_size.has_value() &&
      state.reserved_size.has_value()) {
    target_state->reserved_size =
        std::max(*target_state->reserved_size, *state.reserved_size);
  } else {
    target_state->reserved_size = base::nullopt;
  }
}

}  // namespace v8::internal::compiler::turboshaft
