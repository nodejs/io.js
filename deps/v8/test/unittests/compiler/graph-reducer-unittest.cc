// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/graph-reducer-unittest.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-graph.h"
#include "test/unittests/test-utils.h"

using testing::_;
using testing::DefaultValue;
using testing::ElementsAre;
using testing::Return;
using testing::Sequence;
using testing::StrictMock;
using testing::UnorderedElementsAre;

namespace v8 {
namespace internal {
namespace compiler {
namespace graph_reducer_unittest {

namespace {

struct TestOperator : public Operator {
  TestOperator(Operator::Opcode opcode, Operator::Properties properties,
               const char* op_name, size_t value_in, size_t value_out)
      : Operator(opcode, properties, op_name, value_in, 0, 0, value_out, 0, 0) {
  }
};


const uint8_t kOpcodeA0 = 10;
const uint8_t kOpcodeA1 = 11;
const uint8_t kOpcodeA2 = 12;
const uint8_t kOpcodeB0 = 20;
const uint8_t kOpcodeB1 = 21;
const uint8_t kOpcodeB2 = 22;
const uint8_t kOpcodeC0 = 30;
const uint8_t kOpcodeC1 = 31;
const uint8_t kOpcodeC2 = 32;

static TestOperator kOpA0(kOpcodeA0, Operator::kNoWrite, "opa1", 0, 1);
static TestOperator kOpA1(kOpcodeA1, Operator::kNoProperties, "opa2", 1, 1);
static TestOperator kOpA2(kOpcodeA2, Operator::kNoProperties, "opa3", 2, 1);
static TestOperator kOpB0(kOpcodeB0, Operator::kNoWrite, "opb0", 0, 1);
static TestOperator kOpB1(kOpcodeB1, Operator::kNoWrite, "opb1", 1, 1);
static TestOperator kOpB2(kOpcodeB2, Operator::kNoWrite, "opb2", 2, 1);
static TestOperator kOpC0(kOpcodeC0, Operator::kNoWrite, "opc0", 0, 1);
static TestOperator kOpC1(kOpcodeC1, Operator::kNoWrite, "opc1", 1, 1);
static TestOperator kOpC2(kOpcodeC2, Operator::kNoWrite, "opc2", 2, 1);

struct MockReducer : public Reducer {
  MOCK_METHOD(const char*, reducer_name, (), (const, override));
  MOCK_METHOD(Reduction, Reduce, (Node*), (override));
};


// Replaces all "A" operators with "B" operators without creating new nodes.
class InPlaceABReducer final : public Reducer {
 public:
  const char* reducer_name() const override { return "InPlaceABReducer"; }
  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeA0:
        EXPECT_EQ(0, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpB0);
        return Replace(node);
      case kOpcodeA1:
        EXPECT_EQ(1, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpB1);
        return Replace(node);
      case kOpcodeA2:
        EXPECT_EQ(2, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpB2);
        return Replace(node);
    }
    return NoChange();
  }
};


// Replaces all "A" operators with "B" operators by allocating new nodes.
class NewABReducer final : public Reducer {
 public:
  explicit NewABReducer(Graph* graph) : graph_(graph) {}

  const char* reducer_name() const override { return "NewABReducer"; }

  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeA0:
        EXPECT_EQ(0, node->InputCount());
        return Replace(graph_->NewNode(&kOpB0));
      case kOpcodeA1:
        EXPECT_EQ(1, node->InputCount());
        return Replace(graph_->NewNode(&kOpB1, node->InputAt(0)));
      case kOpcodeA2:
        EXPECT_EQ(2, node->InputCount());
        return Replace(
            graph_->NewNode(&kOpB2, node->InputAt(0), node->InputAt(1)));
    }
    return NoChange();
  }

 private:
  Graph* const graph_;
};


// Wraps all "kOpA0" nodes in "kOpB1" operators by allocating new nodes.
class A0Wrapper final : public Reducer {
 public:
  explicit A0Wrapper(Graph* graph) : graph_(graph) {}

  const char* reducer_name() const override { return "A0Wrapper"; }

  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeA0:
        EXPECT_EQ(0, node->InputCount());
        return Replace(graph_->NewNode(&kOpB1, node));
    }
    return NoChange();
  }

 private:
  Graph* const graph_;
};


// Wraps all "kOpB0" nodes in two "kOpC1" operators by allocating new nodes.
class B0Wrapper final : public Reducer {
 public:
  explicit B0Wrapper(Graph* graph) : graph_(graph) {}

  const char* reducer_name() const override { return "B0Wrapper"; }

  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeB0:
        EXPECT_EQ(0, node->InputCount());
        return Replace(graph_->NewNode(&kOpC1, graph_->NewNode(&kOpC1, node)));
    }
    return NoChange();
  }

 private:
  Graph* const graph_;
};


// Replaces all "kOpA1" nodes with the first input.
class A1Forwarder final : public Reducer {
 public:
  const char* reducer_name() const override { return "A1Forwarder"; }
  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeA1:
        EXPECT_EQ(1, node->InputCount());
        return Replace(node->InputAt(0));
    }
    return NoChange();
  }
};


// Replaces all "kOpB1" nodes with the first input.
class B1Forwarder final : public Reducer {
 public:
  const char* reducer_name() const override { return "B1Forwarder"; }
  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeB1:
        EXPECT_EQ(1, node->InputCount());
        return Replace(node->InputAt(0));
    }
    return NoChange();
  }
};


// Replaces all "B" operators with "C" operators without creating new nodes.
class InPlaceBCReducer final : public Reducer {
 public:
  const char* reducer_name() const override { return "InPlaceBCReducer"; }
  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeB0:
        EXPECT_EQ(0, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpC0);
        return Replace(node);
      case kOpcodeB1:
        EXPECT_EQ(1, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpC1);
        return Replace(node);
      case kOpcodeB2:
        EXPECT_EQ(2, node->InputCount());
        NodeProperties::ChangeOp(node, &kOpC2);
        return Replace(node);
    }
    return NoChange();
  }
};


// Swaps the inputs to "kOp2A" and "kOp2B" nodes based on ids.
class AB2Sorter final : public Reducer {
 public:
  const char* reducer_name() const override { return "AB2Sorter"; }
  Reduction Reduce(Node* node) final {
    switch (node->op()->opcode()) {
      case kOpcodeA2:
      case kOpcodeB2:
        EXPECT_EQ(2, node->InputCount());
        Node* x = node->InputAt(0);
        Node* y = node->InputAt(1);
        if (x->id() > y->id()) {
          node->ReplaceInput(0, y);
          node->ReplaceInput(1, x);
          return Replace(node);
        }
    }
    return NoChange();
  }
};

}  // namespace


class AdvancedReducerTest : public TestWithZone {
 public:
  AdvancedReducerTest() : TestWithZone(kCompressGraphZone), graph_(zone()) {}

 protected:
  Graph* graph() { return &graph_; }
  TickCounter* tick_counter() { return &tick_counter_; }

 private:
  Graph graph_;
  TickCounter tick_counter_;
};


TEST_F(AdvancedReducerTest, Replace) {
  struct DummyReducer final : public AdvancedReducer {
    explicit DummyReducer(Editor* editor) : AdvancedReducer(editor) {}
    const char* reducer_name() const override { return "DummyReducer"; }
    Reduction Reduce(Node* node) final {
      Replace(node, node);
      return NoChange();
    }
  };
  StrictMock<MockAdvancedReducerEditor> e;
  DummyReducer r(&e);
  Node* node0 = graph()->NewNode(&kOpA0);
  Node* node1 = graph()->NewNode(&kOpA1, node0);
  EXPECT_CALL(e, Replace(node0, node0));
  EXPECT_CALL(e, Replace(node1, node1));
  EXPECT_FALSE(r.Reduce(node0).Changed());
  EXPECT_FALSE(r.Reduce(node1).Changed());
}


TEST_F(AdvancedReducerTest, Revisit) {
  struct DummyReducer final : public AdvancedReducer {
    explicit DummyReducer(Editor* editor) : AdvancedReducer(editor) {}
    const char* reducer_name() const override { return "DummyReducer"; }
    Reduction Reduce(Node* node) final {
      Revisit(node);
      return NoChange();
    }
  };
  StrictMock<MockAdvancedReducerEditor> e;
  DummyReducer r(&e);
  Node* node0 = graph()->NewNode(&kOpA0);
  Node* node1 = graph()->NewNode(&kOpA1, node0);
  EXPECT_CALL(e, Revisit(node0));
  EXPECT_CALL(e, Revisit(node1));
  EXPECT_FALSE(r.Reduce(node0).Changed());
  EXPECT_FALSE(r.Reduce(node1).Changed());
}


namespace {

struct ReplaceWithValueReducer final : public AdvancedReducer {
  explicit ReplaceWithValueReducer(Editor* editor) : AdvancedReducer(editor) {}
  const char* reducer_name() const override {
    return "ReplaceWithValueReducer";
  }
  Reduction Reduce(Node* node) final { return NoChange(); }
  using AdvancedReducer::ReplaceWithValue;
};

const Operator kMockOperator(IrOpcode::kDead, Operator::kNoProperties,
                             "MockOperator", 0, 0, 0, 1, 0, 0);
const Operator kMockOpEffect(IrOpcode::kDead, Operator::kNoProperties,
                             "MockOpEffect", 0, 1, 0, 1, 1, 0);
const Operator kMockOpControl(IrOpcode::kDead, Operator::kNoProperties,
                              "MockOpControl", 0, 0, 1, 1, 0, 1);

}  // namespace


TEST_F(AdvancedReducerTest, ReplaceWithValue_ValueUse) {
  CommonOperatorBuilder common(zone());
  Node* node = graph()->NewNode(&kMockOperator);
  Node* start = graph()->NewNode(common.Start(1));
  Node* zero = graph()->NewNode(common.Int32Constant(0));
  Node* use_value = graph()->NewNode(common.Return(), zero, node, start, start);
  Node* replacement = graph()->NewNode(&kMockOperator);
  GraphReducer graph_reducer(zone(), graph(), nullptr, nullptr);
  ReplaceWithValueReducer r(&graph_reducer);
  r.ReplaceWithValue(node, replacement);
  EXPECT_EQ(replacement, use_value->InputAt(1));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(1, replacement->UseCount());
  EXPECT_THAT(replacement->uses(), ElementsAre(use_value));
}


TEST_F(AdvancedReducerTest, ReplaceWithValue_EffectUse) {
  CommonOperatorBuilder common(zone());
  Node* start = graph()->NewNode(common.Start(1));
  Node* node = graph()->NewNode(&kMockOpEffect, start);
  Node* use_control = graph()->NewNode(common.Merge(1), start);
  Node* use_effect = graph()->NewNode(common.EffectPhi(1), node, use_control);
  Node* replacement = graph()->NewNode(&kMockOperator);
  GraphReducer graph_reducer(zone(), graph(), nullptr, nullptr);
  ReplaceWithValueReducer r(&graph_reducer);
  r.ReplaceWithValue(node, replacement);
  EXPECT_EQ(start, use_effect->InputAt(0));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(3, start->UseCount());
  EXPECT_EQ(0, replacement->UseCount());
  EXPECT_THAT(start->uses(),
              UnorderedElementsAre(use_effect, use_control, node));
}


TEST_F(AdvancedReducerTest, ReplaceWithValue_ControlUse1) {
  CommonOperatorBuilder common(zone());
  Node* start = graph()->NewNode(common.Start(1));
  Node* node = graph()->NewNode(&kMockOpControl, start);
  Node* success = graph()->NewNode(common.IfSuccess(), node);
  Node* use_control = graph()->NewNode(common.Merge(1), success);
  Node* replacement = graph()->NewNode(&kMockOperator);
  GraphReducer graph_reducer(zone(), graph(), nullptr, nullptr);
  ReplaceWithValueReducer r(&graph_reducer);
  r.ReplaceWithValue(node, replacement);
  EXPECT_EQ(start, use_control->InputAt(0));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(2, start->UseCount());
  EXPECT_EQ(0, replacement->UseCount());
  EXPECT_THAT(start->uses(), UnorderedElementsAre(use_control, node));
}


TEST_F(AdvancedReducerTest, ReplaceWithValue_ControlUse2) {
  CommonOperatorBuilder common(zone());
  Node* start = graph()->NewNode(common.Start(1));
  Node* effect = graph()->NewNode(&kMockOperator);
  Node* dead = graph()->NewNode(&kMockOperator);
  Node* node = graph()->NewNode(&kMockOpControl, start);
  Node* success = graph()->NewNode(common.IfSuccess(), node);
  Node* exception = graph()->NewNode(common.IfException(), effect, node);
  Node* use_control = graph()->NewNode(common.Merge(1), success);
  Node* replacement = graph()->NewNode(&kMockOperator);
  GraphReducer graph_reducer(zone(), graph(), tick_counter(), nullptr, dead);
  ReplaceWithValueReducer r(&graph_reducer);
  r.ReplaceWithValue(node, replacement);
  EXPECT_EQ(start, use_control->InputAt(0));
  EXPECT_EQ(dead, exception->InputAt(1));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(2, start->UseCount());
  EXPECT_EQ(1, dead->UseCount());
  EXPECT_EQ(0, replacement->UseCount());
  EXPECT_THAT(start->uses(), UnorderedElementsAre(use_control, node));
  EXPECT_THAT(dead->uses(), ElementsAre(exception));
}


TEST_F(AdvancedReducerTest, ReplaceWithValue_ControlUse3) {
  CommonOperatorBuilder common(zone());
  Node* start = graph()->NewNode(common.Start(1));
  Node* effect = graph()->NewNode(&kMockOperator);
  Node* dead = graph()->NewNode(&kMockOperator);
  Node* node = graph()->NewNode(&kMockOpControl, start);
  Node* success = graph()->NewNode(common.IfSuccess(), node);
  Node* exception = graph()->NewNode(common.IfException(), effect, node);
  Node* use_control = graph()->NewNode(common.Merge(1), success);
  Node* replacement = graph()->NewNode(&kMockOperator);
  GraphReducer graph_reducer(zone(), graph(), tick_counter(), nullptr, dead);
  ReplaceWithValueReducer r(&graph_reducer);
  r.ReplaceWithValue(node, replacement);
  EXPECT_EQ(start, use_control->InputAt(0));
  EXPECT_EQ(dead, exception->InputAt(1));
  EXPECT_EQ(0, node->UseCount());
  EXPECT_EQ(2, start->UseCount());
  EXPECT_EQ(1, dead->UseCount());
  EXPECT_EQ(0, replacement->UseCount());
  EXPECT_THAT(start->uses(), UnorderedElementsAre(use_control, node));
  EXPECT_THAT(dead->uses(), ElementsAre(exception));
}


class GraphReducerTest : public TestWithZone {
 public:
  GraphReducerTest() : TestWithZone(kCompressGraphZone), graph_(zone()) {}

  static void SetUpTestSuite() {
    TestWithZone::SetUpTestSuite();
    DefaultValue<Reduction>::Set(Reducer::NoChange());
  }

  static void TearDownTestSuite() {
    DefaultValue<Reduction>::Clear();
    TestWithZone::TearDownTestSuite();
  }

 protected:
  void ReduceNode(Node* node, Reducer* r) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r);
    reducer.ReduceNode(node);
  }

  void ReduceNode(Node* node, Reducer* r1, Reducer* r2) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.ReduceNode(node);
  }

  void ReduceNode(Node* node, Reducer* r1, Reducer* r2, Reducer* r3) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.AddReducer(r3);
    reducer.ReduceNode(node);
  }

  void ReduceGraph(Reducer* r1) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r1);
    reducer.ReduceGraph();
  }

  void ReduceGraph(Reducer* r1, Reducer* r2) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.ReduceGraph();
  }

  void ReduceGraph(Reducer* r1, Reducer* r2, Reducer* r3) {
    GraphReducer reducer(zone(), graph(), tick_counter(), nullptr);
    reducer.AddReducer(r1);
    reducer.AddReducer(r2);
    reducer.AddReducer(r3);
    reducer.ReduceGraph();
  }

  Graph* graph() { return &graph_; }
  TickCounter* tick_counter() { return &tick_counter_; }

 private:
  Graph graph_;
  TickCounter tick_counter_;
};


TEST_F(GraphReducerTest, NodeIsDeadAfterReplace) {
  StrictMock<MockReducer> r;
  Node* node0 = graph()->NewNode(&kOpA0);
  Node* node1 = graph()->NewNode(&kOpA1, node0);
  Node* node2 = graph()->NewNode(&kOpA1, node0);
  EXPECT_CALL(r, Reduce(node0)).WillOnce(Return(Reducer::NoChange()));
  EXPECT_CALL(r, Reduce(node1)).WillOnce(Return(Reducer::Replace(node2)));
  ReduceNode(node1, &r);
  EXPECT_FALSE(node0->IsDead());
  EXPECT_TRUE(node1->IsDead());
  EXPECT_FALSE(node2->IsDead());
}


TEST_F(GraphReducerTest, ReduceOnceForEveryReducer) {
  StrictMock<MockReducer> r1, r2;
  Node* node0 = graph()->NewNode(&kOpA0);
  EXPECT_CALL(r1, Reduce(node0));
  EXPECT_CALL(r2, Reduce(node0));
  ReduceNode(node0, &r1, &r2);
}


TEST_F(GraphReducerTest, ReduceAgainAfterChanged) {
  Sequence s1, s2, s3;
  StrictMock<MockReducer> r1, r2, r3;
  Node* node0 = graph()->NewNode(&kOpA0);
  EXPECT_CALL(r1, Reduce(node0));
  EXPECT_CALL(r2, Reduce(node0));
  EXPECT_CALL(r3, Reduce(node0)).InSequence(s1, s2, s3).WillOnce(
      Return(Reducer::Changed(node0)));
  EXPECT_CALL(r1, Reduce(node0)).InSequence(s1);
  EXPECT_CALL(r2, Reduce(node0)).InSequence(s2);
  ReduceNode(node0, &r1, &r2, &r3);
}


TEST_F(GraphReducerTest, ReduceGraphFromEnd1) {
  StrictMock<MockReducer> r1;
  Node* n = graph()->NewNode(&kOpA0);
  Node* end = graph()->NewNode(&kOpA1, n);
  graph()->SetEnd(end);
  Sequence s;
  EXPECT_CALL(r1, Reduce(n));
  EXPECT_CALL(r1, Reduce(end));
  ReduceGraph(&r1);
}


TEST_F(GraphReducerTest, ReduceGraphFromEnd2) {
  StrictMock<MockReducer> r1;
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* n2 = graph()->NewNode(&kOpA1, n1);
  Node* n3 = graph()->NewNode(&kOpA1, n1);
  Node* end = graph()->NewNode(&kOpA2, n2, n3);
  graph()->SetEnd(end);
  Sequence s1, s2;
  EXPECT_CALL(r1, Reduce(n1)).InSequence(s1, s2);
  EXPECT_CALL(r1, Reduce(n2)).InSequence(s1);
  EXPECT_CALL(r1, Reduce(n3)).InSequence(s2);
  EXPECT_CALL(r1, Reduce(end)).InSequence(s1, s2);
  ReduceGraph(&r1);
}


TEST_F(GraphReducerTest, ReduceInPlace1) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* end = graph()->NewNode(&kOpA1, n1);
  graph()->SetEnd(end);

  // Tests A* => B* with in-place updates.
  InPlaceABReducer r;
  for (int i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpB0, n1->op());
    EXPECT_EQ(&kOpB1, end->op());
    EXPECT_EQ(n1, end->InputAt(0));
  }
}


TEST_F(GraphReducerTest, ReduceInPlace2) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* n2 = graph()->NewNode(&kOpA1, n1);
  Node* n3 = graph()->NewNode(&kOpA1, n1);
  Node* end = graph()->NewNode(&kOpA2, n2, n3);
  graph()->SetEnd(end);

  // Tests A* => B* with in-place updates.
  InPlaceABReducer r;
  for (int i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpB0, n1->op());
    EXPECT_EQ(&kOpB1, n2->op());
    EXPECT_EQ(n1, n2->InputAt(0));
    EXPECT_EQ(&kOpB1, n3->op());
    EXPECT_EQ(n1, n3->InputAt(0));
    EXPECT_EQ(&kOpB2, end->op());
    EXPECT_EQ(n2, end->InputAt(0));
    EXPECT_EQ(n3, end->InputAt(1));
  }
}


TEST_F(GraphReducerTest, ReduceNew1) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* n2 = graph()->NewNode(&kOpA1, n1);
  Node* n3 = graph()->NewNode(&kOpA1, n1);
  Node* end = graph()->NewNode(&kOpA2, n2, n3);
  graph()->SetEnd(end);

  NewABReducer r(graph());
  // Tests A* => B* while creating new nodes.
  for (int i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    if (i == 0) {
      EXPECT_NE(before, graph()->NodeCount());
    } else {
      EXPECT_EQ(before, graph()->NodeCount());
    }
    Node* nend = graph()->end();
    EXPECT_NE(end, nend);  // end() should be updated too.

    Node* nn2 = nend->InputAt(0);
    Node* nn3 = nend->InputAt(1);
    Node* nn1 = nn2->InputAt(0);

    EXPECT_EQ(nn1, nn3->InputAt(0));

    EXPECT_EQ(&kOpB0, nn1->op());
    EXPECT_EQ(&kOpB1, nn2->op());
    EXPECT_EQ(&kOpB1, nn3->op());
    EXPECT_EQ(&kOpB2, nend->op());
  }
}


TEST_F(GraphReducerTest, Wrapping1) {
  Node* end = graph()->NewNode(&kOpA0);
  graph()->SetEnd(end);
  EXPECT_EQ(1U, graph()->NodeCount());

  A0Wrapper r(graph());

  ReduceGraph(&r);
  EXPECT_EQ(2U, graph()->NodeCount());

  Node* nend = graph()->end();
  EXPECT_NE(end, nend);
  EXPECT_EQ(&kOpB1, nend->op());
  EXPECT_EQ(1, nend->InputCount());
  EXPECT_EQ(end, nend->InputAt(0));
}


TEST_F(GraphReducerTest, Wrapping2) {
  Node* end = graph()->NewNode(&kOpB0);
  graph()->SetEnd(end);
  EXPECT_EQ(1U, graph()->NodeCount());

  B0Wrapper r(graph());

  ReduceGraph(&r);
  EXPECT_EQ(3U, graph()->NodeCount());

  Node* nend = graph()->end();
  EXPECT_NE(end, nend);
  EXPECT_EQ(&kOpC1, nend->op());
  EXPECT_EQ(1, nend->InputCount());

  Node* n1 = nend->InputAt(0);
  EXPECT_NE(end, n1);
  EXPECT_EQ(&kOpC1, n1->op());
  EXPECT_EQ(1, n1->InputCount());
  EXPECT_EQ(end, n1->InputAt(0));
}


TEST_F(GraphReducerTest, Forwarding1) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* end = graph()->NewNode(&kOpA1, n1);
  graph()->SetEnd(end);

  A1Forwarder r;

  // Tests A1(x) => x
  for (int i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpA0, n1->op());
    EXPECT_EQ(n1, graph()->end());
  }
}


TEST_F(GraphReducerTest, Forwarding2) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* n2 = graph()->NewNode(&kOpA1, n1);
  Node* n3 = graph()->NewNode(&kOpA1, n1);
  Node* end = graph()->NewNode(&kOpA2, n2, n3);
  graph()->SetEnd(end);

  A1Forwarder r;

  // Tests reducing A2(A1(x), A1(y)) => A2(x, y).
  for (int i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpA0, n1->op());
    EXPECT_EQ(n1, end->InputAt(0));
    EXPECT_EQ(n1, end->InputAt(1));
    EXPECT_EQ(&kOpA2, end->op());
    EXPECT_EQ(0, n2->UseCount());
    EXPECT_EQ(0, n3->UseCount());
  }
}


TEST_F(GraphReducerTest, Forwarding3) {
  // Tests reducing a chain of A1(A1(A1(A1(x)))) => x.
  for (int i = 0; i < 8; i++) {
    Node* n1 = graph()->NewNode(&kOpA0);
    Node* end = n1;
    for (int j = 0; j < i; j++) {
      end = graph()->NewNode(&kOpA1, end);
    }
    graph()->SetEnd(end);

    A1Forwarder r;

    for (size_t j = 0; j < 3; j++) {
      size_t before = graph()->NodeCount();
      ReduceGraph(&r);
      EXPECT_EQ(before, graph()->NodeCount());
      EXPECT_EQ(&kOpA0, n1->op());
      EXPECT_EQ(n1, graph()->end());
    }
  }
}


TEST_F(GraphReducerTest, ReduceForward1) {
  Node* n1 = graph()->NewNode(&kOpA0);
  Node* n2 = graph()->NewNode(&kOpA1, n1);
  Node* n3 = graph()->NewNode(&kOpA1, n1);
  Node* end = graph()->NewNode(&kOpA2, n2, n3);
  graph()->SetEnd(end);

  InPlaceABReducer r;
  B1Forwarder f;

  // Tests first reducing A => B, then B1(x) => x.
  for (size_t i = 0; i < 3; i++) {
    size_t before = graph()->NodeCount();
    ReduceGraph(&r, &f);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpB0, n1->op());
    EXPECT_TRUE(n2->IsDead());
    EXPECT_EQ(n1, end->InputAt(0));
    EXPECT_TRUE(n3->IsDead());
    EXPECT_EQ(n1, end->InputAt(0));
    EXPECT_EQ(&kOpB2, end->op());
    EXPECT_EQ(0, n2->UseCount());
    EXPECT_EQ(0, n3->UseCount());
  }
}


TEST_F(GraphReducerTest, Sorter1) {
  AB2Sorter r;
  for (int i = 0; i < 6; i++) {
    Node* n1 = graph()->NewNode(&kOpA0);
    Node* n2 = graph()->NewNode(&kOpA1, n1);
    Node* n3 = graph()->NewNode(&kOpA1, n1);
    Node* end = nullptr;  // Initialize to please the compiler.

    if (i == 0) end = graph()->NewNode(&kOpA2, n2, n3);
    if (i == 1) end = graph()->NewNode(&kOpA2, n3, n2);
    if (i == 2) end = graph()->NewNode(&kOpA2, n2, n1);
    if (i == 3) end = graph()->NewNode(&kOpA2, n1, n2);
    if (i == 4) end = graph()->NewNode(&kOpA2, n3, n1);
    if (i == 5) end = graph()->NewNode(&kOpA2, n1, n3);

    graph()->SetEnd(end);

    size_t before = graph()->NodeCount();
    ReduceGraph(&r);
    EXPECT_EQ(before, graph()->NodeCount());
    EXPECT_EQ(&kOpA0, n1->op());
    EXPECT_EQ(&kOpA1, n2->op());
    EXPECT_EQ(&kOpA1, n3->op());
    EXPECT_EQ(&kOpA2, end->op());
    EXPECT_EQ(end, graph()->end());
    EXPECT_LE(end->InputAt(0)->id(), end->InputAt(1)->id());
  }
}


namespace {

// Generate a node graph with the given permutations.
void GenDAG(Graph* graph, int* p3, int* p2, int* p1) {
  Node* level4 = graph->NewNode(&kOpA0);
  Node* level3[] = {graph->NewNode(&kOpA1, level4),
                    graph->NewNode(&kOpA1, level4)};

  Node* level2[] = {graph->NewNode(&kOpA1, level3[p3[0]]),
                    graph->NewNode(&kOpA1, level3[p3[1]]),
                    graph->NewNode(&kOpA1, level3[p3[0]]),
                    graph->NewNode(&kOpA1, level3[p3[1]])};

  Node* level1[] = {graph->NewNode(&kOpA2, level2[p2[0]], level2[p2[1]]),
                    graph->NewNode(&kOpA2, level2[p2[2]], level2[p2[3]])};

  Node* end = graph->NewNode(&kOpA2, level1[p1[0]], level1[p1[1]]);
  graph->SetEnd(end);
}

}  // namespace


TEST_F(GraphReducerTest, SortForwardReduce) {
  // Tests combined reductions on a series of DAGs.
  for (int j = 0; j < 2; j++) {
    int p3[] = {j, 1 - j};
    for (int m = 0; m < 2; m++) {
      int p1[] = {m, 1 - m};
      for (int k = 0; k < 24; k++) {  // All permutations of 0, 1, 2, 3
        int p2[] = {-1, -1, -1, -1};
        int n = k;
        for (int d = 4; d >= 1; d--) {  // Construct permutation.
          int p = n % d;
          for (int z = 0; z < 4; z++) {
            if (p2[z] == -1) {
              if (p == 0) p2[z] = d - 1;
              p--;
            }
          }
          n = n / d;
        }

        GenDAG(graph(), p3, p2, p1);

        AB2Sorter r1;
        A1Forwarder r2;
        InPlaceABReducer r3;

        ReduceGraph(&r1, &r2, &r3);

        Node* end = graph()->end();
        EXPECT_EQ(&kOpB2, end->op());
        Node* n1 = end->InputAt(0);
        Node* n2 = end->InputAt(1);
        EXPECT_NE(n1, n2);
        EXPECT_LT(n1->id(), n2->id());
        EXPECT_EQ(&kOpB2, n1->op());
        EXPECT_EQ(&kOpB2, n2->op());
        Node* n4 = n1->InputAt(0);
        EXPECT_EQ(&kOpB0, n4->op());
        EXPECT_EQ(n4, n1->InputAt(1));
        EXPECT_EQ(n4, n2->InputAt(0));
        EXPECT_EQ(n4, n2->InputAt(1));
      }
    }
  }
}


TEST_F(GraphReducerTest, Order) {
  // Test that the order of reducers doesn't matter, as they should be
  // rerun for changed nodes.
  for (int i = 0; i < 2; i++) {
    Node* n1 = graph()->NewNode(&kOpA0);
    Node* end = graph()->NewNode(&kOpA1, n1);
    graph()->SetEnd(end);

    InPlaceABReducer abr;
    InPlaceBCReducer bcr;

    // Tests A* => C* with in-place updates.
    for (size_t j = 0; j < 3; j++) {
      size_t before = graph()->NodeCount();
      if (i == 0) {
        ReduceGraph(&abr, &bcr);
      } else {
        ReduceGraph(&bcr, &abr);
      }

      EXPECT_EQ(before, graph()->NodeCount());
      EXPECT_EQ(&kOpC0, n1->op());
      EXPECT_EQ(&kOpC1, end->op());
      EXPECT_EQ(n1, end->InputAt(0));
    }
  }
}

}  // namespace graph_reducer_unittest
}  // namespace compiler
}  // namespace internal
}  // namespace v8
