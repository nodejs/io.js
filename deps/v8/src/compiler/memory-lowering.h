// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MEMORY_LOWERING_H_
#define V8_COMPILER_MEMORY_LOWERING_H_

#include "src/compiler/graph-assembler.h"
#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class CommonOperatorBuilder;
struct ElementAccess;
class Graph;
class JSGraph;
class MachineOperatorBuilder;
class Node;
class Operator;

// Provides operations to lower all simplified memory access and allocation
// related nodes (i.e. Allocate, LoadField, StoreField and friends) to machine
// operators.
class MemoryLowering final : public Reducer {
 public:
  enum class AllocationFolding { kDoAllocationFolding, kDontAllocationFolding };
  class AllocationGroup;

  // An allocation state is propagated on the effect paths through the graph.
  class AllocationState final : public ZoneObject {
   public:
    AllocationState(const AllocationState&) = delete;
    AllocationState& operator=(const AllocationState&) = delete;

    static AllocationState const* Empty(Zone* zone) {
      return zone->New<AllocationState>();
    }
    static AllocationState const* Closed(AllocationGroup* group, Node* effect,
                                         Zone* zone) {
      return zone->New<AllocationState>(group, effect);
    }
    static AllocationState const* Open(AllocationGroup* group, intptr_t size,
                                       Node* top, Node* effect, Zone* zone) {
      return zone->New<AllocationState>(group, size, top, effect);
    }

    bool IsYoungGenerationAllocation() const;

    AllocationGroup* group() const { return group_; }
    Node* top() const { return top_; }
    Node* effect() const { return effect_; }
    intptr_t size() const { return size_; }

   private:
    friend Zone;

    AllocationState();
    explicit AllocationState(AllocationGroup* group, Node* effect);
    AllocationState(AllocationGroup* group, intptr_t size, Node* top,
                    Node* effect);

    AllocationGroup* const group_;
    // The upper bound of the combined allocated object size on the current path
    // (max int if allocation folding is impossible on this path).
    intptr_t const size_;
    Node* const top_;
    Node* const effect_;
  };

  using WriteBarrierAssertFailedCallback = std::function<void(
      Node* node, Node* object, const char* name, Zone* temp_zone)>;

  MemoryLowering(
      JSGraph* jsgraph, Zone* zone, JSGraphAssembler* graph_assembler,
      bool is_wasm,
      AllocationFolding allocation_folding =
          AllocationFolding::kDontAllocationFolding,
      WriteBarrierAssertFailedCallback callback = [](Node*, Node*, const char*,
                                                     Zone*) { UNREACHABLE(); },
      const char* function_debug_name = nullptr);

  const char* reducer_name() const override { return "MemoryReducer"; }

  // Perform memory lowering reduction on the given Node.
  Reduction Reduce(Node* node) override;

  // Specific reducers for each optype to enable keeping track of
  // AllocationState by the MemoryOptimizer.
  Reduction ReduceAllocateRaw(Node* node, AllocationType allocation_type,
                              AllocationState const** state);
  Reduction ReduceLoadFromObject(Node* node);
  Reduction ReduceLoadElement(Node* node);
  Reduction ReduceLoadField(Node* node);
  Reduction ReduceStoreToObject(Node* node,
                                AllocationState const* state = nullptr);
  Reduction ReduceStoreElement(Node* node,
                               AllocationState const* state = nullptr);
  Reduction ReduceStoreField(Node* node,
                             AllocationState const* state = nullptr);
  Reduction ReduceStore(Node* node, AllocationState const* state = nullptr);

 private:
  Reduction ReduceAllocateRaw(Node* node);
  WriteBarrierKind ComputeWriteBarrierKind(Node* node, Node* object,
                                           Node* value,
                                           AllocationState const* state,
                                           WriteBarrierKind);
  Reduction ReduceLoadExternalPointerField(Node* node);
  Reduction ReduceLoadBoundedSize(Node* node);
  Reduction ReduceLoadMap(Node* node);
  Node* ComputeIndex(ElementAccess const& access, Node* node);
  void EnsureAllocateOperator();
  Node* GetWasmInstanceNode();

  // Align the value to kObjectAlignment8GbHeap if V8_COMPRESS_POINTERS_8GB is
  // defined.
  Node* AlignToAllocationAlignment(Node* address);

  Graph* graph() const { return graph_; }
  Isolate* isolate() const { return isolate_; }
  Zone* zone() const { return zone_; }
  inline Zone* graph_zone() const;
  CommonOperatorBuilder* common() const { return common_; }
  MachineOperatorBuilder* machine() const { return machine_; }
  JSGraphAssembler* gasm() const { return graph_assembler_; }

  SetOncePointer<const Operator> allocate_operator_;
  SetOncePointer<Node> wasm_instance_node_;
  Isolate* isolate_;
  Zone* zone_;
  Graph* graph_;
  CommonOperatorBuilder* common_;
  MachineOperatorBuilder* machine_;
  JSGraphAssembler* graph_assembler_;
  bool is_wasm_;
  AllocationFolding allocation_folding_;
  WriteBarrierAssertFailedCallback write_barrier_assert_failed_;
  const char* function_debug_name_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MemoryLowering);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MEMORY_LOWERING_H_
