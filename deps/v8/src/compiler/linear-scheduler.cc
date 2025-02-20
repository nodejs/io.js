// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/linear-scheduler.h"

#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

LinearScheduler::LinearScheduler(Zone* zone, Graph* graph)
    : graph_(graph), control_level_(zone), early_schedule_position_(zone) {
  ComputeControlLevel();
}

void LinearScheduler::ComputeControlLevel() {
  Node* start = graph_->start();
  SetControlLevel(start, 0);

  // Do BFS from the start node and compute the level of
  // each control node.
  std::queue<Node*> queue({start});
  while (!queue.empty()) {
    Node* node = queue.front();
    int level = GetControlLevel(node);
    queue.pop();
    for (Edge const edge : node->use_edges()) {
      if (!NodeProperties::IsControlEdge(edge)) continue;
      Node* use = edge.from();
      if (use->opcode() == IrOpcode::kLoopExit &&
          node->opcode() == IrOpcode::kLoop)
        continue;
      if (control_level_.find(use) == control_level_.end() &&
          use->opcode() != IrOpcode::kEnd) {
        SetControlLevel(use, level + 1);
        queue.push(use);
      }
    }
  }
}

Node* LinearScheduler::GetEarlySchedulePosition(Node* node) {
  DCHECK(!NodeProperties::IsControl(node));

  auto it = early_schedule_position_.find(node);
  if (it != early_schedule_position_.end()) return it->second;

  std::stack<NodeState> stack;
  stack.push({node, nullptr, 0});
  Node* early_schedule_position = nullptr;
  while (!stack.empty()) {
    NodeState& top = stack.top();
    if (NodeProperties::IsPhi(top.node)) {
      // For phi node, the early schedule position is its control node.
      early_schedule_position = NodeProperties::GetControlInput(top.node);
    } else if (top.node->InputCount() == 0) {
      // For node without inputs, the early schedule position is start node.
      early_schedule_position = graph_->start();
    } else {
      // For others, the early schedule position is one of its inputs' early
      // schedule position with maximal level.
      if (top.input_index == top.node->InputCount()) {
        // All inputs are visited, set early schedule position.
        early_schedule_position = top.early_schedule_position;
      } else {
        // Visit top's input and find its early schedule position.
        Node* input = top.node->InputAt(top.input_index);
        Node* input_early_schedule_position = nullptr;
        if (NodeProperties::IsControl(input)) {
          input_early_schedule_position = input;
        } else {
          auto early_pos_it = early_schedule_position_.find(input);
          if (early_pos_it != early_schedule_position_.end()) {
            input_early_schedule_position = early_pos_it->second;
          }
        }
        if (input_early_schedule_position != nullptr) {
          if (top.early_schedule_position == nullptr ||
              GetControlLevel(top.early_schedule_position) <
                  GetControlLevel(input_early_schedule_position)) {
            top.early_schedule_position = input_early_schedule_position;
          }
          top.input_index += 1;
        } else {
          top.input_index += 1;
          stack.push({input, nullptr, 0});
        }
        continue;
      }
    }

    // Found top's early schedule position, set it to the cache and pop it out
    // of the stack.
    SetEarlySchedulePosition(top.node, early_schedule_position);
    stack.pop();
    // Update early schedule position of top's use.
    if (!stack.empty()) {
      NodeState& use = stack.top();
      if (use.early_schedule_position == nullptr ||
          GetControlLevel(use.early_schedule_position) <
              GetControlLevel(early_schedule_position)) {
        use.early_schedule_position = early_schedule_position;
      }
    }
  }

  DCHECK(early_schedule_position != nullptr);
  return early_schedule_position;
}

bool LinearScheduler::SameBasicBlock(Node* node0, Node* node1) {
  Node* early_schedule_position0 = NodeProperties::IsControl(node0)
                                       ? node0
                                       : GetEarlySchedulePosition(node0);
  Node* early_schedule_position1 = NodeProperties::IsControl(node1)
                                       ? node1
                                       : GetEarlySchedulePosition(node1);
  return early_schedule_position0 == early_schedule_position1;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
