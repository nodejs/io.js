// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/node-properties.h"

#include "src/compiler/common-operator.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/map-inference.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/verifier.h"

namespace v8 {
namespace internal {
namespace compiler {

// static

// static
bool NodeProperties::IsValueEdge(Edge edge) {
  Node* const node = edge.from();
  return IsInputRange(edge, FirstValueIndex(node),
                      node->op()->ValueInputCount());
}


// static
bool NodeProperties::IsContextEdge(Edge edge) {
  Node* const node = edge.from();
  return IsInputRange(edge, FirstContextIndex(node),
                      OperatorProperties::GetContextInputCount(node->op()));
}


// static
bool NodeProperties::IsFrameStateEdge(Edge edge) {
  Node* const node = edge.from();
  return IsInputRange(edge, FirstFrameStateIndex(node),
                      OperatorProperties::GetFrameStateInputCount(node->op()));
}


// static
bool NodeProperties::IsEffectEdge(Edge edge) {
  Node* const node = edge.from();
  return IsInputRange(edge, FirstEffectIndex(node),
                      node->op()->EffectInputCount());
}


// static
bool NodeProperties::IsControlEdge(Edge edge) {
  Node* const node = edge.from();
  return IsInputRange(edge, FirstControlIndex(node),
                      node->op()->ControlInputCount());
}


// static
bool NodeProperties::IsExceptionalCall(Node* node, Node** out_exception) {
  if (node->op()->HasProperty(Operator::kNoThrow)) return false;
  for (Edge const edge : node->use_edges()) {
    if (!NodeProperties::IsControlEdge(edge)) continue;
    if (edge.from()->opcode() == IrOpcode::kIfException) {
      if (out_exception != nullptr) *out_exception = edge.from();
      return true;
    }
  }
  return false;
}

// static
Node* NodeProperties::FindSuccessfulControlProjection(Node* node) {
  CHECK_GT(node->op()->ControlOutputCount(), 0);
  if (node->op()->HasProperty(Operator::kNoThrow)) return node;
  for (Edge const edge : node->use_edges()) {
    if (!NodeProperties::IsControlEdge(edge)) continue;
    if (edge.from()->opcode() == IrOpcode::kIfSuccess) {
      return edge.from();
    }
  }
  return node;
}

// static
void NodeProperties::ReplaceValueInput(Node* node, Node* value, int index) {
  CHECK_LE(0, index);
  CHECK_LT(index, node->op()->ValueInputCount());
  node->ReplaceInput(FirstValueIndex(node) + index, value);
}


// static
void NodeProperties::ReplaceValueInputs(Node* node, Node* value) {
  int value_input_count = node->op()->ValueInputCount();
  CHECK_GT(value_input_count, 0);
  node->ReplaceInput(0, value);
  while (--value_input_count > 0) {
    node->RemoveInput(value_input_count);
  }
}


// static
void NodeProperties::ReplaceContextInput(Node* node, Node* context) {
  CHECK(OperatorProperties::HasContextInput(node->op()));
  node->ReplaceInput(FirstContextIndex(node), context);
}


// static
void NodeProperties::ReplaceControlInput(Node* node, Node* control, int index) {
  CHECK_LE(0, index);
  CHECK_LT(index, node->op()->ControlInputCount());
  node->ReplaceInput(FirstControlIndex(node) + index, control);
}


// static
void NodeProperties::ReplaceEffectInput(Node* node, Node* effect, int index) {
  CHECK_LE(0, index);
  CHECK_LT(index, node->op()->EffectInputCount());
  return node->ReplaceInput(FirstEffectIndex(node) + index, effect);
}


// static
void NodeProperties::ReplaceFrameStateInput(Node* node, Node* frame_state) {
  CHECK(OperatorProperties::HasFrameStateInput(node->op()));
  node->ReplaceInput(FirstFrameStateIndex(node), frame_state);
}

// static
void NodeProperties::RemoveNonValueInputs(Node* node) {
  node->TrimInputCount(node->op()->ValueInputCount());
}


// static
void NodeProperties::RemoveValueInputs(Node* node) {
  int value_input_count = node->op()->ValueInputCount();
  while (--value_input_count >= 0) {
    node->RemoveInput(value_input_count);
  }
}


void NodeProperties::MergeControlToEnd(Graph* graph,
                                       CommonOperatorBuilder* common,
                                       Node* node) {
  graph->end()->AppendInput(graph->zone(), node);
  graph->end()->set_op(common->End(graph->end()->InputCount()));
}

void NodeProperties::RemoveControlFromEnd(Graph* graph,
                                          CommonOperatorBuilder* common,
                                          Node* node) {
  int index_to_remove = -1;
  for (int i = 0; i < graph->end()->op()->ControlInputCount(); i++) {
    int index = NodeProperties::FirstControlIndex(graph->end()) + i;
    if (graph->end()->InputAt(index) == node) {
      index_to_remove = index;
      break;
    }
  }
  CHECK_NE(-1, index_to_remove);
  graph->end()->RemoveInput(index_to_remove);
  graph->end()->set_op(common->End(graph->end()->InputCount()));
}

// static
void NodeProperties::ReplaceUses(Node* node, Node* value, Node* effect,
                                 Node* success, Node* exception) {
  // Requires distinguishing between value, effect and control edges.
  for (Edge edge : node->use_edges()) {
    if (IsControlEdge(edge)) {
      if (edge.from()->opcode() == IrOpcode::kIfSuccess) {
        DCHECK_NOT_NULL(success);
        edge.UpdateTo(success);
      } else if (edge.from()->opcode() == IrOpcode::kIfException) {
        DCHECK_NOT_NULL(exception);
        edge.UpdateTo(exception);
      } else {
        DCHECK_NOT_NULL(success);
        edge.UpdateTo(success);
      }
    } else if (IsEffectEdge(edge)) {
      DCHECK_NOT_NULL(effect);
      edge.UpdateTo(effect);
    } else {
      DCHECK_NOT_NULL(value);
      edge.UpdateTo(value);
    }
  }
}


// static
void NodeProperties::ChangeOp(Node* node, const Operator* new_op) {
  node->set_op(new_op);
  Verifier::VerifyNode(node);
}

// static
void NodeProperties::ChangeOpUnchecked(Node* node, const Operator* new_op) {
  node->set_op(new_op);
}

// static
Node* NodeProperties::FindFrameStateBefore(Node* node,
                                           Node* unreachable_sentinel) {
  Node* effect = NodeProperties::GetEffectInput(node);
  while (effect->opcode() != IrOpcode::kCheckpoint) {
    if (effect->opcode() == IrOpcode::kDead ||
        effect->opcode() == IrOpcode::kUnreachable) {
      return unreachable_sentinel;
    }
    DCHECK(effect->op()->HasProperty(Operator::kNoWrite));
    DCHECK_EQ(1, effect->op()->EffectInputCount());
    effect = NodeProperties::GetEffectInput(effect);
  }
  Node* frame_state = GetFrameStateInput(effect);
  return frame_state;
}

// static
Node* NodeProperties::FindProjection(Node* node, size_t projection_index) {
  for (auto use : node->uses()) {
    if (use->opcode() == IrOpcode::kProjection &&
        ProjectionIndexOf(use->op()) == projection_index) {
      return use;
    }
  }
  return nullptr;
}


// static
void NodeProperties::CollectValueProjections(Node* node, Node** projections,
                                             size_t projection_count) {
#ifdef DEBUG
  for (size_t index = 0; index < projection_count; ++index) {
    DCHECK_NULL(projections[index]);
  }
#endif
  for (Edge const edge : node->use_edges()) {
    if (!IsValueEdge(edge)) continue;
    Node* use = edge.from();
    DCHECK_EQ(IrOpcode::kProjection, use->opcode());
    projections[ProjectionIndexOf(use->op())] = use;
  }
}


// static
void NodeProperties::CollectControlProjections(Node* node, Node** projections,
                                               size_t projection_count) {
#ifdef DEBUG
  DCHECK_LE(static_cast<int>(projection_count), node->UseCount());
  std::memset(projections, 0, sizeof(*projections) * projection_count);
#endif
  size_t if_value_index = 0;
  for (Edge const edge : node->use_edges()) {
    if (!IsControlEdge(edge)) continue;
    Node* use = edge.from();
    size_t index;
    switch (use->opcode()) {
      case IrOpcode::kIfTrue:
        DCHECK_EQ(IrOpcode::kBranch, node->opcode());
        index = 0;
        break;
      case IrOpcode::kIfFalse:
        DCHECK_EQ(IrOpcode::kBranch, node->opcode());
        index = 1;
        break;
      case IrOpcode::kIfSuccess:
        DCHECK(!node->op()->HasProperty(Operator::kNoThrow));
        index = 0;
        break;
      case IrOpcode::kIfException:
        DCHECK(!node->op()->HasProperty(Operator::kNoThrow));
        index = 1;
        break;
      case IrOpcode::kIfValue:
        DCHECK_EQ(IrOpcode::kSwitch, node->opcode());
        index = if_value_index++;
        break;
      case IrOpcode::kIfDefault:
        DCHECK_EQ(IrOpcode::kSwitch, node->opcode());
        index = projection_count - 1;
        break;
      default:
        continue;
    }
    DCHECK_LT(if_value_index, projection_count);
    DCHECK_LT(index, projection_count);
    DCHECK_NULL(projections[index]);
    projections[index] = use;
  }
#ifdef DEBUG
  for (size_t index = 0; index < projection_count; ++index) {
    DCHECK_NOT_NULL(projections[index]);
  }
#endif
}

// static
MachineRepresentation NodeProperties::GetProjectionType(
    Node const* projection) {
  size_t index = ProjectionIndexOf(projection->op());
  Node* input = projection->InputAt(0);
  switch (input->opcode()) {
    case IrOpcode::kInt32AddWithOverflow:
    case IrOpcode::kInt32SubWithOverflow:
    case IrOpcode::kInt32MulWithOverflow:
      CHECK_LE(index, static_cast<size_t>(1));
      return index == 0 ? MachineRepresentation::kWord32
                        : MachineRepresentation::kBit;
    case IrOpcode::kInt64AddWithOverflow:
    case IrOpcode::kInt64SubWithOverflow:
    case IrOpcode::kInt64MulWithOverflow:
      CHECK_LE(index, static_cast<size_t>(1));
      return index == 0 ? MachineRepresentation::kWord64
                        : MachineRepresentation::kBit;
    case IrOpcode::kTryTruncateFloat64ToInt32:
    case IrOpcode::kTryTruncateFloat64ToUint32:
      CHECK_LE(index, static_cast<size_t>(1));
      return index == 0 ? MachineRepresentation::kWord32
                        : MachineRepresentation::kBit;
    case IrOpcode::kTryTruncateFloat32ToInt64:
    case IrOpcode::kTryTruncateFloat64ToInt64:
    case IrOpcode::kTryTruncateFloat64ToUint64:
    case IrOpcode::kTryTruncateFloat32ToUint64:
      CHECK_LE(index, static_cast<size_t>(1));
      return index == 0 ? MachineRepresentation::kWord64
                        : MachineRepresentation::kBit;
    case IrOpcode::kCall: {
      auto call_descriptor = CallDescriptorOf(input->op());
      return call_descriptor->GetReturnType(index).representation();
    }
    case IrOpcode::kInt32PairAdd:
    case IrOpcode::kInt32PairSub:
    case IrOpcode::kWord32AtomicPairLoad:
    case IrOpcode::kWord32AtomicPairAdd:
    case IrOpcode::kWord32AtomicPairSub:
    case IrOpcode::kWord32AtomicPairAnd:
    case IrOpcode::kWord32AtomicPairOr:
    case IrOpcode::kWord32AtomicPairXor:
    case IrOpcode::kWord32AtomicPairExchange:
    case IrOpcode::kWord32AtomicPairCompareExchange:
      CHECK_LE(index, static_cast<size_t>(1));
      return MachineRepresentation::kWord32;
    default:
      return MachineRepresentation::kNone;
  }
}

// static
bool NodeProperties::IsSame(Node* a, Node* b) {
  for (;;) {
    if (a->opcode() == IrOpcode::kCheckHeapObject) {
      a = GetValueInput(a, 0);
      continue;
    }
    if (b->opcode() == IrOpcode::kCheckHeapObject) {
      b = GetValueInput(b, 0);
      continue;
    }
    return a == b;
  }
}

// static
OptionalMapRef NodeProperties::GetJSCreateMap(JSHeapBroker* broker,
                                              Node* receiver) {
  DCHECK(receiver->opcode() == IrOpcode::kJSCreate ||
         receiver->opcode() == IrOpcode::kJSCreateArray);
  HeapObjectMatcher mtarget(GetValueInput(receiver, 0));
  HeapObjectMatcher mnewtarget(GetValueInput(receiver, 1));
  if (mtarget.HasResolvedValue() && mnewtarget.HasResolvedValue() &&
      mnewtarget.Ref(broker).IsJSFunction()) {
    ObjectRef target = mtarget.Ref(broker);
    JSFunctionRef newtarget = mnewtarget.Ref(broker).AsJSFunction();
    if (newtarget.map(broker).has_prototype_slot() &&
        newtarget.has_initial_map(broker)) {
      MapRef initial_map = newtarget.initial_map(broker);
      if (initial_map.GetConstructor(broker).equals(target)) {
        DCHECK(target.AsJSFunction().map(broker).is_constructor());
        DCHECK(newtarget.map(broker).is_constructor());
        return initial_map;
      }
    }
  }
  return base::nullopt;
}

// static
NodeProperties::InferMapsResult NodeProperties::InferMapsUnsafe(
    JSHeapBroker* broker, Node* receiver, Effect effect,
    ZoneRefSet<Map>* maps_out) {
  HeapObjectMatcher m(receiver);
  if (m.HasResolvedValue()) {
    HeapObjectRef ref = m.Ref(broker);
    // We don't use ICs for the Array.prototype and the Object.prototype
    // because the runtime has to be able to intercept them properly, so
    // we better make sure that TurboFan doesn't outsmart the system here
    // by storing to elements of either prototype directly.
    //
    // TODO(bmeurer): This can be removed once the Array.prototype and
    // Object.prototype have NO_ELEMENTS elements kind.
    if (!ref.IsJSObject() ||
        !broker->IsArrayOrObjectPrototype(ref.AsJSObject())) {
      if (ref.map(broker).is_stable()) {
        // The {receiver_map} is only reliable when we install a stability
        // code dependency.
        *maps_out = ZoneRefSet<Map>{ref.map(broker)};
        return kUnreliableMaps;
      }
    }
  }
  InferMapsResult result = kReliableMaps;
  while (true) {
    switch (effect->opcode()) {
      case IrOpcode::kMapGuard: {
        Node* const object = GetValueInput(effect, 0);
        if (IsSame(receiver, object)) {
          *maps_out = MapGuardMapsOf(effect->op());
          return result;
        }
        break;
      }
      case IrOpcode::kCheckMaps: {
        Node* const object = GetValueInput(effect, 0);
        if (IsSame(receiver, object)) {
          *maps_out = CheckMapsParametersOf(effect->op()).maps();
          return result;
        }
        break;
      }
      case IrOpcode::kJSCreate: {
        if (IsSame(receiver, effect)) {
          OptionalMapRef initial_map = GetJSCreateMap(broker, receiver);
          if (initial_map.has_value()) {
            *maps_out = ZoneRefSet<Map>{initial_map.value()};
            return result;
          }
          // We reached the allocation of the {receiver}.
          return kNoMaps;
        }
        result = kUnreliableMaps;  // JSCreate can have side-effect.
        break;
      }
      case IrOpcode::kJSCreatePromise: {
        if (IsSame(receiver, effect)) {
          *maps_out = ZoneRefSet<Map>{broker->target_native_context()
                                          .promise_function(broker)
                                          .initial_map(broker)};
          return result;
        }
        break;
      }
      case IrOpcode::kStoreField: {
        // We only care about StoreField of maps.
        Node* const object = GetValueInput(effect, 0);
        FieldAccess const& access = FieldAccessOf(effect->op());
        if (access.base_is_tagged == kTaggedBase &&
            access.offset == HeapObject::kMapOffset) {
          if (IsSame(receiver, object)) {
            Node* const value = GetValueInput(effect, 1);
            HeapObjectMatcher m2(value);
            if (m2.HasResolvedValue()) {
              *maps_out = ZoneRefSet<Map>{m2.Ref(broker).AsMap()};
              return result;
            }
          }
          // Without alias analysis we cannot tell whether this
          // StoreField[map] affects {receiver} or not.
          result = kUnreliableMaps;
        }
        break;
      }
      case IrOpcode::kJSStoreMessage:
      case IrOpcode::kJSStoreModule:
      case IrOpcode::kStoreElement:
      case IrOpcode::kStoreTypedElement: {
        // These never change the map of objects.
        break;
      }
      case IrOpcode::kFinishRegion: {
        // FinishRegion renames the output of allocations, so we need
        // to update the {receiver} that we are looking for, if the
        // {receiver} matches the current {effect}.
        if (IsSame(receiver, effect)) receiver = GetValueInput(effect, 0);
        break;
      }
      case IrOpcode::kEffectPhi: {
        Node* control = GetControlInput(effect);
        if (control->opcode() != IrOpcode::kLoop) {
          DCHECK(control->opcode() == IrOpcode::kDead ||
                 control->opcode() == IrOpcode::kMerge);
          return kNoMaps;
        }

        // Continue search for receiver map outside the loop. Since operations
        // inside the loop may change the map, the result is unreliable.
        effect = GetEffectInput(effect, 0);
        result = kUnreliableMaps;
        continue;
      }
      default: {
        DCHECK_EQ(1, effect->op()->EffectOutputCount());
        if (effect->op()->EffectInputCount() != 1) {
          // Didn't find any appropriate CheckMaps node.
          return kNoMaps;
        }
        if (!effect->op()->HasProperty(Operator::kNoWrite)) {
          // Without alias/escape analysis we cannot tell whether this
          // {effect} affects {receiver} or not.
          result = kUnreliableMaps;
        }
        break;
      }
    }

    // Stop walking the effect chain once we hit the definition of
    // the {receiver} along the {effect}s.
    if (IsSame(receiver, effect)) return kNoMaps;

    // Continue with the next {effect}.
    DCHECK_EQ(1, effect->op()->EffectInputCount());
    effect = NodeProperties::GetEffectInput(effect);
  }
}

// static
bool NodeProperties::NoObservableSideEffectBetween(Node* effect,
                                                   Node* dominator) {
  while (effect != dominator) {
    if (effect->op()->EffectInputCount() == 1 &&
        effect->op()->properties() & Operator::kNoWrite) {
      effect = NodeProperties::GetEffectInput(effect);
    } else {
      return false;
    }
  }
  return true;
}

// static
bool NodeProperties::CanBePrimitive(JSHeapBroker* broker, Node* receiver,
                                    Effect effect) {
  switch (receiver->opcode()) {
#define CASE(Opcode) case IrOpcode::k##Opcode:
    JS_CONSTRUCT_OP_LIST(CASE)
    JS_CREATE_OP_LIST(CASE)
#undef CASE
    case IrOpcode::kCheckReceiver:
    case IrOpcode::kConvertReceiver:
    case IrOpcode::kJSGetSuperConstructor:
    case IrOpcode::kJSToObject:
      return false;
    case IrOpcode::kHeapConstant: {
      HeapObjectRef value = HeapObjectMatcher(receiver).Ref(broker);
      return value.map(broker).IsPrimitiveMap();
    }
    default: {
      MapInference inference(broker, receiver, effect);
      return !inference.HaveMaps() ||
             !inference.AllOfInstanceTypesAreJSReceiver();
    }
  }
}

// static
bool NodeProperties::CanBeNullOrUndefined(JSHeapBroker* broker, Node* receiver,
                                          Effect effect) {
  if (CanBePrimitive(broker, receiver, effect)) {
    switch (receiver->opcode()) {
      case IrOpcode::kCheckInternalizedString:
      case IrOpcode::kCheckNumber:
      case IrOpcode::kCheckSmi:
      case IrOpcode::kCheckString:
      case IrOpcode::kCheckSymbol:
      case IrOpcode::kJSToLength:
      case IrOpcode::kJSToName:
      case IrOpcode::kJSToNumber:
      case IrOpcode::kJSToNumberConvertBigInt:
      case IrOpcode::kJSToNumeric:
      case IrOpcode::kJSToString:
      case IrOpcode::kToBoolean:
        return false;
      case IrOpcode::kHeapConstant: {
        HeapObjectRef value = HeapObjectMatcher(receiver).Ref(broker);
        OddballType type = value.map(broker).oddball_type(broker);
        return type == OddballType::kNull || type == OddballType::kUndefined;
      }
      default:
        return true;
    }
  }
  return false;
}

// static
Node* NodeProperties::GetOuterContext(Node* node, size_t* depth) {
  Node* context = NodeProperties::GetContextInput(node);
  while (*depth > 0 &&
         IrOpcode::IsContextChainExtendingOpcode(context->opcode())) {
    context = NodeProperties::GetContextInput(context);
    (*depth)--;
  }
  return context;
}

// static
Type NodeProperties::GetTypeOrAny(const Node* node) {
  return IsTyped(node) ? node->type() : Type::Any();
}

// static
bool NodeProperties::AllValueInputsAreTyped(Node* node) {
  int input_count = node->op()->ValueInputCount();
  for (int index = 0; index < input_count; ++index) {
    if (!IsTyped(GetValueInput(node, index))) return false;
  }
  return true;
}

// static
bool NodeProperties::IsInputRange(Edge edge, int first, int num) {
  if (num == 0) return false;
  int const index = edge.index();
  return first <= index && index < first + num;
}

// static
size_t NodeProperties::HashCode(Node* node) {
  size_t h = base::hash_combine(node->op()->HashCode(), node->InputCount());
  for (Node* input : node->inputs()) {
    h = base::hash_combine(h, input->id());
  }
  return h;
}

// static
bool NodeProperties::Equals(Node* a, Node* b) {
  DCHECK_NOT_NULL(a);
  DCHECK_NOT_NULL(b);
  DCHECK_NOT_NULL(a->op());
  DCHECK_NOT_NULL(b->op());
  if (!a->op()->Equals(b->op())) return false;
  if (a->InputCount() != b->InputCount()) return false;
  Node::Inputs aInputs = a->inputs();
  Node::Inputs bInputs = b->inputs();

  auto aIt = aInputs.begin();
  auto bIt = bInputs.begin();
  auto aEnd = aInputs.end();

  for (; aIt != aEnd; ++aIt, ++bIt) {
    DCHECK_NOT_NULL(*aIt);
    DCHECK_NOT_NULL(*bIt);
    if ((*aIt)->id() != (*bIt)->id()) return false;
  }
  return true;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
