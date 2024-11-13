// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_PRE_REGALLOC_CODEGEN_PROCESSORS_H_
#define V8_MAGLEV_MAGLEV_PRE_REGALLOC_CODEGEN_PROCESSORS_H_

#include "src/codegen/register-configuration.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"

namespace v8::internal::maglev {

class ValueLocationConstraintProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

#define DEF_PROCESS_NODE(NAME)                                      \
  ProcessResult Process(NAME* node, const ProcessingState& state) { \
    node->InitTemporaries();                                        \
    node->SetValueLocationConstraints();                            \
    return ProcessResult::kContinue;                                \
  }
  NODE_BASE_LIST(DEF_PROCESS_NODE)
#undef DEF_PROCESS_NODE
};

class DecompressedUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
#ifdef V8_COMPRESS_POINTERS
    node->MarkTaggedInputsAsDecompressing();
#endif
    return ProcessResult::kContinue;
  }
};

class MaxCallDepthProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {
    graph->set_max_call_stack_args(max_call_stack_args_);
    graph->set_max_deopted_stack_size(max_deopted_stack_size_);
  }
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (NodeT::kProperties.is_call() ||
                  NodeT::kProperties.needs_register_snapshot()) {
      int node_stack_args = node->MaxCallStackArgs();
      if constexpr (NodeT::kProperties.needs_register_snapshot()) {
        // Pessimistically assume that we'll push all registers in deferred
        // calls.
        node_stack_args +=
            kAllocatableGeneralRegisterCount + kAllocatableDoubleRegisterCount;
      }
      max_call_stack_args_ = std::max(max_call_stack_args_, node_stack_args);
    }
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      UpdateMaxDeoptedStackSize(node->eager_deopt_info());
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      UpdateMaxDeoptedStackSize(node->lazy_deopt_info());
    }
    return ProcessResult::kContinue;
  }

 private:
  void UpdateMaxDeoptedStackSize(DeoptInfo* deopt_info) {
    const DeoptFrame* deopt_frame = &deopt_info->top_frame();
    int frame_size = 0;
    if (deopt_frame->type() == DeoptFrame::FrameType::kInterpretedFrame) {
      if (&deopt_frame->as_interpreted().unit() == last_seen_unit_) return;
      last_seen_unit_ = &deopt_frame->as_interpreted().unit();
      frame_size = deopt_frame->as_interpreted().unit().max_arguments() *
                   kSystemPointerSize;
    }

    do {
      frame_size += ConservativeFrameSize(deopt_frame);
      deopt_frame = deopt_frame->parent();
    } while (deopt_frame != nullptr);
    max_deopted_stack_size_ = std::max(frame_size, max_deopted_stack_size_);
  }
  int ConservativeFrameSize(const DeoptFrame* deopt_frame) {
    switch (deopt_frame->type()) {
      case DeoptFrame::FrameType::kInterpretedFrame: {
        auto info = UnoptimizedFrameInfo::Conservative(
            deopt_frame->as_interpreted().unit().parameter_count(),
            deopt_frame->as_interpreted().unit().register_count());
        return info.frame_size_in_bytes();
      }
      case DeoptFrame::FrameType::kConstructInvokeStubFrame: {
        return FastConstructStubFrameInfo::Conservative().frame_size_in_bytes();
      }
      case DeoptFrame::FrameType::kInlinedArgumentsFrame: {
        return std::max(
            0,
            static_cast<int>(
                deopt_frame->as_inlined_arguments().arguments().size() -
                deopt_frame->as_inlined_arguments().unit().parameter_count()) *
                kSystemPointerSize);
      }
      case DeoptFrame::FrameType::kBuiltinContinuationFrame: {
        // PC + FP + Closure + Params + Context
        const RegisterConfiguration* config = RegisterConfiguration::Default();
        auto info = BuiltinContinuationFrameInfo::Conservative(
            deopt_frame->as_builtin_continuation().parameters().length(),
            Builtins::CallInterfaceDescriptorFor(
                deopt_frame->as_builtin_continuation().builtin_id()),
            config);
        return info.frame_size_in_bytes();
      }
    }
  }

  int max_call_stack_args_ = 0;
  int max_deopted_stack_size_ = 0;
  // Optimize UpdateMaxDeoptedStackSize to not re-calculate if it sees the same
  // compilation unit multiple times in a row.
  const MaglevCompilationUnit* last_seen_unit_ = nullptr;
};

class LiveRangeAndNextUseProcessor {
 public:
  explicit LiveRangeAndNextUseProcessor(MaglevCompilationInfo* compilation_info)
      : compilation_info_(compilation_info) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) { DCHECK(loop_used_nodes_.empty()); }
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    if (!block->has_state()) return BlockProcessResult::kContinue;
    if (block->state()->is_loop()) {
      loop_used_nodes_.push_back(
          LoopUsedNodes{{}, kInvalidNodeId, kInvalidNodeId, block});
    }
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    node->set_id(next_node_id_++);
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    if (loop_used_nodes && node->properties().is_call() &&
        loop_used_nodes->header->has_state()) {
      if (loop_used_nodes->first_call == kInvalidNodeId) {
        loop_used_nodes->first_call = node->id();
      }
      loop_used_nodes->last_call = node->id();
    }
    MarkInputUses(node, state);
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  void MarkInputUses(NodeT* node, const ProcessingState& state) {
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    // Mark input uses in the same order as inputs are assigned in the register
    // allocator (see StraightForwardRegisterAllocator::AssignInputs).
    node->ForAllInputsInRegallocAssignmentOrder(
        [&](NodeBase::InputAllocationPolicy, Input* input) {
          MarkUse(input->node(), node->id(), input, loop_used_nodes);
        });
    if constexpr (NodeT::kProperties.can_eager_deopt()) {
      MarkCheckpointNodes(node, node->eager_deopt_info(), loop_used_nodes,
                          state);
    }
    if constexpr (NodeT::kProperties.can_lazy_deopt()) {
      MarkCheckpointNodes(node, node->lazy_deopt_info(), loop_used_nodes,
                          state);
    }
  }

  void MarkInputUses(Phi* node, const ProcessingState& state) {
    // Don't mark Phi uses when visiting the node, because of loop phis.
    // Instead, they'll be visited while processing Jump/JumpLoop.
  }

  // Specialize the two unconditional jumps to extend their Phis' inputs' live
  // ranges.

  void MarkInputUses(JumpLoop* node, const ProcessingState& state) {
    int i = state.block()->predecessor_id();
    BasicBlock* target = node->target();
    uint32_t use = node->id();

    DCHECK(!loop_used_nodes_.empty());
    LoopUsedNodes loop_used_nodes = std::move(loop_used_nodes_.back());
    loop_used_nodes_.pop_back();

    LoopUsedNodes* outer_loop_used_nodes = GetCurrentLoopUsedNodes();

    if (target->has_phi()) {
      for (Phi* phi : *target->phis()) {
        DCHECK(phi->is_used());
        ValueNode* input = phi->input(i).node();
        MarkUse(input, use, &phi->input(i), outer_loop_used_nodes);
      }
    }

    DCHECK_EQ(loop_used_nodes.header, target);
    if (!loop_used_nodes.used_nodes.empty()) {
      // Try to avoid unnecessary reloads or spills across the back-edge based
      // on use positions and calls inside the loop.
      ZonePtrList<ValueNode>& reload_hints =
          loop_used_nodes.header->reload_hints();
      ZonePtrList<ValueNode>& spill_hints =
          loop_used_nodes.header->spill_hints();
      for (auto p : loop_used_nodes.used_nodes) {
        // If the node is used before the first call and after the last call,
        // keep it in a register across the back-edge.
        if (p.second.first_register_use != kInvalidNodeId &&
            (loop_used_nodes.first_call == kInvalidNodeId ||
             (p.second.first_register_use <= loop_used_nodes.first_call &&
              p.second.last_register_use > loop_used_nodes.last_call))) {
          reload_hints.Add(p.first, compilation_info_->zone());
        }
        // If the node is not used, or used after the first call and before the
        // last call, keep it spilled across the back-edge.
        if (p.second.first_register_use == kInvalidNodeId ||
            (loop_used_nodes.first_call != kInvalidNodeId &&
             p.second.first_register_use > loop_used_nodes.first_call &&
             p.second.last_register_use <= loop_used_nodes.last_call)) {
          spill_hints.Add(p.first, compilation_info_->zone());
        }
      }

      // Uses of nodes in this loop may need to propagate to an outer loop, so
      // that they're lifetime is extended there too.
      // TODO(leszeks): We only need to extend the lifetime in one outermost
      // loop, allow nodes to be "moved" between lifetime extensions.
      base::Vector<Input> used_node_inputs =
          compilation_info_->zone()->AllocateVector<Input>(
              loop_used_nodes.used_nodes.size());
      int i = 0;
      for (auto& [used_node, info] : loop_used_nodes.used_nodes) {
        Input* input = new (&used_node_inputs[i++]) Input(used_node);
        MarkUse(used_node, use, input, outer_loop_used_nodes);
      }
      node->set_used_nodes(used_node_inputs);
    }
  }
  void MarkInputUses(Jump* node, const ProcessingState& state) {
    MarkJumpInputUses(node->id(), node->target(), state);
  }
  void MarkInputUses(CheckpointedJump* node, const ProcessingState& state) {
    MarkJumpInputUses(node->id(), node->target(), state);
  }
  void MarkJumpInputUses(uint32_t use, BasicBlock* target,
                         const ProcessingState& state) {
    int i = state.block()->predecessor_id();
    if (!target->has_phi()) return;
    LoopUsedNodes* loop_used_nodes = GetCurrentLoopUsedNodes();
    Phi::List& phis = *target->phis();
    for (auto it = phis.begin(); it != phis.end();) {
      Phi* phi = *it;
      if (!phi->is_used()) {
        // Skip unused phis -- we're processing phis out of order with the dead
        // node sweeping processor, so we will still observe unused phis here.
        // We can eagerly remove them while we're at it so that the dead node
        // sweeping processor doesn't have to revisit them.
        it = phis.RemoveAt(it);
      } else {
        ValueNode* input = phi->input(i).node();
        MarkUse(input, use, &phi->input(i), loop_used_nodes);
        ++it;
      }
    }
  }

 private:
  struct NodeUse {
    // First and last register use inside a loop.
    NodeIdT first_register_use;
    NodeIdT last_register_use;
  };

  struct LoopUsedNodes {
    std::map<ValueNode*, NodeUse> used_nodes;
    NodeIdT first_call;
    NodeIdT last_call;
    BasicBlock* header;
  };

  LoopUsedNodes* GetCurrentLoopUsedNodes() {
    if (loop_used_nodes_.empty()) return nullptr;
    return &loop_used_nodes_.back();
  }

  void MarkUse(ValueNode* node, uint32_t use_id, InputLocation* input,
               LoopUsedNodes* loop_used_nodes) {
    DCHECK(!node->Is<Identity>());

    node->record_next_use(use_id, input);

    // If we are in a loop, loop_used_nodes is non-null. In this case, check if
    // the incoming node is from outside the loop, and make sure to extend its
    // lifetime to the loop end if yes.
    if (loop_used_nodes) {
      // If the node's id is smaller than the smallest id inside the loop, then
      // it must have been created before the loop. This means that it's alive
      // on loop entry, and therefore has to be alive across the loop back edge
      // too.
      if (node->id() < loop_used_nodes->header->first_id()) {
        auto [it, info] = loop_used_nodes->used_nodes.emplace(
            node, NodeUse{kInvalidNodeId, kInvalidNodeId});
        if (input->operand().IsUnallocated()) {
          const auto& operand =
              compiler::UnallocatedOperand::cast(input->operand());
          if (operand.HasRegisterPolicy() || operand.HasFixedRegisterPolicy() ||
              operand.HasFixedFPRegisterPolicy()) {
            if (it->second.first_register_use == kInvalidNodeId) {
              it->second.first_register_use = use_id;
            }
            it->second.last_register_use = use_id;
          }
        }
      }
    }
  }

  void MarkCheckpointNodes(NodeBase* node, EagerDeoptInfo* deopt_info,
                           LoopUsedNodes* loop_used_nodes,
                           const ProcessingState& state) {
    int use_id = node->id();
    detail::DeepForEachInputRemovingIdentities(
        deopt_info, [&](ValueNode* node, InputLocation* input) {
          MarkUse(node, use_id, input, loop_used_nodes);
        });
  }
  void MarkCheckpointNodes(NodeBase* node, LazyDeoptInfo* deopt_info,
                           LoopUsedNodes* loop_used_nodes,
                           const ProcessingState& state) {
    int use_id = node->id();
    detail::DeepForEachInputRemovingIdentities(
        deopt_info, [&](ValueNode* node, InputLocation* input) {
          MarkUse(node, use_id, input, loop_used_nodes);
        });
  }

  MaglevCompilationInfo* compilation_info_;
  uint32_t next_node_id_ = kFirstValidNodeId;
  std::vector<LoopUsedNodes> loop_used_nodes_;
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_PRE_REGALLOC_CODEGEN_PROCESSORS_H_
