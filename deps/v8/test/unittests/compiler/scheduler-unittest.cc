// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/scheduler.h"

#include "src/codegen/tick-counter.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turbofan-graph.h"
#include "src/compiler/verifier.h"
#include "test/unittests/compiler/compiler-test-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::AnyOf;

namespace v8 {
namespace internal {
namespace compiler {

class SchedulerTest : public TestWithIsolateAndZone {
 public:
  SchedulerTest()
      : TestWithIsolateAndZone(kCompressGraphZone),
        graph_(zone()),
        common_(zone()),
        simplified_(zone()),
        js_(zone()) {}

  Schedule* ComputeAndVerifySchedule(size_t expected) {
    if (v8_flags.trace_turbo) {
      SourcePositionTable table(graph());
      NodeOriginTable table2(graph());
      StdoutStream{} << AsJSON(*graph(), &table, &table2);
    }

    Schedule* schedule = Scheduler::ComputeSchedule(
        zone(), graph(), Scheduler::kSplitNodes, tick_counter(), nullptr);

    if (v8_flags.trace_turbo_scheduler) {
      StdoutStream{} << *schedule << std::endl;
    }
    ScheduleVerifier::Run(schedule);
    EXPECT_EQ(expected, GetScheduledNodeCount(schedule));
    return schedule;
  }

  size_t GetScheduledNodeCount(const Schedule* schedule) {
    size_t node_count = 0;
    for (auto block : *schedule->rpo_order()) {
      node_count += block->NodeCount();
      if (block->control() != BasicBlock::kNone) ++node_count;
    }
    return node_count;
  }

  Graph* graph() { return &graph_; }
  CommonOperatorBuilder* common() { return &common_; }
  SimplifiedOperatorBuilder* simplified() { return &simplified_; }
  JSOperatorBuilder* js() { return &js_; }
  TickCounter* tick_counter() { return &tick_counter_; }

 private:
  TickCounter tick_counter_;
  Graph graph_;
  CommonOperatorBuilder common_;
  SimplifiedOperatorBuilder simplified_;
  JSOperatorBuilder js_;
};


namespace {

const Operator kHeapConstant(IrOpcode::kHeapConstant, Operator::kPure,
                             "HeapConstant", 0, 0, 0, 1, 0, 0);
const Operator kIntAdd(IrOpcode::kInt32Add, Operator::kPure, "Int32Add", 2, 0,
                       0, 1, 0, 0);
const Operator kMockCall(IrOpcode::kCall, Operator::kNoProperties, "MockCall",
                         0, 0, 1, 1, 1, 2);
const Operator kMockTailCall(IrOpcode::kTailCall, Operator::kNoProperties,
                             "MockTailCall", 1, 1, 1, 0, 0, 1);

}  // namespace


TEST_F(SchedulerTest, BuildScheduleEmpty) {
  graph()->SetStart(graph()->NewNode(common()->Start(0)));
  graph()->SetEnd(graph()->NewNode(common()->End(1), graph()->start()));
  USE(Scheduler::ComputeSchedule(zone(), graph(), Scheduler::kNoFlags,
                                 tick_counter(), nullptr));
}


TEST_F(SchedulerTest, BuildScheduleOneParameter) {
  graph()->SetStart(graph()->NewNode(common()->Start(0)));

  Node* p1 = graph()->NewNode(common()->Parameter(0), graph()->start());
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, p1, graph()->start(),
                               graph()->start());

  graph()->SetEnd(graph()->NewNode(common()->End(1), ret));

  USE(Scheduler::ComputeSchedule(zone(), graph(), Scheduler::kNoFlags,
                                 tick_counter(), nullptr));
}


namespace {

Node* CreateDiamond(Graph* graph, CommonOperatorBuilder* common, Node* cond) {
  Node* tv = graph->NewNode(common->Int32Constant(6));
  Node* fv = graph->NewNode(common->Int32Constant(7));
  Node* br = graph->NewNode(common->Branch(), cond, graph->start());
  Node* t = graph->NewNode(common->IfTrue(), br);
  Node* f = graph->NewNode(common->IfFalse(), br);
  Node* m = graph->NewNode(common->Merge(2), t, f);
  Node* phi =
      graph->NewNode(common->Phi(MachineRepresentation::kTagged, 2), tv, fv, m);
  return phi;
}

}  // namespace


TARGET_TEST_F(SchedulerTest, FloatingDiamond1) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* d1 = CreateDiamond(graph(), common(), p0);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, d1, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(14);
}

TARGET_TEST_F(SchedulerTest, FloatingDeadDiamond1) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* d1 = CreateDiamond(graph(), common(), p0);
  USE(d1);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, p0, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(5);
}

TARGET_TEST_F(SchedulerTest, FloatingDeadDiamond2) {
  Graph* g = graph();
  Node* start = g->NewNode(common()->Start(1));
  g->SetStart(start);

  Node* n1 = g->NewNode(common()->Parameter(1), start);

  Node* n2 = g->NewNode(common()->Branch(), n1, start);
  Node* n3 = g->NewNode(common()->IfTrue(), n2);
  Node* n4 = g->NewNode(common()->IfFalse(), n2);
  Node* n5 = g->NewNode(common()->Int32Constant(-100));
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* n6 = g->NewNode(common()->Return(), zero, n5, start, n4);
  Node* n7 = g->NewNode(common()->Int32Constant(0));
  Node* n8 = g->NewNode(common()->Return(), zero, n7, start, n3);
  Node* n9 = g->NewNode(common()->End(2), n6, n8);

  // Dead nodes
  Node* n10 = g->NewNode(common()->Branch(), n1, n3);
  Node* n11 = g->NewNode(common()->IfTrue(), n10);
  Node* n12 = g->NewNode(common()->IfFalse(), n10);
  Node* n13 = g->NewNode(common()->Merge(2), n11, n12);
  Node* n14 =
      g->NewNode(common()->Phi(MachineRepresentation::kWord32, 2), n1, n7, n13);

  USE(n14);

  g->SetEnd(n9);

  ComputeAndVerifySchedule(11);
}

TARGET_TEST_F(SchedulerTest, FloatingDiamond2) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* d1 = CreateDiamond(graph(), common(), p0);
  Node* d2 = CreateDiamond(graph(), common(), p1);
  Node* add = graph()->NewNode(&kIntAdd, d1, d2);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, add, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(25);
}


TARGET_TEST_F(SchedulerTest, FloatingDiamond3) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* d1 = CreateDiamond(graph(), common(), p0);
  Node* d2 = CreateDiamond(graph(), common(), p1);
  Node* add = graph()->NewNode(&kIntAdd, d1, d2);
  Node* d3 = CreateDiamond(graph(), common(), add);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, d3, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(34);
}


TARGET_TEST_F(SchedulerTest, NestedFloatingDiamonds) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* fv = graph()->NewNode(common()->Int32Constant(7));
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
                               fv, phi1, m);
  Node* ephi1 = graph()->NewNode(common()->EffectPhi(2), start, map, m);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, ephi1, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(24);
}


TARGET_TEST_F(SchedulerTest, NestedFloatingDiamondWithChain) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);
  Node* c = graph()->NewNode(common()->Int32Constant(7));

  Node* brA1 = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* tA1 = graph()->NewNode(common()->IfTrue(), brA1);
  Node* fA1 = graph()->NewNode(common()->IfFalse(), brA1);
  Node* mA1 = graph()->NewNode(common()->Merge(2), tA1, fA1);
  Node* phiA1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), p0, p1, mA1);

  Node* brB1 = graph()->NewNode(common()->Branch(), p1, graph()->start());
  Node* tB1 = graph()->NewNode(common()->IfTrue(), brB1);
  Node* fB1 = graph()->NewNode(common()->IfFalse(), brB1);
  Node* mB1 = graph()->NewNode(common()->Merge(2), tB1, fB1);
  Node* phiB1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), p0, p1, mB1);

  Node* brA2 = graph()->NewNode(common()->Branch(), phiB1, mA1);
  Node* tA2 = graph()->NewNode(common()->IfTrue(), brA2);
  Node* fA2 = graph()->NewNode(common()->IfFalse(), brA2);
  Node* mA2 = graph()->NewNode(common()->Merge(2), tA2, fA2);
  Node* phiA2 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), phiB1, c, mA2);

  Node* brB2 = graph()->NewNode(common()->Branch(), phiA1, mB1);
  Node* tB2 = graph()->NewNode(common()->IfTrue(), brB2);
  Node* fB2 = graph()->NewNode(common()->IfFalse(), brB2);
  Node* mB2 = graph()->NewNode(common()->Merge(2), tB2, fB2);
  Node* phiB2 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), phiA1, c, mB2);

  Node* add = graph()->NewNode(&kIntAdd, phiA2, phiB2);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, add, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(37);
}


TARGET_TEST_F(SchedulerTest, NestedFloatingDiamondWithLoop) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* fv = graph()->NewNode(common()->Int32Constant(7));
  Node* br = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);

  Node* loop = graph()->NewNode(common()->Loop(2), f, start);
  Node* ind = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               p0, p0, loop);

  Node* add = graph()->NewNode(&kIntAdd, ind, fv);
  Node* br1 = graph()->NewNode(common()->Branch(), add, loop);
  Node* t1 = graph()->NewNode(common()->IfTrue(), br1);
  Node* f1 = graph()->NewNode(common()->IfFalse(), br1);

  loop->ReplaceInput(1, t1);  // close loop.
  ind->ReplaceInput(1, ind);  // close induction variable.

  Node* m = graph()->NewNode(common()->Merge(2), t, f1);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               fv, ind, m);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(21);
}


TARGET_TEST_F(SchedulerTest, LoopedFloatingDiamond1) {
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

  ComputeAndVerifySchedule(21);
}


TARGET_TEST_F(SchedulerTest, LoopedFloatingDiamond2) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* c = graph()->NewNode(common()->Int32Constant(7));
  Node* loop = graph()->NewNode(common()->Loop(2), start, start);
  Node* ind = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               p0, p0, loop);

  Node* br1 = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t1 = graph()->NewNode(common()->IfTrue(), br1);
  Node* f1 = graph()->NewNode(common()->IfFalse(), br1);
  Node* m1 = graph()->NewNode(common()->Merge(2), t1, f1);
  Node* phi1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), c, ind, m1);

  Node* add = graph()->NewNode(&kIntAdd, ind, phi1);

  Node* br = graph()->NewNode(common()->Branch(), add, loop);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);

  loop->ReplaceInput(1, t);   // close loop.
  ind->ReplaceInput(1, add);  // close induction variable.

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, ind, start, f);
  Node* end = graph()->NewNode(common()->End(2), ret, f);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(21);
}


TARGET_TEST_F(SchedulerTest, LoopedFloatingDiamond3) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);

  Node* c = graph()->NewNode(common()->Int32Constant(7));
  Node* loop = graph()->NewNode(common()->Loop(2), start, start);
  Node* ind = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               p0, p0, loop);

  Node* br1 = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t1 = graph()->NewNode(common()->IfTrue(), br1);
  Node* f1 = graph()->NewNode(common()->IfFalse(), br1);

  Node* loop1 = graph()->NewNode(common()->Loop(2), t1, start);
  Node* ind1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), p0, p0, loop);

  Node* add1 = graph()->NewNode(&kIntAdd, ind1, c);
  Node* br2 = graph()->NewNode(common()->Branch(), add1, loop1);
  Node* t2 = graph()->NewNode(common()->IfTrue(), br2);
  Node* f2 = graph()->NewNode(common()->IfFalse(), br2);

  loop1->ReplaceInput(1, t2);   // close inner loop.
  ind1->ReplaceInput(1, ind1);  // close inner induction variable.

  Node* m1 = graph()->NewNode(common()->Merge(2), f1, f2);
  Node* phi1 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), c, ind1, m1);

  Node* add = graph()->NewNode(&kIntAdd, ind, phi1);

  Node* br = graph()->NewNode(common()->Branch(), add, loop);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);

  loop->ReplaceInput(1, t);   // close loop.
  ind->ReplaceInput(1, add);  // close induction variable.

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, ind, start, f);
  Node* end = graph()->NewNode(common()->End(2), ret, f);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(29);
}


TARGET_TEST_F(SchedulerTest, PhisPushedDownToDifferentBranches) {
  Node* start = graph()->NewNode(common()->Start(2));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* p1 = graph()->NewNode(common()->Parameter(1), start);

  Node* v1 = graph()->NewNode(common()->Int32Constant(1));
  Node* v2 = graph()->NewNode(common()->Int32Constant(2));
  Node* v3 = graph()->NewNode(common()->Int32Constant(3));
  Node* v4 = graph()->NewNode(common()->Int32Constant(4));
  Node* br = graph()->NewNode(common()->Branch(), p0, graph()->start());
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);
  Node* m = graph()->NewNode(common()->Merge(2), t, f);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               v1, v2, m);
  Node* phi2 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), v3, v4, m);

  Node* br2 = graph()->NewNode(common()->Branch(), p1, graph()->start());
  Node* t2 = graph()->NewNode(common()->IfTrue(), br2);
  Node* f2 = graph()->NewNode(common()->IfFalse(), br2);
  Node* m2 = graph()->NewNode(common()->Merge(2), t2, f2);
  Node* phi3 = graph()->NewNode(
      common()->Phi(MachineRepresentation::kTagged, 2), phi, phi2, m2);

  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi3, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(25);
}


TARGET_TEST_F(SchedulerTest, BranchHintTrue) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* tv = graph()->NewNode(common()->Int32Constant(6));
  Node* fv = graph()->NewNode(common()->Int32Constant(7));
  Node* br = graph()->NewNode(common()->Branch(BranchHint::kTrue), p0, start);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);
  Node* m = graph()->NewNode(common()->Merge(2), t, f);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               tv, fv, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  Schedule* schedule = ComputeAndVerifySchedule(14);
  // Make sure the false block is marked as deferred.
  EXPECT_FALSE(schedule->block(t)->deferred());
  EXPECT_TRUE(schedule->block(f)->deferred());
}


TARGET_TEST_F(SchedulerTest, BranchHintFalse) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* tv = graph()->NewNode(common()->Int32Constant(6));
  Node* fv = graph()->NewNode(common()->Int32Constant(7));
  Node* br = graph()->NewNode(common()->Branch(BranchHint::kFalse), p0, start);
  Node* t = graph()->NewNode(common()->IfTrue(), br);
  Node* f = graph()->NewNode(common()->IfFalse(), br);
  Node* m = graph()->NewNode(common()->Merge(2), t, f);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               tv, fv, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  Schedule* schedule = ComputeAndVerifySchedule(14);
  // Make sure the true block is marked as deferred.
  EXPECT_TRUE(schedule->block(t)->deferred());
  EXPECT_FALSE(schedule->block(f)->deferred());
}


TARGET_TEST_F(SchedulerTest, CallException) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* c1 = graph()->NewNode(&kMockCall, start);
  Node* ok1 = graph()->NewNode(common()->IfSuccess(), c1);
  Node* ex1 = graph()->NewNode(common()->IfException(), c1, c1);
  Node* c2 = graph()->NewNode(&kMockCall, ok1);
  Node* ok2 = graph()->NewNode(common()->IfSuccess(), c2);
  Node* ex2 = graph()->NewNode(common()->IfException(), c2, c2);
  Node* hdl = graph()->NewNode(common()->Merge(2), ex1, ex2);
  Node* m = graph()->NewNode(common()->Merge(2), ok2, hdl);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               c2, p0, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, m);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  Schedule* schedule = ComputeAndVerifySchedule(18);
  // Make sure the exception blocks as well as the handler are deferred.
  EXPECT_TRUE(schedule->block(ex1)->deferred());
  EXPECT_TRUE(schedule->block(ex2)->deferred());
  EXPECT_TRUE(schedule->block(hdl)->deferred());
  EXPECT_FALSE(schedule->block(m)->deferred());
}


TARGET_TEST_F(SchedulerTest, TailCall) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* call = graph()->NewNode(&kMockTailCall, p0, start, start);
  Node* end = graph()->NewNode(common()->End(1), call);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(4);
}


TARGET_TEST_F(SchedulerTest, Switch) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* sw = graph()->NewNode(common()->Switch(3), p0, start);
  Node* c0 = graph()->NewNode(common()->IfValue(0), sw);
  Node* v0 = graph()->NewNode(common()->Int32Constant(11));
  Node* c1 = graph()->NewNode(common()->IfValue(1), sw);
  Node* v1 = graph()->NewNode(common()->Int32Constant(22));
  Node* d = graph()->NewNode(common()->IfDefault(), sw);
  Node* vd = graph()->NewNode(common()->Int32Constant(33));
  Node* m = graph()->NewNode(common()->Merge(3), c0, c1, d);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kWord32, 3),
                               v0, v1, vd, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, m);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(17);
}


TARGET_TEST_F(SchedulerTest, FloatingSwitch) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* p0 = graph()->NewNode(common()->Parameter(0), start);
  Node* sw = graph()->NewNode(common()->Switch(3), p0, start);
  Node* c0 = graph()->NewNode(common()->IfValue(0), sw);
  Node* v0 = graph()->NewNode(common()->Int32Constant(11));
  Node* c1 = graph()->NewNode(common()->IfValue(1), sw);
  Node* v1 = graph()->NewNode(common()->Int32Constant(22));
  Node* d = graph()->NewNode(common()->IfDefault(), sw);
  Node* vd = graph()->NewNode(common()->Int32Constant(33));
  Node* m = graph()->NewNode(common()->Merge(3), c0, c1, d);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kWord32, 3),
                               v0, v1, vd, m);
  Node* zero = graph()->NewNode(common()->Int32Constant(0));
  Node* ret = graph()->NewNode(common()->Return(), zero, phi, start, start);
  Node* end = graph()->NewNode(common()->End(1), ret);

  graph()->SetEnd(end);

  ComputeAndVerifySchedule(17);
}


TARGET_TEST_F(SchedulerTest, Terminate) {
  Node* start = graph()->NewNode(common()->Start(1));
  graph()->SetStart(start);

  Node* loop = graph()->NewNode(common()->Loop(2), start, start);
  loop->ReplaceInput(1, loop);  // self loop, NTL.

  Node* effect = graph()->NewNode(common()->EffectPhi(2), start, start, loop);
  effect->ReplaceInput(1, effect);  // self loop.

  Node* terminate = graph()->NewNode(common()->Terminate(), effect, loop);
  Node* end = graph()->NewNode(common()->End(1), terminate);
  graph()->SetEnd(end);

  Schedule* schedule = ComputeAndVerifySchedule(6);
  BasicBlock* block = schedule->block(loop);
  EXPECT_EQ(block, schedule->block(effect));
  EXPECT_GE(block->rpo_number(), 0);
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
