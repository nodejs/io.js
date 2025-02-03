// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/raw-machine-assembler.h"

#include <optional>

#include "src/base/small-vector.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/scheduler.h"
#include "src/heap/factory-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

RawMachineAssembler::RawMachineAssembler(
    Isolate* isolate, Graph* graph, CallDescriptor* call_descriptor,
    MachineRepresentation word, MachineOperatorBuilder::Flags flags,
    MachineOperatorBuilder::AlignmentRequirements alignment_requirements)
    : isolate_(isolate),
      graph_(graph),
      schedule_(zone()->New<Schedule>(zone())),
      source_positions_(zone()->New<SourcePositionTable>(graph)),
      machine_(zone(), word, flags, alignment_requirements),
      common_(zone()),
      simplified_(zone()),
      call_descriptor_(call_descriptor),
      target_parameter_(nullptr),
      parameters_(parameter_count(), zone()),
      current_block_(schedule()->start()) {
  int param_count = static_cast<int>(parameter_count());
  // Add an extra input for the JSFunction parameter to the start node.
  graph->SetStart(graph->NewNode(common_.Start(param_count + 1)));
  if (call_descriptor->IsJSFunctionCall()) {
    target_parameter_ = AddNode(
        common()->Parameter(Linkage::kJSCallClosureParamIndex), graph->start());
  }
  for (size_t i = 0; i < parameter_count(); ++i) {
    parameters_[i] =
        AddNode(common()->Parameter(static_cast<int>(i)), graph->start());
  }
  graph->SetEnd(graph->NewNode(common_.End(0)));
  source_positions_->AddDecorator();
}

void RawMachineAssembler::SetCurrentExternalSourcePosition(
    FileAndLine file_and_line) {
  int file_id =
      isolate()->LookupOrAddExternallyCompiledFilename(file_and_line.first);
  SourcePosition p = SourcePosition::External(file_and_line.second, file_id);
  DCHECK_EQ(p.ExternalLine(), file_and_line.second);
  source_positions()->SetCurrentPosition(p);
}

FileAndLine RawMachineAssembler::GetCurrentExternalSourcePosition() const {
  SourcePosition p = source_positions_->GetCurrentPosition();
  if (!p.IsKnown()) return {nullptr, -1};
  int file_id = p.ExternalFileId();
  const char* file_name = isolate()->GetExternallyCompiledFilename(file_id);
  int line = p.ExternalLine();
  return {file_name, line};
}

Node* RawMachineAssembler::NullConstant() {
  return HeapConstant(isolate()->factory()->null_value());
}

Node* RawMachineAssembler::UndefinedConstant() {
  return HeapConstant(isolate()->factory()->undefined_value());
}

Node* RawMachineAssembler::RelocatableIntPtrConstant(intptr_t value,
                                                     RelocInfo::Mode rmode) {
  return kSystemPointerSize == 8
             ? RelocatableInt64Constant(value, rmode)
             : RelocatableInt32Constant(static_cast<int>(value), rmode);
}

Node* RawMachineAssembler::OptimizedAllocate(Node* size,
                                             AllocationType allocation) {
  return AddNode(simplified()->AllocateRaw(Type::Any(), allocation), size);
}

Schedule* RawMachineAssembler::ExportForTest() {
  // Compute the correct codegen order.
  DCHECK(schedule_->rpo_order()->empty());
  if (v8_flags.trace_turbo_scheduler) {
    PrintF("--- RAW SCHEDULE -------------------------------------------\n");
    StdoutStream{} << *schedule_;
  }
  schedule_->EnsureCFGWellFormedness();
  Scheduler::ComputeSpecialRPO(zone(), schedule_);
  Scheduler::GenerateDominatorTree(schedule_);
  schedule_->PropagateDeferredMark();
  if (v8_flags.trace_turbo_scheduler) {
    PrintF("--- EDGE SPLIT AND PROPAGATED DEFERRED SCHEDULE ------------\n");
    StdoutStream{} << *schedule_;
  }
  // Invalidate RawMachineAssembler.
  source_positions_->RemoveDecorator();
  Schedule* schedule = schedule_;
  schedule_ = nullptr;
  return schedule;
}

Graph* RawMachineAssembler::ExportForOptimization() {
  // Compute the correct codegen order.
  DCHECK(schedule_->rpo_order()->empty());
  if (v8_flags.trace_turbo_scheduler) {
    PrintF("--- RAW SCHEDULE -------------------------------------------\n");
    StdoutStream{} << *schedule_;
  }
  schedule_->EnsureCFGWellFormedness();
  OptimizeControlFlow(schedule_, graph(), common());
  Scheduler::ComputeSpecialRPO(zone(), schedule_);
  if (v8_flags.trace_turbo_scheduler) {
    PrintF("--- SCHEDULE BEFORE GRAPH CREATION -------------------------\n");
    StdoutStream{} << *schedule_;
  }
  MakeReschedulable();
  // Invalidate RawMachineAssembler.
  schedule_ = nullptr;
  return graph();
}

void RawMachineAssembler::OptimizeControlFlow(Schedule* schedule, Graph* graph,
                                              CommonOperatorBuilder* common) {
  for (bool changed = true; changed;) {
    changed = false;
    for (size_t i = 0; i < schedule->all_blocks()->size(); ++i) {
      BasicBlock* block = (*schedule->all_blocks())[i];
      if (block == nullptr) continue;

      // Short-circuit a goto if the succeeding block is not a control-flow
      // merge. This is not really useful on it's own since graph construction
      // has the same effect, but combining blocks improves the pattern-match on
      // their structure below.
      if (block->control() == BasicBlock::kGoto) {
        DCHECK_EQ(block->SuccessorCount(), 1);
        BasicBlock* successor = block->SuccessorAt(0);
        if (successor->PredecessorCount() == 1) {
          DCHECK_EQ(successor->PredecessorAt(0), block);
          for (Node* node : *successor) {
            schedule->SetBlockForNode(nullptr, node);
            schedule->AddNode(block, node);
          }
          block->set_control(successor->control());
          Node* control_input = successor->control_input();
          block->set_control_input(control_input);
          if (control_input) {
            schedule->SetBlockForNode(block, control_input);
          }
          if (successor->deferred()) block->set_deferred(true);
          block->ClearSuccessors();
          schedule->MoveSuccessors(successor, block);
          schedule->ClearBlockById(successor->id());
          changed = true;
          --i;
          continue;
        }
      }
      // Block-cloning in the simple case where a block consists only of a phi
      // node and a branch on that phi. This just duplicates the branch block
      // for each predecessor, replacing the phi node with the corresponding phi
      // input.
      if (block->control() == BasicBlock::kBranch && block->NodeCount() == 1) {
        Node* phi = block->NodeAt(0);
        if (phi->opcode() != IrOpcode::kPhi) continue;
        Node* branch = block->control_input();
        DCHECK_EQ(branch->opcode(), IrOpcode::kBranch);
        if (NodeProperties::GetValueInput(branch, 0) != phi) continue;
        if (phi->UseCount() != 1) continue;
        DCHECK_EQ(phi->op()->ValueInputCount(), block->PredecessorCount());

        // Turn projection blocks into normal blocks.
        DCHECK_EQ(block->SuccessorCount(), 2);
        BasicBlock* true_block = block->SuccessorAt(0);
        BasicBlock* false_block = block->SuccessorAt(1);
        DCHECK_EQ(true_block->NodeAt(0)->opcode(), IrOpcode::kIfTrue);
        DCHECK_EQ(false_block->NodeAt(0)->opcode(), IrOpcode::kIfFalse);
        (*true_block->begin())->Kill();
        true_block->RemoveNode(true_block->begin());
        (*false_block->begin())->Kill();
        false_block->RemoveNode(false_block->begin());
        true_block->ClearPredecessors();
        false_block->ClearPredecessors();

        size_t arity = block->PredecessorCount();
        for (size_t j = 0; j < arity; ++j) {
          BasicBlock* predecessor = block->PredecessorAt(j);
          predecessor->ClearSuccessors();
          if (block->deferred()) predecessor->set_deferred(true);
          Node* branch_clone = graph->CloneNode(branch);
          int phi_input = static_cast<int>(j);
          NodeProperties::ReplaceValueInput(
              branch_clone, NodeProperties::GetValueInput(phi, phi_input), 0);
          BasicBlock* new_true_block = schedule->NewBasicBlock();
          BasicBlock* new_false_block = schedule->NewBasicBlock();
          new_true_block->AddNode(
              graph->NewNode(common->IfTrue(), branch_clone));
          new_false_block->AddNode(
              graph->NewNode(common->IfFalse(), branch_clone));
          schedule->AddGoto(new_true_block, true_block);
          schedule->AddGoto(new_false_block, false_block);
          DCHECK_EQ(predecessor->control(), BasicBlock::kGoto);
          predecessor->set_control(BasicBlock::kNone);
          schedule->AddBranch(predecessor, branch_clone, new_true_block,
                              new_false_block);
        }
        branch->Kill();
        schedule->ClearBlockById(block->id());
        changed = true;
        continue;
      }
    }
  }
}

void RawMachineAssembler::MakeReschedulable() {
  std::vector<Node*> block_final_control(schedule_->all_blocks_.size());
  std::vector<Node*> block_final_effect(schedule_->all_blocks_.size());

  struct LoopHeader {
    BasicBlock* block;
    Node* loop_node;
    Node* effect_phi;
  };
  std::vector<LoopHeader> loop_headers;

  // These are hoisted outside of the loop to avoid re-allocation.
  std::vector<Node*> merge_inputs;
  std::vector<Node*> effect_phi_inputs;

  for (BasicBlock* block : *schedule_->rpo_order()) {
    Node* current_control;
    Node* current_effect;
    if (block == schedule_->start()) {
      current_control = current_effect = graph()->start();
    } else if (block == schedule_->end()) {
      for (size_t i = 0; i < block->PredecessorCount(); ++i) {
        NodeProperties::MergeControlToEnd(
            graph(), common(), block->PredecessorAt(i)->control_input());
      }
    } else if (block->IsLoopHeader()) {
      // The graph()->start() inputs are just placeholders until we computed the
      // real back-edges and re-structure the control flow so the loop has
      // exactly two predecessors.
      current_control = graph()->NewNode(common()->Loop(2), graph()->start(),
                                         graph()->start());
      current_effect =
          graph()->NewNode(common()->EffectPhi(2), graph()->start(),
                           graph()->start(), current_control);

      Node* terminate = graph()->NewNode(common()->Terminate(), current_effect,
                                         current_control);
      NodeProperties::MergeControlToEnd(graph(), common(), terminate);
      loop_headers.push_back(
          LoopHeader{block, current_control, current_effect});
    } else if (block->PredecessorCount() == 1) {
      BasicBlock* predecessor = block->PredecessorAt(0);
      DCHECK_LT(predecessor->rpo_number(), block->rpo_number());
      current_effect = block_final_effect[predecessor->id().ToSize()];
      current_control = block_final_control[predecessor->id().ToSize()];
    } else {
      // Create control merge nodes and effect phis for all predecessor blocks.
      merge_inputs.clear();
      effect_phi_inputs.clear();
      int predecessor_count = static_cast<int>(block->PredecessorCount());
      for (int i = 0; i < predecessor_count; ++i) {
        BasicBlock* predecessor = block->PredecessorAt(i);
        DCHECK_LT(predecessor->rpo_number(), block->rpo_number());
        merge_inputs.push_back(block_final_control[predecessor->id().ToSize()]);
        effect_phi_inputs.push_back(
            block_final_effect[predecessor->id().ToSize()]);
      }
      current_control = graph()->NewNode(common()->Merge(predecessor_count),
                                         static_cast<int>(merge_inputs.size()),
                                         merge_inputs.data());
      effect_phi_inputs.push_back(current_control);
      current_effect = graph()->NewNode(
          common()->EffectPhi(predecessor_count),
          static_cast<int>(effect_phi_inputs.size()), effect_phi_inputs.data());
    }

    auto update_current_control_and_effect = [&](Node* node) {
      bool existing_effect_and_control =
          IrOpcode::IsIfProjectionOpcode(node->opcode()) ||
          IrOpcode::IsPhiOpcode(node->opcode());
      if (node->op()->EffectInputCount() > 0) {
        DCHECK_EQ(1, node->op()->EffectInputCount());
        if (existing_effect_and_control) {
          NodeProperties::ReplaceEffectInput(node, current_effect);
        } else {
          node->AppendInput(graph()->zone(), current_effect);
        }
      }
      if (node->op()->ControlInputCount() > 0) {
        DCHECK_EQ(1, node->op()->ControlInputCount());
        if (existing_effect_and_control) {
          NodeProperties::ReplaceControlInput(node, current_control);
        } else {
          node->AppendInput(graph()->zone(), current_control);
        }
      }
      if (node->op()->EffectOutputCount() > 0) {
        DCHECK_EQ(1, node->op()->EffectOutputCount());
        current_effect = node;
      }
      if (node->op()->ControlOutputCount() > 0) {
        current_control = node;
      }
    };

    for (Node* node : *block) {
      update_current_control_and_effect(node);
    }
    if (block->deferred()) MarkControlDeferred(current_control);

    if (Node* block_terminator = block->control_input()) {
      update_current_control_and_effect(block_terminator);
    }

    block_final_effect[block->id().ToSize()] = current_effect;
    block_final_control[block->id().ToSize()] = current_control;
  }

  // Fix-up loop backedges and re-structure control flow so that loop nodes have
  // exactly two control predecessors.
  for (const LoopHeader& loop_header : loop_headers) {
    BasicBlock* block = loop_header.block;
    std::vector<BasicBlock*> loop_entries;
    std::vector<BasicBlock*> loop_backedges;
    for (size_t i = 0; i < block->PredecessorCount(); ++i) {
      BasicBlock* predecessor = block->PredecessorAt(i);
      if (block->LoopContains(predecessor)) {
        loop_backedges.push_back(predecessor);
      } else {
        DCHECK(loop_backedges.empty());
        loop_entries.push_back(predecessor);
      }
    }
    DCHECK(!loop_entries.empty());
    DCHECK(!loop_backedges.empty());

    int entrance_count = static_cast<int>(loop_entries.size());
    int backedge_count = static_cast<int>(loop_backedges.size());
    Node* control_loop_entry = CreateNodeFromPredecessors(
        loop_entries, block_final_control, common()->Merge(entrance_count), {});
    Node* control_backedge =
        CreateNodeFromPredecessors(loop_backedges, block_final_control,
                                   common()->Merge(backedge_count), {});
    Node* effect_loop_entry = CreateNodeFromPredecessors(
        loop_entries, block_final_effect, common()->EffectPhi(entrance_count),
        {control_loop_entry});
    Node* effect_backedge = CreateNodeFromPredecessors(
        loop_backedges, block_final_effect, common()->EffectPhi(backedge_count),
        {control_backedge});

    loop_header.loop_node->ReplaceInput(0, control_loop_entry);
    loop_header.loop_node->ReplaceInput(1, control_backedge);
    loop_header.effect_phi->ReplaceInput(0, effect_loop_entry);
    loop_header.effect_phi->ReplaceInput(1, effect_backedge);

    for (Node* node : *block) {
      if (node->opcode() == IrOpcode::kPhi) {
        MakePhiBinary(node, static_cast<int>(loop_entries.size()),
                      control_loop_entry, control_backedge);
      }
    }
  }
}

Node* RawMachineAssembler::CreateNodeFromPredecessors(
    const std::vector<BasicBlock*>& predecessors,
    const std::vector<Node*>& sidetable, const Operator* op,
    const std::vector<Node*>& additional_inputs) {
  if (predecessors.size() == 1) {
    return sidetable[predecessors.front()->id().ToSize()];
  }
  std::vector<Node*> inputs;
  inputs.reserve(predecessors.size());
  for (BasicBlock* predecessor : predecessors) {
    inputs.push_back(sidetable[predecessor->id().ToSize()]);
  }
  for (Node* additional_input : additional_inputs) {
    inputs.push_back(additional_input);
  }
  return graph()->NewNode(op, static_cast<int>(inputs.size()), inputs.data());
}

void RawMachineAssembler::MakePhiBinary(Node* phi, int split_point,
                                        Node* left_control,
                                        Node* right_control) {
  int value_count = phi->op()->ValueInputCount();
  if (value_count == 2) return;
  DCHECK_LT(split_point, value_count);
  DCHECK_GT(split_point, 0);

  MachineRepresentation rep = PhiRepresentationOf(phi->op());
  int left_input_count = split_point;
  int right_input_count = value_count - split_point;

  Node* left_input;
  if (left_input_count == 1) {
    left_input = NodeProperties::GetValueInput(phi, 0);
  } else {
    std::vector<Node*> inputs;
    inputs.reserve(left_input_count);
    for (int i = 0; i < left_input_count; ++i) {
      inputs.push_back(NodeProperties::GetValueInput(phi, i));
    }
    inputs.push_back(left_control);
    left_input =
        graph()->NewNode(common()->Phi(rep, static_cast<int>(left_input_count)),
                         static_cast<int>(inputs.size()), inputs.data());
  }

  Node* right_input;
  if (right_input_count == 1) {
    right_input = NodeProperties::GetValueInput(phi, split_point);
  } else {
    std::vector<Node*> inputs;
    for (int i = split_point; i < value_count; ++i) {
      inputs.push_back(NodeProperties::GetValueInput(phi, i));
    }
    inputs.push_back(right_control);
    right_input = graph()->NewNode(
        common()->Phi(rep, static_cast<int>(right_input_count)),
        static_cast<int>(inputs.size()), inputs.data());
  }

  Node* control = NodeProperties::GetControlInput(phi);
  phi->TrimInputCount(3);
  phi->ReplaceInput(0, left_input);
  phi->ReplaceInput(1, right_input);
  phi->ReplaceInput(2, control);
  NodeProperties::ChangeOp(phi, common()->Phi(rep, 2));
}

void RawMachineAssembler::MarkControlDeferred(Node* control_node) {
  BranchHint new_branch_hint;
  Node* responsible_branch = nullptr;
  while (responsible_branch == nullptr) {
    switch (control_node->opcode()) {
      case IrOpcode::kIfException:
        // IfException projections are deferred by default.
        return;
      case IrOpcode::kIfSuccess:
        control_node = NodeProperties::GetControlInput(control_node);
        continue;
      case IrOpcode::kIfValue: {
        IfValueParameters parameters = IfValueParametersOf(control_node->op());
        if (parameters.hint() != BranchHint::kFalse) {
          NodeProperties::ChangeOp(
              control_node, common()->IfValue(parameters.value(),
                                              parameters.comparison_order(),
                                              BranchHint::kFalse));
        }
        return;
      }
      case IrOpcode::kIfDefault:
        if (BranchHintOf(control_node->op()) != BranchHint::kFalse) {
          NodeProperties::ChangeOp(control_node,
                                   common()->IfDefault(BranchHint::kFalse));
        }
        return;
      case IrOpcode::kIfTrue: {
        Node* branch = NodeProperties::GetControlInput(control_node);
        BranchHint hint = BranchHintOf(branch->op());
        if (hint == BranchHint::kTrue) {
          // The other possibility is also deferred, so the responsible branch
          // has to be before.
          control_node = NodeProperties::GetControlInput(branch);
          continue;
        }
        new_branch_hint = BranchHint::kFalse;
        responsible_branch = branch;
        break;
      }
      case IrOpcode::kIfFalse: {
        Node* branch = NodeProperties::GetControlInput(control_node);
        BranchHint hint = BranchHintOf(branch->op());
        if (hint == BranchHint::kFalse) {
          // The other possibility is also deferred, so the responsible branch
          // has to be before.
          control_node = NodeProperties::GetControlInput(branch);
          continue;
        }
        new_branch_hint = BranchHint::kTrue;
        responsible_branch = branch;
        break;
      }
      case IrOpcode::kMerge:
        for (int i = 0; i < control_node->op()->ControlInputCount(); ++i) {
          MarkControlDeferred(NodeProperties::GetControlInput(control_node, i));
        }
        return;
      case IrOpcode::kLoop:
        control_node = NodeProperties::GetControlInput(control_node, 0);
        continue;
      case IrOpcode::kBranch:
      case IrOpcode::kSwitch:
        UNREACHABLE();
      case IrOpcode::kStart:
        return;
      default:
        DCHECK_EQ(1, control_node->op()->ControlInputCount());
        control_node = NodeProperties::GetControlInput(control_node);
        continue;
    }
  }

  BranchHint hint = BranchHintOf(responsible_branch->op());
  if (hint == new_branch_hint) return;
  NodeProperties::ChangeOp(responsible_branch,
                           common()->Branch(new_branch_hint));
}

Node* RawMachineAssembler::TargetParameter() {
  DCHECK_NOT_NULL(target_parameter_);
  return target_parameter_;
}

Node* RawMachineAssembler::Parameter(size_t index) {
  DCHECK_LT(index, parameter_count());
  return parameters_[index];
}


void RawMachineAssembler::Goto(RawMachineLabel* label) {
  DCHECK(current_block_ != schedule()->end());
  schedule()->AddGoto(CurrentBlock(), Use(label));
  current_block_ = nullptr;
}


void RawMachineAssembler::Branch(Node* condition, RawMachineLabel* true_val,
                                 RawMachineLabel* false_val) {
  DCHECK(current_block_ != schedule()->end());
  Node* branch = MakeNode(common()->Branch(BranchHint::kNone), 1, &condition);
  BasicBlock* true_block = schedule()->NewBasicBlock();
  BasicBlock* false_block = schedule()->NewBasicBlock();
  schedule()->AddBranch(CurrentBlock(), branch, true_block, false_block);

  true_block->AddNode(MakeNode(common()->IfTrue(), 1, &branch));
  schedule()->AddGoto(true_block, Use(true_val));

  false_block->AddNode(MakeNode(common()->IfFalse(), 1, &branch));
  schedule()->AddGoto(false_block, Use(false_val));

  current_block_ = nullptr;
}

void RawMachineAssembler::Continuations(Node* call, RawMachineLabel* if_success,
                                        RawMachineLabel* if_exception) {
  DCHECK_NOT_NULL(schedule_);
  DCHECK_NOT_NULL(current_block_);
  schedule()->AddCall(CurrentBlock(), call, Use(if_success), Use(if_exception));
  current_block_ = nullptr;
}

void RawMachineAssembler::Switch(Node* index, RawMachineLabel* default_label,
                                 const int32_t* case_values,
                                 RawMachineLabel** case_labels,
                                 size_t case_count) {
  DCHECK_NE(schedule()->end(), current_block_);
  size_t succ_count = case_count + 1;
  Node* switch_node = MakeNode(common()->Switch(succ_count), 1, &index);
  BasicBlock** succ_blocks = zone()->AllocateArray<BasicBlock*>(succ_count);
  for (size_t i = 0; i < case_count; ++i) {
    int32_t case_value = case_values[i];
    BasicBlock* case_block = schedule()->NewBasicBlock();
    Node* case_node =
        graph()->NewNode(common()->IfValue(case_value), switch_node);
    schedule()->AddNode(case_block, case_node);
    schedule()->AddGoto(case_block, Use(case_labels[i]));
    succ_blocks[i] = case_block;
  }
  BasicBlock* default_block = schedule()->NewBasicBlock();
  Node* default_node = graph()->NewNode(common()->IfDefault(), switch_node);
  schedule()->AddNode(default_block, default_node);
  schedule()->AddGoto(default_block, Use(default_label));
  succ_blocks[case_count] = default_block;
  schedule()->AddSwitch(CurrentBlock(), switch_node, succ_blocks, succ_count);
  current_block_ = nullptr;
}

void RawMachineAssembler::Return(Node* value) {
  Node* values[] = {Int32Constant(0), value};
  Node* ret = MakeNode(common()->Return(1), 2, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::Return(Node* v1, Node* v2) {
  Node* values[] = {Int32Constant(0), v1, v2};
  Node* ret = MakeNode(common()->Return(2), 3, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::Return(Node* v1, Node* v2, Node* v3) {
  Node* values[] = {Int32Constant(0), v1, v2, v3};
  Node* ret = MakeNode(common()->Return(3), 4, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::Return(Node* v1, Node* v2, Node* v3, Node* v4) {
  Node* values[] = {Int32Constant(0), v1, v2, v3, v4};
  Node* ret = MakeNode(common()->Return(4), 5, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::Return(int count, Node* vs[]) {
  using Node_ptr = Node*;
  Node** values = new Node_ptr[count + 1];
  values[0] = Int32Constant(0);
  for (int i = 0; i < count; ++i) values[i + 1] = vs[i];
  Node* ret = MakeNode(common()->Return(count), count + 1, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
  delete[] values;
}

void RawMachineAssembler::PopAndReturn(Node* pop, Node* value) {
  // PopAndReturn is supposed to be using ONLY in CSA/Torque builtins for
  // dropping ALL JS arguments that are currently located on the stack.
  // The check below ensures that there are no directly accessible stack
  // parameters from current builtin, which implies that the builtin with
  // JS calling convention (TFJ) was created with kDontAdaptArgumentsSentinel.
  // This simplifies semantics of this instruction because in case of presence
  // of directly accessible stack parameters it's impossible to distinguish
  // the following cases:
  // 1) stack parameter is included in JS arguments (and therefore it will be
  //    dropped as a part of 'pop' number of arguments),
  // 2) stack parameter is NOT included in JS arguments (and therefore it should
  //    be dropped in ADDITION to the 'pop' number of arguments).
  // Additionally, in order to simplify assembly code, PopAndReturn is also
  // not allowed in builtins with stub linkage and parameters on stack.
  CHECK_EQ(call_descriptor()->ParameterSlotCount(), 0);
  Node* values[] = {pop, value};
  Node* ret = MakeNode(common()->Return(1), 2, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::PopAndReturn(Node* pop, Node* v1, Node* v2) {
  Node* values[] = {pop, v1, v2};
  Node* ret = MakeNode(common()->Return(2), 3, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::PopAndReturn(Node* pop, Node* v1, Node* v2,
                                       Node* v3) {
  Node* values[] = {pop, v1, v2, v3};
  Node* ret = MakeNode(common()->Return(3), 4, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::PopAndReturn(Node* pop, Node* v1, Node* v2, Node* v3,
                                       Node* v4) {
  Node* values[] = {pop, v1, v2, v3, v4};
  Node* ret = MakeNode(common()->Return(4), 5, values);
  schedule()->AddReturn(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::AbortCSADcheck(Node* message) {
  AddNode(machine()->AbortCSADcheck(), message);
}

void RawMachineAssembler::DebugBreak() { AddNode(machine()->DebugBreak()); }

void RawMachineAssembler::Unreachable() {
  Node* ret = MakeNode(common()->Throw(), 0, nullptr);
  schedule()->AddThrow(CurrentBlock(), ret);
  current_block_ = nullptr;
}

void RawMachineAssembler::Comment(const std::string& msg) {
  size_t length = msg.length() + 1;
  char* zone_buffer = zone()->AllocateArray<char>(length);
  MemCopy(zone_buffer, msg.c_str(), length);
  AddNode(machine()->Comment(zone_buffer));
}

void RawMachineAssembler::StaticAssert(Node* value, const char* source) {
  AddNode(common()->StaticAssert(source), value);
}

Node* RawMachineAssembler::CallN(CallDescriptor* call_descriptor,
                                 int input_count, Node* const* inputs) {
  DCHECK(!call_descriptor->NeedsFrameState());
  // +1 is for target.
  DCHECK_EQ(input_count, call_descriptor->ParameterCount() + 1);
  return AddNode(common()->Call(call_descriptor), input_count, inputs);
}

Node* RawMachineAssembler::CallNWithFrameState(CallDescriptor* call_descriptor,
                                               int input_count,
                                               Node* const* inputs) {
  DCHECK(call_descriptor->NeedsFrameState());
  // +2 is for target and frame state.
  DCHECK_EQ(input_count, call_descriptor->ParameterCount() + 2);
  return AddNode(common()->Call(call_descriptor), input_count, inputs);
}

void RawMachineAssembler::TailCallN(CallDescriptor* call_descriptor,
                                    int input_count, Node* const* inputs) {
  // +1 is for target.
  DCHECK_EQ(input_count, call_descriptor->ParameterCount() + 1);
  Node* tail_call =
      MakeNode(common()->TailCall(call_descriptor), input_count, inputs);
  schedule()->AddTailCall(CurrentBlock(), tail_call);
  current_block_ = nullptr;
}

namespace {

enum FunctionDescriptorMode { kHasFunctionDescriptor, kNoFunctionDescriptor };

Node* CallCFunctionImpl(
    RawMachineAssembler* rasm, Node* function,
    std::optional<MachineType> return_type,
    std::initializer_list<RawMachineAssembler::CFunctionArg> args,
    bool caller_saved_regs, SaveFPRegsMode mode,
    FunctionDescriptorMode no_function_descriptor) {
  static constexpr std::size_t kNumCArgs = 10;

  MachineSignature::Builder builder(rasm->zone(), return_type ? 1 : 0,
                                    args.size());
  if (return_type) {
    builder.AddReturn(*return_type);
  }
  for (const auto& arg : args) builder.AddParam(arg.first);

  bool caller_saved_fp_regs =
      caller_saved_regs && (mode == SaveFPRegsMode::kSave);
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  if (caller_saved_regs) flags |= CallDescriptor::kCallerSavedRegisters;
  if (caller_saved_fp_regs) flags |= CallDescriptor::kCallerSavedFPRegisters;
  if (no_function_descriptor) flags |= CallDescriptor::kNoFunctionDescriptor;
  auto call_descriptor =
      Linkage::GetSimplifiedCDescriptor(rasm->zone(), builder.Build(), flags);

  base::SmallVector<Node*, kNumCArgs> nodes(args.size() + 1);
  nodes[0] = function;
  std::transform(
      args.begin(), args.end(), std::next(nodes.begin()),
      [](const RawMachineAssembler::CFunctionArg& arg) { return arg.second; });

  auto common = rasm->common();
  return rasm->AddNode(common->Call(call_descriptor),
                       static_cast<int>(nodes.size()), nodes.begin());
}

}  // namespace

Node* RawMachineAssembler::CallCFunction(
    Node* function, std::optional<MachineType> return_type,
    std::initializer_list<RawMachineAssembler::CFunctionArg> args) {
  return CallCFunctionImpl(this, function, return_type, args, false,
                           SaveFPRegsMode::kIgnore, kHasFunctionDescriptor);
}

Node* RawMachineAssembler::CallCFunctionWithoutFunctionDescriptor(
    Node* function, MachineType return_type,
    std::initializer_list<RawMachineAssembler::CFunctionArg> args) {
  return CallCFunctionImpl(this, function, return_type, args, false,
                           SaveFPRegsMode::kIgnore, kNoFunctionDescriptor);
}

Node* RawMachineAssembler::CallCFunctionWithCallerSavedRegisters(
    Node* function, MachineType return_type, SaveFPRegsMode mode,
    std::initializer_list<RawMachineAssembler::CFunctionArg> args) {
  return CallCFunctionImpl(this, function, return_type, args, true, mode,
                           kHasFunctionDescriptor);
}

BasicBlock* RawMachineAssembler::Use(RawMachineLabel* label) {
  label->used_ = true;
  return EnsureBlock(label);
}

BasicBlock* RawMachineAssembler::EnsureBlock(RawMachineLabel* label) {
  if (label->block_ == nullptr) {
    label->block_ = schedule()->NewBasicBlock();
  }
  return label->block_;
}

void RawMachineAssembler::Bind(RawMachineLabel* label) {
  DCHECK_NULL(current_block_);
  DCHECK(!label->bound_);
  label->bound_ = true;
  current_block_ = EnsureBlock(label);
  current_block_->set_deferred(label->deferred_);
}

#if DEBUG
void RawMachineAssembler::Bind(RawMachineLabel* label,
                               AssemblerDebugInfo info) {
  if (current_block_ != nullptr) {
    std::stringstream str;
    str << "Binding label without closing previous block:"
        << "\n#    label:          " << info
        << "\n#    previous block: " << *current_block_;
    FATAL("%s", str.str().c_str());
  }
  Bind(label);
  current_block_->set_debug_info(info);
}

void RawMachineAssembler::PrintCurrentBlock(std::ostream& os) {
  os << CurrentBlock();
}

void RawMachineAssembler::SetInitialDebugInformation(
    AssemblerDebugInfo debug_info) {
  CurrentBlock()->set_debug_info(debug_info);
}
#endif  // DEBUG

bool RawMachineAssembler::InsideBlock() { return current_block_ != nullptr; }

BasicBlock* RawMachineAssembler::CurrentBlock() {
  DCHECK(current_block_);
  return current_block_;
}

Node* RawMachineAssembler::Phi(MachineRepresentation rep, int input_count,
                               Node* const* inputs) {
  Node** buffer = zone()->AllocateArray<Node*>(input_count + 1);
  std::copy(inputs, inputs + input_count, buffer);
  buffer[input_count] = graph()->start();
  return AddNode(common()->Phi(rep, input_count), input_count + 1, buffer);
}

void RawMachineAssembler::AppendPhiInput(Node* phi, Node* new_input) {
  const Operator* op = phi->op();
  const Operator* new_op = common()->ResizeMergeOrPhi(op, phi->InputCount());
  phi->InsertInput(zone(), phi->InputCount() - 1, new_input);
  NodeProperties::ChangeOp(phi, new_op);
}

Node* RawMachineAssembler::AddNode(const Operator* op, int input_count,
                                   Node* const* inputs) {
  DCHECK_NOT_NULL(schedule_);
  DCHECK_NOT_NULL(current_block_);
  Node* node = MakeNode(op, input_count, inputs);
  schedule()->AddNode(CurrentBlock(), node);
  return node;
}

Node* RawMachineAssembler::MakeNode(const Operator* op, int input_count,
                                    Node* const* inputs) {
  // The raw machine assembler nodes do not have effect and control inputs,
  // so we disable checking input counts here.
  return graph()->NewNodeUnchecked(op, input_count, inputs);
}

RawMachineLabel::~RawMachineLabel() {
#if DEBUG
  if (bound_ == used_) return;
  std::stringstream str;
  if (bound_) {
    str << "A label has been bound but it's not used."
        << "\n#    label: " << *block_;
  } else {
    str << "A label has been used but it's not bound.";
  }
  FATAL("%s", str.str().c_str());
#endif  // DEBUG
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
