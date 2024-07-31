// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-operator-reducer.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/simplified-operator.h"
#include "src/numbers/conversions-inl.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::BitEq;


namespace v8 {
namespace internal {
namespace compiler {
namespace simplified_operator_reducer_unittest {

class SimplifiedOperatorReducerTest : public GraphTest {
 public:
  explicit SimplifiedOperatorReducerTest(int num_parameters = 1)
      : GraphTest(num_parameters), simplified_(zone()) {}
  ~SimplifiedOperatorReducerTest() override = default;

 protected:
  Reduction Reduce(Node* node) {
    MachineOperatorBuilder machine(zone());
    JSOperatorBuilder javascript(zone());
    JSGraph jsgraph(isolate(), graph(), common(), &javascript, simplified(),
                    &machine);
    GraphReducer graph_reducer(zone(), graph(), tick_counter(), broker());
    SimplifiedOperatorReducer reducer(&graph_reducer, &jsgraph, broker(),
                                      BranchSemantics::kJS);
    return reducer.Reduce(node);
  }

  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  SimplifiedOperatorBuilder simplified_;
};


template <typename T>
class SimplifiedOperatorReducerTestWithParam
    : public SimplifiedOperatorReducerTest,
      public ::testing::WithParamInterface<T> {
 public:
  explicit SimplifiedOperatorReducerTestWithParam(int num_parameters = 1)
      : SimplifiedOperatorReducerTest(num_parameters) {}
  ~SimplifiedOperatorReducerTestWithParam() override = default;
};


namespace {

const double kFloat64Values[] = {
    -V8_INFINITY, -6.52696e+290, -1.05768e+290, -5.34203e+268, -1.01997e+268,
    -8.22758e+266, -1.58402e+261, -5.15246e+241, -5.92107e+226, -1.21477e+226,
    -1.67913e+188, -1.6257e+184, -2.60043e+170, -2.52941e+168, -3.06033e+116,
    -4.56201e+52, -3.56788e+50, -9.9066e+38, -3.07261e+31, -2.1271e+09,
    -1.91489e+09, -1.73053e+09, -9.30675e+08, -26030, -20453, -15790, -11699,
    -111, -97, -78, -63, -58, -1.53858e-06, -2.98914e-12, -1.14741e-39,
    -8.20347e-57, -1.48932e-59, -3.17692e-66, -8.93103e-81, -3.91337e-83,
    -6.0489e-92, -8.83291e-113, -4.28266e-117, -1.92058e-178, -2.0567e-192,
    -1.68167e-194, -1.51841e-214, -3.98738e-234, -7.31851e-242, -2.21875e-253,
    -1.11612e-293, -0.0, 0.0, 2.22507e-308, 1.06526e-307, 4.16643e-227,
    6.76624e-223, 2.0432e-197, 3.16254e-184, 1.37315e-173, 2.88603e-172,
    1.54155e-99, 4.42923e-81, 1.40539e-73, 5.4462e-73, 1.24064e-58, 3.11167e-58,
    2.75826e-39, 0.143815, 58, 67, 601, 7941, 11644, 13697, 25680, 29882,
    1.32165e+08, 1.62439e+08, 4.16837e+08, 9.59097e+08, 1.32491e+09, 1.8728e+09,
    1.0672e+17, 2.69606e+46, 1.98285e+79, 1.0098e+82, 7.93064e+88, 3.67444e+121,
    9.36506e+123, 7.27954e+162, 3.05316e+168, 1.16171e+175, 1.64771e+189,
    1.1622e+202, 2.00748e+239, 2.51778e+244, 3.90282e+306, 1.79769e+308,
    V8_INFINITY};


const int32_t kInt32Values[] = {
    -2147483647 - 1, -2104508227, -2103151830, -1435284490, -1378926425,
    -1318814539, -1289388009, -1287537572, -1279026536, -1241605942,
    -1226046939, -941837148, -779818051, -413830641, -245798087, -184657557,
    -127145950, -105483328, -32325, -26653, -23858, -23834, -22363, -19858,
    -19044, -18744, -15528, -5309, -3372, -2093, -104, -98, -97, -93, -84, -80,
    -78, -76, -72, -58, -57, -56, -55, -45, -40, -34, -32, -25, -24, -5, -2, 0,
    3, 10, 24, 34, 42, 46, 47, 48, 52, 56, 64, 65, 71, 76, 79, 81, 82, 97, 102,
    103, 104, 106, 107, 109, 116, 122, 3653, 4485, 12405, 16504, 26262, 28704,
    29755, 30554, 16476817, 605431957, 832401070, 873617242, 914205764,
    1062628108, 1087581664, 1488498068, 1534668023, 1661587028, 1696896187,
    1866841746, 2032089723, 2147483647};

const double kNaNs[] = {-std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::quiet_NaN(),
                        base::bit_cast<double>(uint64_t{0x7FFFFFFFFFFFFFFF}),
                        base::bit_cast<double>(uint64_t{0xFFFFFFFFFFFFFFFF})};

const CheckForMinusZeroMode kCheckForMinusZeroModes[] = {
    CheckForMinusZeroMode::kDontCheckForMinusZero,
    CheckForMinusZeroMode::kCheckForMinusZero};

}  // namespace


// -----------------------------------------------------------------------------
// BooleanNot


TEST_F(SimplifiedOperatorReducerTest, BooleanNotWithBooleanNot) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(
      graph()->NewNode(simplified()->BooleanNot(),
                       graph()->NewNode(simplified()->BooleanNot(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(param0, reduction.replacement());
}


TEST_F(SimplifiedOperatorReducerTest, BooleanNotWithFalseConstant) {
  Reduction reduction0 =
      Reduce(graph()->NewNode(simplified()->BooleanNot(), FalseConstant()));
  ASSERT_TRUE(reduction0.Changed());
  EXPECT_THAT(reduction0.replacement(), IsTrueConstant());
}


TEST_F(SimplifiedOperatorReducerTest, BooleanNotWithTrueConstant) {
  Reduction reduction1 =
      Reduce(graph()->NewNode(simplified()->BooleanNot(), TrueConstant()));
  ASSERT_TRUE(reduction1.Changed());
  EXPECT_THAT(reduction1.replacement(), IsFalseConstant());
}


// -----------------------------------------------------------------------------
// ChangeTaggedToBit

TEST_F(SimplifiedOperatorReducerTest, ChangeBitToTaggedWithChangeTaggedToBit) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeBitToTagged(),
      graph()->NewNode(simplified()->ChangeTaggedToBit(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(param0, reduction.replacement());
}

TEST_F(SimplifiedOperatorReducerTest, ChangeBitToTaggedWithZeroConstant) {
  Reduction reduction = Reduce(
      graph()->NewNode(simplified()->ChangeBitToTagged(), Int32Constant(0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsFalseConstant());
}

TEST_F(SimplifiedOperatorReducerTest, ChangeBitToTaggedWithOneConstant) {
  Reduction reduction = Reduce(
      graph()->NewNode(simplified()->ChangeBitToTagged(), Int32Constant(1)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsTrueConstant());
}


// -----------------------------------------------------------------------------
// ChangeTaggedToBit

TEST_F(SimplifiedOperatorReducerTest, ChangeTaggedToBitWithFalseConstant) {
  Reduction reduction = Reduce(
      graph()->NewNode(simplified()->ChangeTaggedToBit(), FalseConstant()));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsInt32Constant(0));
}

TEST_F(SimplifiedOperatorReducerTest, ChangeTaggedToBitWithTrueConstant) {
  Reduction reduction = Reduce(
      graph()->NewNode(simplified()->ChangeTaggedToBit(), TrueConstant()));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsInt32Constant(1));
}

TEST_F(SimplifiedOperatorReducerTest, ChangeTaggedToBitWithChangeBitToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeTaggedToBit(),
      graph()->NewNode(simplified()->ChangeBitToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(param0, reduction.replacement());
}

// -----------------------------------------------------------------------------
// ChangeFloat64ToTagged

TEST_F(SimplifiedOperatorReducerTest, ChangeFloat64ToTaggedWithConstant) {
  TRACED_FOREACH(CheckForMinusZeroMode, mode, kCheckForMinusZeroModes) {
    TRACED_FOREACH(double, n, kFloat64Values) {
      Reduction reduction = Reduce(graph()->NewNode(
          simplified()->ChangeFloat64ToTagged(mode), Float64Constant(n)));
      ASSERT_TRUE(reduction.Changed());
      EXPECT_THAT(reduction.replacement(), IsNumberConstant(BitEq(n)));
    }
  }
}

// -----------------------------------------------------------------------------
// ChangeInt32ToTagged


TEST_F(SimplifiedOperatorReducerTest, ChangeInt32ToTaggedWithConstant) {
  TRACED_FOREACH(int32_t, n, kInt32Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeInt32ToTagged(), Int32Constant(n)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsNumberConstant(BitEq(FastI2D(n))));
  }
}


// -----------------------------------------------------------------------------
// ChangeTaggedToFloat64


TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToFloat64WithChangeFloat64ToTagged) {
  Node* param0 = Parameter(0);
  TRACED_FOREACH(CheckForMinusZeroMode, mode, kCheckForMinusZeroModes) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeTaggedToFloat64(),
        graph()->NewNode(simplified()->ChangeFloat64ToTagged(mode), param0)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_EQ(param0, reduction.replacement());
  }
}

TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToFloat64WithChangeInt32ToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeTaggedToFloat64(),
      graph()->NewNode(simplified()->ChangeInt32ToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsChangeInt32ToFloat64(param0));
}


TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToFloat64WithChangeUint32ToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeTaggedToFloat64(),
      graph()->NewNode(simplified()->ChangeUint32ToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsChangeUint32ToFloat64(param0));
}


TEST_F(SimplifiedOperatorReducerTest, ChangeTaggedToFloat64WithConstant) {
  TRACED_FOREACH(double, n, kFloat64Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeTaggedToFloat64(), NumberConstant(n)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq(n)));
  }
}


TEST_F(SimplifiedOperatorReducerTest, ChangeTaggedToFloat64WithNaNConstant) {
  TRACED_FOREACH(double, nan, kNaNs) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeTaggedToFloat64(), NumberConstant(nan)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFloat64Constant(BitEq(nan)));
  }
}


// -----------------------------------------------------------------------------
// ChangeTaggedToInt32

TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToInt32WithChangeFloat64ToTagged) {
  Node* param0 = Parameter(0);
  TRACED_FOREACH(CheckForMinusZeroMode, mode, kCheckForMinusZeroModes) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeTaggedToInt32(),
        graph()->NewNode(simplified()->ChangeFloat64ToTagged(mode), param0)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsChangeFloat64ToInt32(param0));
  }
}

TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToInt32WithChangeInt32ToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeTaggedToInt32(),
      graph()->NewNode(simplified()->ChangeInt32ToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(param0, reduction.replacement());
}


// -----------------------------------------------------------------------------
// ChangeTaggedToUint32

TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToUint32WithChangeFloat64ToTagged) {
  Node* param0 = Parameter(0);
  TRACED_FOREACH(CheckForMinusZeroMode, mode, kCheckForMinusZeroModes) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->ChangeTaggedToUint32(),
        graph()->NewNode(simplified()->ChangeFloat64ToTagged(mode), param0)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsChangeFloat64ToUint32(param0));
  }
}

TEST_F(SimplifiedOperatorReducerTest,
       ChangeTaggedToUint32WithChangeUint32ToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ChangeTaggedToUint32(),
      graph()->NewNode(simplified()->ChangeUint32ToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(param0, reduction.replacement());
}


// -----------------------------------------------------------------------------
// TruncateTaggedToWord32

TEST_F(SimplifiedOperatorReducerTest,
       TruncateTaggedToWord3WithChangeFloat64ToTagged) {
  Node* param0 = Parameter(0);
  TRACED_FOREACH(CheckForMinusZeroMode, mode, kCheckForMinusZeroModes) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->TruncateTaggedToWord32(),
        graph()->NewNode(simplified()->ChangeFloat64ToTagged(mode), param0)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsTruncateFloat64ToWord32(param0));
  }
}

TEST_F(SimplifiedOperatorReducerTest, TruncateTaggedToWord32WithConstant) {
  TRACED_FOREACH(double, n, kFloat64Values) {
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->TruncateTaggedToWord32(), NumberConstant(n)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsInt32Constant(DoubleToInt32(n)));
  }
}

// -----------------------------------------------------------------------------
// CheckedFloat64ToInt32

TEST_F(SimplifiedOperatorReducerTest, CheckedFloat64ToInt32WithConstant) {
  Node* effect = graph()->start();
  Node* control = graph()->start();
  TRACED_FOREACH(int32_t, n, kInt32Values) {
    Reduction r = Reduce(graph()->NewNode(
        simplified()->CheckedFloat64ToInt32(
            CheckForMinusZeroMode::kDontCheckForMinusZero, FeedbackSource()),
        Float64Constant(n), effect, control));
    ASSERT_TRUE(r.Changed());
    EXPECT_THAT(r.replacement(), IsInt32Constant(n));
  }
}

// -----------------------------------------------------------------------------
// CheckHeapObject

TEST_F(SimplifiedOperatorReducerTest, CheckHeapObjectWithChangeBitToTagged) {
  Node* param0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* value = graph()->NewNode(simplified()->ChangeBitToTagged(), param0);
  Reduction reduction = Reduce(graph()->NewNode(simplified()->CheckHeapObject(),
                                                value, effect, control));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(SimplifiedOperatorReducerTest, CheckHeapObjectWithHeapConstant) {
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Handle<HeapObject> kHeapObjects[] = {
      factory()->empty_string(), factory()->null_value(),
      factory()->species_symbol(), factory()->undefined_value()};
  TRACED_FOREACH(Handle<HeapObject>, object, kHeapObjects) {
    Node* value = HeapConstantNoHole(object);
    Reduction reduction = Reduce(graph()->NewNode(
        simplified()->CheckHeapObject(), value, effect, control));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_EQ(value, reduction.replacement());
  }
}

TEST_F(SimplifiedOperatorReducerTest, CheckHeapObjectWithCheckHeapObject) {
  Node* param0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* value = effect = graph()->NewNode(simplified()->CheckHeapObject(),
                                          param0, effect, control);
  Reduction reduction = Reduce(graph()->NewNode(simplified()->CheckHeapObject(),
                                                value, effect, control));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

// -----------------------------------------------------------------------------
// CheckSmi

TEST_F(SimplifiedOperatorReducerTest, CheckSmiWithChangeInt31ToTaggedSigned) {
  Node* param0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* value =
      graph()->NewNode(simplified()->ChangeInt31ToTaggedSigned(), param0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->CheckSmi(FeedbackSource()), value, effect, control));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(SimplifiedOperatorReducerTest, CheckSmiWithNumberConstant) {
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* value = NumberConstant(1.0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->CheckSmi(FeedbackSource()), value, effect, control));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

TEST_F(SimplifiedOperatorReducerTest, CheckSmiWithCheckSmi) {
  Node* param0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  Node* value = effect = graph()->NewNode(
      simplified()->CheckSmi(FeedbackSource()), param0, effect, control);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->CheckSmi(FeedbackSource()), value, effect, control));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_EQ(value, reduction.replacement());
}

// -----------------------------------------------------------------------------
// NumberAbs

TEST_F(SimplifiedOperatorReducerTest, NumberAbsWithNumberConstant) {
  TRACED_FOREACH(double, n, kFloat64Values) {
    Reduction reduction =
        Reduce(graph()->NewNode(simplified()->NumberAbs(), NumberConstant(n)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsNumberConstant(std::fabs(n)));
  }
}

// -----------------------------------------------------------------------------
// ObjectIsSmi

TEST_F(SimplifiedOperatorReducerTest, ObjectIsSmiWithChangeBitToTagged) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ObjectIsSmi(),
      graph()->NewNode(simplified()->ChangeBitToTagged(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsFalseConstant());
}

TEST_F(SimplifiedOperatorReducerTest,
       ObjectIsSmiWithChangeInt31ToTaggedSigned) {
  Node* param0 = Parameter(0);
  Reduction reduction = Reduce(graph()->NewNode(
      simplified()->ObjectIsSmi(),
      graph()->NewNode(simplified()->ChangeInt31ToTaggedSigned(), param0)));
  ASSERT_TRUE(reduction.Changed());
  EXPECT_THAT(reduction.replacement(), IsTrueConstant());
}

TEST_F(SimplifiedOperatorReducerTest, ObjectIsSmiWithHeapConstant) {
  Handle<HeapObject> kHeapObjects[] = {
      factory()->empty_string(), factory()->null_value(),
      factory()->species_symbol(), factory()->undefined_value()};
  TRACED_FOREACH(Handle<HeapObject>, o, kHeapObjects) {
    Reduction reduction = Reduce(
        graph()->NewNode(simplified()->ObjectIsSmi(), HeapConstantNoHole(o)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsFalseConstant());
  }
}

TEST_F(SimplifiedOperatorReducerTest, ObjectIsSmiWithNumberConstant) {
  TRACED_FOREACH(double, n, kFloat64Values) {
    Reduction reduction = Reduce(
        graph()->NewNode(simplified()->ObjectIsSmi(), NumberConstant(n)));
    ASSERT_TRUE(reduction.Changed());
    EXPECT_THAT(reduction.replacement(), IsBooleanConstant(IsSmiDouble(n)));
  }
}

// -----------------------------------------------------------------------------
// CheckedInt32Add

TEST_F(SimplifiedOperatorReducerTest,
       CheckedInt32AddConsecutivelyWithConstants) {
  Node* p0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  TRACED_FOREACH(int32_t, a, kInt32Values) {
    TRACED_FOREACH(int32_t, b, kInt32Values) {
      Node* add1 = graph()->NewNode(simplified()->CheckedInt32Add(), p0,
                                    Int32Constant(a), effect, control);
      Node* add2 = graph()->NewNode(simplified()->CheckedInt32Add(), add1,
                                    Int32Constant(b), add1, control);

      Reduction r = Reduce(add2);
      int32_t c;
      bool overflow = base::bits::SignedAddOverflow32(a, b, &c);
      if ((a >= 0) == (b >= 0) && !overflow) {
        ASSERT_TRUE(r.Changed());
        Node* new_node = r.replacement();
        ASSERT_EQ(new_node->opcode(), IrOpcode::kCheckedInt32Add);
        ASSERT_EQ(new_node->InputAt(0), p0);
        EXPECT_THAT(new_node->InputAt(1), IsInt32Constant(c));
        ASSERT_EQ(new_node->InputAt(2), effect);
        ASSERT_EQ(new_node->InputAt(3), control);
        EXPECT_TRUE(add1->uses().empty());
      } else {
        ASSERT_FALSE(r.Changed());
      }
    }
  }
}

TEST_F(SimplifiedOperatorReducerTest,
       CheckedInt32AddConsecutivelyWithConstantsNoChanged) {
  Node* p0 = Parameter(0);
  Node* effect = graph()->start();
  Node* control = graph()->start();
  TRACED_FOREACH(int32_t, a, kInt32Values) {
    TRACED_FOREACH(int32_t, b, kInt32Values) {
      Node* add1 = graph()->NewNode(simplified()->CheckedInt32Add(), p0,
                                    Int32Constant(a), effect, control);
      Node* add2 = graph()->NewNode(simplified()->CheckedInt32Add(), add1,
                                    Int32Constant(b), add1, control);
      Node* add3 = graph()->NewNode(simplified()->CheckedInt32Add(), add1,
                                    Int32Constant(b), effect, control);

      // No changed since add1 has other value uses.
      Reduction r = Reduce(add2);
      ASSERT_FALSE(r.Changed());
      r = Reduce(add3);
      ASSERT_FALSE(r.Changed());
    }
  }
}

}  // namespace simplified_operator_reducer_unittest
}  // namespace compiler
}  // namespace internal
}  // namespace v8
