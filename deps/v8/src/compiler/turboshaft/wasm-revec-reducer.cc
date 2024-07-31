// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-revec-reducer.h"

#include "src/base/logging.h"

#define TRACE(...)                                  \
  do {                                              \
    if (v8_flags.trace_wasm_revectorize) {          \
      PrintF("Revec: %s %d: ", __func__, __LINE__); \
      PrintF(__VA_ARGS__);                          \
    }                                               \
  } while (false)

namespace v8::internal::compiler::turboshaft {

std::string GetSimdOpcodeName(Operation const& op) {
  std::ostringstream oss;
  if (op.Is<Simd128BinopOp>() || op.Is<Simd128UnaryOp>() ||
      op.Is<Simd128ShiftOp>() || op.Is<Simd128TestOp>() ||
      op.Is<Simd128TernaryOp>()) {
    op.PrintOptions(oss);
  } else {
    oss << OpcodeName(op.opcode);
  }
  return oss.str();
}

//  This class is the wrapper for StoreOp/LoadOp, which is helpful to calcualte
//  the relative offset between two StoreOp/LoadOp.
template <typename Op,
          typename = std::enable_if_t<
              std::is_same_v<Op, StoreOp> || std::is_same_v<Op, LoadOp> ||
              std::is_same_v<Op, Simd128LoadTransformOp>>>
class StoreLoadInfo {
 public:
  StoreLoadInfo(const Graph* graph, const Op* op)
      : op_(op), offset_(op->offset) {
    base_ = &graph->Get(op->base());
    const ChangeOp* change = nullptr;
    if constexpr (std::is_same_v<Op, Simd128LoadTransformOp>) {
      change = graph->Get(op->index()).template TryCast<ChangeOp>();
    } else {
      if (!op->index().has_value()) return;
      change = graph->Get(op->index().value()).template TryCast<ChangeOp>();
    }
    if (change == nullptr) {
      SetInvalid();
      return;
    }
    DCHECK_EQ(change->kind, ChangeOp::Kind::kZeroExtend);
    const Operation* change_input = &graph->Get(change->input());
    if (const ConstantOp* const_op = change_input->TryCast<ConstantOp>()) {
      DCHECK_EQ(const_op->kind, ConstantOp::Kind::kWord32);
      int new_offset;
      if (base::bits::SignedAddOverflow32(static_cast<int>(const_op->word32()),
                                          offset_, &new_offset)) {
        // offset is overflow
        SetInvalid();
        return;
      }
      offset_ = new_offset;
      return;
    }
    index_ = change_input;
  }

  base::Optional<int> operator-(const StoreLoadInfo<Op>& rhs) const {
    DCHECK(IsValid() && rhs.IsValid());
    bool calculatable = base_ == rhs.base_ && index_ == rhs.index_;

    if constexpr (std::is_same_v<Op, Simd128LoadTransformOp>) {
      calculatable &= (op_->load_kind == rhs.op_->load_kind &&
                       op_->transform_kind == rhs.op_->transform_kind);
    } else {
      calculatable &= (op_->kind == rhs.op_->kind);
    }

    if constexpr (std::is_same_v<Op, StoreOp>) {
      // TODO(v8:12716) If one store has a full write barrier and the other has
      // no write barrier, consider combine them with a full write barrier.
      calculatable &= (op_->write_barrier == rhs.op_->write_barrier);
    }

    if (calculatable) {
      return offset_ - rhs.offset_;
    }
    return {};
  }

  bool IsValid() const { return op_ != nullptr; }

  const Operation* index() const { return index_; }
  int offset() const { return offset_; }
  const Op* op() const { return op_; }

 private:
  void SetInvalid() { op_ = nullptr; }

  const Op* op_;
  const Operation* base_ = nullptr;
  const Operation* index_ = nullptr;
  int offset_;
};

struct StoreInfoCompare {
  bool operator()(const StoreLoadInfo<StoreOp>& lhs,
                  const StoreLoadInfo<StoreOp>& rhs) const {
    if (lhs.index() != rhs.index()) {
      return lhs.index() < rhs.index();
    }
    return lhs.offset() < rhs.offset();
  }
};

using StoreInfoSet = ZoneSet<StoreLoadInfo<StoreOp>, StoreInfoCompare>;

// Return whether the stride of node_group equal to a specific value
template <class Op, class Info>
bool LoadStrideEqualTo(const Graph& graph, const NodeGroup& node_group,
                       int stride) {
  base::SmallVector<Info, 2> load_infos;
  for (OpIndex op_idx : node_group) {
    const Operation& op = graph.Get(op_idx);
    const Op& load_op = op.Cast<Op>();
    Info info(&graph, &load_op);
    if (!info.IsValid()) {
      return false;
    }
    load_infos.push_back(info);
  }
  return load_infos[1] - load_infos[0] == stride;
}

// Returns true if all of the nodes in node_group are identical.
// Splat opcode in WASM SIMD is used to create vector with identical lanes.
template <typename T>
bool IsSplat(const T& node_group) {
  DCHECK_EQ(node_group.size(), 2);
  return node_group[1] == node_group[0];
}

void PackNode::Print(Graph* graph) const {
  Operation& op = graph->Get(nodes_[0]);
  TRACE("%s(#%d, #%d)\n", GetSimdOpcodeName(op).c_str(), nodes_[0].id(),
        nodes_[1].id());
}

PackNode* SLPTree::GetPackNode(OpIndex node) {
  auto itr = node_to_packnode_.find(node);
  if (itr != node_to_packnode_.end()) {
    return itr->second;
  }
  return nullptr;
}

template <typename FunctionType>
void ForEach(FunctionType callback,
             ZoneUnorderedMap<OpIndex, PackNode*>& node_map) {
  std::unordered_set<PackNode const*> visited;

  for (auto& entry : node_map) {
    PackNode const* pnode = entry.second;
    if (!pnode || visited.find(pnode) != visited.end()) {
      continue;
    }
    visited.insert(pnode);

    callback(pnode);
  }
}

void SLPTree::Print(const char* info) {
  TRACE("%s, %zu Packed node:\n", info, node_to_packnode_.size());
  if (!v8_flags.trace_wasm_revectorize) {
    return;
  }

  ForEach([this](PackNode const* pnode) { pnode->Print(&graph_); },
          node_to_packnode_);
}

PackNode* SLPTree::NewPackNode(const NodeGroup& node_group) {
  Operation& op = graph_.Get(node_group[0]);
  TRACE("PackNode %s(#%d, #%d)\n", GetSimdOpcodeName(op).c_str(),
        node_group[0].id(), node_group[1].id());
  PackNode* pnode = phase_zone_->New<PackNode>(node_group);
  for (OpIndex node : node_group) {
    node_to_packnode_[node] = pnode;
  }
  return pnode;
}

PackNode* SLPTree::NewPackNodeAndRecurs(const NodeGroup& node_group,
                                        int start_index, int count,
                                        unsigned depth) {
  PackNode* pnode = NewPackNode(node_group);
  for (int i = start_index; i < start_index + count; ++i) {
    // Prepare the operand vector.
    NodeGroup operands(graph_.Get(node_group[0]).input(i),
                       graph_.Get(node_group[1]).input(i));

    if (!BuildTreeRec(operands, depth + 1)) {
      return nullptr;
    }
  }
  return pnode;
}

void SLPTree::DeleteTree() { node_to_packnode_.clear(); }

bool CannotSwapProtectedLoads(OpEffects first, OpEffects second) {
  EffectDimensions produces = first.produces;
  // The control flow effects produces by Loads are due to trap handler. We can
  // ignore this kind of effect when swapping two Loads that both have trap
  // handler.
  produces.control_flow = false;
  return produces.bits() & (second.consumes.bits());
}

bool IsProtectedLoad(Operation& op) {
  if (op.opcode == Opcode::kLoad) {
    return op.Cast<LoadOp>().kind.with_trap_handler;
  } else if (op.opcode == Opcode::kSimd128LoadTransform) {
    return op.Cast<Simd128LoadTransformOp>().load_kind.with_trap_handler;
  }
  return false;
}

bool SLPTree::IsSideEffectFree(OpIndex first, OpIndex second) {
  DCHECK_LE(first.offset(), second.offset());
  if (first == second) return true;
  OpEffects effects = graph().Get(second).Effects();
  OpIndex prev_node = graph().PreviousIndex(second);
  while (prev_node != first) {
    OpEffects prev_effects = graph().Get(prev_node).Effects();
    if ((IsProtectedLoad(graph().Get(second)) &&
         IsProtectedLoad(graph().Get(prev_node)))
            ? CannotSwapProtectedLoads(prev_effects, effects)
            : CannotSwapOperations(prev_effects, effects)) {
      TRACE("break side effect %d, %d\n", prev_node.id(), second.id());
      return false;
    }
    prev_node = graph().PreviousIndex(prev_node);
  }
  return true;
}

// Returns true if op in node_group have same kind.
bool IsSameOpAndKind(const Operation& op0, const Operation& op1) {
#define CASE(operation)                                           \
  case Opcode::k##operation: {                                    \
    using Op = opcode_to_operation_map<Opcode::k##operation>::Op; \
    return op0.Cast<Op>().kind == op1.Cast<Op>().kind;            \
  }
  switch (op0.opcode) {
    CASE(Simd128Unary)
    CASE(Simd128Binop)
    CASE(Simd128Shift)
    CASE(Simd128Ternary)
    default:
      return true;
  }
#undef CASE
}

bool SLPTree::CanBePacked(const NodeGroup& node_group) {
  OpIndex node0 = node_group[0];
  OpIndex node1 = node_group[1];
  Operation& op0 = graph_.Get(node0);
  Operation& op1 = graph_.Get(node1);

  if (op0.opcode != op1.opcode) {
    TRACE("Different opcode\n");
    return false;
  }

  if (graph().BlockIndexOf(node0) != graph().BlockIndexOf(node1)) {
    TRACE("Can't pack operations of different basic block\n");
    return false;
  }

  // One node can be used more than once. Only support node to PackNode 1:1
  // mapping now, if node A is already packed with B into PackNode (A,B), can't
  // pack it with C into PackNode (A,C) anymore.
  if (GetPackNode(node0) != GetPackNode(node1)) {
    return false;
  }

  if (!IsSameOpAndKind(op0, op1)) {
    TRACE("(%s, %s) have different op\n", GetSimdOpcodeName(op0).c_str(),
          GetSimdOpcodeName(op1).c_str());
    return false;
  }

  if (node0.offset() <= node1.offset() ? !IsSideEffectFree(node0, node1)
                                       : !IsSideEffectFree(node1, node0)) {
    TRACE("Break side effect\n");
    return false;
  }
  return true;
}

bool SLPTree::IsEqual(const OpIndex node0, const OpIndex node1) {
  if (node0 == node1) return true;
  if (const ConstantOp* const0 = graph_.Get(node0).TryCast<ConstantOp>()) {
    if (const ConstantOp* const1 = graph_.Get(node1).TryCast<ConstantOp>()) {
      return *const0 == *const1;
    }
  }
  return false;
}

PackNode* SLPTree::BuildTree(const NodeGroup& roots) {
  root_ = BuildTreeRec(roots, 0);
  return root_;
}

bool IsLoadExtend(const Simd128LoadTransformOp& op) {
  switch (op.transform_kind) {
    case Simd128LoadTransformOp::TransformKind::k8x8S:
    case Simd128LoadTransformOp::TransformKind::k8x8U:
    case Simd128LoadTransformOp::TransformKind::k16x4S:
    case Simd128LoadTransformOp::TransformKind::k16x4U:
    case Simd128LoadTransformOp::TransformKind::k32x2S:
    case Simd128LoadTransformOp::TransformKind::k32x2U:
      return true;
    default:
      return false;
  }
}

bool IsLoadSplat(const Simd128LoadTransformOp& op) {
  switch (op.transform_kind) {
    case Simd128LoadTransformOp::TransformKind::k8Splat:
    case Simd128LoadTransformOp::TransformKind::k16Splat:
    case Simd128LoadTransformOp::TransformKind::k32Splat:
    case Simd128LoadTransformOp::TransformKind::k64Splat:
      return true;
    default:
      return false;
  }
}

PackNode* SLPTree::BuildTreeRec(const NodeGroup& node_group,
                                unsigned recursion_depth) {
  DCHECK_EQ(node_group.size(), 2);

  OpIndex node0 = node_group[0];
  OpIndex node1 = node_group[1];
  Operation& op0 = graph_.Get(node0);
  Operation& op1 = graph_.Get(node1);

  if (recursion_depth == RecursionMaxDepth) {
    TRACE("Failed due to max recursion depth!\n");
    return nullptr;
  }

  if (!CanBePacked(node_group)) {
    return nullptr;
  }

  // Check if this is a duplicate of another entry.
  for (OpIndex op_idx : node_group) {
    if (PackNode* p = GetPackNode(op_idx)) {
      Operation& op = graph_.Get(op_idx);
      if (p != nullptr && !p->IsSame(node_group)) {
        // TODO(jiepan): Gathering due to partial overlap
        TRACE("Failed due to partial overlap at #%d,%s!\n", op_idx.id(),
              GetSimdOpcodeName(op).c_str());
        return nullptr;
      }

      TRACE("Perfect diamond merge at #%d,%s\n", op_idx.id(),
            GetSimdOpcodeName(op).c_str());
      return p;
    }
  }

  int value_in_count = op0.input_count;

  switch (op0.opcode) {
    case Opcode::kSimd128LoadTransform: {
      const Simd128LoadTransformOp& transform_op =
          op0.Cast<Simd128LoadTransformOp>();
      if (IsLoadSplat(transform_op)) {
        TRACE("Simd128LoadTransform: LoadSplat\n");
        if (!IsSplat(node_group)) {
          return nullptr;
        }
      } else if (IsLoadExtend(transform_op)) {
        TRACE("Simd128LoadTransform: LoadExtend\n");
        if (!LoadStrideEqualTo<Simd128LoadTransformOp,
                               StoreLoadInfo<Simd128LoadTransformOp>>(
                graph_, node_group, kSimd128Size / 2)) {
          TRACE("Wrong Access stride\n");
          return nullptr;
        }
      } else {
        TRACE("Load Transfrom k64Zero/k32Zero!\n");
        DCHECK(transform_op.transform_kind ==
                   Simd128LoadTransformOp::TransformKind::k32Zero ||
               transform_op.transform_kind ==
                   Simd128LoadTransformOp::TransformKind::k64Zero);
        // k64Zero/k32Zero is not supported
        TRACE("Simd128LoadTransform: unsupported  k64Zero/k32Zero\n");
        return nullptr;
      }
      PackNode* p = NewPackNode(node_group);
      return p;
    }

    case Opcode::kLoad: {
      TRACE("Load leaf node\n");
      if (op0.Cast<LoadOp>().loaded_rep != MemoryRepresentation::Simd128() ||
          op1.Cast<LoadOp>().loaded_rep != MemoryRepresentation::Simd128()) {
        TRACE("Failed due to non-simd load representation!\n");
        return nullptr;
      }
      if (!LoadStrideEqualTo<LoadOp, StoreLoadInfo<LoadOp>>(graph_, node_group,
                                                            kSimd128Size)) {
        TRACE("Wrong Access stride\n");
        return nullptr;
      }
      // TODO(jiepan): Sort load
      PackNode* p = NewPackNode(node_group);
      return p;
    }
    case Opcode::kStore: {
      TRACE("Added a vector of stores.\n");
      // input: base, value, [index]
      PackNode* pnode = NewPackNodeAndRecurs(node_group, 1, 1, recursion_depth);
      return pnode;
    }
    case Opcode::kSimd128Unary: {
#define UNARY_CASE(op_128, not_used) case Simd128UnaryOp::Kind::k##op_128:
      switch (op0.Cast<Simd128UnaryOp>().kind) {
        SIMD256_UNARY_OP(UNARY_CASE) {
          TRACE("Added a vector of Unary\n");
          PackNode* pnode = NewPackNodeAndRecurs(node_group, 0, value_in_count,
                                                 recursion_depth);
          return pnode;
        }
        default: {
          TRACE("Unsupported Simd128Unary: %s\n",
                GetSimdOpcodeName(op0).c_str());
          return nullptr;
        }
      }
#undef UNARY_CASE
    }
    case Opcode::kSimd128Binop: {
#define BINOP_CASE(op_128, not_used) case Simd128BinopOp::Kind::k##op_128:
      switch (op0.Cast<Simd128BinopOp>().kind) {
        SIMD256_BINOP_SIMPLE_OP(BINOP_CASE) {
          TRACE("Added a vector of BinOp\n");
          PackNode* pnode = NewPackNodeAndRecurs(node_group, 0, value_in_count,
                                                 recursion_depth);
          return pnode;
        }
        default: {
          TRACE("Unsupported Simd128BinopOp: %s\n",
                GetSimdOpcodeName(op0).c_str());
          return nullptr;
        }
      }
#undef BINOP_CASE
    }
    case Opcode::kSimd128Shift: {
      Simd128ShiftOp& shift_op0 = op0.Cast<Simd128ShiftOp>();
      Simd128ShiftOp& shift_op1 = op1.Cast<Simd128ShiftOp>();
      if (IsEqual(shift_op0.shift(), shift_op1.shift())) {
        switch (op0.Cast<Simd128ShiftOp>().kind) {
#define SHIFT_CASE(op_128, not_used) case Simd128ShiftOp::Kind::k##op_128:
          SIMD256_SHIFT_OP(SHIFT_CASE) {
            TRACE("Added a vector of Shift op.\n");
            // We've already checked that the "shift by" input of both shifts is
            // the same, and we'll only pack the 1st input of the shifts
            // together anyways (since on both Simd128 and Simd256, the "shift
            // by" input of shifts is a Word32). Thus we only need to check the
            // 1st input of the shift when recursing.
            constexpr int kShiftValueInCount = 1;
            PackNode* pnode = NewPackNodeAndRecurs(
                node_group, 0, kShiftValueInCount, recursion_depth);
            return pnode;
          }
#undef SHIFT_CASE
          default: {
            TRACE("Unsupported Simd128ShiftOp: %s\n",
                  GetSimdOpcodeName(op0).c_str());
            return nullptr;
          }
        }
      }
      TRACE("Failed due to SimdShiftOp kind or shift scalar is different!\n");
      return nullptr;
    }
    case Opcode::kSimd128Ternary: {
#define TERNARY_CASE(op_128, not_used) case Simd128TernaryOp::Kind::k##op_128:
      switch (op0.Cast<Simd128TernaryOp>().kind) {
        SIMD256_TERNARY_OP(TERNARY_CASE) {
          TRACE("Added a vector of Ternary\n");
          PackNode* pnode = NewPackNodeAndRecurs(node_group, 0, value_in_count,
                                                 recursion_depth);
          return pnode;
        }
#undef TERNARY_CASE
        default: {
          TRACE("Unsupported Simd128Ternary: %s\n",
                GetSimdOpcodeName(op0).c_str());
          return nullptr;
        }
      }
    }

    default:
      TRACE("Default branch #%d:%s\n", node0.id(),
            GetSimdOpcodeName(op0).c_str());
      break;
  }
  return nullptr;
}

bool WasmRevecAnalyzer::CanMergeSLPTrees() {
  for (auto& entry : slp_tree_->GetNodeMapping()) {
    auto itr = revectorizable_node_.find(entry.first);
    if (itr != revectorizable_node_.end() &&
        !itr->second->IsSame(*entry.second)) {
      TRACE("Can't merge slp tree\n");
      return false;
    }
  }
  return true;
}

void WasmRevecAnalyzer::ProcessBlock(const Block& block) {
  StoreInfoSet simd128_stores(phase_zone_);
  for (const Operation& op : base::Reversed(graph_.operations(block))) {
    if (const StoreOp* store_op = op.TryCast<StoreOp>()) {
      if (store_op->stored_rep == MemoryRepresentation::Simd128()) {
        StoreLoadInfo<StoreOp> info(&graph_, store_op);
        if (info.IsValid()) {
          simd128_stores.insert(info);
        }
      }
    }
  }

  if (simd128_stores.size() >= 2) {
    for (auto it = std::next(simd128_stores.cbegin()),
              end = simd128_stores.cend();
         it != end;) {
      const StoreLoadInfo<StoreOp>& info0 = *std::prev(it);
      const StoreLoadInfo<StoreOp>& info1 = *it;
      auto diff = info1 - info0;

      if (diff.has_value()) {
        const int value = diff.value();
        DCHECK_GE(value, 0);
        if (value == kSimd128Size) {
          store_seeds_.push_back({info0.op(), info1.op()});
          if (std::distance(it, end) < 2) {
            break;
          }
          std::advance(it, 2);
          continue;
        }
      }
      it++;
    }
  }
}

void WasmRevecAnalyzer::Run() {
  for (Block& block : base::Reversed(graph_.blocks())) {
    ProcessBlock(block);
  }

  if (store_seeds_.empty()) {
    TRACE("Empty seed\n");
    return;
  }

  if (v8_flags.trace_wasm_revectorize) {
    PrintF("store seeds:\n");
    for (auto pair : store_seeds_) {
      PrintF("{\n");
      PrintF("#%u ", graph_.Index(*pair.first).id());
      Print(*pair.first);
      PrintF("#%u ", graph_.Index(*pair.second).id());
      Print(*pair.second);
      PrintF("}\n");
    }
  }
  slp_tree_ = phase_zone_->New<SLPTree>(graph_, phase_zone_);

  for (auto pair : store_seeds_) {
    NodeGroup roots(graph_.Index(*pair.first), graph_.Index(*pair.second));

    slp_tree_->DeleteTree();
    PackNode* root = slp_tree_->BuildTree(roots);
    if (!root) {
      TRACE("Build tree failed!\n");
      continue;
    }

    slp_tree_->Print("After build tree");

    if (CanMergeSLPTrees()) {
      revectorizable_node_.merge(slp_tree_->GetNodeMapping());
    }
  }

  // Early exist when no revectorizable node found.
  if (revectorizable_node_.empty()) return;

  // Build SIMD usemap
  use_map_ = phase_zone_->New<SimdUseMap>(graph_, phase_zone_);
  if (!DecideVectorize()) {
    revectorizable_node_.clear();
  } else {
    should_reduce_ = true;
    TRACE("Decide to revectorize!\n");
  }
}

bool WasmRevecAnalyzer::DecideVectorize() {
  TRACE("Enter %s\n", __func__);
  int save = 0, cost = 0;
  ForEach(
      [&](PackNode const* pnode) {
        const NodeGroup& nodes = pnode->Nodes();
        // Splat nodes will not cause a saving as it simply extends itself.
        if (!IsSplat(nodes)) {
          save++;
        }

        for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
          if (i > 0 && nodes[i] == nodes[0]) continue;

          for (auto use : use_map_->uses(nodes[i])) {
            if (!GetPackNode(use)) {
              TRACE("External use edge: (%d:%s) -> (%d:%s)\n", use.id(),
                    OpcodeName(graph_.Get(use).opcode), nodes[i].id(),
                    OpcodeName(graph_.Get(nodes[i]).opcode));
              cost++;

              // We only need one Extract node and all other uses can share.
              break;
            }
          }
        }
      },
      revectorizable_node_);

  TRACE("Save: %d, cost: %d\n", save, cost);
  return save > cost;
}

}  // namespace v8::internal::compiler::turboshaft
