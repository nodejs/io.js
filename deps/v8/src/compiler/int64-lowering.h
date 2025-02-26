// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_INT64_LOWERING_H_
#define V8_COMPILER_INT64_LOWERING_H_

#include <memory>

#include "src/compiler/common-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

template <typename T>
class Signature;

namespace compiler {

#if !V8_TARGET_ARCH_32_BIT
class Int64Lowering {
 public:
  Int64Lowering(Graph* graph, MachineOperatorBuilder* machine,
                CommonOperatorBuilder* common,
                SimplifiedOperatorBuilder* simplified_, Zone* zone,
                Signature<MachineRepresentation>* signature) {}

  void LowerGraph() {}
};
#else

class V8_EXPORT_PRIVATE Int64Lowering {
 public:
  Int64Lowering(Graph* graph, MachineOperatorBuilder* machine,
                CommonOperatorBuilder* common,
                SimplifiedOperatorBuilder* simplified_, Zone* zone,
                Signature<MachineRepresentation>* signature);

  void LowerGraph();

 private:
  static int GetParameterCountAfterLowering(
      Signature<MachineRepresentation>* signature);

  enum class State : uint8_t { kUnvisited, kOnStack, kVisited };

  struct Replacement {
    Node* low;
    Node* high;
  };

  Zone* zone() const { return zone_; }
  Graph* graph() const { return graph_; }
  MachineOperatorBuilder* machine() const { return machine_; }
  CommonOperatorBuilder* common() const { return common_; }
  SimplifiedOperatorBuilder* simplified() const { return simplified_; }
  Signature<MachineRepresentation>* signature() const { return signature_; }

  void PushNode(Node* node);
  void LowerNode(Node* node);
  bool DefaultLowering(Node* node, bool low_word_only = false);
  void LowerComparison(Node* node, const Operator* signed_op,
                       const Operator* unsigned_op);
  void LowerWord64AtomicBinop(Node* node, const Operator* op);
  void LowerWord64AtomicNarrowOp(Node* node, const Operator* op);
  void LowerLoadOperator(Node* node, MachineRepresentation rep,
                         const Operator* load_op);
  void LowerStoreOperator(Node* node, MachineRepresentation rep,
                          const Operator* store_op);

  const CallDescriptor* LowerCallDescriptor(
      const CallDescriptor* call_descriptor);

  void ReplaceNode(Node* old, Node* new_low, Node* new_high);
  bool HasReplacementLow(Node* node);
  Node* GetReplacementLow(Node* node);
  bool HasReplacementHigh(Node* node);
  Node* GetReplacementHigh(Node* node);
  void PreparePhiReplacement(Node* phi);
  void GetIndexNodes(Node* index, Node** index_low, Node** index_high);
  void ReplaceNodeWithProjections(Node* node);
  void LowerMemoryBaseAndIndex(Node* node);

  struct NodeState {
    Node* node;
    int input_index;
  };

  Graph* const graph_;
  MachineOperatorBuilder* machine_;
  CommonOperatorBuilder* common_;
  SimplifiedOperatorBuilder* simplified_;
  Zone* zone_;
  Signature<MachineRepresentation>* signature_;
  std::vector<State> state_;
  ZoneDeque<NodeState> stack_;
  Replacement* replacements_;
  Node* placeholder_;
};

#endif  // V8_TARGET_ARCH_32_BIT

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_INT64_LOWERING_H_
