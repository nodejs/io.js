// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-context-specialization.h"

#include "src/compiler/access-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/const-tracking-let-helpers.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/objects/contexts-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

Reduction JSContextSpecialization::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kParameter:
      return ReduceParameter(node);
    case IrOpcode::kJSLoadContext:
      return ReduceJSLoadContext(node);
    case IrOpcode::kJSStoreContext:
      return ReduceJSStoreContext(node);
    case IrOpcode::kJSStoreScriptContext:
      return ReduceJSStoreScriptContext(node);
    case IrOpcode::kJSGetImportMeta:
      return ReduceJSGetImportMeta(node);
    default:
      break;
  }
  return NoChange();
}

Reduction JSContextSpecialization::ReduceParameter(Node* node) {
  DCHECK_EQ(IrOpcode::kParameter, node->opcode());
  int const index = ParameterIndexOf(node->op());
  if (index == Linkage::kJSCallClosureParamIndex) {
    // Constant-fold the function parameter {node}.
    Handle<JSFunction> function;
    if (closure().ToHandle(&function)) {
      Node* value =
          jsgraph()->ConstantNoHole(MakeRef(broker_, function), broker());
      return Replace(value);
    }
  }
  return NoChange();
}

Reduction JSContextSpecialization::SimplifyJSLoadContext(Node* node,
                                                         Node* new_context,
                                                         size_t new_depth) {
  DCHECK_EQ(IrOpcode::kJSLoadContext, node->opcode());
  const ContextAccess& access = ContextAccessOf(node->op());
  DCHECK_LE(new_depth, access.depth());

  if (new_depth == access.depth() &&
      new_context == NodeProperties::GetContextInput(node)) {
    return NoChange();
  }

  const Operator* op = jsgraph_->javascript()->LoadContext(
      new_depth, access.index(), access.immutable());
  NodeProperties::ReplaceContextInput(node, new_context);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

Reduction JSContextSpecialization::SimplifyJSStoreContext(Node* node,
                                                          Node* new_context,
                                                          size_t new_depth) {
  DCHECK_EQ(IrOpcode::kJSStoreContext, node->opcode());
  const ContextAccess& access = ContextAccessOf(node->op());
  DCHECK_LE(new_depth, access.depth());

  if (new_depth == access.depth() &&
      new_context == NodeProperties::GetContextInput(node)) {
    return NoChange();
  }

  const Operator* op =
      jsgraph_->javascript()->StoreContext(new_depth, access.index());
  NodeProperties::ReplaceContextInput(node, new_context);
  NodeProperties::ChangeOp(node, op);
  return Changed(node);
}

namespace {

bool IsContextParameter(Node* node) {
  DCHECK_EQ(IrOpcode::kParameter, node->opcode());
  return ParameterIndexOf(node->op()) ==
         StartNode{NodeProperties::GetValueInput(node, 0)}
             .ContextParameterIndex_MaybeNonStandardLayout();
}

// Given a context {node} and the {distance} from that context to the target
// context (which we want to read from or store to), try to return a
// specialization context.  If successful, update {distance} to whatever
// distance remains from the specialization context.
OptionalContextRef GetSpecializationContext(JSHeapBroker* broker, Node* node,
                                            size_t* distance,
                                            Maybe<OuterContext> maybe_outer) {
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      // TODO(jgruber,chromium:1209798): Using kAssumeMemoryFence works around
      // the fact that the graph stores handles (and not refs). The assumption
      // is that any handle inserted into the graph is safe to read; but we
      // don't preserve the reason why it is safe to read. Thus we must
      // over-approximate here and assume the existence of a memory fence. In
      // the future, we should consider having the graph store ObjectRefs or
      // ObjectData pointer instead, which would make new ref construction here
      // unnecessary.
      HeapObjectRef object =
          MakeRefAssumeMemoryFence(broker, HeapConstantOf(node->op()));
      if (object.IsContext()) return object.AsContext();
      break;
    }
    case IrOpcode::kParameter: {
      OuterContext outer;
      if (maybe_outer.To(&outer) && IsContextParameter(node) &&
          *distance >= outer.distance) {
        *distance -= outer.distance;
        return MakeRef(broker, outer.context);
      }
      break;
    }
    default:
      break;
  }
  return OptionalContextRef();
}

}  // anonymous namespace

Reduction JSContextSpecialization::ReduceJSLoadContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSLoadContext, node->opcode());

  const ContextAccess& access = ContextAccessOf(node->op());
  size_t depth = access.depth();

  // First walk up the context chain in the graph as far as possible.
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  OptionalContextRef maybe_concrete =
      GetSpecializationContext(broker(), context, &depth, outer());
  if (!maybe_concrete.has_value()) {
    // We do not have a concrete context object, so we can only partially reduce
    // the load by folding-in the outer context node.
    return SimplifyJSLoadContext(node, context, depth);
  }

  // Now walk up the concrete context chain for the remaining depth.
  ContextRef concrete = maybe_concrete.value();
  concrete = concrete.previous(broker(), &depth);
  if (depth > 0) {
    TRACE_BROKER_MISSING(broker(), "previous value for context " << concrete);
    return SimplifyJSLoadContext(
        node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
  }

  if (!access.immutable() &&
      !broker()->dependencies()->DependOnConstTrackingLet(
          concrete, access.index(), broker())) {
    // We found the requested context object but since the context slot is
    // mutable we can only partially reduce the load.
    return SimplifyJSLoadContext(
        node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
  }

  // This will hold the final value, if we can figure it out.
  OptionalObjectRef maybe_value;
  maybe_value = concrete.get(broker(), static_cast<int>(access.index()));

  if (!maybe_value.has_value()) {
    TRACE_BROKER_MISSING(broker(), "slot value " << access.index()
                                                 << " for context "
                                                 << concrete);
    return SimplifyJSLoadContext(
        node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
  }

  // Even though the context slot is immutable, the context might have escaped
  // before the function to which it belongs has initialized the slot.
  // We must be conservative and check if the value in the slot is currently
  // the hole or undefined. Only if it is neither of these, can we be sure
  // that it won't change anymore.
  if (maybe_value->IsUndefined() || maybe_value->IsTheHole()) {
    return SimplifyJSLoadContext(
        node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
  }

  // Success. The context load can be replaced with the constant.
  Node* constant = jsgraph_->ConstantNoHole(*maybe_value, broker());
  ReplaceWithValue(node, constant);
  return Replace(constant);
}


Reduction JSContextSpecialization::ReduceJSStoreContext(Node* node) {
  DCHECK_EQ(IrOpcode::kJSStoreContext, node->opcode());

  const ContextAccess& access = ContextAccessOf(node->op());
  size_t depth = access.depth();

  // First walk up the context chain in the graph until we reduce the depth to 0
  // or hit a node that does not have a CreateXYZContext operator.
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  OptionalContextRef maybe_concrete =
      GetSpecializationContext(broker(), context, &depth, outer());
  if (!maybe_concrete.has_value()) {
    // We do not have a concrete context object, so we can only partially reduce
    // the load by folding-in the outer context node.
    return SimplifyJSStoreContext(node, context, depth);
  }

  // Now walk up the concrete context chain for the remaining depth.
  ContextRef concrete = maybe_concrete.value();
  concrete = concrete.previous(broker(), &depth);
  if (depth > 0) {
    TRACE_BROKER_MISSING(broker(), "previous value for context " << concrete);
    return SimplifyJSStoreContext(
        node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
  }

  return SimplifyJSStoreContext(
      node, jsgraph()->ConstantNoHole(concrete, broker()), depth);
}

Reduction JSContextSpecialization::ReduceJSStoreScriptContext(Node* node) {
  DCHECK(v8_flags.const_tracking_let);
  DCHECK_EQ(IrOpcode::kJSStoreScriptContext, node->opcode());

  const ContextAccess& access = ContextAccessOf(node->op());
  size_t depth = access.depth();
  int side_data_index = ConstTrackingLetSideDataIndexForAccess(access.index());

  // First walk up the context chain in the graph until we reduce the depth to 0
  // or hit a node that does not have a CreateXYZContext operator.
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  OptionalContextRef maybe_context =
      GetSpecializationContext(broker(), context, &depth, outer());
  if (IsConstTrackingLetVariableSurelyNotConstant(maybe_context, depth,
                                                  side_data_index, broker())) {
    // The value is not a constant any more, so we don't need to generate
    // code for invalidating the side data.
    const Operator* op =
        jsgraph_->javascript()->StoreContext(access.depth(), access.index());
    NodeProperties::ChangeOp(node, op);
    return Changed(node);
  }

  // The value might be a constant. Generate code which checks the side data and
  // potentially invalidates the constness.
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Generate code to walk up the contexts the remaining depth.
  for (size_t i = 0; i < depth; ++i) {
    context = effect = jsgraph_->graph()->NewNode(
        jsgraph_->simplified()->LoadField(
            AccessBuilder::ForContextSlotKnownPointer(Context::PREVIOUS_INDEX)),
        context, effect, control);
  }

  GenerateCheckConstTrackingLetSideData(context, &effect, &control,
                                        side_data_index, jsgraph_);

  // If we're still here (not deopted) the side data implied that the value was
  // already not a constant, so we can just store into it.
  const Operator* op = jsgraph_->javascript()->StoreContext(0, access.index());
  Node* new_store = jsgraph_->graph()->NewNode(
      op, NodeProperties::GetValueInput(node, 0), context, effect, control);
  return Replace(new_store);
}

OptionalContextRef GetModuleContext(JSHeapBroker* broker, Node* node,
                                    Maybe<OuterContext> maybe_context) {
  size_t depth = std::numeric_limits<size_t>::max();
  Node* context = NodeProperties::GetOuterContext(node, &depth);

  auto find_context = [broker](ContextRef c) {
    while (c.map(broker).instance_type() != MODULE_CONTEXT_TYPE) {
      size_t depth = 1;
      c = c.previous(broker, &depth);
      CHECK_EQ(depth, 0);
    }
    return c;
  };

  switch (context->opcode()) {
    case IrOpcode::kHeapConstant: {
      // TODO(jgruber,chromium:1209798): Using kAssumeMemoryFence works around
      // the fact that the graph stores handles (and not refs). The assumption
      // is that any handle inserted into the graph is safe to read; but we
      // don't preserve the reason why it is safe to read. Thus we must
      // over-approximate here and assume the existence of a memory fence. In
      // the future, we should consider having the graph store ObjectRefs or
      // ObjectData pointer instead, which would make new ref construction here
      // unnecessary.
      HeapObjectRef object =
          MakeRefAssumeMemoryFence(broker, HeapConstantOf(context->op()));
      if (object.IsContext()) {
        return find_context(object.AsContext());
      }
      break;
    }
    case IrOpcode::kParameter: {
      OuterContext outer;
      if (maybe_context.To(&outer) && IsContextParameter(context)) {
        return find_context(MakeRef(broker, outer.context));
      }
      break;
    }
    default:
      break;
  }

  return OptionalContextRef();
}

Reduction JSContextSpecialization::ReduceJSGetImportMeta(Node* node) {
  OptionalContextRef maybe_context = GetModuleContext(broker(), node, outer());
  if (!maybe_context.has_value()) return NoChange();

  ContextRef context = maybe_context.value();
  OptionalObjectRef module = context.get(broker(), Context::EXTENSION_INDEX);
  if (!module.has_value()) return NoChange();
  OptionalObjectRef import_meta =
      module->AsSourceTextModule().import_meta(broker());
  if (!import_meta.has_value()) return NoChange();
  if (!import_meta->IsJSObject()) {
    DCHECK(import_meta->IsTheHole());
    // The import.meta object has not yet been created. Let JSGenericLowering
    // replace the operator with a runtime call.
    return NoChange();
  }

  Node* import_meta_const = jsgraph()->ConstantNoHole(*import_meta, broker());
  ReplaceWithValue(node, import_meta_const);
  return Changed(import_meta_const);
}

Isolate* JSContextSpecialization::isolate() const {
  return jsgraph()->isolate();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
