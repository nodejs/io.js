// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_DIAMOND_H_
#define V8_COMPILER_DIAMOND_H_

#include "src/compiler/common-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

// A helper to make it easier to build diamond-shaped control patterns.
struct Diamond {
  Graph* graph;
  CommonOperatorBuilder* common;
  Node* branch;
  Node* if_true;
  Node* if_false;
  Node* merge;

  Diamond(Graph* g, CommonOperatorBuilder* b, Node* cond,
          BranchHint hint = BranchHint::kNone,
          BranchSemantics semantics = BranchSemantics::kUnspecified) {
    graph = g;
    common = b;
    branch =
        graph->NewNode(common->Branch(hint, semantics), cond, graph->start());
    if_true = graph->NewNode(common->IfTrue(), branch);
    if_false = graph->NewNode(common->IfFalse(), branch);
    merge = graph->NewNode(common->Merge(2), if_true, if_false);
  }

  // Place {this} after {that} in control flow order.
  void Chain(Diamond const& that) { branch->ReplaceInput(1, that.merge); }

  // Place {this} after {that} in control flow order.
  void Chain(Node* that) { branch->ReplaceInput(1, that); }

  // Nest {this} into either the if_true or if_false branch of {that}.
  void Nest(Diamond const& that, bool cond) {
    if (cond) {
      branch->ReplaceInput(1, that.if_true);
      that.merge->ReplaceInput(0, merge);
    } else {
      branch->ReplaceInput(1, that.if_false);
      that.merge->ReplaceInput(1, merge);
    }
  }

  Node* Phi(MachineRepresentation rep, Node* tv, Node* fv) {
    return graph->NewNode(common->Phi(rep, 2), tv, fv, merge);
  }

  Node* EffectPhi(Node* tv, Node* fv) {
    return graph->NewNode(common->EffectPhi(2), tv, fv, merge);
  }
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_DIAMOND_H_
