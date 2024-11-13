// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_LABELLER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_LABELLER_H_

#include <map>

#include "src/maglev/maglev-graph.h"
#include "src/maglev/maglev-ir.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevGraphLabeller {
 public:
  struct Provenance {
    const MaglevCompilationUnit* unit = nullptr;
    BytecodeOffset bytecode_offset = BytecodeOffset::None();
    SourcePosition position = SourcePosition::Unknown();
  };
  struct NodeInfo {
    int label = -1;
    Provenance provenance;
  };

  void RegisterNode(const NodeBase* node, const MaglevCompilationUnit* unit,
                    BytecodeOffset bytecode_offset, SourcePosition position) {
    if (nodes_
            .emplace(node, NodeInfo{next_node_label_,
                                    {unit, bytecode_offset, position}})
            .second) {
      next_node_label_++;
    }
  }
  void RegisterNode(const NodeBase* node) {
    RegisterNode(node, nullptr, BytecodeOffset::None(),
                 SourcePosition::Unknown());
  }
  void RegisterBasicBlock(const BasicBlock* block) {
    block_ids_[block] = next_block_label_++;
  }

  int BlockId(const BasicBlock* block) { return block_ids_[block]; }
  int NodeId(const NodeBase* node) { return nodes_[node].label; }
  const Provenance& GetNodeProvenance(const NodeBase* node) {
    return nodes_[node].provenance;
  }

  int max_node_id() const { return next_node_label_ - 1; }

  void PrintNodeLabel(std::ostream& os, const NodeBase* node) {
    if (node != nullptr && node->Is<VirtualObject>()) {
      // VirtualObjects are unregisted nodes, since they are not attached to
      // the graph, but its inlined allocation is.
      const VirtualObject* vo = node->Cast<VirtualObject>();
      os << "VO{" << vo->id() << "}:";
      node = vo->allocation();
    }
    auto node_id_it = nodes_.find(node);

    if (node_id_it == nodes_.end()) {
      os << "<unregistered node " << node << ">";
      return;
    }

    if (node->has_id()) {
      os << "v" << node->id() << "/";
    }
    os << "n" << node_id_it->second.label;
  }

  void PrintInput(std::ostream& os, const Input& input) {
    PrintNodeLabel(os, input.node());
    os << ":" << input.operand();
  }

 private:
  std::map<const BasicBlock*, int> block_ids_;
  std::map<const NodeBase*, NodeInfo> nodes_;
  int next_block_label_ = 1;
  int next_node_label_ = 1;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_LABELLER_H_
