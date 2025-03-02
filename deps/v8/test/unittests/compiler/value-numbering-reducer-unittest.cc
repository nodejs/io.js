// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/value-numbering-reducer.h"

#include <limits>

#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-graph.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace value_numbering_reducer_unittest {

struct TestOperator : public Operator {
  TestOperator(Operator::Opcode opcode, Operator::Properties properties,
               size_t value_in, size_t value_out)
      : Operator(opcode, properties, "TestOp", value_in, 0, 0, value_out, 0,
                 0) {}
};


static const TestOperator kOp0(0, Operator::kIdempotent, 0, 1);
static const TestOperator kOp1(1, Operator::kIdempotent, 1, 1);


class ValueNumberingReducerTest : public TestWithZone {
 public:
  ValueNumberingReducerTest()
      : TestWithZone(kCompressGraphZone),
        graph_(zone()),
        reducer_(zone(), graph()->zone()) {}

 protected:
  Reduction Reduce(Node* node) { return reducer_.Reduce(node); }

  Graph* graph() { return &graph_; }

 private:
  Graph graph_;
  ValueNumberingReducer reducer_;
};


TEST_F(ValueNumberingReducerTest, AllInputsAreChecked) {
  Node* na = graph()->NewNode(&kOp0);
  Node* nb = graph()->NewNode(&kOp0);
  Node* n1 = graph()->NewNode(&kOp1, na);
  Node* n2 = graph()->NewNode(&kOp1, nb);
  EXPECT_FALSE(Reduce(n1).Changed());
  EXPECT_FALSE(Reduce(n2).Changed());
}


TEST_F(ValueNumberingReducerTest, DeadNodesAreNeverReturned) {
  Node* n0 = graph()->NewNode(&kOp0);
  Node* n1 = graph()->NewNode(&kOp1, n0);
  EXPECT_FALSE(Reduce(n1).Changed());
  n1->Kill();
  EXPECT_FALSE(Reduce(graph()->NewNode(&kOp1, n0)).Changed());
}


TEST_F(ValueNumberingReducerTest, OnlyEliminatableNodesAreReduced) {
  TestOperator op(0, Operator::kNoProperties, 0, 1);
  Node* n0 = graph()->NewNode(&op);
  Node* n1 = graph()->NewNode(&op);
  EXPECT_FALSE(Reduce(n0).Changed());
  EXPECT_FALSE(Reduce(n1).Changed());
}


TEST_F(ValueNumberingReducerTest, OperatorEqualityNotIdentity) {
  static const size_t kMaxInputCount = 16;
  Node* inputs[kMaxInputCount];
  for (size_t i = 0; i < arraysize(inputs); ++i) {
    Operator::Opcode opcode = static_cast<Operator::Opcode>(kMaxInputCount + i);
    inputs[i] = graph()->NewNode(
        zone()->New<TestOperator>(opcode, Operator::kIdempotent, 0, 1));
  }
  TRACED_FORRANGE(size_t, input_count, 0, arraysize(inputs)) {
    const TestOperator op1(static_cast<Operator::Opcode>(input_count),
                           Operator::kIdempotent, input_count, 1);
    Node* n1 = graph()->NewNode(&op1, static_cast<int>(input_count), inputs);
    Reduction r1 = Reduce(n1);
    EXPECT_FALSE(r1.Changed());

    const TestOperator op2(static_cast<Operator::Opcode>(input_count),
                           Operator::kIdempotent, input_count, 1);
    Node* n2 = graph()->NewNode(&op2, static_cast<int>(input_count), inputs);
    Reduction r2 = Reduce(n2);
    EXPECT_TRUE(r2.Changed());
    EXPECT_EQ(n1, r2.replacement());
  }
}


TEST_F(ValueNumberingReducerTest, SubsequentReductionsYieldTheSameNode) {
  static const size_t kMaxInputCount = 16;
  Node* inputs[kMaxInputCount];
  for (size_t i = 0; i < arraysize(inputs); ++i) {
    Operator::Opcode opcode = static_cast<Operator::Opcode>(2 + i);
    inputs[i] = graph()->NewNode(
        zone()->New<TestOperator>(opcode, Operator::kIdempotent, 0, 1));
  }
  TRACED_FORRANGE(size_t, input_count, 0, arraysize(inputs)) {
    const TestOperator op1(1, Operator::kIdempotent, input_count, 1);
    Node* n = graph()->NewNode(&op1, static_cast<int>(input_count), inputs);
    Reduction r = Reduce(n);
    EXPECT_FALSE(r.Changed());

    r = Reduce(graph()->NewNode(&op1, static_cast<int>(input_count), inputs));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(n, r.replacement());

    r = Reduce(graph()->NewNode(&op1, static_cast<int>(input_count), inputs));
    ASSERT_TRUE(r.Changed());
    EXPECT_EQ(n, r.replacement());
  }
}


TEST_F(ValueNumberingReducerTest, WontReplaceNodeWithItself) {
  Node* n = graph()->NewNode(&kOp0);
  EXPECT_FALSE(Reduce(n).Changed());
  EXPECT_FALSE(Reduce(n).Changed());
}

}  // namespace value_numbering_reducer_unittest
}  // namespace compiler
}  // namespace internal
}  // namespace v8
