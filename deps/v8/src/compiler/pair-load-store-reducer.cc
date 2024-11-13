// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/pair-load-store-reducer.h"

#include <optional>

#include "src/compiler/machine-graph.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

std::optional<std::tuple<int, const Operator*>> CanBePaired(
    Node* node1, Node* node2, MachineOperatorBuilder* machine,
    Isolate* isolate) {
  DCHECK(node1->opcode() == IrOpcode::kStore &&
         node1->opcode() == IrOpcode::kStore);

  Node* base1 = node1->InputAt(0);
  Node* base2 = node2->InputAt(0);
  if (base1 != base2) return {};

  auto rep1 = StoreRepresentationOf(node1->op());
  auto rep2 = StoreRepresentationOf(node2->op());
  auto combo = machine->TryStorePair(rep1, rep2);
  if (!combo) return {};

  Node* index1 = node1->InputAt(1);
  Node* index2 = node2->InputAt(1);

  int idx1, idx2;
  if (index1->opcode() == IrOpcode::kInt64Constant) {
    idx1 = static_cast<int>(OpParameter<int64_t>(index1->op()));
  } else {
    return {};
  }
  if (index2->opcode() == IrOpcode::kInt64Constant) {
    idx2 = static_cast<int>(OpParameter<int64_t>(index2->op()));
  } else {
    return {};
  }

  int bytesize = 1 << ElementSizeLog2Of(rep1.representation());
  int diff = idx2 - idx1;
  if (diff != bytesize && diff != -bytesize) {
    return {};
  }

  return {{diff, *combo}};
}

}  // namespace

PairLoadStoreReducer::PairLoadStoreReducer(Editor* editor,
                                           MachineGraph* mcgraph,
                                           Isolate* isolate)
    : AdvancedReducer(editor), mcgraph_(mcgraph), isolate_(isolate) {}

Reduction PairLoadStoreReducer::Reduce(Node* cur) {
  if (cur->opcode() != IrOpcode::kStore) {
    return Reduction();
  }

  Node* prev = NodeProperties::GetEffectInput(cur);
  if (prev->opcode() != IrOpcode::kStore) {
    return Reduction();
  }

  if (!prev->OwnedBy(cur)) {
    return Reduction();
  }

  auto pairing = CanBePaired(prev, cur, mcgraph_->machine(), isolate_);
  if (!pairing) return Reduction();

  if (std::get<int>(*pairing) > 0) {
    prev->InsertInput(mcgraph_->zone(), 3, cur->InputAt(2));
  } else {
    NodeProperties::ReplaceValueInput(prev, cur->InputAt(1), 1);
    prev->InsertInput(mcgraph_->zone(), 2, cur->InputAt(2));
  }
  NodeProperties::ChangeOp(prev, std::get<const Operator*>(*pairing));
  Replace(cur, prev);
  cur->Kill();
  return Reduction(prev);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
