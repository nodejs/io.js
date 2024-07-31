// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/frame-elider.h"

#include "src/base/iterator.h"

namespace v8 {
namespace internal {
namespace compiler {

FrameElider::FrameElider(InstructionSequence* code, bool has_dummy_end_block)
    : code_(code), has_dummy_end_block_(has_dummy_end_block) {}

void FrameElider::Run() {
  MarkBlocks();
  PropagateMarks();
  MarkDeConstruction();
}

void FrameElider::MarkBlocks() {
  for (InstructionBlock* block : instruction_blocks()) {
    if (block->needs_frame()) continue;
    for (int i = block->code_start(); i < block->code_end(); ++i) {
      const Instruction* instr = InstructionAt(i);
      if (instr->IsCall() || instr->IsDeoptimizeCall() ||
          instr->arch_opcode() == ArchOpcode::kArchStackPointerGreaterThan ||
          instr->arch_opcode() == ArchOpcode::kArchFramePointer) {
        block->mark_needs_frame();
        break;
      }
      if (instr->arch_opcode() == ArchOpcode::kArchStackSlot &&
          instr->InputAt(0)->IsImmediate() &&
          code_->GetImmediate(ImmediateOperand::cast(instr->InputAt(0)))
                  .ToInt32() > 0) {
        // We shouldn't allow accesses to the stack below the current stack
        // pointer (indicated by positive slot indices).
        // This is in particular because signal handlers (which could, of
        // course, be triggered at any point in time) will overwrite this
        // memory.
        block->mark_needs_frame();
        break;
      }
    }
  }
}

void FrameElider::PropagateMarks() {
  while (PropagateInOrder() || PropagateReversed()) {
  }
}

void FrameElider::MarkDeConstruction() {
  for (InstructionBlock* block : instruction_blocks()) {
    if (block->needs_frame()) {
      // Special case: The start block needs a frame.
      if (block->predecessors().empty()) {
        block->mark_must_construct_frame();
        if (block->SuccessorCount() == 0) {
          // We only have a single block, so the block also needs to be marked
          // to deconstruct the frame.
          const Instruction* last =
              InstructionAt(block->last_instruction_index());
          // The only cases when we need to deconstruct are ret and jump.
          if (last->IsRet() || last->IsJump()) {
            block->mark_must_deconstruct_frame();
          }
        }
      }
      // Find "frame -> no frame" transitions, inserting frame
      // deconstructions.
      for (RpoNumber& succ : block->successors()) {
        if (!InstructionBlockAt(succ)->needs_frame()) {
          DCHECK_EQ(1U, block->SuccessorCount());
          const Instruction* last =
              InstructionAt(block->last_instruction_index());
          if (last->IsThrow() || last->IsTailCall() ||
              last->IsDeoptimizeCall()) {
            // We need to keep the frame if we exit the block through any
            // of these.
            continue;
          }
          // The only cases when we need to deconstruct are ret and jump.
          DCHECK(last->IsRet() || last->IsJump());
          block->mark_must_deconstruct_frame();
        }
      }
    } else {
      // Find "no frame -> frame" transitions, inserting frame constructions.
      for (RpoNumber& succ : block->successors()) {
        if (InstructionBlockAt(succ)->needs_frame()) {
          DCHECK_NE(1U, block->SuccessorCount());
          InstructionBlockAt(succ)->mark_must_construct_frame();
        }
      }
    }
  }
}

bool FrameElider::PropagateInOrder() {
  bool changed = false;
  for (InstructionBlock* block : instruction_blocks()) {
    changed |= PropagateIntoBlock(block);
  }
  return changed;
}

bool FrameElider::PropagateReversed() {
  bool changed = false;
  for (InstructionBlock* block : base::Reversed(instruction_blocks())) {
    changed |= PropagateIntoBlock(block);
  }
  return changed;
}

bool FrameElider::PropagateIntoBlock(InstructionBlock* block) {
  // Already marked, nothing to do...
  if (block->needs_frame()) return false;

  // Turbofan does have an empty dummy end block, which we need to ignore here.
  // However, Turboshaft does not have such a block.
  if (has_dummy_end_block_) {
    // Never mark the dummy end node, otherwise we might incorrectly decide to
    // put frame deconstruction code there later,
    if (block->successors().empty()) return false;
  }

  // Propagate towards the end ("downwards") if there is a predecessor needing
  // a frame, but don't "bleed" from deferred code to non-deferred code.
  for (RpoNumber& pred : block->predecessors()) {
    if (InstructionBlockAt(pred)->needs_frame() &&
        (!InstructionBlockAt(pred)->IsDeferred() || block->IsDeferred())) {
      block->mark_needs_frame();
      return true;
    }
  }

  // Propagate towards start ("upwards")
  bool need_frame_successors = false;
  if (block->SuccessorCount() == 1) {
    // For single successors, propagate the needs_frame information.
    need_frame_successors =
        InstructionBlockAt(block->successors()[0])->needs_frame();
  } else {
    // For multiple successors, each successor must only have a single
    // predecessor (because the graph is in edge-split form), so each successor
    // can independently create/dismantle a frame if needed. Given this
    // independent control, only propagate needs_frame if all non-deferred
    // blocks need a frame.
    for (RpoNumber& succ : block->successors()) {
      InstructionBlock* successor_block = InstructionBlockAt(succ);
      DCHECK_EQ(1, successor_block->PredecessorCount());
      if (!successor_block->IsDeferred()) {
        if (successor_block->needs_frame()) {
          need_frame_successors = true;
        } else {
          return false;
        }
      }
    }
  }
  if (need_frame_successors) {
    block->mark_needs_frame();
    return true;
  } else {
    return false;
  }
}

const InstructionBlocks& FrameElider::instruction_blocks() const {
  return code_->instruction_blocks();
}

InstructionBlock* FrameElider::InstructionBlockAt(RpoNumber rpo_number) const {
  return code_->InstructionBlockAt(rpo_number);
}

Instruction* FrameElider::InstructionAt(int index) const {
  return code_->InstructionAt(index);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
