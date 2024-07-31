// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/interpreter/control-flow-builders.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace interpreter {


BreakableControlFlowBuilder::~BreakableControlFlowBuilder() {
  BindBreakTarget();
  DCHECK(break_labels_.empty() || break_labels_.is_bound());
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(
        node_, SourceRangeKind::kContinuation);
  }
}

void BreakableControlFlowBuilder::BindBreakTarget() {
  break_labels_.Bind(builder());
}

void BreakableControlFlowBuilder::EmitJump(BytecodeLabels* sites) {
  builder()->Jump(sites->New());
}

void BreakableControlFlowBuilder::EmitJumpIfTrue(
    BytecodeArrayBuilder::ToBooleanMode mode, BytecodeLabels* sites) {
  builder()->JumpIfTrue(mode, sites->New());
}

void BreakableControlFlowBuilder::EmitJumpIfFalse(
    BytecodeArrayBuilder::ToBooleanMode mode, BytecodeLabels* sites) {
  builder()->JumpIfFalse(mode, sites->New());
}

void BreakableControlFlowBuilder::EmitJumpIfUndefined(BytecodeLabels* sites) {
  builder()->JumpIfUndefined(sites->New());
}

void BreakableControlFlowBuilder::EmitJumpIfNull(BytecodeLabels* sites) {
  builder()->JumpIfNull(sites->New());
}

LoopBuilder::~LoopBuilder() {
  DCHECK(continue_labels_.empty() || continue_labels_.is_bound());
  DCHECK(end_labels_.empty() || end_labels_.is_bound());
}

void LoopBuilder::LoopHeader() {
  // Jumps from before the loop header into the loop violate ordering
  // requirements of bytecode basic blocks. The only entry into a loop
  // must be the loop header. Surely breaks is okay? Not if nested
  // and misplaced between the headers.
  DCHECK(break_labels_.empty() && continue_labels_.empty() &&
         end_labels_.empty());
  builder()->Bind(&loop_header_);
}

void LoopBuilder::LoopBody() {
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(block_coverage_body_slot_);
  }
}

void LoopBuilder::JumpToHeader(int loop_depth, LoopBuilder* const parent_loop) {
  BindLoopEnd();
  if (parent_loop &&
      loop_header_.offset() == parent_loop->loop_header_.offset()) {
    // TurboFan can't cope with multiple loops that have the same loop header
    // bytecode offset. If we have an inner loop with the same header offset
    // than its parent loop, we do not create a JumpLoop bytecode. Instead, we
    // Jump to our parent's JumpToHeader which in turn can be a JumpLoop or, iff
    // they are a nested inner loop too, a Jump to its parent's JumpToHeader.
    parent_loop->JumpToLoopEnd();
  } else {
    // Pass the proper loop depth to the backwards branch for triggering OSR.
    // For purposes of OSR, the loop depth is capped at `kMaxOsrUrgency - 1`.
    // Once that urgency is reached, all loops become OSR candidates.
    //
    // The loop must have closed form, i.e. all loop elements are within the
    // loop, the loop header precedes the body and next elements in the loop.
    int slot_index = feedback_vector_spec_->AddJumpLoopSlot().ToInt();
    builder()->JumpLoop(
        &loop_header_, std::min(loop_depth, FeedbackVector::kMaxOsrUrgency - 1),
        source_position_, slot_index);
  }
}

void LoopBuilder::BindContinueTarget() { continue_labels_.Bind(builder()); }

void LoopBuilder::BindLoopEnd() { end_labels_.Bind(builder()); }

SwitchBuilder::~SwitchBuilder() {
#ifdef DEBUG
  for (auto site : case_sites_) {
    DCHECK(!site.has_referrer_jump() || site.is_bound());
  }
#endif
}

void SwitchBuilder::BindCaseTargetForJumpTable(int case_value,
                                               CaseClause* clause) {
  builder()->Bind(jump_table_, case_value);
  BuildBlockCoverage(clause);
}

void SwitchBuilder::BindCaseTargetForCompareJump(int index,
                                                 CaseClause* clause) {
  builder()->Bind(&case_sites_.at(index));
  BuildBlockCoverage(clause);
}

void SwitchBuilder::JumpToCaseIfTrue(BytecodeArrayBuilder::ToBooleanMode mode,
                                     int index) {
  builder()->JumpIfTrue(mode, &case_sites_.at(index));
}

// Precondition: tag is in the accumulator
void SwitchBuilder::EmitJumpTableIfExists(
    int min_case, int max_case, std::map<int, CaseClause*>& covered_cases) {
  builder()->SwitchOnSmiNoFeedback(jump_table_);
  fall_through_.Bind(builder());
  for (int j = min_case; j <= max_case; ++j) {
    if (covered_cases.find(j) == covered_cases.end()) {
      this->BindCaseTargetForJumpTable(j, nullptr);
    }
  }
}

void SwitchBuilder::BindDefault(CaseClause* clause) {
  default_.Bind(builder());
  BuildBlockCoverage(clause);
}

void SwitchBuilder::JumpToDefault() { this->EmitJump(&default_); }

void SwitchBuilder::JumpToFallThroughIfFalse() {
  this->EmitJumpIfFalse(BytecodeArrayBuilder::ToBooleanMode::kAlreadyBoolean,
                        &fall_through_);
}

TryCatchBuilder::~TryCatchBuilder() {
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(
        statement_, SourceRangeKind::kContinuation);
  }
}

void TryCatchBuilder::BeginTry(Register context) {
  builder()->MarkTryBegin(handler_id_, context);
}


void TryCatchBuilder::EndTry() {
  builder()->MarkTryEnd(handler_id_);
  builder()->Jump(&exit_);
  builder()->MarkHandler(handler_id_, catch_prediction_);

  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(statement_,
                                                   SourceRangeKind::kCatch);
  }
}

void TryCatchBuilder::EndCatch() { builder()->Bind(&exit_); }

TryFinallyBuilder::~TryFinallyBuilder() {
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(
        statement_, SourceRangeKind::kContinuation);
  }
}

void TryFinallyBuilder::BeginTry(Register context) {
  builder()->MarkTryBegin(handler_id_, context);
}


void TryFinallyBuilder::LeaveTry() {
  builder()->Jump(finalization_sites_.New());
}


void TryFinallyBuilder::EndTry() {
  builder()->MarkTryEnd(handler_id_);
}


void TryFinallyBuilder::BeginHandler() {
  builder()->Bind(&handler_);
  builder()->MarkHandler(handler_id_, catch_prediction_);
}

void TryFinallyBuilder::BeginFinally() {
  finalization_sites_.Bind(builder());

  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(statement_,
                                                   SourceRangeKind::kFinally);
  }
}

void TryFinallyBuilder::EndFinally() {
  // Nothing to be done here.
}

ConditionalChainControlFlowBuilder::~ConditionalChainControlFlowBuilder() {
  end_labels_.Bind(builder());
#ifdef DEBUG
  DCHECK(end_labels_.empty() || end_labels_.is_bound());

  for (auto* label : then_labels_list_) {
    DCHECK(label->empty() || label->is_bound());
  }

  for (auto* label : else_labels_list_) {
    DCHECK(label->empty() || label->is_bound());
  }
#endif
}

void ConditionalChainControlFlowBuilder::JumpToEnd() {
  builder()->Jump(end_labels_.New());
}

void ConditionalChainControlFlowBuilder::ThenAt(size_t index) {
  DCHECK_LT(index, then_labels_list_.length());
  then_labels_at(index)->Bind(builder());
  if (block_coverage_builder_) {
    block_coverage_builder_->IncrementBlockCounter(
        block_coverage_then_slot_at(index));
  }
}

void ConditionalChainControlFlowBuilder::ElseAt(size_t index) {
  DCHECK_LT(index, else_labels_list_.length());
  else_labels_at(index)->Bind(builder());
  if (block_coverage_builder_) {
    block_coverage_builder_->IncrementBlockCounter(
        block_coverage_else_slot_at(index));
  }
}

ConditionalControlFlowBuilder::~ConditionalControlFlowBuilder() {
  if (!else_labels_.is_bound()) else_labels_.Bind(builder());
  end_labels_.Bind(builder());

  DCHECK(end_labels_.empty() || end_labels_.is_bound());
  DCHECK(then_labels_.empty() || then_labels_.is_bound());
  DCHECK(else_labels_.empty() || else_labels_.is_bound());

  // IfStatement requires a continuation counter, Conditional does not (as it
  // can only contain expressions).
  if (block_coverage_builder_ != nullptr && node_->IsIfStatement()) {
    block_coverage_builder_->IncrementBlockCounter(
        node_, SourceRangeKind::kContinuation);
  }
}

void ConditionalControlFlowBuilder::JumpToEnd() {
  DCHECK(end_labels_.empty());  // May only be called once.
  builder()->Jump(end_labels_.New());
}

void ConditionalControlFlowBuilder::Then() {
  then_labels()->Bind(builder());
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(block_coverage_then_slot_);
  }
}

void ConditionalControlFlowBuilder::Else() {
  else_labels()->Bind(builder());
  if (block_coverage_builder_ != nullptr) {
    block_coverage_builder_->IncrementBlockCounter(block_coverage_else_slot_);
  }
}

}  // namespace interpreter
}  // namespace internal
}  // namespace v8
