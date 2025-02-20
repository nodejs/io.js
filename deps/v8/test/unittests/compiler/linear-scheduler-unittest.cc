// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/linear-scheduler.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turbofan-graph.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::AnyOf;

namespace v8 {
namespace internal {
namespace compiler {

class LinearSchedulerTest : public TestWithIsolateAndZone {
 public:
  LinearSchedulerTest()
      : TestWithIsolateAndZone(kCompressGraphZone),
        graph_(zone()),
        common_(zone()),
        simplified_(zone()) {}

  Graph* graph() { return &graph_; }
  CommonOperatorBuilder* common() { return &common_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }

 private:
  Graph graph_;
  CommonOperatorBuilder common_;
  SimplifiedOperatorBuilder simplified_;
};

namespace {

const Operator kIntAdd(IrOpcode::kInt32Add, Operator::kPure, "Int32Add", 2, 0,
                       0, 1, 0, 0);

}  // namespace

TEST_F(LinearSchedulerTest, BuildSimpleScheduleEmpty) {
  Node* start = graph()->NewNode(common()->Start(0));
  graph()->SetStart(start);

  Node* end = graph()->NewNode(common()->End(1), graph()->start());
  graph()->SetEnd(end);

  LinearScheduler simple_scheduler(zone(), graph());
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(start, end));
}

TEST_F(LinearSchedulerTest, BuildSimpleScheduleOneParameter) {
  graph()->SetStart(graph()->NewNode(common()->Start(0)));

  Node* p1 = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, p1, graph()->start(),
                               graph()->start());

  graph()->SetEnd(graph()->NewNode(common()->End(1), ret));

  LinearScheduler simple_scheduler(zone(), graph());
  EXPECT_TRUE(simple_scheduler.SameBasicBlock(p1, zero));
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(zero, ret));
}

TARGET_TEST_F(LinearSchedulerTest, FloatingDiamond) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* cond = graph()->NewNode(common()->Parameter(0), start);
  Node* tv = graph()->NewNode(common()->Int32Constant(6));
  Node* fv = graph()->NewNode(common()->Int32Constant(7));
  Node* br = graph()->NewNode(common()->Branch(), cond, start);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);
  Node* m = graph()->NewNode(common()->Merge(2), t, f);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               tv, fv, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  LinearScheduler simple_scheduler(zone(), graph());
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(t, f));
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(phi, t));
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(phi, f));
}

TARGET_TEST_F(LinearSchedulerTest, NestedFloatingDiamonds) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* tv = graph()->NewNode(common()->Int32Constant(7));
  Node* br = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);

  Node* map = graph()->NewNode(
      simplified()->LoadElement(AccessBuilder::ForFixedArrayElement()), p0, p0,
      start, f);
  Node* br1 = graph()->NewNode(common()->Branch(), map, graph()->start());
  Node* t1 = graph()->NewNode(common()->IfTrue(), br1);
  Node* f1 = graph()->NewNode(common()->IfFalse(), br1);
  Node* m1 = graph()->NewNode(common()->Merge(2), t1, f1);
  Node* ttrue = graph()->NewNode(common()->Int32Constant(1));
  Node* ffalse = graph()->NewNode(common()->Int32Constant(0));
  Node* phi1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), ttrue, ffalse, m1);

  Node* m = graph()->NewNode(common()->Merge(2), t, f);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               tv, phi1, m);
  Node* ephi1 = graph()->NewNode(common()->EffectPhi(2), start, map, m);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, ephi1, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  LinearScheduler simple_scheduler(zone(), graph());
  EXPECT_TRUE(simple_scheduler.SameBasicBlock(map, f));
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(map, br1));
  EXPECT_TRUE(simple_scheduler.SameBasicBlock(ephi1, phi));
}

TARGET_TEST_F(LinearSchedulerTest, LoopedFloatingDiamond) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* c = graph()->NewNode(common()->Int32Constant(7));
  Node* loop = graph()->NewNode(common()->Loop(2), start, start);
  Node* ind = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               p0, p0, loop);
  Node* add = graph()->NewNode(&kIntAdd, ind, c);

  Node* br = graph()->NewNode(common()->Branch(), add, loop);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);

  Node* br1 = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t1 = graph()->NewNode(common()->IfTrue(), br1);
  Node* f1 = graph()->NewNode(common()->IfFalse(), br1);
  Node* m1 = graph()->NewNode(common()->Merge(2), t1, f1);
  Node* phi1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), add, p0, m1);

  loop->ReplaceInput(1, t);    // close loop.
  ind->ReplaceInput(1, phi1);  // close induction variable.

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, ind, start, f);
  Node* end = graph()->NewNode(common()->End(2), ret, f);

  graph()->SetEnd(end);

  LinearScheduler simple_scheduler(zone(), graph());
  EXPECT_TRUE(simple_scheduler.SameBasicBlock(ind, loop));
  EXPECT_TRUE(simple_scheduler.SameBasicBlock(phi1, m1));
  EXPECT_FALSE(simple_scheduler.SameBasicBlock(loop, m1));
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
