// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-address-reassociation.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/turbofan-graph.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

// Wasm address reassociation.
//
// wasm32 load and store operations use a 32-bit dynamic offset along with a
// 32-bit static index to create a 33-bit effective address. This means that
// to use a static index, greater than zero, the producer needs to prove that
// the addition of the index won't overflow. However, if we're performing
// address computations with 64-bits, we should be able to more readily use
// immediate indexes.
//
// So, the purpose of this transform is to pattern match certain address
// computations and reorganize the operands for more efficient code generation.
//
// Many addresses will be computed in the form like this:
// - ProtectedLoad (IntPtrAdd (base_reg, immediate_offset), register_offset)
// - ProtectedStore (IntPtrAdd (base_reg, immediate_offset), register_offset)

// And this pass aims to transform this into:
// - ProtectedLoad (IntPtrAdd (base_reg, register_offset), immediate_offset)
// - ProtectedStore (IntPtrAdd (base_reg, register_offset), immediate_offset)
//
// This allows the reuse of a base pointer across multiple instructions, each of
// which then has the opportunity to use an immediate offset.

WasmAddressReassociation::WasmAddressReassociation(JSGraph* jsgraph, Zone* zone)
    : graph_(jsgraph->graph()),
      common_(jsgraph->common()),
      machine_(jsgraph->machine()),
      candidate_base_addrs_(zone),
      candidates_(zone),
      zone_(zone) {}

void WasmAddressReassociation::Optimize() {
  for (auto& candidate : candidates_) {
    const CandidateAddressKey& key = candidate.first;
    if (!ShouldTryOptimize(key)) continue;
    // We've found multiple instances of addresses in the form
    // object(base + imm_offset), reg_offset
    // So, create a new object for these operations to share and then use an
    // immediate offset:
    // object(base, reg_offset), imm_offset
    Node* new_object = CreateNewBase(key);
    CandidateMemOps& mem_ops = candidate.second;
    size_t num_nodes = mem_ops.GetNumNodes();
    for (size_t i = 0; i < num_nodes; ++i) {
      Node* mem_op = mem_ops.mem_op(i);
      Node* imm_offset =
          graph_->NewNode(common_->Int64Constant(mem_ops.imm_offset(i)));
      ReplaceInputs(mem_op, new_object, imm_offset);
    }
  }
}

bool WasmAddressReassociation::ShouldTryOptimize(
    const CandidateAddressKey& key) const {
  // We already process the graph in terms of effect chains in an attempt to
  // reduce the risk of creating large live-ranges, but also set a lower
  // bound for the number of required users so that the benefits are more
  // likely to outweigh any detrimental affects, such as additions being shared
  // and so the number of operations is increased. Benchmarking showed two or
  // more was a good heuristic.
  return candidates_.at(key).GetNumNodes() > 1;
}

Node* WasmAddressReassociation::CreateNewBase(const CandidateAddressKey& key) {
  CandidateBaseAddr& candidate_base_addr = candidate_base_addrs_.at(key);
  Node* base = candidate_base_addr.base();
  Node* reg_offset = candidate_base_addr.offset();
  return graph_->NewNode(machine_->Int64Add(), base, reg_offset);
}

void WasmAddressReassociation::ReplaceInputs(Node* mem_op, Node* base,
                                             Node* offset) {
  DCHECK_GT(mem_op->InputCount(), 1);
  DCHECK(NodeProperties::IsConstant(offset));
  mem_op->ReplaceInput(0, base);
  mem_op->ReplaceInput(1, offset);
}

void WasmAddressReassociation::VisitProtectedMemOp(Node* node,
                                                   NodeId effect_chain) {
  DCHECK(node->opcode() == IrOpcode::kProtectedLoad ||
         node->opcode() == IrOpcode::kProtectedStore);

  Node* base(node->InputAt(0));
  Node* offset(node->InputAt(1));

  if (base->opcode() == IrOpcode::kInt64Add &&
      offset->opcode() == IrOpcode::kInt64Add) {
    Int64BinopMatcher base_add(base);
    Int64BinopMatcher offset_add(offset);
    if (base_add.right().HasResolvedValue() &&
        !base_add.left().HasResolvedValue() &&
        offset_add.right().HasResolvedValue() &&
        !offset_add.left().HasResolvedValue()) {
      Node* base_reg = base_add.left().node();
      Node* reg_offset = offset_add.left().node();
      int64_t imm_offset =
          base_add.right().ResolvedValue() + offset_add.right().ResolvedValue();
      return AddCandidate(node, base_reg, reg_offset, imm_offset, effect_chain);
    }
  }
  if (base->opcode() == IrOpcode::kInt64Add) {
    Int64BinopMatcher base_add(base);
    if (base_add.right().HasResolvedValue() &&
        !base_add.left().HasResolvedValue()) {
      Node* base_reg = base_add.left().node();
      Node* reg_offset = node->InputAt(1);
      int64_t imm_offset = base_add.right().ResolvedValue();
      return AddCandidate(node, base_reg, reg_offset, imm_offset, effect_chain);
    }
  }
  if (offset->opcode() == IrOpcode::kInt64Add) {
    Int64BinopMatcher offset_add(offset);
    if (offset_add.right().HasResolvedValue() &&
        !offset_add.left().HasResolvedValue()) {
      Node* base_reg = node->InputAt(0);
      Node* reg_offset = offset_add.left().node();
      int64_t imm_offset = offset_add.right().ResolvedValue();
      return AddCandidate(node, base_reg, reg_offset, imm_offset, effect_chain);
    }
  }
}

void WasmAddressReassociation::AddCandidate(Node* mem_op, Node* base,
                                            Node* reg_offset,
                                            int64_t imm_offset,
                                            NodeId effect_chain) {
  // Sort base and offset so that the key is the same for either permutation.
  if (base->id() > reg_offset->id()) {
    std::swap(base, reg_offset);
  }
  CandidateAddressKey key =
      std::make_tuple(base->id(), reg_offset->id(), effect_chain);
  bool is_new =
      candidate_base_addrs_.emplace(key, CandidateBaseAddr(base, reg_offset))
          .second;
  auto it = is_new ? candidates_.emplace(key, CandidateMemOps(zone_)).first
                   : candidates_.find(key);
  it->second.AddCandidate(mem_op, imm_offset);
}

bool WasmAddressReassociation::HasCandidateBaseAddr(
    const CandidateAddressKey& key) const {
  return candidate_base_addrs_.count(key);
}

void WasmAddressReassociation::CandidateMemOps::AddCandidate(
    Node* mem_op, int64_t imm_offset) {
  DCHECK(mem_op->opcode() == IrOpcode::kProtectedLoad ||
         mem_op->opcode() == IrOpcode::kProtectedStore);
  mem_ops_.push_back(mem_op);
  imm_offsets_.push_back(imm_offset);
}

size_t WasmAddressReassociation::CandidateMemOps::GetNumNodes() const {
  DCHECK_EQ(mem_ops_.size(), imm_offsets_.size());
  return mem_ops_.size();
}

Node* WasmAddressReassociation::CandidateMemOps::mem_op(size_t i) const {
  return mem_ops_[i];
}

int64_t WasmAddressReassociation::CandidateMemOps::imm_offset(size_t i) const {
  return imm_offsets_[i];
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
