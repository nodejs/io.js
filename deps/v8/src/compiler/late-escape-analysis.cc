// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/late-escape-analysis.h"

#include <optional>

#include "src/compiler/js-graph.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

LateEscapeAnalysis::LateEscapeAnalysis(Editor* editor, Graph* graph,
                                       CommonOperatorBuilder* common,
                                       Zone* zone)
    : AdvancedReducer(editor),
      dead_(graph->NewNode(common->Dead())),
      all_allocations_(zone),
      escaping_allocations_(zone),
      revisit_(zone) {}

namespace {

bool IsStore(Edge edge) {
  DCHECK_EQ(edge.to()->opcode(), IrOpcode::kAllocateRaw);
  DCHECK(NodeProperties::IsValueEdge(edge));

  switch (edge.from()->opcode()) {
    case IrOpcode::kInitializeImmutableInObject:
    case IrOpcode::kStore:
    case IrOpcode::kStoreElement:
    case IrOpcode::kStoreField:
    case IrOpcode::kStoreToObject:
      return edge.index() == 0;
    default:
      return false;
  }
}

bool IsEscapingAllocationWitness(Edge edge) {
  if (edge.to()->opcode() != IrOpcode::kAllocateRaw) return false;
  if (!NodeProperties::IsValueEdge(edge)) return false;
  return !IsStore(edge);
}

}  // namespace

Reduction LateEscapeAnalysis::Reduce(Node* node) {
  if (node->opcode() == IrOpcode::kAllocateRaw) {
    all_allocations_.insert(node);
    return NoChange();
  }

  for (Edge edge : node->input_edges()) {
    if (IsEscapingAllocationWitness(edge)) {
      RecordEscapingAllocation(edge.to());
    }
  }

  return NoChange();
}

void LateEscapeAnalysis::Finalize() {
  for (Node* alloc : all_allocations_) {
    if (!IsEscaping(alloc)) {
      RemoveAllocation(alloc);
    }
  }
  while (!revisit_.empty()) {
    Node* alloc = revisit_.front();
    revisit_.pop_front();
    if (!IsEscaping(alloc) && !alloc->IsDead()) {
      RemoveAllocation(alloc);
    }
  }
}

namespace {

std::optional<Node*> TryGetStoredValue(Node* node) {
  int value_index;
  switch (node->opcode()) {
    case IrOpcode::kInitializeImmutableInObject:
    case IrOpcode::kStore:
    case IrOpcode::kStoreElement:
    case IrOpcode::kStoreToObject:
      value_index = 2;
      break;
    case IrOpcode::kStoreField:
      value_index = 1;
      break;
    default:
      return {};
  }

  return NodeProperties::GetValueInput(node, value_index);
}

}  // namespace

bool LateEscapeAnalysis::IsEscaping(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAllocateRaw);
  auto escaping = escaping_allocations_.find(node);
  if (escaping == escaping_allocations_.end()) return false;
  return escaping->second != 0;
}

void LateEscapeAnalysis::RemoveAllocation(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAllocateRaw);
  for (Edge edge : node->use_edges()) {
    if (!NodeProperties::IsValueEdge(edge)) continue;
    Node* use = edge.from();
    if (use->IsDead()) continue;
    // The value stored by this Store node might be another allocation which has
    // no more uses. Affected allocations are revisited.
    if (std::optional<Node*> stored_value = TryGetStoredValue(use);
        stored_value.has_value() &&
        stored_value.value()->opcode() == IrOpcode::kAllocateRaw &&
        stored_value.value() != node) {
      RemoveWitness(stored_value.value());
      revisit_.push_back(stored_value.value());
    }
    ReplaceWithValue(use, dead());
    use->Kill();
  }

  // Remove the allocation from the effect and control chains.
  ReplaceWithValue(node, dead());
  node->Kill();
}

void LateEscapeAnalysis::RecordEscapingAllocation(Node* allocation) {
  DCHECK_EQ(allocation->opcode(), IrOpcode::kAllocateRaw);
  escaping_allocations_[allocation]++;
}

void LateEscapeAnalysis::RemoveWitness(Node* allocation) {
  DCHECK_EQ(allocation->opcode(), IrOpcode::kAllocateRaw);
  DCHECK_GT(escaping_allocations_[allocation], 0);
  escaping_allocations_[allocation]--;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
