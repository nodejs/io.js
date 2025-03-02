// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/loop-peeling.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/loop-analysis.h"
#include "src/compiler/node-marker.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone.h"

// Loop peeling is an optimization that copies the body of a loop, creating
// a new copy of the body called the "peeled iteration" that represents the
// first iteration. Beginning with a loop as follows:

//             E
//             |                 A
//             |                 |                     (backedges)
//             | +---------------|---------------------------------+
//             | | +-------------|-------------------------------+ |
//             | | |             | +--------+                    | |
//             | | |             | | +----+ |                    | |
//             | | |             | | |    | |                    | |
//           ( Loop )<-------- ( phiA )   | |                    | |
//              |                 |       | |                    | |
//      ((======P=================U=======|=|=====))             | |
//      ((                                | |     ))             | |
//      ((        X <---------------------+ |     ))             | |
//      ((                                  |     ))             | |
//      ((     body                         |     ))             | |
//      ((                                  |     ))             | |
//      ((        Y <-----------------------+     ))             | |
//      ((                                        ))             | |
//      ((===K====L====M==========================))             | |
//           |    |    |                                         | |
//           |    |    +-----------------------------------------+ |
//           |    +------------------------------------------------+
//           |
//          exit

// The body of the loop is duplicated so that all nodes considered "inside"
// the loop (e.g. {P, U, X, Y, K, L, M}) have a corresponding copies in the
// peeled iteration (e.g. {P', U', X', Y', K', L', M'}). What were considered
// backedges of the loop correspond to edges from the peeled iteration to
// the main loop body, with multiple backedges requiring a merge.

// Similarly, any exits from the loop body need to be merged with "exits"
// from the peeled iteration, resulting in the graph as follows:

//             E
//             |                 A
//             |                 |
//      ((=====P'================U'===============))
//      ((                                        ))
//      ((        X'<-------------+               ))
//      ((                        |               ))
//      ((   peeled iteration     |               ))
//      ((                        |               ))
//      ((        Y'<-----------+ |               ))
//      ((                      | |               ))
//      ((===K'===L'====M'======|=|===============))
//           |    |     |       | |
//  +--------+    +-+ +-+       | |
//  |               | |         | |
//  |              Merge <------phi
//  |                |           |
//  |          +-----+           |
//  |          |                 |                     (backedges)
//  |          | +---------------|---------------------------------+
//  |          | | +-------------|-------------------------------+ |
//  |          | | |             | +--------+                    | |
//  |          | | |             | | +----+ |                    | |
//  |          | | |             | | |    | |                    | |
//  |        ( Loop )<-------- ( phiA )   | |                    | |
//  |           |                 |       | |                    | |
//  |   ((======P=================U=======|=|=====))             | |
//  |   ((                                | |     ))             | |
//  |   ((        X <---------------------+ |     ))             | |
//  |   ((                                  |     ))             | |
//  |   ((     body                         |     ))             | |
//  |   ((                                  |     ))             | |
//  |   ((        Y <-----------------------+     ))             | |
//  |   ((                                        ))             | |
//  |   ((===K====L====M==========================))             | |
//  |        |    |    |                                         | |
//  |        |    |    +-----------------------------------------+ |
//  |        |    +------------------------------------------------+
//  |        |
//  |        |
//  +----+ +-+
//       | |
//      Merge
//        |
//      exit

// Note that the boxes ((===)) above are not explicitly represented in the
// graph, but are instead computed by the {LoopFinder}.

namespace v8 {
namespace internal {
namespace compiler {

class PeeledIterationImpl : public PeeledIteration {
 public:
  NodeVector node_pairs_;
  explicit PeeledIterationImpl(Zone* zone) : node_pairs_(zone) {}
};


Node* PeeledIteration::map(Node* node) {
  // TODO(turbofan): we use a simple linear search, since the peeled iteration
  // is really only used in testing.
  PeeledIterationImpl* impl = static_cast<PeeledIterationImpl*>(this);
  for (size_t i = 0; i < impl->node_pairs_.size(); i += 2) {
    if (impl->node_pairs_[i] == node) return impl->node_pairs_[i + 1];
  }
  return node;
}

PeeledIteration* LoopPeeler::Peel(LoopTree::Loop* loop) {
  if (!CanPeel(loop)) return nullptr;

  //============================================================================
  // Construct the peeled iteration.
  //============================================================================
  PeeledIterationImpl* iter = tmp_zone_->New<PeeledIterationImpl>(tmp_zone_);
  uint32_t estimated_peeled_size = 5 + loop->TotalSize() * 2;
  NodeCopier copier(graph_, estimated_peeled_size, &iter->node_pairs_, 1);

  Node* dead = graph_->NewNode(common_->Dead());

  // Map the loop header nodes to their entry values.
  for (Node* node : loop_tree_->HeaderNodes(loop)) {
    copier.Insert(node, node->InputAt(kAssumedLoopEntryIndex));
  }

  // Copy all the nodes of loop body for the peeled iteration.
  copier.CopyNodes(graph_, tmp_zone_, dead, loop_tree_->BodyNodes(loop),
                   source_positions_, node_origins_);

  //============================================================================
  // Replace the entry to the loop with the output of the peeled iteration.
  //============================================================================
  Node* loop_node = loop_tree_->GetLoopControl(loop);
  Node* new_entry;
  int backedges = loop_node->InputCount() - 1;
  if (backedges > 1) {
    // Multiple backedges from original loop, therefore multiple output edges
    // from the peeled iteration.
    NodeVector inputs(tmp_zone_);
    for (int i = 1; i < loop_node->InputCount(); i++) {
      inputs.push_back(copier.map(loop_node->InputAt(i)));
    }
    Node* merge =
        graph_->NewNode(common_->Merge(backedges), backedges, &inputs[0]);

    // Merge values from the multiple output edges of the peeled iteration.
    for (Node* node : loop_tree_->HeaderNodes(loop)) {
      if (node->opcode() == IrOpcode::kLoop) continue;  // already done.
      inputs.clear();
      for (int i = 0; i < backedges; i++) {
        inputs.push_back(copier.map(node->InputAt(1 + i)));
      }
      for (Node* input : inputs) {
        if (input != inputs[0]) {  // Non-redundant phi.
          inputs.push_back(merge);
          const Operator* op = common_->ResizeMergeOrPhi(node->op(), backedges);
          Node* phi = graph_->NewNode(op, backedges + 1, &inputs[0]);
          node->ReplaceInput(0, phi);
          break;
        }
      }
    }
    new_entry = merge;
  } else {
    // Only one backedge, simply replace the input to loop with output of
    // peeling.
    for (Node* node : loop_tree_->HeaderNodes(loop)) {
      node->ReplaceInput(0, copier.map(node->InputAt(1)));
    }
    new_entry = copier.map(loop_node->InputAt(1));
  }
  loop_node->ReplaceInput(0, new_entry);

  //============================================================================
  // Change the exit and exit markers to merge/phi/effect-phi.
  //============================================================================
  for (Node* exit : loop_tree_->ExitNodes(loop)) {
    switch (exit->opcode()) {
      case IrOpcode::kLoopExit:
        // Change the loop exit node to a merge node.
        exit->ReplaceInput(1, copier.map(exit->InputAt(0)));
        NodeProperties::ChangeOp(exit, common_->Merge(2));
        break;
      case IrOpcode::kLoopExitValue:
        // Change exit marker to phi.
        exit->InsertInput(graph_->zone(), 1, copier.map(exit->InputAt(0)));
        NodeProperties::ChangeOp(
            exit, common_->Phi(LoopExitValueRepresentationOf(exit->op()), 2));
        break;
      case IrOpcode::kLoopExitEffect:
        // Change effect exit marker to effect phi.
        exit->InsertInput(graph_->zone(), 1, copier.map(exit->InputAt(0)));
        NodeProperties::ChangeOp(exit, common_->EffectPhi(2));
        break;
      default:
        break;
    }
  }
  return iter;
}

void LoopPeeler::PeelInnerLoops(LoopTree::Loop* loop) {
  // If the loop has nested loops, peel inside those.
  if (!loop->children().empty()) {
    for (LoopTree::Loop* inner_loop : loop->children()) {
      PeelInnerLoops(inner_loop);
    }
    return;
  }
  // Only peel small-enough loops.
  if (loop->TotalSize() > LoopPeeler::kMaxPeeledNodes) return;
  if (v8_flags.trace_turbo_loop) {
    PrintF("Peeling loop with header: ");
    for (Node* node : loop_tree_->HeaderNodes(loop)) {
      PrintF("%i ", node->id());
    }
    PrintF("\n");
  }

  Peel(loop);
}

void LoopPeeler::EliminateLoopExit(Node* node) {
  DCHECK_EQ(IrOpcode::kLoopExit, node->opcode());
  // The exit markers take the loop exit as input. We iterate over uses
  // and remove all the markers from the graph.
  for (Edge edge : node->use_edges()) {
    if (NodeProperties::IsControlEdge(edge)) {
      Node* marker = edge.from();
      if (marker->opcode() == IrOpcode::kLoopExitValue) {
        NodeProperties::ReplaceUses(marker, marker->InputAt(0));
        marker->Kill();
      } else if (marker->opcode() == IrOpcode::kLoopExitEffect) {
        NodeProperties::ReplaceUses(marker, nullptr,
                                    NodeProperties::GetEffectInput(marker));
        marker->Kill();
      }
    }
  }
  NodeProperties::ReplaceUses(node, nullptr, nullptr,
                              NodeProperties::GetControlInput(node, 0));
  node->Kill();
}

void LoopPeeler::PeelInnerLoopsOfTree() {
  for (LoopTree::Loop* loop : loop_tree_->outer_loops()) {
    PeelInnerLoops(loop);
  }

  EliminateLoopExits(graph_, tmp_zone_);
}

// static
void LoopPeeler::EliminateLoopExits(Graph* graph, Zone* tmp_zone) {
  ZoneQueue<Node*> queue(tmp_zone);
  BitVector visited(static_cast<int>(graph->NodeCount()), tmp_zone);
  queue.push(graph->end());
  while (!queue.empty()) {
    Node* node = queue.front();
    queue.pop();

    if (node->opcode() == IrOpcode::kLoopExit) {
      Node* control = NodeProperties::GetControlInput(node);
      EliminateLoopExit(node);
      if (!visited.Contains(control->id())) {
        visited.Add(control->id());
        queue.push(control);
      }
    } else {
      for (int i = 0; i < node->op()->ControlInputCount(); i++) {
        Node* control = NodeProperties::GetControlInput(node, i);
        if (!visited.Contains(control->id())) {
          visited.Add(control->id());
          queue.push(control);
        }
      }
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
