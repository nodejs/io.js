// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_GRAPH_VERIFIER_H_
#define V8_MAGLEV_MAGLEV_GRAPH_VERIFIER_H_

#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir.h"

namespace v8 {
namespace internal {
namespace maglev {

class Graph;

// TODO(victorgomes): Add more verification.
class MaglevGraphVerifier {
 public:
  explicit MaglevGraphVerifier(MaglevCompilationInfo* compilation_info) {
    if (compilation_info->has_graph_labeller()) {
      graph_labeller_ = compilation_info->graph_labeller();
    }
  }

  void PreProcessGraph(Graph* graph) {}
  void PostProcessGraph(Graph* graph) {}
  BlockProcessResult PreProcessBasicBlock(BasicBlock* block) {
    return BlockProcessResult::kContinue;
  }
  void PostPhiProcessing() {}

  template <typename NodeT>
  ProcessResult Process(NodeT* node, const ProcessingState& state) {
    node->VerifyInputs(graph_labeller_);
    return ProcessResult::kContinue;
  }

 private:
  MaglevGraphLabeller* graph_labeller_ = nullptr;
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_GRAPH_VERIFIER_H_
