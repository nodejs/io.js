// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/wasm-gc-operator-reducer.h"

#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/wasm-compiler-definitions.h"
#include "src/wasm/wasm-subtyping.h"

namespace v8 {
namespace internal {
namespace compiler {

WasmGCOperatorReducer::WasmGCOperatorReducer(
    Editor* editor, Zone* temp_zone_, MachineGraph* mcgraph,
    const wasm::WasmModule* module, SourcePositionTable* source_position_table)
    : AdvancedReducerWithControlPathState(editor, temp_zone_, mcgraph->graph()),
      mcgraph_(mcgraph),
      gasm_(mcgraph, mcgraph->zone()),
      module_(module),
      source_position_table_(source_position_table) {}

Reduction WasmGCOperatorReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kStart:
      return ReduceStart(node);
    case IrOpcode::kWasmStructGet:
    case IrOpcode::kWasmStructSet:
      return ReduceWasmStructOperation(node);
    case IrOpcode::kWasmArrayLength:
      return ReduceWasmArrayLength(node);
    case IrOpcode::kAssertNotNull:
      return ReduceAssertNotNull(node);
    case IrOpcode::kIsNull:
    case IrOpcode::kIsNotNull:
      return ReduceCheckNull(node);
    case IrOpcode::kWasmTypeCheck:
      return ReduceWasmTypeCheck(node);
    case IrOpcode::kWasmTypeCheckAbstract:
      return ReduceWasmTypeCheckAbstract(node);
    case IrOpcode::kWasmTypeCast:
      return ReduceWasmTypeCast(node);
    case IrOpcode::kWasmTypeCastAbstract:
      return ReduceWasmTypeCastAbstract(node);
    case IrOpcode::kTypeGuard:
      return ReduceTypeGuard(node);
    case IrOpcode::kWasmAnyConvertExtern:
      return ReduceWasmAnyConvertExtern(node);
    case IrOpcode::kMerge:
      return ReduceMerge(node);
    case IrOpcode::kIfTrue:
      return ReduceIf(node, true);
    case IrOpcode::kIfFalse:
      return ReduceIf(node, false);
    case IrOpcode::kDead:
      return NoChange();
    case IrOpcode::kLoop:
      return TakeStatesFromFirstControl(node);
    default:
      if (node->op()->ControlOutputCount() > 0) {
        DCHECK_EQ(1, node->op()->ControlInputCount());
        return TakeStatesFromFirstControl(node);
      } else {
        return NoChange();
      }
  }
}

namespace {
bool InDeadBranch(Node* node) {
  return node->opcode() == IrOpcode::kDead ||
         node->opcode() == IrOpcode::kDeadValue ||
         NodeProperties::GetType(node).AsWasm().type.is_uninhabited();
}

Node* GetAlias(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kWasmTypeCast:
    case IrOpcode::kWasmTypeCastAbstract:
    case IrOpcode::kTypeGuard:
    case IrOpcode::kAssertNotNull:
      return NodeProperties::GetValueInput(node, 0);
    default:
      return nullptr;
  }
}

}  // namespace

Node* WasmGCOperatorReducer::SetType(Node* node, wasm::ValueType type) {
  NodeProperties::SetType(node, Type::Wasm(type, module_, graph()->zone()));
  return node;
}

Reduction WasmGCOperatorReducer::UpdateNodeAndAliasesTypes(
    Node* state_owner, ControlPathTypes parent_state, Node* node,
    wasm::TypeInModule type, bool in_new_block) {
  ControlPathTypes previous_knowledge = GetState(state_owner);
  if (!previous_knowledge.IsEmpty()) {
    NodeWithType current_info = previous_knowledge.LookupState(node);
    if (current_info.IsSet() && current_info.type == type) return NoChange();
  }
  Node* current = node;
  ControlPathTypes current_state = parent_state;
  while (current != nullptr) {
    UpdateStates(state_owner, current_state, current, {current, type},
                 in_new_block);
    current = GetAlias(current);
    current_state = GetState(state_owner);
    in_new_block = false;
  }
  return Changed(state_owner);
}

Reduction WasmGCOperatorReducer::ReduceStart(Node* node) {
  return UpdateStates(node, ControlPathTypes(zone()));
}

wasm::TypeInModule WasmGCOperatorReducer::ObjectTypeFromContext(
    Node* object, Node* control, bool allow_non_wasm) {
  if (object->opcode() == IrOpcode::kDead ||
      object->opcode() == IrOpcode::kDeadValue) {
    return {};
  }
  if (!IsReduced(control)) return {};
  if (allow_non_wasm && !NodeProperties::IsTyped(object)) return {};
  Type raw_type = NodeProperties::GetType(object);
  if (allow_non_wasm && !raw_type.IsWasm()) return {};
  wasm::TypeInModule type_from_node = raw_type.AsWasm();
  ControlPathTypes state = GetState(control);
  NodeWithType type_from_state = state.LookupState(object);
  // We manually resolve TypeGuard aliases in the state.
  while (object->opcode() == IrOpcode::kTypeGuard && !type_from_state.IsSet()) {
    object = NodeProperties::GetValueInput(object, 0);
    type_from_state = state.LookupState(object);
  }
  if (!type_from_state.IsSet()) return type_from_node;
  return wasm::Intersection(type_from_node, type_from_state.type);
}

Reduction WasmGCOperatorReducer::ReduceWasmStructOperation(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kWasmStructGet ||
         node->opcode() == IrOpcode::kWasmStructSet);
  Node* control = NodeProperties::GetControlInput(node);
  if (!IsReduced(control)) return NoChange();
  Node* object = NodeProperties::GetValueInput(node, 0);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();

  if (object_type.type.is_non_nullable()) {
    // If the object is known to be non-nullable in the context, remove the null
    // check.
    auto op_params = OpParameter<WasmFieldInfo>(node->op());
    const Operator* new_op =
        node->opcode() == IrOpcode::kWasmStructGet
            ? simplified()->WasmStructGet(op_params.type, op_params.field_index,
                                          op_params.is_signed,
                                          kWithoutNullCheck)
            : simplified()->WasmStructSet(op_params.type, op_params.field_index,
                                          kWithoutNullCheck);
    NodeProperties::ChangeOp(node, new_op);
  }

  object_type.type = object_type.type.AsNonNull();

  return UpdateNodeAndAliasesTypes(node, GetState(control), object, object_type,
                                   false);
}

Reduction WasmGCOperatorReducer::ReduceWasmArrayLength(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmArrayLength);
  Node* control = NodeProperties::GetControlInput(node);
  if (!IsReduced(control)) return NoChange();
  Node* object = NodeProperties::GetValueInput(node, 0);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();

  if (object_type.type.is_non_nullable()) {
    // If the object is known to be non-nullable in the context, remove the null
    // check.
    const Operator* new_op = simplified()->WasmArrayLength(kWithoutNullCheck);
    NodeProperties::ChangeOp(node, new_op);
  }

  object_type.type = object_type.type.AsNonNull();

  return UpdateNodeAndAliasesTypes(node, GetState(control), object, object_type,
                                   false);
}

// If the condition of this node's branch is a type check or a null check,
// add the additional information about the type-checked node to the path
// state.
Reduction WasmGCOperatorReducer::ReduceIf(Node* node, bool condition) {
  DCHECK(node->opcode() == IrOpcode::kIfTrue ||
         node->opcode() == IrOpcode::kIfFalse);
  Node* branch = NodeProperties::GetControlInput(node);
  if (branch->opcode() == IrOpcode::kDead) return NoChange();
  DCHECK_EQ(branch->opcode(), IrOpcode::kBranch);
  if (!IsReduced(branch)) return NoChange();
  ControlPathTypes parent_state = GetState(branch);
  Node* condition_node = NodeProperties::GetValueInput(branch, 0);
  switch (condition_node->opcode()) {
    case IrOpcode::kWasmTypeCheck:
    case IrOpcode::kWasmTypeCheckAbstract: {
      if (!condition) break;
      Node* object = NodeProperties::GetValueInput(condition_node, 0);
      wasm::TypeInModule object_type = ObjectTypeFromContext(object, branch);
      if (object_type.type.is_uninhabited()) return NoChange();

      wasm::ValueType to_type =
          OpParameter<WasmTypeCheckConfig>(condition_node->op()).to;

      // TODO(12166): Think about {module_} below if we have cross-module
      // inlining.
      wasm::TypeInModule new_type =
          wasm::Intersection(object_type, {to_type, module_});
      return UpdateNodeAndAliasesTypes(node, parent_state, object, new_type,
                                       true);
    }
    case IrOpcode::kIsNull:
    case IrOpcode::kIsNotNull: {
      Node* object = NodeProperties::GetValueInput(condition_node, 0);
      Node* control = NodeProperties::GetControlInput(condition_node);
      wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
      if (object_type.type.is_uninhabited()) return NoChange();
      // If the checked value is null, narrow the type to the corresponding
      // null type, otherwise to a non-null reference.
      bool is_null =
          condition == (condition_node->opcode() == IrOpcode::kIsNull);
      object_type.type = is_null ? wasm::ToNullSentinel(object_type)
                                 : object_type.type.AsNonNull();
      return UpdateNodeAndAliasesTypes(node, parent_state, object, object_type,
                                       true);
    }
    default:
      break;
  }
  return TakeStatesFromFirstControl(node);
}

Reduction WasmGCOperatorReducer::ReduceMerge(Node* node) {
  // Shortcut for the case when we do not know anything about some
  // input.
  Node::Inputs inputs = node->inputs();
  for (Node* input : inputs) {
    if (!IsReduced(input)) return NoChange();
  }

  auto input_it = inputs.begin();

  DCHECK_GT(inputs.count(), 0);

  ControlPathTypes types = GetState(*input_it);
  ++input_it;

  auto input_end = inputs.end();
  for (; input_it != input_end; ++input_it) {
    // Change the current type block list to a longest common prefix of this
    // state list and the other list. (The common prefix should correspond to
    // the state of the common dominator.)
    // TODO(manoskouk): Consider computing unions for some types.
    types.ResetToCommonAncestor(GetState(*input_it));
  }
  return UpdateStates(node, types);
}

Reduction WasmGCOperatorReducer::ReduceAssertNotNull(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kAssertNotNull);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* control = NodeProperties::GetControlInput(node);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();

  // Optimize the check away if the argument is known to be non-null.
  if (object_type.type.is_non_nullable()) {
    // First, relax control.
    ReplaceWithValue(node, node, node, control);
    // Use a TypeGuard node to not lose any type information.
    NodeProperties::ChangeOp(
        node, common()->TypeGuard(NodeProperties::GetType(node)));
    return Changed(node);
  }

  object_type.type = object_type.type.AsNonNull();
  return UpdateNodeAndAliasesTypes(node, GetState(control), node, object_type,
                                   false);
}

Reduction WasmGCOperatorReducer::ReduceCheckNull(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kIsNull ||
         node->opcode() == IrOpcode::kIsNotNull);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* control = NodeProperties::GetControlInput(node);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();

  // Optimize the check away if the argument is known to be non-null.
  if (object_type.type.is_non_nullable()) {
    ReplaceWithValue(node,
                     SetType(gasm_.Int32Constant(
                                 node->opcode() == IrOpcode::kIsNull ? 0 : 1),
                             wasm::kWasmI32));
    node->Kill();
    return Replace(object);  // Irrelevant replacement.
  }

  // Optimize the check away if the argument is known to be null.
  if (object->opcode() == IrOpcode::kNull) {
    ReplaceWithValue(node,
                     SetType(gasm_.Int32Constant(
                                 node->opcode() == IrOpcode::kIsNull ? 1 : 0),
                             wasm::kWasmI32));
    node->Kill();
    return Replace(object);  // Irrelevant replacement.
  }

  return NoChange();
}

Reduction WasmGCOperatorReducer::ReduceWasmAnyConvertExtern(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmAnyConvertExtern);
  // Remove redundant any.convert_extern(extern.convert_any(...)) pattern.
  Node* input = NodeProperties::GetValueInput(node, 0);
  while (input->opcode() == IrOpcode::kTypeGuard) {
    input = NodeProperties::GetValueInput(input, 0);
  }
  if (input->opcode() == IrOpcode::kDead ||
      input->opcode() == IrOpcode::kDeadValue) {
    return NoChange();
  }
  if (input->opcode() == IrOpcode::kWasmExternConvertAny) {
    // "Skip" the extern.convert_any which doesn't have an effect on the value.
    input = NodeProperties::GetValueInput(input, 0);
    ReplaceWithValue(node, input);
    node->Kill();
    return Replace(input);
  }
  return TakeStatesFromFirstControl(node);
}

Reduction WasmGCOperatorReducer::ReduceTypeGuard(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kTypeGuard);
  Node* control = NodeProperties::GetControlInput(node);
  Node* object = NodeProperties::GetValueInput(node, 0);

  // Since TypeGuards can be generated for JavaScript, and this phase is run
  // for wasm-into-JS inlining, we cannot assume the object has a wasm type.
  wasm::TypeInModule object_type =
      ObjectTypeFromContext(object, control, /* allow_non_wasm = */ true);
  if (object_type.type.is_uninhabited()) return NoChange();
  wasm::TypeInModule guarded_type = TypeGuardTypeOf(node->op()).AsWasm();

  wasm::TypeInModule new_type = wasm::Intersection(object_type, guarded_type);

  return UpdateNodeAndAliasesTypes(node, GetState(control), node, new_type,
                                   false);
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCast(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCast);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* rtt = NodeProperties::GetValueInput(node, 1);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();
  if (InDeadBranch(rtt)) return NoChange();
  wasm::TypeInModule rtt_type = NodeProperties::GetType(rtt).AsWasm();
  bool to_nullable =
      OpParameter<WasmTypeCheckConfig>(node->op()).to.is_nullable();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(),
                            wasm::HeapType(rtt_type.type.ref_index()),
                            object_type.module, rtt_type.module)) {
    if (to_nullable) {
      // Type cast will always succeed. Turn it into a TypeGuard to not lose any
      // type information.
      // First, relax control.
      ReplaceWithValue(node, node, node, control);
      // Remove rtt input.
      node->RemoveInput(1);
      NodeProperties::ChangeOp(
          node, common()->TypeGuard(NodeProperties::GetType(node)));
      return Changed(node);
    } else {
      gasm_.InitializeEffectControl(effect, control);
      Node* assert_not_null = gasm_.AssertNotNull(object, object_type.type,
                                                  TrapId::kTrapIllegalCast);
      UpdateSourcePosition(assert_not_null, node);
      return Replace(SetType(assert_not_null, object_type.type.AsNonNull()));
    }
  }

  if (wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               wasm::HeapType(rtt_type.type.ref_index()),
                               object_type.module, rtt_type.module)) {
    gasm_.InitializeEffectControl(effect, control);
    // A cast between unrelated types can only succeed if the argument is null.
    // Otherwise, it always fails.
    Node* non_trapping_condition = object_type.type.is_nullable() && to_nullable
                                       ? gasm_.IsNull(object, object_type.type)
                                       : gasm_.Int32Constant(0);
    gasm_.TrapUnless(SetType(non_trapping_condition, wasm::kWasmI32),
                     TrapId::kTrapIllegalCast);
    UpdateSourcePosition(gasm_.effect(), node);
    Node* null_node = SetType(gasm_.Null(object_type.type),
                              wasm::ToNullSentinel(object_type));
    ReplaceWithValue(node, null_node, gasm_.effect(), gasm_.control());
    node->Kill();
    return Replace(null_node);
  }

  // TODO(12166): Think about modules below if we have cross-module inlining.

  // Update the from-type in the type cast.
  WasmTypeCheckConfig current_config =
      OpParameter<WasmTypeCheckConfig>(node->op());
  NodeProperties::ChangeOp(node, gasm_.simplified()->WasmTypeCast(
                                     {object_type.type, current_config.to}));

  wasm::TypeInModule new_type = wasm::Intersection(
      object_type,
      {wasm::ValueType::RefNull(rtt_type.type.ref_index()), module_});

  return UpdateNodeAndAliasesTypes(node, GetState(control), node, new_type,
                                   false);
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCastAbstract(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCastAbstract);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* object = NodeProperties::GetValueInput(node, 0);
  WasmTypeCheckConfig config = OpParameter<WasmTypeCheckConfig>(node->op());

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();
  const bool to_nullable = config.to.is_nullable();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(), config.to.heap_type(),
                            object_type.module)) {
    if (to_nullable || object_type.type.is_non_nullable()) {
      // Type cast will always succeed. Turn it into a TypeGuard to not lose any
      // type information.
      // First, relax control.
      ReplaceWithValue(node, node, node, control);
      NodeProperties::ChangeOp(
          node, common()->TypeGuard(NodeProperties::GetType(node)));
      return Changed(node);
    } else {
      gasm_.InitializeEffectControl(effect, control);
      Node* assert_not_null = gasm_.AssertNotNull(object, object_type.type,
                                                  TrapId::kTrapIllegalCast);
      UpdateSourcePosition(assert_not_null, node);
      return Replace(SetType(assert_not_null, object_type.type.AsNonNull()));
    }
  }

  if (wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               config.to.heap_type(), object_type.module,
                               object_type.module)) {
    gasm_.InitializeEffectControl(effect, control);
    // A cast between unrelated types can only succeed if the argument is null.
    // Otherwise, it always fails.
    Node* non_trapping_condition = object_type.type.is_nullable() && to_nullable
                                       ? gasm_.IsNull(object, object_type.type)
                                       : gasm_.Int32Constant(0);
    gasm_.TrapUnless(SetType(non_trapping_condition, wasm::kWasmI32),
                     TrapId::kTrapIllegalCast);
    UpdateSourcePosition(gasm_.effect(), node);
    Node* null_node = SetType(gasm_.Null(object_type.type),
                              wasm::ToNullSentinel(object_type));
    ReplaceWithValue(node, null_node, gasm_.effect(), gasm_.control());
    node->Kill();
    return Replace(null_node);
  }

  // Update the from-type in the type cast.
  NodeProperties::ChangeOp(node, gasm_.simplified()->WasmTypeCastAbstract(
                                     {object_type.type, config.to}));

  wasm::TypeInModule new_type =
      wasm::Intersection(object_type, {config.to, module_});

  return UpdateNodeAndAliasesTypes(node, GetState(control), node, new_type,
                                   false);
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCheck(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCheck);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* rtt = NodeProperties::GetValueInput(node, 1);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();
  if (InDeadBranch(rtt)) return NoChange();
  wasm::TypeInModule rtt_type = NodeProperties::GetType(rtt).AsWasm();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(),
                            wasm::HeapType(rtt_type.type.ref_index()),
                            object_type.module, rtt_type.module)) {
    bool null_succeeds =
        OpParameter<WasmTypeCheckConfig>(node->op()).to.is_nullable();
    // Type cast will fail only on null.
    gasm_.InitializeEffectControl(effect, control);
    Node* condition = SetType(object_type.type.is_nullable() && !null_succeeds
                                  ? gasm_.IsNotNull(object, object_type.type)
                                  : gasm_.Int32Constant(1),
                              wasm::kWasmI32);
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  if (wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               wasm::HeapType(rtt_type.type.ref_index()),
                               object_type.module, rtt_type.module)) {
    bool null_succeeds =
        OpParameter<WasmTypeCheckConfig>(node->op()).to.is_nullable();
    Node* condition = nullptr;
    if (null_succeeds && object_type.type.is_nullable()) {
      // The cast only succeeds in case of null.
      gasm_.InitializeEffectControl(effect, control);
      condition =
          SetType(gasm_.IsNull(object, object_type.type), wasm::kWasmI32);
    } else {
      // The cast never succeeds.
      condition = SetType(gasm_.Int32Constant(0), wasm::kWasmI32);
    }
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  // TODO(12166): Think about modules below if we have cross-module inlining.

  // Update the from-type in the type cast.
  WasmTypeCheckConfig current_config =
      OpParameter<WasmTypeCheckConfig>(node->op());
  NodeProperties::ChangeOp(node, gasm_.simplified()->WasmTypeCheck(
                                     {object_type.type, current_config.to}));

  return TakeStatesFromFirstControl(node);
}

Reduction WasmGCOperatorReducer::ReduceWasmTypeCheckAbstract(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWasmTypeCheckAbstract);
  Node* object = NodeProperties::GetValueInput(node, 0);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  WasmTypeCheckConfig config = OpParameter<WasmTypeCheckConfig>(node->op());

  wasm::TypeInModule object_type = ObjectTypeFromContext(object, control);
  if (object_type.type.is_uninhabited()) return NoChange();
  const bool null_succeeds = config.to.is_nullable();

  if (wasm::IsHeapSubtypeOf(object_type.type.heap_type(), config.to.heap_type(),
                            object_type.module)) {
    // Type cast will fail only on null.
    gasm_.InitializeEffectControl(effect, control);
    Node* condition = SetType(object_type.type.is_nullable() && !null_succeeds
                                  ? gasm_.IsNotNull(object, object_type.type)
                                  : gasm_.Int32Constant(1),
                              wasm::kWasmI32);
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  // This can never result from user code, only from internal shortcuts,
  // e.g. when using externrefs as strings.
  const bool implicit_internalize =
      config.from.heap_representation() == wasm::HeapType::kExtern &&
      wasm::IsHeapSubtypeOf(config.to.heap_type(),
                            wasm::HeapType(wasm::HeapType::kAny),
                            object_type.module);
  if (!implicit_internalize &&
      wasm::HeapTypesUnrelated(object_type.type.heap_type(),
                               config.to.heap_type(), object_type.module,
                               object_type.module)) {
    Node* condition = nullptr;
    if (null_succeeds && object_type.type.is_nullable()) {
      // The cast only succeeds in case of null.
      gasm_.InitializeEffectControl(effect, control);
      condition =
          SetType(gasm_.IsNull(object, object_type.type), wasm::kWasmI32);
    } else {
      // The cast never succeeds.
      condition = SetType(gasm_.Int32Constant(0), wasm::kWasmI32);
    }
    ReplaceWithValue(node, condition);
    node->Kill();
    return Replace(condition);
  }

  // Update the from-type in the type cast.
  NodeProperties::ChangeOp(node, gasm_.simplified()->WasmTypeCheckAbstract(
                                     {object_type.type, config.to}));

  return TakeStatesFromFirstControl(node);
}

void WasmGCOperatorReducer::UpdateSourcePosition(Node* new_node,
                                                 Node* old_node) {
  if (source_position_table_) {
    SourcePosition position =
        source_position_table_->GetSourcePosition(old_node);
    DCHECK(position.ScriptOffset() != kNoSourcePosition);
    source_position_table_->SetSourcePosition(new_node, position);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
