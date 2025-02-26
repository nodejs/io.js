// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_
#define V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_

#include "src/compiler/heap-refs.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-printer.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/js-function.h"

namespace v8::internal::maglev {

// Optimizations involving loops which cannot be done at graph building time.
// Currently mainly loop invariant code motion.
class LoopOptimizationProcessor {
 public:
  explicit LoopOptimizationProcessor(MaglevGraphBuilder* builder)
      : zone(builder->zone()) {
    was_deoptimized =
        builder->compilation_unit()->feedback().was_once_deoptimized();
  }

  void PreProcessGraph(Graph* graph) {}
  void PostPhiProcessing() {}

  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    current_block = block;
    if (current_block->is_loop()) {
      loop_effects = current_block->state()->loop_effects();
      if (loop_effects) return BlockProcessResult::kContinue;
    } else {
      // TODO(olivf): Some dominance analysis would allow us to keep loop
      // effects longer than just the first block of the loop.
      loop_effects = nullptr;
    }
    return BlockProcessResult::kSkip;
  }

  bool IsLoopPhi(Node* input) {
    DCHECK(current_block->is_loop());
    if (auto phi = input->TryCast<Phi>()) {
      if (phi->is_loop_phi() && phi->merge_state() == current_block->state()) {
        return true;
      }
    }
    return false;
  }

  bool CanHoist(Node* candidate) {
    DCHECK_EQ(candidate->input_count(), 1);
    DCHECK(current_block->is_loop());
    ValueNode* input = candidate->input(0).node();
    DCHECK(!IsLoopPhi(input));
    // For hoisting an instruction we need:
    // * A unique loop entry block.
    // * Inputs live before the loop (i.e., not defined inside the loop).
    // * No hoisting over checks (done eagerly by clearing loop_effects).
    // TODO(olivf): We should enforce loops having a unique entry block at graph
    // building time.
    if (current_block->predecessor_count() != 2) return false;
    BasicBlock* loop_entry = current_block->predecessor_at(0);
    if (loop_entry->successors().size() != 1) {
      return false;
    }
    if (IsConstantNode(input->opcode())) return true;
    return input->owner() != current_block;
  }

  ProcessResult Process(LoadTaggedFieldForContextSlot* ltf,
                        const ProcessingState& state) {
    DCHECK(loop_effects);
    ValueNode* object = ltf->object_input().node();
    if (IsLoopPhi(object)) {
      return ProcessResult::kContinue;
    }
    auto key = std::tuple{object, ltf->offset()};
    if (!loop_effects->may_have_aliasing_contexts &&
        !loop_effects->unstable_aspects_cleared &&
        !loop_effects->context_slot_written.count(key) && CanHoist(ltf)) {
      return ProcessResult::kHoist;
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(LoadTaggedFieldForProperty* ltf,
                        const ProcessingState& state) {
    return ProcessNamedLoad(ltf, ltf->object_input().node(), ltf->name());
  }

  ProcessResult Process(StringLength* len, const ProcessingState& state) {
    return ProcessNamedLoad(
        len, len->object_input().node(),
        KnownNodeAspects::LoadedPropertyMapKey::StringLength());
  }

  ProcessResult Process(LoadTypedArrayLength* len,
                        const ProcessingState& state) {
    return ProcessNamedLoad(
        len, len->receiver_input().node(),
        KnownNodeAspects::LoadedPropertyMapKey::TypedArrayLength());
  }

  ProcessResult ProcessNamedLoad(Node* load, ValueNode* object,
                                 KnownNodeAspects::LoadedPropertyMapKey name) {
    DCHECK(!load->properties().can_deopt());
    if (!loop_effects) return ProcessResult::kContinue;
    if (IsLoopPhi(object)) {
      return ProcessResult::kContinue;
    }
    if (!loop_effects->unstable_aspects_cleared &&
        !loop_effects->keys_cleared.count(name) &&
        !loop_effects->objects_written.count(object) && CanHoist(load)) {
      return ProcessResult::kHoist;
    }
    return ProcessResult::kContinue;
  }

  ProcessResult Process(CheckMaps* maps, const ProcessingState& state) {
    DCHECK(loop_effects);
    // Hoisting a check out of a loop can cause it to trigger more than actually
    // needed (i.e., if the loop is executed 0 times). This could lead to
    // deoptimization loops as there is no feedback to learn here. Thus, we
    // abort this optimization if the function deoptimized previously. Also, if
    // hoisting of this check fails we need to abort (and not continue) to
    // ensure we are not hoisting other instructions over it.
    if (was_deoptimized) return ProcessResult::kSkipBlock;
    ValueNode* object = maps->receiver_input().node();
    if (IsLoopPhi(object)) {
      return ProcessResult::kSkipBlock;
    }
    if (!loop_effects->unstable_aspects_cleared && CanHoist(maps)) {
      if (auto j = current_block->predecessor_at(0)
                       ->control_node()
                       ->TryCast<CheckpointedJump>()) {
        maps->SetEagerDeoptInfo(zone, j->eager_deopt_info()->top_frame(),
                                maps->eager_deopt_info()->feedback_to_update());
        return ProcessResult::kHoist;
      }
    }
    return ProcessResult::kSkipBlock;
  }

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    // Ensure we are not hoisting over checks.
    if (node->properties().can_eager_deopt()) {
      loop_effects = nullptr;
      return ProcessResult::kSkipBlock;
    }
    return ProcessResult::kContinue;
  }

  void PostProcessGraph(Graph* graph) {}

  Zone* zone;
  BasicBlock* current_block;
  const LoopEffects* loop_effects;
  bool was_deoptimized;
};

template <typename NodeT>
constexpr bool CanBeStoreToNonEscapedObject() {
  return std::is_same_v<NodeT, StoreMap> ||
         std::is_same_v<NodeT, StoreTaggedFieldWithWriteBarrier> ||
         std::is_same_v<NodeT, StoreTaggedFieldNoWriteBarrier> ||
         std::is_same_v<NodeT, StoreTrustedPointerFieldWithWriteBarrier> ||
         std::is_same_v<NodeT, StoreFloat64>;
}

class AnyUseMarkingProcessor {
 public:
  void PreProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

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
      if (node->input(0).node()->template Is<InlinedAllocation>()) {
        stores_to_allocations_.push_back(node);
      }
    }

    return ProcessResult::kContinue;
  }

#ifdef DEBUG
  ProcessResult Process(Dead* node, const ProcessingState& state) {
    UNREACHABLE();
  }
#endif  // DEBUG

  void PostProcessGraph(Graph* graph) {
    RunEscapeAnalysis(graph);
    DropUseOfValueInStoresToCapturedAllocations();
  }

 private:
  std::vector<Node*> stores_to_allocations_;

  void EscapeAllocation(Graph* graph, InlinedAllocation* alloc,
                        Graph::SmallAllocationVector& deps) {
    if (alloc->HasBeenAnalysed() && alloc->HasEscaped()) return;
    alloc->SetEscaped();
    for (auto dep : deps) {
      EscapeAllocation(graph, dep,
                       graph->allocations_escape_map().find(dep)->second);
    }
  }

  void VerifyEscapeAnalysis(Graph* graph) {
#ifdef DEBUG
    for (auto it : graph->allocations_escape_map()) {
      auto alloc = it.first;
      DCHECK(alloc->HasBeenAnalysed());
      if (alloc->HasEscaped()) {
        for (auto dep : it.second) {
          DCHECK(dep->HasEscaped());
        }
      }
    }
#endif  // DEBUG
  }

  void RunEscapeAnalysis(Graph* graph) {
    for (auto it : graph->allocations_escape_map()) {
      auto alloc = it.first;
      if (alloc->HasBeenAnalysed()) continue;
      // Check if all its uses are non escaping.
      if (alloc->IsEscaping()) {
        // Escape this allocation and all its dependencies.
        EscapeAllocation(graph, alloc, it.second);
      } else {
        // Try to capture the allocation. This can still change if a escaped
        // allocation has this value as one of its dependencies.
        alloc->SetElided();
      }
    }
    // Check that we've reached a fixpoint.
    VerifyEscapeAnalysis(graph);
  }

  void DropUseOfValueInStoresToCapturedAllocations() {
    for (Node* node : stores_to_allocations_) {
      InlinedAllocation* alloc =
          node->input(0).node()->Cast<InlinedAllocation>();
      // Since we don't analyze if allocations will escape until a fixpoint,
      // this could drop an use of an allocation and turn it non-escaping.
      if (alloc->HasBeenElided()) {
        // Skip first input.
        for (int i = 1; i < node->input_count(); i++) {
          DropInputUses(node->input(i));
        }
      }
    }
  }

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
};

class DeadNodeSweepingProcessor {
 public:
  explicit DeadNodeSweepingProcessor(MaglevCompilationInfo* compilation_info) {
    if (V8_UNLIKELY(compilation_info->has_graph_labeller())) {
      labeller_ = compilation_info->graph_labeller();
    }
  }

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  ProcessResult Process(AllocationBlock* node, const ProcessingState& state) {
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
    // If size is zero, then none of the inlined allocations have escaped, we
    // can remove the allocation block.
    if (size == 0) return ProcessResult::kRemove;
    return ProcessResult::kContinue;
  }

  ProcessResult Process(InlinedAllocation* node, const ProcessingState& state) {
    // Remove inlined allocation that became non-escaping.
    if (!node->HasEscaped()) {
      if (v8_flags.trace_maglev_escape_analysis) {
        std::cout << "* Removing allocation node "
                  << PrintNodeLabel(labeller_, node) << std::endl;
      }
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
        return ProcessResult::kRemove;
      }
      return ProcessResult::kContinue;
    }

    if constexpr (CanBeStoreToNonEscapedObject<NodeT>()) {
      if (InlinedAllocation* object =
              node->input(0).node()->template TryCast<InlinedAllocation>()) {
        if (!object->HasEscaped()) {
          if (v8_flags.trace_maglev_escape_analysis) {
            std::cout << "* Removing store node "
                      << PrintNodeLabel(labeller_, node) << " to allocation "
                      << PrintNodeLabel(labeller_, object) << std::endl;
          }
          return ProcessResult::kRemove;
        }
      }
    }
    return ProcessResult::kContinue;
  }

 private:
  MaglevGraphLabeller* labeller_ = nullptr;
};

}  // namespace v8::internal::maglev

#endif  // V8_MAGLEV_MAGLEV_POST_HOC_OPTIMIZATIONS_PROCESSORS_H_
