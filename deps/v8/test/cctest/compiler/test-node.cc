// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include "src/compiler/node.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-graph.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {
namespace compiler {
namespace node {

#define NONE reinterpret_cast<Node*>(1)

static Operator dummy_operator0(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 0, 0, 0, 1, 0, 0);
static Operator dummy_operator1(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 1, 0, 0, 1, 0, 0);
static Operator dummy_operator2(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 2, 0, 0, 1, 0, 0);
static Operator dummy_operator3(IrOpcode::kParameter, Operator::kNoWrite,
                                "dummy", 3, 0, 0, 1, 0, 0);

#define CHECK_USES(node, ...)                                          \
  do {                                                                 \
    Node* __array[] = {__VA_ARGS__};                                   \
    int __size =                                                       \
        __array[0] != NONE ? static_cast<int>(arraysize(__array)) : 0; \
    CheckUseChain(node, __array, __size);                              \
  } while (false)


namespace {

using NodeMSet = std::multiset<Node*, std::less<Node*>>;

void CheckUseChain(Node* node, Node** uses, int use_count) {
  // Check ownership.
  if (use_count == 1) CHECK(node->OwnedBy(uses[0]));
  if (use_count > 1) {
    Node* first_use = uses[0];
    bool different_uses = false;
    for (int i = 0; i < use_count; i++) {
      if (uses[i] != first_use) {
        different_uses = true;
        break;
      }
    }
    if (different_uses) {
      // If there are different uses, check that node is not owned by any use.
      for (int i = 0; i < use_count; i++) {
        CHECK(!node->OwnedBy(uses[i]));
      }
    } else {
      // If all uses are the same, check that node is owned by that use.
      CHECK(node->OwnedBy(first_use));
    }
  }

  // Check the self-reported use count.
  CHECK_EQ(use_count, node->UseCount());

  // Build the expectation set.
  NodeMSet expect_set;
  for (int i = 0; i < use_count; i++) {
    expect_set.insert(uses[i]);
  }

  {
    // Check that iterating over the uses gives the right counts.
    NodeMSet use_set;
    for (auto use : node->uses()) {
      use_set.insert(use);
    }
    CHECK(expect_set == use_set);
  }

  {
    // Check that iterating over the use edges gives the right counts,
    // input indices, from(), and to() pointers.
    NodeMSet use_set;
    for (auto edge : node->use_edges()) {
      CHECK_EQ(node, edge.to());
      CHECK_EQ(node, edge.from()->InputAt(edge.index()));
      use_set.insert(edge.from());
    }
    CHECK(expect_set == use_set);
  }

  {
    // Check the use nodes actually have the node as inputs.
    for (Node* use : node->uses()) {
      size_t count = 0;
      for (Node* input : use->inputs()) {
        if (input == node) count++;
      }
      CHECK_EQ(count, expect_set.count(use));
    }
  }
}


void CheckInputs(Node* node, Node** inputs, int input_count) {
  CHECK_EQ(input_count, node->InputCount());
  // Check InputAt().
  for (int i = 0; i < static_cast<int>(input_count); i++) {
    CHECK_EQ(inputs[i], node->InputAt(i));
  }

  // Check input iterator.
  int index = 0;
  for (Node* input : node->inputs()) {
    CHECK_EQ(inputs[index], input);
    index++;
  }

  // Check use lists of inputs.
  for (int i = 0; i < static_cast<int>(input_count); i++) {
    Node* input = inputs[i];
    if (!input) continue;  // skip null inputs
    bool found = false;
    // Check regular use list.
    for (Node* use : input->uses()) {
      if (use == node) {
        found = true;
        break;
      }
    }
    CHECK(found);
    int count = 0;
    // Check use edge list.
    for (auto edge : input->use_edges()) {
      if (edge.from() == node && edge.to() == input && edge.index() == i) {
        count++;
      }
    }
    CHECK_EQ(1, count);
  }
}

}  // namespace


#define CHECK_INPUTS(node, ...)                                        \
  do {                                                                 \
    Node* __array[] = {__VA_ARGS__};                                   \
    int __size =                                                       \
        __array[0] != NONE ? static_cast<int>(arraysize(__array)) : 0; \
    CheckInputs(node, __array, __size);                                \
  } while (false)


TEST(NodeUseIteratorReplaceUses) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);
  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);
  Node* n3 = graph.NewNode(&dummy_operator0);

  CHECK_USES(n0, n1, n2);

  CHECK_INPUTS(n1, n0);
  CHECK_INPUTS(n2, n0);

  n0->ReplaceUses(n3);

  CHECK_USES(n0, NONE);
  CHECK_USES(n1, NONE);
  CHECK_USES(n2, NONE);
  CHECK_USES(n3, n1, n2);

  CHECK_INPUTS(n1, n3);
  CHECK_INPUTS(n2, n3);
}


TEST(NodeUseIteratorReplaceUsesSelf) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);
  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);

  CHECK_USES(n0, n1);
  CHECK_USES(n1, NONE);

  n1->ReplaceInput(0, n1);  // Create self-reference.

  CHECK_USES(n0, NONE);
  CHECK_USES(n1, n1);

  Node* n2 = graph.NewNode(&dummy_operator0);

  n1->ReplaceUses(n2);

  CHECK_USES(n0, NONE);
  CHECK_USES(n1, NONE);
  CHECK_USES(n2, n1);
}


TEST(ReplaceInput) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);
  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator0);
  Node* n2 = graph.NewNode(&dummy_operator0);
  Node* n3 = graph.NewNode(&dummy_operator3, n0, n1, n2);
  Node* n4 = graph.NewNode(&dummy_operator0);

  CHECK_USES(n0, n3);
  CHECK_USES(n1, n3);
  CHECK_USES(n2, n3);
  CHECK_USES(n3, NONE);
  CHECK_USES(n4, NONE);

  CHECK_INPUTS(n3, n0, n1, n2);

  n3->ReplaceInput(1, n4);

  CHECK_USES(n1, NONE);
  CHECK_USES(n4, n3);

  CHECK_INPUTS(n3, n0, n4, n2);
}


TEST(OwnedBy) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);

    CHECK(!n0->OwnedBy(n1));
    CHECK(!n1->OwnedBy(n0));

    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    CHECK(n0->OwnedBy(n2));
    CHECK(!n2->OwnedBy(n0));

    Node* n3 = graph.NewNode(&dummy_operator1, n0);
    CHECK(!n0->OwnedBy(n2));
    CHECK(!n0->OwnedBy(n3));
    CHECK(!n2->OwnedBy(n0));
    CHECK(!n3->OwnedBy(n0));
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator1, n0);
    CHECK(n0->OwnedBy(n1));
    CHECK(!n1->OwnedBy(n0));
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    CHECK(!n0->OwnedBy(n1));
    CHECK(!n0->OwnedBy(n2));
    CHECK(!n1->OwnedBy(n0));
    CHECK(!n1->OwnedBy(n2));
    CHECK(!n2->OwnedBy(n0));
    CHECK(!n2->OwnedBy(n1));

    Node* n3 = graph.NewNode(&dummy_operator0);
    n2->ReplaceInput(0, n3);

    CHECK(n0->OwnedBy(n1));
    CHECK(!n1->OwnedBy(n0));
    CHECK(!n1->OwnedBy(n0));
    CHECK(!n1->OwnedBy(n2));
    CHECK(!n2->OwnedBy(n0));
    CHECK(!n2->OwnedBy(n1));
    CHECK(n3->OwnedBy(n2));
    CHECK(!n2->OwnedBy(n3));
  }
}


TEST(Uses) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);

  CHECK_USES(n0, n1);
  CHECK_USES(n1, NONE);

  Node* n2 = graph.NewNode(&dummy_operator1, n0);

  CHECK_USES(n0, n1, n2);
  CHECK_USES(n2, NONE);

  Node* n3 = graph.NewNode(&dummy_operator1, n0);

  CHECK_USES(n0, n1, n2, n3);
  CHECK_USES(n3, NONE);
}


TEST(Inputs) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);
  Node* n3 = graph.NewNode(&dummy_operator3, n0, n1, n2);

  CHECK_INPUTS(n3, n0, n1, n2);

  Node* n4 = graph.NewNode(&dummy_operator3, n0, n1, n2);
  n3->AppendInput(graph.zone(), n4);

  CHECK_INPUTS(n3, n0, n1, n2, n4);
  CHECK_USES(n4, n3);

  n3->AppendInput(graph.zone(), n4);

  CHECK_INPUTS(n3, n0, n1, n2, n4, n4);
  CHECK_USES(n4, n3, n3);

  Node* n5 = graph.NewNode(&dummy_operator1, n4);

  CHECK_USES(n4, n3, n3, n5);
}

TEST(InsertInputs) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);

  {
    Node* node = graph.NewNode(&dummy_operator1, n0);
    node->InsertInputs(graph.zone(), 0, 1);
    node->ReplaceInput(0, n1);
    CHECK_INPUTS(node, n1, n0);
  }
  {
    Node* node = graph.NewNode(&dummy_operator1, n0);
    node->InsertInputs(graph.zone(), 0, 2);
    node->ReplaceInput(0, node);
    node->ReplaceInput(1, n2);
    CHECK_INPUTS(node, node, n2, n0);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 0, 1);
    node->ReplaceInput(0, node);
    CHECK_INPUTS(node, node, n0, n1, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 1, 1);
    node->ReplaceInput(1, node);
    CHECK_INPUTS(node, n0, node, n1, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 2, 1);
    node->ReplaceInput(2, node);
    CHECK_INPUTS(node, n0, n1, node, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 2, 1);
    node->ReplaceInput(2, node);
    CHECK_INPUTS(node, n0, n1, node, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 0, 4);
    node->ReplaceInput(0, node);
    node->ReplaceInput(1, node);
    node->ReplaceInput(2, node);
    node->ReplaceInput(3, node);
    CHECK_INPUTS(node, node, node, node, node, n0, n1, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 1, 4);
    node->ReplaceInput(1, node);
    node->ReplaceInput(2, node);
    node->ReplaceInput(3, node);
    node->ReplaceInput(4, node);
    CHECK_INPUTS(node, n0, node, node, node, node, n1, n2);
  }
  {
    Node* node = graph.NewNode(&dummy_operator3, n0, n1, n2);
    node->InsertInputs(graph.zone(), 2, 4);
    node->ReplaceInput(2, node);
    node->ReplaceInput(3, node);
    node->ReplaceInput(4, node);
    node->ReplaceInput(5, node);
    CHECK_INPUTS(node, n0, n1, node, node, node, node, n2);
  }
}

TEST(RemoveInput) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);

  CHECK_INPUTS(n0, NONE);
  CHECK_INPUTS(n1, n0);
  CHECK_INPUTS(n2, n0, n1);
  CHECK_USES(n0, n1, n2);

  n1->RemoveInput(0);
  CHECK_INPUTS(n1, NONE);
  CHECK_USES(n0, n2);

  n2->RemoveInput(0);
  CHECK_INPUTS(n2, n1);
  CHECK_USES(n0, NONE);
  CHECK_USES(n1, n2);

  n2->RemoveInput(0);
  CHECK_INPUTS(n2, NONE);
  CHECK_USES(n0, NONE);
  CHECK_USES(n1, NONE);
  CHECK_USES(n2, NONE);
}


TEST(AppendInputsAndIterator) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);

  CHECK_INPUTS(n0, NONE);
  CHECK_INPUTS(n1, n0);
  CHECK_INPUTS(n2, n0, n1);
  CHECK_USES(n0, n1, n2);

  Node* n3 = graph.NewNode(&dummy_operator0);

  n2->AppendInput(graph.zone(), n3);

  CHECK_INPUTS(n2, n0, n1, n3);
  CHECK_USES(n3, n2);
}


TEST(NullInputsSimple) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);

  CHECK_INPUTS(n0, NONE);
  CHECK_INPUTS(n1, n0);
  CHECK_INPUTS(n2, n0, n1);
  CHECK_USES(n0, n1, n2);

  n2->ReplaceInput(0, nullptr);

  CHECK_INPUTS(n2, nullptr, n1);

  CHECK_USES(n0, n1);

  n2->ReplaceInput(1, nullptr);

  CHECK_INPUTS(n2, nullptr, nullptr);

  CHECK_USES(n1, NONE);
}


TEST(NullInputsAppended) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);
  Node* n3 = graph.NewNode(&dummy_operator1, n0);
  n3->AppendInput(graph.zone(), n1);
  n3->AppendInput(graph.zone(), n2);

  CHECK_INPUTS(n3, n0, n1, n2);
  CHECK_USES(n0, n1, n2, n3);
  CHECK_USES(n1, n3);
  CHECK_USES(n2, n3);

  n3->ReplaceInput(1, nullptr);
  CHECK_USES(n1, NONE);

  CHECK_INPUTS(n3, n0, nullptr, n2);
}


TEST(ReplaceUsesFromAppendedInputs) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator1, n0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);
  Node* n3 = graph.NewNode(&dummy_operator0);

  CHECK_INPUTS(n2, n0);

  n2->AppendInput(graph.zone(), n1);
  CHECK_INPUTS(n2, n0, n1);
  CHECK_USES(n1, n2);

  n2->AppendInput(graph.zone(), n0);
  CHECK_INPUTS(n2, n0, n1, n0);
  CHECK_USES(n1, n2);
  CHECK_USES(n0, n2, n1, n2);

  n0->ReplaceUses(n3);

  CHECK_USES(n0, NONE);
  CHECK_INPUTS(n2, n3, n1, n3);
  CHECK_USES(n3, n2, n1, n2);
}


TEST(ReplaceInputMultipleUses) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* n0 = graph.NewNode(&dummy_operator0);
  Node* n1 = graph.NewNode(&dummy_operator0);
  Node* n2 = graph.NewNode(&dummy_operator1, n0);
  n2->ReplaceInput(0, n1);
  CHECK_EQ(0, n0->UseCount());
  CHECK_EQ(1, n1->UseCount());

  Node* n3 = graph.NewNode(&dummy_operator1, n0);
  n3->ReplaceInput(0, n1);
  CHECK_EQ(0, n0->UseCount());
  CHECK_EQ(2, n1->UseCount());
}


TEST(TrimInputCountInline) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator1, n0);
    n1->TrimInputCount(1);
    CHECK_INPUTS(n1, n0);
    CHECK_USES(n0, n1);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator1, n0);
    n1->TrimInputCount(0);
    CHECK_INPUTS(n1, NONE);
    CHECK_USES(n0, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);
    n2->TrimInputCount(2);
    CHECK_INPUTS(n2, n0, n1);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, n2);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator2, n0, n1);
    n2->TrimInputCount(0);
    CHECK_INPUTS(n2, NONE);
    CHECK_USES(n0, NONE);
    CHECK_USES(n1, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator2, n0, n0);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator2, n0, n0);
    n2->TrimInputCount(0);
    CHECK_INPUTS(n2, NONE);
    CHECK_USES(n0, NONE);
  }
}


TEST(TrimInputCountOutOfLine1) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    n1->AppendInput(graph.zone(), n0);
    CHECK_INPUTS(n1, n0);
    CHECK_USES(n0, n1);

    n1->TrimInputCount(1);
    CHECK_INPUTS(n1, n0);
    CHECK_USES(n0, n1);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    n1->AppendInput(graph.zone(), n0);
    CHECK_EQ(1, n1->InputCount());
    n1->TrimInputCount(0);
    CHECK_EQ(0, n1->InputCount());
    CHECK_EQ(0, n0->UseCount());
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator0);
    n2->AppendInput(graph.zone(), n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(2);
    CHECK_INPUTS(n2, n0, n1);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, n2);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator0);
    n2->AppendInput(graph.zone(), n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, NONE);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator0);
    n2->AppendInput(graph.zone(), n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(0);
    CHECK_INPUTS(n2, NONE);
    CHECK_USES(n0, NONE);
    CHECK_USES(n1, NONE);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator0);
    n2->AppendInput(graph.zone(), n0);
    n2->AppendInput(graph.zone(), n0);
    CHECK_INPUTS(n2, n0, n0);
    CHECK_USES(n0, n2, n2);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator0);
    n2->AppendInput(graph.zone(), n0);
    n2->AppendInput(graph.zone(), n0);
    CHECK_INPUTS(n2, n0, n0);
    CHECK_USES(n0, n2, n2);
    n2->TrimInputCount(0);
    CHECK_INPUTS(n2, NONE);
    CHECK_USES(n0, NONE);
  }
}


TEST(TrimInputCountOutOfLine2) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(2);
    CHECK_INPUTS(n2, n0, n1);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, n2);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
    CHECK_USES(n1, NONE);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    n2->AppendInput(graph.zone(), n1);
    CHECK_INPUTS(n2, n0, n1);
    n2->TrimInputCount(0);
    CHECK_INPUTS(n2, NONE);
    CHECK_USES(n0, NONE);
    CHECK_USES(n1, NONE);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    n2->AppendInput(graph.zone(), n0);
    CHECK_INPUTS(n2, n0, n0);
    CHECK_USES(n0, n2, n2);
    n2->TrimInputCount(1);
    CHECK_INPUTS(n2, n0);
    CHECK_USES(n0, n2);
    CHECK_USES(n2, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n2 = graph.NewNode(&dummy_operator1, n0);
    n2->AppendInput(graph.zone(), n0);
    CHECK_EQ(2, n2->InputCount());
    CHECK_EQ(2, n0->UseCount());
    n2->TrimInputCount(0);
    CHECK_EQ(0, n2->InputCount());
    CHECK_EQ(0, n0->UseCount());
    CHECK_EQ(0, n2->UseCount());
  }
}


TEST(NullAllInputs) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  for (int i = 0; i < 2; i++) {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator1, n0);
    Node* n2;
    if (i == 0) {
      n2 = graph.NewNode(&dummy_operator2, n0, n1);
      CHECK_INPUTS(n2, n0, n1);
    } else {
      n2 = graph.NewNode(&dummy_operator1, n0);
      CHECK_INPUTS(n2, n0);
      n2->AppendInput(graph.zone(), n1);  // with out-of-line input.
      CHECK_INPUTS(n2, n0, n1);
    }

    n0->NullAllInputs();
    CHECK_INPUTS(n0, NONE);

    CHECK_USES(n0, n1, n2);
    n1->NullAllInputs();
    CHECK_INPUTS(n1, nullptr);
    CHECK_INPUTS(n2, n0, n1);
    CHECK_USES(n0, n2);

    n2->NullAllInputs();
    CHECK_INPUTS(n1, nullptr);
    CHECK_INPUTS(n2, nullptr, nullptr);
    CHECK_USES(n0, NONE);
  }

  {
    Node* n0 = graph.NewNode(&dummy_operator0);
    Node* n1 = graph.NewNode(&dummy_operator1, n0);
    n1->ReplaceInput(0, n1);  // self-reference.

    CHECK_INPUTS(n0, NONE);
    CHECK_INPUTS(n1, n1);
    CHECK_USES(n0, NONE);
    CHECK_USES(n1, n1);
    n1->NullAllInputs();

    CHECK_INPUTS(n0, NONE);
    CHECK_INPUTS(n1, nullptr);
    CHECK_USES(n0, NONE);
    CHECK_USES(n1, NONE);
  }
}


TEST(AppendAndTrim) {
  v8::internal::AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME, kCompressGraphZone);
  Graph graph(&zone);

  Node* nodes[] = {
      graph.NewNode(&dummy_operator0), graph.NewNode(&dummy_operator0),
      graph.NewNode(&dummy_operator0), graph.NewNode(&dummy_operator0),
      graph.NewNode(&dummy_operator0)};

  int max = static_cast<int>(arraysize(nodes));

  Node* last = graph.NewNode(&dummy_operator0);

  for (int i = 0; i < max; i++) {
    last->AppendInput(graph.zone(), nodes[i]);
    CheckInputs(last, nodes, i + 1);

    for (int j = 0; j < max; j++) {
      if (j <= i) CHECK_USES(nodes[j], last);
      if (j > i) CHECK_USES(nodes[j], NONE);
    }

    CHECK_USES(last, NONE);
  }

  for (int i = max; i >= 0; i--) {
    last->TrimInputCount(i);
    CheckInputs(last, nodes, i);

    for (int j = 0; j < i; j++) {
      if (j < i) CHECK_USES(nodes[j], last);
      if (j >= i) CHECK_USES(nodes[j], NONE);
    }

    CHECK_USES(last, NONE);
  }
}

#undef NONE
#undef CHECK_USES
#undef CHECK_INPUTS

}  // namespace node
}  // namespace compiler
}  // namespace internal
}  // namespace v8
