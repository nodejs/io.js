// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/control-equivalence.h"

#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/utils/bit-vector.h"
#include "src/zone/zone-containers.h"
#include "test/unittests/compiler/graph-unittest.h"

namespace v8 {
namespace internal {
namespace compiler {

#define ASSERT_EQUIVALENCE(...)                           \
  do {                                                    \
    Node* __n[] = {__VA_ARGS__};                          \
    ASSERT_TRUE(IsEquivalenceClass(arraysize(__n), __n)); \
  } while (false)

class ControlEquivalenceTest : public GraphTest {
 public:
  ControlEquivalenceTest() : all_nodes_(zone()), classes_(zone()) {
    Store(graph()->start());
  }

 protected:
  void ComputeEquivalence(Node* end_node) {
    graph()->SetEnd(graph()->NewNode(common()->End(1), end_node));
    if (v8_flags.trace_turbo) {
      SourcePositionTable table(graph());
      NodeOriginTable table2(graph());
      StdoutStream{} << AsJSON(*graph(), &table, &table2);
    }
    ControlEquivalence equivalence(zone(), graph());
    equivalence.Run(end_node);
    classes_.resize(graph()->NodeCount());
    for (Node* node : all_nodes_) {
      classes_[node->id()] = equivalence.ClassOf(node);
    }
  }

  bool IsEquivalenceClass(size_t length, Node** nodes) {
    BitVector in_class(static_cast<int>(graph()->NodeCount()), zone());
    size_t expected_class = classes_[nodes[0]->id()];
    for (size_t i = 0; i < length; ++i) {
      in_class.Add(nodes[i]->id());
    }
    for (Node* node : all_nodes_) {
      if (in_class.Contains(node->id())) {
        if (classes_[node->id()] != expected_class) return false;
      } else {
        if (classes_[node->id()] == expected_class) return false;
      }
    }
    return true;
  }

  Node* Value() { return NumberConstant(0.0); }

  Node* Branch(Node* control) {
    return Store(graph()->NewNode(common()->Branch(), Value(), control));
  }

  Node* IfTrue(Node* control) {
    return Store(graph()->NewNode(common()->IfTrue(), control));
  }

  Node* IfFalse(Node* control) {
    return Store(graph()->NewNode(common()->IfFalse(), control));
  }

  Node* Merge1(Node* control) {
    return Store(graph()->NewNode(common()->Merge(1), control));
  }

  Node* Merge2(Node* control1, Node* control2) {
    return Store(graph()->NewNode(common()->Merge(2), control1, control2));
  }

  Node* Loop2(Node* control) {
    return Store(graph()->NewNode(common()->Loop(2), control, control));
  }

  Node* End(Node* control) {
    return Store(graph()->NewNode(common()->End(1), control));
  }

 private:
  Node* Store(Node* node) {
    all_nodes_.push_back(node);
    return node;
  }

  ZoneVector<Node*> all_nodes_;
  ZoneVector<size_t> classes_;
};


// -----------------------------------------------------------------------------
// Test cases.


TEST_F(ControlEquivalenceTest, Empty1) {
  Node* start = graph()->start();
  ComputeEquivalence(start);

  ASSERT_EQUIVALENCE(start);
}


TEST_F(ControlEquivalenceTest, Empty2) {
  Node* start = graph()->start();
  Node* merge1 = Merge1(start);
  ComputeEquivalence(merge1);

  ASSERT_EQUIVALENCE(start, merge1);
}


TEST_F(ControlEquivalenceTest, Diamond1) {
  Node* start = graph()->start();
  Node* b = Branch(start);
  Node* t = IfTrue(b);
  Node* f = IfFalse(b);
  Node* m = Merge2(t, f);
  ComputeEquivalence(m);

  ASSERT_EQUIVALENCE(b, m, start);
  ASSERT_EQUIVALENCE(f);
  ASSERT_EQUIVALENCE(t);
}


TEST_F(ControlEquivalenceTest, Diamond2) {
  Node* start = graph()->start();
  Node* b1 = Branch(start);
  Node* t1 = IfTrue(b1);
  Node* f1 = IfFalse(b1);
  Node* b2 = Branch(f1);
  Node* t2 = IfTrue(b2);
  Node* f2 = IfFalse(b2);
  Node* m2 = Merge2(t2, f2);
  Node* m1 = Merge2(t1, m2);
  ComputeEquivalence(m1);

  ASSERT_EQUIVALENCE(b1, m1, start);
  ASSERT_EQUIVALENCE(t1);
  ASSERT_EQUIVALENCE(f1, b2, m2);
  ASSERT_EQUIVALENCE(t2);
  ASSERT_EQUIVALENCE(f2);
}


TEST_F(ControlEquivalenceTest, Diamond3) {
  Node* start = graph()->start();
  Node* b1 = Branch(start);
  Node* t1 = IfTrue(b1);
  Node* f1 = IfFalse(b1);
  Node* m1 = Merge2(t1, f1);
  Node* b2 = Branch(m1);
  Node* t2 = IfTrue(b2);
  Node* f2 = IfFalse(b2);
  Node* m2 = Merge2(t2, f2);
  ComputeEquivalence(m2);

  ASSERT_EQUIVALENCE(b1, m1, b2, m2, start);
  ASSERT_EQUIVALENCE(t1);
  ASSERT_EQUIVALENCE(f1);
  ASSERT_EQUIVALENCE(t2);
  ASSERT_EQUIVALENCE(f2);
}


TEST_F(ControlEquivalenceTest, Switch1) {
  Node* start = graph()->start();
  Node* b1 = Branch(start);
  Node* t1 = IfTrue(b1);
  Node* f1 = IfFalse(b1);
  Node* b2 = Branch(f1);
  Node* t2 = IfTrue(b2);
  Node* f2 = IfFalse(b2);
  Node* b3 = Branch(f2);
  Node* t3 = IfTrue(b3);
  Node* f3 = IfFalse(b3);
  Node* m1 = Merge2(t1, t2);
  Node* m2 = Merge2(m1, t3);
  Node* m3 = Merge2(m2, f3);
  ComputeEquivalence(m3);

  ASSERT_EQUIVALENCE(b1, m3, start);
  ASSERT_EQUIVALENCE(t1);
  ASSERT_EQUIVALENCE(f1, b2);
  ASSERT_EQUIVALENCE(t2);
  ASSERT_EQUIVALENCE(f2, b3);
  ASSERT_EQUIVALENCE(t3);
  ASSERT_EQUIVALENCE(f3);
  ASSERT_EQUIVALENCE(m1);
  ASSERT_EQUIVALENCE(m2);
}


TEST_F(ControlEquivalenceTest, Loop1) {
  Node* start = graph()->start();
  Node* l = Loop2(start);
  l->ReplaceInput(1, l);
  ComputeEquivalence(l);

  ASSERT_EQUIVALENCE(start);
  ASSERT_EQUIVALENCE(l);
}


TEST_F(ControlEquivalenceTest, Loop2) {
  Node* start = graph()->start();
  Node* l = Loop2(start);
  Node* b = Branch(l);
  Node* t = IfTrue(b);
  Node* f = IfFalse(b);
  l->ReplaceInput(1, t);
  ComputeEquivalence(f);

  ASSERT_EQUIVALENCE(f, start);
  ASSERT_EQUIVALENCE(t);
  ASSERT_EQUIVALENCE(l, b);
}


TEST_F(ControlEquivalenceTest, Irreducible) {
  Node* start = graph()->start();
  Node* b1 = Branch(start);
  Node* t1 = IfTrue(b1);
  Node* f1 = IfFalse(b1);
  Node* lp = Loop2(f1);
  Node* m1 = Merge2(t1, lp);
  Node* b2 = Branch(m1);
  Node* t2 = IfTrue(b2);
  Node* f2 = IfFalse(b2);
  Node* m2 = Merge2(t2, f2);
  Node* b3 = Branch(m2);
  Node* t3 = IfTrue(b3);
  Node* f3 = IfFalse(b3);
  lp->ReplaceInput(1, f3);
  ComputeEquivalence(t3);

  ASSERT_EQUIVALENCE(b1, t3, start);
  ASSERT_EQUIVALENCE(t1);
  ASSERT_EQUIVALENCE(f1);
  ASSERT_EQUIVALENCE(m1, b2, m2, b3);
  ASSERT_EQUIVALENCE(t2);
  ASSERT_EQUIVALENCE(f2);
  ASSERT_EQUIVALENCE(f3);
  ASSERT_EQUIVALENCE(lp);
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
