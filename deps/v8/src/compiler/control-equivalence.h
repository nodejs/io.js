// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_CONTROL_EQUIVALENCE_H_
#define V8_COMPILER_CONTROL_EQUIVALENCE_H_

#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

// Determines control dependence equivalence classes for control nodes. Any two
// nodes having the same set of control dependences land in one class. These
// classes can in turn be used to:
//  - Build a program structure tree (PST) for controls in the graph.
//  - Determine single-entry single-exit (SESE) regions within the graph.
//
// Note that this implementation actually uses cycle equivalence to establish
// class numbers. Any two nodes are cycle equivalent if they occur in the same
// set of cycles. It can be shown that control dependence equivalence reduces
// to undirected cycle equivalence for strongly connected control flow graphs.
//
// The algorithm is based on the paper, "The program structure tree: computing
// control regions in linear time" by Johnson, Pearson & Pingali (PLDI94) which
// also contains proofs for the aforementioned equivalence. References to line
// numbers in the algorithm from figure 4 have been added [line:x].
class V8_EXPORT_PRIVATE ControlEquivalence final
    : public NON_EXPORTED_BASE(ZoneObject) {
 public:
  ControlEquivalence(Zone* zone, Graph* graph)
      : zone_(zone),
        graph_(graph),
        dfs_number_(0),
        class_number_(1),
        node_data_(graph->NodeCount(), zone) {}

  // Run the main algorithm starting from the {exit} control node. This causes
  // the following iterations over control edges of the graph:
  //  1) A breadth-first backwards traversal to determine the set of nodes that
  //     participate in the next step. Takes O(E) time and O(N) space.
  //  2) An undirected depth-first backwards traversal that determines class
  //     numbers for all participating nodes. Takes O(E) time and O(N) space.
  void Run(Node* exit);

  // Retrieves a previously computed class number.
  size_t ClassOf(Node* node) {
    DCHECK_NE(kInvalidClass, GetClass(node));
    return GetClass(node);
  }

 private:
  static const size_t kInvalidClass = static_cast<size_t>(-1);
  enum DFSDirection { kInputDirection, kUseDirection };

  struct Bracket {
    DFSDirection direction;  // Direction in which this bracket was added.
    size_t recent_class;     // Cached class when bracket was topmost.
    size_t recent_size;      // Cached set-size when bracket was topmost.
    Node* from;              // Node that this bracket originates from.
    Node* to;                // Node that this bracket points to.
  };

  // The set of brackets for each node during the DFS walk.
  using BracketList = ZoneLinkedList<Bracket>;

  struct DFSStackEntry {
    DFSDirection direction;            // Direction currently used in DFS walk.
    Node::InputEdges::iterator input;  // Iterator used for "input" direction.
    Node::UseEdges::iterator use;      // Iterator used for "use" direction.
    Node* parent_node;                 // Parent node of entry during DFS walk.
    Node* node;                        // Node that this stack entry belongs to.
  };

  // The stack is used during the undirected DFS walk.
  using DFSStack = ZoneStack<DFSStackEntry>;

  struct NodeData : ZoneObject {
    explicit NodeData(Zone* zone)
        : class_number(kInvalidClass),
          blist(BracketList(zone)),
          visited(false),
          on_stack(false) {}

    size_t class_number;  // Equivalence class number assigned to node.
    BracketList blist;    // List of brackets per node.
    bool visited : 1;     // Indicates node has already been visited.
    bool on_stack : 1;    // Indicates node is on DFS stack during walk.
  };

  // The per-node data computed during the DFS walk.
  using Data = ZoneVector<NodeData*>;

  // Called at pre-visit during DFS walk.
  void VisitPre(Node* node);

  // Called at mid-visit during DFS walk.
  void VisitMid(Node* node, DFSDirection direction);

  // Called at post-visit during DFS walk.
  void VisitPost(Node* node, Node* parent_node, DFSDirection direction);

  // Called when hitting a back edge in the DFS walk.
  void VisitBackedge(Node* from, Node* to, DFSDirection direction);

  // Performs and undirected DFS walk of the graph. Conceptually all nodes are
  // expanded, splitting "input" and "use" out into separate nodes. During the
  // traversal, edges towards the representative nodes are preferred.
  //
  //   \ /        - Pre-visit: When N1 is visited in direction D the preferred
  //    x   N1      edge towards N is taken next, calling VisitPre(N).
  //    |         - Mid-visit: After all edges out of N2 in direction D have
  //    |   N       been visited, we switch the direction and start considering
  //    |           edges out of N1 now, and we call VisitMid(N).
  //    x   N2    - Post-visit: After all edges out of N1 in direction opposite
  //   / \          to D have been visited, we pop N and call VisitPost(N).
  //
  // This will yield a true spanning tree (without cross or forward edges) and
  // also discover proper back edges in both directions.
  void RunUndirectedDFS(Node* exit);

  void DetermineParticipationEnqueue(ZoneQueue<Node*>& queue, Node* node);
  void DetermineParticipation(Node* exit);

 private:
  NodeData* GetData(Node* node) {
    size_t const index = node->id();
    if (index >= node_data_.size()) node_data_.resize(index + 1);
    return node_data_[index];
  }
  void AllocateData(Node* node) {
    size_t const index = node->id();
    if (index >= node_data_.size()) node_data_.resize(index + 1);
    node_data_[index] = zone_->New<NodeData>(zone_);
  }

  int NewClassNumber() { return class_number_++; }
  int NewDFSNumber() { return dfs_number_++; }

  bool Participates(Node* node) { return GetData(node) != nullptr; }

  // Accessors for the equivalence class stored within the per-node data.
  size_t GetClass(Node* node) { return GetData(node)->class_number; }
  void SetClass(Node* node, size_t number) {
    DCHECK(Participates(node));
    GetData(node)->class_number = number;
  }

  // Accessors for the bracket list stored within the per-node data.
  BracketList& GetBracketList(Node* node) {
    DCHECK(Participates(node));
    return GetData(node)->blist;
  }
  void SetBracketList(Node* node, BracketList& list) {
    DCHECK(Participates(node));
    GetData(node)->blist = list;
  }

  // Mutates the DFS stack by pushing an entry.
  void DFSPush(DFSStack& stack, Node* node, Node* from, DFSDirection dir);

  // Mutates the DFS stack by popping an entry.
  void DFSPop(DFSStack& stack, Node* node);

  void BracketListDelete(BracketList& blist, Node* to, DFSDirection direction);
  void BracketListTRACE(BracketList& blist);

  Zone* const zone_;
  Graph* const graph_;
  int dfs_number_;    // Generates new DFS pre-order numbers on demand.
  int class_number_;  // Generates new equivalence class numbers on demand.
  Data node_data_;    // Per-node data stored as a side-table.
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_CONTROL_EQUIVALENCE_H_
