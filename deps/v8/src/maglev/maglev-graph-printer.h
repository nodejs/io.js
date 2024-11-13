// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_PRINTER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_PRINTER_H_

#include <memory>
#include <ostream>
#include <set>
#include <vector>

#include "src/maglev/maglev-compilation-unit.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

class BasicBlock;
class ControlNode;
class Graph;
class MaglevCompilationInfo;
class MaglevGraphLabeller;
class Node;
class NodeBase;
class Phi;
class ProcessingState;

#ifdef V8_ENABLE_MAGLEV_GRAPH_PRINTER

class MaglevPrintingVisitor {
 public:
  explicit MaglevPrintingVisitor(MaglevGraphLabeller* graph_labeller,
                                 std::ostream& os);

  void PreProcessGraph(Graph* graph);
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block);
  void PostPhiProcessing() {}
  ProcessResult Process(Phi* phi, const ProcessingState& state);
  ProcessResult Process(Node* node, const ProcessingState& state);
  ProcessResult Process(ControlNode* node, const ProcessingState& state);

  std::ostream& os() { return *os_for_additional_info_; }

 private:
  MaglevGraphLabeller* graph_labeller_;
  std::ostream& os_;
  std::unique_ptr<std::ostream> os_for_additional_info_;
  std::set<BasicBlock*> loop_headers_;
  std::vector<BasicBlock*> targets_;
  NodeIdT max_node_id_ = kInvalidNodeId;
  MaglevGraphLabeller::Provenance existing_provenance_;
};

void PrintGraph(std::ostream& os, MaglevCompilationInfo* compilation_info,
                Graph* const graph);

class PrintNode {
 public:
  PrintNode(MaglevGraphLabeller* graph_labeller, const NodeBase* node,
            bool skip_targets = false)
      : graph_labeller_(graph_labeller),
        node_(node),
        skip_targets_(skip_targets) {}

  void Print(std::ostream& os) const;

 private:
  MaglevGraphLabeller* graph_labeller_;
  const NodeBase* node_;
  // This is used when tracing graph building, since targets might not exist
  // yet.
  const bool skip_targets_;
};

class PrintNodeLabel {
 public:
  PrintNodeLabel(MaglevGraphLabeller* graph_labeller, const NodeBase* node)
      : graph_labeller_(graph_labeller), node_(node) {}

  void Print(std::ostream& os) const;

 private:
  MaglevGraphLabeller* graph_labeller_;
  const NodeBase* node_;
};

#else

// Dummy inlined definitions of the printer classes/functions.

class MaglevPrintingVisitor {
 public:
  explicit MaglevPrintingVisitor(MaglevGraphLabeller* graph_labeller,
                                 std::ostream& os)
      : os_(os) {}

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}
  ProcessResult Process(Phi* phi, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }
  ProcessResult Process(Node* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }
  ProcessResult Process(ControlNode* node, const ProcessingState& state) {
    return ProcessResult::kContinue;
  }

  std::ostream& os() { return os_; }

 private:
  std::ostream& os_;
};

inline void PrintGraph(std::ostream& os,
                       MaglevCompilationInfo* compilation_info,
                       Graph* const graph) {}

class PrintNode {
 public:
  PrintNode(MaglevGraphLabeller* graph_labeller, const NodeBase* node,
            bool skip_targets = false) {}
  void Print(std::ostream& os) const {}
};

class PrintNodeLabel {
 public:
  PrintNodeLabel(MaglevGraphLabeller* graph_labeller, const NodeBase* node) {}
  void Print(std::ostream& os) const {}
};

#endif  // V8_ENABLE_MAGLEV_GRAPH_PRINTER

inline std::ostream& operator<<(std::ostream& os, const PrintNode& printer) {
  printer.Print(os);
  return os;
}

inline std::ostream& operator<<(std::ostream& os,
                                const PrintNodeLabel& printer) {
  printer.Print(os);
  return os;
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_PRINTER_H_
