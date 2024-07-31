// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/decompression-optimizer.h"

#include "src/compiler/graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool IsMachineLoad(Node* const node) {
  const IrOpcode::Value opcode = node->opcode();
  return opcode == IrOpcode::kLoad || opcode == IrOpcode::kProtectedLoad ||
         opcode == IrOpcode::kLoadTrapOnNull ||
         opcode == IrOpcode::kUnalignedLoad ||
         opcode == IrOpcode::kLoadImmutable;
}

bool IsTaggedMachineLoad(Node* const node) {
  return IsMachineLoad(node) &&
         CanBeTaggedPointer(LoadRepresentationOf(node->op()).representation());
}

bool IsHeapConstant(Node* const node) {
  return node->opcode() == IrOpcode::kHeapConstant;
}

bool IsIntConstant(Node* const node) {
  return node->opcode() == IrOpcode::kInt32Constant ||
         node->opcode() == IrOpcode::kInt64Constant;
}

bool IsTaggedPhi(Node* const node) {
  if (node->opcode() == IrOpcode::kPhi) {
    return CanBeTaggedPointer(PhiRepresentationOf(node->op()));
  }
  return false;
}

bool IsWord64BitwiseOp(Node* const node) {
  return node->opcode() == IrOpcode::kWord64And ||
         node->opcode() == IrOpcode::kWord64Or;
}

bool CanBeCompressed(Node* const node) {
  return IsHeapConstant(node) || IsTaggedMachineLoad(node) ||
         IsTaggedPhi(node) || IsWord64BitwiseOp(node);
}

void Replace(Node* const node, Node* const replacement) {
  for (Edge edge : node->use_edges()) {
    edge.UpdateTo(replacement);
  }
  node->Kill();
}

}  // anonymous namespace

DecompressionOptimizer::DecompressionOptimizer(Zone* zone, Graph* graph,
                                               CommonOperatorBuilder* common,
                                               MachineOperatorBuilder* machine)
    : graph_(graph),
      common_(common),
      machine_(machine),
      states_(graph, static_cast<uint32_t>(State::kNumberOfStates)),
      to_visit_(zone),
      compressed_candidate_nodes_(zone) {}

void DecompressionOptimizer::MarkNodes() {
  MaybeMarkAndQueueForRevisit(graph()->end(), State::kOnly32BitsObserved);
  while (!to_visit_.empty()) {
    Node* const node = to_visit_.front();
    to_visit_.pop_front();
    MarkNodeInputs(node);
  }
}

void DecompressionOptimizer::MarkNodeInputs(Node* node) {
  // Mark the value inputs.
  switch (node->opcode()) {
    // UNOPS.
    case IrOpcode::kBitcastTaggedToWord:
    case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits:
    case IrOpcode::kBitcastWordToTagged:
      // Replicate the bitcast's state for its input.
      DCHECK_EQ(node->op()->ValueInputCount(), 1);
      MaybeMarkAndQueueForRevisit(node->InputAt(0),
                                  states_.Get(node));  // value
      break;
    case IrOpcode::kTruncateInt64ToInt32:
      DCHECK_EQ(node->op()->ValueInputCount(), 1);
      MaybeMarkAndQueueForRevisit(node->InputAt(0),
                                  State::kOnly32BitsObserved);  // value
      break;
    // BINOPS.
    case IrOpcode::kInt32LessThan:
    case IrOpcode::kInt32LessThanOrEqual:
    case IrOpcode::kUint32LessThan:
    case IrOpcode::kUint32LessThanOrEqual:
    case IrOpcode::kWord32Equal:
#define Word32Op(Name) case IrOpcode::k##Name:
      MACHINE_BINOP_32_LIST(Word32Op)
#undef Word32Op
      DCHECK_EQ(node->op()->ValueInputCount(), 2);
      MaybeMarkAndQueueForRevisit(node->InputAt(0),
                                  State::kOnly32BitsObserved);  // value_0
      MaybeMarkAndQueueForRevisit(node->InputAt(1),
                                  State::kOnly32BitsObserved);  // value_1
      break;
    // SPECIAL CASES.
    // SPECIAL CASES - Load.
    case IrOpcode::kLoad:
    case IrOpcode::kProtectedLoad:
    case IrOpcode::kLoadTrapOnNull:
    case IrOpcode::kUnalignedLoad:
    case IrOpcode::kLoadImmutable:
      DCHECK_EQ(node->op()->ValueInputCount(), 2);
      // Mark addressing base pointer in compressed form to allow pointer
      // decompression via complex addressing mode.
      if (DECOMPRESS_POINTER_BY_ADDRESSING_MODE &&
          node->InputAt(0)->OwnedBy(node) && IsIntConstant(node->InputAt(1))) {
        MarkAddressingBase(node->InputAt(0));
      } else {
        MaybeMarkAndQueueForRevisit(
            node->InputAt(0),
            State::kEverythingObserved);  // base pointer
        MaybeMarkAndQueueForRevisit(node->InputAt(1),
                                    State::kEverythingObserved);  // index
      }
      break;
    // SPECIAL CASES - Store.
    case IrOpcode::kStore:
    case IrOpcode::kStorePair:
    case IrOpcode::kProtectedStore:
    case IrOpcode::kStoreTrapOnNull:
    case IrOpcode::kUnalignedStore: {
      DCHECK(node->op()->ValueInputCount() == 3 ||
             (node->opcode() == IrOpcode::kStorePair &&
              node->op()->ValueInputCount() == 4));
      MaybeMarkAndQueueForRevisit(node->InputAt(0),
                                  State::kEverythingObserved);  // base pointer
      MaybeMarkAndQueueForRevisit(node->InputAt(1),
                                  State::kEverythingObserved);  // index
      // TODO(v8:7703): When the implementation is done, check if this ternary
      // operator is too restrictive, since we only mark Tagged stores as 32
      // bits.
      MachineRepresentation representation;
      if (node->opcode() == IrOpcode::kUnalignedStore) {
        representation = UnalignedStoreRepresentationOf(node->op());
      } else if (node->opcode() == IrOpcode::kStorePair) {
        representation =
            StorePairRepresentationOf(node->op()).first.representation();
      } else {
        representation = StoreRepresentationOf(node->op()).representation();
      }
      State observed = ElementSizeLog2Of(representation) <= 2
                           ? State::kOnly32BitsObserved
                           : State::kEverythingObserved;

      // We should never see indirect pointer stores here since they need
      // kStoreIndirect. For indirect pointer stores we always need all pointer
      // bits since we'll also perform a load (of the 'self' indirect pointer)
      // from the value being stored.
      DCHECK_NE(representation, MachineRepresentation::kIndirectPointer);

      MaybeMarkAndQueueForRevisit(node->InputAt(2), observed);  // value
      if (node->opcode() == IrOpcode::kStorePair) {
        MaybeMarkAndQueueForRevisit(node->InputAt(3), observed);  // value 2
      }
    } break;
    // SPECIAL CASES - Variable inputs.
    // The deopt code knows how to handle Compressed inputs, both
    // MachineRepresentation kCompressed values and CompressedHeapConstants.
    case IrOpcode::kFrameState:  // Fall through.
    case IrOpcode::kStateValues:
      for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
        // TODO(chromium:1470602): We assume that kStateValues has only tagged
        // inputs so it is safe to mark them as kOnly32BitsObserved.
        DCHECK(!IsWord64BitwiseOp(node->InputAt(i)));
        MaybeMarkAndQueueForRevisit(node->InputAt(i),
                                    State::kOnly32BitsObserved);
      }
      break;
    case IrOpcode::kTypedStateValues: {
      const ZoneVector<MachineType>* machine_types = MachineTypesOf(node->op());
      for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
        State observed = IsAnyTagged(machine_types->at(i).representation())
                             ? State::kOnly32BitsObserved
                             : State::kEverythingObserved;
        MaybeMarkAndQueueForRevisit(node->InputAt(i), observed);
      }
      break;
    }
    case IrOpcode::kPhi: {
      // Replicate the phi's state for its inputs.
      State curr_state = states_.Get(node);
      for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
        MaybeMarkAndQueueForRevisit(node->InputAt(i), curr_state);
      }
      break;
    }
    default:
      // To be conservative, we assume that all value inputs need to be 64 bits
      // unless noted otherwise.
      for (int i = 0; i < node->op()->ValueInputCount(); ++i) {
        MaybeMarkAndQueueForRevisit(node->InputAt(i),
                                    State::kEverythingObserved);
      }
      break;
  }

  // We always mark the non-value input nodes as kOnly32BitsObserved so that
  // they will be visited. If they need to be kEverythingObserved, they will be
  // marked as such in a future pass.
  for (int i = node->op()->ValueInputCount(); i < node->InputCount(); ++i) {
    MaybeMarkAndQueueForRevisit(node->InputAt(i), State::kOnly32BitsObserved);
  }
}

// We mark the addressing base pointer as kOnly32BitsObserved so it can be
// optimized to compressed form. This allows us to move the decompression to
// use-site on X64.
void DecompressionOptimizer::MarkAddressingBase(Node* base) {
  if (IsTaggedMachineLoad(base)) {
    MaybeMarkAndQueueForRevisit(base,
                                State::kOnly32BitsObserved);  // base pointer
  } else if (IsTaggedPhi(base)) {
    bool should_compress = true;
    for (int i = 0; i < base->op()->ValueInputCount(); ++i) {
      if (!IsTaggedMachineLoad(base->InputAt(i)) ||
          !base->InputAt(i)->OwnedBy(base)) {
        should_compress = false;
        break;
      }
    }
    MaybeMarkAndQueueForRevisit(
        base,
        should_compress ? State::kOnly32BitsObserved
                        : State::kEverythingObserved);  // base pointer
  } else {
    MaybeMarkAndQueueForRevisit(base,
                                State::kEverythingObserved);  // base pointer
  }
}

void DecompressionOptimizer::MaybeMarkAndQueueForRevisit(Node* const node,
                                                         State state) {
  DCHECK_NE(state, State::kUnvisited);
  State previous_state = states_.Get(node);
  // Only update the state if we have relevant new information.
  if (previous_state == State::kUnvisited ||
      (previous_state == State::kOnly32BitsObserved &&
       state == State::kEverythingObserved)) {
    states_.Set(node, state);
    to_visit_.push_back(node);

    if (state == State::kOnly32BitsObserved && CanBeCompressed(node)) {
      compressed_candidate_nodes_.push_back(node);
    }
  }
}

void DecompressionOptimizer::ChangeHeapConstant(Node* const node) {
  DCHECK(IsHeapConstant(node));
  NodeProperties::ChangeOp(
      node, common()->CompressedHeapConstant(HeapConstantOf(node->op())));
}

void DecompressionOptimizer::ChangePhi(Node* const node) {
  DCHECK(IsTaggedPhi(node));

  MachineRepresentation mach_rep = PhiRepresentationOf(node->op());
  if (mach_rep == MachineRepresentation::kTagged) {
    mach_rep = MachineRepresentation::kCompressed;
  } else {
    DCHECK_EQ(mach_rep, MachineRepresentation::kTaggedPointer);
    mach_rep = MachineRepresentation::kCompressedPointer;
  }

  NodeProperties::ChangeOp(
      node, common()->Phi(mach_rep, node->op()->ValueInputCount()));
}

void DecompressionOptimizer::ChangeLoad(Node* const node) {
  DCHECK(IsMachineLoad(node));
  // Change to a Compressed MachRep to avoid the full decompression.
  LoadRepresentation load_rep = LoadRepresentationOf(node->op());
  LoadRepresentation compressed_load_rep;
  if (load_rep == MachineType::AnyTagged()) {
    compressed_load_rep = MachineType::AnyCompressed();
  } else {
    DCHECK_EQ(load_rep, MachineType::TaggedPointer());
    compressed_load_rep = MachineType::CompressedPointer();
  }

  // Change to the Operator with the Compressed MachineRepresentation.
  switch (node->opcode()) {
    case IrOpcode::kLoad:
      NodeProperties::ChangeOp(node, machine()->Load(compressed_load_rep));
      break;
    case IrOpcode::kLoadImmutable:
      NodeProperties::ChangeOp(node,
                               machine()->LoadImmutable(compressed_load_rep));
      break;
    case IrOpcode::kProtectedLoad:
      NodeProperties::ChangeOp(node,
                               machine()->ProtectedLoad(compressed_load_rep));
      break;
    case IrOpcode::kLoadTrapOnNull:
      NodeProperties::ChangeOp(node,
                               machine()->LoadTrapOnNull(compressed_load_rep));
      break;
    case IrOpcode::kUnalignedLoad:
      NodeProperties::ChangeOp(node,
                               machine()->UnalignedLoad(compressed_load_rep));
      break;
    default:
      UNREACHABLE();
  }
}

void DecompressionOptimizer::ChangeWord64BitwiseOp(Node* const node,
                                                   const Operator* new_op) {
  Int64Matcher mleft(node->InputAt(0));
  Int64Matcher mright(node->InputAt(1));

  // Replace inputs.
  if (mleft.IsChangeInt32ToInt64() || mleft.IsChangeUint32ToUint64()) {
    node->ReplaceInput(0, mleft.node()->InputAt(0));
  } else if (mleft.IsInt64Constant()) {
    node->ReplaceInput(0, graph()->NewNode(common()->Int32Constant(
                              static_cast<int32_t>(mleft.ResolvedValue()))));
  } else {
    node->ReplaceInput(
        0, graph()->NewNode(machine()->TruncateInt64ToInt32(), mleft.node()));
  }
  if (mright.IsChangeInt32ToInt64() || mright.IsChangeUint32ToUint64()) {
    node->ReplaceInput(1, mright.node()->InputAt(0));
  } else if (mright.IsInt64Constant()) {
    node->ReplaceInput(1, graph()->NewNode(common()->Int32Constant(
                              static_cast<int32_t>(mright.ResolvedValue()))));
  } else {
    node->ReplaceInput(
        1, graph()->NewNode(machine()->TruncateInt64ToInt32(), mright.node()));
  }

  // Replace uses.
  Node* replacement = nullptr;
  for (Edge edge : node->use_edges()) {
    Node* user = edge.from();
    if (user->opcode() == IrOpcode::kTruncateInt64ToInt32) {
      Replace(user, node);
    } else {
      if (replacement == nullptr) {
        replacement =
            graph()->NewNode(machine()->BitcastWord32ToWord64(), node);
      }
      edge.UpdateTo(replacement);
    }
  }

  // Change operator.
  NodeProperties::ChangeOp(node, new_op);
}

void DecompressionOptimizer::ChangeNodes() {
  for (Node* const node : compressed_candidate_nodes_) {
    // compressed_candidate_nodes_ contains all the nodes that once had the
    // State::kOnly32BitsObserved. If we later updated the state to be
    // State::IsEverythingObserved, then we have to ignore them. This is less
    // costly than removing them from the compressed_candidate_nodes_ NodeVector
    // when we update them to State::IsEverythingObserved.
    if (IsEverythingObserved(node)) continue;

    switch (node->opcode()) {
      case IrOpcode::kHeapConstant:
        ChangeHeapConstant(node);
        break;
      case IrOpcode::kPhi:
        ChangePhi(node);
        break;
      case IrOpcode::kWord64And:
        ChangeWord64BitwiseOp(node, machine()->Word32And());
        break;
      case IrOpcode::kWord64Or:
        ChangeWord64BitwiseOp(node, machine()->Word32Or());
        break;
      default:
        ChangeLoad(node);
        break;
    }
  }
}

void DecompressionOptimizer::Reduce() {
  MarkNodes();
  ChangeNodes();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
