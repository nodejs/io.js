// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MEMORY_OPTIMIZER_H_
#define V8_COMPILER_MEMORY_OPTIMIZER_H_

#include "src/compiler/graph-assembler.h"
#include "src/compiler/memory-lowering.h"
#include "src/zone/zone-containers.h"

#ifdef V8_ENABLE_WEBASSEMBLY
#include "src/compiler/wasm-address-reassociation.h"
#else
namespace v8 {
namespace internal {
namespace compiler {

class V8_EXPORT_PRIVATE WasmAddressReassociation final {
 public:
  WasmAddressReassociation(JSGraph* jsgraph, Zone* zone) {}
  void Optimize() {}
  void VisitProtectedMemOp(Node* node, uint32_t effect_chain) {}
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8
#endif

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

class JSGraph;
class Graph;

// NodeIds are identifying numbers for nodes that can be used to index auxiliary
// out-of-line data associated with each node.
using NodeId = uint32_t;

// Performs allocation folding and store write barrier elimination
// implicitly, while lowering all simplified memory access and allocation
// related nodes (i.e. Allocate, LoadField, StoreField and friends) to machine
// operators.
class MemoryOptimizer final {
 public:
  MemoryOptimizer(JSHeapBroker* broker, JSGraph* jsgraph, Zone* zone,
                  MemoryLowering::AllocationFolding allocation_folding,
                  const char* function_debug_name, TickCounter* tick_counter,
                  bool is_wasm);
  ~MemoryOptimizer() = default;

  void Optimize();

 private:
  using AllocationState = MemoryLowering::AllocationState;

  // An array of allocation states used to collect states on merges.
  using AllocationStates = ZoneVector<AllocationState const*>;

  // We thread through tokens to represent the current state on a given effect
  // path through the graph.
  struct Token {
    Node* node;
    AllocationState const* state;
    // The most recent EffectPhi in the chain, which is used as a heuristic by
    // address reassociation.
    NodeId effect_chain;
  };

  void VisitNode(Node*, AllocationState const*, NodeId);
  void VisitAllocateRaw(Node*, AllocationState const*, NodeId);
  void VisitCall(Node*, AllocationState const*, NodeId);
  void VisitLoadFromObject(Node*, AllocationState const*, NodeId);
  void VisitLoadElement(Node*, AllocationState const*, NodeId);
  void VisitLoadField(Node*, AllocationState const*, NodeId);
  void VisitProtectedLoad(Node*, AllocationState const*, NodeId);
  void VisitProtectedStore(Node*, AllocationState const*, NodeId);
  void VisitStoreToObject(Node*, AllocationState const*, NodeId);
  void VisitStoreElement(Node*, AllocationState const*, NodeId);
  void VisitStoreField(Node*, AllocationState const*, NodeId);
  void VisitStore(Node*, AllocationState const*, NodeId);
  void VisitOtherEffect(Node*, AllocationState const*, NodeId);

  AllocationState const* MergeStates(AllocationStates const& states);

  void EnqueueMerge(Node*, int, AllocationState const*);
  void EnqueueUses(Node*, AllocationState const*, NodeId);
  void EnqueueUse(Node*, int, AllocationState const*, NodeId);

  void ReplaceUsesAndKillNode(Node* node, Node* replacement);

  // Returns true if the AllocationType of the current AllocateRaw node that we
  // are visiting needs to be updated to kOld, due to propagation of tenuring
  // from outer to inner allocations.
  bool AllocationTypeNeedsUpdateToOld(Node* const user, const Edge edge);

  AllocationState const* empty_state() const { return empty_state_; }
  MemoryLowering* memory_lowering() { return &memory_lowering_; }
  WasmAddressReassociation* wasm_address_reassociation() {
    return &wasm_address_reassociation_;
  }
  Graph* graph() const;
  JSGraph* jsgraph() const { return jsgraph_; }
  Zone* zone() const { return zone_; }

  JSGraphAssembler graph_assembler_;
  MemoryLowering memory_lowering_;
  WasmAddressReassociation wasm_address_reassociation_;
  JSGraph* jsgraph_;
  AllocationState const* const empty_state_;
  ZoneMap<NodeId, AllocationStates> pending_;
  ZoneQueue<Token> tokens_;
  Zone* const zone_;
  TickCounter* const tick_counter_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MemoryOptimizer);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MEMORY_OPTIMIZER_H_
