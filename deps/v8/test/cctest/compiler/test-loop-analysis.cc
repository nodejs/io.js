// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/tick-counter.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/loop-analysis.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/verifier.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {

static Operator kIntAdd(IrOpcode::kInt32Add, Operator::kPure, "Int32Add", 2, 0,
                        0, 1, 0, 0);
static Operator kIntLt(IrOpcode::kInt32LessThan, Operator::kPure,
                       "Int32LessThan", 2, 0, 0, 1, 0, 0);
static Operator kStore(IrOpcode::kStore, Operator::kNoProperties, "Store", 1, 1,
                       1, 0, 1, 0);

static const int kNumLeafs = 4;

// A helper for all tests dealing with LoopFinder.
class LoopFinderTester : HandleAndZoneScope {
 public:
  LoopFinderTester()
      : HandleAndZoneScope(kCompressGraphZone),
        isolate(main_isolate()),
        common(main_zone()),
        graph(main_zone()),
        jsgraph(main_isolate(), &graph, &common, nullptr, nullptr, nullptr),
        start(graph.NewNode(common.Start(1))),
        end(graph.NewNode(common.End(1), start)),
        p0(graph.NewNode(common.Parameter(0), start)),
        zero(jsgraph.Int32Constant(0)),
        one(jsgraph.OneConstant()),
        half(jsgraph.ConstantNoHole(0.5)),
        self(graph.NewNode(common.Int32Constant(0xAABBCCDD))),
        dead(graph.NewNode(common.Dead())),
        loop_tree(nullptr) {
    graph.SetEnd(end);
    graph.SetStart(start);
    leaf[0] = zero;
    leaf[1] = one;
    leaf[2] = half;
    leaf[3] = p0;
  }

  Isolate* isolate;
  TickCounter tick_counter;
  CommonOperatorBuilder common;
  Graph graph;
  JSGraph jsgraph;
  Node* start;
  Node* end;
  Node* p0;
  Node* zero;
  Node* one;
  Node* half;
  Node* self;
  Node* dead;
  Node* leaf[kNumLeafs];
  LoopTree* loop_tree;

  Node* Phi(Node* a) {
    return SetSelfReferences(graph.NewNode(op(1, false), a, start));
  }

  Node* Phi(Node* a, Node* b) {
    return SetSelfReferences(graph.NewNode(op(2, false), a, b, start));
  }

  Node* Phi(Node* a, Node* b, Node* c) {
    return SetSelfReferences(graph.NewNode(op(3, false), a, b, c, start));
  }

  Node* Phi(Node* a, Node* b, Node* c, Node* d) {
    return SetSelfReferences(graph.NewNode(op(4, false), a, b, c, d, start));
  }

  Node* EffectPhi(Node* a) {
    return SetSelfReferences(graph.NewNode(op(1, true), a, start));
  }

  Node* EffectPhi(Node* a, Node* b) {
    return SetSelfReferences(graph.NewNode(op(2, true), a, b, start));
  }

  Node* EffectPhi(Node* a, Node* b, Node* c) {
    return SetSelfReferences(graph.NewNode(op(3, true), a, b, c, start));
  }

  Node* EffectPhi(Node* a, Node* b, Node* c, Node* d) {
    return SetSelfReferences(graph.NewNode(op(4, true), a, b, c, d, start));
  }

  Node* SetSelfReferences(Node* node) {
    for (Edge edge : node->input_edges()) {
      if (edge.to() == self) node->ReplaceInput(edge.index(), node);
    }
    return node;
  }

  const Operator* op(int count, bool effect) {
    return effect ? common.EffectPhi(count)
                  : common.Phi(MachineRepresentation::kTagged, count);
  }

  Node* Return(Node* val, Node* effect, Node* control) {
    Node* ret = graph.NewNode(common.Return(), zero, val, effect, control);
    end->ReplaceInput(0, ret);
    return ret;
  }

  LoopTree* GetLoopTree() {
    if (loop_tree == nullptr) {
      if (v8_flags.trace_turbo_graph) {
        StdoutStream{} << AsRPO(graph);
      }
      Zone zone(main_isolate()->allocator(), ZONE_NAME);
      loop_tree = LoopFinder::BuildLoopTree(&graph, &tick_counter, &zone);
    }
    return loop_tree;
  }

  void CheckLoop(Node** header, int header_count, Node** body, int body_count) {
    LoopTree* tree = GetLoopTree();
    LoopTree::Loop* loop = tree->ContainingLoop(header[0]);
    CHECK(loop);

    CHECK(header_count == static_cast<int>(loop->HeaderSize()));
    for (int i = 0; i < header_count; i++) {
      // Each header node should be in the loop.
      CHECK_EQ(loop, tree->ContainingLoop(header[i]));
      CheckRangeContains(tree->HeaderNodes(loop), header[i]);
    }

    CHECK_EQ(body_count, static_cast<int>(loop->BodySize()));
    // TODO(turbofan): O(n^2) set equivalence in this test.
    for (int i = 0; i < body_count; i++) {
      // Each body node should be contained in the loop.
      CHECK(tree->Contains(loop, body[i]));
      CheckRangeContains(tree->BodyNodes(loop), body[i]);
    }
  }

  void CheckRangeContains(NodeRange range, Node* node) {
    CHECK_NE(range.end(), std::find(range.begin(), range.end(), node));
  }

  void CheckNestedLoops(Node** chain, int chain_count) {
    LoopTree* tree = GetLoopTree();
    for (int i = 0; i < chain_count; i++) {
      Node* header = chain[i];
      // Each header should be in a loop.
      LoopTree::Loop* loop = tree->ContainingLoop(header);
      CHECK(loop);
      // Check parentage.
      LoopTree::Loop* parent =
          i == 0 ? nullptr : tree->ContainingLoop(chain[i - 1]);
      CHECK_EQ(parent, loop->parent());
      for (int j = i - 1; j >= 0; j--) {
        // This loop should be nested inside all the outer loops.
        Node* outer_header = chain[j];
        LoopTree::Loop* outer = tree->ContainingLoop(outer_header);
        CHECK(tree->Contains(outer, header));
        CHECK(!tree->Contains(loop, outer_header));
      }
    }
  }

  Zone* zone() { return main_zone(); }
};


struct While {
  LoopFinderTester& t;
  Node* branch;
  Node* if_true;
  Node* exit;
  Node* loop;

  While(LoopFinderTester& R, Node* cond) : t(R) {
    loop = t.graph.NewNode(t.common.Loop(2), t.start, t.start);
    branch = t.graph.NewNode(t.common.Branch(), cond, loop);
    if_true = t.graph.NewNode(t.common.IfTrue(), branch);
    exit = t.graph.NewNode(t.common.IfFalse(), branch);
    loop->ReplaceInput(1, if_true);
  }

  void chain(Node* control) { loop->ReplaceInput(0, control); }
  void nest(While* that) {
    that->loop->ReplaceInput(1, exit);
    this->loop->ReplaceInput(0, that->if_true);
  }
};


struct Counter {
  Node* base;
  Node* inc;
  Node* phi;
  Node* add;

  Counter(While* w, int32_t b, int32_t k)
      : base(w->t.jsgraph.Int32Constant(b)),
        inc(w->t.jsgraph.Int32Constant(k)) {
    Build(w);
  }

  Counter(While* w, Node* b, Node* k) : base(b), inc(k) { Build(w); }

  void Build(While* w) {
    phi = w->t.graph.NewNode(w->t.op(2, false), base, base, w->loop);
    add = w->t.graph.NewNode(&kIntAdd, phi, inc);
    phi->ReplaceInput(1, add);
  }
};


struct StoreLoop {
  Node* base;
  Node* val;
  Node* phi;
  Node* store;

  explicit StoreLoop(While* w)
      : base(w->t.graph.start()), val(w->t.jsgraph.Int32Constant(13)) {
    Build(w);
  }

  StoreLoop(While* w, Node* b, Node* v) : base(b), val(v) { Build(w); }

  void Build(While* w) {
    phi = w->t.graph.NewNode(w->t.op(2, true), base, base, w->loop);
    store = w->t.graph.NewNode(&kStore, val, phi, w->loop);
    phi->ReplaceInput(1, store);
  }
};


TEST(LaLoop1) {
  // One loop.
  LoopFinderTester t;
  While w(t, t.p0);
  t.Return(t.p0, t.start, w.exit);

  Node* chain[] = {w.loop};
  t.CheckNestedLoops(chain, 1);

  Node* header[] = {w.loop};
  Node* body[] = {w.branch, w.if_true};
  t.CheckLoop(header, 1, body, 2);
}


TEST(LaLoop1phi) {
  // One loop with a simple phi.
  LoopFinderTester t;
  While w(t, t.p0);
  Node* phi = t.graph.NewNode(t.common.Phi(MachineRepresentation::kTagged, 2),
                              t.zero, t.one, w.loop);
  t.Return(phi, t.start, w.exit);

  Node* chain[] = {w.loop};
  t.CheckNestedLoops(chain, 1);

  Node* header[] = {w.loop, phi};
  Node* body[] = {w.branch, w.if_true};
  t.CheckLoop(header, 2, body, 2);
}


TEST(LaLoop1c) {
  // One loop with a counter.
  LoopFinderTester t;
  While w(t, t.p0);
  Counter c(&w, 0, 1);
  t.Return(c.phi, t.start, w.exit);

  Node* chain[] = {w.loop};
  t.CheckNestedLoops(chain, 1);

  Node* header[] = {w.loop, c.phi};
  Node* body[] = {w.branch, w.if_true, c.add};
  t.CheckLoop(header, 2, body, 3);
}


TEST(LaLoop1e) {
  // One loop with an effect phi.
  LoopFinderTester t;
  While w(t, t.p0);
  StoreLoop c(&w);
  t.Return(t.p0, c.phi, w.exit);

  Node* chain[] = {w.loop};
  t.CheckNestedLoops(chain, 1);

  Node* header[] = {w.loop, c.phi};
  Node* body[] = {w.branch, w.if_true, c.store};
  t.CheckLoop(header, 2, body, 3);
}


TEST(LaLoop1d) {
  // One loop with two counters.
  LoopFinderTester t;
  While w(t, t.p0);
  Counter c1(&w, 0, 1);
  Counter c2(&w, 1, 1);
  t.Return(t.graph.NewNode(&kIntAdd, c1.phi, c2.phi), t.start, w.exit);

  Node* chain[] = {w.loop};
  t.CheckNestedLoops(chain, 1);

  Node* header[] = {w.loop, c1.phi, c2.phi};
  Node* body[] = {w.branch, w.if_true, c1.add, c2.add};
  t.CheckLoop(header, 3, body, 4);
}


TEST(LaLoop2) {
  // One loop following another.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  w2.chain(w1.exit);
  t.Return(t.p0, t.start, w2.exit);

  {
    Node* chain[] = {w1.loop};
    t.CheckNestedLoops(chain, 1);

    Node* header[] = {w1.loop};
    Node* body[] = {w1.branch, w1.if_true};
    t.CheckLoop(header, 1, body, 2);
  }

  {
    Node* chain[] = {w2.loop};
    t.CheckNestedLoops(chain, 1);

    Node* header[] = {w2.loop};
    Node* body[] = {w2.branch, w2.if_true};
    t.CheckLoop(header, 1, body, 2);
  }
}


TEST(LaLoop2c) {
  // One loop following another, each with counters.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  Counter c1(&w1, 0, 1);
  Counter c2(&w2, 0, 1);
  w2.chain(w1.exit);
  t.Return(t.graph.NewNode(&kIntAdd, c1.phi, c2.phi), t.start, w2.exit);

  {
    Node* chain[] = {w1.loop};
    t.CheckNestedLoops(chain, 1);

    Node* header[] = {w1.loop, c1.phi};
    Node* body[] = {w1.branch, w1.if_true, c1.add};
    t.CheckLoop(header, 2, body, 3);
  }

  {
    Node* chain[] = {w2.loop};
    t.CheckNestedLoops(chain, 1);

    Node* header[] = {w2.loop, c2.phi};
    Node* body[] = {w2.branch, w2.if_true, c2.add};
    t.CheckLoop(header, 2, body, 3);
  }
}


TEST(LaLoop2cc) {
  // One loop following another; second loop uses phi from first.
  for (int i = 0; i < 8; i++) {
    LoopFinderTester t;
    While w1(t, t.p0);
    While w2(t, t.p0);
    Counter c1(&w1, 0, 1);

    // various usage scenarios for the second loop.
    Counter c2(&w2, i & 1 ? t.p0 : c1.phi, i & 2 ? t.p0 : c1.phi);
    if (i & 3) w2.branch->ReplaceInput(0, c1.phi);

    w2.chain(w1.exit);
    t.Return(t.graph.NewNode(&kIntAdd, c1.phi, c2.phi), t.start, w2.exit);

    {
      Node* chain[] = {w1.loop};
      t.CheckNestedLoops(chain, 1);

      Node* header[] = {w1.loop, c1.phi};
      Node* body[] = {w1.branch, w1.if_true, c1.add};
      t.CheckLoop(header, 2, body, 3);
    }

    {
      Node* chain[] = {w2.loop};
      t.CheckNestedLoops(chain, 1);

      Node* header[] = {w2.loop, c2.phi};
      Node* body[] = {w2.branch, w2.if_true, c2.add};
      t.CheckLoop(header, 2, body, 3);
    }
  }
}


TEST(LaNestedLoop1) {
  // One loop nested in another.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  w2.nest(&w1);
  t.Return(t.p0, t.start, w1.exit);

  Node* chain[] = {w1.loop, w2.loop};
  t.CheckNestedLoops(chain, 2);

  Node* h1[] = {w1.loop};
  Node* b1[] = {w1.branch, w1.if_true, w2.loop, w2.branch, w2.if_true, w2.exit};
  t.CheckLoop(h1, 1, b1, 6);

  Node* h2[] = {w2.loop};
  Node* b2[] = {w2.branch, w2.if_true};
  t.CheckLoop(h2, 1, b2, 2);
}


TEST(LaNestedLoop1c) {
  // One loop nested in another, each with a counter.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  Counter c1(&w1, 0, 1);
  Counter c2(&w2, 0, 1);
  w2.branch->ReplaceInput(0, c2.phi);
  w2.nest(&w1);
  t.Return(c1.phi, t.start, w1.exit);

  Node* chain[] = {w1.loop, w2.loop};
  t.CheckNestedLoops(chain, 2);

  Node* h1[] = {w1.loop, c1.phi};
  Node* b1[] = {w1.branch, w1.if_true, w2.loop, w2.branch, w2.if_true,
                w2.exit,   c2.phi,     c1.add,  c2.add};
  t.CheckLoop(h1, 2, b1, 9);

  Node* h2[] = {w2.loop, c2.phi};
  Node* b2[] = {w2.branch, w2.if_true, c2.add};
  t.CheckLoop(h2, 2, b2, 3);
}


TEST(LaNestedLoop1x) {
  // One loop nested in another.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  w2.nest(&w1);

  const Operator* op = t.common.Phi(MachineRepresentation::kWord32, 2);
  Node* p1a = t.graph.NewNode(op, t.p0, t.p0, w1.loop);
  Node* p1b = t.graph.NewNode(op, t.p0, t.p0, w1.loop);
  Node* p2a = t.graph.NewNode(op, p1a, t.p0, w2.loop);
  Node* p2b = t.graph.NewNode(op, p1b, t.p0, w2.loop);

  p1a->ReplaceInput(1, p2b);
  p1b->ReplaceInput(1, p2a);

  p2a->ReplaceInput(1, p2b);
  p2b->ReplaceInput(1, p2a);

  t.Return(t.p0, t.start, w1.exit);

  Node* chain[] = {w1.loop, w2.loop};
  t.CheckNestedLoops(chain, 2);

  Node* h1[] = {w1.loop, p1a, p1b};
  Node* b1[] = {w1.branch, w1.if_true, w2.loop,    p2a,
                p2b,       w2.branch,  w2.if_true, w2.exit};
  t.CheckLoop(h1, 3, b1, 8);

  Node* h2[] = {w2.loop, p2a, p2b};
  Node* b2[] = {w2.branch, w2.if_true};
  t.CheckLoop(h2, 3, b2, 2);
}


TEST(LaNestedLoop2) {
  // Two loops nested in an outer loop.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  While w3(t, t.p0);
  w2.nest(&w1);
  w3.nest(&w1);
  w3.chain(w2.exit);
  t.Return(t.p0, t.start, w1.exit);

  Node* chain1[] = {w1.loop, w2.loop};
  t.CheckNestedLoops(chain1, 2);

  Node* chain2[] = {w1.loop, w3.loop};
  t.CheckNestedLoops(chain2, 2);

  Node* h1[] = {w1.loop};
  Node* b1[] = {w1.branch, w1.if_true, w2.loop,   w2.branch,  w2.if_true,
                w2.exit,   w3.loop,    w3.branch, w3.if_true, w3.exit};
  t.CheckLoop(h1, 1, b1, 10);

  Node* h2[] = {w2.loop};
  Node* b2[] = {w2.branch, w2.if_true};
  t.CheckLoop(h2, 1, b2, 2);

  Node* h3[] = {w3.loop};
  Node* b3[] = {w3.branch, w3.if_true};
  t.CheckLoop(h3, 1, b3, 2);
}


TEST(LaNestedLoop3) {
  // Three nested loops.
  LoopFinderTester t;
  While w1(t, t.p0);
  While w2(t, t.p0);
  While w3(t, t.p0);
  w2.loop->ReplaceInput(0, w1.if_true);
  w3.loop->ReplaceInput(0, w2.if_true);
  w2.loop->ReplaceInput(1, w3.exit);
  w1.loop->ReplaceInput(1, w2.exit);
  t.Return(t.p0, t.start, w1.exit);

  Node* chain[] = {w1.loop, w2.loop, w3.loop};
  t.CheckNestedLoops(chain, 3);

  Node* h1[] = {w1.loop};
  Node* b1[] = {w1.branch, w1.if_true, w2.loop,   w2.branch,  w2.if_true,
                w2.exit,   w3.loop,    w3.branch, w3.if_true, w3.exit};
  t.CheckLoop(h1, 1, b1, 10);

  Node* h2[] = {w2.loop};
  Node* b2[] = {w2.branch, w2.if_true, w3.loop, w3.branch, w3.if_true, w3.exit};
  t.CheckLoop(h2, 1, b2, 6);

  Node* h3[] = {w3.loop};
  Node* b3[] = {w3.branch, w3.if_true};
  t.CheckLoop(h3, 1, b3, 2);
}


TEST(LaNestedLoop3c) {
  // Three nested loops with counters.
  LoopFinderTester t;
  While w1(t, t.p0);
  Counter c1(&w1, 0, 1);
  While w2(t, t.p0);
  Counter c2(&w2, 0, 1);
  While w3(t, t.p0);
  Counter c3(&w3, 0, 1);
  w2.loop->ReplaceInput(0, w1.if_true);
  w3.loop->ReplaceInput(0, w2.if_true);
  w2.loop->ReplaceInput(1, w3.exit);
  w1.loop->ReplaceInput(1, w2.exit);
  w1.branch->ReplaceInput(0, c1.phi);
  w2.branch->ReplaceInput(0, c2.phi);
  w3.branch->ReplaceInput(0, c3.phi);
  t.Return(c1.phi, t.start, w1.exit);

  Node* chain[] = {w1.loop, w2.loop, w3.loop};
  t.CheckNestedLoops(chain, 3);

  Node* h1[] = {w1.loop, c1.phi};
  Node* b1[] = {w1.branch, w1.if_true, c1.add,    c2.add,     c2.add,
                c2.phi,    c3.phi,     w2.loop,   w2.branch,  w2.if_true,
                w2.exit,   w3.loop,    w3.branch, w3.if_true, w3.exit};
  t.CheckLoop(h1, 2, b1, 15);

  Node* h2[] = {w2.loop, c2.phi};
  Node* b2[] = {w2.branch, w2.if_true, c2.add,     c3.add, c3.phi,
                w3.loop,   w3.branch,  w3.if_true, w3.exit};
  t.CheckLoop(h2, 2, b2, 9);

  Node* h3[] = {w3.loop, c3.phi};
  Node* b3[] = {w3.branch, w3.if_true, c3.add};
  t.CheckLoop(h3, 2, b3, 3);
}


TEST(LaMultipleExit1) {
  const int kMaxExits = 10;
  Node* merge[1 + kMaxExits];
  Node* body[2 * kMaxExits];

  // A single loop with {i} exits.
  for (int i = 1; i < kMaxExits; i++) {
    LoopFinderTester t;
    Node* cond = t.p0;

    int merge_count = 0;
    int body_count = 0;
    Node* loop = t.graph.NewNode(t.common.Loop(2), t.start, t.start);
    Node* last = loop;

    for (int e = 0; e < i; e++) {
      Node* branch = t.graph.NewNode(t.common.Branch(), cond, last);
      Node* if_true = t.graph.NewNode(t.common.IfTrue(), branch);
      Node* exit = t.graph.NewNode(t.common.IfFalse(), branch);
      last = if_true;

      body[body_count++] = branch;
      body[body_count++] = if_true;
      merge[merge_count++] = exit;
    }

    loop->ReplaceInput(1, last);  // form loop backedge.
    Node* end = t.graph.NewNode(t.common.Merge(i), i, merge);  // form exit.
    t.graph.SetEnd(end);

    Node* h[] = {loop};
    t.CheckLoop(h, 1, body, body_count);
  }
}


TEST(LaMultipleBackedge1) {
  const int kMaxBackedges = 10;
  Node* loop_inputs[1 + kMaxBackedges];
  Node* body[3 * kMaxBackedges];

  // A single loop with {i} backedges.
  for (int i = 1; i < kMaxBackedges; i++) {
    LoopFinderTester t;

    for (int j = 0; j <= i; j++) loop_inputs[j] = t.start;
    Node* loop = t.graph.NewNode(t.common.Loop(1 + i), 1 + i, loop_inputs);

    Node* cond = t.p0;
    int body_count = 0;
    Node* exit = loop;

    for (int b = 0; b < i; b++) {
      Node* branch = t.graph.NewNode(t.common.Branch(), cond, exit);
      Node* if_true = t.graph.NewNode(t.common.IfTrue(), branch);
      Node* if_false = t.graph.NewNode(t.common.IfFalse(), branch);
      exit = if_false;

      body[body_count++] = branch;
      body[body_count++] = if_true;
      if (b != (i - 1)) body[body_count++] = if_false;

      loop->ReplaceInput(1 + b, if_true);
    }

    t.graph.SetEnd(exit);

    Node* h[] = {loop};
    t.CheckLoop(h, 1, body, body_count);
  }
}


TEST(LaEdgeMatrix1) {
  // Test various kinds of extra edges added to a simple loop.
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      for (int k = 0; k < 3; k++) {
        LoopFinderTester t;

        Node* p1 = t.jsgraph.Int32Constant(11);
        Node* p2 = t.jsgraph.Int32Constant(22);
        Node* p3 = t.jsgraph.Int32Constant(33);

        Node* loop = t.graph.NewNode(t.common.Loop(2), t.start, t.start);
        Node* phi = t.graph.NewNode(
            t.common.Phi(MachineRepresentation::kWord32, 2), t.one, p1, loop);
        Node* cond = t.graph.NewNode(&kIntAdd, phi, p2);
        Node* branch = t.graph.NewNode(t.common.Branch(), cond, loop);
        Node* if_true = t.graph.NewNode(t.common.IfTrue(), branch);
        Node* exit = t.graph.NewNode(t.common.IfFalse(), branch);
        loop->ReplaceInput(1, if_true);
        Node* zero = t.graph.NewNode(t.common.Int32Constant(0));
        Node* ret = t.graph.NewNode(t.common.Return(), zero, p3, t.start, exit);
        t.graph.SetEnd(ret);

        Node* choices[] = {p1, phi, cond};
        p1->ReplaceUses(choices[i]);
        p2->ReplaceUses(choices[j]);
        p3->ReplaceUses(choices[k]);

        Node* header[] = {loop, phi};
        Node* body[] = {cond, branch, if_true};
        t.CheckLoop(header, 2, body, 3);
      }
    }
  }
}


void RunEdgeMatrix2(int i) {
  CHECK(i >= 0 && i < 5);
  for (int j = 0; j < 5; j++) {
    for (int k = 0; k < 5; k++) {
      LoopFinderTester t;

      Node* p1 = t.jsgraph.Int32Constant(11);
      Node* p2 = t.jsgraph.Int32Constant(22);
      Node* p3 = t.jsgraph.Int32Constant(33);

      // outer loop.
      Node* loop1 = t.graph.NewNode(t.common.Loop(2), t.start, t.start);
      Node* phi1 = t.graph.NewNode(
          t.common.Phi(MachineRepresentation::kWord32, 2), t.one, p1, loop1);
      Node* cond1 = t.graph.NewNode(&kIntAdd, phi1, t.one);
      Node* branch1 = t.graph.NewNode(t.common.Branch(), cond1, loop1);
      Node* if_true1 = t.graph.NewNode(t.common.IfTrue(), branch1);
      Node* exit1 = t.graph.NewNode(t.common.IfFalse(), branch1);

      // inner loop.
      Node* loop2 = t.graph.NewNode(t.common.Loop(2), if_true1, t.start);
      Node* phi2 = t.graph.NewNode(
          t.common.Phi(MachineRepresentation::kWord32, 2), t.one, p2, loop2);
      Node* cond2 = t.graph.NewNode(&kIntAdd, phi2, p3);
      Node* branch2 = t.graph.NewNode(t.common.Branch(), cond2, loop2);
      Node* if_true2 = t.graph.NewNode(t.common.IfTrue(), branch2);
      Node* exit2 = t.graph.NewNode(t.common.IfFalse(), branch2);
      loop2->ReplaceInput(1, if_true2);
      loop1->ReplaceInput(1, exit2);

      Node* zero = t.graph.NewNode(t.common.Int32Constant(0));
      Node* ret =
          t.graph.NewNode(t.common.Return(), zero, phi1, t.start, exit1);
      t.graph.SetEnd(ret);

      Node* choices[] = {p1, phi1, cond1, phi2, cond2};
      p1->ReplaceUses(choices[i]);
      p2->ReplaceUses(choices[j]);
      p3->ReplaceUses(choices[k]);

      Node* header1[] = {loop1, phi1};
      Node* body1[] = {cond1, branch1, if_true1, exit2,   loop2,
                       phi2,  cond2,   branch2,  if_true2};
      t.CheckLoop(header1, 2, body1, 9);

      Node* header2[] = {loop2, phi2};
      Node* body2[] = {cond2, branch2, if_true2};
      t.CheckLoop(header2, 2, body2, 3);

      Node* chain[] = {loop1, loop2};
      t.CheckNestedLoops(chain, 2);
    }
  }
}


TEST(LaEdgeMatrix2_0) { RunEdgeMatrix2(0); }


TEST(LaEdgeMatrix2_1) { RunEdgeMatrix2(1); }


TEST(LaEdgeMatrix2_2) { RunEdgeMatrix2(2); }


TEST(LaEdgeMatrix2_3) { RunEdgeMatrix2(3); }


TEST(LaEdgeMatrix2_4) { RunEdgeMatrix2(4); }


// Generates a triply-nested loop with extra edges between the phis and
// conditions according to the edge choice parameters.
void RunEdgeMatrix3(int c1a, int c1b, int c1c,    // line break
                    int c2a, int c2b, int c2c,    // line break
                    int c3a, int c3b, int c3c) {  // line break
  LoopFinderTester t;

  Node* p1a = t.jsgraph.Int32Constant(11);
  Node* p1b = t.jsgraph.Int32Constant(22);
  Node* p1c = t.jsgraph.Int32Constant(33);
  Node* p2a = t.jsgraph.Int32Constant(44);
  Node* p2b = t.jsgraph.Int32Constant(55);
  Node* p2c = t.jsgraph.Int32Constant(66);
  Node* p3a = t.jsgraph.Int32Constant(77);
  Node* p3b = t.jsgraph.Int32Constant(88);
  Node* p3c = t.jsgraph.Int32Constant(99);

  // L1 depth = 0
  Node* loop1 = t.graph.NewNode(t.common.Loop(2), t.start, t.start);
  Node* phi1 = t.graph.NewNode(t.common.Phi(MachineRepresentation::kWord32, 2),
                               p1a, p1c, loop1);
  Node* cond1 = t.graph.NewNode(&kIntAdd, phi1, p1b);
  Node* branch1 = t.graph.NewNode(t.common.Branch(), cond1, loop1);
  Node* if_true1 = t.graph.NewNode(t.common.IfTrue(), branch1);
  Node* exit1 = t.graph.NewNode(t.common.IfFalse(), branch1);

  // L2 depth = 1
  Node* loop2 = t.graph.NewNode(t.common.Loop(2), if_true1, t.start);
  Node* phi2 = t.graph.NewNode(t.common.Phi(MachineRepresentation::kWord32, 2),
                               p2a, p2c, loop2);
  Node* cond2 = t.graph.NewNode(&kIntAdd, phi2, p2b);
  Node* branch2 = t.graph.NewNode(t.common.Branch(), cond2, loop2);
  Node* if_true2 = t.graph.NewNode(t.common.IfTrue(), branch2);
  Node* exit2 = t.graph.NewNode(t.common.IfFalse(), branch2);

  // L3 depth = 2
  Node* loop3 = t.graph.NewNode(t.common.Loop(2), if_true2, t.start);
  Node* phi3 = t.graph.NewNode(t.common.Phi(MachineRepresentation::kWord32, 2),
                               p3a, p3c, loop3);
  Node* cond3 = t.graph.NewNode(&kIntAdd, phi3, p3b);
  Node* branch3 = t.graph.NewNode(t.common.Branch(), cond3, loop3);
  Node* if_true3 = t.graph.NewNode(t.common.IfTrue(), branch3);
  Node* exit3 = t.graph.NewNode(t.common.IfFalse(), branch3);

  loop3->ReplaceInput(1, if_true3);
  loop2->ReplaceInput(1, exit3);
  loop1->ReplaceInput(1, exit2);

  Node* zero = t.graph.NewNode(t.common.Int32Constant(0));
  Node* ret = t.graph.NewNode(t.common.Return(), zero, phi1, t.start, exit1);
  t.graph.SetEnd(ret);

  // Mutate the graph according to the edge choices.

  Node* o1[] = {t.one};
  Node* o2[] = {t.one, phi1, cond1};
  Node* o3[] = {t.one, phi1, cond1, phi2, cond2};

  p1a->ReplaceUses(o1[c1a]);
  p1b->ReplaceUses(o1[c1b]);

  p2a->ReplaceUses(o2[c2a]);
  p2b->ReplaceUses(o2[c2b]);

  p3a->ReplaceUses(o3[c3a]);
  p3b->ReplaceUses(o3[c3b]);

  Node* l2[] = {phi1, cond1, phi2, cond2};
  Node* l3[] = {phi1, cond1, phi2, cond2, phi3, cond3};

  p1c->ReplaceUses(l2[c1c]);
  p2c->ReplaceUses(l3[c2c]);
  p3c->ReplaceUses(l3[c3c]);

  // Run the tests and verify loop structure.

  Node* chain[] = {loop1, loop2, loop3};
  t.CheckNestedLoops(chain, 3);

  Node* header1[] = {loop1, phi1};
  Node* body1[] = {cond1, branch1, if_true1, exit2,    loop2,
                   phi2,  cond2,   branch2,  if_true2, exit3,
                   loop3, phi3,    cond3,    branch3,  if_true3};
  t.CheckLoop(header1, 2, body1, 15);

  Node* header2[] = {loop2, phi2};
  Node* body2[] = {cond2, branch2, if_true2, exit3,   loop3,
                   phi3,  cond3,   branch3,  if_true3};
  t.CheckLoop(header2, 2, body2, 9);

  Node* header3[] = {loop3, phi3};
  Node* body3[] = {cond3, branch3, if_true3};
  t.CheckLoop(header3, 2, body3, 3);
}


// Runs all combinations with a fixed {i}.
static void RunEdgeMatrix3_i(int i) {
  for (int a = 0; a < 1; a++) {
    for (int b = 0; b < 1; b++) {
      for (int c = 0; c < 4; c++) {
        for (int d = 0; d < 3; d++) {
          for (int e = 0; e < 3; e++) {
            for (int f = 0; f < 6; f++) {
              for (int g = 0; g < 5; g++) {
                for (int h = 0; h < 5; h++) {
                  RunEdgeMatrix3(a, b, c, d, e, f, g, h, i);
                }
              }
            }
          }
        }
      }
    }
  }
}


// Test all possible legal triply-nested loops with conditions and phis.
TEST(LaEdgeMatrix3_0) { RunEdgeMatrix3_i(0); }


TEST(LaEdgeMatrix3_1) { RunEdgeMatrix3_i(1); }


TEST(LaEdgeMatrix3_2) { RunEdgeMatrix3_i(2); }


TEST(LaEdgeMatrix3_3) { RunEdgeMatrix3_i(3); }


TEST(LaEdgeMatrix3_4) { RunEdgeMatrix3_i(4); }


TEST(LaEdgeMatrix3_5) { RunEdgeMatrix3_i(5); }


static void RunManyChainedLoops_i(int count) {
  LoopFinderTester t;
  Node** nodes = t.zone()->AllocateArray<Node*>(count * 4);
  Node* k11 = t.jsgraph.Int32Constant(11);
  Node* k12 = t.jsgraph.Int32Constant(12);
  Node* last = t.start;

  // Build loops.
  for (int i = 0; i < count; i++) {
    Node* loop = t.graph.NewNode(t.common.Loop(2), last, t.start);
    Node* phi = t.graph.NewNode(t.common.Phi(MachineRepresentation::kWord32, 2),
                                k11, k12, loop);
    Node* branch = t.graph.NewNode(t.common.Branch(), phi, loop);
    Node* if_true = t.graph.NewNode(t.common.IfTrue(), branch);
    Node* exit = t.graph.NewNode(t.common.IfFalse(), branch);
    loop->ReplaceInput(1, if_true);

    nodes[i * 4 + 0] = loop;
    nodes[i * 4 + 1] = phi;
    nodes[i * 4 + 2] = branch;
    nodes[i * 4 + 3] = if_true;

    last = exit;
  }

  Node* zero = t.graph.NewNode(t.common.Int32Constant(0));
  Node* ret = t.graph.NewNode(t.common.Return(), zero, t.p0, t.start, last);
  t.graph.SetEnd(ret);

  // Verify loops.
  for (int i = 0; i < count; i++) {
    t.CheckLoop(nodes + i * 4, 2, nodes + i * 4 + 2, 2);
  }
}


static void RunManyNestedLoops_i(int count) {
  LoopFinderTester t;
  Node** nodes = t.zone()->AllocateArray<Node*>(count * 5);
  Node* k11 = t.jsgraph.Int32Constant(11);
  Node* k12 = t.jsgraph.Int32Constant(12);
  Node* outer = nullptr;
  Node* entry = t.start;

  // Build loops.
  Node* zero = t.graph.NewNode(t.common.Int32Constant(0));
  for (int i = 0; i < count; i++) {
    Node* loop = t.graph.NewNode(t.common.Loop(2), entry, t.start);
    Node* phi = t.graph.NewNode(t.common.Phi(MachineRepresentation::kWord32, 2),
                                k11, k12, loop);
    Node* branch = t.graph.NewNode(t.common.Branch(), phi, loop);
    Node* if_true = t.graph.NewNode(t.common.IfTrue(), branch);
    Node* exit = t.graph.NewNode(t.common.IfFalse(), branch);

    nodes[i * 5 + 0] = exit;     // outside
    nodes[i * 5 + 1] = loop;     // header
    nodes[i * 5 + 2] = phi;      // header
    nodes[i * 5 + 3] = branch;   // body
    nodes[i * 5 + 4] = if_true;  // body

    if (outer != nullptr) {
      // inner loop.
      outer->ReplaceInput(1, exit);
    } else {
      // outer loop.
      Node* ret = t.graph.NewNode(t.common.Return(), zero, t.p0, t.start, exit);
      t.graph.SetEnd(ret);
    }
    outer = loop;
    entry = if_true;
  }
  outer->ReplaceInput(1, entry);  // innermost loop.

  // Verify loops.
  for (int i = 0; i < count; i++) {
    int k = i * 5;
    t.CheckLoop(nodes + k + 1, 2, nodes + k + 3, count * 5 - k - 3);
  }
}


TEST(LaManyChained_30) { RunManyChainedLoops_i(30); }
TEST(LaManyChained_31) { RunManyChainedLoops_i(31); }
TEST(LaManyChained_32) { RunManyChainedLoops_i(32); }
TEST(LaManyChained_33) { RunManyChainedLoops_i(33); }
TEST(LaManyChained_34) { RunManyChainedLoops_i(34); }
TEST(LaManyChained_62) { RunManyChainedLoops_i(62); }
TEST(LaManyChained_63) { RunManyChainedLoops_i(63); }
TEST(LaManyChained_64) { RunManyChainedLoops_i(64); }

TEST(LaManyNested_30) { RunManyNestedLoops_i(30); }
TEST(LaManyNested_31) { RunManyNestedLoops_i(31); }
TEST(LaManyNested_32) { RunManyNestedLoops_i(32); }
TEST(LaManyNested_33) { RunManyNestedLoops_i(33); }
TEST(LaManyNested_34) { RunManyNestedLoops_i(34); }
TEST(LaManyNested_62) { RunManyNestedLoops_i(62); }
TEST(LaManyNested_63) { RunManyNestedLoops_i(63); }
TEST(LaManyNested_64) { RunManyNestedLoops_i(64); }


TEST(LaPhiTangle) { LoopFinderTester t; }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
