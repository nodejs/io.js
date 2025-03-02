// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOFAN_GRAPH_H_
#define V8_COMPILER_TURBOFAN_GRAPH_H_

#include <array>

#include "src/base/compiler-specific.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// Forward declarations.
class GraphDecorator;
class Node;
class Operator;

// Marks are used during traversal of the graph to distinguish states of nodes.
// Each node has a mark which is a monotonically increasing integer, and a
// {NodeMarker} has a range of values that indicate states of a node.
using Mark = uint32_t;

// NodeIds are identifying numbers for nodes that can be used to index auxiliary
// out-of-line data associated with each node.
using NodeId = uint32_t;

class V8_EXPORT_PRIVATE Graph final : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  explicit Graph(Zone* zone);
  Graph(const Graph&) = delete;
  Graph& operator=(const Graph&) = delete;

  // Scope used when creating a subgraph for inlining. Automatically preserves
  // the original start and end nodes of the graph, and resets them when you
  // leave the scope.
  class V8_NODISCARD SubgraphScope final {
   public:
    explicit SubgraphScope(Graph* graph)
        : graph_(graph), start_(graph->start()), end_(graph->end()) {}
    ~SubgraphScope() {
      graph_->SetStart(start_);
      graph_->SetEnd(end_);
    }
    SubgraphScope(const SubgraphScope&) = delete;
    SubgraphScope& operator=(const SubgraphScope&) = delete;

   private:
    Graph* const graph_;
    Node* const start_;
    Node* const end_;
  };

  // Base implementation used by all factory methods.
  Node* NewNodeUnchecked(const Operator* op, int input_count,
                         Node* const* inputs, bool incomplete = false);

  // Factory that checks the input count.
  Node* NewNode(const Operator* op, int input_count, Node* const* inputs,
                bool incomplete = false);

  // Factory template for nodes with static input counts.
  // Note: Template magic below is used to ensure this method is only considered
  // for argument types convertible to Node* during overload resolution.
  template <typename... Nodes>
  Node* NewNode(const Operator* op, Nodes... nodes)
    requires(std::conjunction_v<std::is_convertible<Nodes, Node*>...>)
  {
    std::array<Node*, sizeof...(nodes)> nodes_arr{
        {static_cast<Node*>(nodes)...}};
    return NewNode(op, nodes_arr.size(), nodes_arr.data());
  }

  // Clone the {node}, and assign a new node id to the copy.
  Node* CloneNode(const Node* node);

  Zone* zone() const { return zone_; }
  Node* start() const { return start_; }
  Node* end() const { return end_; }

  void SetStart(Node* start) { start_ = start; }
  void SetEnd(Node* end) { end_ = end; }

  size_t NodeCount() const { return next_node_id_; }

  void Decorate(Node* node);
  void AddDecorator(GraphDecorator* decorator);
  void RemoveDecorator(GraphDecorator* decorator);

  // Very simple print API usable in a debugger.
  void Print() const;

  bool HasSimd() const { return has_simd_; }
  void SetSimd(bool has_simd) { has_simd_ = has_simd; }

  void RecordSimdStore(Node* store);
  ZoneVector<Node*> const& GetSimdStoreNodes();

 private:
  friend class NodeMarkerBase;

  inline NodeId NextNodeId();

  Zone* const zone_;
  Node* start_;
  Node* end_;
  Mark mark_max_;
  NodeId next_node_id_;
  ZoneVector<GraphDecorator*> decorators_;
  bool has_simd_;
  ZoneVector<Node*> simd_stores_;
};

// A graph decorator can be used to add behavior to the creation of nodes
// in a graph.
class GraphDecorator : public ZoneObject {
 public:
  virtual ~GraphDecorator() = default;
  virtual void Decorate(Node* node) = 0;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_TURBOFAN_GRAPH_H_
