// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/memory-optimizer.h"

#include "src/base/logging.h"
#include "src/codegen/tick-counter.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/roots/roots-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool CanAllocate(const Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kAbortCSADcheck:
    case IrOpcode::kBitcastTaggedToWord:
    case IrOpcode::kBitcastWordToTagged:
    case IrOpcode::kCheckTurboshaftTypeOf:
    case IrOpcode::kComment:
    case IrOpcode::kDebugBreak:
    case IrOpcode::kDeoptimizeIf:
    case IrOpcode::kDeoptimizeUnless:
    case IrOpcode::kEffectPhi:
    case IrOpcode::kIfException:
    case IrOpcode::kLoad:
    case IrOpcode::kLoadImmutable:
    case IrOpcode::kLoadElement:
    case IrOpcode::kLoadField:
    case IrOpcode::kLoadFromObject:
    case IrOpcode::kLoadImmutableFromObject:
    case IrOpcode::kMemoryBarrier:
    case IrOpcode::kProtectedLoad:
    case IrOpcode::kLoadTrapOnNull:
    case IrOpcode::kProtectedStore:
    case IrOpcode::kStoreTrapOnNull:
    case IrOpcode::kRetain:
    case IrOpcode::kStackPointerGreaterThan:
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kLoadLane:
    case IrOpcode::kLoadTransform:
    case IrOpcode::kStoreLane:
    case IrOpcode::kLoadStackPointer:
    case IrOpcode::kSetStackPointer:
#endif  // V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kStaticAssert:
    // TODO(turbofan): Store nodes might do a bump-pointer allocation.
    //              We should introduce a special bump-pointer store node to
    //              differentiate that.
    case IrOpcode::kStore:
    case IrOpcode::kStoreElement:
    case IrOpcode::kStoreField:
    case IrOpcode::kStoreToObject:
    case IrOpcode::kTraceInstruction:
    case IrOpcode::kInitializeImmutableInObject:
    case IrOpcode::kTrapIf:
    case IrOpcode::kTrapUnless:
    case IrOpcode::kUnalignedLoad:
    case IrOpcode::kUnalignedStore:
    case IrOpcode::kUnreachable:
    case IrOpcode::kWord32AtomicAdd:
    case IrOpcode::kWord32AtomicAnd:
    case IrOpcode::kWord32AtomicCompareExchange:
    case IrOpcode::kWord32AtomicExchange:
    case IrOpcode::kWord32AtomicLoad:
    case IrOpcode::kWord32AtomicOr:
    case IrOpcode::kWord32AtomicPairAdd:
    case IrOpcode::kWord32AtomicPairAnd:
    case IrOpcode::kWord32AtomicPairCompareExchange:
    case IrOpcode::kWord32AtomicPairExchange:
    case IrOpcode::kWord32AtomicPairLoad:
    case IrOpcode::kWord32AtomicPairOr:
    case IrOpcode::kWord32AtomicPairStore:
    case IrOpcode::kWord32AtomicPairSub:
    case IrOpcode::kWord32AtomicPairXor:
    case IrOpcode::kWord32AtomicStore:
    case IrOpcode::kWord32AtomicSub:
    case IrOpcode::kWord32AtomicXor:
    case IrOpcode::kWord64AtomicAdd:
    case IrOpcode::kWord64AtomicAnd:
    case IrOpcode::kWord64AtomicCompareExchange:
    case IrOpcode::kWord64AtomicExchange:
    case IrOpcode::kWord64AtomicLoad:
    case IrOpcode::kWord64AtomicOr:
    case IrOpcode::kWord64AtomicStore:
    case IrOpcode::kWord64AtomicSub:
    case IrOpcode::kWord64AtomicXor:
      return false;

    case IrOpcode::kCall:
      return !(CallDescriptorOf(node->op())->flags() &
               CallDescriptor::kNoAllocate);
    default:
      break;
  }
  return true;
}

Node* SearchAllocatingNode(Node* start, Node* limit, Zone* temp_zone) {
  ZoneQueue<Node*> queue(temp_zone);
  ZoneSet<Node*> visited(temp_zone);
  visited.insert(limit);
  queue.push(start);

  while (!queue.empty()) {
    Node* const current = queue.front();
    queue.pop();
    if (visited.find(current) == visited.end()) {
      visited.insert(current);

      if (CanAllocate(current)) {
        return current;
      }

      for (int i = 0; i < current->op()->EffectInputCount(); ++i) {
        queue.push(NodeProperties::GetEffectInput(current, i));
      }
    }
  }
  return nullptr;
}

bool CanLoopAllocate(Node* loop_effect_phi, Zone* temp_zone) {
  Node* const control = NodeProperties::GetControlInput(loop_effect_phi);
  // Start the effect chain walk from the loop back edges.
  for (int i = 1; i < control->InputCount(); ++i) {
    if (SearchAllocatingNode(loop_effect_phi->InputAt(i), loop_effect_phi,
                             temp_zone) != nullptr) {
      return true;
    }
  }
  return false;
}

Node* EffectPhiForPhi(Node* phi) {
  Node* control = NodeProperties::GetControlInput(phi);
  for (Node* use : control->uses()) {
    if (use->opcode() == IrOpcode::kEffectPhi) {
      return use;
    }
  }
  return nullptr;
}

void WriteBarrierAssertFailed(Node* node, Node* object, const char* name,
                              Zone* temp_zone) {
  std::stringstream str;
  str << "MemoryOptimizer could not remove write barrier for node #"
      << node->id() << "\n";
  str << "  Run mksnapshot with --csa-trap-on-node=" << name << ","
      << node->id() << " to break in CSA code.\n";
  Node* object_position = object;
  if (object_position->opcode() == IrOpcode::kPhi) {
    object_position = EffectPhiForPhi(object_position);
  }
  Node* allocating_node = nullptr;
  if (object_position && object_position->op()->EffectOutputCount() > 0) {
    allocating_node = SearchAllocatingNode(node, object_position, temp_zone);
  }
  if (allocating_node) {
    str << "\n  There is a potentially allocating node in between:\n";
    str << "    " << *allocating_node << "\n";
    str << "  Run mksnapshot with --csa-trap-on-node=" << name << ","
        << allocating_node->id() << " to break there.\n";
    if (allocating_node->opcode() == IrOpcode::kCall) {
      str << "  If this is a never-allocating runtime call, you can add an "
             "exception to Runtime::MayAllocate.\n";
    }
  } else {
    str << "\n  It seems the store happened to something different than a "
           "direct "
           "allocation:\n";
    str << "    " << *object << "\n";
    str << "  Run mksnapshot with --csa-trap-on-node=" << name << ","
        << object->id() << " to break there.\n";
  }
  FATAL("%s", str.str().c_str());
}

}  // namespace

MemoryOptimizer::MemoryOptimizer(
    JSHeapBroker* broker, JSGraph* jsgraph, Zone* zone,
    MemoryLowering::AllocationFolding allocation_folding,
    const char* function_debug_name, TickCounter* tick_counter, bool is_wasm)
    : graph_assembler_(broker, jsgraph, zone, BranchSemantics::kMachine),
      memory_lowering_(jsgraph, zone, &graph_assembler_, is_wasm,
                       allocation_folding, WriteBarrierAssertFailed,
                       function_debug_name),
      wasm_address_reassociation_(jsgraph, zone),
      jsgraph_(jsgraph),
      empty_state_(AllocationState::Empty(zone)),
      pending_(zone),
      tokens_(zone),
      zone_(zone),
      tick_counter_(tick_counter) {}

void MemoryOptimizer::Optimize() {
  EnqueueUses(graph()->start(), empty_state(), graph()->start()->id());
  while (!tokens_.empty()) {
    Token const token = tokens_.front();
    tokens_.pop();
    VisitNode(token.node, token.state, token.effect_chain);
  }
  if (v8_flags.turbo_wasm_address_reassociation) {
    wasm_address_reassociation()->Optimize();
  }
  DCHECK(pending_.empty());
  DCHECK(tokens_.empty());
}

void MemoryOptimizer::VisitNode(Node* node, AllocationState const* state,
                                NodeId effect_chain) {
  tick_counter_->TickAndMaybeEnterSafepoint();
  DCHECK(!node->IsDead());
  DCHECK_LT(0, node->op()->EffectInputCount());
  switch (node->opcode()) {
    case IrOpcode::kAllocate:
      // Allocate nodes were purged from the graph in effect-control
      // linearization.
      UNREACHABLE();
    case IrOpcode::kAllocateRaw:
      return VisitAllocateRaw(node, state, effect_chain);
    case IrOpcode::kCall:
      return VisitCall(node, state, effect_chain);
    case IrOpcode::kLoadFromObject:
    case IrOpcode::kLoadImmutableFromObject:
      return VisitLoadFromObject(node, state, effect_chain);
    case IrOpcode::kLoadElement:
      return VisitLoadElement(node, state, effect_chain);
    case IrOpcode::kLoadField:
      return VisitLoadField(node, state, effect_chain);
    case IrOpcode::kProtectedLoad:
      return VisitProtectedLoad(node, state, effect_chain);
    case IrOpcode::kProtectedStore:
      return VisitProtectedStore(node, state, effect_chain);
    case IrOpcode::kStoreToObject:
    case IrOpcode::kInitializeImmutableInObject:
      return VisitStoreToObject(node, state, effect_chain);
    case IrOpcode::kStoreElement:
      return VisitStoreElement(node, state, effect_chain);
    case IrOpcode::kStoreField:
      return VisitStoreField(node, state, effect_chain);
    case IrOpcode::kStore:
      return VisitStore(node, state, effect_chain);
    case IrOpcode::kStorePair:
      // Store pairing should happen after this pass.
      UNREACHABLE();
    default:
      if (!CanAllocate(node)) {
        // These operations cannot trigger GC.
        return VisitOtherEffect(node, state, effect_chain);
      }
  }
  DCHECK_EQ(0, node->op()->EffectOutputCount());
}

bool MemoryOptimizer::AllocationTypeNeedsUpdateToOld(Node* const node,
                                                     const Edge edge) {
  // Test to see if we need to update the AllocationType.
  if (node->opcode() == IrOpcode::kStoreField && edge.index() == 1) {
    Node* parent = node->InputAt(0);
    if (parent->opcode() == IrOpcode::kAllocateRaw &&
        AllocationTypeOf(parent->op()) == AllocationType::kOld) {
      return true;
    }
  }

  return false;
}

void MemoryOptimizer::ReplaceUsesAndKillNode(Node* node, Node* replacement) {
  // Replace all uses of node and kill the node to make sure we don't leave
  // dangling dead uses.
  DCHECK_NE(replacement, node);
  NodeProperties::ReplaceUses(node, replacement, graph_assembler_.effect(),
                              graph_assembler_.control());
  node->Kill();
}

void MemoryOptimizer::VisitAllocateRaw(Node* node, AllocationState const* state,
                                       NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kAllocateRaw, node->opcode());
  const AllocateParameters& allocation = AllocateParametersOf(node->op());
  AllocationType allocation_type = allocation.allocation_type();

  // Propagate tenuring from outer allocations to inner allocations, i.e.
  // when we allocate an object in old space and store a newly allocated
  // child object into the pretenured object, then the newly allocated
  // child object also should get pretenured to old space.
  if (allocation_type == AllocationType::kOld) {
    for (Edge const edge : node->use_edges()) {
      Node* const user = edge.from();
      if (user->opcode() == IrOpcode::kStoreField && edge.index() == 0) {
        Node* child = user->InputAt(1);
        if (child->opcode() == IrOpcode::kAllocateRaw &&
            AllocationTypeOf(child->op()) == AllocationType::kYoung) {
          NodeProperties::ChangeOp(child, node->op());
          break;
        }
      }
    }
  } else {
    DCHECK_EQ(AllocationType::kYoung, allocation_type);
    for (Edge const edge : node->use_edges()) {
      Node* const user = edge.from();
      if (AllocationTypeNeedsUpdateToOld(user, edge)) {
        allocation_type = AllocationType::kOld;
        break;
      }
    }
  }

  Reduction reduction =
      memory_lowering()->ReduceAllocateRaw(node, allocation_type, &state);
  CHECK(reduction.Changed() && reduction.replacement() != node);

  ReplaceUsesAndKillNode(node, reduction.replacement());

  EnqueueUses(state->effect(), state, effect_chain);
}

void MemoryOptimizer::VisitLoadFromObject(Node* node,
                                          AllocationState const* state,
                                          NodeId effect_chain) {
  DCHECK(node->opcode() == IrOpcode::kLoadFromObject ||
         node->opcode() == IrOpcode::kLoadImmutableFromObject);
  Reduction reduction = memory_lowering()->ReduceLoadFromObject(node);
  EnqueueUses(node, state, effect_chain);
  if (V8_MAP_PACKING_BOOL && reduction.replacement() != node) {
    ReplaceUsesAndKillNode(node, reduction.replacement());
  }
}

void MemoryOptimizer::VisitStoreToObject(Node* node,
                                         AllocationState const* state,
                                         NodeId effect_chain) {
  DCHECK(node->opcode() == IrOpcode::kStoreToObject ||
         node->opcode() == IrOpcode::kInitializeImmutableInObject);
  memory_lowering()->ReduceStoreToObject(node, state);
  EnqueueUses(node, state, effect_chain);
}

void MemoryOptimizer::VisitLoadElement(Node* node, AllocationState const* state,
                                       NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kLoadElement, node->opcode());
  memory_lowering()->ReduceLoadElement(node);
  EnqueueUses(node, state, effect_chain);
}

void MemoryOptimizer::VisitLoadField(Node* node, AllocationState const* state,
                                     NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kLoadField, node->opcode());
  Reduction reduction = memory_lowering()->ReduceLoadField(node);
  DCHECK(reduction.Changed());
  // In case of replacement, the replacement graph should not require futher
  // lowering, so we can proceed iterating the graph from the node uses.
  EnqueueUses(node, state, effect_chain);

  // Node can be replaced under two cases:
  //   1. V8_ENABLE_SANDBOX is true and loading an external pointer value.
  //   2. V8_MAP_PACKING_BOOL is enabled.
  DCHECK_IMPLIES(!V8_ENABLE_SANDBOX_BOOL && !V8_MAP_PACKING_BOOL,
                 reduction.replacement() == node);
  if ((V8_ENABLE_SANDBOX_BOOL || V8_MAP_PACKING_BOOL) &&
      reduction.replacement() != node) {
    ReplaceUsesAndKillNode(node, reduction.replacement());
  }
}

void MemoryOptimizer::VisitProtectedLoad(Node* node,
                                         AllocationState const* state,
                                         NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kProtectedLoad, node->opcode());
  if (v8_flags.turbo_wasm_address_reassociation) {
    wasm_address_reassociation()->VisitProtectedMemOp(node, effect_chain);
    EnqueueUses(node, state, effect_chain);
  } else {
    VisitOtherEffect(node, state, effect_chain);
  }
}

void MemoryOptimizer::VisitProtectedStore(Node* node,
                                          AllocationState const* state,
                                          NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kProtectedStore, node->opcode());
  if (v8_flags.turbo_wasm_address_reassociation) {
    wasm_address_reassociation()->VisitProtectedMemOp(node, effect_chain);
    EnqueueUses(node, state, effect_chain);
  } else {
    VisitOtherEffect(node, state, effect_chain);
  }
}

void MemoryOptimizer::VisitStoreElement(Node* node,
                                        AllocationState const* state,
                                        NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kStoreElement, node->opcode());
  memory_lowering()->ReduceStoreElement(node, state);
  EnqueueUses(node, state, effect_chain);
}

void MemoryOptimizer::VisitStoreField(Node* node, AllocationState const* state,
                                      NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kStoreField, node->opcode());
  memory_lowering()->ReduceStoreField(node, state);
  EnqueueUses(node, state, effect_chain);
}
void MemoryOptimizer::VisitStore(Node* node, AllocationState const* state,
                                 NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kStore, node->opcode());
  memory_lowering()->ReduceStore(node, state);
  EnqueueUses(node, state, effect_chain);
}

void MemoryOptimizer::VisitCall(Node* node, AllocationState const* state,
                                NodeId effect_chain) {
  DCHECK_EQ(IrOpcode::kCall, node->opcode());
  // If the call can allocate, we start with a fresh state.
  if (!(CallDescriptorOf(node->op())->flags() & CallDescriptor::kNoAllocate)) {
    state = empty_state();
  }
  EnqueueUses(node, state, effect_chain);
}

void MemoryOptimizer::VisitOtherEffect(Node* node, AllocationState const* state,
                                       NodeId effect_chain) {
  EnqueueUses(node, state, effect_chain);
}

MemoryOptimizer::AllocationState const* MemoryOptimizer::MergeStates(
    AllocationStates const& states) {
  // Check if all states are the same; or at least if all allocation
  // states belong to the same allocation group.
  AllocationState const* state = states.front();
  MemoryLowering::AllocationGroup* group = state->group();
  for (size_t i = 1; i < states.size(); ++i) {
    if (states[i] != state) state = nullptr;
    if (states[i]->group() != group) group = nullptr;
  }
  if (state == nullptr) {
    if (group != nullptr) {
      // We cannot fold any more allocations into this group, but we can still
      // eliminate write barriers on stores to this group.
      // TODO(bmeurer): We could potentially just create a Phi here to merge
      // the various tops; but we need to pay special attention not to create
      // an unschedulable graph.
      state = AllocationState::Closed(group, nullptr, zone());
    } else {
      // The states are from different allocation groups.
      state = empty_state();
    }
  }
  return state;
}

void MemoryOptimizer::EnqueueMerge(Node* node, int index,
                                   AllocationState const* state) {
  DCHECK_EQ(IrOpcode::kEffectPhi, node->opcode());
  NodeId effect_chain = node->id();
  int const input_count = node->InputCount() - 1;
  DCHECK_LT(0, input_count);
  Node* const control = node->InputAt(input_count);
  if (control->opcode() == IrOpcode::kLoop) {
    if (index == 0) {
      if (CanLoopAllocate(node, zone())) {
        // If the loop can allocate,  we start with an empty state at the
        // beginning.
        EnqueueUses(node, empty_state(), effect_chain);
      } else {
        // If the loop cannot allocate, we can just propagate the state from
        // before the loop.
        EnqueueUses(node, state, effect_chain);
      }
    } else {
      // Do not revisit backedges.
    }
  } else {
    DCHECK_EQ(IrOpcode::kMerge, control->opcode());
    // Check if we already know about this pending merge.
    NodeId const id = node->id();
    auto it = pending_.find(id);
    if (it == pending_.end()) {
      // Insert a new pending merge.
      it = pending_.insert(std::make_pair(id, AllocationStates(zone()))).first;
    }
    // Add the next input state.
    it->second.push_back(state);
    // Check if states for all inputs are available by now.
    if (it->second.size() == static_cast<size_t>(input_count)) {
      // All inputs to this effect merge are done, merge the states given all
      // input constraints, drop the pending merge and enqueue uses of the
      // EffectPhi {node}.
      state = MergeStates(it->second);
      EnqueueUses(node, state, effect_chain);
      pending_.erase(it);
    }
  }
}

void MemoryOptimizer::EnqueueUses(Node* node, AllocationState const* state,
                                  NodeId effect_chain) {
  for (Edge const edge : node->use_edges()) {
    if (NodeProperties::IsEffectEdge(edge)) {
      EnqueueUse(edge.from(), edge.index(), state, effect_chain);
    }
  }
}

void MemoryOptimizer::EnqueueUse(Node* node, int index,
                                 AllocationState const* state,
                                 NodeId effect_chain) {
  if (node->opcode() == IrOpcode::kEffectPhi) {
    // An EffectPhi represents a merge of different effect chains, which
    // needs special handling depending on whether the merge is part of a
    // loop or just a normal control join.
    EnqueueMerge(node, index, state);
  } else {
    Token token = {node, state, effect_chain};
    tokens_.push(token);
  }
}

Graph* MemoryOptimizer::graph() const { return jsgraph()->graph(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
