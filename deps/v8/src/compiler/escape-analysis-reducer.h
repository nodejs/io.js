// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/base/compiler-specific.h"
#include "src/common/globals.h"
#include "src/compiler/escape-analysis.h"
#include "src/compiler/graph-reducer.h"

namespace v8 {
namespace internal {
namespace compiler {

class Deduplicator;
class JSGraph;

// Perform hash-consing when creating or mutating nodes. Used to avoid duplicate
// nodes when creating ObjectState, StateValues and FrameState nodes
class NodeHashCache {
 public:
  NodeHashCache(Graph* graph, Zone* zone)
      : graph_(graph), cache_(zone), temp_nodes_(zone) {}

  // Handle to a conceptually new mutable node. Tries to reuse existing nodes
  // and to recycle memory if possible.
  class Constructor {
   public:
    // Construct a new node as a clone of [from].
    Constructor(NodeHashCache* cache, Node* from)
        : node_cache_(cache), from_(from), tmp_(nullptr) {}
    // Construct a new node from scratch.
    Constructor(NodeHashCache* cache, const Operator* op, int input_count,
                Node** inputs, Type type);

    // Modify the new node.
    void ReplaceValueInput(Node* input, int i) {
      if (!tmp_ && input == NodeProperties::GetValueInput(from_, i)) return;
      Node* node = MutableNode();
      NodeProperties::ReplaceValueInput(node, input, i);
    }
    void ReplaceInput(Node* input, int i) {
      if (!tmp_ && input == from_->InputAt(i)) return;
      Node* node = MutableNode();
      node->ReplaceInput(i, input);
    }

    // Obtain the mutated node or a cached copy. Invalidates the [Constructor].
    Node* Get();

   private:
    Node* MutableNode();

    NodeHashCache* node_cache_;
    // Original node, copied on write.
    Node* from_;
    // Temporary node used for mutations, can be recycled if cache is hit.
    Node* tmp_;
  };

 private:
  Node* Query(Node* node);
  void Insert(Node* node) { cache_.insert(node); }

  Graph* graph_;
  struct NodeEquals {
    bool operator()(Node* a, Node* b) const {
      return NodeProperties::Equals(a, b);
    }
  };
  struct NodeHashCode {
    size_t operator()(Node* n) const { return NodeProperties::HashCode(n); }
  };
  ZoneUnorderedSet<Node*, NodeHashCode, NodeEquals> cache_;
  // Unused nodes whose memory can be recycled.
  ZoneVector<Node*> temp_nodes_;
};

// Modify the graph according to the information computed in the previous phase.
class V8_EXPORT_PRIVATE EscapeAnalysisReducer final
    : public NON_EXPORTED_BASE(AdvancedReducer) {
 public:
  EscapeAnalysisReducer(Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker,
                        EscapeAnalysisResult analysis_result, Zone* zone);
  EscapeAnalysisReducer(const EscapeAnalysisReducer&) = delete;
  EscapeAnalysisReducer& operator=(const EscapeAnalysisReducer&) = delete;

  Reduction Reduce(Node* node) override;
  const char* reducer_name() const override { return "EscapeAnalysisReducer"; }
  void Finalize() override;

  // Verifies that all virtual allocation nodes have been dealt with. Run it
  // after this reducer has been applied.
  void VerifyReplacement() const;

 private:
  void ReduceFrameStateInputs(Node* node);
  Node* ReduceDeoptState(Node* node, Node* effect, Deduplicator* deduplicator);
  Node* ObjectIdNode(const VirtualObject* vobject);
  Reduction ReplaceNode(Node* original, Node* replacement);

  JSGraph* jsgraph() const { return jsgraph_; }
  Isolate* isolate() const { return jsgraph_->isolate(); }
  EscapeAnalysisResult analysis_result() const { return analysis_result_; }
  Zone* zone() const { return zone_; }

  JSGraph* const jsgraph_;
  JSHeapBroker* const broker_;
  EscapeAnalysisResult analysis_result_;
  ZoneVector<Node*> object_id_cache_;
  NodeHashCache node_cache_;
  ZoneSet<Node*> arguments_elements_;
  Zone* const zone_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_ESCAPE_ANALYSIS_REDUCER_H_
