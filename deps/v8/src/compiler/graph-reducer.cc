// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-reducer.h"

#include <functional>
#include <limits>

#include "src/codegen/tick-counter.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"
#include "src/compiler/verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class GraphReducer::State : uint8_t {
  kUnvisited,
  kRevisit,
  kOnStack,
  kVisited
};


void Reducer::Finalize() {}

Reduction Reducer::Reduce(Node* node,
                          ObserveNodeManager* observe_node_manager) {
  Reduction reduction = Reduce(node);
  if (V8_UNLIKELY(observe_node_manager && reduction.Changed())) {
    observe_node_manager->OnNodeChanged(reducer_name(), node,
                                        reduction.replacement());
  }
  return reduction;
}

GraphReducer::GraphReducer(Zone* zone, Graph* graph, TickCounter* tick_counter,
                           JSHeapBroker* broker, Node* dead,
                           ObserveNodeManager* observe_node_manager)
    : graph_(graph),
      dead_(dead),
      state_(graph, 4),
      reducers_(zone),
      revisit_(zone),
      stack_(zone),
      tick_counter_(tick_counter),
      broker_(broker),
      observe_node_manager_(observe_node_manager) {
  if (dead != nullptr) {
    NodeProperties::SetType(dead_, Type::None());
  }
}

GraphReducer::~GraphReducer() = default;


void GraphReducer::AddReducer(Reducer* reducer) {
  reducers_.push_back(reducer);
}


void GraphReducer::ReduceNode(Node* node) {
  DCHECK(stack_.empty());
  DCHECK(revisit_.empty());
  Push(node);
  for (;;) {
    if (!stack_.empty()) {
      // Process the node on the top of the stack, potentially pushing more or
      // popping the node off the stack.
      ReduceTop();
    } else if (!revisit_.empty()) {
      // If the stack becomes empty, revisit any nodes in the revisit queue.
      node = revisit_.front();
      revisit_.pop();
      if (state_.Get(node) == State::kRevisit) {
        // state can change while in queue.
        Push(node);
      }
    } else {
      // Run all finalizers.
      for (Reducer* const reducer : reducers_) reducer->Finalize();

      // Check if we have new nodes to revisit.
      if (revisit_.empty()) break;
    }
  }
  DCHECK(revisit_.empty());
  DCHECK(stack_.empty());
}


void GraphReducer::ReduceGraph() { ReduceNode(graph()->end()); }


Reduction GraphReducer::Reduce(Node* const node) {
  auto skip = reducers_.end();
  for (auto i = reducers_.begin(); i != reducers_.end();) {
    if (i != skip) {
      tick_counter_->TickAndMaybeEnterSafepoint();
      Reduction reduction = (*i)->Reduce(node, observe_node_manager_);
      if (!reduction.Changed()) {
        // No change from this reducer.
      } else if (reduction.replacement() == node) {
        // {replacement} == {node} represents an in-place reduction. Rerun
        // all the other reducers for this node, as now there may be more
        // opportunities for reduction.
        if (v8_flags.trace_turbo_reduction) {
          UnparkedScopeIfNeeded unparked(broker_);
          // TODO(neis): Disallow racy handle dereference once we stop
          // supporting --no-local-heaps --no-concurrent-inlining.
          AllowHandleDereference allow_deref;
          StdoutStream{} << "- In-place update of #" << *node << " by reducer "
                         << (*i)->reducer_name() << std::endl;
        }
        skip = i;
        i = reducers_.begin();
        continue;
      } else {
        // {node} was replaced by another node.
        if (v8_flags.trace_turbo_reduction) {
          UnparkedScopeIfNeeded unparked(broker_);
          // TODO(neis): Disallow racy handle dereference once we stop
          // supporting --no-local-heaps --no-concurrent-inlining.
          AllowHandleDereference allow_deref;
          StdoutStream{} << "- Replacement of #" << *node << " with #"
                         << *(reduction.replacement()) << " by reducer "
                         << (*i)->reducer_name() << std::endl;
        }
        return reduction;
      }
    }
    ++i;
  }
  if (skip == reducers_.end()) {
    // No change from any reducer.
    return Reducer::NoChange();
  }
  // At least one reducer did some in-place reduction.
  return Reducer::Changed(node);
}


void GraphReducer::ReduceTop() {
  NodeState& entry = stack_.top();
  Node* node = entry.node;
  DCHECK_EQ(State::kOnStack, state_.Get(node));

  if (node->IsDead()) return Pop();  // Node was killed while on stack.

  Node::Inputs node_inputs = node->inputs();

  // Recurse on an input if necessary.
  int start = entry.input_index < node_inputs.count() ? entry.input_index : 0;
  for (int i = start; i < node_inputs.count(); ++i) {
    Node* input = node_inputs[i];
    if (input != node && Recurse(input)) {
      entry.input_index = i + 1;
      return;
    }
  }
  for (int i = 0; i < start; ++i) {
    Node* input = node_inputs[i];
    if (input != node && Recurse(input)) {
      entry.input_index = i + 1;
      return;
    }
  }

  // Remember the max node id before reduction.
  NodeId const max_id = static_cast<NodeId>(graph()->NodeCount() - 1);

  // All inputs should be visited or on stack. Apply reductions to node.
  Reduction reduction = Reduce(node);

  // If there was no reduction, pop {node} and continue.
  if (!reduction.Changed()) return Pop();

  // Check if the reduction is an in-place update of the {node}.
  Node* const replacement = reduction.replacement();
  if (replacement == node) {
    for (Node* const user : node->uses()) {
      DCHECK_IMPLIES(user == node, state_.Get(node) != State::kVisited);
      Revisit(user);
    }

    // In-place update of {node}, may need to recurse on an input.
    node_inputs = node->inputs();
    for (int i = 0; i < node_inputs.count(); ++i) {
      Node* input = node_inputs[i];
      if (input != node && Recurse(input)) {
        entry.input_index = i + 1;
        return;
      }
    }
  }

  // After reducing the node, pop it off the stack.
  Pop();

  // Check if we have a new replacement.
  if (replacement != node) {
    Replace(node, replacement, max_id);
  }
}


void GraphReducer::Replace(Node* node, Node* replacement) {
  Replace(node, replacement, std::numeric_limits<NodeId>::max());
}


void GraphReducer::Replace(Node* node, Node* replacement, NodeId max_id) {
  if (node == graph()->start()) graph()->SetStart(replacement);
  if (node == graph()->end()) graph()->SetEnd(replacement);
  if (replacement->id() <= max_id) {
    // {replacement} is an old node, so unlink {node} and assume that
    // {replacement} was already reduced and finish.
    for (Edge edge : node->use_edges()) {
      Node* const user = edge.from();
      Verifier::VerifyEdgeInputReplacement(edge, replacement);
      edge.UpdateTo(replacement);
      // Don't revisit this node if it refers to itself.
      if (user != node) Revisit(user);
    }
    node->Kill();
  } else {
    // Replace all old uses of {node} with {replacement}, but allow new nodes
    // created by this reduction to use {node}.
    for (Edge edge : node->use_edges()) {
      Node* const user = edge.from();
      if (user->id() <= max_id) {
        edge.UpdateTo(replacement);
        // Don't revisit this node if it refers to itself.
        if (user != node) Revisit(user);
      }
    }
    // Unlink {node} if it's no longer used.
    if (node->uses().empty()) node->Kill();

    // If there was a replacement, reduce it after popping {node}.
    Recurse(replacement);
  }
}


void GraphReducer::ReplaceWithValue(Node* node, Node* value, Node* effect,
                                    Node* control) {
  if (effect == nullptr && node->op()->EffectInputCount() > 0) {
    effect = NodeProperties::GetEffectInput(node);
  }
  if (control == nullptr && node->op()->ControlInputCount() > 0) {
    control = NodeProperties::GetControlInput(node);
  }

  // Requires distinguishing between value, effect and control edges.
  for (Edge edge : node->use_edges()) {
    Node* const user = edge.from();
    DCHECK(!user->IsDead());
    if (NodeProperties::IsControlEdge(edge)) {
      if (user->opcode() == IrOpcode::kIfSuccess) {
        Replace(user, control);
      } else if (user->opcode() == IrOpcode::kIfException) {
        DCHECK_NOT_NULL(dead_);
        edge.UpdateTo(dead_);
        Revisit(user);
      } else {
        DCHECK_NOT_NULL(control);
        edge.UpdateTo(control);
        Revisit(user);
      }
    } else if (NodeProperties::IsEffectEdge(edge)) {
      DCHECK_NOT_NULL(effect);
      edge.UpdateTo(effect);
      Revisit(user);
    } else {
      DCHECK_NOT_NULL(value);
      edge.UpdateTo(value);
      Revisit(user);
    }
  }
}


void GraphReducer::Pop() {
  Node* node = stack_.top().node;
  state_.Set(node, State::kVisited);
  stack_.pop();
}


void GraphReducer::Push(Node* const node) {
  DCHECK_NE(State::kOnStack, state_.Get(node));
  state_.Set(node, State::kOnStack);
  stack_.push({node, 0});
}


bool GraphReducer::Recurse(Node* node) {
  if (state_.Get(node) > State::kRevisit) return false;
  Push(node);
  return true;
}


void GraphReducer::Revisit(Node* node) {
  if (state_.Get(node) == State::kVisited) {
    state_.Set(node, State::kRevisit);
    revisit_.push(node);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
