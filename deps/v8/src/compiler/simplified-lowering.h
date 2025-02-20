// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_SIMPLIFIED_LOWERING_H_
#define V8_COMPILER_SIMPLIFIED_LOWERING_H_

#include "src/compiler/js-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/simplified-operator.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

// Forward declarations.
class NodeOriginTable;
class ObserveNodeManager;
class RepresentationChanger;
class RepresentationSelector;
class SourcePositionTable;
class TypeCache;

class V8_EXPORT_PRIVATE SimplifiedLowering final {
 public:
  SimplifiedLowering(JSGraph* jsgraph, JSHeapBroker* broker, Zone* zone,
                     SourcePositionTable* source_position,
                     NodeOriginTable* node_origins, TickCounter* tick_counter,
                     Linkage* linkage, OptimizedCompilationInfo* info,
                     ObserveNodeManager* observe_node_manager = nullptr);
  ~SimplifiedLowering() = default;

  void LowerAllNodes();

  void DoMax(Node* node, Operator const* op, MachineRepresentation rep);
  void DoMin(Node* node, Operator const* op, MachineRepresentation rep);
  void DoJSToNumberOrNumericTruncatesToFloat64(
      Node* node, RepresentationSelector* selector);
  void DoJSToNumberOrNumericTruncatesToWord32(Node* node,
                                              RepresentationSelector* selector);
  void DoIntegral32ToBit(Node* node);
  void DoOrderedNumberToBit(Node* node);
  void DoNumberToBit(Node* node);
  void DoIntegerToUint8Clamped(Node* node);
  void DoNumberToUint8Clamped(Node* node);
  void DoSigned32ToUint8Clamped(Node* node);
  void DoUnsigned32ToUint8Clamped(Node* node);
  void DoNumberToFloat16RawBits(Node* node);
  void DoFloat16RawBitsToNumber(Node* node);

 private:
  // The purpose of this nested class is to hide method
  // v8::internal::compiler::NodeProperties::ChangeOp which should not be
  // directly used by code in SimplifiedLowering.
  // SimplifiedLowering code should call SimplifiedLowering::ChangeOp instead,
  // in order to notify the changes to ObserveNodeManager and support the
  // %ObserveNode intrinsic.
  class NodeProperties : public compiler::NodeProperties {
    static void ChangeOp(Node* node, const Operator* new_op) { UNREACHABLE(); }
  };
  void ChangeOp(Node* node, const Operator* new_op);

  JSGraph* const jsgraph_;
  JSHeapBroker* broker_;
  Zone* const zone_;
  TypeCache const* type_cache_;
  SetOncePointer<Node> to_number_code_;
  SetOncePointer<Node> to_number_convert_big_int_code_;
  SetOncePointer<Node> to_numeric_code_;
  SetOncePointer<Operator const> to_number_operator_;
  SetOncePointer<Operator const> to_number_convert_big_int_operator_;
  SetOncePointer<Operator const> to_numeric_operator_;

  // TODO(danno): SimplifiedLowering shouldn't know anything about the source
  // positions table, but must for now since there currently is no other way to
  // pass down source position information to nodes created during
  // lowering. Once this phase becomes a vanilla reducer, it should get source
  // position information via the SourcePositionWrapper like all other reducers.
  SourcePositionTable* source_positions_;
  NodeOriginTable* node_origins_;

  TickCounter* const tick_counter_;
  Linkage* const linkage_;
  OptimizedCompilationInfo* info_;

  ObserveNodeManager* const observe_node_manager_;

  Node* Float64Round(Node* const node);
  Node* Float64Sign(Node* const node);
  Node* Int32Abs(Node* const node);
  Node* Int32Div(Node* const node);
  Node* Int32Mod(Node* const node);
  Node* Int32Sign(Node* const node);
  Node* Uint32Div(Node* const node);
  Node* Uint32Mod(Node* const node);

  Node* ToNumberCode();
  Node* ToNumberConvertBigIntCode();
  Node* ToNumericCode();
  Operator const* ToNumberOperator();
  Operator const* ToNumberConvertBigIntOperator();
  Operator const* ToNumericOperator();

  friend class RepresentationSelector;

  Isolate* isolate() { return jsgraph_->isolate(); }
  Zone* zone() { return jsgraph_->zone(); }
  JSGraph* jsgraph() { return jsgraph_; }
  Graph* graph() { return jsgraph()->graph(); }
  CommonOperatorBuilder* common() { return jsgraph()->common(); }
  MachineOperatorBuilder* machine() { return jsgraph()->machine(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph()->simplified(); }
  Linkage* linkage() { return linkage_; }
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_SIMPLIFIED_LOWERING_H_
