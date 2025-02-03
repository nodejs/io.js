// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/int64-lowering.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/diamond.h"
#include "src/compiler/graph.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/wasm-call-descriptors.h"
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-engine.h"
// TODO(wasm): Remove this include.
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/zone.h"

#if V8_TARGET_ARCH_32_BIT

namespace v8 {
namespace internal {
namespace compiler {

Int64Lowering::Int64Lowering(Graph* graph, MachineOperatorBuilder* machine,
                             CommonOperatorBuilder* common,
                             SimplifiedOperatorBuilder* simplified, Zone* zone,
                             Signature<MachineRepresentation>* signature)
    : graph_(graph),
      machine_(machine),
      common_(common),
      simplified_(simplified),
      zone_(zone),
      signature_(signature),
      state_(graph->NodeCount(), State::kUnvisited),
      stack_(zone),
      replacements_(nullptr),
      placeholder_(graph->NewNode(common->Dead())) {
  DCHECK_NOT_NULL(graph);
  DCHECK_NOT_NULL(graph->end());
  replacements_ = zone->AllocateArray<Replacement>(graph->NodeCount());
  memset(replacements_, 0, sizeof(Replacement) * graph->NodeCount());
}

void Int64Lowering::LowerGraph() {
  stack_.push_back({graph()->end(), 0});
  state_[graph()->end()->id()] = State::kOnStack;

  while (!stack_.empty()) {
    NodeState& top = stack_.back();
    if (top.input_index == top.node->InputCount()) {
      // All inputs of top have already been lowered, now lower top.
      Node* node = top.node;
      stack_.pop_back();
      state_[node->id()] = State::kVisited;
      LowerNode(node);
    } else {
      // Push the next input onto the stack.
      Node* input = top.node->InputAt(top.input_index++);
      if (state_[input->id()] == State::kUnvisited) {
        if (input->opcode() == IrOpcode::kPhi) {
          // To break cycles with phi nodes we push phis on a separate stack so
          // that they are processed after all other nodes.
          PreparePhiReplacement(input);
          stack_.push_front({input, 0});
        } else if (input->opcode() == IrOpcode::kEffectPhi ||
                   input->opcode() == IrOpcode::kLoop) {
          stack_.push_front({input, 0});
        } else {
          stack_.push_back({input, 0});
        }
        state_[input->id()] = State::kOnStack;
      }
    }
  }
}

namespace {

int GetReturnIndexAfterLowering(const CallDescriptor* call_descriptor,
                                int old_index) {
  int result = old_index;
  for (int i = 0; i < old_index; i++) {
    if (call_descriptor->GetReturnType(i).representation() ==
        MachineRepresentation::kWord64) {
      result++;
    }
  }
  return result;
}

int GetReturnCountAfterLowering(const CallDescriptor* call_descriptor) {
  return GetReturnIndexAfterLowering(
      call_descriptor, static_cast<int>(call_descriptor->ReturnCount()));
}

int GetParameterIndexAfterLowering(
    Signature<MachineRepresentation>* signature, int old_index) {
  int result = old_index;
  // Be robust towards special indexes (>= param count).
  int max_to_check =
      std::min(old_index, static_cast<int>(signature->parameter_count()));
  for (int i = 0; i < max_to_check; i++) {
    if (signature->GetParam(i) == MachineRepresentation::kWord64) {
      result++;
    }
  }
  return result;
}

int GetReturnCountAfterLowering(Signature<MachineRepresentation>* signature) {
  int result = static_cast<int>(signature->return_count());
  for (int i = 0; i < static_cast<int>(signature->return_count()); i++) {
    if (signature->GetReturn(i) == MachineRepresentation::kWord64) {
      result++;
    }
  }
  return result;
}

}  // namespace

void Int64Lowering::LowerWord64AtomicBinop(Node* node, const Operator* op) {
  DCHECK_EQ(5, node->InputCount());
  LowerMemoryBaseAndIndex(node);
  Node* value = node->InputAt(2);
  node->ReplaceInput(2, GetReplacementLow(value));
  node->InsertInput(zone(), 3, GetReplacementHigh(value));
  NodeProperties::ChangeOp(node, op);
  ReplaceNodeWithProjections(node);
}

void Int64Lowering::LowerWord64AtomicNarrowOp(Node* node, const Operator* op) {
  DefaultLowering(node, true);
  NodeProperties::ChangeOp(node, op);
  ReplaceNode(node, node, graph()->NewNode(common()->Int32Constant(0)));
}

// static
int Int64Lowering::GetParameterCountAfterLowering(
    Signature<MachineRepresentation>* signature) {
  // GetParameterIndexAfterLowering(parameter_count) returns the parameter count
  // after lowering.
  return GetParameterIndexAfterLowering(
      signature, static_cast<int>(signature->parameter_count()));
}

void Int64Lowering::GetIndexNodes(Node* index, Node** index_low,
                                  Node** index_high) {
  // We want to transform constant indices into constant indices, because
  // wasm-typer depends on them.
  Int32Matcher m(index);
  Node* index_second =
      m.HasResolvedValue()
          ? graph()->NewNode(common()->Int32Constant(m.ResolvedValue() + 4))
          : graph()->NewNode(machine()->Int32Add(), index,
                             graph()->NewNode(common()->Int32Constant(4)));
#if defined(V8_TARGET_LITTLE_ENDIAN)
  *index_low = index;
  *index_high = index_second;
#elif defined(V8_TARGET_BIG_ENDIAN)
  *index_low = index_second;
  *index_high = index;
#endif
}

void Int64Lowering::LowerLoadOperator(Node* node, MachineRepresentation rep,
                                      const Operator* load_op) {
  if (rep == MachineRepresentation::kWord64) {
    LowerMemoryBaseAndIndex(node);
    Node* base = node->InputAt(0);
    Node* index = node->InputAt(1);
    Node* index_low;
    Node* index_high;
    GetIndexNodes(index, &index_low, &index_high);
    Node* high_node;
    if (node->InputCount() > 2) {
      Node* effect_high = node->InputAt(2);
      Node* control_high = node->InputAt(3);
      high_node = graph()->NewNode(load_op, base, index_high, effect_high,
                                   control_high);
      // change the effect change from old_node --> old_effect to
      // old_node --> high_node --> old_effect.
      node->ReplaceInput(2, high_node);
    } else {
      high_node = graph()->NewNode(load_op, base, index_high);
    }
    node->ReplaceInput(1, index_low);
    NodeProperties::ChangeOp(node, load_op);
    ReplaceNode(node, node, high_node);
  } else {
    DefaultLowering(node);
  }
}

void Int64Lowering::LowerStoreOperator(Node* node, MachineRepresentation rep,
                                       const Operator* store_op) {
  if (rep == MachineRepresentation::kWord64) {
    // We change the original store node to store the low word, and create
    // a new store node to store the high word. The effect and control edges
    // are copied from the original store to the new store node, the effect
    // edge of the original store is redirected to the new store.
    LowerMemoryBaseAndIndex(node);
    Node* base = node->InputAt(0);
    Node* index = node->InputAt(1);
    Node* index_low;
    Node* index_high;
    GetIndexNodes(index, &index_low, &index_high);
    Node* value = node->InputAt(2);
    DCHECK(HasReplacementLow(value));
    DCHECK(HasReplacementHigh(value));

    Node* high_node;
    if (node->InputCount() > 3) {
      Node* effect_high = node->InputAt(3);
      Node* control_high = node->InputAt(4);
      high_node = graph()->NewNode(store_op, base, index_high,
                                   GetReplacementHigh(value), effect_high,
                                   control_high);
      node->ReplaceInput(3, high_node);

    } else {
      high_node = graph()->NewNode(store_op, base, index_high,
                                   GetReplacementHigh(value));
    }

    node->ReplaceInput(1, index_low);
    node->ReplaceInput(2, GetReplacementLow(value));
    NodeProperties::ChangeOp(node, store_op);
    ReplaceNode(node, node, high_node);
  } else {
    DefaultLowering(node, true);
  }
}

void Int64Lowering::LowerNode(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kInt64Constant: {
      int64_t value = OpParameter<int64_t>(node->op());
      Node* low_node = graph()->NewNode(
          common()->Int32Constant(static_cast<int32_t>(value & 0xFFFFFFFF)));
      Node* high_node = graph()->NewNode(
          common()->Int32Constant(static_cast<int32_t>(value >> 32)));
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kLoad: {
      MachineRepresentation rep =
          LoadRepresentationOf(node->op()).representation();
      LowerLoadOperator(node, rep, machine()->Load(MachineType::Int32()));
      break;
    }
    case IrOpcode::kUnalignedLoad: {
      MachineRepresentation rep =
          LoadRepresentationOf(node->op()).representation();
      LowerLoadOperator(node, rep,
                        machine()->UnalignedLoad(MachineType::Int32()));
      break;
    }
    case IrOpcode::kLoadImmutable: {
      MachineRepresentation rep =
          LoadRepresentationOf(node->op()).representation();
      LowerLoadOperator(node, rep,
                        machine()->LoadImmutable(MachineType::Int32()));
      break;
    }
    case IrOpcode::kLoadFromObject: {
      ObjectAccess access = ObjectAccessOf(node->op());
      LowerLoadOperator(node, access.machine_type.representation(),
                        simplified()->LoadFromObject(ObjectAccess(
                            MachineType::Int32(), access.write_barrier_kind)));
      break;
    }
    case IrOpcode::kLoadImmutableFromObject: {
      ObjectAccess access = ObjectAccessOf(node->op());
      LowerLoadOperator(node, access.machine_type.representation(),
                        simplified()->LoadImmutableFromObject(ObjectAccess(
                            MachineType::Int32(), access.write_barrier_kind)));
      break;
    }
    case IrOpcode::kStore: {
      StoreRepresentation store_rep = StoreRepresentationOf(node->op());
      LowerStoreOperator(
          node, store_rep.representation(),
          machine()->Store(StoreRepresentation(
              MachineRepresentation::kWord32, store_rep.write_barrier_kind())));
      break;
    }
    case IrOpcode::kUnalignedStore: {
      UnalignedStoreRepresentation store_rep =
          UnalignedStoreRepresentationOf(node->op());
      LowerStoreOperator(
          node, store_rep,
          machine()->UnalignedStore(MachineRepresentation::kWord32));
      break;
    }
    case IrOpcode::kStoreToObject: {
      ObjectAccess access = ObjectAccessOf(node->op());
      LowerStoreOperator(node, access.machine_type.representation(),
                         simplified()->StoreToObject(ObjectAccess(
                             MachineType::Int32(), access.write_barrier_kind)));
      break;
    }
    case IrOpcode::kInitializeImmutableInObject: {
      ObjectAccess access = ObjectAccessOf(node->op());
      LowerStoreOperator(node, access.machine_type.representation(),
                         simplified()->InitializeImmutableInObject(ObjectAccess(
                             MachineType::Int32(), access.write_barrier_kind)));
      break;
    }
    case IrOpcode::kStart: {
      int parameter_count = GetParameterCountAfterLowering(signature());
      // Only exchange the node if the parameter count actually changed.
      if (parameter_count != static_cast<int>(signature()->parameter_count())) {
        int delta =
            parameter_count - static_cast<int>(signature()->parameter_count());
        int new_output_count = node->op()->ValueOutputCount() + delta;
        NodeProperties::ChangeOp(node, common()->Start(new_output_count));
      }
      break;
    }
    case IrOpcode::kParameter: {
      DCHECK_EQ(1, node->InputCount());
      int param_count = static_cast<int>(signature()->parameter_count());
      // Only exchange the node if the parameter count actually changed. We do
      // not even have to do the default lowering because the the start node,
      // the only input of a parameter node, only changes if the parameter count
      // changes.
      if (GetParameterCountAfterLowering(signature()) != param_count) {
        int old_index = ParameterIndexOf(node->op());
        // Adjust old_index to be compliant with the signature.
        --old_index;
        int new_index = GetParameterIndexAfterLowering(signature(), old_index);
        // Adjust new_index to consider the instance parameter.
        ++new_index;
        NodeProperties::ChangeOp(node, common()->Parameter(new_index));

        if (old_index < 0 || old_index >= param_count) {
          // Special parameters (JS closure/context) don't have kWord64
          // representation anyway.
          break;
        }

        if (signature()->GetParam(old_index) ==
            MachineRepresentation::kWord64) {
          Node* high_node = graph()->NewNode(common()->Parameter(new_index + 1),
                                             graph()->start());
          ReplaceNode(node, node, high_node);
        }
      }
      break;
    }
    case IrOpcode::kReturn: {
      int input_count = node->InputCount();
      DefaultLowering(node);
      if (input_count != node->InputCount()) {
        int new_return_count = GetReturnCountAfterLowering(signature());
        if (static_cast<int>(signature()->return_count()) != new_return_count) {
          NodeProperties::ChangeOp(node, common()->Return(new_return_count));
        }
      }
      break;
    }
    case IrOpcode::kTailCall: {
      auto call_descriptor =
          const_cast<CallDescriptor*>(CallDescriptorOf(node->op()));
      bool returns_require_lowering =
          GetReturnCountAfterLowering(call_descriptor) !=
          static_cast<int>(call_descriptor->ReturnCount());
      if (DefaultLowering(node) || returns_require_lowering) {
        // Tail calls do not have return values, so adjusting the call
        // descriptor is enough.
        NodeProperties::ChangeOp(
            node, common()->TailCall(LowerCallDescriptor(call_descriptor)));
      }
      break;
    }
    case IrOpcode::kCall: {
      auto call_descriptor = CallDescriptorOf(node->op());

      bool returns_require_lowering =
          GetReturnCountAfterLowering(call_descriptor) !=
          static_cast<int>(call_descriptor->ReturnCount());
      if (DefaultLowering(node) || returns_require_lowering) {
        // We have to adjust the call descriptor.
        NodeProperties::ChangeOp(
            node, common()->Call(LowerCallDescriptor(call_descriptor)));
      }
      if (returns_require_lowering) {
        size_t return_arity = call_descriptor->ReturnCount();
        if (return_arity == 1) {
          // We access the additional return values through projections.
          ReplaceNodeWithProjections(node);
        } else {
          ZoneVector<Node*> projections(return_arity, zone());
          NodeProperties::CollectValueProjections(node, projections.data(),
                                                  return_arity);
          for (size_t old_index = 0, new_index = 0; old_index < return_arity;
               ++old_index, ++new_index) {
            Node* use_node = projections[old_index];
            DCHECK_EQ(ProjectionIndexOf(use_node->op()), old_index);
            DCHECK_EQ(GetReturnIndexAfterLowering(call_descriptor,
                                                  static_cast<int>(old_index)),
                      static_cast<int>(new_index));
            if (new_index != old_index) {
              NodeProperties::ChangeOp(
                  use_node, common()->Projection(new_index));
            }
            if (call_descriptor->GetReturnType(old_index).representation() ==
                MachineRepresentation::kWord64) {
              Node* high_node = graph()->NewNode(
                  common()->Projection(new_index + 1), node, graph()->start());
              ReplaceNode(use_node, use_node, high_node);
              ++new_index;
            }
          }
        }
      }
      break;
    }
    case IrOpcode::kWord64And: {
      DCHECK_EQ(2, node->InputCount());
      Node* left = node->InputAt(0);
      Node* right = node->InputAt(1);

      Node* low_node =
          graph()->NewNode(machine()->Word32And(), GetReplacementLow(left),
                           GetReplacementLow(right));
      Node* high_node =
          graph()->NewNode(machine()->Word32And(), GetReplacementHigh(left),
                           GetReplacementHigh(right));
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kTruncateInt64ToInt32: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      ReplaceNode(node, GetReplacementLow(input), nullptr);
      node->NullAllInputs();
      break;
    }
    case IrOpcode::kInt64Add: {
      DCHECK_EQ(2, node->InputCount());

      Node* right = node->InputAt(1);
      node->ReplaceInput(1, GetReplacementLow(right));
      node->AppendInput(zone(), GetReplacementHigh(right));

      Node* left = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(left));
      node->InsertInput(zone(), 1, GetReplacementHigh(left));

      NodeProperties::ChangeOp(node, machine()->Int32PairAdd());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kInt64Sub: {
      DCHECK_EQ(2, node->InputCount());

      Node* right = node->InputAt(1);
      node->ReplaceInput(1, GetReplacementLow(right));
      node->AppendInput(zone(), GetReplacementHigh(right));

      Node* left = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(left));
      node->InsertInput(zone(), 1, GetReplacementHigh(left));

      NodeProperties::ChangeOp(node, machine()->Int32PairSub());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kInt64Mul: {
      DCHECK_EQ(2, node->InputCount());

      Node* right = node->InputAt(1);
      node->ReplaceInput(1, GetReplacementLow(right));
      node->AppendInput(zone(), GetReplacementHigh(right));

      Node* left = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(left));
      node->InsertInput(zone(), 1, GetReplacementHigh(left));

      NodeProperties::ChangeOp(node, machine()->Int32PairMul());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kWord64Or: {
      DCHECK_EQ(2, node->InputCount());
      Node* left = node->InputAt(0);
      Node* right = node->InputAt(1);

      Node* low_node =
          graph()->NewNode(machine()->Word32Or(), GetReplacementLow(left),
                           GetReplacementLow(right));
      Node* high_node =
          graph()->NewNode(machine()->Word32Or(), GetReplacementHigh(left),
                           GetReplacementHigh(right));
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kWord64Xor: {
      DCHECK_EQ(2, node->InputCount());
      Node* left = node->InputAt(0);
      Node* right = node->InputAt(1);

      Node* low_node =
          graph()->NewNode(machine()->Word32Xor(), GetReplacementLow(left),
                           GetReplacementLow(right));
      Node* high_node =
          graph()->NewNode(machine()->Word32Xor(), GetReplacementHigh(left),
                           GetReplacementHigh(right));
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kWord64Shl: {
      // TODO(turbofan): if the shift count >= 32, then we can set the low word
      // of the output to 0 and just calculate the high word.
      DCHECK_EQ(2, node->InputCount());
      Node* shift = node->InputAt(1);
      if (HasReplacementLow(shift)) {
        // We do not have to care about the high word replacement, because
        // the shift can only be between 0 and 63 anyways.
        node->ReplaceInput(1, GetReplacementLow(shift));
      }

      Node* value = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(value));
      node->InsertInput(zone(), 1, GetReplacementHigh(value));

      NodeProperties::ChangeOp(node, machine()->Word32PairShl());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kWord64Shr: {
      // TODO(turbofan): if the shift count >= 32, then we can set the low word
      // of the output to 0 and just calculate the high word.
      DCHECK_EQ(2, node->InputCount());
      Node* shift = node->InputAt(1);
      if (HasReplacementLow(shift)) {
        // We do not have to care about the high word replacement, because
        // the shift can only be between 0 and 63 anyways.
        node->ReplaceInput(1, GetReplacementLow(shift));
      }

      Node* value = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(value));
      node->InsertInput(zone(), 1, GetReplacementHigh(value));

      NodeProperties::ChangeOp(node, machine()->Word32PairShr());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kWord64Sar: {
      // TODO(turbofan): if the shift count >= 32, then we can set the low word
      // of the output to 0 and just calculate the high word.
      DCHECK_EQ(2, node->InputCount());
      Node* shift = node->InputAt(1);
      if (HasReplacementLow(shift)) {
        // We do not have to care about the high word replacement, because
        // the shift can only be between 0 and 63 anyways.
        node->ReplaceInput(1, GetReplacementLow(shift));
      }

      Node* value = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(value));
      node->InsertInput(zone(), 1, GetReplacementHigh(value));

      NodeProperties::ChangeOp(node, machine()->Word32PairSar());
      // We access the additional return values through projections.
      ReplaceNodeWithProjections(node);
      break;
    }
    case IrOpcode::kWord64Equal: {
      DCHECK_EQ(2, node->InputCount());
      Node* left = node->InputAt(0);
      Node* right = node->InputAt(1);

      // TODO(wasm): Use explicit comparisons and && here?
      Node* replacement = graph()->NewNode(
          machine()->Word32Equal(),
          graph()->NewNode(
              machine()->Word32Or(),
              graph()->NewNode(machine()->Word32Xor(), GetReplacementLow(left),
                               GetReplacementLow(right)),
              graph()->NewNode(machine()->Word32Xor(), GetReplacementHigh(left),
                               GetReplacementHigh(right))),
          graph()->NewNode(common()->Int32Constant(0)));
      ReplaceNode(node, replacement, nullptr);
      break;
    }
    case IrOpcode::kInt64LessThan: {
      LowerComparison(node, machine()->Int32LessThan(),
                      machine()->Uint32LessThan());
      break;
    }
    case IrOpcode::kInt64LessThanOrEqual: {
      LowerComparison(node, machine()->Int32LessThan(),
                      machine()->Uint32LessThanOrEqual());
      break;
    }
    case IrOpcode::kUint64LessThan: {
      LowerComparison(node, machine()->Uint32LessThan(),
                      machine()->Uint32LessThan());
      break;
    }
    case IrOpcode::kUint64LessThanOrEqual: {
      LowerComparison(node, machine()->Uint32LessThan(),
                      machine()->Uint32LessThanOrEqual());
      break;
    }
    case IrOpcode::kSignExtendWord32ToInt64:
    case IrOpcode::kChangeInt32ToInt64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      if (HasReplacementLow(input)) {
        input = GetReplacementLow(input);
      }
      // We use SAR to preserve the sign in the high word.
      Node* high_node =
          graph()->NewNode(machine()->Word32Sar(), input,
                           graph()->NewNode(common()->Int32Constant(31)));
      ReplaceNode(node, input, high_node);
      node->NullAllInputs();
      break;
    }
    case IrOpcode::kChangeUint32ToUint64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      if (HasReplacementLow(input)) {
        input = GetReplacementLow(input);
      }
      ReplaceNode(node, input, graph()->NewNode(common()->Int32Constant(0)));
      node->NullAllInputs();
      break;
    }
    case IrOpcode::kBitcastInt64ToFloat64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);

      Node* high_half =
          graph()->NewNode(machine()->Float64InsertHighWord32(),
                           graph()->NewNode(common()->Float64Constant(0.0)),
                           GetReplacementHigh(input));
      Node* result = graph()->NewNode(machine()->Float64InsertLowWord32(),
                                      high_half, GetReplacementLow(input));
      ReplaceNode(node, result, nullptr);
      break;
    }
    case IrOpcode::kBitcastFloat64ToInt64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      if (HasReplacementLow(input)) {
        input = GetReplacementLow(input);
      }

      Node* low_node =
          graph()->NewNode(machine()->Float64ExtractLowWord32(), input);
      Node* high_node =
          graph()->NewNode(machine()->Float64ExtractHighWord32(), input);
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kWord64RolLowerable:
      DCHECK(machine()->Word32Rol().IsSupported());
      [[fallthrough]];
    case IrOpcode::kWord64RorLowerable: {
      DCHECK_EQ(3, node->InputCount());
      Node* input = node->InputAt(0);
      Node* shift = HasReplacementLow(node->InputAt(1))
                        ? GetReplacementLow(node->InputAt(1))
                        : node->InputAt(1);
      Int32Matcher m(shift);
      if (m.HasResolvedValue()) {
        // Precondition: 0 <= shift < 64.
        int32_t shift_value = m.ResolvedValue() & 0x3F;
        if (shift_value == 0) {
          ReplaceNode(node, GetReplacementLow(input),
                      GetReplacementHigh(input));
        } else if (shift_value == 32) {
          ReplaceNode(node, GetReplacementHigh(input),
                      GetReplacementLow(input));
        } else {
          Node* low_input;
          Node* high_input;
          if (shift_value < 32) {
            low_input = GetReplacementLow(input);
            high_input = GetReplacementHigh(input);
          } else {
            low_input = GetReplacementHigh(input);
            high_input = GetReplacementLow(input);
          }
          int32_t masked_shift_value = shift_value & 0x1F;
          Node* masked_shift =
              graph()->NewNode(common()->Int32Constant(masked_shift_value));
          Node* inv_shift = graph()->NewNode(
              common()->Int32Constant(32 - masked_shift_value));

          auto* op1 = machine()->Word32Shr();
          auto* op2 = machine()->Word32Shl();
          bool is_ror = node->opcode() == IrOpcode::kWord64RorLowerable;
          if (!is_ror) std::swap(op1, op2);

          Node* low_node =
              graph()->NewNode(machine()->Word32Or(),
                               graph()->NewNode(op1, low_input, masked_shift),
                               graph()->NewNode(op2, high_input, inv_shift));
          Node* high_node =
              graph()->NewNode(machine()->Word32Or(),
                               graph()->NewNode(op1, high_input, masked_shift),
                               graph()->NewNode(op2, low_input, inv_shift));
          ReplaceNode(node, low_node, high_node);
        }
      } else {
        Node* safe_shift = shift;
        if (!machine()->Word32ShiftIsSafe()) {
          safe_shift =
              graph()->NewNode(machine()->Word32And(), shift,
                               graph()->NewNode(common()->Int32Constant(0x1F)));
        }

        bool is_ror = node->opcode() == IrOpcode::kWord64RorLowerable;
        Node* inv_mask =
            is_ror ? graph()->NewNode(
                         machine()->Word32Xor(),
                         graph()->NewNode(
                             machine()->Word32Shr(),
                             graph()->NewNode(common()->Int32Constant(-1)),
                             safe_shift),
                         graph()->NewNode(common()->Int32Constant(-1)))
                   : graph()->NewNode(
                         machine()->Word32Shl(),
                         graph()->NewNode(common()->Int32Constant(-1)),
                         safe_shift);

        Node* bit_mask =
            graph()->NewNode(machine()->Word32Xor(), inv_mask,
                             graph()->NewNode(common()->Int32Constant(-1)));

        // We have to mask the shift value for this comparison. If
        // !machine()->Word32ShiftIsSafe() then the masking should already be
        // part of the graph.
        Node* masked_shift6 = shift;
        if (machine()->Word32ShiftIsSafe()) {
          masked_shift6 =
              graph()->NewNode(machine()->Word32And(), shift,
                               graph()->NewNode(common()->Int32Constant(0x3F)));
        }

        Diamond lt32(
            graph(), common(),
            graph()->NewNode(machine()->Int32LessThan(), masked_shift6,
                             graph()->NewNode(common()->Int32Constant(32))));
        lt32.Chain(NodeProperties::GetControlInput(node));

        // The low word and the high word can be swapped either at the input or
        // at the output. We swap the inputs so that shift does not have to be
        // kept for so long in a register.
        Node* input_low =
            lt32.Phi(MachineRepresentation::kWord32, GetReplacementLow(input),
                     GetReplacementHigh(input));
        Node* input_high =
            lt32.Phi(MachineRepresentation::kWord32, GetReplacementHigh(input),
                     GetReplacementLow(input));

        const Operator* oper =
            is_ror ? machine()->Word32Ror() : machine()->Word32Rol().op();

        Node* rotate_low = graph()->NewNode(oper, input_low, safe_shift);
        Node* rotate_high = graph()->NewNode(oper, input_high, safe_shift);

        auto* mask1 = bit_mask;
        auto* mask2 = inv_mask;
        if (!is_ror) std::swap(mask1, mask2);

        Node* low_node = graph()->NewNode(
            machine()->Word32Or(),
            graph()->NewNode(machine()->Word32And(), rotate_low, mask1),
            graph()->NewNode(machine()->Word32And(), rotate_high, mask2));
        Node* high_node = graph()->NewNode(
            machine()->Word32Or(),
            graph()->NewNode(machine()->Word32And(), rotate_high, mask1),
            graph()->NewNode(machine()->Word32And(), rotate_low, mask2));
        ReplaceNode(node, low_node, high_node);
      }
      break;
    }
    case IrOpcode::kWord64ClzLowerable: {
      DCHECK_EQ(2, node->InputCount());
      Node* input = node->InputAt(0);
      Diamond d(
          graph(), common(),
          graph()->NewNode(machine()->Word32Equal(), GetReplacementHigh(input),
                           graph()->NewNode(common()->Int32Constant(0))));
      d.Chain(NodeProperties::GetControlInput(node));

      Node* low_node = d.Phi(
          MachineRepresentation::kWord32,
          graph()->NewNode(machine()->Int32Add(),
                           graph()->NewNode(machine()->Word32Clz(),
                                            GetReplacementLow(input)),
                           graph()->NewNode(common()->Int32Constant(32))),
          graph()->NewNode(machine()->Word32Clz(), GetReplacementHigh(input)));
      ReplaceNode(node, low_node, graph()->NewNode(common()->Int32Constant(0)));
      break;
    }
    case IrOpcode::kWord64CtzLowerable: {
      DCHECK_EQ(2, node->InputCount());
      DCHECK(machine()->Word32Ctz().IsSupported());
      Node* input = node->InputAt(0);
      Diamond d(
          graph(), common(),
          graph()->NewNode(machine()->Word32Equal(), GetReplacementLow(input),
                           graph()->NewNode(common()->Int32Constant(0))));
      d.Chain(NodeProperties::GetControlInput(node));

      Node* low_node =
          d.Phi(MachineRepresentation::kWord32,
                graph()->NewNode(machine()->Int32Add(),
                                 graph()->NewNode(machine()->Word32Ctz().op(),
                                                  GetReplacementHigh(input)),
                                 graph()->NewNode(common()->Int32Constant(32))),
                graph()->NewNode(machine()->Word32Ctz().op(),
                                 GetReplacementLow(input)));
      ReplaceNode(node, low_node, graph()->NewNode(common()->Int32Constant(0)));
      break;
    }
    case IrOpcode::kWord64Ror:
    case IrOpcode::kWord64Rol:
    case IrOpcode::kWord64Ctz:
    case IrOpcode::kWord64Clz:
      FATAL("%s operator should not be used in 32-bit systems",
            node->op()->mnemonic());
    case IrOpcode::kWord64Popcnt: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      // We assume that a Word64Popcnt node only has been created if
      // Word32Popcnt is actually supported.
      DCHECK(machine()->Word32Popcnt().IsSupported());
      Node* low_node =
          graph()->NewNode(machine()->Int32Add(),
                           graph()->NewNode(machine()->Word32Popcnt().op(),
                                            GetReplacementLow(input)),
                           graph()->NewNode(machine()->Word32Popcnt().op(),
                                            GetReplacementHigh(input)));
      ReplaceNode(node, low_node, graph()->NewNode(common()->Int32Constant(0)));
      break;
    }
    case IrOpcode::kPhi: {
      MachineRepresentation rep = PhiRepresentationOf(node->op());
      if (rep == MachineRepresentation::kWord64) {
        // The replacement nodes have already been created, we only have to
        // replace placeholder nodes.
        Node* low_node = GetReplacementLow(node);
        Node* high_node = GetReplacementHigh(node);
        for (int i = 0; i < node->op()->ValueInputCount(); i++) {
          low_node->ReplaceInput(i, GetReplacementLow(node->InputAt(i)));
          high_node->ReplaceInput(i, GetReplacementHigh(node->InputAt(i)));
        }
      } else {
        DefaultLowering(node);
      }
      break;
    }
    case IrOpcode::kLoopExitValue: {
      MachineRepresentation rep = LoopExitValueRepresentationOf(node->op());
      if (rep == MachineRepresentation::kWord64) {
        Node* low_node = graph()->NewNode(
            common()->LoopExitValue(MachineRepresentation::kWord32),
            GetReplacementLow(node->InputAt(0)), node->InputAt(1));
        Node* high_node = graph()->NewNode(
            common()->LoopExitValue(MachineRepresentation::kWord32),
            GetReplacementHigh(node->InputAt(0)), node->InputAt(1));
        ReplaceNode(node, low_node, high_node);
      } else {
        DefaultLowering(node);
      }
      break;
    }
    case IrOpcode::kWord64ReverseBytes: {
      Node* input = node->InputAt(0);
      Node* low_node = graph()->NewNode(machine()->Word32ReverseBytes(),
                                        GetReplacementHigh(input));
      Node* high_node = graph()->NewNode(machine()->Word32ReverseBytes(),
                                         GetReplacementLow(input));
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kSignExtendWord8ToInt64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      if (HasReplacementLow(input)) {
        input = GetReplacementLow(input);
      }
      // Sign extend low node to Int32
      Node* low_node =
          graph()->NewNode(machine()->SignExtendWord8ToInt32(), input);
      // We use SAR to preserve the sign in the high word.
      Node* high_node =
          graph()->NewNode(machine()->Word32Sar(), low_node,
                           graph()->NewNode(common()->Int32Constant(31)));
      ReplaceNode(node, low_node, high_node);
      node->NullAllInputs();
      break;
    }
    case IrOpcode::kSignExtendWord16ToInt64: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      if (HasReplacementLow(input)) {
        input = GetReplacementLow(input);
      }
      // Sign extend low node to Int32
      Node* low_node =
          graph()->NewNode(machine()->SignExtendWord16ToInt32(), input);
      // We use SAR to preserve the sign in the high word.
      Node* high_node =
          graph()->NewNode(machine()->Word32Sar(), low_node,
                           graph()->NewNode(common()->Int32Constant(31)));
      ReplaceNode(node, low_node, high_node);
      node->NullAllInputs();
      break;
    }
    case IrOpcode::kWord64AtomicLoad: {
      DCHECK_EQ(4, node->InputCount());
      AtomicLoadParameters params = AtomicLoadParametersOf(node->op());
      DefaultLowering(node, true);
      if (params.representation() == MachineType::Uint64()) {
        NodeProperties::ChangeOp(
            node, machine()->Word32AtomicPairLoad(params.order()));
        ReplaceNodeWithProjections(node);
      } else {
        NodeProperties::ChangeOp(node, machine()->Word32AtomicLoad(params));
        ReplaceNode(node, node, graph()->NewNode(common()->Int32Constant(0)));
      }
      break;
    }
    case IrOpcode::kWord64AtomicStore: {
      DCHECK_EQ(5, node->InputCount());
      AtomicStoreParameters params = AtomicStoreParametersOf(node->op());
      if (params.representation() == MachineRepresentation::kWord64) {
        LowerMemoryBaseAndIndex(node);
        Node* value = node->InputAt(2);
        node->ReplaceInput(2, GetReplacementLow(value));
        node->InsertInput(zone(), 3, GetReplacementHigh(value));
        NodeProperties::ChangeOp(
            node, machine()->Word32AtomicPairStore(params.order()));
      } else {
        DefaultLowering(node, true);
        NodeProperties::ChangeOp(node, machine()->Word32AtomicStore(params));
      }
      break;
    }
#define ATOMIC_CASE(name)                                                   \
  case IrOpcode::kWord64Atomic##name: {                                     \
    MachineType type = AtomicOpType(node->op());                            \
    if (type == MachineType::Uint64()) {                                    \
      LowerWord64AtomicBinop(node, machine()->Word32AtomicPair##name());    \
    } else {                                                                \
      LowerWord64AtomicNarrowOp(node, machine()->Word32Atomic##name(type)); \
    }                                                                       \
    break;                                                                  \
  }
      ATOMIC_CASE(Add)
      ATOMIC_CASE(Sub)
      ATOMIC_CASE(And)
      ATOMIC_CASE(Or)
      ATOMIC_CASE(Xor)
      ATOMIC_CASE(Exchange)
#undef ATOMIC_CASE
    case IrOpcode::kWord64AtomicCompareExchange: {
      MachineType type = AtomicOpType(node->op());
      if (type == MachineType::Uint64()) {
        LowerMemoryBaseAndIndex(node);
        Node* old_value = node->InputAt(2);
        Node* new_value = node->InputAt(3);
        node->ReplaceInput(2, GetReplacementLow(old_value));
        node->ReplaceInput(3, GetReplacementHigh(old_value));
        node->InsertInput(zone(), 4, GetReplacementLow(new_value));
        node->InsertInput(zone(), 5, GetReplacementHigh(new_value));
        NodeProperties::ChangeOp(node,
                                 machine()->Word32AtomicPairCompareExchange());
        ReplaceNodeWithProjections(node);
      } else {
        DCHECK(type == MachineType::Uint32() || type == MachineType::Uint16() ||
               type == MachineType::Uint8());
        DefaultLowering(node, true);
        NodeProperties::ChangeOp(node,
                                 machine()->Word32AtomicCompareExchange(type));
        ReplaceNode(node, node, graph()->NewNode(common()->Int32Constant(0)));
      }
      break;
    }
    case IrOpcode::kI64x2Splat: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      node->ReplaceInput(0, GetReplacementLow(input));
      node->AppendInput(zone(), GetReplacementHigh(input));
      NodeProperties::ChangeOp(node, machine()->I64x2SplatI32Pair());
      break;
    }
    case IrOpcode::kI64x2ExtractLane: {
      DCHECK_EQ(1, node->InputCount());
      Node* input = node->InputAt(0);
      int32_t lane = OpParameter<int32_t>(node->op());
      Node* low_node =
          graph()->NewNode(machine()->I32x4ExtractLane(lane * 2), input);
      Node* high_node =
          graph()->NewNode(machine()->I32x4ExtractLane(lane * 2 + 1), input);
      ReplaceNode(node, low_node, high_node);
      break;
    }
    case IrOpcode::kI64x2ReplaceLane: {
      DCHECK_EQ(2, node->InputCount());
      int32_t lane = OpParameter<int32_t>(node->op());
      Node* input = node->InputAt(1);
      node->ReplaceInput(1, GetReplacementLow(input));
      node->AppendInput(zone(), GetReplacementHigh(input));
      NodeProperties::ChangeOp(node, machine()->I64x2ReplaceLaneI32Pair(lane));
      break;
    }

    default: { DefaultLowering(node); }
  }
}

void Int64Lowering::LowerComparison(Node* node, const Operator* high_word_op,
                                    const Operator* low_word_op) {
  DCHECK_EQ(2, node->InputCount());
  Node* left = node->InputAt(0);
  Node* right = node->InputAt(1);
  Node* replacement = graph()->NewNode(
      machine()->Word32Or(),
      graph()->NewNode(high_word_op, GetReplacementHigh(left),
                       GetReplacementHigh(right)),
      graph()->NewNode(
          machine()->Word32And(),
          graph()->NewNode(machine()->Word32Equal(), GetReplacementHigh(left),
                           GetReplacementHigh(right)),
          graph()->NewNode(low_word_op, GetReplacementLow(left),
                           GetReplacementLow(right))));
  ReplaceNode(node, replacement, nullptr);
}

bool Int64Lowering::DefaultLowering(Node* node, bool low_word_only) {
  bool something_changed = false;
  for (int i = NodeProperties::PastValueIndex(node) - 1; i >= 0; i--) {
    Node* input = node->InputAt(i);
    if (HasReplacementLow(input)) {
      something_changed = true;
      node->ReplaceInput(i, GetReplacementLow(input));
    }
    if (!low_word_only && HasReplacementHigh(input)) {
      something_changed = true;
      node->InsertInput(zone(), i + 1, GetReplacementHigh(input));
    }
  }
  return something_changed;
}

const CallDescriptor* Int64Lowering::LowerCallDescriptor(
    const CallDescriptor* call_descriptor) {
  CallDescriptor* maybe_special_replacement =
      wasm::GetWasmEngine()->call_descriptors()->GetLoweredCallDescriptor(
          call_descriptor);
  if (maybe_special_replacement) return maybe_special_replacement;
  return GetI32WasmCallDescriptor(zone(), call_descriptor);
}

void Int64Lowering::ReplaceNode(Node* old, Node* new_low, Node* new_high) {
  // if new_low == nullptr, then also new_high == nullptr.
  DCHECK(new_low != nullptr || new_high == nullptr);
  replacements_[old->id()].low = new_low;
  replacements_[old->id()].high = new_high;
}

bool Int64Lowering::HasReplacementLow(Node* node) {
  return replacements_[node->id()].low != nullptr;
}

Node* Int64Lowering::GetReplacementLow(Node* node) {
  Node* result = replacements_[node->id()].low;
  DCHECK(result);
  return result;
}

bool Int64Lowering::HasReplacementHigh(Node* node) {
  return replacements_[node->id()].high != nullptr;
}

Node* Int64Lowering::GetReplacementHigh(Node* node) {
  Node* result = replacements_[node->id()].high;
  DCHECK(result);
  return result;
}

void Int64Lowering::PreparePhiReplacement(Node* phi) {
  MachineRepresentation rep = PhiRepresentationOf(phi->op());
  if (rep == MachineRepresentation::kWord64) {
    // We have to create the replacements for a phi node before we actually
    // lower the phi to break potential cycles in the graph. The replacements of
    // input nodes do not exist yet, so we use a placeholder node to pass the
    // graph verifier.
    int value_count = phi->op()->ValueInputCount();
    Node** inputs_low = zone()->AllocateArray<Node*>(value_count + 1);
    Node** inputs_high = zone()->AllocateArray<Node*>(value_count + 1);
    for (int i = 0; i < value_count; i++) {
      inputs_low[i] = placeholder_;
      inputs_high[i] = placeholder_;
    }
    inputs_low[value_count] = NodeProperties::GetControlInput(phi, 0);
    inputs_high[value_count] = NodeProperties::GetControlInput(phi, 0);
    ReplaceNode(phi,
                graph()->NewNode(
                    common()->Phi(MachineRepresentation::kWord32, value_count),
                    value_count + 1, inputs_low, false),
                graph()->NewNode(
                    common()->Phi(MachineRepresentation::kWord32, value_count),
                    value_count + 1, inputs_high, false));
  }
}

void Int64Lowering::ReplaceNodeWithProjections(Node* node) {
  DCHECK(node != nullptr);
  Node* low_node =
      graph()->NewNode(common()->Projection(0), node, graph()->start());
  Node* high_node =
      graph()->NewNode(common()->Projection(1), node, graph()->start());
  ReplaceNode(node, low_node, high_node);
}

void Int64Lowering::LowerMemoryBaseAndIndex(Node* node) {
  DCHECK(node != nullptr);
  // Low word only replacements for memory operands for 32-bit address space.
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  if (HasReplacementLow(base)) {
    node->ReplaceInput(0, GetReplacementLow(base));
  }
  if (HasReplacementLow(index)) {
    node->ReplaceInput(1, GetReplacementLow(index));
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_32_BIT
