// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_MACHINE_GRAPH_H_
#define V8_COMPILER_MACHINE_GRAPH_H_

#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/compiler/common-node-cache.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-aux-data.h"
#include "src/runtime/runtime.h"

namespace v8 {
namespace internal {
namespace compiler {

// Implements a facade on a Graph, enhancing the graph with machine-specific
// notions, including a builder for common and machine operators, as well
// as caching primitive constants.
class V8_EXPORT_PRIVATE MachineGraph : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  MachineGraph(Graph* graph, CommonOperatorBuilder* common,
               MachineOperatorBuilder* machine)
      : graph_(graph),
        common_(common),
        machine_(machine),
        cache_(zone()),
        call_counts_(zone()) {}
  MachineGraph(const MachineGraph&) = delete;
  MachineGraph& operator=(const MachineGraph&) = delete;

  // Creates a new (unique) Int32Constant node.
  Node* UniqueInt32Constant(int32_t value);

  Node* UniqueInt64Constant(int64_t value);

  // Creates a Int32Constant node, usually canonicalized.
  Node* Int32Constant(int32_t value);
  Node* Uint32Constant(uint32_t value) {
    return Int32Constant(base::bit_cast<int32_t>(value));
  }

  // Creates a Int64Constant node, usually canonicalized.
  Node* Int64Constant(int64_t value);
  Node* Uint64Constant(uint64_t value) {
    return Int64Constant(base::bit_cast<int64_t>(value));
  }

  // Creates a Int32Constant/Int64Constant node, depending on the word size of
  // the target machine.
  // TODO(turbofan): Code using Int32Constant/Int64Constant to store pointer
  // constants is probably not serializable.
  Node* IntPtrConstant(intptr_t value);
  Node* UintPtrConstant(uintptr_t value);
  Node* UniqueIntPtrConstant(intptr_t value);

  Node* TaggedIndexConstant(intptr_t value);

  Node* RelocatableInt32Constant(int32_t value, RelocInfo::Mode rmode);
  Node* RelocatableInt64Constant(int64_t value, RelocInfo::Mode rmode);
  Node* RelocatableIntPtrConstant(intptr_t value, RelocInfo::Mode rmode);
  Node* RelocatableWasmBuiltinCallTarget(Builtin builtin);

  // Creates a Float32Constant node, usually canonicalized.
  Node* Float32Constant(float value);

  // Creates a Float64Constant node, usually canonicalized.
  Node* Float64Constant(double value);

  // Creates a PointerConstant node.
  Node* PointerConstant(intptr_t value);
  template <typename T>
  Node* PointerConstant(T* value) {
    return PointerConstant(reinterpret_cast<intptr_t>(value));
  }

  // Creates an ExternalConstant node, usually canonicalized.
  Node* ExternalConstant(ExternalReference ref);
  Node* ExternalConstant(Runtime::FunctionId function_id);

  // Global cache of the dead node.
  Node* Dead() {
    return Dead_ ? Dead_ : Dead_ = graph_->NewNode(common_->Dead());
  }

  // Store and retrieve call count information.
  void StoreCallCount(NodeId call_id, int count) {
    call_counts_.Put(call_id, count);
  }
  int GetCallCount(NodeId call_id) { return call_counts_.Get(call_id); }
  // Use this to keep the number of map rehashings to a minimum.
  void ReserveCallCounts(size_t num_call_instructions) {
    call_counts_.Reserve(num_call_instructions);
  }

  CommonOperatorBuilder* common() const { return common_; }
  MachineOperatorBuilder* machine() const { return machine_; }
  Graph* graph() const { return graph_; }
  Zone* zone() const { return graph()->zone(); }

 protected:
  Graph* graph_;
  CommonOperatorBuilder* common_;
  MachineOperatorBuilder* machine_;
  CommonNodeCache cache_;
  NodeAuxDataMap<int, -1> call_counts_;
  Node* Dead_ = nullptr;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_MACHINE_GRAPH_H_
