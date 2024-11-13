// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_BASIC_BLOCK_H_
#define V8_MAGLEV_MAGLEV_BASIC_BLOCK_H_

#include <vector>

#include "src/base/small-vector.h"
#include "src/codegen/label.h"
#include "src/compiler/turboshaft/snapshot-table.h"
#include "src/maglev/maglev-interpreter-frame-state.h"
#include "src/maglev/maglev-ir.h"
#include "src/zone/zone-list.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace maglev {

using NodeIterator = Node::List::Iterator;
using NodeConstIterator = Node::List::Iterator;

class BasicBlock {
 public:
  using Snapshot = compiler::turboshaft::SnapshotTable<ValueNode*>::Snapshot;
  using MaybeSnapshot =
      compiler::turboshaft::SnapshotTable<ValueNode*>::MaybeSnapshot;

  explicit BasicBlock(MergePointInterpreterFrameState* state, Zone* zone)
      : type_(state ? kMerge : kOther),
        control_node_(nullptr),
        state_(state),
        reload_hints_(0, zone),
        spill_hints_(0, zone) {}

  NodeIdT first_id() const {
    if (has_phi()) return phis()->first()->id();
    if (nodes_.is_empty()) {
      return control_node()->id();
    }
    auto node = nodes_.first();
    while (node && node->Is<Identity>()) {
      node = node->NextNode();
    }
    return node ? node->id() : control_node()->id();
  }

  NodeIdT FirstNonGapMoveId() const {
    if (has_phi()) return phis()->first()->id();
    if (!nodes_.is_empty()) {
      for (const Node* node : nodes_) {
        if (IsGapMoveNode(node->opcode())) continue;
        if (node->Is<Identity>()) continue;
        return node->id();
      }
    }
    return control_node()->id();
  }

  Node::List& nodes() { return nodes_; }

  ControlNode* control_node() const { return control_node_; }
  void set_control_node(ControlNode* control_node) {
    DCHECK_NULL(control_node_);
    control_node_ = control_node;
  }

  bool has_phi() const { return has_state() && state_->has_phi(); }

  bool is_merge_block() const { return type_ == kMerge; }
  bool is_edge_split_block() const { return type_ == kEdgeSplit; }

  bool is_loop() const { return has_state() && state()->is_loop(); }

  MergePointRegisterState& edge_split_block_register_state() {
    DCHECK_EQ(type_, kEdgeSplit);
    DCHECK_NOT_NULL(edge_split_block_register_state_);
    return *edge_split_block_register_state_;
  }

  bool contains_node_id(NodeIdT id) const {
    return id >= first_id() && id <= control_node()->id();
  }

  void set_edge_split_block_register_state(
      MergePointRegisterState* register_state) {
    DCHECK_EQ(type_, kEdgeSplit);
    edge_split_block_register_state_ = register_state;
  }

  void set_edge_split_block(BasicBlock* predecessor) {
    DCHECK_EQ(type_, kOther);
    DCHECK(nodes_.is_empty());
    DCHECK(control_node()->Is<Jump>());
    type_ = kEdgeSplit;
    predecessor_ = predecessor;
  }

  BasicBlock* predecessor() const {
    DCHECK(type_ == kEdgeSplit || type_ == kOther);
    return predecessor_;
  }
  void set_predecessor(BasicBlock* predecessor) {
    DCHECK(type_ == kEdgeSplit || type_ == kOther);
    DCHECK_NULL(edge_split_block_register_state_);
    predecessor_ = predecessor;
  }

  bool is_start_block_of_switch_case() const {
    return is_start_block_of_switch_case_;
  }
  void set_start_block_of_switch_case(bool value) {
    is_start_block_of_switch_case_ = value;
  }

  Phi::List* phis() const {
    DCHECK(has_phi());
    return state_->phis();
  }
  void AddPhi(Phi* phi) const {
    DCHECK(has_state());
    state_->phis()->Add(phi);
  }

  int predecessor_count() const {
    DCHECK(has_state());
    return state()->predecessor_count();
  }

  BasicBlock* predecessor_at(int i) const {
    DCHECK(has_state());
    return state_->predecessor_at(i);
  }

  BasicBlock* backedge_predecessor() const {
    DCHECK(is_loop());
    return predecessor_at(predecessor_count() - 1);
  }

  int predecessor_id() const {
    return control_node()->Cast<UnconditionalControlNode>()->predecessor_id();
  }
  void set_predecessor_id(int id) {
    control_node()->Cast<UnconditionalControlNode>()->set_predecessor_id(id);
  }

  base::SmallVector<BasicBlock*, 2> successors() const;

  template <typename Func>
  void ForEachPredecessor(Func&& functor) const {
    if (type_ == kEdgeSplit || type_ == kOther) {
      BasicBlock* predecessor_block = predecessor();
      if (predecessor_block) {
        functor(predecessor_block);
      }
    } else {
      for (int i = 0; i < predecessor_count(); i++) {
        functor(predecessor_at(i));
      }
    }
  }

  template <typename Func>
  void ForEachSuccessor(Func&& functor) const {
    ControlNode* control = control_node();
    if (auto node = control->TryCast<UnconditionalControlNode>()) {
      functor(node->target());
    } else if (auto node = control->TryCast<BranchControlNode>()) {
      functor(node->if_true());
      functor(node->if_false());
    } else if (auto node = control->TryCast<Switch>()) {
      for (int i = 0; i < node->size(); i++) {
        functor(node->targets()[i].block_ptr());
      }
      if (node->has_fallthrough()) {
        functor(node->fallthrough());
      }
    }
  }

  Label* label() {
    // If this fails, jump threading is missing for the node. See
    // MaglevCodeGeneratingNodeProcessor::PatchJumps.
    DCHECK_EQ(this, RealJumpTarget());
    return &label_;
  }
  MergePointInterpreterFrameState* state() const {
    DCHECK(has_state());
    return state_;
  }
  bool has_state() const { return type_ == kMerge && state_ != nullptr; }

  bool is_exception_handler_block() const {
    return has_state() && state_->is_exception_handler();
  }

  Snapshot snapshot() const {
    DCHECK(snapshot_.has_value());
    return snapshot_.value();
  }

  void SetSnapshot(Snapshot snapshot) { snapshot_.Set(snapshot); }

  ZonePtrList<ValueNode>& reload_hints() { return reload_hints_; }
  ZonePtrList<ValueNode>& spill_hints() { return spill_hints_; }

  // If the basic block is an empty (unnecessary) block containing only an
  // unconditional jump to the successor block, return the successor block.
  BasicBlock* RealJumpTarget() {
    if (real_jump_target_cache_ != nullptr) {
      return real_jump_target_cache_;
    }

    BasicBlock* current = this;
    while (true) {
      if (!current->nodes_.is_empty() || current->is_loop() ||
          current->is_exception_handler_block() ||
          current->HasPhisOrRegisterMerges()) {
        break;
      }
      Jump* control = current->control_node()->TryCast<Jump>();
      if (!control) {
        break;
      }
      BasicBlock* next = control->target();
      if (next->HasPhisOrRegisterMerges()) {
        break;
      }
      current = next;
    }
    real_jump_target_cache_ = current;
    return current;
  }

  bool is_deferred() const { return deferred_; }
  void set_deferred(bool deferred) { deferred_ = deferred; }

 private:
  bool HasPhisOrRegisterMerges() const {
    if (!has_state()) {
      return false;
    }
    if (has_phi()) {
      return true;
    }
    bool has_register_merge = false;
#ifdef V8_ENABLE_MAGLEV
    if (!state()->register_state().is_initialized()) {
      // This can happen when the graph has disconnected blocks; bail out and
      // don't jump thread them.
      return true;
    }

    state()->register_state().ForEachGeneralRegister(
        [&](Register reg, RegisterState& state) {
          ValueNode* node;
          RegisterMerge* merge;
          if (LoadMergeState(state, &node, &merge)) {
            has_register_merge = true;
          }
        });
    state()->register_state().ForEachDoubleRegister(
        [&](DoubleRegister reg, RegisterState& state) {
          ValueNode* node;
          RegisterMerge* merge;
          if (LoadMergeState(state, &node, &merge)) {
            has_register_merge = true;
          }
        });
#endif  // V8_ENABLE_MAGLEV
    return has_register_merge;
  }

  enum : uint8_t {
    kMerge,
    kEdgeSplit,
    kOther
  } type_;
  bool is_start_block_of_switch_case_ = false;
  Node::List nodes_;
  ControlNode* control_node_;
  union {
    MergePointInterpreterFrameState* state_;
    MergePointRegisterState* edge_split_block_register_state_;
  };
  // For kEdgeSplit and kOther blocks.
  BasicBlock* predecessor_ = nullptr;
  Label label_;
  // Hints about which nodes should be in registers or spilled when entering
  // this block. Only relevant for loop headers.
  ZonePtrList<ValueNode> reload_hints_;
  ZonePtrList<ValueNode> spill_hints_;
  // {snapshot_} is used during PhiRepresentationSelection in order to track to
  // phi tagging nodes that come out of this basic block.
  MaybeSnapshot snapshot_;
  BasicBlock* real_jump_target_cache_ = nullptr;
  bool deferred_ = false;
};

inline base::SmallVector<BasicBlock*, 2> BasicBlock::successors() const {
  ControlNode* control = control_node();
  if (auto node = control->TryCast<UnconditionalControlNode>()) {
    return {node->target()};
  } else if (auto node = control->TryCast<BranchControlNode>()) {
    return {node->if_true(), node->if_false()};
  } else if (auto node = control->TryCast<Switch>()) {
    base::SmallVector<BasicBlock*, 2> succs;
    for (int i = 0; i < node->size(); i++) {
      succs.push_back(node->targets()[i].block_ptr());
    }
    if (node->has_fallthrough()) {
      succs.push_back(node->fallthrough());
    }
    return succs;
  } else {
    return base::SmallVector<BasicBlock*, 2>();
  }
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_MAGLEV_MAGLEV_BASIC_BLOCK_H_
