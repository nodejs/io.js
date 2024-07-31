// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_SPILL_PLACER_H_
#define V8_COMPILER_BACKEND_SPILL_PLACER_H_

#include "src/compiler/backend/instruction.h"

namespace v8 {
namespace internal {

namespace compiler {

class LiveRangeFinder;
class TopLevelLiveRange;
class RegisterAllocationData;

// SpillPlacer is an implementation of an algorithm to find optimal spill
// insertion positions, where optimal is defined as:
//
// 1. Spills needed by deferred code don't affect non-deferred code.
// 2. No control-flow path spills the same value more than once in non-deferred
//    blocks.
// 3. Where possible based on #2, control-flow paths through non-deferred code
//    that don't need the value to be on the stack don't execute any spills.
// 4. The fewest number of spill instructions is written to meet these rules.
// 5. Spill instructions are placed as early as possible.
//
// These rules are an attempt to make code paths that don't need to spill faster
// while not increasing code size too much.
//
// Considering just one value at a time for now, the steps are:
//
// 1. If the value is defined in a deferred block, or needs its value to be on
//    the stack during the definition block, emit a move right after the
//    definition and exit.
// 2. Build an array representing the state at each block, where the state can
//    be any of the following:
//    - unmarked (default/initial state)
//    - definition
//    - spill required
//    - spill required in non-deferred successor
//    - spill required in deferred successor
// 3. Mark the block containing the definition.
// 4. Mark as "spill required" all blocks that contain any part of a spilled
//    LiveRange, or any use that requires the value to be on the stack.
// 5. Walk the block list backward, setting the "spill required in successor"
//    values where appropriate. If both deferred and non-deferred successors
//    require a spill, then the result should be "spill required in non-deferred
//    successor".
// 6. Walk the block list forward, updating marked blocks to "spill required" if
//    all of their predecessors agree that a spill is required. Furthermore, if
//    a block is marked as "spill required in non-deferred successor" and any
//    non-deferred predecessor is marked as "spill required", then the current
//    block is updated to "spill required". We must mark these merge points as
//    "spill required" to obey rule #2 above: if we didn't, then there would
//    exist a control-flow path through two different spilled regions.
// 7. Walk the block list backward again, updating blocks to "spill required" if
//    all of their successors agree that a spill is required, or if the current
//    block is deferred and any of its successors require spills. If only some
//    successors of a non-deferred block require spills, then insert spill moves
//    at the beginning of those successors. If we manage to smear the "spill
//    required" value all the way to the definition block, then insert a spill
//    move at the definition instead. (Spilling at the definition implies that
//    we didn't emit any other spill moves, and there is a DCHECK mechanism to
//    ensure that invariant.)
//
// Loop back-edges can be safely ignored in every step. Anything that the loop
// header needs on-stack will be spilled either in the loop header itself or
// sometime before entering the loop, so its back-edge predecessors don't need
// to contain any data about the loop header.
//
// The operations described in those steps are simple Boolean logic, so we can
// easily process a batch of values at the same time as an optimization.
class SpillPlacer {
 public:
  SpillPlacer(RegisterAllocationData* data, Zone* zone);

  ~SpillPlacer();

  SpillPlacer(const SpillPlacer&) = delete;
  SpillPlacer& operator=(const SpillPlacer&) = delete;

  // Adds the given TopLevelLiveRange to the SpillPlacer's state. Will
  // eventually commit spill moves for that range and mark the range to indicate
  // whether its value is spilled at the definition or some later point, so that
  // subsequent phases can know whether to assume the value is always on-stack.
  // However, those steps may happen during a later call to Add or during the
  // destructor.
  void Add(TopLevelLiveRange* range);

 private:
  RegisterAllocationData* data() const { return data_; }

  // While initializing data for a range, returns the index within each Entry
  // where data about that range should be stored. May cause data about previous
  // ranges to be committed to make room if the table is full.
  int GetOrCreateIndexForLatestVreg(int vreg);

  bool IsLatestVreg(int vreg) const {
    return assigned_indices_ > 0 &&
           vreg_numbers_[assigned_indices_ - 1] == vreg;
  }

  // Processes all of the ranges which have been added, inserts spill moves for
  // them to the instruction sequence, and marks the ranges with whether they
  // are spilled at the definition or later.
  void CommitSpills();

  void ClearData();

  // Updates the iteration bounds first_block_ and last_block_ so that they
  // include the new value.
  void ExpandBoundsToInclude(RpoNumber block);

  void SetSpillRequired(InstructionBlock* block, int vreg,
                        RpoNumber top_start_block);

  void SetDefinition(RpoNumber block, int vreg);

  // The first backward pass is responsible for marking blocks which do not
  // themselves need the value to be on the stack, but which do have successors
  // requiring the value to be on the stack.
  void FirstBackwardPass();

  // The forward pass is responsible for selecting merge points that should
  // require the value to be on the stack.
  void ForwardPass();

  // The second backward pass is responsible for propagating the spill
  // requirements to the earliest block where all successors can agree a spill
  // is required. It also emits the actual spill instructions.
  void SecondBackwardPass();

  void CommitSpill(int vreg, InstructionBlock* predecessor,
                   InstructionBlock* successor);

  // Each Entry represents the state for 64 values at a block, so that we can
  // compute a batch of values in parallel.
  class Entry;
  static constexpr int kValueIndicesPerEntry = 64;

  // Objects provided to the constructor, which all outlive this SpillPlacer.
  RegisterAllocationData* data_;
  Zone* zone_;

  // An array of one Entry per block, where blocks are in reverse post-order.
  Entry* entries_ = nullptr;

  // An array representing which TopLevelLiveRange is in each bit.
  int* vreg_numbers_ = nullptr;

  // The number of vreg_numbers_ that have been assigned.
  int assigned_indices_ = 0;

  // The first and last block that have any definitions or uses in the current
  // batch of values. In large functions, tracking these bounds can help prevent
  // additional work.
  RpoNumber first_block_ = RpoNumber::Invalid();
  RpoNumber last_block_ = RpoNumber::Invalid();
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_SPILL_PLACER_H_
