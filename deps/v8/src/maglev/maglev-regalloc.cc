// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-regalloc.h"

#include <sstream>
#include <type_traits>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/compiler/backend/instruction.h"
#include "src/heap/parked-scope.h"
#include "src/maglev/maglev-code-gen-state.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-regalloc-data.h"
#include "src/zone/zone-containers.h"

#ifdef V8_TARGET_ARCH_ARM
#include "src/codegen/arm/register-arm.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/codegen/arm64/register-arm64.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/register-x64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/codegen/s390/register-s390.h"
#else
#error "Maglev does not supported this architecture."
#endif

namespace v8 {
namespace internal {

namespace maglev {

namespace {

constexpr RegisterStateFlags initialized_node{true, false};
constexpr RegisterStateFlags initialized_merge{true, true};

using BlockReverseIterator = std::vector<BasicBlock>::reverse_iterator;

// A target is a fallthrough of a control node if its ID is the next ID
// after the control node.
//
// TODO(leszeks): Consider using the block iterator instead.
bool IsTargetOfNodeFallthrough(ControlNode* node, BasicBlock* target) {
  return node->id() + 1 == target->first_id();
}

ControlNode* NearestPostDominatingHole(ControlNode* node) {
  // Conditional control nodes don't cause holes themselves. So, the nearest
  // post-dominating hole is the conditional control node's next post-dominating
  // hole.
  if (node->Is<BranchControlNode>()) {
    return node->next_post_dominating_hole();
  }

  // If the node is a Jump, it may be a hole, but only if it is not a
  // fallthrough (jump to the immediately next block). Otherwise, it will point
  // to the nearest post-dominating hole in its own "next" field.
  if (node->Is<Jump>() || node->Is<CheckpointedJump>()) {
    BasicBlock* target;
    if (auto jmp = node->TryCast<Jump>()) {
      target = jmp->target();
    } else {
      target = node->Cast<CheckpointedJump>()->target();
    }
    if (IsTargetOfNodeFallthrough(node, target)) {
      return node->next_post_dominating_hole();
    }
  }

  // If the node is a Switch, it can only have a hole if there is no
  // fallthrough.
  if (Switch* _switch = node->TryCast<Switch>()) {
    if (_switch->has_fallthrough()) {
      return _switch->next_post_dominating_hole();
    }
  }

  return node;
}

ControlNode* HighestPostDominatingHole(ControlNode* first,
                                       ControlNode* second) {
  // Either find the merge-point of both branches, or the highest reachable
  // control-node of the longest branch after the last node of the shortest
  // branch.

  // As long as there's no merge-point.
  while (first != second) {
    // Walk the highest branch to find where it goes.
    if (first->id() > second->id()) std::swap(first, second);

    // If the first branch terminates or jumps back, we've found highest
    // reachable control-node of the longest branch (the second control
    // node).
    if (first->Is<TerminalControlNode>() || first->Is<JumpLoop>()) {
      return second;
    }

    // Continue one step along the highest branch. This may cross over the
    // lowest branch in case it returns or loops. If labelled blocks are
    // involved such swapping of which branch is the highest branch can
    // occur multiple times until a return/jumploop/merge is discovered.
    first = first->next_post_dominating_hole();
  }

  // Once the branches merged, we've found the gap-chain that's relevant
  // for the control node.
  return first;
}

template <size_t kSize>
ControlNode* HighestPostDominatingHole(
    base::SmallVector<ControlNode*, kSize>& holes) {
  // Sort them from highest to shortest.
  std::sort(holes.begin(), holes.end(),
            [](ControlNode* first, ControlNode* second) {
              return first->id() > second->id();
            });
  DCHECK_GT(holes.size(), 1);
  // Find the highest post dominating hole.
  ControlNode* post_dominating_hole = holes.back();
  holes.pop_back();
  while (holes.size() > 0) {
    ControlNode* next_hole = holes.back();
    holes.pop_back();
    post_dominating_hole =
        HighestPostDominatingHole(post_dominating_hole, next_hole);
  }
  return post_dominating_hole;
}

bool IsLiveAtTarget(ValueNode* node, ControlNode* source, BasicBlock* target) {
  DCHECK_NOT_NULL(node);
  DCHECK(!node->has_no_more_uses());

  // If we're looping, a value can only be live if it was live before the loop.
  if (target->control_node()->id() <= source->id()) {
    // Gap moves may already be inserted in the target, so skip over those.
    return node->id() < target->FirstNonGapMoveId();
  }

  // Drop all values on resumable loop headers.
  if (target->has_state() && target->state()->is_resumable_loop()) return false;

  // TODO(verwaest): This should be true but isn't because we don't yet
  // eliminate dead code.
  // DCHECK_GT(node->next_use, source->id());
  // TODO(verwaest): Since we don't support deopt yet we can only deal with
  // direct branches. Add support for holes.
  return node->live_range().end >= target->first_id();
}

// TODO(dmercadier): this function should never clear any registers, since dead
// registers should always have been cleared:
//  - Nodes without uses have their output registers cleared right after their
//    allocation by `FreeRegistersUsedBy(node)`.
//  - Once the last use of a Node has been processed, its register is freed (by
//    UpdateUse, called from Assigned***Input, called by AssignInputs).
// Thus, this function should DCHECK that all of the registers are live at
// target, rather than clearing the ones that aren't.
template <typename RegisterT>
void ClearDeadFallthroughRegisters(RegisterFrameState<RegisterT>& registers,
                                   ConditionalControlNode* control_node,
                                   BasicBlock* target) {
  RegListBase<RegisterT> list = registers.used();
  while (list != registers.empty()) {
    RegisterT reg = list.PopFirst();
    ValueNode* node = registers.GetValue(reg);
    if (!IsLiveAtTarget(node, control_node, target)) {
      registers.FreeRegistersUsedBy(node);
      // Update the registers we're visiting to avoid revisiting this node.
      list.clear(registers.free());
    }
  }
}

bool IsDeadNodeToSkip(Node* node) {
  if (!node->Is<ValueNode>()) return false;
  ValueNode* value = node->Cast<ValueNode>();
  return value->has_no_more_uses() &&
         !value->properties().is_required_when_unused();
}

}  // namespace

StraightForwardRegisterAllocator::StraightForwardRegisterAllocator(
    MaglevCompilationInfo* compilation_info, Graph* graph)
    : compilation_info_(compilation_info), graph_(graph) {
  ComputePostDominatingHoles();
  AllocateRegisters();
  uint32_t tagged_stack_slots = tagged_.top;
  uint32_t untagged_stack_slots = untagged_.top;
  if (graph_->is_osr()) {
    // Fix our stack frame to be compatible with the source stack frame of this
    // OSR transition:
    // 1) Ensure the section with tagged slots is big enough to receive all
    //    live OSR-in values.
    for (auto val : graph_->osr_values()) {
      if (val->result().operand().IsAllocated() &&
          val->stack_slot() >= tagged_stack_slots) {
        tagged_stack_slots = val->stack_slot() + 1;
      }
    }
    // 2) Ensure we never have to shrink stack frames when OSR'ing into Maglev.
    //    We don't grow tagged slots or they might end up being uninitialized.
    uint32_t source_frame_size =
        graph_->min_maglev_stackslots_for_unoptimized_frame_size();
    uint32_t target_frame_size = tagged_stack_slots + untagged_stack_slots;
    if (source_frame_size > target_frame_size) {
      untagged_stack_slots += source_frame_size - target_frame_size;
    }
  }
#ifdef V8_TARGET_ARCH_ARM64
  // Due to alignment constraints, we add one untagged slot if
  // stack_slots + fixed_slot_count is odd.
  static_assert(StandardFrameConstants::kFixedSlotCount % 2 == 1);
  if ((tagged_stack_slots + untagged_stack_slots) % 2 == 0) {
    untagged_stack_slots++;
  }
#endif  // V8_TARGET_ARCH_ARM64
  graph_->set_tagged_stack_slots(tagged_stack_slots);
  graph_->set_untagged_stack_slots(untagged_stack_slots);
}

StraightForwardRegisterAllocator::~StraightForwardRegisterAllocator() = default;

// Compute, for all forward control nodes (i.e. excluding Return and JumpLoop) a
// tree of post-dominating control flow holes.
//
// Control flow which interrupts linear control flow fallthrough for basic
// blocks is considered to introduce a control flow "hole".
//
//                   A──────┐                │
//                   │ Jump │                │
//                   └──┬───┘                │
//                  {   │  B──────┐          │
//     Control flow {   │  │ Jump │          │ Linear control flow
//     hole after A {   │  └─┬────┘          │
//                  {   ▼    ▼ Fallthrough   │
//                     C──────┐              │
//                     │Return│              │
//                     └──────┘              ▼
//
// It is interesting, for each such hole, to know what the next hole will be
// that we will unconditionally reach on our way to an exit node. Such
// subsequent holes are in "post-dominators" of the current block.
//
// As an example, consider the following CFG, with the annotated holes. The
// post-dominating hole tree is the transitive closure of the post-dominator
// tree, up to nodes which are holes (in this example, A, D, F and H).
//
//                       CFG               Immediate       Post-dominating
//                                      post-dominators          holes
//                   A──────┐
//                   │ Jump │               A                 A
//                   └──┬───┘               │                 │
//                  {   │  B──────┐         │                 │
//     Control flow {   │  │ Jump │         │   B             │       B
//     hole after A {   │  └─┬────┘         │   │             │       │
//                  {   ▼    ▼              │   │             │       │
//                     C──────┐             │   │             │       │
//                     │Branch│             └►C◄┘             │   C   │
//                     └┬────┬┘               │               │   │   │
//                      ▼    │                │               │   │   │
//                   D──────┐│                │               │   │   │
//                   │ Jump ││              D │               │ D │   │
//                   └──┬───┘▼              │ │               │ │ │   │
//                  {   │  E──────┐         │ │               │ │ │   │
//     Control flow {   │  │ Jump │         │ │ E             │ │ │ E │
//     hole after D {   │  └─┬────┘         │ │ │             │ │ │ │ │
//                  {   ▼    ▼              │ │ │             │ │ │ │ │
//                     F──────┐             │ ▼ │             │ │ ▼ │ │
//                     │ Jump │             └►F◄┘             └─┴►F◄┴─┘
//                     └─────┬┘               │                   │
//                  {        │  G──────┐      │                   │
//     Control flow {        │  │ Jump │      │ G                 │ G
//     hole after F {        │  └─┬────┘      │ │                 │ │
//                  {        ▼    ▼           │ │                 │ │
//                          H──────┐          ▼ │                 ▼ │
//                          │Return│          H◄┘                 H◄┘
//                          └──────┘
//
// Since we only care about forward control, loop jumps are treated the same as
// returns -- they terminate the post-dominating hole chain.
//
void StraightForwardRegisterAllocator::ComputePostDominatingHoles() {
  // For all blocks, find the list of jumps that jump over code unreachable from
  // the block. Such a list of jumps terminates in return or jumploop.
  for (BasicBlock* block : base::Reversed(*graph_)) {
    ControlNode* control = block->control_node();
    if (auto node = control->TryCast<UnconditionalControlNode>()) {
      // If the current control node is a jump, prepend it to the list of jumps
      // at the target.
      control->set_next_post_dominating_hole(
          NearestPostDominatingHole(node->target()->control_node()));
    } else if (auto node = control->TryCast<BranchControlNode>()) {
      ControlNode* first =
          NearestPostDominatingHole(node->if_true()->control_node());
      ControlNode* second =
          NearestPostDominatingHole(node->if_false()->control_node());
      control->set_next_post_dominating_hole(
          HighestPostDominatingHole(first, second));
    } else if (auto node = control->TryCast<Switch>()) {
      int num_targets = node->size() + (node->has_fallthrough() ? 1 : 0);
      if (num_targets == 1) {
        // If we have a single target, the next post dominating hole
        // is the same one as the target.
        DCHECK(!node->has_fallthrough());
        control->set_next_post_dominating_hole(NearestPostDominatingHole(
            node->targets()[0].block_ptr()->control_node()));
        continue;
      }
      // Calculate the post dominating hole for each target.
      base::SmallVector<ControlNode*, 16> holes(num_targets);
      for (int i = 0; i < node->size(); i++) {
        holes[i] = NearestPostDominatingHole(
            node->targets()[i].block_ptr()->control_node());
      }
      if (node->has_fallthrough()) {
        holes[node->size()] =
            NearestPostDominatingHole(node->fallthrough()->control_node());
      }
      control->set_next_post_dominating_hole(HighestPostDominatingHole(holes));
    }
  }
}

void StraightForwardRegisterAllocator::PrintLiveRegs() const {
  bool first = true;
  auto print = [&](auto reg, ValueNode* node) {
    if (first) {
      first = false;
    } else {
      printing_visitor_->os() << ", ";
    }
    printing_visitor_->os() << reg << "=v" << node->id();
  };
  general_registers_.ForEachUsedRegister(print);
  double_registers_.ForEachUsedRegister(print);
}

void StraightForwardRegisterAllocator::AllocateRegisters() {
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_.reset(new MaglevPrintingVisitor(
        compilation_info_->graph_labeller(), std::cout));
    printing_visitor_->PreProcessGraph(graph_);
  }

  for (const auto& [ref, constant] : graph_->constants()) {
    constant->SetConstantLocation();
    USE(ref);
  }
  for (const auto& [index, constant] : graph_->root()) {
    constant->SetConstantLocation();
    USE(index);
  }
  for (const auto& [value, constant] : graph_->smi()) {
    constant->SetConstantLocation();
    USE(value);
  }
  for (const auto& [value, constant] : graph_->tagged_index()) {
    constant->SetConstantLocation();
    USE(value);
  }
  for (const auto& [value, constant] : graph_->int32()) {
    constant->SetConstantLocation();
    USE(value);
  }
  for (const auto& [value, constant] : graph_->uint32()) {
    constant->SetConstantLocation();
    USE(value);
  }
  for (const auto& [value, constant] : graph_->float64()) {
    constant->SetConstantLocation();
    USE(value);
  }
  for (const auto& [address, constant] : graph_->external_references()) {
    constant->SetConstantLocation();
    USE(address);
  }
  for (const auto& [ref, constant] : graph_->trusted_constants()) {
    constant->SetConstantLocation();
    USE(ref);
  }

  for (block_it_ = graph_->begin(); block_it_ != graph_->end(); ++block_it_) {
    BasicBlock* block = *block_it_;
    current_node_ = nullptr;

    // Restore mergepoint state.
    if (block->has_state()) {
      if (block->state()->is_exception_handler()) {
        // Exceptions start from a blank state of register values.
        ClearRegisterValues();
      } else if (block->state()->is_resumable_loop() &&
                 block->state()->predecessor_count() <= 1) {
        // Loops that are only reachable through JumpLoop start from a blank
        // state of register values.
        // This should actually only support predecessor_count == 1, but we
        // currently don't eliminate resumable loop headers (and subsequent code
        // until the next resume) that end up being unreachable from JumpLoop.
        ClearRegisterValues();
      } else {
        InitializeRegisterValues(block->state()->register_state());
      }
    } else if (block->is_edge_split_block()) {
      InitializeRegisterValues(block->edge_split_block_register_state());
    }

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->PreProcessBasicBlock(block);
      printing_visitor_->os() << "live regs: ";
      PrintLiveRegs();

      ControlNode* control = NearestPostDominatingHole(block->control_node());
      if (!control->Is<JumpLoop>()) {
        printing_visitor_->os() << "\n[holes:";
        while (true) {
          if (control->Is<JumpLoop>()) {
            printing_visitor_->os() << " " << control->id() << "↰";
            break;
          } else if (control->Is<UnconditionalControlNode>()) {
            BasicBlock* target =
                control->Cast<UnconditionalControlNode>()->target();
            printing_visitor_->os()
                << " " << control->id() << "-" << target->first_id();
            control = control->next_post_dominating_hole();
            DCHECK_NOT_NULL(control);
            continue;
          } else if (control->Is<Switch>()) {
            Switch* _switch = control->Cast<Switch>();
            DCHECK(!_switch->has_fallthrough());
            DCHECK_GE(_switch->size(), 1);
            BasicBlock* first_target = _switch->targets()[0].block_ptr();
            printing_visitor_->os()
                << " " << control->id() << "-" << first_target->first_id();
            control = control->next_post_dominating_hole();
            DCHECK_NOT_NULL(control);
            continue;
          } else if (control->Is<Return>()) {
            printing_visitor_->os() << " " << control->id() << ".";
            break;
          } else if (control->Is<Deopt>() || control->Is<Abort>()) {
            printing_visitor_->os() << " " << control->id() << "✖️";
            break;
          }
          UNREACHABLE();
        }
        printing_visitor_->os() << "]";
      }
      printing_visitor_->os() << std::endl;
    }

    // Activate phis.
    if (block->has_phi()) {
      Phi::List& phis = *block->phis();
      // Firstly, make the phi live, and try to assign it to an input
      // location.
      for (auto phi_it = phis.begin(); phi_it != phis.end();) {
        Phi* phi = *phi_it;
        if (!phi->has_valid_live_range()) {
          // We might still have left over dead Phis, due to phis being kept
          // alive by deopts that the representation analysis dropped. Clear
          // them out now.
          phi_it = phis.RemoveAt(phi_it);
        } else {
          DCHECK(phi->has_valid_live_range());
          phi->SetNoSpill();
          TryAllocateToInput(phi);
          ++phi_it;
        }
      }
      if (block->is_exception_handler_block()) {
        // If we are in exception handler block, then we find the ExceptionPhi
        // (the first one by default) that is marked with the
        // virtual_accumulator and force kReturnRegister0. This corresponds to
        // the exception message object.
        for (Phi* phi : phis) {
          DCHECK_EQ(phi->input_count(), 0);
          DCHECK(phi->is_exception_phi());
          if (phi->owner() == interpreter::Register::virtual_accumulator()) {
            if (!phi->has_no_more_uses()) {
              phi->result().SetAllocated(ForceAllocate(kReturnRegister0, phi));
              if (v8_flags.trace_maglev_regalloc) {
                printing_visitor_->Process(phi, ProcessingState(block_it_));
                printing_visitor_->os() << "phi (exception message object) "
                                        << phi->result().operand() << std::endl;
              }
            }
          } else if (phi->owner().is_parameter() &&
                     phi->owner().is_receiver()) {
            // The receiver is a special case for a fairly silly reason:
            // OptimizedFrame::Summarize requires the receiver (and the
            // function) to be in a stack slot, since its value must be
            // available even though we're not deoptimizing (and thus register
            // states are not available).
            //
            // TODO(leszeks):
            // For inlined functions / nested graph generation, this a) doesn't
            // work (there's no receiver stack slot); and b) isn't necessary
            // (Summarize only looks at noninlined functions).
            phi->Spill(compiler::AllocatedOperand(
                compiler::AllocatedOperand::STACK_SLOT,
                MachineRepresentation::kTagged,
                (StandardFrameConstants::kExpressionsOffset -
                 UnoptimizedFrameConstants::kRegisterFileFromFp) /
                        kSystemPointerSize +
                    interpreter::Register::receiver().index()));
            phi->result().SetAllocated(phi->spill_slot());
            // Break once both accumulator and receiver have been processed.
            break;
          }
        }
      }
      // Secondly try to assign the phi to a free register.
      for (Phi* phi : phis) {
        DCHECK(phi->has_valid_live_range());
        if (phi->result().operand().IsAllocated()) continue;
        if (phi->use_double_register()) {
          if (!double_registers_.UnblockedFreeIsEmpty()) {
            compiler::AllocatedOperand allocation =
                double_registers_.AllocateRegister(phi, phi->hint());
            phi->result().SetAllocated(allocation);
            SetLoopPhiRegisterHint(phi, allocation.GetDoubleRegister());
            if (v8_flags.trace_maglev_regalloc) {
              printing_visitor_->Process(phi, ProcessingState(block_it_));
              printing_visitor_->os()
                  << "phi (new reg) " << phi->result().operand() << std::endl;
            }
          }
        } else {
          // We'll use a general purpose register for this Phi.
          if (!general_registers_.UnblockedFreeIsEmpty()) {
            compiler::AllocatedOperand allocation =
                general_registers_.AllocateRegister(phi, phi->hint());
            phi->result().SetAllocated(allocation);
            SetLoopPhiRegisterHint(phi, allocation.GetRegister());
            if (v8_flags.trace_maglev_regalloc) {
              printing_visitor_->Process(phi, ProcessingState(block_it_));
              printing_visitor_->os()
                  << "phi (new reg) " << phi->result().operand() << std::endl;
            }
          }
        }
      }
      // Finally just use a stack slot.
      for (Phi* phi : phis) {
        DCHECK(phi->has_valid_live_range());
        if (phi->result().operand().IsAllocated()) continue;
        AllocateSpillSlot(phi);
        // TODO(verwaest): Will this be used at all?
        phi->result().SetAllocated(phi->spill_slot());
        if (v8_flags.trace_maglev_regalloc) {
          printing_visitor_->Process(phi, ProcessingState(block_it_));
          printing_visitor_->os()
              << "phi (stack) " << phi->result().operand() << std::endl;
        }
      }

      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os() << "live regs: ";
        PrintLiveRegs();
        printing_visitor_->os() << std::endl;
      }
      general_registers_.clear_blocked();
      double_registers_.clear_blocked();
    }
    VerifyRegisterState();

    node_it_ = block->nodes().begin();
    for (; node_it_ != block->nodes().end();) {
      Node* node = *node_it_;

      if (IsDeadNodeToSkip(node)) {
        // We remove unused pure nodes.
        if (v8_flags.trace_maglev_regalloc) {
          printing_visitor_->os()
              << "Removing unused node "
              << PrintNodeLabel(graph_labeller(), node) << "\n";
        }

        if (!node->Is<Identity>()) {
          // Updating the uses of the inputs in order to free dead input
          // registers. We don't do this for Identity nodes, because they were
          // skipped during use marking, and their inputs are thus not aware
          // that they were used by this node.
          DCHECK(!node->properties().can_deopt());
          node->ForAllInputsInRegallocAssignmentOrder(
              [&](NodeBase::InputAllocationPolicy, Input* input) {
                UpdateUse(input);
              });
        }

        node_it_ = block->nodes().RemoveAt(node_it_);
        continue;
      }

      AllocateNode(node);
      ++node_it_;
    }
    AllocateControlNode(block->control_node(), block);
  }
}

void StraightForwardRegisterAllocator::FreeRegistersUsedBy(ValueNode* node) {
  if (node->use_double_register()) {
    double_registers_.FreeRegistersUsedBy(node);
  } else {
    general_registers_.FreeRegistersUsedBy(node);
  }
}

void StraightForwardRegisterAllocator::UpdateUse(
    ValueNode* node, InputLocation* input_location) {
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "Using " << PrintNodeLabel(graph_labeller(), node) << "...\n";
  }

  DCHECK(!node->has_no_more_uses());

  // Update the next use.
  node->advance_next_use(input_location->next_use_id());

  if (!node->has_no_more_uses()) return;

  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "  freeing " << PrintNodeLabel(graph_labeller(), node) << "\n";
  }

  // If a value is dead, make sure it's cleared.
  FreeRegistersUsedBy(node);

  // If the stack slot is a local slot, free it so it can be reused.
  if (node->is_spilled()) {
    compiler::AllocatedOperand slot = node->spill_slot();
    if (slot.index() > 0) {
      SpillSlots& slots =
          slot.representation() == MachineRepresentation::kTagged ? tagged_
                                                                  : untagged_;
      DCHECK_IMPLIES(
          slots.free_slots.size() > 0,
          slots.free_slots.back().freed_at_position <= node->live_range().end);
      bool double_slot =
          IsDoubleRepresentation(node->properties().value_representation());
      slots.free_slots.emplace_back(slot.index(), node->live_range().end,
                                    double_slot);
    }
  }
}

void StraightForwardRegisterAllocator::AllocateEagerDeopt(
    const EagerDeoptInfo& deopt_info) {
  detail::DeepForEachInput(
      &deopt_info, [&](ValueNode* node, InputLocation* input) {
        DCHECK(!node->Is<Identity>());
        // We might have dropped this node without spilling it. Spill it now.
        if (!node->has_register() && !node->is_loadable()) {
          Spill(node);
        }
        input->InjectLocation(node->allocation());
        UpdateUse(node, input);
      });
}

void StraightForwardRegisterAllocator::AllocateLazyDeopt(
    const LazyDeoptInfo& deopt_info) {
  detail::DeepForEachInput(&deopt_info,
                           [&](ValueNode* node, InputLocation* input) {
                             DCHECK(!node->Is<Identity>());
                             // Lazy deopts always need spilling, and should
                             // always be loaded from their loadable slot.
                             Spill(node);
                             input->InjectLocation(node->loadable_slot());
                             UpdateUse(node, input);
                           });
}

#ifdef DEBUG
namespace {
#define GET_NODE_RESULT_REGISTER_T(RegisterT, AssignedRegisterT) \
  RegisterT GetNodeResult##RegisterT(Node* node) {               \
    ValueNode* value_node = node->TryCast<ValueNode>();          \
    if (!value_node) return RegisterT::no_reg();                 \
    if (!value_node->result().operand().Is##RegisterT()) {       \
      return RegisterT::no_reg();                                \
    }                                                            \
    return value_node->result().AssignedRegisterT();             \
  }
GET_NODE_RESULT_REGISTER_T(Register, AssignedGeneralRegister)
GET_NODE_RESULT_REGISTER_T(DoubleRegister, AssignedDoubleRegister)
#undef GET_NODE_RESULT_REGISTER_T
}  // namespace
#endif  // DEBUG

void StraightForwardRegisterAllocator::AllocateNode(Node* node) {
  // We shouldn't be visiting any gap moves during allocation, we should only
  // have inserted gap moves in past visits.
  DCHECK(!node->Is<GapMove>());
  DCHECK(!node->Is<ConstantGapMove>());

  current_node_ = node;
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "Allocating " << PrintNodeLabel(graph_labeller(), node)
        << " inputs...\n";
  }
  AssignInputs(node);
  VerifyInputs(node);

  if (node->properties().is_call()) SpillAndClearRegisters();

  // Allocate node output.
  if (node->Is<ValueNode>()) {
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os() << "Allocating result...\n";
    }
    AllocateNodeResult(node->Cast<ValueNode>());
  }

  // Eager deopts might happen after the node result has been set, so allocate
  // them after result allocation.
  if (node->properties().can_eager_deopt()) {
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os() << "Allocating eager deopt inputs...\n";
    }
    AllocateEagerDeopt(*node->eager_deopt_info());
  }

  // Lazy deopts are semantically after the node, so allocate them last.
  if (node->properties().can_lazy_deopt()) {
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os() << "Allocating lazy deopt inputs...\n";
    }
    // Ensure all values live from a throwing node across its catch block are
    // spilled so they can properly be merged after the catch block.
    if (node->properties().can_throw()) {
      ExceptionHandlerInfo* info = node->exception_handler_info();
      if (info->HasExceptionHandler() && !info->ShouldLazyDeopt() &&
          !node->properties().is_call()) {
        BasicBlock* block = info->catch_block.block_ptr();
        auto spill = [&](auto reg, ValueNode* node) {
          if (node->live_range().end < block->first_id()) return;
          Spill(node);
        };
        general_registers_.ForEachUsedRegister(spill);
        double_registers_.ForEachUsedRegister(spill);
      }
    }
    AllocateLazyDeopt(*node->lazy_deopt_info());
  }

  // Make sure to save snapshot after allocate eager deopt registers.
  if (node->properties().needs_register_snapshot()) SaveRegisterSnapshot(node);

  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->Process(node, ProcessingState(block_it_));
    printing_visitor_->os() << "live regs: ";
    PrintLiveRegs();
    printing_visitor_->os() << "\n";
  }

  // Result register should not be in temporaries.
  DCHECK_IMPLIES(GetNodeResultRegister(node) != Register::no_reg(),
                 !node->general_temporaries().has(GetNodeResultRegister(node)));
  DCHECK_IMPLIES(
      GetNodeResultDoubleRegister(node) != DoubleRegister::no_reg(),
      !node->double_temporaries().has(GetNodeResultDoubleRegister(node)));

  // All the temporaries should be free by the end.
  DCHECK_EQ(general_registers_.free() | node->general_temporaries(),
            general_registers_.free());
  DCHECK_EQ(double_registers_.free() | node->double_temporaries(),
            double_registers_.free());
  general_registers_.clear_blocked();
  double_registers_.clear_blocked();
  VerifyRegisterState();
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::DropRegisterValueAtEnd(
    RegisterT reg, bool force_spill) {
  RegisterFrameState<RegisterT>& list = GetRegisterFrameState<RegisterT>();
  list.unblock(reg);
  if (!list.free().has(reg)) {
    ValueNode* node = list.GetValue(reg);
    // If the register is not live after the current node, just remove its
    // value.
    if (IsCurrentNodeLastUseOf(node)) {
      node->RemoveRegister(reg);
    } else {
      DropRegisterValue(list, reg, force_spill);
    }
    list.AddToFree(reg);
  }
}

void StraightForwardRegisterAllocator::AllocateNodeResult(ValueNode* node) {
  DCHECK(!node->Is<Phi>());

  node->SetNoSpill();

  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(node->result().operand());

  if (operand.basic_policy() == compiler::UnallocatedOperand::FIXED_SLOT) {
    DCHECK(node->Is<InitialValue>());
    DCHECK_IMPLIES(!graph_->is_osr(), operand.fixed_slot_index() < 0);
    // Set the stack slot to exactly where the value is.
    compiler::AllocatedOperand location(compiler::AllocatedOperand::STACK_SLOT,
                                        node->GetMachineRepresentation(),
                                        operand.fixed_slot_index());
    node->result().SetAllocated(location);
    node->Spill(location);

    int idx = operand.fixed_slot_index();
    if (idx > 0) {
      // Reserve this slot by increasing the top and also marking slots below as
      // free. Assumes slots are processed in increasing order.
      CHECK(node->is_tagged());
      CHECK_GE(idx, tagged_.top);
      for (int i = tagged_.top; i < idx; ++i) {
        bool double_slot =
            IsDoubleRepresentation(node->properties().value_representation());
        tagged_.free_slots.emplace_back(i, node->live_range().start,
                                        double_slot);
      }
      tagged_.top = idx + 1;
    }
    return;
  }

  switch (operand.extended_policy()) {
    case compiler::UnallocatedOperand::FIXED_REGISTER: {
      Register r = Register::from_code(operand.fixed_register_index());
      DropRegisterValueAtEnd(r);
      node->result().SetAllocated(ForceAllocate(r, node));
      break;
    }

    case compiler::UnallocatedOperand::MUST_HAVE_REGISTER:
      node->result().SetAllocated(AllocateRegisterAtEnd(node));
      break;

    case compiler::UnallocatedOperand::SAME_AS_INPUT: {
      Input& input = node->input(operand.input_index());
      node->result().SetAllocated(ForceAllocate(input, node));
      // Clear any hint that (probably) comes from this constraint.
      if (node->has_hint()) input.node()->ClearHint();
      break;
    }

    case compiler::UnallocatedOperand::FIXED_FP_REGISTER: {
      DoubleRegister r =
          DoubleRegister::from_code(operand.fixed_register_index());
      DropRegisterValueAtEnd(r);
      node->result().SetAllocated(ForceAllocate(r, node));
      break;
    }

    case compiler::UnallocatedOperand::NONE:
      DCHECK(IsConstantNode(node->opcode()));
      break;

    case compiler::UnallocatedOperand::MUST_HAVE_SLOT:
    case compiler::UnallocatedOperand::REGISTER_OR_SLOT:
    case compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT:
      UNREACHABLE();
  }

  // Immediately kill the register use if the node doesn't have a valid
  // live-range.
  // TODO(verwaest): Remove once we can avoid allocating such registers.
  if (!node->has_valid_live_range() &&
      node->result().operand().IsAnyRegister()) {
    DCHECK(node->has_register());
    FreeRegistersUsedBy(node);
    DCHECK(!node->has_register());
    DCHECK(node->has_no_more_uses());
  }
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::DropRegisterValue(
    RegisterFrameState<RegisterT>& registers, RegisterT reg, bool force_spill) {
  // The register should not already be free.
  DCHECK(!registers.free().has(reg));

  ValueNode* node = registers.GetValue(reg);

  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os() << "  dropping " << reg << " value "
                            << PrintNodeLabel(graph_labeller(), node) << "\n";
  }

  MachineRepresentation mach_repr = node->GetMachineRepresentation();

  // Remove the register from the node's list.
  node->RemoveRegister(reg);
  // Return if the removed value already has another register or is loadable
  // from memory.
  if (node->has_register() || node->is_loadable()) return;
  // Try to move the value to another register. Do so without blocking that
  // register, as we may still want to use it elsewhere.
  if (!registers.UnblockedFreeIsEmpty() && !force_spill) {
    RegisterT target_reg = registers.unblocked_free().first();
    RegisterT hint_reg = node->GetRegisterHint<RegisterT>();
    if (hint_reg.is_valid() && registers.unblocked_free().has(hint_reg)) {
      target_reg = hint_reg;
    }
    registers.RemoveFromFree(target_reg);
    registers.SetValueWithoutBlocking(target_reg, node);
    // Emit a gapmove.
    compiler::AllocatedOperand source(compiler::LocationOperand::REGISTER,
                                      mach_repr, reg.code());
    compiler::AllocatedOperand target(compiler::LocationOperand::REGISTER,
                                      mach_repr, target_reg.code());
    AddMoveBeforeCurrentNode(node, source, target);
    return;
  }

  // If all else fails, spill the value.
  Spill(node);
}

void StraightForwardRegisterAllocator::DropRegisterValue(Register reg) {
  DropRegisterValue<Register>(general_registers_, reg);
}

void StraightForwardRegisterAllocator::DropRegisterValue(DoubleRegister reg) {
  DropRegisterValue<DoubleRegister>(double_registers_, reg);
}

void StraightForwardRegisterAllocator::InitializeBranchTargetPhis(
    int predecessor_id, BasicBlock* target) {
  DCHECK(!target->is_edge_split_block());

  if (!target->has_phi()) return;

  // Phi moves are emitted by resolving all phi moves as a single parallel move,
  // which means we shouldn't update register state as we go (as if we were
  // emitting a series of serialised moves) but rather take 'old' register
  // state as the phi input.
  Phi::List& phis = *target->phis();
  for (auto phi_it = phis.begin(); phi_it != phis.end();) {
    Phi* phi = *phi_it;
    if (!phi->has_valid_live_range()) {
      // We might still have left over dead Phis, due to phis being kept
      // alive by deopts that the representation analysis dropped. Clear
      // them out now.
      phi_it = phis.RemoveAt(phi_it);
    } else {
      Input& input = phi->input(predecessor_id);
      input.InjectLocation(input.node()->allocation());
      ++phi_it;
    }
  }
}

void StraightForwardRegisterAllocator::InitializeConditionalBranchTarget(
    ConditionalControlNode* control_node, BasicBlock* target) {
  DCHECK(!target->has_phi());

  if (target->has_state()) {
    // Not a fall-through branch, copy the state over.
    return InitializeBranchTargetRegisterValues(control_node, target);
  }
  if (target->is_edge_split_block()) {
    return InitializeEmptyBlockRegisterValues(control_node, target);
  }

  // Clear dead fall-through registers.
  DCHECK_EQ(control_node->id() + 1, target->first_id());
  ClearDeadFallthroughRegisters<Register>(general_registers_, control_node,
                                          target);
  ClearDeadFallthroughRegisters<DoubleRegister>(double_registers_, control_node,
                                                target);
}

void StraightForwardRegisterAllocator::AllocateControlNode(ControlNode* node,
                                                           BasicBlock* block) {
  current_node_ = node;

  // Control nodes can't lazy deopt at the moment.
  DCHECK(!node->properties().can_lazy_deopt());

  if (node->Is<Abort>()) {
    // Do nothing.
    DCHECK(node->general_temporaries().is_empty());
    DCHECK(node->double_temporaries().is_empty());
    DCHECK_EQ(node->num_temporaries_needed<Register>(), 0);
    DCHECK_EQ(node->num_temporaries_needed<DoubleRegister>(), 0);
    DCHECK_EQ(node->input_count(), 0);
    // Either there are no special properties, or there's a call but it doesn't
    // matter because we'll abort anyway.
    DCHECK_IMPLIES(
        node->properties() != OpProperties(0),
        node->properties() == OpProperties::Call() && node->Is<Abort>());

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->Process(node, ProcessingState(block_it_));
    }
  } else if (node->Is<Deopt>()) {
    // No temporaries.
    DCHECK(node->general_temporaries().is_empty());
    DCHECK(node->double_temporaries().is_empty());
    DCHECK_EQ(node->num_temporaries_needed<Register>(), 0);
    DCHECK_EQ(node->num_temporaries_needed<DoubleRegister>(), 0);
    DCHECK_EQ(node->input_count(), 0);
    DCHECK_EQ(node->properties(), OpProperties::EagerDeopt());

    AllocateEagerDeopt(*node->eager_deopt_info());

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->Process(node, ProcessingState(block_it_));
    }
  } else if (auto unconditional = node->TryCast<UnconditionalControlNode>()) {
    // No temporaries.
    DCHECK(node->general_temporaries().is_empty());
    DCHECK(node->double_temporaries().is_empty());
    DCHECK_EQ(node->num_temporaries_needed<Register>(), 0);
    DCHECK_EQ(node->num_temporaries_needed<DoubleRegister>(), 0);
    DCHECK_EQ(node->input_count(), 0);
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    DCHECK(!node->properties().needs_register_snapshot());
    DCHECK(!node->properties().is_call());

    auto predecessor_id = block->predecessor_id();
    auto target = unconditional->target();

    InitializeBranchTargetPhis(predecessor_id, target);
    MergeRegisterValues(unconditional, target, predecessor_id);
    if (target->has_phi()) {
      for (Phi* phi : *target->phis()) {
        UpdateUse(&phi->input(predecessor_id));
      }
    }

    // For JumpLoops, now update the uses of any node used in, but not defined
    // in the loop. This makes sure that such nodes' lifetimes are extended to
    // the entire body of the loop. This must be after phi initialisation so
    // that value dropping in the phi initialisation doesn't think these
    // extended lifetime nodes are dead.
    if (auto jump_loop = node->TryCast<JumpLoop>()) {
      for (Input& input : jump_loop->used_nodes()) {
        if (!input.node()->has_register() && !input.node()->is_loadable()) {
          // If the value isn't loadable by the end of a loop (this can happen
          // e.g. when a deferred throw doesn't spill it, and an exception
          // handler drops the value)
          Spill(input.node());
        }
        UpdateUse(&input);
      }
    }

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->Process(node, ProcessingState(block_it_));
    }
  } else {
    DCHECK(node->Is<ConditionalControlNode>() || node->Is<Return>());
    AssignInputs(node);
    VerifyInputs(node);

    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());

    if (node->properties().is_call()) SpillAndClearRegisters();

    DCHECK(!node->properties().needs_register_snapshot());

    DCHECK_EQ(general_registers_.free() | node->general_temporaries(),
              general_registers_.free());
    DCHECK_EQ(double_registers_.free() | node->double_temporaries(),
              double_registers_.free());

    general_registers_.clear_blocked();
    double_registers_.clear_blocked();
    VerifyRegisterState();

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->Process(node, ProcessingState(block_it_));
    }

    // Finally, initialize the merge states of branch targets, including the
    // fallthrough, with the final state after all allocation
    if (auto conditional = node->TryCast<BranchControlNode>()) {
      InitializeConditionalBranchTarget(conditional, conditional->if_true());
      InitializeConditionalBranchTarget(conditional, conditional->if_false());
    } else if (Switch* control_node = node->TryCast<Switch>()) {
      const BasicBlockRef* targets = control_node->targets();
      for (int i = 0; i < control_node->size(); i++) {
        InitializeConditionalBranchTarget(control_node, targets[i].block_ptr());
      }
      if (control_node->has_fallthrough()) {
        InitializeConditionalBranchTarget(control_node,
                                          control_node->fallthrough());
      }
    }
  }

  VerifyRegisterState();
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::SetLoopPhiRegisterHint(Phi* phi,
                                                              RegisterT reg) {
  compiler::UnallocatedOperand hint(
      std::is_same_v<RegisterT, Register>
          ? compiler::UnallocatedOperand::FIXED_REGISTER
          : compiler::UnallocatedOperand::FIXED_FP_REGISTER,
      reg.code(), kNoVreg);
  for (Input& input : *phi) {
    if (input.node()->id() > phi->id()) {
      input.node()->SetHint(hint);
    }
  }
}

void StraightForwardRegisterAllocator::TryAllocateToInput(Phi* phi) {
  // Try allocate phis to a register used by any of the inputs.
  for (Input& input : *phi) {
    if (input.operand().IsRegister()) {
      // We assume Phi nodes only point to tagged values, and so they use a
      // general register.
      Register reg = input.AssignedGeneralRegister();
      if (general_registers_.unblocked_free().has(reg)) {
        phi->result().SetAllocated(ForceAllocate(reg, phi));
        SetLoopPhiRegisterHint(phi, reg);
        DCHECK_EQ(general_registers_.GetValue(reg), phi);
        if (v8_flags.trace_maglev_regalloc) {
          printing_visitor_->Process(phi, ProcessingState(block_it_));
          printing_visitor_->os()
              << "phi (reuse) " << input.operand() << std::endl;
        }
        return;
      }
    }
  }
}

void StraightForwardRegisterAllocator::AddMoveBeforeCurrentNode(
    ValueNode* node, compiler::InstructionOperand source,
    compiler::AllocatedOperand target) {
  Node* gap_move;
  if (source.IsConstant()) {
    DCHECK(IsConstantNode(node->opcode()));
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os()
          << "  constant gap move: " << target << " ← "
          << PrintNodeLabel(graph_labeller(), node) << std::endl;
    }
    gap_move =
        Node::New<ConstantGapMove>(compilation_info_->zone(), 0, node, target);
  } else {
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os() << "  gap move: " << target << " ← "
                              << PrintNodeLabel(graph_labeller(), node) << ":"
                              << source << std::endl;
    }
    gap_move =
        Node::New<GapMove>(compilation_info_->zone(), 0,
                           compiler::AllocatedOperand::cast(source), target);
  }
  gap_move->InitTemporaries();
  if (compilation_info_->has_graph_labeller()) {
    graph_labeller()->RegisterNode(gap_move);
  }
  if (*node_it_ == nullptr) {
    DCHECK(current_node_->Is<ControlNode>());
    // We're at the control node, so append instead.
    (*block_it_)->nodes().Add(gap_move);
    node_it_ = (*block_it_)->nodes().end();
  } else {
    DCHECK_NE(node_it_, (*block_it_)->nodes().end());
    // We should not add any gap move before a GetSecondReturnedValue.
    DCHECK_NE(node_it_->opcode(), Opcode::kGetSecondReturnedValue);
    node_it_.InsertBefore(gap_move);
  }
}

void StraightForwardRegisterAllocator::Spill(ValueNode* node) {
  if (node->is_loadable()) return;
  AllocateSpillSlot(node);
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "  spill: " << node->spill_slot() << " ← "
        << PrintNodeLabel(graph_labeller(), node) << std::endl;
  }
}

void StraightForwardRegisterAllocator::AssignFixedInput(Input& input) {
  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(input.operand());
  ValueNode* node = input.node();
  compiler::InstructionOperand location = node->allocation();

  switch (operand.extended_policy()) {
    case compiler::UnallocatedOperand::MUST_HAVE_REGISTER:
      // Allocated in AssignArbitraryRegisterInput.
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os()
            << "- " << PrintNodeLabel(graph_labeller(), input.node())
            << " has arbitrary register\n";
      }
      return;

    case compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT:
      // Allocated in AssignAnyInput.
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os()
            << "- " << PrintNodeLabel(graph_labeller(), input.node())
            << " has arbitrary location\n";
      }
      return;

    case compiler::UnallocatedOperand::FIXED_REGISTER: {
      Register reg = Register::from_code(operand.fixed_register_index());
      input.SetAllocated(ForceAllocate(reg, node));
      break;
    }

    case compiler::UnallocatedOperand::FIXED_FP_REGISTER: {
      DoubleRegister reg =
          DoubleRegister::from_code(operand.fixed_register_index());
      input.SetAllocated(ForceAllocate(reg, node));
      break;
    }

    case compiler::UnallocatedOperand::REGISTER_OR_SLOT:
    case compiler::UnallocatedOperand::SAME_AS_INPUT:
    case compiler::UnallocatedOperand::NONE:
    case compiler::UnallocatedOperand::MUST_HAVE_SLOT:
      UNREACHABLE();
  }
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "- " << PrintNodeLabel(graph_labeller(), input.node())
        << " in forced " << input.operand() << "\n";
  }

  compiler::AllocatedOperand allocated =
      compiler::AllocatedOperand::cast(input.operand());
  if (location != allocated) {
    AddMoveBeforeCurrentNode(node, location, allocated);
  }
  UpdateUse(&input);
  // Clear any hint that (probably) comes from this fixed use.
  input.node()->ClearHint();
}

void StraightForwardRegisterAllocator::MarkAsClobbered(
    ValueNode* node, const compiler::AllocatedOperand& location) {
  if (node->use_double_register()) {
    DoubleRegister reg = location.GetDoubleRegister();
    DCHECK(double_registers_.is_blocked(reg));
    DropRegisterValue(reg);
    double_registers_.AddToFree(reg);
  } else {
    Register reg = location.GetRegister();
    DCHECK(general_registers_.is_blocked(reg));
    DropRegisterValue(reg);
    general_registers_.AddToFree(reg);
  }
}

namespace {

#ifdef DEBUG
bool IsInRegisterLocation(ValueNode* node,
                          compiler::InstructionOperand location) {
  DCHECK(location.IsAnyRegister());
  compiler::AllocatedOperand allocation =
      compiler::AllocatedOperand::cast(location);
  DCHECK_IMPLIES(node->use_double_register(), allocation.IsDoubleRegister());
  DCHECK_IMPLIES(!node->use_double_register(), allocation.IsRegister());
  if (node->use_double_register()) {
    return node->is_in_register(allocation.GetDoubleRegister());
  } else {
    return node->is_in_register(allocation.GetRegister());
  }
}
#endif  // DEBUG

bool SameAsInput(ValueNode* node, Input& input) {
  auto operand = compiler::UnallocatedOperand::cast(node->result().operand());
  return operand.HasSameAsInputPolicy() &&
         &input == &node->input(operand.input_index());
}

compiler::InstructionOperand InputHint(NodeBase* node, Input& input) {
  ValueNode* value_node = node->TryCast<ValueNode>();
  if (!value_node) return input.node()->hint();
  DCHECK(value_node->result().operand().IsUnallocated());
  if (SameAsInput(value_node, input)) {
    return value_node->hint();
  } else {
    return input.node()->hint();
  }
}

}  // namespace

void StraightForwardRegisterAllocator::AssignArbitraryRegisterInput(
    NodeBase* result_node, Input& input) {
  // Already assigned in AssignFixedInput
  if (!input.operand().IsUnallocated()) return;

  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(input.operand());
  if (operand.extended_policy() ==
      compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT) {
    // Allocated in AssignAnyInput.
    return;
  }

  DCHECK_EQ(operand.extended_policy(),
            compiler::UnallocatedOperand::MUST_HAVE_REGISTER);

  ValueNode* node = input.node();
  bool is_clobbered = input.Cloberred();

  compiler::AllocatedOperand location = ([&] {
    compiler::InstructionOperand existing_register_location;
    auto hint = InputHint(result_node, input);
    if (is_clobbered) {
      // For clobbered inputs, we want to pick a different register than
      // non-clobbered inputs, so that we don't clobber those.
      existing_register_location =
          node->use_double_register()
              ? double_registers_.TryChooseUnblockedInputRegister(node)
              : general_registers_.TryChooseUnblockedInputRegister(node);
    } else {
      ValueNode* value_node = result_node->TryCast<ValueNode>();
      // Only use the hint if it helps with the result's allocation due to
      // same-as-input policy. Otherwise this doesn't affect regalloc.
      auto result_hint = value_node && SameAsInput(value_node, input)
                             ? value_node->hint()
                             : compiler::InstructionOperand();
      existing_register_location =
          node->use_double_register()
              ? double_registers_.TryChooseInputRegister(node, result_hint)
              : general_registers_.TryChooseInputRegister(node, result_hint);
    }

    // Reuse an existing register if possible.
    if (existing_register_location.IsAnyLocationOperand()) {
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os()
            << "- " << PrintNodeLabel(graph_labeller(), input.node()) << " in "
            << (is_clobbered ? "clobbered " : "") << existing_register_location
            << "\n";
      }
      return compiler::AllocatedOperand::cast(existing_register_location);
    }

    // Otherwise, allocate a register for the node and load it in from there.
    compiler::InstructionOperand existing_location = node->allocation();
    compiler::AllocatedOperand allocation = AllocateRegister(node, hint);
    DCHECK_NE(existing_location, allocation);
    AddMoveBeforeCurrentNode(node, existing_location, allocation);

    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os()
          << "- " << PrintNodeLabel(graph_labeller(), input.node()) << " in "
          << (is_clobbered ? "clobbered " : "") << allocation << " ← "
          << node->allocation() << "\n";
    }
    return allocation;
  })();

  input.SetAllocated(location);

  UpdateUse(&input);
  // Only need to mark the location as clobbered if the node wasn't already
  // killed by UpdateUse.
  if (is_clobbered && !node->has_no_more_uses()) {
    MarkAsClobbered(node, location);
  }
  // Clobbered inputs should no longer be in the allocated location, as far as
  // the register allocator is concerned. This will happen either via
  // clobbering, or via this being the last use.
  DCHECK_IMPLIES(is_clobbered, !IsInRegisterLocation(node, location));
}

void StraightForwardRegisterAllocator::AssignAnyInput(Input& input) {
  // Already assigned in AssignFixedInput or AssignArbitraryRegisterInput.
  if (!input.operand().IsUnallocated()) return;

  DCHECK_EQ(
      compiler::UnallocatedOperand::cast(input.operand()).extended_policy(),
      compiler::UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT);

  ValueNode* node = input.node();
  compiler::InstructionOperand location = node->allocation();

  input.InjectLocation(location);
  if (location.IsAnyRegister()) {
    compiler::AllocatedOperand allocation =
        compiler::AllocatedOperand::cast(location);
    if (allocation.IsDoubleRegister()) {
      double_registers_.block(allocation.GetDoubleRegister());
    } else {
      general_registers_.block(allocation.GetRegister());
    }
  }
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "- " << PrintNodeLabel(graph_labeller(), input.node())
        << " in original " << location << "\n";
  }
  UpdateUse(&input);
}

void StraightForwardRegisterAllocator::AssignInputs(NodeBase* node) {
  // We allocate arbitrary register inputs after fixed inputs, since the fixed
  // inputs may clobber the arbitrarily chosen ones. Finally we assign the
  // location for the remaining inputs. Since inputs can alias a node, one of
  // the inputs could be assigned a register in AssignArbitraryRegisterInput
  // (and respectivelly its node location), therefore we wait until all
  // registers are allocated before assigning any location for these inputs.
  // TODO(dmercadier): consider using `ForAllInputsInRegallocAssignmentOrder` to
  // iterate the inputs. Since UseMarkingProcessor uses this helper to iterate
  // inputs, and it has to iterate them in the same order as this function,
  // using the iteration helper in both places would be better.
  for (Input& input : *node) AssignFixedInput(input);
  AssignFixedTemporaries(node);
  for (Input& input : *node) AssignArbitraryRegisterInput(node, input);
  AssignArbitraryTemporaries(node);
  for (Input& input : *node) AssignAnyInput(input);
}

void StraightForwardRegisterAllocator::VerifyInputs(NodeBase* node) {
#ifdef DEBUG
  for (Input& input : *node) {
    if (input.operand().IsRegister()) {
      Register reg =
          compiler::AllocatedOperand::cast(input.operand()).GetRegister();
      if (general_registers_.GetValueMaybeFreeButBlocked(reg) != input.node()) {
        FATAL("Input node n%d is not in expected register %s",
              graph_labeller()->NodeId(input.node()), RegisterName(reg));
      }
    } else if (input.operand().IsDoubleRegister()) {
      DoubleRegister reg =
          compiler::AllocatedOperand::cast(input.operand()).GetDoubleRegister();
      if (double_registers_.GetValueMaybeFreeButBlocked(reg) != input.node()) {
        FATAL("Input node n%d is not in expected register %s",
              graph_labeller()->NodeId(input.node()), RegisterName(reg));
      }
    } else {
      if (input.operand() != input.node()->allocation()) {
        std::stringstream ss;
        ss << input.operand();
        FATAL("Input node n%d is not in operand %s",
              graph_labeller()->NodeId(input.node()), ss.str().c_str());
      }
    }
  }
#endif
}

void StraightForwardRegisterAllocator::VerifyRegisterState() {
#ifdef DEBUG
  // We shouldn't have any blocked registers by now.
  DCHECK(general_registers_.blocked().is_empty());
  DCHECK(double_registers_.blocked().is_empty());

  auto NodeNameForFatal = [&](ValueNode* node) {
    std::stringstream ss;
    if (compilation_info_->has_graph_labeller()) {
      ss << PrintNodeLabel(compilation_info_->graph_labeller(), node);
    } else {
      ss << "<" << node << ">";
    }
    return ss.str();
  };

  for (Register reg : general_registers_.used()) {
    ValueNode* node = general_registers_.GetValue(reg);
    if (!node->is_in_register(reg)) {
      FATAL("Node %s doesn't think it is in register %s",
            NodeNameForFatal(node).c_str(), RegisterName(reg));
    }
  }
  for (DoubleRegister reg : double_registers_.used()) {
    ValueNode* node = double_registers_.GetValue(reg);
    if (!node->is_in_register(reg)) {
      FATAL("Node %s doesn't think it is in register %s",
            NodeNameForFatal(node).c_str(), RegisterName(reg));
    }
  }

  auto ValidateValueNode = [this, NodeNameForFatal](ValueNode* node) {
    if (node->use_double_register()) {
      for (DoubleRegister reg : node->result_registers<DoubleRegister>()) {
        if (double_registers_.unblocked_free().has(reg)) {
          FATAL("Node %s thinks it's in register %s but it's free",
                NodeNameForFatal(node).c_str(), RegisterName(reg));
        } else if (double_registers_.GetValue(reg) != node) {
          FATAL("Node %s thinks it's in register %s but it contains %s",
                NodeNameForFatal(node).c_str(), RegisterName(reg),
                NodeNameForFatal(double_registers_.GetValue(reg)).c_str());
        }
      }
    } else {
      for (Register reg : node->result_registers<Register>()) {
        if (general_registers_.unblocked_free().has(reg)) {
          FATAL("Node %s thinks it's in register %s but it's free",
                NodeNameForFatal(node).c_str(), RegisterName(reg));
        } else if (general_registers_.GetValue(reg) != node) {
          FATAL("Node %s thinks it's in register %s but it contains %s",
                NodeNameForFatal(node).c_str(), RegisterName(reg),
                NodeNameForFatal(general_registers_.GetValue(reg)).c_str());
        }
      }
    }
  };

  for (BasicBlock* block : *graph_) {
    if (block->has_phi()) {
      for (Phi* phi : *block->phis()) {
        ValidateValueNode(phi);
      }
    }
    for (Node* node : block->nodes()) {
      if (ValueNode* value_node = node->TryCast<ValueNode>()) {
        if (node->Is<Identity>()) continue;
        ValidateValueNode(value_node);
      }
    }
  }

#endif
}

void StraightForwardRegisterAllocator::SpillRegisters() {
  auto spill = [&](auto reg, ValueNode* node) { Spill(node); };
  general_registers_.ForEachUsedRegister(spill);
  double_registers_.ForEachUsedRegister(spill);
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::SpillAndClearRegisters(
    RegisterFrameState<RegisterT>& registers) {
  while (registers.used() != registers.empty()) {
    RegisterT reg = registers.used().first();
    ValueNode* node = registers.GetValue(reg);
    if (v8_flags.trace_maglev_regalloc) {
      printing_visitor_->os() << "  clearing registers with "
                              << PrintNodeLabel(graph_labeller(), node) << "\n";
    }
    Spill(node);
    registers.FreeRegistersUsedBy(node);
    DCHECK(!registers.used().has(reg));
  }
}

void StraightForwardRegisterAllocator::SpillAndClearRegisters() {
  SpillAndClearRegisters(general_registers_);
  SpillAndClearRegisters(double_registers_);
}

void StraightForwardRegisterAllocator::SaveRegisterSnapshot(NodeBase* node) {
  RegisterSnapshot snapshot;
  general_registers_.ForEachUsedRegister([&](Register reg, ValueNode* node) {
    if (node->properties().value_representation() ==
        ValueRepresentation::kTagged) {
      snapshot.live_tagged_registers.set(reg);
    }
  });
  snapshot.live_registers = general_registers_.used();
  snapshot.live_double_registers = double_registers_.used();
  // If a value node, then the result register is removed from the snapshot.
  if (ValueNode* value_node = node->TryCast<ValueNode>()) {
    if (value_node->use_double_register()) {
      snapshot.live_double_registers.clear(
          ToDoubleRegister(value_node->result()));
    } else {
      Register reg = ToRegister(value_node->result());
      snapshot.live_registers.clear(reg);
      snapshot.live_tagged_registers.clear(reg);
    }
  }
  if (node->properties().can_eager_deopt()) {
    // If we eagerly deopt after a deferred call, the registers saved by the
    // runtime call might not include the inputs into the eager deopt. Here, we
    // make sure that all the eager deopt registers are included in the
    // snapshot.
    detail::DeepForEachInput(
        node->eager_deopt_info(), [&](ValueNode* node, InputLocation* input) {
          if (!input->IsAnyRegister()) return;
          if (input->IsDoubleRegister()) {
            snapshot.live_double_registers.set(input->AssignedDoubleRegister());
          } else {
            snapshot.live_registers.set(input->AssignedGeneralRegister());
            if (node->is_tagged()) {
              snapshot.live_tagged_registers.set(
                  input->AssignedGeneralRegister());
            }
          }
        });
  }
  node->set_register_snapshot(snapshot);
}

void StraightForwardRegisterAllocator::AllocateSpillSlot(ValueNode* node) {
  DCHECK(!node->is_loadable());
  uint32_t free_slot;
  bool is_tagged = (node->properties().value_representation() ==
                    ValueRepresentation::kTagged);
  uint32_t slot_size = 1;
  bool double_slot =
      IsDoubleRepresentation(node->properties().value_representation());
  if constexpr (kDoubleSize != kSystemPointerSize) {
    if (double_slot) {
      slot_size = kDoubleSize / kSystemPointerSize;
    }
  }
  SpillSlots& slots = is_tagged ? tagged_ : untagged_;
  MachineRepresentation representation = node->GetMachineRepresentation();
  // TODO(victorgomes): We don't currently reuse double slots on arm.
  if (!v8_flags.maglev_reuse_stack_slots || slot_size > 1 ||
      slots.free_slots.empty()) {
    free_slot = slots.top + slot_size - 1;
    slots.top += slot_size;
  } else {
    NodeIdT start = node->live_range().start;
    auto it =
        std::upper_bound(slots.free_slots.begin(), slots.free_slots.end(),
                         start, [](NodeIdT s, const SpillSlotInfo& slot_info) {
                           return slot_info.freed_at_position >= s;
                         });
    // {it} points to the first invalid slot. Decrement it to get to the last
    // valid slot freed before {start}.
    if (it != slots.free_slots.begin()) {
      --it;
    }

    // TODO(olivf): Currently we cannot mix double and normal stack slots since
    // the gap resolver treats them independently and cannot detect cycles via
    // shared slots.
    while (it != slots.free_slots.begin()) {
      if (it->double_slot == double_slot) break;
      --it;
    }

    if (it != slots.free_slots.begin()) {
      CHECK_EQ(double_slot, it->double_slot);
      CHECK_GT(start, it->freed_at_position);
      free_slot = it->slot_index;
      slots.free_slots.erase(it);
    } else {
      free_slot = slots.top++;
    }
  }
  node->Spill(compiler::AllocatedOperand(compiler::AllocatedOperand::STACK_SLOT,
                                         representation, free_slot));
}

template <typename RegisterT>
RegisterT StraightForwardRegisterAllocator::PickRegisterToFree(
    RegListBase<RegisterT> reserved) {
  RegisterFrameState<RegisterT>& registers = GetRegisterFrameState<RegisterT>();
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os() << "  need to free a register... ";
  }
  int furthest_use = 0;
  RegisterT best = RegisterT::no_reg();
  for (RegisterT reg : (registers.used() - reserved)) {
    ValueNode* value = registers.GetValue(reg);

    // The cheapest register to clear is a register containing a value that's
    // contained in another register as well. Since we found the register while
    // looping over unblocked registers, we can simply use this register.
    if (value->num_registers() > 1) {
      best = reg;
      break;
    }
    int use = value->current_next_use();
    if (use > furthest_use) {
      furthest_use = use;
      best = reg;
    }
  }
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "  chose " << best << " with next use " << furthest_use << "\n";
  }
  return best;
}

template <typename RegisterT>
RegisterT StraightForwardRegisterAllocator::FreeUnblockedRegister(
    RegListBase<RegisterT> reserved) {
  RegisterFrameState<RegisterT>& registers = GetRegisterFrameState<RegisterT>();
  RegisterT best =
      PickRegisterToFree<RegisterT>(registers.blocked() | reserved);
  DCHECK(best.is_valid());
  DCHECK(!registers.is_blocked(best));
  DropRegisterValue(registers, best);
  registers.AddToFree(best);
  return best;
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::AllocateRegister(
    ValueNode* node, const compiler::InstructionOperand& hint) {
  compiler::InstructionOperand allocation;
  if (node->use_double_register()) {
    if (double_registers_.UnblockedFreeIsEmpty()) {
      FreeUnblockedRegister<DoubleRegister>();
    }
    return double_registers_.AllocateRegister(node, hint);
  } else {
    if (general_registers_.UnblockedFreeIsEmpty()) {
      FreeUnblockedRegister<Register>();
    }
    return general_registers_.AllocateRegister(node, hint);
  }
}

namespace {
template <typename RegisterT>
static RegisterT GetRegisterHint(const compiler::InstructionOperand& hint) {
  if (hint.IsInvalid()) return RegisterT::no_reg();
  DCHECK(hint.IsUnallocated());
  return RegisterT::from_code(
      compiler::UnallocatedOperand::cast(hint).fixed_register_index());
}

}  // namespace

bool StraightForwardRegisterAllocator::IsCurrentNodeLastUseOf(ValueNode* node) {
  return node->live_range().end == current_node_->id();
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::EnsureFreeRegisterAtEnd(
    const compiler::InstructionOperand& hint) {
  RegisterFrameState<RegisterT>& registers = GetRegisterFrameState<RegisterT>();
  // If we still have free registers, pick one of those.
  if (!registers.unblocked_free().is_empty()) return;

  // If the current node is a last use of an input, pick a register containing
  // the input. Prefer the hint register if available.
  RegisterT hint_reg = GetRegisterHint<RegisterT>(hint);
  if (!registers.free().has(hint_reg) && registers.blocked().has(hint_reg) &&
      IsCurrentNodeLastUseOf(registers.GetValue(hint_reg))) {
    DropRegisterValueAtEnd(hint_reg);
    return;
  }
  // Only search in the used-blocked list, since we don't want to assign the
  // result register to a temporary (free + blocked).
  for (RegisterT reg : (registers.blocked() - registers.free())) {
    if (IsCurrentNodeLastUseOf(registers.GetValue(reg))) {
      DropRegisterValueAtEnd(reg);
      return;
    }
  }

  // Pick any input-blocked register based on regular heuristics.
  RegisterT reg = hint.IsInvalid()
                      ? PickRegisterToFree<RegisterT>(registers.empty())
                      : GetRegisterHint<RegisterT>(hint);
  DropRegisterValueAtEnd(reg);
}

compiler::AllocatedOperand
StraightForwardRegisterAllocator::AllocateRegisterAtEnd(ValueNode* node) {
  if (node->use_double_register()) {
    EnsureFreeRegisterAtEnd<DoubleRegister>(node->hint());
    return double_registers_.AllocateRegister(node, node->hint());
  } else {
    EnsureFreeRegisterAtEnd<Register>(node->hint());
    return general_registers_.AllocateRegister(node, node->hint());
  }
}

template <typename RegisterT>
compiler::AllocatedOperand StraightForwardRegisterAllocator::ForceAllocate(
    RegisterFrameState<RegisterT>& registers, RegisterT reg, ValueNode* node) {
  DCHECK(!registers.is_blocked(reg));
  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os()
        << "  forcing " << reg << " to "
        << PrintNodeLabel(graph_labeller(), node) << "...\n";
  }
  if (registers.free().has(reg)) {
    // If it's already free, remove it from the free list.
    registers.RemoveFromFree(reg);
  } else if (registers.GetValue(reg) == node) {
    registers.block(reg);
    return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                      node->GetMachineRepresentation(),
                                      reg.code());
  } else {
    DCHECK(!registers.is_blocked(reg));
    DropRegisterValue(registers, reg);
  }
#ifdef DEBUG
  DCHECK(!registers.free().has(reg));
#endif
  registers.unblock(reg);
  registers.SetValue(reg, node);
  return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                    node->GetMachineRepresentation(),
                                    reg.code());
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::ForceAllocate(
    Register reg, ValueNode* node) {
  DCHECK(!node->use_double_register());
  return ForceAllocate<Register>(general_registers_, reg, node);
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::ForceAllocate(
    DoubleRegister reg, ValueNode* node) {
  DCHECK(node->use_double_register());
  return ForceAllocate<DoubleRegister>(double_registers_, reg, node);
}

compiler::AllocatedOperand StraightForwardRegisterAllocator::ForceAllocate(
    const Input& input, ValueNode* node) {
  if (input.IsDoubleRegister()) {
    DoubleRegister reg = input.AssignedDoubleRegister();
    DropRegisterValueAtEnd(reg);
    return ForceAllocate(reg, node);
  } else {
    Register reg = input.AssignedGeneralRegister();
    DropRegisterValueAtEnd(reg);
    return ForceAllocate(reg, node);
  }
}

namespace {
template <typename RegisterT>
compiler::AllocatedOperand OperandForNodeRegister(ValueNode* node,
                                                  RegisterT reg) {
  return compiler::AllocatedOperand(compiler::LocationOperand::REGISTER,
                                    node->GetMachineRepresentation(),
                                    reg.code());
}
}  // namespace

template <typename RegisterT>
compiler::InstructionOperand
RegisterFrameState<RegisterT>::TryChooseInputRegister(
    ValueNode* node, const compiler::InstructionOperand& hint) {
  RegTList result_registers = node->result_registers<RegisterT>();
  if (result_registers.is_empty()) return compiler::InstructionOperand();

  // Prefer to return an existing blocked register.
  RegTList blocked_result_registers = result_registers & blocked_;
  if (!blocked_result_registers.is_empty()) {
    RegisterT reg = GetRegisterHint<RegisterT>(hint);
    if (!blocked_result_registers.has(reg)) {
      reg = blocked_result_registers.first();
    }
    return OperandForNodeRegister(node, reg);
  }

  RegisterT reg = result_registers.first();
  block(reg);
  return OperandForNodeRegister(node, reg);
}

template <typename RegisterT>
compiler::InstructionOperand
RegisterFrameState<RegisterT>::TryChooseUnblockedInputRegister(
    ValueNode* node) {
  RegTList result_excl_blocked = node->result_registers<RegisterT>() - blocked_;
  if (result_excl_blocked.is_empty()) return compiler::InstructionOperand();
  RegisterT reg = result_excl_blocked.first();
  block(reg);
  return OperandForNodeRegister(node, reg);
}

template <typename RegisterT>
compiler::AllocatedOperand RegisterFrameState<RegisterT>::AllocateRegister(
    ValueNode* node, const compiler::InstructionOperand& hint) {
  DCHECK(!unblocked_free().is_empty());
  RegisterT reg = GetRegisterHint<RegisterT>(hint);
  if (!unblocked_free().has(reg)) {
    reg = unblocked_free().first();
  }
  RemoveFromFree(reg);

  // Allocation succeeded. This might have found an existing allocation.
  // Simply update the state anyway.
  SetValue(reg, node);
  return OperandForNodeRegister(node, reg);
}

template <typename RegisterT>
void StraightForwardRegisterAllocator::AssignFixedTemporaries(
    RegisterFrameState<RegisterT>& registers, NodeBase* node) {
  RegListBase<RegisterT> fixed_temporaries = node->temporaries<RegisterT>();

  // Make sure that any initially set temporaries are definitely free.
  for (RegisterT reg : fixed_temporaries) {
    DCHECK(!registers.is_blocked(reg));
    if (!registers.free().has(reg)) {
      DropRegisterValue(registers, reg);
      registers.AddToFree(reg);
    }
    registers.block(reg);
  }

  if (v8_flags.trace_maglev_regalloc && !fixed_temporaries.is_empty()) {
    if constexpr (std::is_same_v<RegisterT, Register>) {
      printing_visitor_->os()
          << "Fixed Temporaries: " << fixed_temporaries << "\n";
    } else {
      printing_visitor_->os()
          << "Fixed Double Temporaries: " << fixed_temporaries << "\n";
    }
  }

  // After allocating the specific/fixed temporary registers, we empty the node
  // set, so that it is used to allocate only the arbitrary/available temporary
  // register that is going to be inserted in the scratch scope.
  node->temporaries<RegisterT>() = {};
}

void StraightForwardRegisterAllocator::AssignFixedTemporaries(NodeBase* node) {
  AssignFixedTemporaries(general_registers_, node);
  AssignFixedTemporaries(double_registers_, node);
}

namespace {
template <typename RegisterT>
RegListBase<RegisterT> GetReservedRegisters(NodeBase* node_base) {
  if (!node_base->Is<ValueNode>()) return RegListBase<RegisterT>();
  ValueNode* node = node_base->Cast<ValueNode>();
  compiler::UnallocatedOperand operand =
      compiler::UnallocatedOperand::cast(node->result().operand());
  RegListBase<RegisterT> reserved = {node->GetRegisterHint<RegisterT>()};
  if (operand.basic_policy() == compiler::UnallocatedOperand::FIXED_SLOT) {
    DCHECK(node->Is<InitialValue>());
    return reserved;
  }
  if constexpr (std::is_same_v<RegisterT, Register>) {
    if (operand.extended_policy() ==
        compiler::UnallocatedOperand::FIXED_REGISTER) {
      reserved.set(Register::from_code(operand.fixed_register_index()));
    }
  } else {
    static_assert(std::is_same_v<RegisterT, DoubleRegister>);
    if (operand.extended_policy() ==
        compiler::UnallocatedOperand::FIXED_FP_REGISTER) {
      reserved.set(DoubleRegister::from_code(operand.fixed_register_index()));
    }
  }
  return reserved;
}
}  // namespace

template <typename RegisterT>
void StraightForwardRegisterAllocator::AssignArbitraryTemporaries(
    RegisterFrameState<RegisterT>& registers, NodeBase* node) {
  int num_temporaries_needed = node->num_temporaries_needed<RegisterT>();
  if (num_temporaries_needed == 0) return;

  DCHECK_GT(num_temporaries_needed, 0);
  RegListBase<RegisterT> temporaries = node->temporaries<RegisterT>();
  DCHECK(temporaries.is_empty());
  int remaining_temporaries_needed = num_temporaries_needed;

  // If the node is a ValueNode with a fixed result register, we should not
  // assign a temporary to the result register, nor its hint.
  RegListBase<RegisterT> reserved = GetReservedRegisters<RegisterT>(node);
  for (RegisterT reg : (registers.unblocked_free() - reserved)) {
    registers.block(reg);
    DCHECK(!temporaries.has(reg));
    temporaries.set(reg);
    if (--remaining_temporaries_needed == 0) break;
  }

  // Free extra registers if necessary.
  for (int i = 0; i < remaining_temporaries_needed; ++i) {
    DCHECK((registers.unblocked_free() - reserved).is_empty());
    RegisterT reg = FreeUnblockedRegister<RegisterT>(reserved);
    registers.block(reg);
    DCHECK(!temporaries.has(reg));
    temporaries.set(reg);
  }

  DCHECK_GE(temporaries.Count(), num_temporaries_needed);

  node->assign_temporaries(temporaries);
  if (v8_flags.trace_maglev_regalloc) {
    if constexpr (std::is_same_v<RegisterT, Register>) {
      printing_visitor_->os() << "Temporaries: " << temporaries << "\n";
    } else {
      printing_visitor_->os() << "Double Temporaries: " << temporaries << "\n";
    }
  }
}

void StraightForwardRegisterAllocator::AssignArbitraryTemporaries(
    NodeBase* node) {
  AssignArbitraryTemporaries(general_registers_, node);
  AssignArbitraryTemporaries(double_registers_, node);
}

namespace {
template <typename RegisterT>
void ClearRegisterState(RegisterFrameState<RegisterT>& registers) {
  while (!registers.used().is_empty()) {
    RegisterT reg = registers.used().first();
    ValueNode* node = registers.GetValue(reg);
    registers.FreeRegistersUsedBy(node);
    DCHECK(!registers.used().has(reg));
  }
}
}  // namespace

template <typename Function>
void StraightForwardRegisterAllocator::ForEachMergePointRegisterState(
    MergePointRegisterState& merge_point_state, Function&& f) {
  merge_point_state.ForEachGeneralRegister(
      [&](Register reg, RegisterState& state) {
        f(general_registers_, reg, state);
      });
  merge_point_state.ForEachDoubleRegister(
      [&](DoubleRegister reg, RegisterState& state) {
        f(double_registers_, reg, state);
      });
}

void StraightForwardRegisterAllocator::ClearRegisterValues() {
  ClearRegisterState(general_registers_);
  ClearRegisterState(double_registers_);

  // All registers should be free by now.
  DCHECK_EQ(general_registers_.unblocked_free(),
            MaglevAssembler::GetAllocatableRegisters());
  DCHECK_EQ(double_registers_.unblocked_free(),
            MaglevAssembler::GetAllocatableDoubleRegisters());
}

void StraightForwardRegisterAllocator::InitializeRegisterValues(
    MergePointRegisterState& target_state) {
  // First clear the register state.
  ClearRegisterValues();

  // Then fill it in with target information.
  auto fill = [&](auto& registers, auto reg, RegisterState& state) {
    ValueNode* node;
    RegisterMerge* merge;
    LoadMergeState(state, &node, &merge);
    if (node != nullptr) {
      registers.RemoveFromFree(reg);
      registers.SetValue(reg, node);
    } else {
      DCHECK(!state.GetPayload().is_merge);
    }
  };
  ForEachMergePointRegisterState(target_state, fill);

  // SetValue will have blocked registers, unblock them.
  general_registers_.clear_blocked();
  double_registers_.clear_blocked();
}

#ifdef DEBUG

bool StraightForwardRegisterAllocator::IsInRegister(
    MergePointRegisterState& target_state, ValueNode* incoming) {
  bool found = false;
  auto find = [&found, &incoming](auto reg, RegisterState& state) {
    ValueNode* node;
    RegisterMerge* merge;
    LoadMergeState(state, &node, &merge);
    if (node == incoming) found = true;
  };
  if (incoming->use_double_register()) {
    target_state.ForEachDoubleRegister(find);
  } else {
    target_state.ForEachGeneralRegister(find);
  }
  return found;
}

// Returns true if {first_id} or {last_id} are forward-reachable from {current}.
bool StraightForwardRegisterAllocator::IsForwardReachable(
    BasicBlock* start_block, NodeIdT first_id, NodeIdT last_id) {
  ZoneQueue<BasicBlock*> queue(compilation_info_->zone());
  ZoneSet<BasicBlock*> seen(compilation_info_->zone());
  while (!queue.empty()) {
    BasicBlock* curr = queue.front();
    queue.pop();

    if (curr->contains_node_id(first_id) || curr->contains_node_id(last_id)) {
      return true;
    }

    if (curr->control_node()->Is<JumpLoop>()) {
      // A JumpLoop will have a backward edge. Since we are only interested in
      // checking forward reachability, we ignore its successors.
      continue;
    }

    for (BasicBlock* succ : curr->successors()) {
      if (seen.insert(succ).second) {
        queue.push(succ);
      }
      // Since we skipped JumpLoop, only forward edges should remain.
      DCHECK_GT(succ->first_id(), curr->first_id());
    }
  }

  return false;
}

#endif  //  DEBUG

// If a node needs a register before the first call and after the last call of
// the loop, initialize the merge state with a register for this node to avoid
// an unnecessary spill + reload on every iteration.
template <typename RegisterT>
void StraightForwardRegisterAllocator::HoistLoopReloads(
    BasicBlock* target, RegisterFrameState<RegisterT>& registers) {
  for (ValueNode* node : target->reload_hints()) {
    DCHECK(general_registers_.blocked().is_empty());
    if (registers.free().is_empty()) break;
    if (node->has_register()) continue;
    // The value is in a liveness hole, don't try to reload it.
    if (!node->is_loadable()) continue;
    if ((node->use_double_register() && std::is_same_v<RegisterT, Register>) ||
        (!node->use_double_register() &&
         std::is_same_v<RegisterT, DoubleRegister>)) {
      continue;
    }
    RegisterT target_reg = node->GetRegisterHint<RegisterT>();
    if (!registers.free().has(target_reg)) {
      target_reg = registers.free().first();
    }
    compiler::AllocatedOperand target(compiler::LocationOperand::REGISTER,
                                      node->GetMachineRepresentation(),
                                      target_reg.code());
    registers.RemoveFromFree(target_reg);
    registers.SetValueWithoutBlocking(target_reg, node);
    AddMoveBeforeCurrentNode(node, node->loadable_slot(), target);
  }
}

// Same as above with spills: if the node does not need a register before the
// first call and after the last call of the loop, keep it spilled in the merge
// state to avoid an unnecessary reload + spill on every iteration.
void StraightForwardRegisterAllocator::HoistLoopSpills(BasicBlock* target) {
  for (ValueNode* node : target->spill_hints()) {
    if (!node->has_register()) continue;
    // Do not move to a different register, the goal is to keep the value
    // spilled on the back-edge.
    const bool kForceSpill = true;
    if (node->use_double_register()) {
      for (DoubleRegister reg : node->result_registers<DoubleRegister>()) {
        DropRegisterValueAtEnd(reg, kForceSpill);
      }
    } else {
      for (Register reg : node->result_registers<Register>()) {
        DropRegisterValueAtEnd(reg, kForceSpill);
      }
    }
  }
}

void StraightForwardRegisterAllocator::InitializeBranchTargetRegisterValues(
    ControlNode* source, BasicBlock* target) {
  MergePointRegisterState& target_state = target->state()->register_state();
  DCHECK(!target_state.is_initialized());
  auto init = [&](auto& registers, auto reg, RegisterState& state) {
    ValueNode* node = nullptr;
    DCHECK(registers.blocked().is_empty());
    if (!registers.free().has(reg)) {
      node = registers.GetValue(reg);
      if (!IsLiveAtTarget(node, source, target)) node = nullptr;
    }
    state = {node, initialized_node};
  };
  HoistLoopReloads(target, general_registers_);
  HoistLoopReloads(target, double_registers_);
  HoistLoopSpills(target);
  ForEachMergePointRegisterState(target_state, init);
}

void StraightForwardRegisterAllocator::InitializeEmptyBlockRegisterValues(
    ControlNode* source, BasicBlock* target) {
  DCHECK(target->is_edge_split_block());
  MergePointRegisterState* register_state =
      compilation_info_->zone()->New<MergePointRegisterState>();

  DCHECK(!register_state->is_initialized());
  auto init = [&](auto& registers, auto reg, RegisterState& state) {
    ValueNode* node = nullptr;
    DCHECK(registers.blocked().is_empty());
    if (!registers.free().has(reg)) {
      node = registers.GetValue(reg);
      if (!IsLiveAtTarget(node, source, target)) node = nullptr;
    }
    state = {node, initialized_node};
  };
  ForEachMergePointRegisterState(*register_state, init);

  target->set_edge_split_block_register_state(register_state);
}

void StraightForwardRegisterAllocator::MergeRegisterValues(ControlNode* control,
                                                           BasicBlock* target,
                                                           int predecessor_id) {
  if (target->is_edge_split_block()) {
    return InitializeEmptyBlockRegisterValues(control, target);
  }

  MergePointRegisterState& target_state = target->state()->register_state();
  if (!target_state.is_initialized()) {
    // This is the first block we're merging, initialize the values.
    return InitializeBranchTargetRegisterValues(control, target);
  }

  if (v8_flags.trace_maglev_regalloc) {
    printing_visitor_->os() << "Merging registers...\n";
  }

  int predecessor_count = target->state()->predecessor_count();
  auto merge = [&](auto& registers, auto reg, RegisterState& state) {
    ValueNode* node;
    RegisterMerge* merge;
    LoadMergeState(state, &node, &merge);

    // This isn't quite the right machine representation for Int32 nodes, but
    // those are stored in the same registers as Tagged nodes so in this case it
    // doesn't matter.
    MachineRepresentation mach_repr = std::is_same_v<decltype(reg), Register>
                                          ? MachineRepresentation::kTagged
                                          : MachineRepresentation::kFloat64;
    compiler::AllocatedOperand register_info = {
        compiler::LocationOperand::REGISTER, mach_repr, reg.code()};

    ValueNode* incoming = nullptr;
    DCHECK(registers.blocked().is_empty());
    if (!registers.free().has(reg)) {
      incoming = registers.GetValue(reg);
      if (!IsLiveAtTarget(incoming, control, target)) {
        if (v8_flags.trace_maglev_regalloc) {
          printing_visitor_->os() << "  " << reg << " - incoming node "
                                  << PrintNodeLabel(graph_labeller(), incoming)
                                  << " dead at target\n";
        }
        incoming = nullptr;
      }
    }

    if (incoming == node) {
      // We're using the same register as the target already has. If registers
      // are merged, add input information.
      if (v8_flags.trace_maglev_regalloc) {
        if (node) {
          printing_visitor_->os()
              << "  " << reg << " - incoming node same as node: "
              << PrintNodeLabel(graph_labeller(), node) << "\n";
        }
      }
      if (merge) merge->operand(predecessor_id) = register_info;
      return;
    }

    if (node == nullptr) {
      // Don't load new nodes at loop headers.
      if (control->Is<JumpLoop>()) return;
    } else if (!node->is_loadable() && !node->has_register()) {
      // If we have a node already, but can't load it here, we must be in a
      // liveness hole for it, so nuke the merge state.
      // This can only happen for conversion nodes, as they can split and take
      // over the liveness of the node they are converting.
      // TODO(v8:7700): Overeager DCHECK.
      // DCHECK(node->properties().is_conversion());
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os() << "  " << reg << " - can't load "
                                << PrintNodeLabel(graph_labeller(), node)
                                << ", dropping the merge\n";
      }
      // We always need to be able to restore values on JumpLoop since the value
      // is definitely live at the loop header.
      CHECK(!control->Is<JumpLoop>());
      state = {nullptr, initialized_node};
      return;
    }

    if (merge) {
      // The register is already occupied with a different node. Figure out
      // where that node is allocated on the incoming branch.
      merge->operand(predecessor_id) = node->allocation();
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os() << "  " << reg << " - merge: loading "
                                << PrintNodeLabel(graph_labeller(), node)
                                << " from " << node->allocation() << " \n";
      }

      if (incoming != nullptr) {
        // If {incoming} isn't loadable or available in a register, then we are
        // in a liveness hole, and none of its uses should be reachable from
        // {target} (for simplicity/speed, we only check the first and last use
        // though).
        DCHECK_IMPLIES(
            !incoming->is_loadable() && !IsInRegister(target_state, incoming),
            !IsForwardReachable(target, incoming->current_next_use(),
                                incoming->live_range().end));
      }

      return;
    }

    DCHECK_IMPLIES(node == nullptr, incoming != nullptr);
    if (node == nullptr && !incoming->is_loadable()) {
      // If the register is unallocated at the merge point, and the incoming
      // value isn't spilled, that means we must have seen it already in a
      // different register.
      // This maybe not be true for conversion nodes, as they can split and take
      // over the liveness of the node they are converting.
      // TODO(v8:7700): This DCHECK is overeager, {incoming} can be a Phi node
      // containing conversion nodes.
      // DCHECK_IMPLIES(!IsInRegister(target_state, incoming),
      //                incoming->properties().is_conversion());
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os()
            << "  " << reg << " - can't load incoming "
            << PrintNodeLabel(graph_labeller(), incoming) << ", bailing out\n";
      }
      return;
    }

    const size_t size = sizeof(RegisterMerge) +
                        predecessor_count * sizeof(compiler::AllocatedOperand);
    void* buffer = compilation_info_->zone()->Allocate<void*>(size);
    merge = new (buffer) RegisterMerge();
    merge->node = node == nullptr ? incoming : node;

    // If the register is unallocated at the merge point, allocation so far
    // is the loadable slot for the incoming value. Otherwise all incoming
    // branches agree that the current node is in the register info.
    compiler::InstructionOperand info_so_far =
        node == nullptr ? incoming->loadable_slot() : register_info;

    // Initialize the entire array with info_so_far since we don't know in
    // which order we've seen the predecessors so far. Predecessors we
    // haven't seen yet will simply overwrite their entry later.
    for (int i = 0; i < predecessor_count; i++) {
      merge->operand(i) = info_so_far;
    }
    // If the register is unallocated at the merge point, fill in the
    // incoming value. Otherwise find the merge-point node in the incoming
    // state.
    if (node == nullptr) {
      merge->operand(predecessor_id) = register_info;
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os() << "  " << reg << " - new merge: loading new "
                                << PrintNodeLabel(graph_labeller(), incoming)
                                << " from " << register_info << " \n";
      }
    } else {
      merge->operand(predecessor_id) = node->allocation();
      if (v8_flags.trace_maglev_regalloc) {
        printing_visitor_->os() << "  " << reg << " - new merge: loading "
                                << PrintNodeLabel(graph_labeller(), node)
                                << " from " << node->allocation() << " \n";
      }
    }
    state = {merge, initialized_merge};
  };
  ForEachMergePointRegisterState(target_state, merge);
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
