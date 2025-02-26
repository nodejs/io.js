// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/loop-peeling.h"

#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turbofan-graph.h"
#include "test/unittests/compiler/graph-unittest.h"
#include "test/unittests/compiler/node-test-utils.h"
#include "testing/gmock-support.h"

using testing::AllOf;
using testing::BitEq;
using testing::Capture;
using testing::CaptureEq;

namespace v8 {
namespace internal {
namespace compiler {

struct While {
  Node* loop;
  Node* branch;
  Node* if_true;
  Node* if_false;
  Node* exit;
};


// A helper for building branches.
struct Branch {
  Node* branch;
  Node* if_true;
  Node* if_false;
};


// A helper for building counters attached to loops.
struct Counter {
  Node* base;
  Node* inc;
  Node* phi;
  Node* add;
  Node* exit_marker;
};


class LoopPeelingTest : public GraphTest {
 public:
  LoopPeelingTest() : GraphTest(1), machine_(zone()) {}
  ~LoopPeelingTest() override = default;

 protected:
  MachineOperatorBuilder machine_;

  MachineOperatorBuilder* machine() { return &machine_; }

  LoopTree* GetLoopTree() {
    if (v8_flags.trace_turbo_graph) {
      StdoutStream{} << AsRPO(*graph());
    }
    Zone zone(isolate()->allocator(), ZONE_NAME);
    return LoopFinder::BuildLoopTree(graph(), tick_counter(), &zone);
  }


  PeeledIteration* PeelOne() {
    LoopTree* loop_tree = GetLoopTree();
    LoopTree::Loop* loop = loop_tree->outer_loops()[0];
    LoopPeeler peeler(graph(), common(), loop_tree, zone(), source_positions(),
                      node_origins());
    EXPECT_TRUE(peeler.CanPeel(loop));
    return Peel(peeler, loop);
  }

  PeeledIteration* Peel(LoopPeeler peeler, LoopTree::Loop* loop) {
    EXPECT_TRUE(peeler.CanPeel(loop));
    PeeledIteration* peeled = peeler.Peel(loop);
    if (v8_flags.trace_turbo_graph) {
      StdoutStream{} << AsRPO(*graph());
    }
    return peeled;
  }

  Node* InsertReturn(Node* val, Node* effect, Node* control) {
    Node* zero = graph()->NewNode(common()->Int32Constant(0));
    Node* r = graph()->NewNode(common()->Return(), zero, val, effect, control);
    graph()->SetEnd(r);
    return r;
  }

  Node* ExpectPeeled(Node* node, PeeledIteration* iter) {
    Node* p = iter->map(node);
    EXPECT_NE(node, p);
    return p;
  }

  void ExpectNotPeeled(Node* node, PeeledIteration* iter) {
    EXPECT_EQ(node, iter->map(node));
  }

  While NewWhile(Node* cond, Node* control = nullptr) {
    if (control == nullptr) control = start();
    While w;
    w.loop = graph()->NewNode(common()->Loop(2), control, control);
    w.branch = graph()->NewNode(common()->Branch(), cond, w.loop);
    w.if_true = graph()->NewNode(common()->IfTrue(), w.branch);
    w.if_false = graph()->NewNode(common()->IfFalse(), w.branch);
    w.exit = graph()->NewNode(common()->LoopExit(), w.if_false, w.loop);
    w.loop->ReplaceInput(1, w.if_true);
    return w;
  }

  void Chain(While* a, Node* control) { a->loop->ReplaceInput(0, control); }
  void Nest(While* a, While* b) {
    b->loop->ReplaceInput(1, a->exit);
    a->loop->ReplaceInput(0, b->if_true);
  }
  Node* NewPhi(While* w, Node* a, Node* b) {
    return graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2), a,
                            b, w->loop);
  }

  Branch NewBranch(Node* cond, Node* control = nullptr) {
    Branch b;
    if (control == nullptr) control = start();
    b.branch = graph()->NewNode(common()->Branch(), cond, control);
    b.if_true = graph()->NewNode(common()->IfTrue(), b.branch);
    b.if_false = graph()->NewNode(common()->IfFalse(), b.branch);
    return b;
  }

  Counter NewCounter(While* w, int32_t b, int32_t k) {
    Counter c;
    c.base = Int32Constant(b);
    c.inc = Int32Constant(k);
    c.phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                             c.base, c.base, w->loop);
    c.add = graph()->NewNode(machine()->Int32Add(), c.phi, c.inc);
    c.phi->ReplaceInput(1, c.add);
    c.exit_marker = graph()->NewNode(
        common()->LoopExitValue(MachineRepresentation::kTagged), c.phi,
        w->exit);
    return c;
  }
};


TEST_F(LoopPeelingTest, SimpleLoop) {
  Node* p0 = Parameter(0);
  While w = NewWhile(p0);
  Node* r = InsertReturn(p0, start(), w.exit);

  PeeledIteration* peeled = PeelOne();

  Node* br1 = ExpectPeeled(w.branch, peeled);
  Node* if_true1 = ExpectPeeled(w.if_true, peeled);
  Node* if_false1 = ExpectPeeled(w.if_false, peeled);

  EXPECT_THAT(br1, IsBranch(p0, start()));
  EXPECT_THAT(if_true1, IsIfTrue(br1));
  EXPECT_THAT(if_false1, IsIfFalse(br1));

  EXPECT_THAT(w.loop, IsLoop(if_true1, w.if_true));
  EXPECT_THAT(r, IsReturn(p0, start(), IsMerge(w.if_false, if_false1)));
}


TEST_F(LoopPeelingTest, SimpleLoopWithCounter) {
  Node* p0 = Parameter(0);
  While w = NewWhile(p0);
  Counter c = NewCounter(&w, 0, 1);
  Node* r = InsertReturn(c.exit_marker, start(), w.exit);

  PeeledIteration* peeled = PeelOne();

  Node* br1 = ExpectPeeled(w.branch, peeled);
  Node* if_true1 = ExpectPeeled(w.if_true, peeled);
  Node* if_false1 = ExpectPeeled(w.if_false, peeled);

  EXPECT_THAT(br1, IsBranch(p0, start()));
  EXPECT_THAT(if_true1, IsIfTrue(br1));
  EXPECT_THAT(if_false1, IsIfFalse(br1));
  EXPECT_THAT(w.loop, IsLoop(if_true1, w.if_true));

  EXPECT_THAT(peeled->map(c.add), IsInt32Add(c.base, c.inc));

  EXPECT_THAT(w.exit, IsMerge(w.if_false, if_false1));
  EXPECT_THAT(
      r, IsReturn(IsPhi(MachineRepresentation::kTagged, c.phi, c.base, w.exit),
                  start(), w.exit));
}


TEST_F(LoopPeelingTest, SimpleNestedLoopWithCounter_peel_outer) {
  Node* p0 = Parameter(0);
  While outer = NewWhile(p0);
  While inner = NewWhile(p0);
  Nest(&inner, &outer);

  Counter c = NewCounter(&outer, 0, 1);
  Node* r = InsertReturn(c.exit_marker, start(), outer.exit);

  PeeledIteration* peeled = PeelOne();

  Node* bro = ExpectPeeled(outer.branch, peeled);
  Node* if_trueo = ExpectPeeled(outer.if_true, peeled);
  Node* if_falseo = ExpectPeeled(outer.if_false, peeled);

  EXPECT_THAT(bro, IsBranch(p0, start()));
  EXPECT_THAT(if_trueo, IsIfTrue(bro));
  EXPECT_THAT(if_falseo, IsIfFalse(bro));

  Node* bri = ExpectPeeled(inner.branch, peeled);
  Node* if_truei = ExpectPeeled(inner.if_true, peeled);
  Node* if_falsei = ExpectPeeled(inner.if_false, peeled);
  Node* exiti = ExpectPeeled(inner.exit, peeled);

  EXPECT_THAT(bri, IsBranch(p0, ExpectPeeled(inner.loop, peeled)));
  EXPECT_THAT(if_truei, IsIfTrue(bri));
  EXPECT_THAT(if_falsei, IsIfFalse(bri));

  EXPECT_THAT(outer.loop, IsLoop(exiti, inner.exit));
  EXPECT_THAT(peeled->map(c.add), IsInt32Add(c.base, c.inc));

  Capture<Node*> merge;
  EXPECT_THAT(outer.exit, IsMerge(outer.if_false, if_falseo));
  EXPECT_THAT(r, IsReturn(IsPhi(MachineRepresentation::kTagged, c.phi, c.base,
                                outer.exit),
                          start(), outer.exit));
}


TEST_F(LoopPeelingTest, SimpleNestedLoopWithCounter_peel_inner) {
  Node* p0 = Parameter(0);
  While outer = NewWhile(p0);
  While inner = NewWhile(p0);
  Nest(&inner, &outer);

  Counter c = NewCounter(&outer, 0, 1);
  Node* r = InsertReturn(c.exit_marker, start(), outer.exit);

  LoopTree* loop_tree = GetLoopTree();
  LoopTree::Loop* loop = loop_tree->ContainingLoop(inner.loop);
  EXPECT_NE(nullptr, loop);
  EXPECT_EQ(1u, loop->depth());

  LoopPeeler peeler(graph(), common(), loop_tree, zone(), source_positions(),
                    node_origins());
  PeeledIteration* peeled = Peel(peeler, loop);

  ExpectNotPeeled(outer.loop, peeled);
  ExpectNotPeeled(outer.branch, peeled);
  ExpectNotPeeled(outer.if_true, peeled);
  ExpectNotPeeled(outer.if_false, peeled);
  ExpectNotPeeled(outer.exit, peeled);

  Node* bri = ExpectPeeled(inner.branch, peeled);
  Node* if_truei = ExpectPeeled(inner.if_true, peeled);
  Node* if_falsei = ExpectPeeled(inner.if_false, peeled);

  EXPECT_THAT(bri, IsBranch(p0, ExpectPeeled(inner.loop, peeled)));
  EXPECT_THAT(if_truei, IsIfTrue(bri));
  EXPECT_THAT(if_falsei, IsIfFalse(bri));

  EXPECT_THAT(inner.exit, IsMerge(inner.if_false, if_falsei));
  EXPECT_THAT(outer.loop, IsLoop(start(), inner.exit));
  ExpectNotPeeled(c.add, peeled);

  EXPECT_THAT(r, IsReturn(c.exit_marker, start(), outer.exit));
}


TEST_F(LoopPeelingTest, SimpleInnerCounter_peel_inner) {
  Node* p0 = Parameter(0);
  While outer = NewWhile(p0);
  While inner = NewWhile(p0);
  Nest(&inner, &outer);
  Counter c = NewCounter(&inner, 0, 1);
  Node* phi = NewPhi(&outer, Int32Constant(11), c.exit_marker);

  Node* r = InsertReturn(phi, start(), outer.exit);

  LoopTree* loop_tree = GetLoopTree();
  LoopTree::Loop* loop = loop_tree->ContainingLoop(inner.loop);
  EXPECT_NE(nullptr, loop);
  EXPECT_EQ(1u, loop->depth());

  LoopPeeler peeler(graph(), common(), loop_tree, zone(), source_positions(),
                    node_origins());
  PeeledIteration* peeled = Peel(peeler, loop);

  ExpectNotPeeled(outer.loop, peeled);
  ExpectNotPeeled(outer.branch, peeled);
  ExpectNotPeeled(outer.if_true, peeled);
  ExpectNotPeeled(outer.if_false, peeled);
  ExpectNotPeeled(outer.exit, peeled);

  Node* bri = ExpectPeeled(inner.branch, peeled);
  Node* if_truei = ExpectPeeled(inner.if_true, peeled);
  Node* if_falsei = ExpectPeeled(inner.if_false, peeled);

  EXPECT_THAT(bri, IsBranch(p0, ExpectPeeled(inner.loop, peeled)));
  EXPECT_THAT(if_truei, IsIfTrue(bri));
  EXPECT_THAT(if_falsei, IsIfFalse(bri));

  EXPECT_THAT(inner.exit, IsMerge(inner.if_false, if_falsei));
  EXPECT_THAT(outer.loop, IsLoop(start(), inner.exit));
  EXPECT_THAT(peeled->map(c.add), IsInt32Add(c.base, c.inc));

  EXPECT_THAT(c.exit_marker,
              IsPhi(MachineRepresentation::kTagged, c.phi, c.base, inner.exit));

  EXPECT_THAT(phi, IsPhi(MachineRepresentation::kTagged, IsInt32Constant(11),
                         c.exit_marker, outer.loop));

  EXPECT_THAT(r, IsReturn(phi, start(), outer.exit));
}


TEST_F(LoopPeelingTest, TwoBackedgeLoop) {
  Node* p0 = Parameter(0);
  Node* loop = graph()->NewNode(common()->Loop(3), start(), start(), start());
  Branch b1 = NewBranch(p0, loop);
  Branch b2 = NewBranch(p0, b1.if_true);

  loop->ReplaceInput(1, b2.if_true);
  loop->ReplaceInput(2, b2.if_false);

  Node* exit = graph()->NewNode(common()->LoopExit(), b1.if_false, loop);

  Node* r = InsertReturn(p0, start(), exit);

  PeeledIteration* peeled = PeelOne();

  Node* b1b = ExpectPeeled(b1.branch, peeled);
  Node* b1t = ExpectPeeled(b1.if_true, peeled);
  Node* b1f = ExpectPeeled(b1.if_false, peeled);

  EXPECT_THAT(b1b, IsBranch(p0, start()));
  EXPECT_THAT(ExpectPeeled(b1.if_true, peeled), IsIfTrue(b1b));
  EXPECT_THAT(b1f, IsIfFalse(b1b));

  Node* b2b = ExpectPeeled(b2.branch, peeled);
  Node* b2t = ExpectPeeled(b2.if_true, peeled);
  Node* b2f = ExpectPeeled(b2.if_false, peeled);

  EXPECT_THAT(b2b, IsBranch(p0, b1t));
  EXPECT_THAT(b2t, IsIfTrue(b2b));
  EXPECT_THAT(b2f, IsIfFalse(b2b));

  EXPECT_THAT(loop, IsLoop(IsMerge(b2t, b2f), b2.if_true, b2.if_false));
  EXPECT_THAT(exit, IsMerge(b1.if_false, b1f));
  EXPECT_THAT(r, IsReturn(p0, start(), exit));
}


TEST_F(LoopPeelingTest, TwoBackedgeLoopWithPhi) {
  Node* p0 = Parameter(0);
  Node* loop = graph()->NewNode(common()->Loop(3), start(), start(), start());
  Branch b1 = NewBranch(p0, loop);
  Branch b2 = NewBranch(p0, b1.if_true);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 3),
                               Int32Constant(0), Int32Constant(1),
                               Int32Constant(2), loop);

  loop->ReplaceInput(1, b2.if_true);
  loop->ReplaceInput(2, b2.if_false);

  Node* exit = graph()->NewNode(common()->LoopExit(), b1.if_false, loop);
  Node* exit_marker = graph()->NewNode(
      common()->LoopExitValue(MachineRepresentation::kTagged), phi, exit);
  Node* r = InsertReturn(exit_marker, start(), exit);

  PeeledIteration* peeled = PeelOne();

  Node* b1b = ExpectPeeled(b1.branch, peeled);
  Node* b1t = ExpectPeeled(b1.if_true, peeled);
  Node* b1f = ExpectPeeled(b1.if_false, peeled);

  EXPECT_THAT(b1b, IsBranch(p0, start()));
  EXPECT_THAT(ExpectPeeled(b1.if_true, peeled), IsIfTrue(b1b));
  EXPECT_THAT(b1f, IsIfFalse(b1b));

  Node* b2b = ExpectPeeled(b2.branch, peeled);
  Node* b2t = ExpectPeeled(b2.if_true, peeled);
  Node* b2f = ExpectPeeled(b2.if_false, peeled);

  EXPECT_THAT(b2b, IsBranch(p0, b1t));
  EXPECT_THAT(b2t, IsIfTrue(b2b));
  EXPECT_THAT(b2f, IsIfFalse(b2b));

  EXPECT_THAT(loop, IsLoop(IsMerge(b2t, b2f), b2.if_true, b2.if_false));

  EXPECT_THAT(phi,
              IsPhi(MachineRepresentation::kTagged,
                    IsPhi(MachineRepresentation::kTagged, IsInt32Constant(1),
                          IsInt32Constant(2), IsMerge(b2t, b2f)),
                    IsInt32Constant(1), IsInt32Constant(2), loop));

  EXPECT_THAT(exit, IsMerge(b1.if_false, b1f));
  EXPECT_THAT(exit_marker, IsPhi(MachineRepresentation::kTagged, phi,
                                 IsInt32Constant(0), exit));
  EXPECT_THAT(r, IsReturn(exit_marker, start(), exit));
}


TEST_F(LoopPeelingTest, TwoBackedgeLoopWithCounter) {
  Node* p0 = Parameter(0);
  Node* loop = graph()->NewNode(common()->Loop(3), start(), start(), start());
  Branch b1 = NewBranch(p0, loop);
  Branch b2 = NewBranch(p0, b1.if_true);
  Node* phi = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 3),
                               Int32Constant(0), Int32Constant(1),
                               Int32Constant(2), loop);

  phi->ReplaceInput(
      1, graph()->NewNode(machine()->Int32Add(), phi, Int32Constant(1)));
  phi->ReplaceInput(
      2, graph()->NewNode(machine()->Int32Add(), phi, Int32Constant(2)));

  loop->ReplaceInput(1, b2.if_true);
  loop->ReplaceInput(2, b2.if_false);

  Node* exit = graph()->NewNode(common()->LoopExit(), b1.if_false, loop);
  Node* exit_marker = graph()->NewNode(
      common()->LoopExitValue(MachineRepresentation::kTagged), phi, exit);
  Node* r = InsertReturn(exit_marker, start(), exit);

  PeeledIteration* peeled = PeelOne();

  Node* b1b = ExpectPeeled(b1.branch, peeled);
  Node* b1t = ExpectPeeled(b1.if_true, peeled);
  Node* b1f = ExpectPeeled(b1.if_false, peeled);

  EXPECT_THAT(b1b, IsBranch(p0, start()));
  EXPECT_THAT(ExpectPeeled(b1.if_true, peeled), IsIfTrue(b1b));
  EXPECT_THAT(b1f, IsIfFalse(b1b));

  Node* b2b = ExpectPeeled(b2.branch, peeled);
  Node* b2t = ExpectPeeled(b2.if_true, peeled);
  Node* b2f = ExpectPeeled(b2.if_false, peeled);

  EXPECT_THAT(b2b, IsBranch(p0, b1t));
  EXPECT_THAT(b2t, IsIfTrue(b2b));
  EXPECT_THAT(b2f, IsIfFalse(b2b));

  Capture<Node*> entry;
  EXPECT_THAT(loop, IsLoop(AllOf(CaptureEq(&entry), IsMerge(b2t, b2f)),
                           b2.if_true, b2.if_false));

  Node* eval = phi->InputAt(0);

  EXPECT_THAT(eval, IsPhi(MachineRepresentation::kTagged,
                          IsInt32Add(IsInt32Constant(0), IsInt32Constant(1)),
                          IsInt32Add(IsInt32Constant(0), IsInt32Constant(2)),
                          CaptureEq(&entry)));

  EXPECT_THAT(phi, IsPhi(MachineRepresentation::kTagged, eval,
                         IsInt32Add(phi, IsInt32Constant(1)),
                         IsInt32Add(phi, IsInt32Constant(2)), loop));

  EXPECT_THAT(exit, IsMerge(b1.if_false, b1f));
  EXPECT_THAT(exit_marker, IsPhi(MachineRepresentation::kTagged, phi,
                                 IsInt32Constant(0), exit));
  EXPECT_THAT(r, IsReturn(exit_marker, start(), exit));
}

TEST_F(LoopPeelingTest, TwoExitLoop) {
  Node* p0 = Parameter(0);
  Node* loop = graph()->NewNode(common()->Loop(2), start(), start());
  Branch b1 = NewBranch(p0, loop);
  Branch b2 = NewBranch(p0, b1.if_true);

  loop->ReplaceInput(1, b2.if_true);

  Node* exit1 = graph()->NewNode(common()->LoopExit(), b1.if_false, loop);
  Node* exit2 = graph()->NewNode(common()->LoopExit(), b2.if_false, loop);

  Node* merge = graph()->NewNode(common()->Merge(2), exit1, exit2);
  Node* r = InsertReturn(p0, start(), merge);

  PeeledIteration* peeled = PeelOne();

  Node* b1p = ExpectPeeled(b1.branch, peeled);
  Node* if_true1p = ExpectPeeled(b1.if_true, peeled);
  Node* if_false1p = ExpectPeeled(b1.if_false, peeled);

  Node* b2p = ExpectPeeled(b2.branch, peeled);
  Node* if_true2p = ExpectPeeled(b2.if_true, peeled);
  Node* if_false2p = ExpectPeeled(b2.if_false, peeled);

  EXPECT_THAT(b1p, IsBranch(p0, start()));
  EXPECT_THAT(if_true1p, IsIfTrue(b1p));
  EXPECT_THAT(if_false1p, IsIfFalse(b1p));

  EXPECT_THAT(b2p, IsBranch(p0, if_true1p));
  EXPECT_THAT(if_true2p, IsIfTrue(b2p));
  EXPECT_THAT(if_false2p, IsIfFalse(b2p));

  EXPECT_THAT(exit1, IsMerge(b1.if_false, if_false1p));
  EXPECT_THAT(exit2, IsMerge(b2.if_false, if_false2p));

  EXPECT_THAT(loop, IsLoop(if_true2p, b2.if_true));

  EXPECT_THAT(merge, IsMerge(exit1, exit2));
  EXPECT_THAT(r, IsReturn(p0, start(), merge));
}

TEST_F(LoopPeelingTest, SimpleLoopWithUnmarkedExit) {
  Node* p0 = Parameter(0);
  Node* loop = graph()->NewNode(common()->Loop(2), start(), start());
  Branch b = NewBranch(p0, loop);
  loop->ReplaceInput(1, b.if_true);

  InsertReturn(p0, start(), b.if_false);

  {
    LoopTree* loop_tree = GetLoopTree();
    LoopTree::Loop* outer_loop = loop_tree->outer_loops()[0];
    LoopPeeler peeler(graph(), common(), loop_tree, zone(), source_positions(),
                      node_origins());
    EXPECT_FALSE(peeler.CanPeel(outer_loop));
  }
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
