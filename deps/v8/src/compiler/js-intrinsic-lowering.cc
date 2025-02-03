// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-intrinsic-lowering.h"

#include <stack>

#include "src/codegen/callable.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

JSIntrinsicLowering::JSIntrinsicLowering(Editor* editor, JSGraph* jsgraph,
                                         JSHeapBroker* broker)
    : AdvancedReducer(editor), jsgraph_(jsgraph), broker_(broker) {}

Reduction JSIntrinsicLowering::Reduce(Node* node) {
  if (node->opcode() != IrOpcode::kJSCallRuntime) return NoChange();
  const Runtime::Function* const f =
      Runtime::FunctionForId(CallRuntimeParametersOf(node->op()).id());
  switch (f->function_id) {
    case Runtime::kIsBeingInterpreted:
      return ReduceIsBeingInterpreted(node);
    case Runtime::kTurbofanStaticAssert:
      return ReduceTurbofanStaticAssert(node);
    case Runtime::kVerifyType:
      return ReduceVerifyType(node);
    case Runtime::kCheckTurboshaftTypeOf:
      return ReduceCheckTurboshaftTypeOf(node);
    default:
      break;
  }
  if (f->intrinsic_type != Runtime::IntrinsicType::INLINE) return NoChange();
  switch (f->function_id) {
    case Runtime::kInlineCopyDataProperties:
      return ReduceCopyDataProperties(node);
    case Runtime::kInlineCopyDataPropertiesWithExcludedPropertiesOnStack:
      return ReduceCopyDataPropertiesWithExcludedPropertiesOnStack(node);
    case Runtime::kInlineCreateIterResultObject:
      return ReduceCreateIterResultObject(node);
    case Runtime::kInlineDeoptimizeNow:
      return ReduceDeoptimizeNow(node);
    case Runtime::kInlineGeneratorClose:
      return ReduceGeneratorClose(node);
    case Runtime::kInlineCreateJSGeneratorObject:
      return ReduceCreateJSGeneratorObject(node);
    case Runtime::kInlineAsyncFunctionAwait:
      return ReduceAsyncFunctionAwait(node);
    case Runtime::kInlineAsyncFunctionEnter:
      return ReduceAsyncFunctionEnter(node);
    case Runtime::kInlineAsyncFunctionReject:
      return ReduceAsyncFunctionReject(node);
    case Runtime::kInlineAsyncFunctionResolve:
      return ReduceAsyncFunctionResolve(node);
    case Runtime::kInlineAsyncGeneratorAwait:
      return ReduceAsyncGeneratorAwait(node);
    case Runtime::kInlineAsyncGeneratorReject:
      return ReduceAsyncGeneratorReject(node);
    case Runtime::kInlineAsyncGeneratorResolve:
      return ReduceAsyncGeneratorResolve(node);
    case Runtime::kInlineAsyncGeneratorYieldWithAwait:
      return ReduceAsyncGeneratorYieldWithAwait(node);
    case Runtime::kInlineGeneratorGetResumeMode:
      return ReduceGeneratorGetResumeMode(node);
    case Runtime::kInlineIncBlockCounter:
      return ReduceIncBlockCounter(node);
    case Runtime::kInlineGetImportMetaObject:
      return ReduceGetImportMetaObject(node);
    default:
      break;
  }
  return NoChange();
}

Reduction JSIntrinsicLowering::ReduceCopyDataProperties(Node* node) {
  return Change(
      node, Builtins::CallableFor(isolate(), Builtin::kCopyDataProperties), 0);
}

Reduction
JSIntrinsicLowering::ReduceCopyDataPropertiesWithExcludedPropertiesOnStack(
    Node* node) {
  int input_count =
      static_cast<int>(CallRuntimeParametersOf(node->op()).arity());
  CallDescriptor::Flags flags = CallDescriptor::kNeedsFrameState;
  auto callable = Builtins::CallableFor(
      isolate(), Builtin::kCopyDataPropertiesWithExcludedProperties);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(), input_count - 1, flags,
      node->op()->properties());
  node->InsertInput(graph()->zone(), 0,
                    jsgraph()->HeapConstantNoHole(callable.code()));
  node->InsertInput(graph()->zone(), 2,
                    jsgraph()->SmiConstant(input_count - 1));
  NodeProperties::ChangeOp(node, common()->Call(call_descriptor));
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceCreateIterResultObject(Node* node) {
  Node* const value = NodeProperties::GetValueInput(node, 0);
  Node* const done = NodeProperties::GetValueInput(node, 1);
  Node* const context = NodeProperties::GetContextInput(node);
  Node* const effect = NodeProperties::GetEffectInput(node);
  return Change(node, javascript()->CreateIterResultObject(), value, done,
                context, effect);
}

Reduction JSIntrinsicLowering::ReduceDeoptimizeNow(Node* node) {
  Node* const frame_state = NodeProperties::GetFrameStateInput(node);
  Node* const effect = NodeProperties::GetEffectInput(node);
  Node* const control = NodeProperties::GetControlInput(node);

  Node* deoptimize = graph()->NewNode(
      common()->Deoptimize(DeoptimizeReason::kDeoptimizeNow, FeedbackSource()),
      frame_state, effect, control);
  MergeControlToEnd(graph(), common(), deoptimize);

  node->TrimInputCount(0);
  NodeProperties::ChangeOp(node, common()->Dead());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceCreateJSGeneratorObject(Node* node) {
  Node* const closure = NodeProperties::GetValueInput(node, 0);
  Node* const receiver = NodeProperties::GetValueInput(node, 1);
  Node* const context = NodeProperties::GetContextInput(node);
  Node* const effect = NodeProperties::GetEffectInput(node);
  Node* const control = NodeProperties::GetControlInput(node);
  Operator const* const op = javascript()->CreateGeneratorObject();
  Node* create_generator =
      graph()->NewNode(op, closure, receiver, context, effect, control);
  ReplaceWithValue(node, create_generator, create_generator);
  return Changed(create_generator);
}

Reduction JSIntrinsicLowering::ReduceGeneratorClose(Node* node) {
  Node* const generator = NodeProperties::GetValueInput(node, 0);
  Node* const effect = NodeProperties::GetEffectInput(node);
  Node* const control = NodeProperties::GetControlInput(node);
  Node* const closed =
      jsgraph()->ConstantNoHole(JSGeneratorObject::kGeneratorClosed);
  Node* const undefined = jsgraph()->UndefinedConstant();
  Operator const* const op = simplified()->StoreField(
      AccessBuilder::ForJSGeneratorObjectContinuation());

  ReplaceWithValue(node, undefined, node);
  NodeProperties::RemoveType(node);
  return Change(node, op, generator, closed, effect, control);
}

Reduction JSIntrinsicLowering::ReduceAsyncFunctionAwait(Node* node) {
  return Change(
      node, Builtins::CallableFor(isolate(), Builtin::kAsyncFunctionAwait), 0);
}

Reduction JSIntrinsicLowering::ReduceAsyncFunctionEnter(Node* node) {
  NodeProperties::ChangeOp(node, javascript()->AsyncFunctionEnter());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceAsyncFunctionReject(Node* node) {
  RelaxControls(node);
  NodeProperties::ChangeOp(node, javascript()->AsyncFunctionReject());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceAsyncFunctionResolve(Node* node) {
  RelaxControls(node);
  NodeProperties::ChangeOp(node, javascript()->AsyncFunctionResolve());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceAsyncGeneratorAwait(Node* node) {
  return Change(
      node, Builtins::CallableFor(isolate(), Builtin::kAsyncGeneratorAwait), 0);
}

Reduction JSIntrinsicLowering::ReduceAsyncGeneratorReject(Node* node) {
  return Change(
      node, Builtins::CallableFor(isolate(), Builtin::kAsyncGeneratorReject),
      0);
}

Reduction JSIntrinsicLowering::ReduceAsyncGeneratorResolve(Node* node) {
  return Change(
      node, Builtins::CallableFor(isolate(), Builtin::kAsyncGeneratorResolve),
      0);
}

Reduction JSIntrinsicLowering::ReduceAsyncGeneratorYieldWithAwait(Node* node) {
  return Change(
      node,
      Builtins::CallableFor(isolate(), Builtin::kAsyncGeneratorYieldWithAwait),
      0);
}

Reduction JSIntrinsicLowering::ReduceGeneratorGetResumeMode(Node* node) {
  Node* const generator = NodeProperties::GetValueInput(node, 0);
  Node* const effect = NodeProperties::GetEffectInput(node);
  Node* const control = NodeProperties::GetControlInput(node);
  Operator const* const op =
      simplified()->LoadField(AccessBuilder::ForJSGeneratorObjectResumeMode());

  return Change(node, op, generator, effect, control);
}

Reduction JSIntrinsicLowering::ReduceIsInstanceType(
    Node* node, InstanceType instance_type) {
  // if (%_IsSmi(value)) {
  //   return false;
  // } else {
  //   return %_GetInstanceType(%_GetMap(value)) == instance_type;
  // }
  Node* value = NodeProperties::GetValueInput(node, 0);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), value);
  Node* branch = graph()->NewNode(common()->Branch(), check, control);

  Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
  Node* etrue = effect;
  Node* vtrue = jsgraph()->FalseConstant();

  Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
  Node* efalse = effect;
  Node* map = efalse =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()), value,
                       efalse, if_false);
  Node* map_instance_type = efalse = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapInstanceType()), map, efalse,
      if_false);
  Node* vfalse =
      graph()->NewNode(simplified()->NumberEqual(), map_instance_type,
                       jsgraph()->ConstantNoHole(instance_type));

  Node* merge = graph()->NewNode(common()->Merge(2), if_true, if_false);

  // Replace all effect uses of {node} with the {ephi}.
  Node* ephi = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, merge);
  ReplaceWithValue(node, node, ephi, merge);

  // Turn the {node} into a Phi.
  return Change(node, common()->Phi(MachineRepresentation::kTagged, 2), vtrue,
                vfalse, merge);
}

Reduction JSIntrinsicLowering::ReduceIsJSReceiver(Node* node) {
  return Change(node, simplified()->ObjectIsReceiver());
}

Reduction JSIntrinsicLowering::ReduceTurbofanStaticAssert(Node* node) {
  if (v8_flags.always_turbofan) {
    // Ignore static asserts, as we most likely won't have enough information
    RelaxEffectsAndControls(node);
  } else {
    Node* value = NodeProperties::GetValueInput(node, 0);
    Node* effect = NodeProperties::GetEffectInput(node);
    Node* assert = graph()->NewNode(
        common()->StaticAssert("%TurbofanStaticAssert"), value, effect);
    ReplaceWithValue(node, node, assert, nullptr);
  }
  return Changed(jsgraph_->UndefinedConstant());
}

Reduction JSIntrinsicLowering::ReduceVerifyType(Node* node) {
  Node* value = NodeProperties::GetValueInput(node, 0);
  Node* effect = NodeProperties::GetEffectInput(node);
  effect = graph()->NewNode(simplified()->VerifyType(), value, effect);
  ReplaceWithValue(node, value, effect);
  return Changed(effect);
}

Reduction JSIntrinsicLowering::ReduceCheckTurboshaftTypeOf(Node* node) {
  Node* value = node->InputAt(0);
  if (!v8_flags.turboshaft) {
    RelaxEffectsAndControls(node);
    ReplaceWithValue(node, value);
    return Changed(value);
  }

  Node* pattern = node->InputAt(1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* check = graph()->NewNode(simplified()->CheckTurboshaftTypeOf(), value,
                                 pattern, effect, control);
  ReplaceWithValue(node, value, check);
  return Changed(value);
}

Reduction JSIntrinsicLowering::ReduceIsBeingInterpreted(Node* node) {
  RelaxEffectsAndControls(node);
  return Changed(jsgraph_->FalseConstant());
}

Reduction JSIntrinsicLowering::Change(Node* node, const Operator* op) {
  // Replace all effect uses of {node} with the effect dependency.
  RelaxEffectsAndControls(node);
  // Remove the inputs corresponding to context, effect and control.
  NodeProperties::RemoveNonValueInputs(node);
  // Finally update the operator to the new one.
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceToLength(Node* node) {
  NodeProperties::ChangeOp(node, javascript()->ToLength());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceToObject(Node* node) {
  NodeProperties::ChangeOp(node, javascript()->ToObject());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceToString(Node* node) {
  // ToString is unnecessary if the input is a string.
  HeapObjectMatcher m(NodeProperties::GetValueInput(node, 0));
  if (m.HasResolvedValue() && m.Ref(broker()).IsString()) {
    ReplaceWithValue(node, m.node());
    return Replace(m.node());
  }
  NodeProperties::ChangeOp(node, javascript()->ToString());
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceCall(Node* node) {
  int const arity =
      static_cast<int>(CallRuntimeParametersOf(node->op()).arity());
  static constexpr int kTargetAndReceiver = 2;
  static_assert(JSCallNode::kFeedbackVectorIsLastInput);
  Node* feedback = jsgraph()->UndefinedConstant();
  node->InsertInput(graph()->zone(), arity, feedback);
  NodeProperties::ChangeOp(
      node,
      javascript()->Call(JSCallNode::ArityForArgc(arity - kTargetAndReceiver)));
  return Changed(node);
}

Reduction JSIntrinsicLowering::ReduceIncBlockCounter(Node* node) {
  DCHECK(!Linkage::NeedsFrameStateInput(Runtime::kIncBlockCounter));
  DCHECK(!Linkage::NeedsFrameStateInput(Runtime::kInlineIncBlockCounter));
  return Change(node,
                Builtins::CallableFor(isolate(), Builtin::kIncBlockCounter), 0,
                kDoesNotNeedFrameState);
}

Reduction JSIntrinsicLowering::ReduceGetImportMetaObject(Node* node) {
  NodeProperties::ChangeOp(node, javascript()->GetImportMeta());
  return Changed(node);
}

Reduction JSIntrinsicLowering::Change(Node* node, const Operator* op, Node* a,
                                      Node* b) {
  RelaxControls(node);
  node->ReplaceInput(0, a);
  node->ReplaceInput(1, b);
  node->TrimInputCount(2);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSIntrinsicLowering::Change(Node* node, const Operator* op, Node* a,
                                      Node* b, Node* c) {
  RelaxControls(node);
  node->ReplaceInput(0, a);
  node->ReplaceInput(1, b);
  node->ReplaceInput(2, c);
  node->TrimInputCount(3);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSIntrinsicLowering::Change(Node* node, const Operator* op, Node* a,
                                      Node* b, Node* c, Node* d) {
  RelaxControls(node);
  node->ReplaceInput(0, a);
  node->ReplaceInput(1, b);
  node->ReplaceInput(2, c);
  node->ReplaceInput(3, d);
  node->TrimInputCount(4);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSIntrinsicLowering::Change(Node* node, Callable const& callable,
                                      int stack_parameter_count,
                                      enum FrameStateFlag frame_state_flag) {
  CallDescriptor::Flags flags = frame_state_flag == kNeedsFrameState
                                    ? CallDescriptor::kNeedsFrameState
                                    : CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(), stack_parameter_count, flags,
      node->op()->properties());
  node->InsertInput(graph()->zone(), 0,
                    jsgraph()->HeapConstantNoHole(callable.code()));
  NodeProperties::ChangeOp(node, common()->Call(call_descriptor));
  return Changed(node);
}

Graph* JSIntrinsicLowering::graph() const { return jsgraph()->graph(); }

Isolate* JSIntrinsicLowering::isolate() const { return jsgraph()->isolate(); }

CommonOperatorBuilder* JSIntrinsicLowering::common() const {
  return jsgraph()->common();
}

JSOperatorBuilder* JSIntrinsicLowering::javascript() const {
  return jsgraph_->javascript();
}

SimplifiedOperatorBuilder* JSIntrinsicLowering::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
