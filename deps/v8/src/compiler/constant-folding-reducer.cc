// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/constant-folding-reducer.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {
Node* TryGetConstant(JSGraph* jsgraph, Node* node, JSHeapBroker* broker) {
  Type type = NodeProperties::GetType(node);
  Node* result;
  if (type.IsNone()) {
    result = nullptr;
  } else if (type.Is(Type::Null())) {
    result = jsgraph->NullConstant();
  } else if (type.Is(Type::Undefined())) {
    result = jsgraph->UndefinedConstant();
  } else if (type.Is(Type::MinusZero())) {
    result = jsgraph->MinusZeroConstant();
  } else if (type.Is(Type::NaN())) {
    result = jsgraph->NaNConstant();
  } else if (type.IsHeapConstant()) {
    result = jsgraph->ConstantNoHole(type.AsHeapConstant()->Ref(), broker);
  } else if (type.Is(Type::PlainNumber()) && type.Min() == type.Max()) {
    result = jsgraph->ConstantNoHole(type.Min());
  } else {
    result = nullptr;
  }
  DCHECK_EQ(result != nullptr, type.IsSingleton());
  DCHECK_IMPLIES(result != nullptr,
                 type.Equals(NodeProperties::GetType(result)));
  return result;
}

}  // namespace

ConstantFoldingReducer::ConstantFoldingReducer(Editor* editor, JSGraph* jsgraph,
                                               JSHeapBroker* broker)
    : AdvancedReducer(editor), jsgraph_(jsgraph), broker_(broker) {}

ConstantFoldingReducer::~ConstantFoldingReducer() = default;

Reduction ConstantFoldingReducer::Reduce(Node* node) {
  if (!NodeProperties::IsConstant(node) && NodeProperties::IsTyped(node) &&
      node->op()->HasProperty(Operator::kEliminatable) &&
      node->opcode() != IrOpcode::kFinishRegion &&
      node->opcode() != IrOpcode::kTypeGuard) {
    Node* constant = TryGetConstant(jsgraph(), node, broker());
    if (constant != nullptr) {
      DCHECK(NodeProperties::IsTyped(constant));
      DCHECK_EQ(node->op()->ControlOutputCount(), 0);
      ReplaceWithValue(node, constant);
      return Replace(constant);
    }
  }
  return NoChange();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
