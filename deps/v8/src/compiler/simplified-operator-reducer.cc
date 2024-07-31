// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/simplified-operator-reducer.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/numbers/conversions-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

Decision DecideObjectIsSmi(Node* const input) {
  NumberMatcher m(input);
  if (m.HasResolvedValue()) {
    return IsSmiDouble(m.ResolvedValue()) ? Decision::kTrue : Decision::kFalse;
  }
  if (m.IsAllocate()) return Decision::kFalse;
  if (m.IsChangeBitToTagged()) return Decision::kFalse;
  if (m.IsChangeInt31ToTaggedSigned()) return Decision::kTrue;
  if (m.IsHeapConstant()) return Decision::kFalse;
  return Decision::kUnknown;
}

}  // namespace

SimplifiedOperatorReducer::SimplifiedOperatorReducer(
    Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker,
    BranchSemantics branch_semantics)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      broker_(broker),
      branch_semantics_(branch_semantics) {}

SimplifiedOperatorReducer::~SimplifiedOperatorReducer() = default;


Reduction SimplifiedOperatorReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kBooleanNot: {
      HeapObjectMatcher m(node->InputAt(0));
      if (m.Is(factory()->true_value())) return ReplaceBoolean(false);
      if (m.Is(factory()->false_value())) return ReplaceBoolean(true);
      if (m.IsBooleanNot()) return Replace(m.InputAt(0));
      break;
    }
    case IrOpcode::kChangeBitToTagged: {
      Int32Matcher m(node->InputAt(0));
      if (m.Is(0)) return Replace(jsgraph()->FalseConstant());
      if (m.Is(1)) return Replace(jsgraph()->TrueConstant());
      if (m.IsChangeTaggedToBit()) return Replace(m.InputAt(0));
      break;
    }
    case IrOpcode::kChangeTaggedToBit: {
      HeapObjectMatcher m(node->InputAt(0));
      if (m.HasResolvedValue()) {
        base::Optional<bool> maybe_result =
            m.Ref(broker()).TryGetBooleanValue(broker());
        if (maybe_result.has_value()) return ReplaceInt32(*maybe_result);
      }
      if (m.IsChangeBitToTagged()) return Replace(m.InputAt(0));
      break;
    }
    case IrOpcode::kChangeFloat64ToTagged: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) return ReplaceNumber(m.ResolvedValue());
      if (m.IsChangeTaggedToFloat64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeInt31ToTaggedSigned:
    case IrOpcode::kChangeInt32ToTagged: {
      Int32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) return ReplaceNumber(m.ResolvedValue());
      if (m.IsChangeTaggedSignedToInt32()) {
        return Replace(m.InputAt(0));
      }
      break;
    }
    case IrOpcode::kChangeTaggedToFloat64:
    case IrOpcode::kTruncateTaggedToFloat64: {
      NumberMatcher m(node->InputAt(0));
      if (m.HasResolvedValue()) return ReplaceFloat64(m.ResolvedValue());
      if (m.IsChangeFloat64ToTagged() || m.IsChangeFloat64ToTaggedPointer()) {
        return Replace(m.node()->InputAt(0));
      }
      if (m.IsChangeInt31ToTaggedSigned() || m.IsChangeInt32ToTagged()) {
        return Change(node, machine()->ChangeInt32ToFloat64(), m.InputAt(0));
      }
      if (m.IsChangeUint32ToTagged()) {
        return Change(node, machine()->ChangeUint32ToFloat64(), m.InputAt(0));
      }
      break;
    }
    case IrOpcode::kChangeTaggedSignedToInt32:
    case IrOpcode::kChangeTaggedToInt32: {
      NumberMatcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt32(DoubleToInt32(m.ResolvedValue()));
      if (m.IsChangeFloat64ToTagged() || m.IsChangeFloat64ToTaggedPointer()) {
        return Change(node, machine()->ChangeFloat64ToInt32(), m.InputAt(0));
      }
      if (m.IsChangeInt31ToTaggedSigned() || m.IsChangeInt32ToTagged()) {
        return Replace(m.InputAt(0));
      }
      break;
    }
    case IrOpcode::kChangeTaggedToUint32: {
      NumberMatcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceUint32(DoubleToUint32(m.ResolvedValue()));
      if (m.IsChangeFloat64ToTagged() || m.IsChangeFloat64ToTaggedPointer()) {
        return Change(node, machine()->ChangeFloat64ToUint32(), m.InputAt(0));
      }
      if (m.IsChangeUint32ToTagged()) return Replace(m.InputAt(0));
      break;
    }
    case IrOpcode::kChangeUint32ToTagged: {
      Uint32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceNumber(FastUI2D(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kTruncateTaggedToWord32: {
      NumberMatcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt32(DoubleToInt32(m.ResolvedValue()));
      if (m.IsChangeInt31ToTaggedSigned() || m.IsChangeInt32ToTagged() ||
          m.IsChangeUint32ToTagged()) {
        return Replace(m.InputAt(0));
      }
      if (m.IsChangeFloat64ToTagged() || m.IsChangeFloat64ToTaggedPointer()) {
        return Change(node, machine()->TruncateFloat64ToWord32(), m.InputAt(0));
      }
      break;
    }
    case IrOpcode::kCheckedFloat64ToInt32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue() && IsInt32Double(m.ResolvedValue())) {
        Node* value =
            jsgraph()->Int32Constant(static_cast<int32_t>(m.ResolvedValue()));
        ReplaceWithValue(node, value);
        return Replace(value);
      }
      break;
    }
    case IrOpcode::kCheckedTaggedToArrayIndex:
    case IrOpcode::kCheckedTaggedToInt32:
    case IrOpcode::kCheckedTaggedSignedToInt32: {
      NodeMatcher m(node->InputAt(0));
      if (m.IsConvertTaggedHoleToUndefined()) {
        node->ReplaceInput(0, m.InputAt(0));
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kCheckIf: {
      HeapObjectMatcher m(node->InputAt(0));
      if (m.Is(factory()->true_value())) {
        Node* const effect = NodeProperties::GetEffectInput(node);
        return Replace(effect);
      }
      break;
    }
    case IrOpcode::kCheckNumber: {
      NodeMatcher m(node->InputAt(0));
      if (m.IsConvertTaggedHoleToUndefined()) {
        node->ReplaceInput(0, m.InputAt(0));
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kCheckHeapObject: {
      Node* const input = node->InputAt(0);
      if (DecideObjectIsSmi(input) == Decision::kFalse) {
        ReplaceWithValue(node, input);
        return Replace(input);
      }
      NodeMatcher m(input);
      if (m.IsCheckHeapObject()) {
        ReplaceWithValue(node, input);
        return Replace(input);
      }
      break;
    }
    case IrOpcode::kCheckSmi: {
      Node* const input = node->InputAt(0);
      if (DecideObjectIsSmi(input) == Decision::kTrue) {
        ReplaceWithValue(node, input);
        return Replace(input);
      }
      NodeMatcher m(input);
      if (m.IsCheckSmi()) {
        ReplaceWithValue(node, input);
        return Replace(input);
      } else if (m.IsConvertTaggedHoleToUndefined()) {
        node->ReplaceInput(0, m.InputAt(0));
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kObjectIsSmi: {
      Node* const input = node->InputAt(0);
      switch (DecideObjectIsSmi(input)) {
        case Decision::kTrue:
          return ReplaceBoolean(true);
        case Decision::kFalse:
          return ReplaceBoolean(false);
        case Decision::kUnknown:
          break;
      }
      break;
    }
    case IrOpcode::kNumberAbs: {
      NumberMatcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceNumber(std::fabs(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kReferenceEqual: {
      HeapObjectBinopMatcher m(node);
      if (m.left().node() == m.right().node()) return ReplaceBoolean(true);
      break;
    }
    case IrOpcode::kCheckedInt32Add: {
      // (x + a) + b => x + (a + b) where a and b are constants and have the
      // same sign.
      Int32BinopMatcher m(node);
      if (m.right().HasResolvedValue()) {
        Node* checked_int32_add = m.left().node();
        if (checked_int32_add->opcode() == IrOpcode::kCheckedInt32Add) {
          Int32BinopMatcher n(checked_int32_add);
          if (n.right().HasResolvedValue() &&
              (n.right().ResolvedValue() >= 0) ==
                  (m.right().ResolvedValue() >= 0)) {
            int32_t val;
            bool overflow = base::bits::SignedAddOverflow32(
                n.right().ResolvedValue(), m.right().ResolvedValue(), &val);
            if (!overflow) {
              bool has_no_other_uses = true;
              for (Edge edge : checked_int32_add->use_edges()) {
                if (!edge.from()->IsDead() && edge.from() != node) {
                  has_no_other_uses = false;
                  break;
                }
              }
              if (has_no_other_uses) {
                node->ReplaceInput(0, n.left().node());
                node->ReplaceInput(1, jsgraph()->Int32Constant(val));
                RelaxEffectsAndControls(checked_int32_add);
                checked_int32_add->Kill();
                return Changed(node);
              }
            }
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return NoChange();
}

Reduction SimplifiedOperatorReducer::Change(Node* node, const Operator* op,
                                            Node* a) {
  DCHECK_EQ(node->InputCount(), OperatorProperties::GetTotalInputCount(op));
  DCHECK_LE(1, node->InputCount());
  node->ReplaceInput(0, a);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction SimplifiedOperatorReducer::ReplaceBoolean(bool value) {
  if (branch_semantics_ == BranchSemantics::kJS) {
    return Replace(jsgraph()->BooleanConstant(value));
  } else {
    return ReplaceInt32(value);
  }
}

Reduction SimplifiedOperatorReducer::ReplaceFloat64(double value) {
  return Replace(jsgraph()->Float64Constant(value));
}


Reduction SimplifiedOperatorReducer::ReplaceInt32(int32_t value) {
  return Replace(jsgraph()->Int32Constant(value));
}


Reduction SimplifiedOperatorReducer::ReplaceNumber(double value) {
  return Replace(jsgraph()->ConstantNoHole(value));
}


Reduction SimplifiedOperatorReducer::ReplaceNumber(int32_t value) {
  return Replace(jsgraph()->ConstantNoHole(value));
}

Factory* SimplifiedOperatorReducer::factory() const {
  return jsgraph()->isolate()->factory();
}

Graph* SimplifiedOperatorReducer::graph() const { return jsgraph()->graph(); }

MachineOperatorBuilder* SimplifiedOperatorReducer::machine() const {
  return jsgraph()->machine();
}

SimplifiedOperatorBuilder* SimplifiedOperatorReducer::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
