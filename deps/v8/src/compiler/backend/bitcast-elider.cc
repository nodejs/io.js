// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/backend/bitcast-elider.h"

#include "src/compiler/graph.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool IsBitcast(Node* node) {
  // We can only elide kBitcastTaggedToWordForTagAndSmiBits and
  // kBitcastWordToTaggedSigned because others might affect GC / safepoint
  // tables.
  return node->opcode() == IrOpcode::kBitcastTaggedToWordForTagAndSmiBits ||
         node->opcode() == IrOpcode::kBitcastWordToTaggedSigned;
}

bool OwnedByWord32Op(Node* node) {
#if V8_TARGET_ARCH_LOONG64 || V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_RISCV64
  return false;
#else
  for (Node* const use : node->uses()) {
    switch (use->opcode()) {
      case IrOpcode::kWord32Equal:
      case IrOpcode::kInt32LessThan:
      case IrOpcode::kInt32LessThanOrEqual:
      case IrOpcode::kUint32LessThan:
      case IrOpcode::kUint32LessThanOrEqual:
      case IrOpcode::kChangeInt32ToInt64:
#define Word32Op(Name) case IrOpcode::k##Name:
        MACHINE_BINOP_32_LIST(Word32Op)
#undef Word32Op
        break;
      default:
        return false;
    }
  }
  return true;
#endif
}

void Replace(Node* node, Node* replacement) {
  for (Edge edge : node->use_edges()) {
    edge.UpdateTo(replacement);
  }
  node->Kill();
}

}  // namespace

void BitcastElider::Enqueue(Node* node) {
  if (seen_.Get(node)) return;
  seen_.Set(node, true);
  to_visit_.push(node);
}

void BitcastElider::Revisit(Node* node) { to_visit_.push(node); }

void BitcastElider::VisitNode(Node* node) {
  for (int i = 0; i < node->InputCount(); i++) {
    Node* input = node->InputAt(i);
    // This can happen as a result of previous replacements.
    if (input == nullptr) continue;
    if (input->opcode() == IrOpcode::kTruncateInt64ToInt32 &&
        OwnedByWord32Op(input)) {
      Replace(input, input->InputAt(0));
      Revisit(node);
    } else if (is_builtin_ && IsBitcast(input)) {
      Replace(input, input->InputAt(0));
      Revisit(node);
    } else {
      Enqueue(input);
    }
  }
}

void BitcastElider::ProcessGraph() {
  Enqueue(graph_->end());
  while (!to_visit_.empty()) {
    Node* node = to_visit_.front();
    to_visit_.pop();
    VisitNode(node);
  }
}

BitcastElider::BitcastElider(Zone* zone, Graph* graph, bool is_builtin)
    : graph_(graph),
      to_visit_(zone),
      seen_(graph, 2),
      is_builtin_(is_builtin) {}

void BitcastElider::Reduce() { ProcessGraph(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
