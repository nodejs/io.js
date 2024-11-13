// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_TORQUE_CFG_H_
#define V8_TORQUE_CFG_H_

#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "src/torque/ast.h"
#include "src/torque/instructions.h"
#include "src/torque/source-positions.h"
#include "src/torque/types.h"

namespace v8::internal::torque {

class ControlFlowGraph;

class Block {
 public:
  explicit Block(ControlFlowGraph* cfg, size_t id,
                 std::optional<Stack<const Type*>> input_types,
                 bool is_deferred)
      : cfg_(cfg),
        input_types_(std::move(input_types)),
        id_(id),
        is_deferred_(is_deferred) {}
  void Add(Instruction instruction) {
    DCHECK(!IsComplete());
    instructions_.push_back(std::move(instruction));
  }

  bool HasInputTypes() const { return input_types_ != std::nullopt; }
  const Stack<const Type*>& InputTypes() const { return *input_types_; }
  void SetInputTypes(const Stack<const Type*>& input_types);
  void Retype() {
    Stack<const Type*> current_stack = InputTypes();
    for (const Instruction& instruction : instructions()) {
      instruction.TypeInstruction(&current_stack, cfg_);
    }
  }

  std::vector<Instruction>& instructions() { return instructions_; }
  const std::vector<Instruction>& instructions() const { return instructions_; }
  bool IsComplete() const {
    return !instructions_.empty() && instructions_.back()->IsBlockTerminator();
  }
  size_t id() const { return id_; }
  bool IsDeferred() const { return is_deferred_; }

  void MergeInputDefinitions(const Stack<DefinitionLocation>& input_definitions,
                             Worklist<Block*>* worklist) {
    if (!input_definitions_) {
      input_definitions_ = input_definitions;
      if (worklist) worklist->Enqueue(this);
      return;
    }

    DCHECK_EQ(input_definitions_->Size(), input_definitions.Size());
    bool changed = false;
    for (BottomOffset i = {0}; i < input_definitions.AboveTop(); ++i) {
      auto& current = input_definitions_->Peek(i);
      auto& input = input_definitions.Peek(i);
      if (current == input) continue;
      if (current == DefinitionLocation::Phi(this, i.offset)) continue;
      input_definitions_->Poke(i, DefinitionLocation::Phi(this, i.offset));
      changed = true;
    }

    if (changed && worklist) worklist->Enqueue(this);
  }
  bool HasInputDefinitions() const {
    return input_definitions_ != std::nullopt;
  }
  const Stack<DefinitionLocation>& InputDefinitions() const {
    DCHECK(HasInputDefinitions());
    return *input_definitions_;
  }

  bool IsDead() const { return !HasInputDefinitions(); }

 private:
  ControlFlowGraph* cfg_;
  std::vector<Instruction> instructions_;
  std::optional<Stack<const Type*>> input_types_;
  std::optional<Stack<DefinitionLocation>> input_definitions_;
  const size_t id_;
  bool is_deferred_;
};

class ControlFlowGraph {
 public:
  explicit ControlFlowGraph(Stack<const Type*> input_types) {
    start_ = NewBlock(std::move(input_types), false);
    PlaceBlock(start_);
  }

  Block* NewBlock(std::optional<Stack<const Type*>> input_types,
                  bool is_deferred) {
    blocks_.emplace_back(this, next_block_id_++, std::move(input_types),
                         is_deferred);
    return &blocks_.back();
  }
  void PlaceBlock(Block* block) { placed_blocks_.push_back(block); }
  template <typename UnaryPredicate>
  void UnplaceBlockIf(UnaryPredicate&& predicate) {
    auto newEnd = std::remove_if(placed_blocks_.begin(), placed_blocks_.end(),
                                 std::forward<UnaryPredicate>(predicate));
    placed_blocks_.erase(newEnd, placed_blocks_.end());
  }
  Block* start() const { return start_; }
  std::optional<Block*> end() const { return end_; }
  void set_end(Block* end) { end_ = end; }
  void SetReturnType(TypeVector t) {
    if (!return_type_) {
      return_type_ = t;
      return;
    }
    if (t != *return_type_) {
      std::stringstream message;
      message << "expected return type ";
      PrintCommaSeparatedList(message, *return_type_);
      message << " instead of ";
      PrintCommaSeparatedList(message, t);
      ReportError(message.str());
    }
  }
  const std::vector<Block*>& blocks() const { return placed_blocks_; }
  size_t NumberOfBlockIds() const { return next_block_id_; }
  std::size_t ParameterCount() const {
    return start_ ? start_->InputTypes().Size() : 0;
  }

 private:
  std::list<Block> blocks_;
  Block* start_;
  std::vector<Block*> placed_blocks_;
  std::optional<Block*> end_;
  std::optional<TypeVector> return_type_;
  size_t next_block_id_ = 0;
};

class CfgAssembler {
 public:
  explicit CfgAssembler(Stack<const Type*> input_types)
      : current_stack_(std::move(input_types)), cfg_(current_stack_) {}

  const ControlFlowGraph& Result() {
    if (!CurrentBlockIsComplete()) {
      cfg_.set_end(current_block_);
    }
    OptimizeCfg();
    DCHECK(CfgIsComplete());
    ComputeInputDefinitions();
    return cfg_;
  }

  Block* NewBlock(std::optional<Stack<const Type*>> input_types = std::nullopt,
                  bool is_deferred = false) {
    return cfg_.NewBlock(std::move(input_types), is_deferred);
  }

  bool CurrentBlockIsComplete() const { return current_block_->IsComplete(); }
  bool CfgIsComplete() const {
    return std::all_of(
        cfg_.blocks().begin(), cfg_.blocks().end(), [this](Block* block) {
          return (cfg_.end() && *cfg_.end() == block) || block->IsComplete();
        });
  }

  void Emit(Instruction instruction) {
    instruction.TypeInstruction(&current_stack_, &cfg_);
    current_block_->Add(std::move(instruction));
  }

  const Stack<const Type*>& CurrentStack() const { return current_stack_; }

  StackRange TopRange(size_t slot_count) const {
    return CurrentStack().TopRange(slot_count);
  }

  void Bind(Block* block);
  void Goto(Block* block);
  // Goto block while keeping {preserved_slots} many slots on the top and
  // deleting additional the slots below these to match the input type of the
  // target block.
  // Returns the StackRange of the preserved slots in the target block.
  StackRange Goto(Block* block, size_t preserved_slots);
  // The condition must be of type bool and on the top of stack. It is removed
  // from the stack before branching.
  void Branch(Block* if_true, Block* if_false);
  // Delete the specified range of slots, moving upper slots to fill the gap.
  void DeleteRange(StackRange range);
  void DropTo(BottomOffset new_level);
  StackRange Peek(StackRange range, std::optional<const Type*> type);
  void Poke(StackRange destination, StackRange origin,
            std::optional<const Type*> type);
  void Print(std::string s);
  void AssertionFailure(std::string message);
  void Unreachable();
  void DebugBreak();

  void PrintCurrentStack(std::ostream& s) { s << "stack: " << current_stack_; }
  void OptimizeCfg();
  void ComputeInputDefinitions();

 private:
  friend class CfgAssemblerScopedTemporaryBlock;
  Stack<const Type*> current_stack_;
  ControlFlowGraph cfg_;
  Block* current_block_ = cfg_.start();
};

class V8_NODISCARD CfgAssemblerScopedTemporaryBlock {
 public:
  CfgAssemblerScopedTemporaryBlock(CfgAssembler* assembler, Block* block)
      : assembler_(assembler), saved_block_(block) {
    saved_stack_ = block->InputTypes();
    DCHECK(!assembler->CurrentBlockIsComplete());
    std::swap(saved_block_, assembler->current_block_);
    std::swap(saved_stack_, assembler->current_stack_);
    assembler->cfg_.PlaceBlock(block);
  }

  ~CfgAssemblerScopedTemporaryBlock() {
    DCHECK(assembler_->CurrentBlockIsComplete());
    std::swap(saved_block_, assembler_->current_block_);
    std::swap(saved_stack_, assembler_->current_stack_);
  }

 private:
  CfgAssembler* assembler_;
  Stack<const Type*> saved_stack_;
  Block* saved_block_;
};

}  // namespace v8::internal::torque

#endif  // V8_TORQUE_CFG_H_
