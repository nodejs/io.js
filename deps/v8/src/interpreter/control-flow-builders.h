// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_
#define V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_

#include <map>

#include "src/ast/ast-source-ranges.h"
#include "src/interpreter/block-coverage-builder.h"
#include "src/interpreter/bytecode-array-builder.h"
#include "src/interpreter/bytecode-generator.h"
#include "src/interpreter/bytecode-jump-table.h"
#include "src/interpreter/bytecode-label.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace interpreter {

class V8_EXPORT_PRIVATE ControlFlowBuilder {
 public:
  explicit ControlFlowBuilder(BytecodeArrayBuilder* builder)
      : builder_(builder) {}
  ControlFlowBuilder(const ControlFlowBuilder&) = delete;
  ControlFlowBuilder& operator=(const ControlFlowBuilder&) = delete;
  virtual ~ControlFlowBuilder() = default;

 protected:
  BytecodeArrayBuilder* builder() const { return builder_; }

 private:
  BytecodeArrayBuilder* builder_;
};

class V8_EXPORT_PRIVATE BreakableControlFlowBuilder
    : public ControlFlowBuilder {
 public:
  BreakableControlFlowBuilder(BytecodeArrayBuilder* builder,
                              BlockCoverageBuilder* block_coverage_builder,
                              AstNode* node)
      : ControlFlowBuilder(builder),
        break_labels_(builder->zone()),
        node_(node),
        block_coverage_builder_(block_coverage_builder) {}
  ~BreakableControlFlowBuilder() override;

  // This method is called when visiting break statements in the AST.
  // Inserts a jump to an unbound label that is patched when the corresponding
  // BindBreakTarget is called.
  void Break() { EmitJump(&break_labels_); }
  void BreakIfTrue(BytecodeArrayBuilder::ToBooleanMode mode) {
    EmitJumpIfTrue(mode, &break_labels_);
  }
  void BreakIfFalse(BytecodeArrayBuilder::ToBooleanMode mode) {
    EmitJumpIfFalse(mode, &break_labels_);
  }
  void BreakIfUndefined() { EmitJumpIfUndefined(&break_labels_); }
  void BreakIfNull() { EmitJumpIfNull(&break_labels_); }

  BytecodeLabels* break_labels() { return &break_labels_; }

 protected:
  void EmitJump(BytecodeLabels* labels);
  void EmitJumpIfTrue(BytecodeArrayBuilder::ToBooleanMode mode,
                      BytecodeLabels* labels);
  void EmitJumpIfFalse(BytecodeArrayBuilder::ToBooleanMode mode,
                       BytecodeLabels* labels);
  void EmitJumpIfUndefined(BytecodeLabels* labels);
  void EmitJumpIfNull(BytecodeLabels* labels);

  // Called from the destructor to update sites that emit jumps for break.
  void BindBreakTarget();

  // Unbound labels that identify jumps for break statements in the code.
  BytecodeLabels break_labels_;

  // A continuation counter (for block coverage) is needed e.g. when
  // encountering a break statement.
  AstNode* node_;
  BlockCoverageBuilder* block_coverage_builder_;
};

// Class to track control flow for block statements (which can break in JS).
class V8_EXPORT_PRIVATE BlockBuilder final
    : public BreakableControlFlowBuilder {
 public:
  BlockBuilder(BytecodeArrayBuilder* builder,
               BlockCoverageBuilder* block_coverage_builder,
               BreakableStatement* statement)
      : BreakableControlFlowBuilder(builder, block_coverage_builder,
                                    statement) {}
};

// A class to help with co-ordinating break and continue statements with
// their loop.
class V8_EXPORT_PRIVATE LoopBuilder final : public BreakableControlFlowBuilder {
 public:
  LoopBuilder(BytecodeArrayBuilder* builder,
              BlockCoverageBuilder* block_coverage_builder, AstNode* node,
              FeedbackVectorSpec* feedback_vector_spec)
      : BreakableControlFlowBuilder(builder, block_coverage_builder, node),
        continue_labels_(builder->zone()),
        end_labels_(builder->zone()),
        feedback_vector_spec_(feedback_vector_spec) {
    if (block_coverage_builder_ != nullptr) {
      block_coverage_body_slot_ =
          block_coverage_builder_->AllocateBlockCoverageSlot(
              node, SourceRangeKind::kBody);
    }
    source_position_ = node ? node->position() : kNoSourcePosition;
  }
  ~LoopBuilder() override;

  void LoopHeader();
  void LoopBody();
  void JumpToHeader(int loop_depth, LoopBuilder* const parent_loop);
  void BindContinueTarget();

  // This method is called when visiting continue statements in the AST.
  // Inserts a jump to an unbound label that is patched when BindContinueTarget
  // is called.
  void Continue() { EmitJump(&continue_labels_); }
  void ContinueIfUndefined() { EmitJumpIfUndefined(&continue_labels_); }
  void ContinueIfNull() { EmitJumpIfNull(&continue_labels_); }

 private:
  // Emit a Jump to our parent_loop_'s end label which could be a JumpLoop or,
  // iff they are a nested inner loop with the same loop header bytecode offset
  // as their parent's, a Jump to its parent's end label.
  void JumpToLoopEnd() { EmitJump(&end_labels_); }
  void BindLoopEnd();

  BytecodeLoopHeader loop_header_;

  // Unbound labels that identify jumps for continue statements in the code and
  // jumps from checking the loop condition to the header for do-while loops.
  BytecodeLabels continue_labels_;

  // Unbound labels that identify jumps for nested inner loops which share the
  // same header offset as this loop. Said inner loops will Jump to our end
  // label, which could be a JumpLoop or, iff we are a nested inner loop too, a
  // Jump to our parent's end label.
  BytecodeLabels end_labels_;

  int block_coverage_body_slot_;
  int source_position_;
  FeedbackVectorSpec* const feedback_vector_spec_;
};

// A class to help with co-ordinating break statements with their switch.
class V8_EXPORT_PRIVATE SwitchBuilder final
    : public BreakableControlFlowBuilder {
 public:
  SwitchBuilder(BytecodeArrayBuilder* builder,
                BlockCoverageBuilder* block_coverage_builder,
                SwitchStatement* statement, int number_of_cases,
                BytecodeJumpTable* jump_table)
      : BreakableControlFlowBuilder(builder, block_coverage_builder, statement),
        case_sites_(builder->zone()),
        default_(builder->zone()),
        fall_through_(builder->zone()),
        jump_table_(jump_table) {
    case_sites_.resize(number_of_cases);
  }

  ~SwitchBuilder() override;

  void BindCaseTargetForJumpTable(int case_value, CaseClause* clause);

  void BindCaseTargetForCompareJump(int index, CaseClause* clause);

  // This method is called when visiting case comparison operation for |index|.
  // Inserts a JumpIfTrue with ToBooleanMode |mode| to a unbound label that is
  // patched when the corresponding SetCaseTarget is called.
  void JumpToCaseIfTrue(BytecodeArrayBuilder::ToBooleanMode mode, int index);

  void EmitJumpTableIfExists(int min_case, int max_case,
                             std::map<int, CaseClause*>& covered_cases);

  void BindDefault(CaseClause* clause);

  void JumpToDefault();

  void JumpToFallThroughIfFalse();

 private:
  // Unbound labels that identify jumps for case statements in the code.
  ZoneVector<BytecodeLabel> case_sites_;
  BytecodeLabels default_;
  BytecodeLabels fall_through_;
  BytecodeJumpTable* jump_table_;

  void BuildBlockCoverage(CaseClause* clause) {
    if (block_coverage_builder_ && clause != nullptr) {
      block_coverage_builder_->IncrementBlockCounter(clause,
                                                     SourceRangeKind::kBody);
    }
  }
};

// A class to help with co-ordinating control flow in try-catch statements.
class V8_EXPORT_PRIVATE TryCatchBuilder final : public ControlFlowBuilder {
 public:
  TryCatchBuilder(BytecodeArrayBuilder* builder,
                  BlockCoverageBuilder* block_coverage_builder,
                  TryCatchStatement* statement,
                  HandlerTable::CatchPrediction catch_prediction)
      : ControlFlowBuilder(builder),
        handler_id_(builder->NewHandlerEntry()),
        catch_prediction_(catch_prediction),
        block_coverage_builder_(block_coverage_builder),
        statement_(statement) {}

  ~TryCatchBuilder() override;

  void BeginTry(Register context);
  void EndTry();
  void EndCatch();

 private:
  int handler_id_;
  HandlerTable::CatchPrediction catch_prediction_;
  BytecodeLabel exit_;

  BlockCoverageBuilder* block_coverage_builder_;
  TryCatchStatement* statement_;
};

// A class to help with co-ordinating control flow in try-finally statements.
class V8_EXPORT_PRIVATE TryFinallyBuilder final : public ControlFlowBuilder {
 public:
  TryFinallyBuilder(BytecodeArrayBuilder* builder,
                    BlockCoverageBuilder* block_coverage_builder,
                    TryFinallyStatement* statement,
                    HandlerTable::CatchPrediction catch_prediction)
      : ControlFlowBuilder(builder),
        handler_id_(builder->NewHandlerEntry()),
        catch_prediction_(catch_prediction),
        finalization_sites_(builder->zone()),
        block_coverage_builder_(block_coverage_builder),
        statement_(statement) {}

  ~TryFinallyBuilder() override;

  void BeginTry(Register context);
  void LeaveTry();
  void EndTry();
  void BeginHandler();
  void BeginFinally();
  void EndFinally();

 private:
  int handler_id_;
  HandlerTable::CatchPrediction catch_prediction_;
  BytecodeLabel handler_;

  // Unbound labels that identify jumps to the finally block in the code.
  BytecodeLabels finalization_sites_;

  BlockCoverageBuilder* block_coverage_builder_;
  TryFinallyStatement* statement_;
};

class V8_EXPORT_PRIVATE ConditionalChainControlFlowBuilder final
    : public ControlFlowBuilder {
 public:
  ConditionalChainControlFlowBuilder(
      BytecodeArrayBuilder* builder,
      BlockCoverageBuilder* block_coverage_builder, AstNode* node,
      size_t then_count)
      : ControlFlowBuilder(builder),
        end_labels_(builder->zone()),
        then_count_(then_count),
        then_labels_list_(static_cast<int>(then_count_), builder->zone()),
        else_labels_list_(static_cast<int>(then_count_), builder->zone()),
        block_coverage_then_slots_(then_count_, builder->zone()),
        block_coverage_else_slots_(then_count_, builder->zone()),
        block_coverage_builder_(block_coverage_builder) {
    DCHECK(node->IsConditionalChain());

    Zone* zone = builder->zone();
    for (size_t i = 0; i < then_count_; ++i) {
      then_labels_list_.Add(zone->New<BytecodeLabels>(zone), zone);
      else_labels_list_.Add(zone->New<BytecodeLabels>(zone), zone);
    }

    if (block_coverage_builder != nullptr) {
      ConditionalChain* conditional_chain = node->AsConditionalChain();
      block_coverage_then_slots_.resize(then_count_);
      block_coverage_else_slots_.resize(then_count_);
      for (size_t i = 0; i < then_count_; ++i) {
        block_coverage_then_slots_[i] =
            block_coverage_builder->AllocateConditionalChainBlockCoverageSlot(
                conditional_chain, SourceRangeKind::kThen, i);
        block_coverage_else_slots_[i] =
            block_coverage_builder->AllocateConditionalChainBlockCoverageSlot(
                conditional_chain, SourceRangeKind::kElse, i);
      }
    }
  }
  ~ConditionalChainControlFlowBuilder() override;

  BytecodeLabels* then_labels_at(size_t index) {
    DCHECK_LT(index, then_count_);
    return then_labels_list_[static_cast<int>(index)];
  }

  BytecodeLabels* else_labels_at(size_t index) {
    DCHECK_LT(index, then_count_);
    return else_labels_list_[static_cast<int>(index)];
  }

  int block_coverage_then_slot_at(size_t index) const {
    DCHECK_LT(index, then_count_);
    return block_coverage_then_slots_[index];
  }

  int block_coverage_else_slot_at(size_t index) const {
    DCHECK_LT(index, then_count_);
    return block_coverage_else_slots_[index];
  }

  void ThenAt(size_t index);
  void ElseAt(size_t index);

  void JumpToEnd();

 private:
  BytecodeLabels end_labels_;
  size_t then_count_;
  ZonePtrList<BytecodeLabels> then_labels_list_;
  ZonePtrList<BytecodeLabels> else_labels_list_;

  ZoneVector<int> block_coverage_then_slots_;
  ZoneVector<int> block_coverage_else_slots_;
  BlockCoverageBuilder* block_coverage_builder_;
};

class V8_EXPORT_PRIVATE ConditionalControlFlowBuilder final
    : public ControlFlowBuilder {
 public:
  ConditionalControlFlowBuilder(BytecodeArrayBuilder* builder,
                                BlockCoverageBuilder* block_coverage_builder,
                                AstNode* node)
      : ControlFlowBuilder(builder),
        end_labels_(builder->zone()),
        then_labels_(builder->zone()),
        else_labels_(builder->zone()),
        node_(node),
        block_coverage_builder_(block_coverage_builder) {
    DCHECK(node->IsIfStatement() || node->IsConditional());
    if (block_coverage_builder != nullptr) {
      block_coverage_then_slot_ =
          block_coverage_builder->AllocateBlockCoverageSlot(
              node, SourceRangeKind::kThen);
      block_coverage_else_slot_ =
          block_coverage_builder->AllocateBlockCoverageSlot(
              node, SourceRangeKind::kElse);
    }
  }
  ~ConditionalControlFlowBuilder() override;

  BytecodeLabels* then_labels() { return &then_labels_; }
  BytecodeLabels* else_labels() { return &else_labels_; }

  void Then();
  void Else();

  void JumpToEnd();

 private:
  BytecodeLabels end_labels_;
  BytecodeLabels then_labels_;
  BytecodeLabels else_labels_;

  AstNode* node_;
  int block_coverage_then_slot_;
  int block_coverage_else_slot_;
  BlockCoverageBuilder* block_coverage_builder_;
};

}  // namespace interpreter
}  // namespace internal
}  // namespace v8

#endif  // V8_INTERPRETER_CONTROL_FLOW_BUILDERS_H_
