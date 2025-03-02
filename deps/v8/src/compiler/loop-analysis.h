// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOOP_ANALYSIS_H_
#define V8_COMPILER_LOOP_ANALYSIS_H_

#include "src/base/iterator.h"
#include "src/common/globals.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class TickCounter;

namespace compiler {

// TODO(titzer): don't assume entry edges have a particular index.
static const int kAssumedLoopEntryIndex = 0;  // assume loops are entered here.

class LoopFinderImpl;
class AllNodes;

using NodeRange = base::iterator_range<Node**>;

// Represents a tree of loops in a graph.
class LoopTree : public ZoneObject {
 public:
  LoopTree(size_t num_nodes, Zone* zone)
      : zone_(zone),
        outer_loops_(zone),
        all_loops_(zone),
        node_to_loop_num_(static_cast<int>(num_nodes), -1, zone),
        loop_nodes_(zone) {}

  // Represents a loop in the tree of loops, including the header nodes,
  // the body, and any nested loops.
  class Loop {
   public:
    Loop* parent() const { return parent_; }
    const ZoneVector<Loop*>& children() const { return children_; }
    uint32_t HeaderSize() const { return body_start_ - header_start_; }
    uint32_t BodySize() const { return exits_start_ - body_start_; }
    uint32_t ExitsSize() const { return exits_end_ - exits_start_; }
    uint32_t TotalSize() const { return exits_end_ - header_start_; }
    uint32_t depth() const { return depth_; }

   private:
    friend class LoopTree;
    friend class LoopFinderImpl;

    explicit Loop(Zone* zone)
        : parent_(nullptr),
          depth_(0),
          children_(zone),
          header_start_(-1),
          body_start_(-1),
          exits_start_(-1),
          exits_end_(-1) {}
    Loop* parent_;
    int depth_;
    ZoneVector<Loop*> children_;
    int header_start_;
    int body_start_;
    int exits_start_;
    int exits_end_;
  };

  // Return the innermost nested loop, if any, that contains {node}.
  Loop* ContainingLoop(Node* node) {
    if (node->id() >= node_to_loop_num_.size()) return nullptr;
    int num = node_to_loop_num_[node->id()];
    return num > 0 ? &all_loops_[num - 1] : nullptr;
  }

  // Check if the {loop} contains the {node}, either directly or by containing
  // a nested loop that contains {node}.
  bool Contains(const Loop* loop, Node* node) {
    for (Loop* c = ContainingLoop(node); c != nullptr; c = c->parent_) {
      if (c == loop) return true;
    }
    return false;
  }

  // Return the list of outer loops.
  const ZoneVector<Loop*>& outer_loops() const { return outer_loops_; }

  // Return a new vector containing the inner loops.
  ZoneVector<const Loop*> inner_loops() const {
    ZoneVector<const Loop*> inner_loops(zone_);
    for (const Loop& loop : all_loops_) {
      if (loop.children().empty()) {
        inner_loops.push_back(&loop);
      }
    }
    return inner_loops;
  }

  // Return the unique loop number for a given loop. Loop numbers start at {1}.
  int LoopNum(const Loop* loop) const {
    return 1 + static_cast<int>(loop - &all_loops_[0]);
  }

  // Return a range which can iterate over the header nodes of {loop}.
  NodeRange HeaderNodes(const Loop* loop) {
    return NodeRange(&loop_nodes_[0] + loop->header_start_,
                     &loop_nodes_[0] + loop->body_start_);
  }

  // Return the header control node for a loop.
  Node* HeaderNode(const Loop* loop);

  // Return a range which can iterate over the body nodes of {loop}.
  NodeRange BodyNodes(const Loop* loop) {
    return NodeRange(&loop_nodes_[0] + loop->body_start_,
                     &loop_nodes_[0] + loop->exits_start_);
  }

  // Return a range which can iterate over the body nodes of {loop}.
  NodeRange ExitNodes(const Loop* loop) {
    return NodeRange(&loop_nodes_[0] + loop->exits_start_,
                     &loop_nodes_[0] + loop->exits_end_);
  }

  // Return a range which can iterate over the nodes of {loop}.
  NodeRange LoopNodes(const Loop* loop) {
    return NodeRange(&loop_nodes_[0] + loop->header_start_,
                     &loop_nodes_[0] + loop->exits_end_);
  }

  // Return the node that represents the control, i.e. the loop node itself.
  Node* GetLoopControl(const Loop* loop) {
    // TODO(turbofan): make the loop control node always first?
    for (Node* node : HeaderNodes(loop)) {
      if (node->opcode() == IrOpcode::kLoop) return node;
    }
    UNREACHABLE();
  }

  Zone* zone() const { return zone_; }

 private:
  friend class LoopFinderImpl;

  Loop* NewLoop() {
    all_loops_.push_back(Loop(zone_));
    Loop* result = &all_loops_.back();
    return result;
  }

  void SetParent(Loop* parent, Loop* child) {
    if (parent != nullptr) {
      parent->children_.push_back(child);
      child->parent_ = parent;
      child->depth_ = parent->depth_ + 1;
    } else {
      outer_loops_.push_back(child);
    }
  }

  Zone* zone_;
  ZoneVector<Loop*> outer_loops_;
  ZoneVector<Loop> all_loops_;
  ZoneVector<int> node_to_loop_num_;
  ZoneVector<Node*> loop_nodes_;
};

class V8_EXPORT_PRIVATE LoopFinder {
 public:
  // Build a loop tree for the entire graph.
  static LoopTree* BuildLoopTree(Graph* graph, TickCounter* tick_counter,
                                 Zone* temp_zone);

  static bool HasMarkedExits(LoopTree* loop_tree, const LoopTree::Loop* loop);

#if V8_ENABLE_WEBASSEMBLY
  enum class Purpose { kLoopPeeling, kLoopUnrolling };

  // Find all nodes in the loop headed by {loop_header} if it contains no nested
  // loops.
  // Assumption: *if* this loop has no nested loops, all exits from the loop are
  // marked with LoopExit, LoopExitEffect, LoopExitValue, or End nodes.
  // Returns {nullptr} if
  // 1) the loop size (in graph nodes) exceeds {max_size},
  // 2) {calls_are_large} and a function call is found in the loop, excluding
  //    calls to a set of wasm builtins,
  // 3) a nested loop is found in the loop.
  static ZoneUnorderedSet<Node*>* FindSmallInnermostLoopFromHeader(
      Node* loop_header, AllNodes& all_nodes, Zone* zone, size_t max_size,
      Purpose purpose);
#endif
};

// Copies a range of nodes any number of times.
class NodeCopier {
 public:
  // {max}: The maximum number of nodes that this copier will track, including
  //        the original nodes and all copies.
  // {p}: A vector that holds the original nodes and all copies.
  // {copy_count}: How many times the nodes should be copied.
  NodeCopier(Graph* graph, uint32_t max, NodeVector* p, uint32_t copy_count)
      : node_map_(graph, max), copies_(p), copy_count_(copy_count) {
    DCHECK_GT(copy_count, 0);
  }

  // Returns the mapping of {node} in the {copy_index}'th copy, or {node} itself
  // if it is not present in the mapping. The copies are 0-indexed.
  Node* map(Node* node, uint32_t copy_index);

  // Helper version of {map} for one copy.
  V8_INLINE Node* map(Node* node) { return map(node, 0); }

  // Insert a new mapping from {original} to {new_copies} into the copier.
  void Insert(Node* original, const NodeVector& new_copies);

  // Helper version of {Insert} for one copy.
  void Insert(Node* original, Node* copy);

  template <typename InputIterator>
  void CopyNodes(Graph* graph, Zone* tmp_zone_, Node* dead,
                 base::iterator_range<InputIterator> nodes,
                 SourcePositionTable* source_positions,
                 NodeOriginTable* node_origins) {
    // Copy all the nodes first.
    for (Node* original : nodes) {
      SourcePositionTable::Scope position(
          source_positions, source_positions->GetSourcePosition(original));
      NodeOriginTable::Scope origin_scope(node_origins, "copy nodes", original);
      node_map_.Set(original, copies_->size() + 1);
      copies_->push_back(original);
      for (uint32_t copy_index = 0; copy_index < copy_count_; copy_index++) {
        Node* copy = graph->CloneNode(original);
        copies_->push_back(copy);
      }
    }

    // Fix inputs of the copies.
    for (Node* original : nodes) {
      for (uint32_t copy_index = 0; copy_index < copy_count_; copy_index++) {
        Node* copy = map(original, copy_index);
        for (int i = 0; i < copy->InputCount(); i++) {
          copy->ReplaceInput(i, map(original->InputAt(i), copy_index));
        }
      }
    }
  }

  bool Marked(Node* node) { return node_map_.Get(node) > 0; }

 private:
  // Maps a node to its index in the {copies_} vector.
  NodeMarker<size_t> node_map_;
  // The vector which contains the mapped nodes.
  NodeVector* copies_;
  // How many copies of the nodes should be generated.
  const uint32_t copy_count_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOOP_ANALYSIS_H_
