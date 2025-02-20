// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/graph-unittest.h"

#include "src/compiler/node-properties.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"  // TODO(everyone): Make typer.h IWYU compliant.
#include "test/unittests/compiler/node-test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {

GraphTest::GraphTest(int num_parameters)
    : TestWithNativeContextAndZone(kCompressGraphZone),
      data_(std::make_unique<Data>(isolate(), zone(), num_parameters)) {}

void GraphTest::Reset() {
  int num_parameters = data_->num_parameters_;
  data_ = nullptr;
  zone()->Reset();
  data_ = std::make_unique<Data>(isolate(), zone(), num_parameters);
}

GraphTest::Data::Data(Isolate* isolate, Zone* zone, int num_parameters)
    : common_(zone),
      graph_(zone),
      broker_(isolate, zone),
      broker_scope_(&broker_, isolate, zone),
      current_broker_(&broker_),
      source_positions_(&graph_),
      node_origins_(&graph_),
      num_parameters_(num_parameters) {
  if (!PersistentHandlesScope::IsActive(isolate)) {
    persistent_scope_.emplace(isolate);
  }
  graph_.SetStart(graph_.NewNode(common_.Start(num_parameters)));
  graph_.SetEnd(graph_.NewNode(common_.End(1), graph_.start()));
  broker_.SetTargetNativeContextRef(isolate->native_context());
}

GraphTest::Data::~Data() {
  if (persistent_scope_) {
    persistent_scope_->Detach();
  }
}

Node* GraphTest::Parameter(int32_t index) {
  return graph()->NewNode(common()->Parameter(index), graph()->start());
}

Node* GraphTest::Parameter(Type type, int32_t index) {
  Node* node = GraphTest::Parameter(index);
  NodeProperties::SetType(node, type);
  return node;
}

Node* GraphTest::Float32Constant(float value) {
  return graph()->NewNode(common()->Float32Constant(value));
}


Node* GraphTest::Float64Constant(double value) {
  return graph()->NewNode(common()->Float64Constant(value));
}


Node* GraphTest::Int32Constant(int32_t value) {
  return graph()->NewNode(common()->Int32Constant(value));
}


Node* GraphTest::Int64Constant(int64_t value) {
  return graph()->NewNode(common()->Int64Constant(value));
}


Node* GraphTest::NumberConstant(double value) {
  return graph()->NewNode(common()->NumberConstant(value));
}

Node* GraphTest::HeapConstantNoHole(const Handle<HeapObject>& value) {
  CHECK(!IsAnyHole(*value));
  Node* node = graph()->NewNode(common()->HeapConstant(value));
  Type type = Type::Constant(broker(), value, zone());
  NodeProperties::SetType(node, type);
  return node;
}

Node* GraphTest::HeapConstantHole(const Handle<HeapObject>& value) {
  CHECK(IsAnyHole(*value));
  Node* node = graph()->NewNode(common()->HeapConstant(value));
  Type type = Type::Constant(broker(), value, zone());
  NodeProperties::SetType(node, type);
  return node;
}

Node* GraphTest::FalseConstant() {
  return HeapConstantNoHole(factory()->false_value());
}


Node* GraphTest::TrueConstant() {
  return HeapConstantNoHole(factory()->true_value());
}


Node* GraphTest::UndefinedConstant() {
  return HeapConstantNoHole(factory()->undefined_value());
}


Node* GraphTest::EmptyFrameState() {
  Node* state_values =
      graph()->NewNode(common()->StateValues(0, SparseInputMask::Dense()));
  FrameStateFunctionInfo const* function_info =
      common()->CreateFrameStateFunctionInfo(
          FrameStateType::kUnoptimizedFunction, 0, 0, 0, {}, {});
  return graph()->NewNode(
      common()->FrameState(BytecodeOffset::None(),
                           OutputFrameStateCombine::Ignore(), function_info),
      state_values, state_values, state_values, NumberConstant(0),
      UndefinedConstant(), graph()->start());
}


Matcher<Node*> GraphTest::IsFalseConstant() {
  return IsHeapConstant(factory()->false_value());
}


Matcher<Node*> GraphTest::IsTrueConstant() {
  return IsHeapConstant(factory()->true_value());
}

Matcher<Node*> GraphTest::IsNullConstant() {
  return IsHeapConstant(factory()->null_value());
}

Matcher<Node*> GraphTest::IsUndefinedConstant() {
  return IsHeapConstant(factory()->undefined_value());
}

TypedGraphTest::TypedGraphTest(int num_parameters)
    : GraphTest(num_parameters),
      typer_(broker(), Typer::kNoFlags, graph(), tick_counter()) {}

TypedGraphTest::~TypedGraphTest() = default;

namespace graph_unittest {

const Operator kDummyOperator(0, Operator::kNoProperties, "Dummy", 0, 0, 0, 1,
                              0, 0);


TEST_F(GraphTest, NewNode) {
  Node* n0 = graph()->NewNode(&kDummyOperator);
  Node* n1 = graph()->NewNode(&kDummyOperator);
  EXPECT_NE(n0, n1);
  EXPECT_LT(0u, n0->id());
  EXPECT_LT(0u, n1->id());
  EXPECT_NE(n0->id(), n1->id());
  EXPECT_EQ(&kDummyOperator, n0->op());
  EXPECT_EQ(&kDummyOperator, n1->op());
}

}  // namespace graph_unittest
}  // namespace compiler
}  // namespace internal
}  // namespace v8
