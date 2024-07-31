// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-compiler.h"

#include <iomanip>
#include <ostream>
#include <type_traits>

#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/threaded-list.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/heap-refs.h"
#include "src/compiler/js-heap-broker.h"
#include "src/deoptimizer/frame-translation-builder.h"
#include "src/execution/frames.h"
#include "src/ic/handler-configuration.h"
#include "src/maglev/maglev-basic-block.h"
#include "src/maglev/maglev-code-generator.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph-verifier.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir-inl.h"
#include "src/maglev/maglev-ir.h"
#include "src/maglev/maglev-phi-representation-selector.h"
#include "src/maglev/maglev-regalloc-data.h"
#include "src/maglev/maglev-regalloc.h"
#include "src/objects/code-inl.h"
#include "src/objects/js-function.h"
#include "src/utils/identity-map.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace maglev {

class ValueLocationConstraintProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

#define DEF_PROCESS_NODE(NAME)                                      \
  ProcessResult Process(NAME* node, const ProcessingState& state) { \
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
  void PreProcessBasicBlock(BasicBlock* block) {}

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
  void PreProcessBasicBlock(BasicBlock* block) {}

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
    if (deopt_frame->type() == DeoptFrame::FrameType::kInterpretedFrame) {
      if (&deopt_frame->as_interpreted().unit() == last_seen_unit_) return;
      last_seen_unit_ = &deopt_frame->as_interpreted().unit();
    }

    int frame_size = 0;
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

thread_local MaglevGraphLabeller* labeller_;

template <typename NodeT>
constexpr bool CanBeStoreToNonEscapedObject() {
  return std::is_same_v<NodeT, StoreMap> ||
         std::is_same_v<NodeT, StoreTaggedFieldWithWriteBarrier> ||
         std::is_same_v<NodeT, StoreTaggedFieldNoWriteBarrier> ||
         std::is_same_v<NodeT, StoreFloat64>;
}

class AnyUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

  ProcessResult Process(InlinedAllocation* node, const ProcessingState& state) {
    if (!node->HasEscaped()) {
      node->input(0).node()->remove_use();
      return ProcessResult::kRemove;
    }
    escaped_allocations_.push_back(node);
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        if (!node->unused_inputs_were_visited()) {
          DropInputUses(node);
        }
        return ProcessResult::kRemove;
      }
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (InlinedAllocation* object =
              node->input(0).node()->template TryCast<InlinedAllocation>()) {
        if (!object->HasEscaped()) {
          // Make sure to remove the uses of the other inputs.
          for (int i = 1; i < node->input_count(); i++) {
            DropInputUses(node->Node::input(i));
          }
          return ProcessResult::kRemove;
        }
      }
    }

    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {
    // Somes escaped allocation might have been marked as non-escaping.
    // Fix the AllocationBlock usages. They will be removed in the
    // DeadNodeSweepingProcessor.
    for (InlinedAllocation* alloc : escaped_allocations_) {
      if (!alloc->HasEscaped()) {
        alloc->allocation_block().node()->remove_use();
      }
    }
  }

 private:
  void DropInputUses(Input& input) {
    ValueNode* input_node = input.node();
    if (input_node->properties().is_required_when_unused() &&
        !input_node->Is<ArgumentsElements>())
      return;
    input_node->remove_use();
    if (!input_node->is_used() && !input_node->unused_inputs_were_visited()) {
      DropInputUses(input_node);
    }
  }

  void DropInputUses(ValueNode* node) {
    for (Input& input : *node) {
      DropInputUses(input);
    }
    DCHECK(!node->properties().can_eager_deopt());
    DCHECK(!node->properties().can_lazy_deopt());
    node->mark_unused_inputs_visited();
  }

  std::vector<InlinedAllocation*> escaped_allocations_;
};

class DeadNodeSweepingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  void PreProcessBasicBlock(BasicBlock* block) {}

  ProcessResult Process(AllocationBlock* node, const ProcessingState& state) {
    if (!node->is_used()) return ProcessResult::kRemove;
    // All non-escaped InlinedAllocations were already removed. We can now set
    // the offsets of the escaped ones.
    // Note: this need to be done before ValueLocationConstraintProcessor, since
    // it access the allocation offsets.
    int size = 0;
    for (auto alloc : node->allocation_list()) {
      if (alloc->HasEscaped()) {
        alloc->set_offset(size);
        size += alloc->size();
      }
    }
    // ... and update its size.
    node->set_size(size);
    return ProcessResult::kContinue;
  }

  ProcessResult Process(InlinedAllocation* node, const ProcessingState& state) {
    // Remove inlined allocation that became non-escaping.
    if (!node->HasEscaped()) {
      return ProcessResult::kRemove;
    }
    return ProcessResult::kContinue;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    if constexpr (IsValueNode(Node::opcode_of<NodeT>) &&
                  (!NodeT::kProperties.is_required_when_unused() ||
                   std::is_same_v<ArgumentsElements, NodeT>)) {
      if (!node->is_used()) {
        if constexpr (std::is_same_v<NodeT, Phi>) {
          // The UseMarkingProcessor will clear dead forward jump Phis eagerly,
          // so the only dead phis that should remain are loop and exception
          // phis.
          DCHECK(node->is_loop_phi() || node->is_exception_phi());
        }
        return ProcessResult::kRemove;
      }
      return ProcessResult::kContinue;
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (InlinedAllocation* object =
              node->input(0).node()->template TryCast<InlinedAllocation>()) {
        if (!object->HasEscaped()) {
          // We might still have stores that have not been cleared for
          // allocations that were initially escaping and then became
          // non-escaping.
          return ProcessResult::kRemove;
        }
      }
    }
    return ProcessResult::kContinue;
  }
};

class LiveRangeAndNextUseProcessor {
 public:
  explicit LiveRangeAndNextUseProcessor(MaglevCompilationInfo* compilation_info)
      : compilation_info_(compilation_info) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) { DCHECK(loop_used_nodes_.empty()); }
  void PreProcessBasicBlock(BasicBlock* block) {
    if (!block->has_state()) return;
    if (block->state()->is_loop()) {
      loop_used_nodes_.push_back(
          LoopUsedNodes{{}, kInvalidNodeId, kInvalidNodeId, block});
    }
  }

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
    if (InlinedAllocation* alloc = node->TryCast<InlinedAllocation>()) {
      if (!alloc->HasEscaped()) {
        // No need to mark use. The allocation has survived as a deopt input.
        return;
      }
    }

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
    detail::DeepForEachInput(deopt_info,
                             [&](ValueNode*& node, InputLocation* input) {
                               if (node->Is<Identity>()) {
                                 node = node->input(0).node();
                               }
                               MarkUse(node, use_id, input, loop_used_nodes);
                             });
  }
  void MarkCheckpointNodes(NodeBase* node, LazyDeoptInfo* deopt_info,
                           LoopUsedNodes* loop_used_nodes,
                           const ProcessingState& state) {
    int use_id = node->id();
    detail::DeepForEachInput(deopt_info,
                             [&](ValueNode*& node, InputLocation* input) {
                               if (node->Is<Identity>()) {
                                 node = node->input(0).node();
                               }
                               MarkUse(node, use_id, input, loop_used_nodes);
                             });
  }

  MaglevCompilationInfo* compilation_info_;
  uint32_t next_node_id_ = kFirstValidNodeId;
  std::vector<LoopUsedNodes> loop_used_nodes_;
};

// static
bool MaglevCompiler::Compile(LocalIsolate* local_isolate,
                             MaglevCompilationInfo* compilation_info) {
  compiler::CurrentHeapBrokerScope current_broker(compilation_info->broker());
  Graph* graph =
      Graph::New(compilation_info->zone(),
                 compilation_info->toplevel_compilation_unit()->is_osr());

  // Build graph.
  if (v8_flags.print_maglev_code || v8_flags.code_comments ||
      v8_flags.print_maglev_graph || v8_flags.print_maglev_graphs ||
      v8_flags.trace_maglev_graph_building ||
      v8_flags.trace_maglev_phi_untagging || v8_flags.trace_maglev_regalloc) {
    compilation_info->set_graph_labeller(labeller_ = new MaglevGraphLabeller());
  }

  {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());

    if (v8_flags.print_maglev_code || v8_flags.print_maglev_graph ||
        v8_flags.print_maglev_graphs || v8_flags.trace_maglev_graph_building ||
        v8_flags.trace_maglev_phi_untagging || v8_flags.trace_maglev_regalloc) {
      MaglevCompilationUnit* top_level_unit =
          compilation_info->toplevel_compilation_unit();
      std::cout << "Compiling " << Brief(*compilation_info->toplevel_function())
                << " with Maglev\n";
      BytecodeArray::Disassemble(top_level_unit->bytecode().object(),
                                 std::cout);
      if (v8_flags.maglev_print_feedback) {
        Print(*top_level_unit->feedback().object(), std::cout);
      }
    }

    MaglevGraphBuilder graph_builder(
        local_isolate, compilation_info->toplevel_compilation_unit(), graph);

    {
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.Maglev.GraphBuilding");
      graph_builder.Build();

      if (v8_flags.print_maglev_graphs) {
        std::cout << "\nAfter graph buiding" << std::endl;
        PrintGraph(std::cout, compilation_info, graph);
      }
    }

    if (v8_flags.maglev_untagged_phis) {
      TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                   "V8.Maglev.PhiUntagging");

      GraphProcessor<MaglevPhiRepresentationSelector> representation_selector(
          &graph_builder);
      representation_selector.ProcessGraph(graph);

      if (v8_flags.print_maglev_graphs) {
        std::cout << "\nAfter Phi untagging" << std::endl;
        PrintGraph(std::cout, compilation_info, graph);
      }
    }
  }

#ifdef DEBUG
  {
    GraphProcessor<MaglevGraphVerifier> verifier(compilation_info);
    verifier.ProcessGraph(graph);
  }
#endif

  {
    // Post-hoc optimisation:
    //   - Dead node marking
    //   - Cleaning up identity nodes
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.DeadCodeMarking");
    GraphMultiProcessor<AnyUseMarkingProcessor> processor;
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After use marking" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }

#ifdef DEBUG
  {
    GraphProcessor<MaglevGraphVerifier> verifier(compilation_info);
    verifier.ProcessGraph(graph);
  }
#endif

  {
    // Preprocessing for register allocation and code gen:
    //   - Remove dead nodes
    //   - Collect input/output location constraints
    //   - Find the maximum number of stack arguments passed to calls
    //   - Collect use information, for SSA liveness and next-use distance.
    //   - Mark
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.NodeProcessing");
    GraphMultiProcessor<DeadNodeSweepingProcessor,
                        ValueLocationConstraintProcessor, MaxCallDepthProcessor,
                        LiveRangeAndNextUseProcessor,
                        DecompressedUseMarkingProcessor>
        processor(LiveRangeAndNextUseProcessor{compilation_info});
    processor.ProcessGraph(graph);
  }

  if (v8_flags.print_maglev_graphs) {
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::cout << "After register allocation pre-processing" << std::endl;
    PrintGraph(std::cout, compilation_info, graph);
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.RegisterAllocation");
    StraightForwardRegisterAllocator allocator(compilation_info, graph);

    if (v8_flags.print_maglev_graph || v8_flags.print_maglev_graphs) {
      UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
      std::cout << "After register allocation" << std::endl;
      PrintGraph(std::cout, compilation_info, graph);
    }
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CodeAssembly");
    UnparkedScopeIfOnBackground unparked_scope(local_isolate->heap());
    std::unique_ptr<MaglevCodeGenerator> code_generator =
        std::make_unique<MaglevCodeGenerator>(local_isolate, compilation_info,
                                              graph);
    bool success = code_generator->Assemble();
    if (!success) {
      return false;
    }

    // Stash the compiled code_generator on the compilation info.
    compilation_info->set_code_generator(std::move(code_generator));
  }

  return true;
}

// static
MaybeHandle<Code> MaglevCompiler::GenerateCode(
    Isolate* isolate, MaglevCompilationInfo* compilation_info) {
  compiler::CurrentHeapBrokerScope current_broker(compilation_info->broker());
  MaglevCodeGenerator* const code_generator =
      compilation_info->code_generator();
  DCHECK_NOT_NULL(code_generator);

  Handle<Code> code;
  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CodeGeneration");
    if (compilation_info->is_detached() ||
        !code_generator->Generate(isolate).ToHandle(&code)) {
      compilation_info->toplevel_compilation_unit()
          ->shared_function_info()
          .object()
          ->set_maglev_compilation_failed(true);
      return {};
    }
  }

  {
    TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
                 "V8.Maglev.CommittingDependencies");
    if (!compilation_info->broker()->dependencies()->Commit(code)) {
      // Don't `set_maglev_compilation_failed` s.t. we may reattempt
      // compilation.
      // TODO(v8:7700): Make this more robust, i.e.: don't recompile endlessly,
      // and possibly attempt to recompile as early as possible.
      return {};
    }
  }

  if (v8_flags.print_maglev_code) {
    Print(*code);
  }

  return code;
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
