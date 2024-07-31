// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_REVECTORIZER_H_
#define V8_COMPILER_REVECTORIZER_H_

// Revectorizer is an optimization to promote pairs of simd128 nodes to new
// simd256 nodes accelerated by wider vector available from hardware e.g. the
// YMM registers from AVX2 instruction set when possible and beneficial. The
// main algorithm is based on the Superword Level Parallel (SLP) vectorization
// technique.

#include <vector>

#include "src/base/small-vector.h"
#include "src/compiler/graph.h"
#include "src/compiler/linear-scheduler.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

class SourcePositionTable;

struct V8_EXPORT_PRIVATE MemoryOffsetComparer {
  bool operator()(const Node* lhs, const Node* rhs) const;
};

using StoreNodeSet = ZoneSet<Node*, MemoryOffsetComparer>;

// A PackNode consists of a fixed number of isomorphic simd128 nodes which can
// execute in parallel and convert to a 256-bit simd node later. The nodes in a
// PackNode must satisfy that they can be scheduled in the same basic block and
// are mutually independent.
class PackNode final : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit PackNode(Zone* zone, const ZoneVector<Node*>& node_group)
      : nodes_(node_group.cbegin(), node_group.cend(), zone),
        operands_(zone),
        revectorized_node_(nullptr) {}
  const ZoneVector<Node*>& Nodes() const { return nodes_; }
  bool IsSame(const ZoneVector<Node*>& node_group) const {
    return nodes_ == node_group;
  }
  Node* RevectorizedNode() const { return revectorized_node_; }
  void SetRevectorizedNode(Node* node) { revectorized_node_ = node; }
  // returns the index operand of this PackNode.
  PackNode* GetOperand(size_t index) {
    DCHECK_LT(index, operands_.size());
    return operands_[index];
  }

  ZoneVector<PackNode*>::size_type GetOperandsSize() const {
    return operands_.size();
  }

  void SetOperand(size_t index, PackNode* pnode) {
    if (operands_.size() < index + 1) operands_.resize(index + 1);
    operands_[index] = pnode;
  }

  void Print() const;

 private:
  ZoneVector<Node*> nodes_;
  ZoneVector<PackNode*> operands_;
  Node* revectorized_node_;
};

// An auxillary tree structure with a set of PackNodes based on the Superword
// Level Parallelism (SLP) vectorization technique. The BuildTree method will
// start from a selected root, e.g. a group of consecutive stores, and extend
// through value inputs to create new PackNodes if the inputs are valid, or
// conclude that the current PackNode is a leaf and terminate the tree.
// Below is an example of SLPTree where loads and stores in each PackNode are
// all consecutive.
// [Load0, Load1]  [Load2, Load3]
//           \       /
//          [Add0, Add1]
//                |
//         [Store0, Store1]
class SLPTree : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit SLPTree(Zone* zone, Graph* graph)
      : zone_(zone),
        graph_(graph),
        root_(nullptr),
        on_stack_(zone),
        stack_(zone),
        node_to_packnode_(zone) {
    scheduler_ = zone->New<LinearScheduler>(zone, graph);
  }

  PackNode* BuildTree(const ZoneVector<Node*>& roots);
  void DeleteTree();

  PackNode* GetPackNode(Node* node);

  void Print(const char* info);

  template <typename FunctionType>
  void ForEach(FunctionType callback);

  Node* GetEarlySchedulePosition(Node* node) {
    return scheduler_->GetEarlySchedulePosition(node);
  }

 private:
  friend class LinearScheduler;

  // This is the recursive part of BuildTree.
  PackNode* BuildTreeRec(const ZoneVector<Node*>& node_group, unsigned depth);

  // Baseline: create a new PackNode, and return.
  PackNode* NewPackNode(const ZoneVector<Node*>& node_group);

  // Recursion: create a new PackNode and call BuildTreeRec recursively
  PackNode* NewPackNodeAndRecurs(const ZoneVector<Node*>& node_group,
                                 int start_index, int count, unsigned depth);

  bool CanBePacked(const ZoneVector<Node*>& node_group);

  Graph* graph() const { return graph_; }
  Zone* zone() const { return zone_; }

  // Node stack operations.
  void PopStack();
  void PushStack(const ZoneVector<Node*>& node_group);
  void ClearStack();
  bool OnStack(Node* node);
  bool AllOnStack(const ZoneVector<Node*>& node_group);
  bool StackTopIsPhi();

  void TryReduceLoadChain(const ZoneVector<Node*>& loads);
  bool IsSideEffectFreeLoad(const ZoneVector<Node*>& node_group);
  bool SameBasicBlock(Node* node0, Node* node1) {
    return scheduler_->SameBasicBlock(node0, node1);
  }

  Zone* const zone_;
  Graph* const graph_;
  PackNode* root_;
  LinearScheduler* scheduler_;
  ZoneSet<Node*> on_stack_;
  ZoneStack<ZoneVector<Node*>> stack_;
  // Maps a specific node to PackNode.
  ZoneUnorderedMap<Node*, PackNode*> node_to_packnode_;
  static constexpr size_t RecursionMaxDepth = 1000;
};

// The Revectorizer pass will firstly collect seeds with valid group of
// consecutive stores as the root to build the SLPTree. If the SLPTree is built
// successfully, it will estimate the cost of the 256-bit transformation for
// each PackNode and conduct the final revectorization if benefitial.
class V8_EXPORT_PRIVATE Revectorizer final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  Revectorizer(Zone* zone, Graph* graph, MachineGraph* mcgraph,
               SourcePositionTable* source_positions);
  void DetectCPUFeatures();
  bool TryRevectorize(const char* name);

 private:
  void CollectSeeds();

  bool ReduceStoreChains(ZoneMap<Node*, StoreNodeSet>* store_chains);
  bool ReduceStoreChain(const ZoneVector<Node*>& Stores);

  void PrintStores(ZoneMap<Node*, StoreNodeSet>* store_chains);
  Zone* zone() const { return zone_; }
  Graph* graph() const { return graph_; }
  MachineGraph* mcgraph() const { return mcgraph_; }

  PackNode* GetPackNode(Node* node) const {
    return slp_tree_->GetPackNode(node);
  }

  bool DecideVectorize();

  void SetEffectInput(PackNode* pnode, int index, Node*& nput);
  void SetMemoryOpInputs(base::SmallVector<Node*, 2>& inputs, PackNode* pnode,
                         int index);
  Node* VectorizeTree(PackNode* pnode);
  void UpdateSources();

  Zone* const zone_;
  Graph* const graph_;
  MachineGraph* const mcgraph_;
  ZoneMap<Node*, ZoneMap<Node*, StoreNodeSet>*> group_of_stores_;
  std::unordered_set<Node*> sources_;
  SLPTree* slp_tree_;
  SourcePositionTable* source_positions_;

  bool support_simd256_;

  compiler::NodeObserver* node_observer_for_test_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_REVECTORIZER_H_
