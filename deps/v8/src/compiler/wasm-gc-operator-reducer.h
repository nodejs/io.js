// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_
#define V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_

#include "src/compiler/control-path-state.h"
#include "src/compiler/graph-reducer.h"
#include "src/compiler/wasm-graph-assembler.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8 {
namespace internal {
namespace compiler {

class MachineGraph;
class SourcePositionTable;

struct NodeWithType {
  NodeWithType() : node(nullptr), type(wasm::kWasmVoid, nullptr) {}
  NodeWithType(Node* node, wasm::TypeInModule type) : node(node), type(type) {}

  bool operator==(const NodeWithType& other) const {
    return node == other.node && type == other.type;
  }
  bool operator!=(const NodeWithType& other) const { return !(*this == other); }

  bool IsSet() { return node != nullptr; }

  Node* node;
  wasm::TypeInModule type;
};

// This class optimizes away wasm-gc type checks and casts. Two types of
// information are used:
// - Types already marked on graph nodes.
// - Path-dependent type information that is inferred when a type check is used
//   as a branch condition.
class WasmGCOperatorReducer final
    : public AdvancedReducerWithControlPathState<NodeWithType,
                                                 kMultipleInstances> {
 public:
  WasmGCOperatorReducer(Editor* editor, Zone* temp_zone_, MachineGraph* mcgraph,
                        const wasm::WasmModule* module,
                        SourcePositionTable* source_position_table);

  const char* reducer_name() const override { return "WasmGCOperatorReducer"; }

  Reduction Reduce(Node* node) final;

 private:
  using ControlPathTypes = ControlPathState<NodeWithType, kMultipleInstances>;

  Reduction ReduceWasmStructOperation(Node* node);
  Reduction ReduceWasmArrayLength(Node* node);
  Reduction ReduceAssertNotNull(Node* node);
  Reduction ReduceCheckNull(Node* node);
  Reduction ReduceWasmTypeCheck(Node* node);
  Reduction ReduceWasmTypeCheckAbstract(Node* node);
  Reduction ReduceWasmTypeCast(Node* node);
  Reduction ReduceWasmTypeCastAbstract(Node* node);
  Reduction ReduceTypeGuard(Node* node);
  Reduction ReduceWasmAnyConvertExtern(Node* node);
  Reduction ReduceMerge(Node* node);
  Reduction ReduceIf(Node* node, bool condition);
  Reduction ReduceStart(Node* node);

  Node* SetType(Node* node, wasm::ValueType type);
  void UpdateSourcePosition(Node* new_node, Node* old_node);
  // Returns the intersection of the type marked on {object} and the type
  // information about object tracked on {control}'s control path (if present).
  // If {allow_non_wasm}, we bail out if the object's type is not a wasm type
  // by returning bottom.
  wasm::TypeInModule ObjectTypeFromContext(Node* object, Node* control,
                                           bool allow_non_wasm = false);
  Reduction UpdateNodeAndAliasesTypes(Node* state_owner,
                                      ControlPathTypes parent_state, Node* node,
                                      wasm::TypeInModule type,
                                      bool in_new_block);

  Graph* graph() { return mcgraph_->graph(); }
  CommonOperatorBuilder* common() { return mcgraph_->common(); }
  SimplifiedOperatorBuilder* simplified() { return gasm_.simplified(); }

  MachineGraph* mcgraph_;
  WasmGraphAssembler gasm_;
  const wasm::WasmModule* module_;
  SourcePositionTable* source_position_table_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_GC_OPERATOR_REDUCER_H_
