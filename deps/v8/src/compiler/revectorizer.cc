// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/revectorizer.h"

#include "src/base/cpu.h"
#include "src/base/logging.h"
#include "src/compiler/all-nodes.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/verifier.h"
#include "src/execution/isolate-inl.h"
#include "src/wasm/simd-shuffle.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE(...)                         \
  do {                                     \
    if (v8_flags.trace_wasm_revectorize) { \
      PrintF("Revec: ");                   \
      PrintF(__VA_ARGS__);                 \
    }                                      \
  } while (false)

namespace {

#define SIMPLE_SIMD_OP(V)                   \
  V(F64x2Add, F64x4Add)                     \
  V(F32x4Add, F32x8Add)                     \
  V(I64x2Add, I64x4Add)                     \
  V(I32x4Add, I32x8Add)                     \
  V(I16x8Add, I16x16Add)                    \
  V(I8x16Add, I8x32Add)                     \
  V(F64x2Sub, F64x4Sub)                     \
  V(F32x4Sub, F32x8Sub)                     \
  V(I64x2Sub, I64x4Sub)                     \
  V(I32x4Sub, I32x8Sub)                     \
  V(I16x8Sub, I16x16Sub)                    \
  V(I8x16Sub, I8x32Sub)                     \
  V(F64x2Mul, F64x4Mul)                     \
  V(F32x4Mul, F32x8Mul)                     \
  V(I64x2Mul, I64x4Mul)                     \
  V(I32x4Mul, I32x8Mul)                     \
  V(I16x8Mul, I16x16Mul)                    \
  V(F64x2Div, F64x4Div)                     \
  V(F32x4Div, F32x8Div)                     \
  V(I16x8AddSatS, I16x16AddSatS)            \
  V(I16x8SubSatS, I16x16SubSatS)            \
  V(I16x8AddSatU, I16x16AddSatU)            \
  V(I16x8SubSatU, I16x16SubSatU)            \
  V(I8x16AddSatS, I8x32AddSatS)             \
  V(I8x16SubSatS, I8x32SubSatS)             \
  V(I8x16AddSatU, I8x32AddSatU)             \
  V(I8x16SubSatU, I8x32SubSatU)             \
  V(F64x2Eq, F64x4Eq)                       \
  V(F32x4Eq, F32x8Eq)                       \
  V(I64x2Eq, I64x4Eq)                       \
  V(I32x4Eq, I32x8Eq)                       \
  V(I16x8Eq, I16x16Eq)                      \
  V(I8x16Eq, I8x32Eq)                       \
  V(F64x2Ne, F64x4Ne)                       \
  V(F32x4Ne, F32x8Ne)                       \
  V(I64x2GtS, I64x4GtS)                     \
  V(I32x4GtS, I32x8GtS)                     \
  V(I16x8GtS, I16x16GtS)                    \
  V(I8x16GtS, I8x32GtS)                     \
  V(F64x2Lt, F64x4Lt)                       \
  V(F32x4Lt, F32x8Lt)                       \
  V(F64x2Le, F64x4Le)                       \
  V(F32x4Le, F32x8Le)                       \
  V(I32x4MinS, I32x8MinS)                   \
  V(I16x8MinS, I16x16MinS)                  \
  V(I8x16MinS, I8x32MinS)                   \
  V(I32x4MinU, I32x8MinU)                   \
  V(I16x8MinU, I16x16MinU)                  \
  V(I8x16MinU, I8x32MinU)                   \
  V(I32x4MaxS, I32x8MaxS)                   \
  V(I16x8MaxS, I16x16MaxS)                  \
  V(I8x16MaxS, I8x32MaxS)                   \
  V(I32x4MaxU, I32x8MaxU)                   \
  V(I16x8MaxU, I16x16MaxU)                  \
  V(I8x16MaxU, I8x32MaxU)                   \
  V(F32x4Abs, F32x8Abs)                     \
  V(I32x4Abs, I32x8Abs)                     \
  V(I16x8Abs, I16x16Abs)                    \
  V(I8x16Abs, I8x32Abs)                     \
  V(F32x4Neg, F32x8Neg)                     \
  V(I32x4Neg, I32x8Neg)                     \
  V(I16x8Neg, I16x16Neg)                    \
  V(I8x16Neg, I8x32Neg)                     \
  V(F64x2Sqrt, F64x4Sqrt)                   \
  V(F32x4Sqrt, F32x8Sqrt)                   \
  V(F64x2Min, F64x4Min)                     \
  V(F32x4Min, F32x8Min)                     \
  V(F64x2Max, F64x4Max)                     \
  V(F32x4Max, F32x8Max)                     \
  V(I64x2Ne, I64x4Ne)                       \
  V(I32x4Ne, I32x8Ne)                       \
  V(I16x8Ne, I16x16Ne)                      \
  V(I8x16Ne, I8x32Ne)                       \
  V(I32x4GtU, I32x8GtU)                     \
  V(I16x8GtU, I16x16GtU)                    \
  V(I8x16GtU, I8x32GtU)                     \
  V(I64x2GeS, I64x4GeS)                     \
  V(I32x4GeS, I32x8GeS)                     \
  V(I16x8GeS, I16x16GeS)                    \
  V(I8x16GeS, I8x32GeS)                     \
  V(I32x4GeU, I32x8GeU)                     \
  V(I16x8GeU, I16x16GeU)                    \
  V(I8x16GeU, I8x32GeU)                     \
  V(F32x4Pmin, F32x8Pmin)                   \
  V(F32x4Pmax, F32x8Pmax)                   \
  V(F64x2Pmin, F64x4Pmin)                   \
  V(F64x2Pmax, F64x4Pmax)                   \
  V(F32x4SConvertI32x4, F32x8SConvertI32x8) \
  V(F32x4UConvertI32x4, F32x8UConvertI32x8) \
  V(I32x4UConvertF32x4, I32x8UConvertF32x8) \
  V(S128And, S256And)                       \
  V(S128Or, S256Or)                         \
  V(S128Xor, S256Xor)                       \
  V(S128Not, S256Not)                       \
  V(S128Select, S256Select)                 \
  V(S128AndNot, S256AndNot)

#define SIMD_SHIFT_OP(V)   \
  V(I64x2Shl, I64x4Shl)    \
  V(I32x4Shl, I32x8Shl)    \
  V(I16x8Shl, I16x16Shl)   \
  V(I32x4ShrS, I32x8ShrS)  \
  V(I16x8ShrS, I16x16ShrS) \
  V(I64x2ShrU, I64x4ShrU)  \
  V(I32x4ShrU, I32x8ShrU)  \
  V(I16x8ShrU, I16x16ShrU)

#define SIMD_SIGN_EXTENSION_CONVERT_OP(V)                               \
  V(I64x2SConvertI32x4Low, I64x2SConvertI32x4High, I64x4SConvertI32x4)  \
  V(I64x2UConvertI32x4Low, I64x2UConvertI32x4High, I64x4UConvertI32x4)  \
  V(I32x4SConvertI16x8Low, I32x4SConvertI16x8High, I32x8SConvertI16x8)  \
  V(I32x4UConvertI16x8Low, I32x4UConvertI16x8High, I32x8UConvertI16x8)  \
  V(I16x8SConvertI8x16Low, I16x8SConvertI8x16High, I16x16SConvertI8x16) \
  V(I16x8UConvertI8x16Low, I16x8UConvertI8x16High, I16x16UConvertI8x16)

#define SIMD_SPLAT_OP(V)     \
  V(I8x16Splat, I8x32Splat)  \
  V(I16x8Splat, I16x16Splat) \
  V(I32x4Splat, I32x8Splat)  \
  V(I64x2Splat, I64x4Splat)

// Currently, only Load/ProtectedLoad/LoadTransfrom are supported.
// TODO(jiepan): add support for UnalignedLoad, LoadLane, LoadTrapOnNull
bool IsSupportedLoad(const Node* node) {
  if (node->opcode() == IrOpcode::kProtectedLoad ||
      node->opcode() == IrOpcode::kLoad ||
      node->opcode() == IrOpcode::kLoadTransform) {
    return true;
  }
  return false;
}

#ifdef DEBUG
bool IsSupportedLoad(const ZoneVector<Node*>& node_group) {
  for (auto node : node_group) {
    if (!IsSupportedLoad(node)) return false;
  }
  return true;
}
#endif

int64_t GetConstantValue(const Node* node) {
  int64_t value = -1;
  if (node->opcode() == IrOpcode::kInt64Constant) {
    value = OpParameter<int64_t>(node->op());
  }
  return value;
}

int64_t GetMemoryOffsetValue(const Node* node) {
  DCHECK(IsSupportedLoad(node) || node->opcode() == IrOpcode::kStore ||
         node->opcode() == IrOpcode::kProtectedStore);

  Node* offset = node->InputAt(0);
  if (offset->opcode() == IrOpcode::kLoadFromObject ||
      offset->opcode() == IrOpcode::kLoad) {
    return 0;
  }

  int64_t offset_value = -1;
  if (offset->opcode() == IrOpcode::kInt64Add) {
    if (NodeProperties::IsConstant(offset->InputAt(0))) {
      offset_value = GetConstantValue(offset->InputAt(0));
    } else if (NodeProperties::IsConstant(offset->InputAt(1))) {
      offset_value = GetConstantValue(offset->InputAt(1));
    }
  }
  return offset_value;
}

// We want to combine load/store nodes with continuous memory address,
// for load/store node, input(0) is memory_start + offset,  input(1) is index,
// we currently use index as the address of the node, nodes with same index and
// continuous offset can be combined together.
Node* GetNodeAddress(const Node* node) {
  Node* address = node->InputAt(1);
  // The index is changed to Uint64 for memory32
  if (address->opcode() == IrOpcode::kChangeUint32ToUint64) {
    address = address->InputAt(0);
  }
  return address;
}

bool IsContinuousAccess(const ZoneVector<Node*>& node_group) {
  DCHECK_GT(node_group.size(), 0);
  int64_t previous_offset = GetMemoryOffsetValue(node_group[0]);
  for (size_t i = 1; i < node_group.size(); ++i) {
    int64_t current_offset = GetMemoryOffsetValue(node_group[i]);
    int64_t diff = current_offset - previous_offset;
    if (diff == 8 && node_group[0]->opcode() == IrOpcode::kLoadTransform) {
      LoadTransformParameters params =
          LoadTransformParametersOf(node_group[0]->op());
      if (params.transformation < LoadTransformation::kFirst128Extend ||
          params.transformation > LoadTransformation::kLast128Extend) {
        TRACE("Non-continuous access!\n");
        return false;
      }
      TRACE("Continuous access with load extend offset!\n");
    } else if (diff != kSimd128Size) {
      TRACE("Non-continuous access!\n");
      return false;
    }
    previous_offset = current_offset;
  }
  return true;
}

// Returns true if all of the nodes in node_group are constants.
bool AllConstant(const ZoneVector<Node*>& node_group) {
  for (Node* node : node_group) {
    if (!NodeProperties::IsConstant(node)) {
      return false;
    }
  }
  return true;
}

// Returns true if all the addresses of the nodes in node_group are identical.
bool AllSameAddress(const ZoneVector<Node*>& nodes) {
  Node* address = GetNodeAddress(nodes[0]);
  for (size_t i = 1; i < nodes.size(); i++) {
    if (GetNodeAddress(nodes[i]) != address) {
      TRACE("Diff address #%d,#%d!\n", address->id(),
            GetNodeAddress(nodes[i])->id());
      return false;
    }
  }
  return true;
}

// Returns true if all of the nodes in node_group are identical.
// Splat opcode in WASM SIMD is used to create vector with identical lanes.
template <typename T>
bool IsSplat(const T& node_group) {
  for (typename T::size_type i = 1; i < node_group.size(); ++i) {
    if (node_group[i] != node_group[0]) {
      return false;
    }
  }
  return true;
}

// Some kinds of node (shuffle, s128const) will have different operator
// instances even if they have the same properties, we can't simply compare the
// operator's address. We should compare their opcode and properties.
V8_INLINE static bool OperatorCanBePacked(const Operator* lhs,
                                          const Operator* rhs) {
  return lhs->opcode() == rhs->opcode() &&
         lhs->properties() == rhs->properties();
}

// Returns true if all of the nodes in node_group have the same type.
bool AllPackableOperator(const ZoneVector<Node*>& node_group) {
  auto op = node_group[0]->op();
  for (ZoneVector<Node*>::size_type i = 1; i < node_group.size(); i++) {
    if (!OperatorCanBePacked(node_group[i]->op(), op)) {
      return false;
    }
  }
  return true;
}

bool ShiftBySameScalar(const ZoneVector<Node*>& node_group) {
  auto node0 = node_group[0];
  for (ZoneVector<Node*>::size_type i = 1; i < node_group.size(); i++) {
    DCHECK_EQ(node_group[i]->op(), node0->op());
    DCHECK_EQ(node0->InputCount(), 2);
    if (node_group[i]->InputAt(1) != node0->InputAt(1)) {
      return false;
    }
  }
  return true;
}

bool IsSignExtensionOperation(IrOpcode::Value op) {
#define CASE(op_low, op_high, not_used) \
  case IrOpcode::k##op_low:             \
  case IrOpcode::k##op_high:
  switch (op) {
    SIMD_SIGN_EXTENSION_CONVERT_OP(CASE)
    return true;
    default:
      return false;
  }
#undef CASE
  UNREACHABLE();
}

bool MaybePackSignExtensionOp(const ZoneVector<Node*>& node_group) {
#define CHECK_SIGN_EXTENSION_CASE(op_low, op_high, not_used)      \
  case IrOpcode::k##op_low: {                                     \
    if (node_group[1]->opcode() == IrOpcode::k##op_high &&        \
        node_group[0]->InputAt(0) == node_group[1]->InputAt(0)) { \
      return true;                                                \
    }                                                             \
    return false;                                                 \
  }
  switch (node_group[0]->opcode()) {
    SIMD_SIGN_EXTENSION_CONVERT_OP(CHECK_SIGN_EXTENSION_CASE)
    default: {
      return false;
    }
  }
#undef CHECK_SIGN_EXTENSION_CASE
  UNREACHABLE();
}

class EffectChainIterator {
 public:
  explicit EffectChainIterator(Node* node) : node_(node), prev_(nullptr) {}

  Node* Advance() {
    prev_ = node_;
    node_ = EffectInputOf(node_);
    return node_;
  }

  Node* Prev() {
    DCHECK_NE(prev_, nullptr);
    return prev_;
  }

  Node* Next() { return EffectInputOf(node_); }

  void Set(Node* node) {
    node_ = node;
    prev_ = nullptr;
  }

  Node* operator*() { return node_; }

 private:
  Node* EffectInputOf(Node* node) {
    DCHECK(IsSupportedLoad(node));
    return node->InputAt(2);
  }

  Node* node_;
  Node* prev_;
};

void InsertAfter(EffectChainIterator& dest, EffectChainIterator& src) {
  Node* dest_next = dest.Next();
  NodeProperties::ReplaceEffectInput(src.Prev(), src.Next());
  NodeProperties::ReplaceEffectInput(*dest, *src);
  NodeProperties::ReplaceEffectInput(*src, dest_next);
}

}  // anonymous namespace

// Sort load/store node by offset
bool MemoryOffsetComparer::operator()(const Node* lhs, const Node* rhs) const {
  return GetMemoryOffsetValue(lhs) < GetMemoryOffsetValue(rhs);
}

void PackNode::Print() const {
  if (revectorized_node_ != nullptr) {
    TRACE("0x%p #%d:%s(%d %d, %s)\n", this, revectorized_node_->id(),
          revectorized_node_->op()->mnemonic(), nodes_[0]->id(),
          nodes_[1]->id(), nodes_[0]->op()->mnemonic());
  } else {
    TRACE("0x%p null(%d %d, %s)\n", this, nodes_[0]->id(), nodes_[1]->id(),
          nodes_[0]->op()->mnemonic());
  }
}

bool SLPTree::CanBePacked(const ZoneVector<Node*>& node_group) {
  DCHECK_EQ(node_group.size(), 2);
  // Only Support simd128 operators or common operators with simd128
  // MachineRepresentation. The MachineRepresentation of root had been checked,
  // and the leaf node will be checked later. here we omit the check of
  // MachineRepresentation, only check the opcode itself.
  IrOpcode::Value op = node_group[0]->opcode();
  if (!NodeProperties::IsSimd128Operation(node_group[0]) &&
      (op != IrOpcode::kStore) && (op != IrOpcode::kProtectedStore) &&
      (op != IrOpcode::kLoad) && (op != IrOpcode::kProtectedLoad) &&
      (op != IrOpcode::kPhi) && (op != IrOpcode::kLoopExitValue) &&
      (op != IrOpcode::kExtractF128)) {
    return false;
  }

  // TODO(jiepan): add support for Constant
  if (AllConstant(node_group)) {
    TRACE("%s(#%d, #%d) are constantant, not supported yet!\n",
          node_group[0]->op()->mnemonic(), node_group[0]->id(),
          node_group[1]->id());
    return false;
  }
  if (IsSignExtensionOperation(op)) {
    if (MaybePackSignExtensionOp(node_group)) {
      return true;
    } else {
      TRACE("%s(#%d, #%d) are not (low, high) sign extension pair\n",
            node_group[0]->op()->mnemonic(), node_group[0]->id(),
            node_group[1]->id());
      return false;
    }
  }
  if (!AllPackableOperator(node_group)) {
    TRACE(
        "%s(#%d, #%d) have different op, and are not sign extension operator\n",
        node_group[0]->op()->mnemonic(), node_group[0]->id(),
        node_group[1]->id());
    return false;
  }
  return true;
}

PackNode* SLPTree::NewPackNode(const ZoneVector<Node*>& node_group) {
  TRACE("PackNode %s(#%d:, #%d)\n", node_group[0]->op()->mnemonic(),
        node_group[0]->id(), node_group[1]->id());
  PackNode* pnode = zone_->New<PackNode>(zone_, node_group);
  for (Node* node : node_group) {
    node_to_packnode_[node] = pnode;
  }
  return pnode;
}

PackNode* SLPTree::NewPackNodeAndRecurs(const ZoneVector<Node*>& node_group,
                                        int start_index, int count,
                                        unsigned recursion_depth) {
  PackNode* pnode = NewPackNode(node_group);
  for (int i = start_index; i < start_index + count; ++i) {
    ZoneVector<Node*> operands(zone_);
    // Prepare the operand vector.
    for (size_t j = 0; j < node_group.size(); j++) {
      Node* node = node_group[j];
      operands.push_back(NodeProperties::GetValueInput(node, i));
    }

    PackNode* child = BuildTreeRec(operands, recursion_depth + 1);
    if (child) {
      pnode->SetOperand(i, child);
    } else {
      return nullptr;
    }
  }
  return pnode;
}

PackNode* SLPTree::GetPackNode(Node* node) {
  auto I = node_to_packnode_.find(node);
  if (I != node_to_packnode_.end()) {
    return I->second;
  }
  return nullptr;
}

void SLPTree::PushStack(const ZoneVector<Node*>& node_group) {
  TRACE("Stack Push (%d %s, %d %s)\n", node_group[0]->id(),
        node_group[0]->op()->mnemonic(), node_group[1]->id(),
        node_group[1]->op()->mnemonic());
  for (auto node : node_group) {
    on_stack_.insert(node);
  }
  stack_.push({node_group});
}

void SLPTree::PopStack() {
  const ZoneVector<Node*>& node_group = stack_.top();
  DCHECK_EQ(node_group.size(), 2);
  TRACE("Stack Pop (%d %s, %d %s)\n", node_group[0]->id(),
        node_group[0]->op()->mnemonic(), node_group[1]->id(),
        node_group[1]->op()->mnemonic());
  for (auto node : node_group) {
    on_stack_.erase(node);
  }
  stack_.pop();
}

bool SLPTree::OnStack(Node* node) {
  return on_stack_.find(node) != on_stack_.end();
}

bool SLPTree::AllOnStack(const ZoneVector<Node*>& node_group) {
  for (auto node : node_group) {
    if (OnStack(node)) return true;
  }
  return false;
}

bool SLPTree::StackTopIsPhi() {
  const ZoneVector<Node*>& node_group = stack_.top();
  DCHECK_EQ(node_group.size(), 2);
  return NodeProperties::IsPhi(node_group[0]);
}

void SLPTree::ClearStack() {
  stack_ = ZoneStack<ZoneVector<Node*>>(zone_);
  on_stack_.clear();
}

// Try to connect the nodes in |loads| by effect edges. This allows us to build
// |PackNode| without breaking effect dependency:
// Before: [Load1]->...->[Load2]->...->[Load3]->...->[Load4]
// After:  [Load1]->[Load2]->[Load3]->[Load4]
void SLPTree::TryReduceLoadChain(const ZoneVector<Node*>& loads) {
  ZoneSet<Node*> visited(zone());
  for (Node* load : loads) {
    if (visited.find(load) != visited.end()) continue;
    visited.insert(load);

    EffectChainIterator dest(load);
    EffectChainIterator it(dest.Next());
    while (SameBasicBlock(*it, load) && IsSupportedLoad(*it)) {
      if (std::find(loads.begin(), loads.end(), *it) != loads.end()) {
        visited.insert(*it);
        if (dest.Next() != *it) {
          Node* prev = it.Prev();
          InsertAfter(dest, it);
          it.Set(prev);
        }
        dest.Advance();
      }
      it.Advance();
    }
  }
}

bool SLPTree::IsSideEffectFreeLoad(const ZoneVector<Node*>& node_group) {
  DCHECK(IsSupportedLoad(node_group));
  DCHECK_EQ(node_group.size(), 2);
  TRACE("Enter IsSideEffectFreeLoad (%d %s, %d %s)\n", node_group[0]->id(),
        node_group[0]->op()->mnemonic(), node_group[1]->id(),
        node_group[1]->op()->mnemonic());

  TryReduceLoadChain(node_group);
  // We only allows Loads that are connected by effect edges.
  if (node_group[0] != node_group[1] &&
      NodeProperties::GetEffectInput(node_group[0]) != node_group[1] &&
      NodeProperties::GetEffectInput(node_group[1]) != node_group[0])
    return false;

  std::stack<Node*> to_visit;
  std::unordered_set<Node*> visited;
  // Visit all the inputs (except for control inputs) of Loads.
  for (size_t i = 0, e = node_group.size(); i < e; i++) {
    Node* load = node_group[i];
    for (int j = 0; j < NodeProperties::FirstControlIndex(load); ++j) {
      Node* input = load->InputAt(j);
      if (std::find(node_group.begin(), node_group.end(), input) ==
          node_group.end()) {
        to_visit.push(input);
      }
    }
  }

  // Check the inputs of Loads and find if they are connected to existing nodes
  // in SLPTree. If there is, then there will be side effect and we can not
  // merge such Loads.
  while (!to_visit.empty()) {
    Node* input = to_visit.top();
    to_visit.pop();
    TRACE("IsSideEffectFreeLoad visit (%d %s)\n", input->id(),
          input->op()->mnemonic());
    if (visited.find(input) == visited.end()) {
      visited.insert(input);

      if (OnStack(input)) {
        TRACE("Has internal dependency because (%d %s) on stack\n", input->id(),
              input->op()->mnemonic());
        return false;
      }

      // If the input is not in same basic block as Loads, it must not be in
      // SLPTree. Otherwise recursively visit all input's edges and find if they
      // are connected to SLPTree.
      if (SameBasicBlock(input, node_group[0])) {
        for (int i = 0; i < NodeProperties::FirstControlIndex(input); ++i) {
          to_visit.push(input->InputAt(i));
        }
      }
    }
  }
  return true;
}

PackNode* SLPTree::BuildTree(const ZoneVector<Node*>& roots) {
  TRACE("Enter %s\n", __func__);

  DeleteTree();

  root_ = BuildTreeRec(roots, 0);
  return root_;
}

PackNode* SLPTree::BuildTreeRec(const ZoneVector<Node*>& node_group,
                                unsigned recursion_depth) {
  TRACE("Enter %s\n", __func__);
  DCHECK_EQ(node_group.size(), 2);

  Node* node0 = node_group[0];
  Node* node1 = node_group[1];

  if (recursion_depth == RecursionMaxDepth) {
    TRACE("Failed due to max recursion depth!\n");
    return nullptr;
  }

  if (AllOnStack(node_group)) {
    if (!StackTopIsPhi()) {
      TRACE("Failed due to (%d %s, %d %s) on stack!\n", node0->id(),
            node0->op()->mnemonic(), node1->id(), node1->op()->mnemonic());
      return nullptr;
    }
  }
  PushStack(node_group);

  if (!CanBePacked(node_group)) {
    return nullptr;
  }

  DCHECK(AllConstant(node_group) || AllPackableOperator(node_group) ||
         MaybePackSignExtensionOp(node_group));

  // Check if this is a duplicate of another entry.
  for (Node* node : node_group) {
    if (PackNode* p = GetPackNode(node)) {
      if (!p->IsSame(node_group)) {
        // TODO(jiepan): Gathering due to partial overlap
        TRACE("Failed due to partial overlap at #%d,%s!\n", node->id(),
              node->op()->mnemonic());
        return nullptr;
      }

      PopStack();
      TRACE("Perfect diamond merge at #%d,%s\n", node->id(),
            node->op()->mnemonic());
      return p;
    }
  }

  if (node0->opcode() == IrOpcode::kS128Zero) {
    PackNode* p = NewPackNode(node_group);
    PopStack();
    return p;
  }
  if (node0->opcode() == IrOpcode::kS128Const) {
    PackNode* p = NewPackNode(node_group);
    PopStack();
    return p;
  }
  if (node0->opcode() == IrOpcode::kExtractF128) {
    Node* source = node0->InputAt(0);
    TRACE("Extract leaf node from #%d,%s!\n", source->id(),
          source->op()->mnemonic());
    // For 256 only, check whether they are from the same source
    if (node0->InputAt(0) == node1->InputAt(0) &&
        (node0->InputAt(0)->opcode() == IrOpcode::kLoadTransform
             ? node0 == node1
             : OpParameter<int32_t>(node0->op()) + 1 ==
                   OpParameter<int32_t>(node1->op()))) {
      TRACE("Added a pair of Extract.\n");
      PackNode* pnode = NewPackNode(node_group);
      PopStack();
      return pnode;
    }
    TRACE("Failed due to ExtractF128!\n");
    return nullptr;
  }

  if (IsSupportedLoad(node0)) {
    TRACE("Load leaf node\n");
    if (!AllSameAddress(node_group)) {
      TRACE("Failed due to different load addr!\n");
      PopStack();
      return nullptr;
    }

    if (!IsSplat(node_group)) {
      if (node0->opcode() == IrOpcode::kProtectedLoad &&
          LoadRepresentationOf(node0->op()).representation() !=
              MachineRepresentation::kSimd128) {
        PopStack();
        return nullptr;
      }

      if (!IsSideEffectFreeLoad(node_group)) {
        TRACE("Failed due to dependency check\n");
        PopStack();
        return nullptr;
      }

      // Sort loads by offset
      ZoneVector<Node*> sorted_node_group(node_group.size(), zone_);
      std::partial_sort_copy(node_group.begin(), node_group.end(),
                             sorted_node_group.begin(), sorted_node_group.end(),
                             MemoryOffsetComparer());
      if (!IsContinuousAccess(sorted_node_group)) {
        TRACE("Failed due to non-continuous load!\n");
        PopStack();
        return nullptr;
      }
    } else if (node0->opcode() == IrOpcode::kLoadTransform) {
      LoadTransformParameters params = LoadTransformParametersOf(node0->op());
      if (params.transformation > LoadTransformation::kLast128Splat) {
        TRACE("LoadTransform failed due to unsupported type #%d!\n",
              node0->id());
        PopStack();
        return nullptr;
      }
      DCHECK_GE(params.transformation, LoadTransformation::kFirst128Splat);
    } else {
      TRACE("Failed due to unsupported splat!\n");
      PopStack();
      return nullptr;
    }

    PackNode* p = NewPackNode(node_group);
    PopStack();
    return p;
  }

  int value_in_count = node0->op()->ValueInputCount();

#define CASE(op128, op256) case IrOpcode::k##op128:
#define SIGN_EXTENSION_CASE(op_low, not_used1, not_used2) \
  case IrOpcode::k##op_low:
  switch (node0->opcode()) {
    case IrOpcode::kPhi: {
      TRACE("Added a vector of PHI nodes.\n");
      MachineRepresentation rep = PhiRepresentationOf(node0->op());
      if (rep != MachineRepresentation::kSimd128) {
        return nullptr;
      }
      PackNode* pnode =
          NewPackNodeAndRecurs(node_group, 0, value_in_count, recursion_depth);
      PopStack();
      return pnode;
    }
    case IrOpcode::kLoopExitValue: {
      MachineRepresentation rep = LoopExitValueRepresentationOf(node0->op());
      if (rep != MachineRepresentation::kSimd128) {
        return nullptr;
      }
      PackNode* pnode =
          NewPackNodeAndRecurs(node_group, 0, value_in_count, recursion_depth);
      PopStack();
      return pnode;
    }
    case IrOpcode::kI8x16Shuffle: {
      // Try match 32x8Splat or 64x4Splat.
      if (IsSplat(node_group)) {
        const uint8_t* shuffle = S128ImmediateParameterOf(node0->op()).data();
        int index;
        if ((wasm::SimdShuffle::TryMatchSplat<4>(shuffle, &index) &&
             node0->InputAt(index >> 2)->opcode() ==
                 IrOpcode::kProtectedLoad) ||
            (wasm::SimdShuffle::TryMatchSplat<2>(shuffle, &index) &&
             node0->InputAt(index >> 1)->opcode() ==
                 IrOpcode::kProtectedLoad)) {
          PopStack();
          return NewPackNode(node_group);
        }
        TRACE("Failed to match splat\n");
        PopStack();
        return nullptr;
      } else {
        PopStack();
        return NewPackNodeAndRecurs(node_group, 0, value_in_count,
                                    recursion_depth);
      }
    }
      // clang-format off
    SIMPLE_SIMD_OP(CASE) {
      TRACE("Added a vector of %s.\n", node0->op()->mnemonic());
      PackNode* pnode = NewPackNodeAndRecurs(node_group, 0, value_in_count,
                                              recursion_depth);
      PopStack();
      return pnode;
    }
    SIMD_SHIFT_OP(CASE) {
      if (ShiftBySameScalar(node_group)) {
        TRACE("Added a vector of %s.\n", node0->op()->mnemonic());
        PackNode* pnode =
            NewPackNodeAndRecurs(node_group, 0, 1, recursion_depth);
        PopStack();
        return pnode;
      }
      TRACE("Failed due to shift with different scalar!\n");
      return nullptr;
    }
    SIMD_SIGN_EXTENSION_CONVERT_OP(SIGN_EXTENSION_CASE) {
      TRACE("add a vector of sign extension op and stop building tree\n");
      PackNode* pnode = NewPackNode(node_group);
      PopStack();
      return pnode;
    }
    SIMD_SPLAT_OP(CASE) {
      TRACE("Added a vector of %s.\n", node0->op()->mnemonic());
      if (node0->InputAt(0) != node1->InputAt(0)) {
        TRACE("Failed due to different splat input");
        return nullptr;
      }
      PackNode* pnode = NewPackNode(node_group);
      PopStack();
      return pnode;
    }
    // clang-format on

    // TODO(jiepan): UnalignedStore, StoreTrapOnNull.
    case IrOpcode::kStore:
    case IrOpcode::kProtectedStore: {
      TRACE("Added a vector of stores.\n");
      if (!AllSameAddress(node_group)) {
        TRACE("Failed due to different store addr!\n");
        return nullptr;
      }
      PackNode* pnode = NewPackNodeAndRecurs(node_group, 2, 1, recursion_depth);
      PopStack();
      return pnode;
    }
    default:
      TRACE("Default branch #%d:%s\n", node0->id(), node0->op()->mnemonic());
      break;
  }
#undef CASE
#undef SIGN_EXTENSION_CASE
  return nullptr;
}

void SLPTree::DeleteTree() {
  ClearStack();
  node_to_packnode_.clear();
}

void SLPTree::Print(const char* info) {
  TRACE("%s, Packed node:\n", info);
  if (!v8_flags.trace_wasm_revectorize) {
    return;
  }

  ForEach([](PackNode const* pnode) { pnode->Print(); });
}

template <typename FunctionType>
void SLPTree::ForEach(FunctionType callback) {
  std::unordered_set<PackNode const*> visited;

  for (auto& entry : node_to_packnode_) {
    PackNode const* pnode = entry.second;
    if (!pnode || visited.find(pnode) != visited.end()) {
      continue;
    }
    visited.insert(pnode);

    callback(pnode);
  }
}

//////////////////////////////////////////////////////

Revectorizer::Revectorizer(Zone* zone, Graph* graph, MachineGraph* mcgraph,
                           SourcePositionTable* source_positions)
    : zone_(zone),
      graph_(graph),
      mcgraph_(mcgraph),
      group_of_stores_(zone),
      source_positions_(source_positions),
      support_simd256_(false) {
  DetectCPUFeatures();
  slp_tree_ = zone_->New<SLPTree>(zone, graph);
  Isolate* isolate = Isolate::TryGetCurrent();
  node_observer_for_test_ = isolate ? isolate->node_observer() : nullptr;
}

bool Revectorizer::DecideVectorize() {
  TRACE("Enter %s\n", __func__);

  int save = 0, cost = 0;
  slp_tree_->ForEach([&](PackNode const* pnode) {
    const ZoneVector<Node*>& nodes = pnode->Nodes();
    IrOpcode::Value op = nodes[0]->opcode();

    // Skip LoopExit as auxiliary nodes are not issued in generated code.
    // Skip Extract128 as we will reuse its revectorized input and no additional
    // extract nodes will be generated.
    if (op == IrOpcode::kLoopExitValue || op == IrOpcode::kExtractF128) {
      return;
    }
    // Splat nodes will not cause a saving as it simply extends itself.
    if (!IsSplat(nodes)) {
      save++;
    }

    for (size_t i = 0; i < nodes.size(); i++) {
      if (i > 0 && nodes[i] == nodes[0]) continue;

      for (auto edge : nodes[i]->use_edges()) {
        if (!NodeProperties::IsValueEdge(edge)) continue;
        Node* useNode = edge.from();
        if (!GetPackNode(useNode) && !(useNode->uses().empty()) &&
            useNode->opcode() != IrOpcode::kLoopExitValue) {
          TRACE("External use edge: (%d:%s) -> (%d:%s)\n", useNode->id(),
                useNode->op()->mnemonic(), nodes[i]->id(),
                nodes[i]->op()->mnemonic());
          cost++;

          // We only need one Extract node and all other uses can share.
          break;
        }
      }
    }
  });

  TRACE("Save: %d, cost: %d\n", save, cost);
  return save > cost;
}

void Revectorizer::SetEffectInput(PackNode* pnode, int index, Node*& input) {
  const ZoneVector<Node*>& nodes = pnode->Nodes();

  // We assumed there's no effect edge to the 3rd node inbetween.
  DCHECK(nodes[0] == nodes[1] ||
         NodeProperties::GetEffectInput(nodes[0]) == nodes[1] ||
         NodeProperties::GetEffectInput(nodes[1]) == nodes[0]);

  // Scanning till find the other effect outside pnode.
  for (size_t i = 0; i < nodes.size(); i++) {
    Node* node128 = nodes[i];
    PackNode* effect = GetPackNode(node128->InputAt(index));
    if (effect == pnode) continue;
    if (effect)
      pnode->SetOperand(index, effect);
    else
      input = node128->InputAt(index);
    break;
  }
}

void Revectorizer::SetMemoryOpInputs(base::SmallVector<Node*, 2>& inputs,
                                     PackNode* pnode, int effect_index) {
  Node* node = pnode->Nodes()[0];
  // Keep the addressing inputs
  inputs[0] = node->InputAt(0);
  inputs[1] = node->InputAt(1);
  // Set the effect input and the value input will be set later
  SetEffectInput(pnode, effect_index, inputs[effect_index]);
  // Set the control input
  inputs[effect_index + 1] = node->InputAt(effect_index + 1);
}

Node* Revectorizer::VectorizeTree(PackNode* pnode) {
  TRACE("Enter %s with PackNode\n", __func__);

  Node* node0 = pnode->Nodes()[0];
  Node* node1 = pnode->Nodes()[1];
  if (pnode->RevectorizedNode()) {
    TRACE("Diamond merged for #%d:%s\n", node0->id(), node0->op()->mnemonic());
    return pnode->RevectorizedNode();
  }

  int input_count = node0->InputCount();
  TRACE("Vectorize #%d:%s, input count: %d\n", node0->id(),
        node0->op()->mnemonic(), input_count);

  IrOpcode::Value op = node0->opcode();
  const Operator* new_op = nullptr;
  Node* source = nullptr;
  Node* dead = mcgraph()->Dead();
  base::SmallVector<Node*, 2> inputs(input_count);
  for (int i = 0; i < input_count; i++) inputs[i] = dead;

  switch (op) {
    case IrOpcode::kPhi: {
      DCHECK_EQ(PhiRepresentationOf(node0->op()),
                MachineRepresentation::kSimd128);
      new_op = mcgraph_->common()->Phi(MachineRepresentation::kSimd256,
                                       input_count - 1);
      inputs[input_count - 1] = NodeProperties::GetControlInput(node0);
      break;
    }
    case IrOpcode::kLoopExitValue: {
      DCHECK_EQ(LoopExitValueRepresentationOf(node0->op()),
                MachineRepresentation::kSimd128);
      new_op =
          mcgraph_->common()->LoopExitValue(MachineRepresentation::kSimd256);
      inputs[input_count - 1] = NodeProperties::GetControlInput(node0);
      break;
    }
#define SIMPLE_CASE(from, to)           \
  case IrOpcode::k##from:               \
    new_op = mcgraph_->machine()->to(); \
    break;
      SIMPLE_SIMD_OP(SIMPLE_CASE)
#undef SIMPLE_CASE
#undef SIMPLE_SIMD_OP

#define SHIFT_CASE(from, to)                   \
  case IrOpcode::k##from: {                    \
    DCHECK(ShiftBySameScalar(pnode->Nodes())); \
    new_op = mcgraph_->machine()->to();        \
    inputs[1] = node0->InputAt(1);             \
    break;                                     \
  }
      SIMD_SHIFT_OP(SHIFT_CASE)
#undef SHIFT_CASE
#undef SIMD_SHIFT_OP

#define SIGN_EXTENSION_CONVERT_CASE(from, not_used, to)          \
  case IrOpcode::k##from: {                                      \
    DCHECK_EQ(node0->InputAt(0), pnode->Nodes()[1]->InputAt(0)); \
    new_op = mcgraph_->machine()->to();                          \
    inputs[0] = node0->InputAt(0);                               \
    break;                                                       \
  }
      SIMD_SIGN_EXTENSION_CONVERT_OP(SIGN_EXTENSION_CONVERT_CASE)
#undef SIGN_EXTENSION_CONVERT_CASE
#undef SIMD_SIGN_EXTENSION_CONVERT_OP

#define SPLAT_CASE(from, to)            \
  case IrOpcode::k##from:               \
    new_op = mcgraph_->machine()->to(); \
    inputs[0] = node0->InputAt(0);      \
    break;
      SIMD_SPLAT_OP(SPLAT_CASE)
#undef SPLAT_CASE
#undef SIMD_SPLAT_OP
    case IrOpcode::kI8x16Shuffle: {
      // clang-format off
      if (IsSplat(pnode->Nodes())) {
        const uint8_t* shuffle = S128ImmediateParameterOf(node0->op()).data();
        int index, offset;

        // Match Splat and Revectorize to LoadSplat as AVX-256 does not support
        // shuffling across 128-bit lane.
        if (wasm::SimdShuffle::TryMatchSplat<4>(shuffle, &index)) {
          new_op = mcgraph_->machine()->LoadTransform(
              MemoryAccessKind::kProtected,
              LoadTransformation::kS256Load32Splat);
          offset = index * 4;
        } else if (wasm::SimdShuffle::TryMatchSplat<2>(shuffle, &index)) {
          new_op = mcgraph_->machine()->LoadTransform(
              MemoryAccessKind::kProtected,
              LoadTransformation::kS256Load64Splat);
          offset = index * 8;
        } else {
          UNREACHABLE();
        }

        source = node0->InputAt(offset >> 4);
        DCHECK_EQ(source->opcode(), IrOpcode::kProtectedLoad);
        inputs.resize_no_init(4);
        // Update LoadSplat offset.
        if (index) {
          SourcePositionTable::Scope scope(source_positions_, source);
          inputs[0] = graph()->NewNode(mcgraph_->machine()->Int64Add(),
                                       source->InputAt(0),
                                       mcgraph_->Int64Constant(offset));
        } else {
          inputs[0] = source->InputAt(0);
        }
        // Keep source index, effect and control inputs.
        inputs[1] = source->InputAt(1);
        inputs[2] = source->InputAt(2);
        inputs[3] = source->InputAt(3);
        input_count = 4;
      } else {
        const uint8_t* shuffle0 = S128ImmediateParameterOf(node0->op()).data();
        const uint8_t* shuffle1 = S128ImmediateParameterOf(node1->op()).data();
        uint8_t new_shuffle[32];

        if (node0->InputAt(0) == node0->InputAt(1) &&
            node1->InputAt(0) == node1->InputAt(1)) {
          // Shuffle is Swizzle
          for (int i = 0; i < 16; ++i) {
            new_shuffle[i] = shuffle0[i] % 16;
            new_shuffle[i + 16] = 16 + shuffle1[i] % 16;
          }
        } else {
          for (int i = 0; i < 16; ++i) {
            if (shuffle0[i] < 16) {
              new_shuffle[i] = shuffle0[i];
            } else {
              new_shuffle[i] = 16 + shuffle0[i];
            }

            if (shuffle1[i] < 16) {
              new_shuffle[i + 16] = 16 + shuffle1[i];
            } else {
              new_shuffle[i + 16] = 32 + shuffle1[i];
            }
          }
        }
        new_op = mcgraph_->machine()->I8x32Shuffle(new_shuffle);
      }
      break;
      // clang-format on
    }
    case IrOpcode::kS128Zero: {
      new_op = mcgraph_->machine()->S256Zero();
      break;
    }
    case IrOpcode::kS128Const: {
      uint8_t value[32];
      const uint8_t* value0 = S128ImmediateParameterOf(node0->op()).data();
      const uint8_t* value1 = S128ImmediateParameterOf(node1->op()).data();
      for (int i = 0; i < kSimd128Size; ++i) {
        value[i] = value0[i];
        value[i + 16] = value1[i];
      }
      new_op = mcgraph_->machine()->S256Const(value);
      break;
    }
    case IrOpcode::kProtectedLoad: {
      DCHECK_EQ(LoadRepresentationOf(node0->op()).representation(),
                MachineRepresentation::kSimd128);
      new_op = mcgraph_->machine()->ProtectedLoad(MachineType::Simd256());
      SetMemoryOpInputs(inputs, pnode, 2);
      break;
    }
    case IrOpcode::kLoad: {
      DCHECK_EQ(LoadRepresentationOf(node0->op()).representation(),
                MachineRepresentation::kSimd128);
      new_op = mcgraph_->machine()->Load(MachineType::Simd256());
      SetMemoryOpInputs(inputs, pnode, 2);
      break;
    }
    case IrOpcode::kProtectedStore: {
      DCHECK_EQ(StoreRepresentationOf(node0->op()).representation(),
                MachineRepresentation::kSimd128);
      new_op =
          mcgraph_->machine()->ProtectedStore(MachineRepresentation::kSimd256);
      SetMemoryOpInputs(inputs, pnode, 3);
      break;
    }
    case IrOpcode::kStore: {
      DCHECK_EQ(StoreRepresentationOf(node0->op()).representation(),
                MachineRepresentation::kSimd128);
      WriteBarrierKind write_barrier_kind =
          StoreRepresentationOf(node0->op()).write_barrier_kind();
      new_op = mcgraph_->machine()->Store(StoreRepresentation(
          MachineRepresentation::kSimd256, write_barrier_kind));
      SetMemoryOpInputs(inputs, pnode, 3);
      break;
    }
    case IrOpcode::kLoadTransform: {
      LoadTransformParameters params = LoadTransformParametersOf(node0->op());
      LoadTransformation new_transformation;

      // clang-format off
      switch (params.transformation) {
        case LoadTransformation::kS128Load8Splat:
          new_transformation = LoadTransformation::kS256Load8Splat;
          break;
        case LoadTransformation::kS128Load16Splat:
          new_transformation = LoadTransformation::kS256Load16Splat;
          break;
        case LoadTransformation::kS128Load32Splat:
          new_transformation = LoadTransformation::kS256Load32Splat;
          break;
        case LoadTransformation::kS128Load64Splat:
          new_transformation = LoadTransformation::kS256Load64Splat;
          break;
        case LoadTransformation::kS128Load8x8S:
          new_transformation = LoadTransformation::kS256Load8x16S;
          break;
        case LoadTransformation::kS128Load8x8U:
          new_transformation = LoadTransformation::kS256Load8x16U;
          break;
        case LoadTransformation::kS128Load16x4S:
          new_transformation = LoadTransformation::kS256Load16x8S;
          break;
        case LoadTransformation::kS128Load16x4U:
          new_transformation = LoadTransformation::kS256Load16x8U;
          break;
        case LoadTransformation::kS128Load32x2S:
          new_transformation = LoadTransformation::kS256Load32x4S;
          break;
        case LoadTransformation::kS128Load32x2U:
          new_transformation = LoadTransformation::kS256Load32x4U;
          break;
        default:
          UNREACHABLE();
      }
      // clang-format on

      new_op =
          mcgraph_->machine()->LoadTransform(params.kind, new_transformation);
      SetMemoryOpInputs(inputs, pnode, 2);
      break;
    }
    case IrOpcode::kExtractF128: {
      pnode->SetRevectorizedNode(node0->InputAt(0));
      // The extract uses other than its parent don't need to change.
      break;
    }
    default:
      UNREACHABLE();
  }

  DCHECK(pnode->RevectorizedNode() || new_op);
  if (new_op != nullptr) {
    SourcePositionTable::Scope scope(source_positions_, node0);
    Node* new_node =
        graph()->NewNode(new_op, input_count, inputs.begin(), true);
    pnode->SetRevectorizedNode(new_node);
    for (int i = 0; i < input_count; i++) {
      if (inputs[i] == dead) {
        new_node->ReplaceInput(i, VectorizeTree(pnode->GetOperand(i)));
      }
    }
    // Extract Uses
    const ZoneVector<Node*>& nodes = pnode->Nodes();
    for (size_t i = 0; i < nodes.size(); i++) {
      if (i > 0 && nodes[i] == nodes[i - 1]) continue;
      Node* input_128 = nullptr;
      for (auto edge : nodes[i]->use_edges()) {
        Node* useNode = edge.from();
        if (!GetPackNode(useNode)) {
          if (NodeProperties::IsValueEdge(edge)) {
            // Extract use
            TRACE("Replace Value Edge from %d:%s, to %d:%s\n", useNode->id(),
                  useNode->op()->mnemonic(), edge.to()->id(),
                  edge.to()->op()->mnemonic());

            if (!input_128) {
              TRACE("Create ExtractF128(%lu) node from #%d\n", i,
                    new_node->id());
              input_128 = graph()->NewNode(
                  mcgraph()->machine()->ExtractF128(static_cast<int32_t>(i)),
                  new_node);
            }
            edge.UpdateTo(input_128);
          } else if (NodeProperties::IsEffectEdge(edge)) {
            TRACE("Replace Effect Edge from %d:%s, to %d:%s\n", useNode->id(),
                  useNode->op()->mnemonic(), edge.to()->id(),
                  edge.to()->op()->mnemonic());

            edge.UpdateTo(new_node);
          }
        }
      }
      if (nodes[i]->uses().empty()) nodes[i]->Kill();
    }

    // Update effect use of NewNode from the dependent source.
    if (op == IrOpcode::kI8x16Shuffle && IsSplat(nodes)) {
      DCHECK(source);
      NodeProperties::ReplaceEffectInput(source, new_node, 0);
      TRACE("Replace Effect Edge from %d:%s, to %d:%s\n", source->id(),
            source->op()->mnemonic(), new_node->id(),
            new_node->op()->mnemonic());
      // Remove unused value use, so that we can safely elimite the node later.
      NodeProperties::ReplaceValueInput(node0, dead, 0);
      NodeProperties::ReplaceValueInput(node0, dead, 1);
      TRACE("Remove Value Input of %d:%s\n", node0->id(),
            node0->op()->mnemonic());

      // We will try cleanup source nodes later
      sources_.insert(source);
    }
  }

  return pnode->RevectorizedNode();
}

void Revectorizer::DetectCPUFeatures() {
  base::CPU cpu;
  if (v8_flags.enable_avx && v8_flags.enable_avx2 && cpu.has_avx2()) {
    support_simd256_ = true;
  }
}

bool Revectorizer::TryRevectorize(const char* function) {
  source_positions_->AddDecorator();

  bool success = false;
  if (support_simd256_ && graph_->GetSimdStoreNodes().size()) {
    TRACE("TryRevectorize %s\n", function);
    CollectSeeds();
    for (auto entry : group_of_stores_) {
      ZoneMap<Node*, StoreNodeSet>* store_chains = entry.second;
      if (store_chains != nullptr) {
        PrintStores(store_chains);
        if (ReduceStoreChains(store_chains)) {
          TRACE("Successful revectorize %s\n", function);
          success = true;
        }
      }
    }
    TRACE("Finish revectorize %s\n", function);
  }
  source_positions_->RemoveDecorator();
  return success;
}

void Revectorizer::UpdateSources() {
  for (auto* src : sources_) {
    std::vector<Node*> effect_uses;
    bool hasExternalValueUse = false;
    for (auto edge : src->use_edges()) {
      Node* use = edge.from();
      if (!GetPackNode(use)) {
        if (NodeProperties::IsValueEdge(edge)) {
          TRACE("Source node has external value dependence %d:%s\n",
                edge.from()->id(), edge.from()->op()->mnemonic());
          hasExternalValueUse = true;
          break;
        } else if (NodeProperties::IsEffectEdge(edge)) {
          effect_uses.push_back(use);
        }
      }
    }

    if (!hasExternalValueUse) {
      // Remove unused source and linearize effect chain.
      Node* effect = NodeProperties::GetEffectInput(src);
      for (auto use : effect_uses) {
        TRACE("Replace Effect Edge for source node from %d:%s, to %d:%s\n",
              use->id(), use->op()->mnemonic(), effect->id(),
              effect->op()->mnemonic());
        NodeProperties::ReplaceEffectInput(use, effect, 0);
      }
    }
  }

  sources_.clear();
}

void Revectorizer::CollectSeeds() {
  for (auto it = graph_->GetSimdStoreNodes().begin();
       it != graph_->GetSimdStoreNodes().end(); ++it) {
    Node* node = *it;
    Node* dominator = slp_tree_->GetEarlySchedulePosition(node);

    if ((GetMemoryOffsetValue(node) % kSimd128Size) != 0) {
      continue;
    }
    Node* address = GetNodeAddress(node);
    ZoneMap<Node*, StoreNodeSet>* store_nodes;
    auto first_level_iter = group_of_stores_.find(dominator);
    if (first_level_iter == group_of_stores_.end()) {
      store_nodes = zone_->New<ZoneMap<Node*, StoreNodeSet>>(zone_);
      group_of_stores_[dominator] = store_nodes;
    } else {
      store_nodes = first_level_iter->second;
    }
    auto second_level_iter = store_nodes->find(address);
    if (second_level_iter == store_nodes->end()) {
      second_level_iter =
          store_nodes->insert({address, StoreNodeSet(zone())}).first;
    }
    second_level_iter->second.insert(node);
  }
}

bool Revectorizer::ReduceStoreChains(
    ZoneMap<Node*, StoreNodeSet>* store_chains) {
  TRACE("Enter %s\n", __func__);
  bool changed = false;
  for (auto chain_iter = store_chains->cbegin();
       chain_iter != store_chains->cend(); ++chain_iter) {
    if (chain_iter->second.size() >= 2 && chain_iter->second.size() % 2 == 0) {
      ZoneVector<Node*> store_chain(chain_iter->second.begin(),
                                    chain_iter->second.end(), zone_);
      for (auto it = store_chain.begin(); it < store_chain.end(); it = it + 2) {
        ZoneVector<Node*> stores_unit(it, it + 2, zone_);
        if ((NodeProperties::GetEffectInput(stores_unit[0]) == stores_unit[1] ||
             NodeProperties::GetEffectInput(stores_unit[1]) ==
                 stores_unit[0]) &&
            ReduceStoreChain(stores_unit)) {
          changed = true;
        }
      }
    }
  }

  return changed;
}

bool Revectorizer::ReduceStoreChain(const ZoneVector<Node*>& Stores) {
  TRACE("Enter %s, root@ (#%d,#%d)\n", __func__, Stores[0]->id(),
        Stores[1]->id());
  if (!IsContinuousAccess(Stores)) {
    return false;
  }

  PackNode* root = slp_tree_->BuildTree(Stores);
  if (!root) {
    TRACE("Build tree failed!\n");
    return false;
  }

  slp_tree_->Print("After build tree");

  if (DecideVectorize()) {
    VectorizeTree(root);
    UpdateSources();
    slp_tree_->Print("After vectorize tree");

    if (node_observer_for_test_) {
      slp_tree_->ForEach([&](const PackNode* pnode) {
        Node* node = pnode->RevectorizedNode();
        if (node) {
          node_observer_for_test_->OnNodeCreated(node);
        }
      });
    }
  }

  TRACE("\n");
  return true;
}

void Revectorizer::PrintStores(ZoneMap<Node*, StoreNodeSet>* store_chains) {
  if (!v8_flags.trace_wasm_revectorize) {
    return;
  }
  TRACE("Enter %s\n", __func__);
  for (auto it = store_chains->cbegin(); it != store_chains->cend(); ++it) {
    if (it->second.size() > 0) {
      TRACE("address = #%d:%s \n", it->first->id(),
            it->first->op()->mnemonic());

      for (auto node : it->second) {
        TRACE("#%d:%s, ", node->id(), node->op()->mnemonic());
      }

      TRACE("\n");
    }
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
