// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-native-context-specialization.h"

#include <optional>

#include "src/base/logging.h"
#include "src/builtins/accessors.h"
#include "src/codegen/code-factory.h"
#include "src/common/globals.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/access-info.h"
#include "src/compiler/allocation-builder-inl.h"
#include "src/compiler/allocation-builder.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/const-tracking-let-helpers.h"
#include "src/compiler/frame-states.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/map-inference.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/property-access-builder.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/type-cache.h"
#include "src/flags/flags.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/elements-kind.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/heap-number.h"
#include "src/objects/string.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool HasNumberMaps(JSHeapBroker* broker, ZoneVector<MapRef> const& maps) {
  for (MapRef map : maps) {
    if (map.IsHeapNumberMap()) return true;
  }
  return false;
}

bool HasOnlyJSArrayMaps(JSHeapBroker* broker, ZoneVector<MapRef> const& maps) {
  for (MapRef map : maps) {
    if (!map.IsJSArrayMap()) return false;
  }
  return true;
}

}  // namespace

JSNativeContextSpecialization::JSNativeContextSpecialization(
    Editor* editor, JSGraph* jsgraph, JSHeapBroker* broker, Flags flags,
    Zone* zone, Zone* shared_zone)
    : AdvancedReducer(editor),
      jsgraph_(jsgraph),
      broker_(broker),
      flags_(flags),
      global_object_(
          broker->target_native_context().global_object(broker).object()),
      global_proxy_(
          broker->target_native_context().global_proxy_object(broker).object()),
      zone_(zone),
      shared_zone_(shared_zone),
      type_cache_(TypeCache::Get()),
      created_strings_(zone) {}

Reduction JSNativeContextSpecialization::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kJSAdd:
      return ReduceJSAdd(node);
    case IrOpcode::kJSAsyncFunctionEnter:
      return ReduceJSAsyncFunctionEnter(node);
    case IrOpcode::kJSAsyncFunctionReject:
      return ReduceJSAsyncFunctionReject(node);
    case IrOpcode::kJSAsyncFunctionResolve:
      return ReduceJSAsyncFunctionResolve(node);
    case IrOpcode::kJSGetSuperConstructor:
      return ReduceJSGetSuperConstructor(node);
    case IrOpcode::kJSFindNonDefaultConstructorOrConstruct:
      return ReduceJSFindNonDefaultConstructorOrConstruct(node);
    case IrOpcode::kJSInstanceOf:
      return ReduceJSInstanceOf(node);
    case IrOpcode::kJSHasInPrototypeChain:
      return ReduceJSHasInPrototypeChain(node);
    case IrOpcode::kJSOrdinaryHasInstance:
      return ReduceJSOrdinaryHasInstance(node);
    case IrOpcode::kJSPromiseResolve:
      return ReduceJSPromiseResolve(node);
    case IrOpcode::kJSResolvePromise:
      return ReduceJSResolvePromise(node);
    case IrOpcode::kJSLoadGlobal:
      return ReduceJSLoadGlobal(node);
    case IrOpcode::kJSStoreGlobal:
      return ReduceJSStoreGlobal(node);
    case IrOpcode::kJSLoadNamed:
      return ReduceJSLoadNamed(node);
    case IrOpcode::kJSLoadNamedFromSuper:
      return ReduceJSLoadNamedFromSuper(node);
    case IrOpcode::kJSSetNamedProperty:
      return ReduceJSSetNamedProperty(node);
    case IrOpcode::kJSHasProperty:
      return ReduceJSHasProperty(node);
    case IrOpcode::kJSLoadProperty:
      return ReduceJSLoadProperty(node);
    case IrOpcode::kJSSetKeyedProperty:
      return ReduceJSSetKeyedProperty(node);
    case IrOpcode::kJSDefineKeyedOwnProperty:
      return ReduceJSDefineKeyedOwnProperty(node);
    case IrOpcode::kJSDefineNamedOwnProperty:
      return ReduceJSDefineNamedOwnProperty(node);
    case IrOpcode::kJSDefineKeyedOwnPropertyInLiteral:
      return ReduceJSDefineKeyedOwnPropertyInLiteral(node);
    case IrOpcode::kJSStoreInArrayLiteral:
      return ReduceJSStoreInArrayLiteral(node);
    case IrOpcode::kJSToObject:
      return ReduceJSToObject(node);
    case IrOpcode::kJSToString:
      return ReduceJSToString(node);
    case IrOpcode::kJSGetIterator:
      return ReduceJSGetIterator(node);
    default:
      break;
  }
  return NoChange();
}

// If {node} is a HeapConstant<String>, return the String's length. If {node} is
// a number, return the maximum size that a stringified number can have.
// Otherwise, we can't easily convert {node} into a String, and we return
// nullopt.
// static
std::optional<size_t> JSNativeContextSpecialization::GetMaxStringLength(
    JSHeapBroker* broker, Node* node) {
  HeapObjectMatcher matcher(node);
  if (matcher.HasResolvedValue() && matcher.Ref(broker).IsString()) {
    StringRef input = matcher.Ref(broker).AsString();
    return input.length();
  }

  NumberMatcher number_matcher(node);
  if (number_matcher.HasResolvedValue()) {
    return kMaxDoubleStringLength;
  }

  // We don't support objects with possibly monkey-patched prototype.toString
  // as it might have side-effects, so we shouldn't attempt lowering them.
  return std::nullopt;
}

Reduction JSNativeContextSpecialization::ReduceJSToString(Node* node) {
  DCHECK_EQ(IrOpcode::kJSToString, node->opcode());
  Node* const input = node->InputAt(0);

  HeapObjectMatcher matcher(input);
  if (matcher.HasResolvedValue() && matcher.Ref(broker()).IsString()) {
    Reduction reduction = Changed(input);  // JSToString(x:string) => x
    ReplaceWithValue(node, reduction.replacement());
    return reduction;
  }

  // TODO(turbofan): This optimization is weaker than what we used to have
  // in js-typed-lowering for OrderedNumbers. We don't have types here though,
  // so alternative approach should be designed if this causes performance
  // regressions and the stronger optimization should be re-implemented.
  NumberMatcher number_matcher(input);
  if (number_matcher.HasResolvedValue()) {
    DirectHandle<Object> num_obj =
        broker()
            ->local_isolate_or_isolate()
            ->factory()
            ->NewNumber<AllocationType::kOld>(number_matcher.ResolvedValue());
    Handle<String> num_str =
        broker()->local_isolate_or_isolate()->factory()->NumberToString(
            num_obj);
    Node* reduced = graph()->NewNode(
        common()->HeapConstant(broker()->CanonicalPersistentHandle(num_str)));

    ReplaceWithValue(node, reduced);
    return Replace(reduced);
  }

  return NoChange();
}

// Return a String from {node}, which should be either a HeapConstant<String>
// (in which case we return the String), or a number (in which case we convert
// it to a String).
Handle<String> JSNativeContextSpecialization::CreateStringConstant(Node* node) {
  DCHECK(IrOpcode::IsConstantOpcode(node->opcode()));
  NumberMatcher number_matcher(node);
  if (number_matcher.HasResolvedValue()) {
    DirectHandle<Object> num_obj =
        broker()
            ->local_isolate_or_isolate()
            ->factory()
            ->NewNumber<AllocationType::kOld>(number_matcher.ResolvedValue());
    // Note that we do not store the result of NumberToString in
    // {created_strings_}, because the latter is used to know if strings are
    // safe to be used in the background, but we always have as additional
    // information the node from which the string was created ({node} is that
    // case), and if this node is a kHeapNumber, then we know that we must have
    // created the string, and that there it is safe to read. So, we don't need
    // {created_strings_} in that case.
    return broker()->local_isolate_or_isolate()->factory()->NumberToString(
        num_obj);
  } else {
    HeapObjectMatcher matcher(node);
    if (matcher.HasResolvedValue() && matcher.Ref(broker()).IsString()) {
      return matcher.Ref(broker()).AsString().object();
    } else {
      UNREACHABLE();
    }
  }
}

namespace {
bool IsStringConstant(JSHeapBroker* broker, Node* node) {
  HeapObjectMatcher matcher(node);
  return matcher.HasResolvedValue() && matcher.Ref(broker).IsString();
}

bool IsStringWithNonAccessibleContent(JSHeapBroker* broker, Node* node) {
  HeapObjectMatcher matcher(node);
  if (matcher.HasResolvedValue() && matcher.Ref(broker).IsString()) {
    StringRef input = matcher.Ref(broker).AsString();
    return !input.IsContentAccessible();
  }
  return false;
}
}  // namespace

Reduction JSNativeContextSpecialization::ReduceJSAsyncFunctionEnter(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSAsyncFunctionEnter, node->opcode());
  Node* closure = NodeProperties::GetValueInput(node, 0);
  Node* receiver = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Node* frame_state = NodeProperties::GetFrameStateInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  if (!dependencies()->DependOnPromiseHookProtector()) return NoChange();

  // Create the promise for the async function.
  Node* promise = effect =
      graph()->NewNode(javascript()->CreatePromise(), context, effect);

  // Create the JSAsyncFunctionObject based on the SharedFunctionInfo
  // extracted from the top-most frame in {frame_state}.
  SharedFunctionInfoRef shared = MakeRef(
      broker(),
      FrameStateInfoOf(frame_state->op()).shared_info().ToHandleChecked());
  DCHECK(shared.is_compiled());
  int register_count =
      shared.internal_formal_parameter_count_without_receiver() +
      shared.GetBytecodeArray(broker()).register_count();
  MapRef fixed_array_map = broker()->fixed_array_map();
  AllocationBuilder ab(jsgraph(), broker(), effect, control);
  if (!ab.CanAllocateArray(register_count, fixed_array_map)) {
    return NoChange();
  }
  Node* value = effect =
      graph()->NewNode(javascript()->CreateAsyncFunctionObject(register_count),
                       closure, receiver, promise, context, effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceJSAsyncFunctionReject(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSAsyncFunctionReject, node->opcode());
  Node* async_function_object = NodeProperties::GetValueInput(node, 0);
  Node* reason = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Node* frame_state = NodeProperties::GetFrameStateInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  if (!dependencies()->DependOnPromiseHookProtector()) return NoChange();

  // Load the promise from the {async_function_object}.
  Node* promise = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSAsyncFunctionObjectPromise()),
      async_function_object, effect, control);

  // Create a nested frame state inside the current method's most-recent
  // {frame_state} that will ensure that lazy deoptimizations at this
  // point will still return the {promise} instead of the result of the
  // JSRejectPromise operation (which yields undefined).
  Node* parameters[] = {promise};
  frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kAsyncFunctionLazyDeoptContinuation, context,
      parameters, arraysize(parameters), frame_state,
      ContinuationFrameStateMode::LAZY);

  // Disable the additional debug event for the rejection since a
  // debug event already happend for the exception that got us here.
  Node* debug_event = jsgraph()->FalseConstant();
  effect = graph()->NewNode(javascript()->RejectPromise(), promise, reason,
                            debug_event, context, frame_state, effect, control);
  ReplaceWithValue(node, promise, effect, control);
  return Replace(promise);
}

Reduction JSNativeContextSpecialization::ReduceJSAsyncFunctionResolve(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSAsyncFunctionResolve, node->opcode());
  Node* async_function_object = NodeProperties::GetValueInput(node, 0);
  Node* value = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Node* frame_state = NodeProperties::GetFrameStateInput(node);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  if (!dependencies()->DependOnPromiseHookProtector()) return NoChange();

  // Load the promise from the {async_function_object}.
  Node* promise = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSAsyncFunctionObjectPromise()),
      async_function_object, effect, control);

  // Create a nested frame state inside the current method's most-recent
  // {frame_state} that will ensure that lazy deoptimizations at this
  // point will still return the {promise} instead of the result of the
  // JSResolvePromise operation (which yields undefined).
  Node* parameters[] = {promise};
  frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kAsyncFunctionLazyDeoptContinuation, context,
      parameters, arraysize(parameters), frame_state,
      ContinuationFrameStateMode::LAZY);

  effect = graph()->NewNode(javascript()->ResolvePromise(), promise, value,
                            context, frame_state, effect, control);
  ReplaceWithValue(node, promise, effect, control);
  return Replace(promise);
}

// Concatenates {left} and {right}. The result is fairly similar to creating a
// new ConsString with {left} and {right} and then flattening it, which we don't
// do because String::Flatten does not support background threads. Rather than
// implementing a full String::Flatten for background threads, we prefered to
// implement this Concatenate function, which, unlike String::Flatten, doesn't
// need to replace ConsStrings by ThinStrings.
Handle<String> JSNativeContextSpecialization::Concatenate(
    Handle<String> left, Handle<String> right) {
  if (left->length() == 0) return right;
  if (right->length() == 0) return left;

  // Repeated concatenations have a quadratic cost (eg, "s+=a;s+=b;s+=c;...").
  // Rather than doing static analysis to determine how many concatenations we
  // there are and how many uses the result of each concatenation have, we
  // generate ConsString when the result of the concatenation would have more
  // than {kConstantStringFlattenMaxSize} characters, and flattened SeqString
  // otherwise.
  // TODO(dmercadier): ideally, we would like to get rid of this constant, and
  // always flatten. This requires some care to avoid the quadratic worst-case.
  constexpr int32_t kConstantStringFlattenMaxSize = 100;

  int32_t length = left->length() + right->length();
  if (length > kConstantStringFlattenMaxSize) {
    // The generational write-barrier doesn't work in background threads, so,
    // if {left} or {right} are in the young generation, we would have to copy
    // them to the local heap (which is old) before creating the (old)
    // ConsString. But, copying a ConsString instead of flattening it to a
    // SeqString makes no sense here (since flattening would be faster and use
    // less memory). Thus, if one of {left} or {right} is a young string, we'll
    // build a SeqString rather than a ConsString, regardless of {length}.
    // TODO(dmercadier, dinfuehr): always build a ConsString here once the
    // generational write-barrier supports background threads.
    if (!LocalHeap::Current() ||
        (!ObjectInYoungGeneration(*left) && !ObjectInYoungGeneration(*right))) {
      return broker()
          ->local_isolate_or_isolate()
          ->factory()
          ->NewConsString(left, right, AllocationType::kOld)
          .ToHandleChecked();
    }
  }

  // If one of the string is not in readonly space, then we need a
  // SharedStringAccessGuardIfNeeded before accessing its content.
  bool require_guard = SharedStringAccessGuardIfNeeded::IsNeeded(
                           *left, broker()->local_isolate_or_isolate()) ||
                       SharedStringAccessGuardIfNeeded::IsNeeded(
                           *right, broker()->local_isolate_or_isolate());
  SharedStringAccessGuardIfNeeded access_guard(
      require_guard ? broker()->local_isolate_or_isolate() : nullptr);

  if (left->IsOneByteRepresentation() && right->IsOneByteRepresentation()) {
    // {left} and {right} are 1-byte ==> the result will be 1-byte.
    // Note that we need a canonical handle, because we insert in
    // {created_strings_} the handle's address, which is kinda meaningless if
    // the handle isn't canonical.
    Handle<SeqOneByteString> flat = broker()->CanonicalPersistentHandle(
        broker()
            ->local_isolate_or_isolate()
            ->factory()
            ->NewRawOneByteString(length, AllocationType::kOld)
            .ToHandleChecked());
    created_strings_.insert(flat);
    DisallowGarbageCollection no_gc;
    String::WriteToFlat(*left, flat->GetChars(no_gc, access_guard), 0,
                        left->length(), access_guard);
    String::WriteToFlat(*right,
                        flat->GetChars(no_gc, access_guard) + left->length(), 0,
                        right->length(), access_guard);
    return flat;
  } else {
    // One (or both) of {left} and {right} is 2-byte ==> the result will be
    // 2-byte.
    Handle<SeqTwoByteString> flat = broker()->CanonicalPersistentHandle(
        broker()
            ->local_isolate_or_isolate()
            ->factory()
            ->NewRawTwoByteString(length, AllocationType::kOld)
            .ToHandleChecked());
    created_strings_.insert(flat);
    DisallowGarbageCollection no_gc;
    String::WriteToFlat(*left, flat->GetChars(no_gc, access_guard), 0,
                        left->length(), access_guard);
    String::WriteToFlat(*right,
                        flat->GetChars(no_gc, access_guard) + left->length(), 0,
                        right->length(), access_guard);
    return flat;
  }
}

bool JSNativeContextSpecialization::StringCanSafelyBeRead(Node* const node,
                                                          Handle<String> str) {
  DCHECK(node->opcode() == IrOpcode::kHeapConstant ||
         node->opcode() == IrOpcode::kNumberConstant);
  if (broker()->IsMainThread()) {
    // All strings are safe to be read on the main thread.
    return true;
  }
  if (node->opcode() == IrOpcode::kNumberConstant) {
    // If {node} is a number constant, then {str} is the stringification of this
    // number which we must have created ourselves.
    return true;
  }
  return !IsStringWithNonAccessibleContent(broker(), node) ||
         created_strings_.find(str) != created_strings_.end();
}

Reduction JSNativeContextSpecialization::ReduceJSAdd(Node* node) {
  // TODO(turbofan): This has to run together with the inlining and
  // native context specialization to be able to leverage the string
  // constant-folding for optimizing property access, but we should
  // nevertheless find a better home for this at some point.
  DCHECK_EQ(IrOpcode::kJSAdd, node->opcode());

  Node* const lhs = node->InputAt(0);
  Node* const rhs = node->InputAt(1);

  std::optional<size_t> lhs_len = GetMaxStringLength(broker(), lhs);
  std::optional<size_t> rhs_len = GetMaxStringLength(broker(), rhs);
  if (!lhs_len || !rhs_len) return NoChange();

  // Fold if at least one of the parameters is a string constant and the
  // addition won't throw due to too long result.
  if (*lhs_len + *rhs_len <= String::kMaxLength &&
      (IsStringConstant(broker(), lhs) || IsStringConstant(broker(), rhs))) {
    // We need canonical handles for {left} and {right}, in order to be able to
    // search {created_strings_} if needed.
    Handle<String> left =
        broker()->CanonicalPersistentHandle(CreateStringConstant(lhs));
    Handle<String> right =
        broker()->CanonicalPersistentHandle(CreateStringConstant(rhs));

    if (!(StringCanSafelyBeRead(lhs, left) &&
          StringCanSafelyBeRead(rhs, right))) {
      // One of {lhs} or {rhs} is not safe to be read in the background.

      if (left->length() + right->length() > ConsString::kMinLength &&
          (!LocalHeap::Current() || (!ObjectInYoungGeneration(*left) &&
                                     !ObjectInYoungGeneration(*right)))) {
        // We can create a ConsString with {left} and {right}, without needing
        // to read their content (and this ConsString will not introduce
        // old-to-new pointers from the background).
        Handle<String> concatenated =
            broker()
                ->local_isolate_or_isolate()
                ->factory()
                ->NewConsString(left, right, AllocationType::kOld)
                .ToHandleChecked();
        Node* reduced = graph()->NewNode(common()->HeapConstant(
            broker()->CanonicalPersistentHandle(concatenated)));
        ReplaceWithValue(node, reduced);
        return Replace(reduced);
      } else {
        // Concatenating those strings would not produce a ConsString but rather
        // a flat string (because the result is small). And, since the strings
        // are not safe to be read in the background, this wouldn't be safe.
        // Or, one of the string is in the young generation, and since the
        // generational barrier doesn't support background threads, we cannot
        // create the ConsString.
        return NoChange();
      }
    }

    Handle<String> concatenated = Concatenate(left, right);
    Node* reduced = graph()->NewNode(common()->HeapConstant(
        broker()->CanonicalPersistentHandle(concatenated)));

    ReplaceWithValue(node, reduced);
    return Replace(reduced);
  }

  return NoChange();
}

Reduction JSNativeContextSpecialization::ReduceJSGetSuperConstructor(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSGetSuperConstructor, node->opcode());
  Node* constructor = NodeProperties::GetValueInput(node, 0);

  // Check if the input is a known JSFunction.
  HeapObjectMatcher m(constructor);
  if (!m.HasResolvedValue() || !m.Ref(broker()).IsJSFunction()) {
    return NoChange();
  }
  JSFunctionRef function = m.Ref(broker()).AsJSFunction();
  MapRef function_map = function.map(broker());
  HeapObjectRef function_prototype = function_map.prototype(broker());

  // We can constant-fold the super constructor access if the
  // {function}s map is stable, i.e. we can use a code dependency
  // to guard against [[Prototype]] changes of {function}.
  if (function_map.is_stable()) {
    dependencies()->DependOnStableMap(function_map);
    Node* value = jsgraph()->ConstantNoHole(function_prototype, broker());
    ReplaceWithValue(node, value);
    return Replace(value);
  }

  return NoChange();
}

Reduction
JSNativeContextSpecialization::ReduceJSFindNonDefaultConstructorOrConstruct(
    Node* node) {
  JSFindNonDefaultConstructorOrConstructNode n(node);
  Node* this_function = n.this_function();
  Node* new_target = n.new_target();
  Node* effect = n.effect();
  Control control = n.control();

  // If the JSFindNonDefaultConstructorOrConstruct operation is inside a try
  // catch, wiring up the graph is complex (reason: if
  // JSFindNonDefaultConstructorOrConstruct reduces to a constant which is
  // something else than a default base ctor, it cannot throw an exception, and
  // the try-catch structure has to be rewired). As this use case is rare, give
  // up optimizing it here.
  if (NodeProperties::IsExceptionalCall(node)) {
    return NoChange();
  }

  // TODO(v8:13091): Don't produce incomplete stack traces when debug is active.
  // We already deopt when a breakpoint is set. But it would be even nicer to
  // avoid producting incomplete stack traces when when debug is active, even if
  // there are no breakpoints - then a user inspecting stack traces via Dev
  // Tools would always see the full stack trace.

  // Check if the input is a known JSFunction.
  HeapObjectMatcher m(this_function);
  if (!m.HasResolvedValue() || !m.Ref(broker()).IsJSFunction()) {
    return NoChange();
  }

  JSFunctionRef this_function_ref = m.Ref(broker()).AsJSFunction();
  MapRef function_map = this_function_ref.map(broker());
  HeapObjectRef current = function_map.prototype(broker());
  // The uppermost JSFunction on the class hierarchy (above it, there can be
  // other JSObjects, e.g., Proxies).
  OptionalJSObjectRef last_function;

  Node* return_value;
  Node* ctor_or_instance;

  // Walk the class inheritance tree until we find a ctor which is not a default
  // derived ctor.
  while (true) {
    if (!current.IsJSFunction()) {
      return NoChange();
    }
    JSFunctionRef current_function = current.AsJSFunction();

    // If there are class fields, bail out. TODO(v8:13091): Handle them here.
    if (current_function.shared(broker())
            .requires_instance_members_initializer()) {
      return NoChange();
    }

    // If there are private methods, bail out. TODO(v8:13091): Handle them here.
    if (current_function.context(broker())
            .scope_info(broker())
            .ClassScopeHasPrivateBrand()) {
      return NoChange();
    }

    FunctionKind kind = current_function.shared(broker()).kind();

    if (kind != FunctionKind::kDefaultDerivedConstructor) {
      // The hierarchy walk will end here; this is the last change to bail out
      // before creating new nodes.
      if (!dependencies()->DependOnArrayIteratorProtector()) {
        return NoChange();
      }
      last_function = current_function;

      if (kind == FunctionKind::kDefaultBaseConstructor) {
        return_value = jsgraph()->BooleanConstant(true);

        // Generate a builtin call for creating the instance.
        Node* constructor =
            jsgraph()->ConstantNoHole(current_function, broker());

        // In the current FrameState setup, the two outputs of this bytecode are
        // poked at indices slot(index(reg_2)) (boolean_output) and
        // slot(index(reg_2) + 1) (object_output). Now we're reducing this
        // bytecode to a builtin call which only has one output (object_output).
        // Change where in the FrameState the output is poked at.

        // The current poke location points to the location for boolean_ouput.
        // We move the poke location by -1, since the poke location decreases
        // when the register index increases (see
        // BytecodeGraphBuilder::Environment::BindRegistersToProjections).

        // The location for boolean_output is already hard-wired to true (which
        // is the correct value here) in
        // BytecodeGraphBuilder::VisitFindNonDefaultConstructorOrConstruct.

        FrameState old_frame_state = n.frame_state();
        auto old_poke_offset = old_frame_state.frame_state_info()
                                   .state_combine()
                                   .GetOffsetToPokeAt();
        FrameState new_frame_state = CloneFrameState(
            jsgraph(), old_frame_state,
            OutputFrameStateCombine::PokeAt(old_poke_offset - 1));

        effect = ctor_or_instance = graph()->NewNode(
            jsgraph()->javascript()->Create(), constructor, new_target,
            n.context(), new_frame_state, effect, control);
      } else {
        return_value = jsgraph()->BooleanConstant(false);
        ctor_or_instance =
            jsgraph()->ConstantNoHole(current_function, broker());
      }
      break;
    }

    // Keep walking up the class tree.
    current = current_function.map(broker()).prototype(broker());
  }

  dependencies()->DependOnStablePrototypeChain(
      function_map, WhereToStart::kStartAtReceiver, last_function);

  // Update the uses of {node}.
  for (Edge edge : node->use_edges()) {
    Node* const user = edge.from();
    if (NodeProperties::IsEffectEdge(edge)) {
      edge.UpdateTo(effect);
    } else if (NodeProperties::IsControlEdge(edge)) {
      edge.UpdateTo(control);
    } else {
      DCHECK(NodeProperties::IsValueEdge(edge));
      switch (ProjectionIndexOf(user->op())) {
        case 0:
          Replace(user, return_value);
          break;
        case 1:
          Replace(user, ctor_or_instance);
          break;
        default:
          UNREACHABLE();
      }
    }
  }
  node->Kill();
  return Replace(return_value);
}

Reduction JSNativeContextSpecialization::ReduceJSInstanceOf(Node* node) {
  JSInstanceOfNode n(node);
  FeedbackParameter const& p = n.Parameters();
  Node* object = n.left();
  Node* constructor = n.right();
  TNode<Object> context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  // Check if the right hand side is a known {receiver}, or
  // we have feedback from the InstanceOfIC.
  OptionalJSObjectRef receiver;
  HeapObjectMatcher m(constructor);
  if (m.HasResolvedValue() && m.Ref(broker()).IsJSObject()) {
    receiver = m.Ref(broker()).AsJSObject();
  } else if (p.feedback().IsValid()) {
    ProcessedFeedback const& feedback =
        broker()->GetFeedbackForInstanceOf(FeedbackSource(p.feedback()));
    if (feedback.IsInsufficient()) return NoChange();
    receiver = feedback.AsInstanceOf().value();
  } else {
    return NoChange();
  }

  if (!receiver.has_value()) return NoChange();

  MapRef receiver_map = receiver->map(broker());
  NameRef name = broker()->has_instance_symbol();
  PropertyAccessInfo access_info =
      broker()->GetPropertyAccessInfo(receiver_map, name, AccessMode::kLoad);

  // TODO(v8:11457) Support dictionary mode holders here.
  if (access_info.IsInvalid() || access_info.HasDictionaryHolder()) {
    return NoChange();
  }
  access_info.RecordDependencies(dependencies());

  PropertyAccessBuilder access_builder(jsgraph(), broker());

  if (access_info.IsNotFound()) {
    // If there's no @@hasInstance handler, the OrdinaryHasInstance operation
    // takes over, but that requires the constructor to be callable.
    if (!receiver_map.is_callable()) return NoChange();

    dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype);

    // Monomorphic property access.
    access_builder.BuildCheckMaps(constructor, &effect, control,
                                  access_info.lookup_start_object_maps());

    // Lower to OrdinaryHasInstance(C, O).
    NodeProperties::ReplaceValueInput(node, constructor, 0);
    NodeProperties::ReplaceValueInput(node, object, 1);
    NodeProperties::ReplaceEffectInput(node, effect);
    static_assert(n.FeedbackVectorIndex() == 2);
    node->RemoveInput(n.FeedbackVectorIndex());
    NodeProperties::ChangeOp(node, javascript()->OrdinaryHasInstance());
    return Changed(node).FollowedBy(ReduceJSOrdinaryHasInstance(node));
  }

  if (access_info.IsFastDataConstant()) {
    OptionalJSObjectRef holder = access_info.holder();
    bool found_on_proto = holder.has_value();
    JSObjectRef holder_ref = found_on_proto ? holder.value() : receiver.value();
    if (access_info.field_representation().IsDouble()) return NoChange();
    OptionalObjectRef constant = holder_ref.GetOwnFastConstantDataProperty(
        broker(), access_info.field_representation(), access_info.field_index(),
        dependencies());
    if (!constant.has_value() || !constant->IsHeapObject() ||
        !constant->AsHeapObject().map(broker()).is_callable()) {
      return NoChange();
    }

    if (found_on_proto) {
      dependencies()->DependOnStablePrototypeChains(
          access_info.lookup_start_object_maps(), kStartAtPrototype,
          holder.value());
    }

    // Check that {constructor} is actually {receiver}.
    constructor = access_builder.BuildCheckValue(constructor, &effect, control,
                                                 receiver->object());

    // Monomorphic property access.
    access_builder.BuildCheckMaps(constructor, &effect, control,
                                  access_info.lookup_start_object_maps());

    // Create a nested frame state inside the current method's most-recent frame
    // state that will ensure that deopts that happen after this point will not
    // fallback to the last Checkpoint--which would completely re-execute the
    // instanceof logic--but rather create an activation of a version of the
    // ToBoolean stub that finishes the remaining work of instanceof and returns
    // to the caller without duplicating side-effects upon a lazy deopt.
    Node* continuation_frame_state = CreateStubBuiltinContinuationFrameState(
        jsgraph(), Builtin::kToBooleanLazyDeoptContinuation, context, nullptr,
        0, frame_state, ContinuationFrameStateMode::LAZY);

    // Call the @@hasInstance handler.
    Node* target = jsgraph()->ConstantNoHole(*constant, broker());
    Node* feedback = jsgraph()->UndefinedConstant();
    // Value inputs plus context, frame state, effect, control.
    static_assert(JSCallNode::ArityForArgc(1) + 4 == 8);
    node->EnsureInputCount(graph()->zone(), 8);
    node->ReplaceInput(JSCallNode::TargetIndex(), target);
    node->ReplaceInput(JSCallNode::ReceiverIndex(), constructor);
    node->ReplaceInput(JSCallNode::ArgumentIndex(0), object);
    node->ReplaceInput(3, feedback);
    node->ReplaceInput(4, context);
    node->ReplaceInput(5, continuation_frame_state);
    node->ReplaceInput(6, effect);
    node->ReplaceInput(7, control);
    NodeProperties::ChangeOp(
        node, javascript()->Call(JSCallNode::ArityForArgc(1), CallFrequency(),
                                 FeedbackSource(),
                                 ConvertReceiverMode::kNotNullOrUndefined));

    // Rewire the value uses of {node} to ToBoolean conversion of the result.
    Node* value = graph()->NewNode(simplified()->ToBoolean(), node);
    for (Edge edge : node->use_edges()) {
      if (NodeProperties::IsValueEdge(edge) && edge.from() != value) {
        edge.UpdateTo(value);
        Revisit(edge.from());
      }
    }
    return Changed(node);
  }

  return NoChange();
}

JSNativeContextSpecialization::InferHasInPrototypeChainResult
JSNativeContextSpecialization::InferHasInPrototypeChain(
    Node* receiver, Effect effect, HeapObjectRef prototype) {
  ZoneRefSet<Map> receiver_maps;
  NodeProperties::InferMapsResult result = NodeProperties::InferMapsUnsafe(
      broker(), receiver, effect, &receiver_maps);
  if (result == NodeProperties::kNoMaps) return kMayBeInPrototypeChain;

  ZoneVector<MapRef> receiver_map_refs(zone());

  // Try to determine either that all of the {receiver_maps} have the given
  // {prototype} in their chain, or that none do. If we can't tell, return
  // kMayBeInPrototypeChain.
  bool all = true;
  bool none = true;
  for (MapRef map : receiver_maps) {
    receiver_map_refs.push_back(map);
    if (result == NodeProperties::kUnreliableMaps && !map.is_stable()) {
      return kMayBeInPrototypeChain;
    }
    while (true) {
      if (IsSpecialReceiverInstanceType(map.instance_type())) {
        return kMayBeInPrototypeChain;
      }
      if (!map.IsJSObjectMap()) {
        all = false;
        break;
      }
      HeapObjectRef map_prototype = map.prototype(broker());
      if (map_prototype.equals(prototype)) {
        none = false;
        break;
      }
      map = map_prototype.map(broker());
      // TODO(v8:11457) Support dictionary mode protoypes here.
      if (!map.is_stable() || map.is_dictionary_map()) {
        return kMayBeInPrototypeChain;
      }
      if (map.oddball_type(broker()) == OddballType::kNull) {
        all = false;
        break;
      }
    }
  }
  DCHECK_IMPLIES(all, !none);
  if (!all && !none) return kMayBeInPrototypeChain;

  {
    OptionalJSObjectRef last_prototype;
    if (all) {
      // We don't need to protect the full chain if we found the prototype, we
      // can stop at {prototype}.  In fact we could stop at the one before
      // {prototype} but since we're dealing with multiple receiver maps this
      // might be a different object each time, so it's much simpler to include
      // {prototype}. That does, however, mean that we must check {prototype}'s
      // map stability.
      if (!prototype.IsJSObject() || !prototype.map(broker()).is_stable()) {
        return kMayBeInPrototypeChain;
      }
      last_prototype = prototype.AsJSObject();
    }
    WhereToStart start = result == NodeProperties::kUnreliableMaps
                             ? kStartAtReceiver
                             : kStartAtPrototype;
    dependencies()->DependOnStablePrototypeChains(receiver_map_refs, start,
                                                  last_prototype);
  }

  DCHECK_EQ(all, !none);
  return all ? kIsInPrototypeChain : kIsNotInPrototypeChain;
}

Reduction JSNativeContextSpecialization::ReduceJSHasInPrototypeChain(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSHasInPrototypeChain, node->opcode());
  Node* value = NodeProperties::GetValueInput(node, 0);
  Node* prototype = NodeProperties::GetValueInput(node, 1);
  Effect effect{NodeProperties::GetEffectInput(node)};

  // Check if we can constant-fold the prototype chain walk
  // for the given {value} and the {prototype}.
  HeapObjectMatcher m(prototype);
  if (m.HasResolvedValue()) {
    InferHasInPrototypeChainResult result =
        InferHasInPrototypeChain(value, effect, m.Ref(broker()));
    if (result != kMayBeInPrototypeChain) {
      Node* result_in_chain =
          jsgraph()->BooleanConstant(result == kIsInPrototypeChain);
      ReplaceWithValue(node, result_in_chain);
      return Replace(result_in_chain);
    }
  }

  return NoChange();
}

Reduction JSNativeContextSpecialization::ReduceJSOrdinaryHasInstance(
    Node* node) {
  DCHECK_EQ(IrOpcode::kJSOrdinaryHasInstance, node->opcode());
  Node* constructor = NodeProperties::GetValueInput(node, 0);
  Node* object = NodeProperties::GetValueInput(node, 1);

  // Check if the {constructor} is known at compile time.
  HeapObjectMatcher m(constructor);
  if (!m.HasResolvedValue()) return NoChange();

  if (m.Ref(broker()).IsJSBoundFunction()) {
    // OrdinaryHasInstance on bound functions turns into a recursive invocation
    // of the instanceof operator again.
    JSBoundFunctionRef function = m.Ref(broker()).AsJSBoundFunction();
    Node* feedback = jsgraph()->UndefinedConstant();
    NodeProperties::ReplaceValueInput(node, object,
                                      JSInstanceOfNode::LeftIndex());
    NodeProperties::ReplaceValueInput(
        node,
        jsgraph()->ConstantNoHole(function.bound_target_function(broker()),
                                  broker()),
        JSInstanceOfNode::RightIndex());
    node->InsertInput(zone(), JSInstanceOfNode::FeedbackVectorIndex(),
                      feedback);
    NodeProperties::ChangeOp(node, javascript()->InstanceOf(FeedbackSource()));
    return Changed(node).FollowedBy(ReduceJSInstanceOf(node));
  }

  if (m.Ref(broker()).IsJSFunction()) {
    // Optimize if we currently know the "prototype" property.

    JSFunctionRef function = m.Ref(broker()).AsJSFunction();

    // TODO(neis): Remove the has_prototype_slot condition once the broker is
    // always enabled.
    if (!function.map(broker()).has_prototype_slot() ||
        !function.has_instance_prototype(broker()) ||
        function.PrototypeRequiresRuntimeLookup(broker())) {
      return NoChange();
    }

    HeapObjectRef prototype =
        dependencies()->DependOnPrototypeProperty(function);
    Node* prototype_constant = jsgraph()->ConstantNoHole(prototype, broker());

    // Lower the {node} to JSHasInPrototypeChain.
    NodeProperties::ReplaceValueInput(node, object, 0);
    NodeProperties::ReplaceValueInput(node, prototype_constant, 1);
    NodeProperties::ChangeOp(node, javascript()->HasInPrototypeChain());
    return Changed(node).FollowedBy(ReduceJSHasInPrototypeChain(node));
  }

  return NoChange();
}

// ES section #sec-promise-resolve
Reduction JSNativeContextSpecialization::ReduceJSPromiseResolve(Node* node) {
  DCHECK_EQ(IrOpcode::kJSPromiseResolve, node->opcode());
  Node* constructor = NodeProperties::GetValueInput(node, 0);
  Node* value = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  FrameState frame_state{NodeProperties::GetFrameStateInput(node)};
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  // Check if the {constructor} is the %Promise% function.
  HeapObjectMatcher m(constructor);
  if (!m.HasResolvedValue() ||
      !m.Ref(broker()).equals(native_context().promise_function(broker()))) {
    return NoChange();
  }

  // Only optimize if {value} cannot be a JSPromise.
  MapInference inference(broker(), value, effect);
  if (!inference.HaveMaps() ||
      inference.AnyOfInstanceTypesAre(JS_PROMISE_TYPE)) {
    return NoChange();
  }

  if (!dependencies()->DependOnPromiseHookProtector()) return NoChange();

  // Create a %Promise% instance and resolve it with {value}.
  Node* promise = effect =
      graph()->NewNode(javascript()->CreatePromise(), context, effect);

  // Create a nested frame state inside the current method's most-recent
  // {frame_state} that will ensure that lazy deoptimizations at this
  // point will still return the {promise} instead of the result of the
  // ResolvePromise operation (which yields undefined).
  Node* parameters[] = {promise};
  frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kAsyncFunctionLazyDeoptContinuation, context,
      parameters, arraysize(parameters), frame_state,
      ContinuationFrameStateMode::LAZY);

  effect = graph()->NewNode(javascript()->ResolvePromise(), promise, value,
                            context, frame_state, effect, control);
  ReplaceWithValue(node, promise, effect, control);
  return Replace(promise);
}

// ES section #sec-promise-resolve-functions
Reduction JSNativeContextSpecialization::ReduceJSResolvePromise(Node* node) {
  DCHECK_EQ(IrOpcode::kJSResolvePromise, node->opcode());
  Node* promise = NodeProperties::GetValueInput(node, 0);
  Node* resolution = NodeProperties::GetValueInput(node, 1);
  Node* context = NodeProperties::GetContextInput(node);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  // Check if we know something about the {resolution}.
  MapInference inference(broker(), resolution, effect);
  if (!inference.HaveMaps()) return NoChange();
  ZoneRefSet<Map> const& resolution_maps = inference.GetMaps();

  // Compute property access info for "then" on {resolution}.
  ZoneVector<PropertyAccessInfo> access_infos(graph()->zone());
  AccessInfoFactory access_info_factory(broker(), graph()->zone());

  for (MapRef map : resolution_maps) {
    access_infos.push_back(broker()->GetPropertyAccessInfo(
        map, broker()->then_string(), AccessMode::kLoad));
  }
  PropertyAccessInfo access_info =
      access_info_factory.FinalizePropertyAccessInfosAsOne(access_infos,
                                                           AccessMode::kLoad);

  // TODO(v8:11457) Support dictionary mode prototypes here.
  if (access_info.IsInvalid() || access_info.HasDictionaryHolder()) {
    return inference.NoChange();
  }

  // Only optimize when {resolution} definitely doesn't have a "then" property.
  if (!access_info.IsNotFound()) return inference.NoChange();

  if (!inference.RelyOnMapsViaStability(dependencies())) {
    return inference.NoChange();
  }

  dependencies()->DependOnStablePrototypeChains(
      access_info.lookup_start_object_maps(), kStartAtPrototype);

  // Simply fulfill the {promise} with the {resolution}.
  Node* value = effect =
      graph()->NewNode(javascript()->FulfillPromise(), promise, resolution,
                       context, effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

namespace {

FieldAccess ForPropertyCellValue(MachineRepresentation representation,
                                 Type type, OptionalMapRef map, NameRef name) {
  WriteBarrierKind kind = kFullWriteBarrier;
  if (representation == MachineRepresentation::kTaggedSigned) {
    kind = kNoWriteBarrier;
  } else if (representation == MachineRepresentation::kTaggedPointer) {
    kind = kPointerWriteBarrier;
  }
  MachineType r = MachineType::TypeForRepresentation(representation);
  FieldAccess access = {
      kTaggedBase, PropertyCell::kValueOffset, name.object(), map, type, r,
      kind, "PropertyCellValue"};
  return access;
}

}  // namespace

// TODO(neis): Try to merge this with ReduceNamedAccess by introducing a new
// PropertyAccessInfo kind for global accesses and using the existing mechanism
// for building loads/stores.
// Note: The "receiver" parameter is only used for DCHECKS, but that's on
// purpose. This way we can assert the super property access cases won't hit the
// code which hasn't been modified to support super property access.
Reduction JSNativeContextSpecialization::ReduceGlobalAccess(
    Node* node, Node* lookup_start_object, Node* receiver, Node* value,
    NameRef name, AccessMode access_mode, Node* key,
    PropertyCellRef property_cell, Node* effect) {
  if (!property_cell.Cache(broker())) {
    TRACE_BROKER_MISSING(broker(), "usable data for " << property_cell);
    return NoChange();
  }

  ObjectRef property_cell_value = property_cell.value(broker());
  if (property_cell_value.IsPropertyCellHole()) {
    // The property cell is no longer valid.
    return NoChange();
  }

  PropertyDetails property_details = property_cell.property_details();
  PropertyCellType property_cell_type = property_details.cell_type();
  DCHECK_EQ(PropertyKind::kData, property_details.kind());

  Node* control = NodeProperties::GetControlInput(node);
  if (effect == nullptr) {
    effect = NodeProperties::GetEffectInput(node);
  }

  // We have additional constraints for stores.
  if (access_mode == AccessMode::kStore) {
    DCHECK_EQ(receiver, lookup_start_object);
    if (property_details.IsReadOnly()) {
      // Don't even bother trying to lower stores to read-only data properties.
      // TODO(neis): We could generate code that checks if the new value equals
      // the old one and then does nothing or deopts, respectively.
      return NoChange();
    } else if (property_cell_type == PropertyCellType::kUndefined) {
      return NoChange();
    } else if (property_cell_type == PropertyCellType::kConstantType) {
      // We rely on stability further below.
      if (property_cell_value.IsHeapObject() &&
          !property_cell_value.AsHeapObject().map(broker()).is_stable()) {
        return NoChange();
      }
    }
  } else if (access_mode == AccessMode::kHas) {
    DCHECK_EQ(receiver, lookup_start_object);
    // has checks cannot follow the fast-path used by loads when these
    // conditions hold.
    if ((property_details.IsConfigurable() || !property_details.IsReadOnly()) &&
        property_details.cell_type() != PropertyCellType::kConstant &&
        property_details.cell_type() != PropertyCellType::kUndefined)
      return NoChange();
  }

  // Ensure that {key} matches the specified {name} (if {key} is given).
  if (key != nullptr) {
    effect = BuildCheckEqualsName(name, key, effect, control);
  }

  // If we have a {lookup_start_object} to validate, we do so by checking that
  // its map is the (target) global proxy's map. This guarantees that in fact
  // the lookup start object is the global proxy.
  // Note: we rely on the map constant below being the same as what is used in
  // NativeContextRef::GlobalIsDetached().
  if (lookup_start_object != nullptr) {
    effect = graph()->NewNode(
        simplified()->CheckMaps(
            CheckMapsFlag::kNone,
            ZoneRefSet<Map>(
                native_context().global_proxy_object(broker()).map(broker()))),
        lookup_start_object, effect, control);
  }

  if (access_mode == AccessMode::kLoad || access_mode == AccessMode::kHas) {
    // Load from non-configurable, read-only data property on the global
    // object can be constant-folded, even without deoptimization support.
    if (!property_details.IsConfigurable() && property_details.IsReadOnly()) {
      value = access_mode == AccessMode::kHas
                  ? jsgraph()->TrueConstant()
                  : jsgraph()->ConstantNoHole(property_cell_value, broker());
    } else {
      // Record a code dependency on the cell if we can benefit from the
      // additional feedback, or the global property is configurable (i.e.
      // can be deleted or reconfigured to an accessor property).
      if (property_details.cell_type() != PropertyCellType::kMutable ||
          property_details.IsConfigurable()) {
        dependencies()->DependOnGlobalProperty(property_cell);
      }

      // Load from constant/undefined global property can be constant-folded.
      if (property_details.cell_type() == PropertyCellType::kConstant ||
          property_details.cell_type() == PropertyCellType::kUndefined) {
        value = access_mode == AccessMode::kHas
                    ? jsgraph()->TrueConstant()
                    : jsgraph()->ConstantNoHole(property_cell_value, broker());
        DCHECK(!property_cell_value.IsHeapObject() ||
               !property_cell_value.IsPropertyCellHole());
      } else {
        DCHECK_NE(AccessMode::kHas, access_mode);

        // Load from constant type cell can benefit from type feedback.
        OptionalMapRef map;
        Type property_cell_value_type = Type::NonInternal();
        MachineRepresentation representation = MachineRepresentation::kTagged;
        if (property_details.cell_type() == PropertyCellType::kConstantType) {
          // Compute proper type based on the current value in the cell.
          if (property_cell_value.IsSmi()) {
            property_cell_value_type = Type::SignedSmall();
            representation = MachineRepresentation::kTaggedSigned;
          } else if (property_cell_value.IsHeapNumber()) {
            property_cell_value_type = Type::Number();
            representation = MachineRepresentation::kTaggedPointer;
          } else {
            MapRef property_cell_value_map =
                property_cell_value.AsHeapObject().map(broker());
            property_cell_value_type =
                Type::For(property_cell_value_map, broker());
            representation = MachineRepresentation::kTaggedPointer;

            // We can only use the property cell value map for map check
            // elimination if it's stable, i.e. the HeapObject wasn't
            // mutated without the cell state being updated.
            if (property_cell_value_map.is_stable()) {
              dependencies()->DependOnStableMap(property_cell_value_map);
              map = property_cell_value_map;
            }
          }
        }
        value = effect = graph()->NewNode(
            simplified()->LoadField(ForPropertyCellValue(
                representation, property_cell_value_type, map, name)),
            jsgraph()->ConstantNoHole(property_cell, broker()), effect,
            control);
      }
    }
  } else if (access_mode == AccessMode::kStore) {
    DCHECK_EQ(receiver, lookup_start_object);
    DCHECK(!property_details.IsReadOnly());
    switch (property_details.cell_type()) {
      case PropertyCellType::kConstant: {
        // Record a code dependency on the cell, and just deoptimize if the new
        // value doesn't match the previous value stored inside the cell.
        dependencies()->DependOnGlobalProperty(property_cell);
        Node* check = graph()->NewNode(
            simplified()->ReferenceEqual(), value,
            jsgraph()->ConstantNoHole(property_cell_value, broker()));
        effect = graph()->NewNode(
            simplified()->CheckIf(DeoptimizeReason::kValueMismatch), check,
            effect, control);
        break;
      }
      case PropertyCellType::kConstantType: {
        // Record a code dependency on the cell, and just deoptimize if the new
        // value's type doesn't match the type of the previous value in the
        // cell.
        dependencies()->DependOnGlobalProperty(property_cell);
        Type property_cell_value_type;
        MachineRepresentation representation = MachineRepresentation::kTagged;
        if (property_cell_value.IsHeapObject()) {
          MapRef property_cell_value_map =
              property_cell_value.AsHeapObject().map(broker());
          dependencies()->DependOnStableMap(property_cell_value_map);

          // Check that the {value} is a HeapObject.
          value = effect = graph()->NewNode(simplified()->CheckHeapObject(),
                                            value, effect, control);
          // Check {value} map against the {property_cell_value} map.
          effect = graph()->NewNode(
              simplified()->CheckMaps(CheckMapsFlag::kNone,
                                      ZoneRefSet<Map>(property_cell_value_map)),
              value, effect, control);
          property_cell_value_type = Type::OtherInternal();
          representation = MachineRepresentation::kTaggedPointer;
        } else {
          // Check that the {value} is a Smi.
          value = effect = graph()->NewNode(
              simplified()->CheckSmi(FeedbackSource()), value, effect, control);
          property_cell_value_type = Type::SignedSmall();
          representation = MachineRepresentation::kTaggedSigned;
        }
        effect =
            graph()->NewNode(simplified()->StoreField(ForPropertyCellValue(
                                 representation, property_cell_value_type,
                                 OptionalMapRef(), name)),
                             jsgraph()->ConstantNoHole(property_cell, broker()),
                             value, effect, control);
        break;
      }
      case PropertyCellType::kMutable: {
        // Record a code dependency on the cell, and just deoptimize if the
        // property ever becomes read-only.
        dependencies()->DependOnGlobalProperty(property_cell);
        effect =
            graph()->NewNode(simplified()->StoreField(ForPropertyCellValue(
                                 MachineRepresentation::kTagged,
                                 Type::NonInternal(), OptionalMapRef(), name)),
                             jsgraph()->ConstantNoHole(property_cell, broker()),
                             value, effect, control);
        break;
      }
      case PropertyCellType::kUndefined:
      case PropertyCellType::kInTransition:
        UNREACHABLE();
    }
  } else {
    return NoChange();
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceJSLoadGlobal(Node* node) {
  JSLoadGlobalNode n(node);
  LoadGlobalParameters const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();

  ProcessedFeedback const& processed =
      broker()->GetFeedbackForGlobalAccess(FeedbackSource(p.feedback()));
  if (processed.IsInsufficient()) return NoChange();

  GlobalAccessFeedback const& feedback = processed.AsGlobalAccess();
  if (feedback.IsScriptContextSlot()) {
    Effect effect = n.effect();
    Node* script_context =
        jsgraph()->ConstantNoHole(feedback.script_context(), broker());
    Node* value = effect =
        graph()->NewNode(javascript()->LoadContext(0, feedback.slot_index(),
                                                   feedback.immutable()),
                         script_context, effect);
    ReplaceWithValue(node, value, effect);
    return Replace(value);
  } else if (feedback.IsPropertyCell()) {
    return ReduceGlobalAccess(node, nullptr, nullptr, nullptr, p.name(),
                              AccessMode::kLoad, nullptr,
                              feedback.property_cell());
  } else {
    DCHECK(feedback.IsMegamorphic());
    return NoChange();
  }
}

Reduction JSNativeContextSpecialization::ReduceJSStoreGlobal(Node* node) {
  JSStoreGlobalNode n(node);
  StoreGlobalParameters const& p = n.Parameters();
  Node* value = n.value();
  if (!p.feedback().IsValid()) return NoChange();

  ProcessedFeedback const& processed =
      broker()->GetFeedbackForGlobalAccess(FeedbackSource(p.feedback()));
  if (processed.IsInsufficient()) return NoChange();

  GlobalAccessFeedback const& feedback = processed.AsGlobalAccess();
  if (feedback.IsScriptContextSlot()) {
    if (feedback.immutable()) return NoChange();
    Node* effect = n.effect();
    Node* control = n.control();
    Node* script_context =
        jsgraph()->ConstantNoHole(feedback.script_context(), broker());

    // StoreGlobal can store to `let` variables declared by another script.
    // Thus, we must check the const tracking let side data and potentially
    // invalidate the constness.
    if (v8_flags.const_tracking_let) {
      int side_data_index =
          ConstTrackingLetSideDataIndexForAccess(feedback.slot_index());
      GenerateCheckConstTrackingLetSideData(script_context, &effect, &control,
                                            side_data_index, jsgraph_);
      // If we're still here (not deopted) the side data implied that the
      // variable was already not-a-constant, so we can just store into it.
    }
    effect =
        graph()->NewNode(javascript()->StoreContext(0, feedback.slot_index()),
                         value, script_context, effect, control);
    ReplaceWithValue(node, value, effect, control);
    return Replace(value);
  } else if (feedback.IsPropertyCell()) {
    return ReduceGlobalAccess(node, nullptr, nullptr, value, p.name(),
                              AccessMode::kStore, nullptr,
                              feedback.property_cell());
  } else {
    DCHECK(feedback.IsMegamorphic());
    return NoChange();
  }
}

Reduction JSNativeContextSpecialization::ReduceMegaDOMPropertyAccess(
    Node* node, Node* value, MegaDOMPropertyAccessFeedback const& feedback,
    FeedbackSource const& source) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSLoadProperty);
  // TODO(mslekova): Add support and tests for kJSLoadNamedFromSuper.
  static_assert(JSLoadNamedNode::ObjectIndex() == 0 &&
                    JSLoadPropertyNode::ObjectIndex() == 0,
                "Assumptions about ObjectIndex have changed, please update "
                "this function.");

  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* frame_state = NodeProperties::GetFrameStateInput(node);

  Node* lookup_start_object = NodeProperties::GetValueInput(node, 0);

  if (!dependencies()->DependOnMegaDOMProtector()) {
    return NoChange();
  }

  FunctionTemplateInfoRef function_template_info = feedback.info();
  int16_t range_start =
      function_template_info.allowed_receiver_instance_type_range_start();
  int16_t range_end =
      function_template_info.allowed_receiver_instance_type_range_end();
  DCHECK_IMPLIES(range_start == 0, range_end == 0);
  DCHECK_LE(range_start, range_end);

  // TODO(mslekova): This could be a new InstanceTypeCheck operator
  // that gets lowered later on (e.g. during generic lowering).
  Node* receiver_map = effect =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                       lookup_start_object, effect, control);
  Node* receiver_instance_type = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapInstanceType()),
      receiver_map, effect, control);

  if (v8_flags.embedder_instance_types && range_start != 0) {
    // Embedder instance ID is set, doing a simple range check.
    Node* diff_to_start =
        graph()->NewNode(simplified()->NumberSubtract(), receiver_instance_type,
                         jsgraph()->ConstantNoHole(range_start));
    Node* range_length = jsgraph()->ConstantNoHole(range_end - range_start);

    // TODO(mslekova): Once we have the InstanceTypeCheck operator, we could
    // lower it to Uint32LessThan later on to perform what is done in bounds.h.
    Node* check = graph()->NewNode(simplified()->NumberLessThanOrEqual(),
                                   diff_to_start, range_length);
    effect = graph()->NewNode(
        simplified()->CheckIf(DeoptimizeReason::kWrongInstanceType), check,
        effect, control);
  } else if (function_template_info.is_signature_undefined(broker())) {
    // Signature is undefined, enough to check if the receiver is a JSApiObject.
    Node* check =
        graph()->NewNode(simplified()->NumberEqual(), receiver_instance_type,
                         jsgraph()->ConstantNoHole(JS_API_OBJECT_TYPE));
    effect = graph()->NewNode(
        simplified()->CheckIf(DeoptimizeReason::kWrongInstanceType), check,
        effect, control);
  } else {
    // Calling out to builtin to do signature check.
    Callable callable = Builtins::CallableFor(
        isolate(), Builtin::kCallFunctionTemplate_CheckCompatibleReceiver);
    int stack_arg_count = callable.descriptor().GetStackParameterCount() +
                          1 /* implicit receiver */;

    CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(), stack_arg_count,
        CallDescriptor::kNeedsFrameState, Operator::kNoProperties);

    Node* inputs[8] = {
        jsgraph()->HeapConstantNoHole(callable.code()),
        jsgraph()->ConstantNoHole(function_template_info, broker()),
        jsgraph()->Int32Constant(stack_arg_count),
        lookup_start_object,
        jsgraph()->ConstantNoHole(native_context(), broker()),
        frame_state,
        effect,
        control};

    value = effect = control =
        graph()->NewNode(common()->Call(call_descriptor), 8, inputs);
    return Replace(value);
  }

  value = InlineApiCall(lookup_start_object, lookup_start_object, frame_state,
                        nullptr /*value*/, &effect, &control,
                        function_template_info);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceNamedAccess(
    Node* node, Node* value, NamedAccessFeedback const& feedback,
    AccessMode access_mode, Node* key) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSSetNamedProperty ||
         node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSSetKeyedProperty ||
         node->opcode() == IrOpcode::kJSDefineNamedOwnProperty ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnPropertyInLiteral ||
         node->opcode() == IrOpcode::kJSHasProperty ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnProperty);
  static_assert(JSLoadNamedNode::ObjectIndex() == 0 &&
                JSSetNamedPropertyNode::ObjectIndex() == 0 &&
                JSLoadPropertyNode::ObjectIndex() == 0 &&
                JSSetKeyedPropertyNode::ObjectIndex() == 0 &&
                JSDefineNamedOwnPropertyNode::ObjectIndex() == 0 &&
                JSSetNamedPropertyNode::ObjectIndex() == 0 &&
                JSDefineKeyedOwnPropertyInLiteralNode::ObjectIndex() == 0 &&
                JSHasPropertyNode::ObjectIndex() == 0 &&
                JSDefineKeyedOwnPropertyNode::ObjectIndex() == 0);
  static_assert(JSLoadNamedFromSuperNode::ReceiverIndex() == 0);

  Node* context = NodeProperties::GetContextInput(node);
  FrameState frame_state{NodeProperties::GetFrameStateInput(node)};
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};

  // receiver = the object we pass to the accessor (if any) as the "this" value.
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  // lookup_start_object = the object where we start looking for the property.
  Node* lookup_start_object;
  if (node->opcode() == IrOpcode::kJSLoadNamedFromSuper) {
    DCHECK(v8_flags.super_ic);
    JSLoadNamedFromSuperNode n(node);
    // Lookup start object is the __proto__ of the home object.
    lookup_start_object = effect =
        BuildLoadPrototypeFromObject(n.home_object(), effect, control);
  } else {
    lookup_start_object = receiver;
  }

  // Either infer maps from the graph or use the feedback.
  ZoneVector<MapRef> inferred_maps(zone());
  if (!InferMaps(lookup_start_object, effect, &inferred_maps)) {
    for (MapRef map : feedback.maps()) {
      inferred_maps.push_back(map);
    }
  }
  RemoveImpossibleMaps(lookup_start_object, &inferred_maps);

  // Check if we have an access o.x or o.x=v where o is the target native
  // contexts' global proxy, and turn that into a direct access to the
  // corresponding global object instead.
  if (inferred_maps.size() == 1) {
    MapRef lookup_start_object_map = inferred_maps[0];
    if (lookup_start_object_map.equals(
            native_context().global_proxy_object(broker()).map(broker()))) {
      if (!native_context().GlobalIsDetached(broker())) {
        OptionalPropertyCellRef cell =
            native_context().global_object(broker()).GetPropertyCell(
                broker(), feedback.name());
        if (!cell.has_value()) return NoChange();
        // Note: The map check generated by ReduceGlobalAccesses ensures that we
        // will deopt when/if GlobalIsDetached becomes true.
        return ReduceGlobalAccess(node, lookup_start_object, receiver, value,
                                  feedback.name(), access_mode, key, *cell,
                                  effect);
      }
    }
  }

  ZoneVector<PropertyAccessInfo> access_infos(zone());
  {
    ZoneVector<PropertyAccessInfo> access_infos_for_feedback(zone());
    for (MapRef map : inferred_maps) {
      if (map.is_deprecated()) continue;

      // TODO(v8:12547): Support writing to objects in shared space, which need
      // a write barrier that calls Object::Share to ensure the RHS is shared.
      if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(
              map.instance_type()) &&
          access_mode == AccessMode::kStore) {
        return NoChange();
      }

      PropertyAccessInfo access_info =
          broker()->GetPropertyAccessInfo(map, feedback.name(), access_mode);
      access_infos_for_feedback.push_back(access_info);
    }

    AccessInfoFactory access_info_factory(broker(), graph()->zone());
    if (!access_info_factory.FinalizePropertyAccessInfos(
            access_infos_for_feedback, access_mode, &access_infos)) {
      return NoChange();
    }
  }

  // Ensure that {key} matches the specified name (if {key} is given).
  if (key != nullptr) {
    effect = BuildCheckEqualsName(feedback.name(), key, effect, control);
  }

  // Collect call nodes to rewire exception edges.
  ZoneVector<Node*> if_exception_nodes(zone());
  ZoneVector<Node*>* if_exceptions = nullptr;
  Node* if_exception = nullptr;
  if (NodeProperties::IsExceptionalCall(node, &if_exception)) {
    if_exceptions = &if_exception_nodes;
  }

  PropertyAccessBuilder access_builder(jsgraph(), broker());

  // Check for the monomorphic cases.
  if (access_infos.size() == 1) {
    PropertyAccessInfo access_info = access_infos.front();
    if (receiver != lookup_start_object) {
      // Super property access. lookup_start_object is a JSReceiver or
      // null. It can't be a number, a string etc. So trying to build the
      // checks in the "else if" branch doesn't make sense.
      access_builder.BuildCheckMaps(lookup_start_object, &effect, control,
                                    access_info.lookup_start_object_maps());

    } else if (!access_builder.TryBuildStringCheck(
                   broker(), access_info.lookup_start_object_maps(), &receiver,
                   &effect, control) &&
               !access_builder.TryBuildNumberCheck(
                   broker(), access_info.lookup_start_object_maps(), &receiver,
                   &effect, control)) {
      // Try to build string check or number check if possible. Otherwise build
      // a map check.

      // TryBuildStringCheck and TryBuildNumberCheck don't update the receiver
      // if they fail.
      DCHECK_EQ(receiver, lookup_start_object);
      if (HasNumberMaps(broker(), access_info.lookup_start_object_maps())) {
        // We need to also let Smi {receiver}s through in this case, so
        // we construct a diamond, guarded by the Sminess of the {receiver}
        // and if {receiver} is not a Smi just emit a sequence of map checks.
        Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), receiver);
        Node* branch = graph()->NewNode(common()->Branch(), check, control);

        Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
        Node* etrue = effect;

        Control if_false{graph()->NewNode(common()->IfFalse(), branch)};
        Effect efalse = effect;
        access_builder.BuildCheckMaps(receiver, &efalse, if_false,
                                      access_info.lookup_start_object_maps());

        control = graph()->NewNode(common()->Merge(2), if_true, if_false);
        effect =
            graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
      } else {
        access_builder.BuildCheckMaps(receiver, &effect, control,
                                      access_info.lookup_start_object_maps());
      }
    } else {
      // At least one of TryBuildStringCheck & TryBuildNumberCheck succeeded
      // and updated the receiver. Update lookup_start_object to match (they
      // should be the same).
      lookup_start_object = receiver;
    }

    // Generate the actual property access.
    std::optional<ValueEffectControl> continuation = BuildPropertyAccess(
        lookup_start_object, receiver, value, context, frame_state, effect,
        control, feedback.name(), if_exceptions, access_info, access_mode);
    if (!continuation) {
      // At this point we maybe have added nodes into the graph (e.g. via
      // NewNode or BuildCheckMaps) in some cases but we haven't connected them
      // to End since we haven't called ReplaceWithValue. Since they are nodes
      // which are not connected with End, they will be removed by graph
      // trimming.
      return NoChange();
    }
    value = continuation->value();
    effect = continuation->effect();
    control = continuation->control();
  } else {
    // The final states for every polymorphic branch. We join them with
    // Merge+Phi+EffectPhi at the bottom.
    ZoneVector<Node*> values(zone());
    ZoneVector<Node*> effects(zone());
    ZoneVector<Node*> controls(zone());

    Node* receiverissmi_control = nullptr;
    Node* receiverissmi_effect = effect;

    if (receiver == lookup_start_object) {
      // Check if {receiver} may be a number.
      bool receiverissmi_possible = false;
      for (PropertyAccessInfo const& access_info : access_infos) {
        if (HasNumberMaps(broker(), access_info.lookup_start_object_maps())) {
          receiverissmi_possible = true;
          break;
        }
      }

      // Handle the case that {receiver} may be a number.
      if (receiverissmi_possible) {
        Node* check = graph()->NewNode(simplified()->ObjectIsSmi(), receiver);
        Node* branch = graph()->NewNode(common()->Branch(), check, control);
        control = graph()->NewNode(common()->IfFalse(), branch);
        receiverissmi_control = graph()->NewNode(common()->IfTrue(), branch);
        receiverissmi_effect = effect;
      }
    }

    // Generate code for the various different property access patterns.
    Node* fallthrough_control = control;
    for (size_t j = 0; j < access_infos.size(); ++j) {
      PropertyAccessInfo const& access_info = access_infos[j];
      Node* this_value = value;
      Node* this_lookup_start_object = lookup_start_object;
      Node* this_receiver = receiver;
      Effect this_effect = effect;
      Control this_control{fallthrough_control};

      // Perform map check on {lookup_start_object}.
      ZoneVector<MapRef> const& lookup_start_object_maps =
          access_info.lookup_start_object_maps();
      {
        // Whether to insert a dedicated MapGuard node into the
        // effect to be able to learn from the control flow.
        bool insert_map_guard = true;

        // Check maps for the {lookup_start_object}s.
        if (j == access_infos.size() - 1) {
          // Last map check on the fallthrough control path, do a
          // conditional eager deoptimization exit here.
          access_builder.BuildCheckMaps(lookup_start_object, &this_effect,
                                        this_control, lookup_start_object_maps);
          fallthrough_control = nullptr;

          // Don't insert a MapGuard in this case, as the CheckMaps
          // node already gives you all the information you need
          // along the effect chain.
          insert_map_guard = false;
        } else {
          // Explicitly branch on the {lookup_start_object_maps}.
          ZoneRefSet<Map> maps(lookup_start_object_maps.begin(),
                               lookup_start_object_maps.end(), graph()->zone());
          Node* check = this_effect =
              graph()->NewNode(simplified()->CompareMaps(maps),
                               lookup_start_object, this_effect, this_control);
          Node* branch =
              graph()->NewNode(common()->Branch(), check, this_control);
          fallthrough_control = graph()->NewNode(common()->IfFalse(), branch);
          this_control = graph()->NewNode(common()->IfTrue(), branch);
        }

        // The Number case requires special treatment to also deal with Smis.
        if (HasNumberMaps(broker(), lookup_start_object_maps)) {
          // Join this check with the "receiver is smi" check above.
          DCHECK_EQ(receiver, lookup_start_object);
          DCHECK_NOT_NULL(receiverissmi_effect);
          DCHECK_NOT_NULL(receiverissmi_control);
          this_control = graph()->NewNode(common()->Merge(2), this_control,
                                          receiverissmi_control);
          this_effect = graph()->NewNode(common()->EffectPhi(2), this_effect,
                                         receiverissmi_effect, this_control);
          receiverissmi_effect = receiverissmi_control = nullptr;

          // The {lookup_start_object} can also be a Smi in this case, so
          // a MapGuard doesn't make sense for this at all.
          insert_map_guard = false;
        }

        // Introduce a MapGuard to learn from this on the effect chain.
        if (insert_map_guard) {
          ZoneRefSet<Map> maps(lookup_start_object_maps.begin(),
                               lookup_start_object_maps.end(), graph()->zone());
          this_effect =
              graph()->NewNode(simplified()->MapGuard(maps),
                               lookup_start_object, this_effect, this_control);
        }

        // If all {lookup_start_object_maps} are Strings we also need to rename
        // the {lookup_start_object} here to make sure that TurboFan knows that
        // along this path the {this_lookup_start_object} is a String. This is
        // because we want strict checking of types, for example for
        // StringLength operators.
        if (HasOnlyStringMaps(broker(), lookup_start_object_maps)) {
          DCHECK_EQ(receiver, lookup_start_object);
          this_lookup_start_object = this_receiver = this_effect =
              graph()->NewNode(common()->TypeGuard(Type::String()),
                               lookup_start_object, this_effect, this_control);
        }
      }

      // Generate the actual property access.
      std::optional<ValueEffectControl> continuation = BuildPropertyAccess(
          this_lookup_start_object, this_receiver, this_value, context,
          frame_state, this_effect, this_control, feedback.name(),
          if_exceptions, access_info, access_mode);
      if (!continuation) {
        // At this point we maybe have added nodes into the graph (e.g. via
        // NewNode or BuildCheckMaps) in some cases but we haven't connected
        // them to End since we haven't called ReplaceWithValue. Since they are
        // nodes which are not connected with End, they will be removed by graph
        // trimming.
        return NoChange();
      }

      values.push_back(continuation->value());
      effects.push_back(continuation->effect());
      controls.push_back(continuation->control());
    }

    DCHECK_NULL(fallthrough_control);

    // Generate the final merge point for all (polymorphic) branches.
    int const control_count = static_cast<int>(controls.size());
    if (control_count == 0) {
      value = effect = control = jsgraph()->Dead();
    } else if (control_count == 1) {
      value = values.front();
      effect = effects.front();
      control = controls.front();
    } else {
      control = graph()->NewNode(common()->Merge(control_count), control_count,
                                 &controls.front());
      values.push_back(control);
      value = graph()->NewNode(
          common()->Phi(MachineRepresentation::kTagged, control_count),
          control_count + 1, &values.front());
      effects.push_back(control);
      effect = graph()->NewNode(common()->EffectPhi(control_count),
                                control_count + 1, &effects.front());
    }
  }

  // Properly rewire IfException edges if {node} is inside a try-block.
  if (!if_exception_nodes.empty()) {
    DCHECK_NOT_NULL(if_exception);
    DCHECK_EQ(if_exceptions, &if_exception_nodes);
    int const if_exception_count = static_cast<int>(if_exceptions->size());
    Node* merge = graph()->NewNode(common()->Merge(if_exception_count),
                                   if_exception_count, &if_exceptions->front());
    if_exceptions->push_back(merge);
    Node* ephi =
        graph()->NewNode(common()->EffectPhi(if_exception_count),
                         if_exception_count + 1, &if_exceptions->front());
    Node* phi = graph()->NewNode(
        common()->Phi(MachineRepresentation::kTagged, if_exception_count),
        if_exception_count + 1, &if_exceptions->front());
    ReplaceWithValue(if_exception, phi, ephi, merge);
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceJSLoadNamed(Node* node) {
  JSLoadNamedNode n(node);
  NamedAccess const& p = n.Parameters();
  Node* const receiver = n.object();
  NameRef name = p.name();

  // Check if we have a constant receiver.
  HeapObjectMatcher m(receiver);
  if (m.HasResolvedValue()) {
    ObjectRef object = m.Ref(broker());
    if (object.IsJSFunction() && name.equals(broker()->prototype_string())) {
      // Optimize "prototype" property of functions.
      JSFunctionRef function = object.AsJSFunction();
      // TODO(neis): Remove the has_prototype_slot condition once the broker is
      // always enabled.
      if (!function.map(broker()).has_prototype_slot() ||
          !function.has_instance_prototype(broker()) ||
          function.PrototypeRequiresRuntimeLookup(broker())) {
        return NoChange();
      }
      HeapObjectRef prototype =
          dependencies()->DependOnPrototypeProperty(function);
      Node* value = jsgraph()->ConstantNoHole(prototype, broker());
      ReplaceWithValue(node, value);
      return Replace(value);
    } else if (object.IsString() && name.equals(broker()->length_string())) {
      // Constant-fold "length" property on constant strings.
      Node* value = jsgraph()->ConstantNoHole(object.AsString().length());
      ReplaceWithValue(node, value);
      return Replace(value);
    }
  }

  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, nullptr, name, jsgraph()->Dead(),
                              FeedbackSource(p.feedback()), AccessMode::kLoad);
}

Reduction JSNativeContextSpecialization::ReduceJSLoadNamedFromSuper(
    Node* node) {
  JSLoadNamedFromSuperNode n(node);
  NamedAccess const& p = n.Parameters();
  NameRef name = p.name();

  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, nullptr, name, jsgraph()->Dead(),
                              FeedbackSource(p.feedback()), AccessMode::kLoad);
}

Reduction JSNativeContextSpecialization::ReduceJSGetIterator(Node* node) {
  JSGetIteratorNode n(node);
  GetIteratorParameters const& p = n.Parameters();

  TNode<Object> receiver = n.receiver();
  TNode<Object> context = n.context();
  FrameState frame_state = n.frame_state();
  Effect effect = n.effect();
  Control control = n.control();

  Node* iterator_exception_node = nullptr;
  Node* if_exception_merge = nullptr;
  Node* if_exception_effect_phi = nullptr;
  Node* if_exception_phi = nullptr;
  bool has_exception_node =
      NodeProperties::IsExceptionalCall(node, &iterator_exception_node);
  int exception_node_index = 0;
  if (has_exception_node) {
    DCHECK_NOT_NULL(iterator_exception_node);
    // If there exists an IfException node for the iterator node, we need
    // to merge all the desugared nodes exception. The iterator node will be
    // desugared to LoadNamed, Call, CallRuntime, we can pre-allocate the
    // nodes with 4 inputs here and we use dead_node as a placeholder for the
    // input, which will be replaced.
    // We use dead_node as a placeholder for the original exception node before
    // it's uses are rewired.

    Node* dead_node = jsgraph()->Dead();
    if_exception_merge =
        graph()->NewNode(common()->Merge(5), dead_node, dead_node, dead_node,
                         dead_node, dead_node);
    if_exception_effect_phi =
        graph()->NewNode(common()->EffectPhi(5), dead_node, dead_node,
                         dead_node, dead_node, dead_node, if_exception_merge);
    if_exception_phi = graph()->NewNode(
        common()->Phi(MachineRepresentation::kTagged, 5), dead_node, dead_node,
        dead_node, dead_node, dead_node, if_exception_merge);
    // Rewire the original exception node uses.
    ReplaceWithValue(iterator_exception_node, if_exception_phi,
                     if_exception_effect_phi, if_exception_merge);
    if_exception_merge->ReplaceInput(exception_node_index,
                                     iterator_exception_node);
    if_exception_effect_phi->ReplaceInput(exception_node_index,
                                          iterator_exception_node);
    if_exception_phi->ReplaceInput(exception_node_index,
                                   iterator_exception_node);
    exception_node_index++;
  }

  // Load iterator property operator
  NameRef iterator_symbol = broker()->iterator_symbol();
  const Operator* load_op =
      javascript()->LoadNamed(iterator_symbol, p.loadFeedback());

  // Lazy deopt of the load iterator property
  // TODO(v8:10047): Use TaggedIndexConstant here once deoptimizer supports it.
  Node* call_slot = jsgraph()->SmiConstant(p.callFeedback().slot.ToInt());
  Node* call_feedback = jsgraph()->HeapConstantNoHole(p.callFeedback().vector);
  Node* lazy_deopt_parameters[] = {receiver, call_slot, call_feedback};
  Node* lazy_deopt_frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kGetIteratorWithFeedbackLazyDeoptContinuation,
      context, lazy_deopt_parameters, arraysize(lazy_deopt_parameters),
      frame_state, ContinuationFrameStateMode::LAZY);
  Node* load_property =
      graph()->NewNode(load_op, receiver, n.feedback_vector(), context,
                       lazy_deopt_frame_state, effect, control);
  effect = load_property;
  control = load_property;

  // Merge the exception path for LoadNamed.
  if (has_exception_node) {
    Node* if_exception =
        graph()->NewNode(common()->IfException(), effect, control);
    if_exception_merge->ReplaceInput(exception_node_index, if_exception);
    if_exception_phi->ReplaceInput(exception_node_index, if_exception);
    if_exception_effect_phi->ReplaceInput(exception_node_index, if_exception);
    exception_node_index++;
    control = graph()->NewNode(common()->IfSuccess(), control);
  }

  Node* check = graph()->NewNode(simplified()->ReferenceEqual(), load_property,
                                 jsgraph()->UndefinedConstant());
  Node* branch =
      graph()->NewNode(common()->Branch(BranchHint::kFalse), check, control);

  {
    Node* if_not_iterator = graph()->NewNode(common()->IfTrue(), branch);
    Node* effect_not_iterator = effect;
    Node* control_not_iterator = if_not_iterator;
    Node* call_runtime = effect_not_iterator = control_not_iterator =
        graph()->NewNode(
            javascript()->CallRuntime(Runtime::kThrowIteratorError, 1),
            receiver, context, frame_state, effect_not_iterator,
            control_not_iterator);
    // Merge the exception path for CallRuntime.
    if (has_exception_node) {
      Node* if_exception = graph()->NewNode(
          common()->IfException(), effect_not_iterator, control_not_iterator);
      if_exception_merge->ReplaceInput(exception_node_index, if_exception);
      if_exception_phi->ReplaceInput(exception_node_index, if_exception);
      if_exception_effect_phi->ReplaceInput(exception_node_index, if_exception);
      exception_node_index++;
      control_not_iterator =
          graph()->NewNode(common()->IfSuccess(), control_not_iterator);
    }
    Node* throw_node =
        graph()->NewNode(common()->Throw(), call_runtime, control_not_iterator);
    MergeControlToEnd(graph(), common(), throw_node);
  }

  control = graph()->NewNode(common()->IfFalse(), branch);

  // Eager deopt of call iterator property
  Node* parameters[] = {receiver, load_property, call_slot, call_feedback};
  Node* eager_deopt_frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kCallIteratorWithFeedback, context, parameters,
      arraysize(parameters), frame_state, ContinuationFrameStateMode::EAGER);
  Node* deopt_checkpoint = graph()->NewNode(
      common()->Checkpoint(), eager_deopt_frame_state, effect, control);
  effect = deopt_checkpoint;

  // Call iterator property operator
  ProcessedFeedback const& feedback =
      broker()->GetFeedbackForCall(p.callFeedback());
  SpeculationMode mode = feedback.IsInsufficient()
                             ? SpeculationMode::kDisallowSpeculation
                             : feedback.AsCall().speculation_mode();
  const Operator* call_op = javascript()->Call(
      JSCallNode::ArityForArgc(0), CallFrequency(), p.callFeedback(),
      ConvertReceiverMode::kNotNullOrUndefined, mode,
      CallFeedbackRelation::kTarget);
  // Lazy deopt to check the call result is JSReceiver.
  Node* call_lazy_deopt_frame_state = CreateStubBuiltinContinuationFrameState(
      jsgraph(), Builtin::kCallIteratorWithFeedbackLazyDeoptContinuation,
      context, nullptr, 0, frame_state, ContinuationFrameStateMode::LAZY);
  Node* call_property = effect = control =
      graph()->NewNode(call_op, load_property, receiver, n.feedback_vector(),
                       context, call_lazy_deopt_frame_state, effect, control);

  // Merge the exception path for Call.
  if (has_exception_node) {
    Node* if_exception =
        graph()->NewNode(common()->IfException(), effect, control);
    if_exception_merge->ReplaceInput(exception_node_index, if_exception);
    if_exception_phi->ReplaceInput(exception_node_index, if_exception);
    if_exception_effect_phi->ReplaceInput(exception_node_index, if_exception);
    exception_node_index++;
    control = graph()->NewNode(common()->IfSuccess(), control);
  }

  // If the result is not JSReceiver, throw invalid iterator exception.
  Node* is_receiver =
      graph()->NewNode(simplified()->ObjectIsReceiver(), call_property);
  Node* branch_node = graph()->NewNode(common()->Branch(BranchHint::kTrue),
                                       is_receiver, control);
  {
    Node* if_not_receiver = graph()->NewNode(common()->IfFalse(), branch_node);
    Node* effect_not_receiver = effect;
    Node* control_not_receiver = if_not_receiver;
    Node* call_runtime = effect_not_receiver = control_not_receiver =
        graph()->NewNode(
            javascript()->CallRuntime(Runtime::kThrowSymbolIteratorInvalid, 0),
            context, frame_state, effect_not_receiver, control_not_receiver);
    // Merge the exception path for CallRuntime.
    if (has_exception_node) {
      Node* if_exception = graph()->NewNode(
          common()->IfException(), effect_not_receiver, control_not_receiver);
      if_exception_merge->ReplaceInput(exception_node_index, if_exception);
      if_exception_phi->ReplaceInput(exception_node_index, if_exception);
      if_exception_effect_phi->ReplaceInput(exception_node_index, if_exception);
      exception_node_index++;
      control_not_receiver =
          graph()->NewNode(common()->IfSuccess(), control_not_receiver);
    }
    Node* throw_node =
        graph()->NewNode(common()->Throw(), call_runtime, control_not_receiver);
    MergeControlToEnd(graph(), common(), throw_node);
  }
  Node* if_receiver = graph()->NewNode(common()->IfTrue(), branch_node);
  ReplaceWithValue(node, call_property, effect, if_receiver);

  if (has_exception_node) {
    DCHECK_EQ(exception_node_index, if_exception_merge->InputCount());
    DCHECK_EQ(exception_node_index, if_exception_effect_phi->InputCount() - 1);
    DCHECK_EQ(exception_node_index, if_exception_phi->InputCount() - 1);
#ifdef DEBUG
    for (Node* input : if_exception_merge->inputs()) {
      DCHECK(!input->IsDead());
    }
    for (Node* input : if_exception_effect_phi->inputs()) {
      DCHECK(!input->IsDead());
    }
    for (Node* input : if_exception_phi->inputs()) {
      DCHECK(!input->IsDead());
    }
#endif
  }
  return Replace(if_receiver);
}

Reduction JSNativeContextSpecialization::ReduceJSSetNamedProperty(Node* node) {
  JSSetNamedPropertyNode n(node);
  NamedAccess const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, nullptr, p.name(), n.value(),
                              FeedbackSource(p.feedback()), AccessMode::kStore);
}

Reduction JSNativeContextSpecialization::ReduceJSDefineNamedOwnProperty(
    Node* node) {
  JSDefineNamedOwnPropertyNode n(node);
  DefineNamedOwnPropertyParameters const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, nullptr, p.name(), n.value(),
                              FeedbackSource(p.feedback()),
                              AccessMode::kStoreInLiteral);
}

Reduction JSNativeContextSpecialization::ReduceElementAccessOnString(
    Node* node, Node* index, Node* value, KeyedAccessMode const& keyed_mode) {
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  // Strings are immutable in JavaScript.
  if (keyed_mode.access_mode() == AccessMode::kStore) return NoChange();

  // `in` cannot be used on strings.
  if (keyed_mode.access_mode() == AccessMode::kHas) return NoChange();

  // Ensure that the {receiver} is actually a String.
  receiver = effect = graph()->NewNode(
      simplified()->CheckString(FeedbackSource()), receiver, effect, control);

  // Determine the {receiver} length.
  Node* length = graph()->NewNode(simplified()->StringLength(), receiver);

  // Load the single character string from {receiver} or yield undefined
  // if the {index} is out of bounds (depending on the {load_mode}).
  value = BuildIndexedStringLoad(receiver, index, length, &effect, &control,
                                 keyed_mode.load_mode());

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

namespace {

OptionalJSTypedArrayRef GetTypedArrayConstant(JSHeapBroker* broker,
                                              Node* receiver) {
  HeapObjectMatcher m(receiver);
  if (!m.HasResolvedValue()) return std::nullopt;
  ObjectRef object = m.Ref(broker);
  if (!object.IsJSTypedArray()) return std::nullopt;
  JSTypedArrayRef typed_array = object.AsJSTypedArray();
  if (typed_array.is_on_heap()) return std::nullopt;
  return typed_array;
}

}  // namespace

void JSNativeContextSpecialization::RemoveImpossibleMaps(
    Node* object, ZoneVector<MapRef>* maps) const {
  OptionalMapRef root_map = InferRootMap(object);
  if (root_map.has_value() && !root_map->is_abandoned_prototype_map()) {
    maps->erase(
        std::remove_if(maps->begin(), maps->end(),
                       [root_map, this](MapRef map) {
                         return map.is_abandoned_prototype_map() ||
                                !map.FindRootMap(broker()).equals(*root_map);
                       }),
        maps->end());
  }
}

// Possibly refine the feedback using inferred map information from the graph.
ElementAccessFeedback const&
JSNativeContextSpecialization::TryRefineElementAccessFeedback(
    ElementAccessFeedback const& feedback, Node* receiver,
    Effect effect) const {
  AccessMode access_mode = feedback.keyed_mode().access_mode();
  bool use_inference =
      access_mode == AccessMode::kLoad || access_mode == AccessMode::kHas;
  if (!use_inference) return feedback;

  ZoneVector<MapRef> inferred_maps(zone());
  if (!InferMaps(receiver, effect, &inferred_maps)) return feedback;

  RemoveImpossibleMaps(receiver, &inferred_maps);
  // TODO(neis): After Refine, the resulting feedback can still contain
  // impossible maps when a target is kept only because more than one of its
  // sources was inferred. Think of a way to completely rule out impossible
  // maps.
  return feedback.Refine(broker(), inferred_maps);
}

Reduction JSNativeContextSpecialization::ReduceElementAccess(
    Node* node, Node* index, Node* value,
    ElementAccessFeedback const& feedback) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSSetKeyedProperty ||
         node->opcode() == IrOpcode::kJSStoreInArrayLiteral ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnPropertyInLiteral ||
         node->opcode() == IrOpcode::kJSHasProperty ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnProperty);
  static_assert(JSLoadPropertyNode::ObjectIndex() == 0 &&
                JSSetKeyedPropertyNode::ObjectIndex() == 0 &&
                JSStoreInArrayLiteralNode::ArrayIndex() == 0 &&
                JSDefineKeyedOwnPropertyInLiteralNode::ObjectIndex() == 0 &&
                JSHasPropertyNode::ObjectIndex() == 0);

  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Effect effect{NodeProperties::GetEffectInput(node)};
  Control control{NodeProperties::GetControlInput(node)};
  Node* context = NodeProperties::GetContextInput(node);

  // TODO(neis): It's odd that we do optimizations below that don't really care
  // about the feedback, but we don't do them when the feedback is megamorphic.
  if (feedback.transition_groups().empty()) return NoChange();

  ElementAccessFeedback const& refined_feedback =
      TryRefineElementAccessFeedback(feedback, receiver, effect);

  AccessMode access_mode = refined_feedback.keyed_mode().access_mode();
  if ((access_mode == AccessMode::kLoad || access_mode == AccessMode::kHas) &&
      receiver->opcode() == IrOpcode::kHeapConstant) {
    Reduction reduction = ReduceElementLoadFromHeapConstant(
        node, index, access_mode, refined_feedback.keyed_mode().load_mode());
    if (reduction.Changed()) return reduction;
  }

  if (!refined_feedback.transition_groups().empty() &&
      refined_feedback.HasOnlyStringMaps(broker())) {
    return ReduceElementAccessOnString(node, index, value,
                                       refined_feedback.keyed_mode());
  }

  AccessInfoFactory access_info_factory(broker(), graph()->zone());
  ZoneVector<ElementAccessInfo> access_infos(zone());
  if (!access_info_factory.ComputeElementAccessInfos(refined_feedback,
                                                     &access_infos) ||
      access_infos.empty()) {
    return NoChange();
  }

  // Do not optimize AccessMode::kDefine for typed arrays.
  if (access_mode == AccessMode::kDefine) {
    for (const ElementAccessInfo& access_info : access_infos) {
      if (IsTypedArrayOrRabGsabTypedArrayElementsKind(
              access_info.elements_kind())) {
        return NoChange();
      }
    }
  }

  // Do not optimize Float16 typed arrays, since they are not yet supported by
  // the rest of the compiler.
  // TODO(v8:14012): We could lower further here and emit LoadTypedElement (like
  // we do for other typed arrays). However, given the lack of hardware support
  // for Float16 operations, it's not clear whether optimizing further would be
  // really useful.
  for (const ElementAccessInfo& access_info : access_infos) {
    if (IsFloat16TypedArrayElementsKind(access_info.elements_kind())) {
      return NoChange();
    }
  }

  // For holey stores or growing stores, we need to check that the prototype
  // chain contains no setters for elements, and we need to guard those checks
  // via code dependencies on the relevant prototype maps.
  if (access_mode == AccessMode::kStore) {
    // TODO(turbofan): We could have a fast path here, that checks for the
    // common case of Array or Object prototype only and therefore avoids
    // the zone allocation of this vector.
    ZoneVector<MapRef> prototype_maps(zone());
    for (ElementAccessInfo const& access_info : access_infos) {
      for (MapRef receiver_map : access_info.lookup_start_object_maps()) {
        // If the {receiver_map} has a prototype and its elements backing
        // store is either holey, or we have a potentially growing store,
        // then we need to check that all prototypes have stable maps with
        // no element accessors and no throwing behavior for elements (and we
        // need to guard against changes to that below).
        if ((IsHoleyOrDictionaryElementsKind(receiver_map.elements_kind()) ||
             StoreModeCanGrow(feedback.keyed_mode().store_mode())) &&
            !receiver_map.PrototypesElementsDoNotHaveAccessorsOrThrow(
                broker(), &prototype_maps)) {
          return NoChange();
        }

        // TODO(v8:12547): Support writing to objects in shared space, which
        // need a write barrier that calls Object::Share to ensure the RHS is
        // shared.
        if (InstanceTypeChecker::IsAlwaysSharedSpaceJSObject(
                receiver_map.instance_type())) {
          return NoChange();
        }
      }
    }
    for (MapRef prototype_map : prototype_maps) {
      dependencies()->DependOnStableMap(prototype_map);
    }
  } else if (access_mode == AccessMode::kHas) {
    // If we have any fast arrays, we need to check and depend on
    // NoElementsProtector.
    for (ElementAccessInfo const& access_info : access_infos) {
      if (IsFastElementsKind(access_info.elements_kind())) {
        if (!dependencies()->DependOnNoElementsProtector()) return NoChange();
        break;
      }
    }
  }

  // Check for the monomorphic case.
  PropertyAccessBuilder access_builder(jsgraph(), broker());
  if (access_infos.size() == 1) {
    ElementAccessInfo access_info = access_infos.front();

    // Perform possible elements kind transitions.
    MapRef transition_target = access_info.lookup_start_object_maps().front();
    for (MapRef transition_source : access_info.transition_sources()) {
      DCHECK_EQ(access_info.lookup_start_object_maps().size(), 1);
      effect = graph()->NewNode(
          simplified()->TransitionElementsKind(ElementsTransition(
              IsSimpleMapChangeTransition(transition_source.elements_kind(),
                                          transition_target.elements_kind())
                  ? ElementsTransition::kFastTransition
                  : ElementsTransition::kSlowTransition,
              transition_source, transition_target)),
          receiver, effect, control);
    }

    // TODO(turbofan): The effect/control linearization will not find a
    // FrameState after the StoreField or Call that is generated for the
    // elements kind transition above. This is because those operators
    // don't have the kNoWrite flag on it, even though they are not
    // observable by JavaScript.
    Node* frame_state =
        NodeProperties::FindFrameStateBefore(node, jsgraph()->Dead());
    effect =
        graph()->NewNode(common()->Checkpoint(), frame_state, effect, control);

    // Perform map check on the {receiver}.
    access_builder.BuildCheckMaps(receiver, &effect, control,
                                  access_info.lookup_start_object_maps());

    // Access the actual element.
    ValueEffectControl continuation =
        BuildElementAccess(receiver, index, value, effect, control, context,
                           access_info, feedback.keyed_mode());
    value = continuation.value();
    effect = continuation.effect();
    control = continuation.control();
  } else {
    // The final states for every polymorphic branch. We join them with
    // Merge+Phi+EffectPhi at the bottom.
    ZoneVector<Node*> values(zone());
    ZoneVector<Node*> effects(zone());
    ZoneVector<Node*> controls(zone());

    // Generate code for the various different element access patterns.
    Node* fallthrough_control = control;
    for (size_t j = 0; j < access_infos.size(); ++j) {
      ElementAccessInfo const& access_info = access_infos[j];
      Node* this_receiver = receiver;
      Node* this_value = value;
      Node* this_index = index;
      Effect this_effect = effect;
      Control this_control{fallthrough_control};

      // Perform possible elements kind transitions.
      MapRef transition_target = access_info.lookup_start_object_maps().front();
      for (MapRef transition_source : access_info.transition_sources()) {
        DCHECK_EQ(access_info.lookup_start_object_maps().size(), 1);
        this_effect = graph()->NewNode(
            simplified()->TransitionElementsKind(ElementsTransition(
                IsSimpleMapChangeTransition(transition_source.elements_kind(),
                                            transition_target.elements_kind())
                    ? ElementsTransition::kFastTransition
                    : ElementsTransition::kSlowTransition,
                transition_source, transition_target)),
            receiver, this_effect, this_control);
      }

      // Perform map check(s) on {receiver}.
      ZoneVector<MapRef> const& receiver_maps =
          access_info.lookup_start_object_maps();
      if (j == access_infos.size() - 1) {
        // Last map check on the fallthrough control path, do a
        // conditional eager deoptimization exit here.
        access_builder.BuildCheckMaps(receiver, &this_effect, this_control,
                                      receiver_maps);
        fallthrough_control = nullptr;
      } else {
        // Explicitly branch on the {receiver_maps}.
        ZoneRefSet<Map> maps(receiver_maps.begin(), receiver_maps.end(),
                             graph()->zone());
        Node* check = this_effect =
            graph()->NewNode(simplified()->CompareMaps(maps), receiver,
                             this_effect, fallthrough_control);
        Node* branch =
            graph()->NewNode(common()->Branch(), check, fallthrough_control);
        fallthrough_control = graph()->NewNode(common()->IfFalse(), branch);
        this_control = graph()->NewNode(common()->IfTrue(), branch);

        // Introduce a MapGuard to learn from this on the effect chain.
        this_effect = graph()->NewNode(simplified()->MapGuard(maps), receiver,
                                       this_effect, this_control);
      }

      // Access the actual element.
      ValueEffectControl continuation = BuildElementAccess(
          this_receiver, this_index, this_value, this_effect, this_control,
          context, access_info, feedback.keyed_mode());
      values.push_back(continuation.value());
      effects.push_back(continuation.effect());
      controls.push_back(continuation.control());
    }

    DCHECK_NULL(fallthrough_control);

    // Generate the final merge point for all (polymorphic) branches.
    int const control_count = static_cast<int>(controls.size());
    if (control_count == 0) {
      value = effect = control = jsgraph()->Dead();
    } else if (control_count == 1) {
      value = values.front();
      effect = effects.front();
      control = controls.front();
    } else {
      control = graph()->NewNode(common()->Merge(control_count), control_count,
                                 &controls.front());
      values.push_back(control);
      value = graph()->NewNode(
          common()->Phi(MachineRepresentation::kTagged, control_count),
          control_count + 1, &values.front());
      effects.push_back(control);
      effect = graph()->NewNode(common()->EffectPhi(control_count),
                                control_count + 1, &effects.front());
    }
  }

  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceElementLoadFromHeapConstant(
    Node* node, Node* key, AccessMode access_mode,
    KeyedAccessLoadMode load_mode) {
  DCHECK(node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSHasProperty);
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  HeapObjectMatcher mreceiver(receiver);
  HeapObjectRef receiver_ref = mreceiver.Ref(broker());
  if (receiver_ref.IsNull() || receiver_ref.IsUndefined() ||
      // The 'in' operator throws a TypeError on primitive values.
      (receiver_ref.IsString() && access_mode == AccessMode::kHas)) {
    return NoChange();
  }

  // Check whether we're accessing a known element on the {receiver} and can
  // constant-fold the load.
  NumberMatcher mkey(key);
  if (mkey.IsInteger() &&
      mkey.IsInRange(0.0, static_cast<double>(JSObject::kMaxElementIndex))) {
    static_assert(JSObject::kMaxElementIndex <= kMaxUInt32);
    const uint32_t index = static_cast<uint32_t>(mkey.ResolvedValue());
    OptionalObjectRef element;

    if (receiver_ref.IsJSObject()) {
      JSObjectRef jsobject_ref = receiver_ref.AsJSObject();
      OptionalFixedArrayBaseRef elements =
          jsobject_ref.elements(broker(), kRelaxedLoad);
      if (elements.has_value()) {
        element = jsobject_ref.GetOwnConstantElement(broker(), *elements, index,
                                                     dependencies());
        if (!element.has_value() && receiver_ref.IsJSArray()) {
          // We didn't find a constant element, but if the receiver is a
          // cow-array we can exploit the fact that any future write to the
          // element will replace the whole elements storage.
          element = receiver_ref.AsJSArray().GetOwnCowElement(broker(),
                                                              *elements, index);
          if (element.has_value()) {
            Node* actual_elements = effect = graph()->NewNode(
                simplified()->LoadField(AccessBuilder::ForJSObjectElements()),
                receiver, effect, control);
            Node* check = graph()->NewNode(
                simplified()->ReferenceEqual(), actual_elements,
                jsgraph()->ConstantNoHole(*elements, broker()));
            effect = graph()->NewNode(
                simplified()->CheckIf(
                    DeoptimizeReason::kCowArrayElementsChanged),
                check, effect, control);
          }
        }
      }
    } else if (receiver_ref.IsString()) {
      element =
          receiver_ref.AsString().GetCharAsStringOrUndefined(broker(), index);
    }

    if (element.has_value()) {
      Node* value = access_mode == AccessMode::kHas
                        ? jsgraph()->TrueConstant()
                        : jsgraph()->ConstantNoHole(*element, broker());
      ReplaceWithValue(node, value, effect, control);
      return Replace(value);
    }
  }

  // For constant Strings we can eagerly strength-reduce the keyed
  // accesses using the known length, which doesn't change.
  if (receiver_ref.IsString()) {
    DCHECK_NE(access_mode, AccessMode::kHas);
    // Ensure that {key} is less than {receiver} length.
    Node* length = jsgraph()->ConstantNoHole(receiver_ref.AsString().length());

    // Load the single character string from {receiver} or yield
    // undefined if the {key} is out of bounds (depending on the
    // {load_mode}).
    Node* value = BuildIndexedStringLoad(receiver, key, length, &effect,
                                         &control, load_mode);
    ReplaceWithValue(node, value, effect, control);
    return Replace(value);
  }

  return NoChange();
}

Reduction JSNativeContextSpecialization::ReducePropertyAccess(
    Node* node, Node* key, OptionalNameRef static_name, Node* value,
    FeedbackSource const& source, AccessMode access_mode) {
  DCHECK_EQ(key == nullptr, static_name.has_value());
  DCHECK(node->opcode() == IrOpcode::kJSLoadProperty ||
         node->opcode() == IrOpcode::kJSSetKeyedProperty ||
         node->opcode() == IrOpcode::kJSStoreInArrayLiteral ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnPropertyInLiteral ||
         node->opcode() == IrOpcode::kJSHasProperty ||
         node->opcode() == IrOpcode::kJSLoadNamed ||
         node->opcode() == IrOpcode::kJSSetNamedProperty ||
         node->opcode() == IrOpcode::kJSDefineNamedOwnProperty ||
         node->opcode() == IrOpcode::kJSLoadNamedFromSuper ||
         node->opcode() == IrOpcode::kJSDefineKeyedOwnProperty);
  DCHECK_GE(node->op()->ControlOutputCount(), 1);

  ProcessedFeedback const& feedback =
      broker()->GetFeedbackForPropertyAccess(source, access_mode, static_name);
  switch (feedback.kind()) {
    case ProcessedFeedback::kInsufficient:
      return ReduceEagerDeoptimize(
          node,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess);
    case ProcessedFeedback::kNamedAccess:
      return ReduceNamedAccess(node, value, feedback.AsNamedAccess(),
                               access_mode, key);
    case ProcessedFeedback::kMegaDOMPropertyAccess:
      DCHECK_EQ(access_mode, AccessMode::kLoad);
      DCHECK_NULL(key);
      return ReduceMegaDOMPropertyAccess(
          node, value, feedback.AsMegaDOMPropertyAccess(), source);
    case ProcessedFeedback::kElementAccess:
      DCHECK_EQ(feedback.AsElementAccess().keyed_mode().access_mode(),
                access_mode);
      DCHECK_NE(node->opcode(), IrOpcode::kJSLoadNamedFromSuper);
      return ReduceElementAccess(node, key, value, feedback.AsElementAccess());
    default:
      UNREACHABLE();
  }
}

Reduction JSNativeContextSpecialization::ReduceEagerDeoptimize(
    Node* node, DeoptimizeReason reason) {
  if (!(flags() & kBailoutOnUninitialized)) return NoChange();

  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  Node* frame_state =
      NodeProperties::FindFrameStateBefore(node, jsgraph()->Dead());
  Node* deoptimize =
      graph()->NewNode(common()->Deoptimize(reason, FeedbackSource()),
                       frame_state, effect, control);
  MergeControlToEnd(graph(), common(), deoptimize);
  node->TrimInputCount(0);
  NodeProperties::ChangeOp(node, common()->Dead());
  return Changed(node);
}

Reduction JSNativeContextSpecialization::ReduceJSHasProperty(Node* node) {
  JSHasPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  Node* value = jsgraph()->Dead();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, n.key(), std::nullopt, value,
                              FeedbackSource(p.feedback()), AccessMode::kHas);
}

Reduction JSNativeContextSpecialization::ReduceJSLoadPropertyWithEnumeratedKey(
    Node* node) {
  // We can optimize a property load if it's being used inside a for..in:
  //   for (name in receiver) {
  //     value = receiver[name];
  //     ...
  //   }
  //
  // If the for..in is in fast-mode, we know that the {receiver} has {name}
  // as own property, otherwise the enumeration wouldn't include it. The graph
  // constructed by the BytecodeGraphBuilder in this case looks like this:

  // receiver
  //  ^    ^
  //  |    |
  //  |    +-+
  //  |      |
  //  |   JSToObject
  //  |      ^
  //  |      |
  //  |      |
  //  |  JSForInNext
  //  |      ^
  //  |      |
  //  +----+ |
  //       | |
  //       | |
  //   JSLoadProperty

  // If the for..in has only seen maps with enum cache consisting of keys
  // and indices so far, we can turn the {JSLoadProperty} into a map check
  // on the {receiver} and then just load the field value dynamically via
  // the {LoadFieldByIndex} operator. The map check is only necessary when
  // TurboFan cannot prove that there is no observable side effect between
  // the {JSForInNext} and the {JSLoadProperty} node.
  //
  // We can do a similar optimization when the receiver of {JSLoadProperty} is
  // not identical to the receiver of {JSForInNext}:
  //   for (name in receiver) {
  //     value = object[name];
  //     ...
  //   }
  //
  // This is because when the key is {JSForInNext}, we will generate a
  // {GetEnumeratedKeyedProperty} bytecode for {JSLoadProperty}. If the bytecode
  // always manages to use the enum cache, we will keep the inline cache in
  // uninitialized state. So If the graph is as below, we can firstly do a map
  // check on {object} and then turn the {JSLoadProperty} into the
  // {LoadFieldByIndex}. This is also safe when the bytecode has never been
  // profiled. When it happens to pass the the map check, we can use the fast
  // path. Otherwise it will trigger a deoptimization.

  // object     receiver
  //  ^             ^
  //  |             |
  //  |             |
  //  |             |
  //  |        JSToObject
  //  |             ^
  //  |             |
  //  |             |
  //  |        JSForInNext
  //  |             ^
  //  |             |
  //  +----+  +-----+
  //       |  |
  //       |  |
  //   JSLoadProperty (insufficient feedback)

  // Also note that it's safe to look through the {JSToObject}, since the
  // [[Get]] operation does an implicit ToObject anyway, and these operations
  // are not observable.

  DCHECK_EQ(IrOpcode::kJSLoadProperty, node->opcode());
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  JSForInNextNode name(NodeProperties::GetValueInput(node, 1));
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);

  if (name.Parameters().mode() != ForInMode::kUseEnumCacheKeysAndIndices) {
    return NoChange();
  }

  Node* object = name.receiver();
  Node* cache_type = name.cache_type();
  Node* index = name.index();
  if (object->opcode() == IrOpcode::kJSToObject) {
    object = NodeProperties::GetValueInput(object, 0);
  }
  bool speculating_object_is_receiver = false;
  if (object != receiver) {
    JSLoadPropertyNode n(node);
    PropertyAccess const& p = n.Parameters();

    ProcessedFeedback const& feedback = broker()->GetFeedbackForPropertyAccess(
        FeedbackSource(p.feedback()), AccessMode::kLoad, std::nullopt);
    // When the feedback is uninitialized, it is either a load from a
    // {GetEnumeratedKeyedProperty} which always hits the enum cache, or a keyed
    // load that had never been reached. In either case, we can check the map
    // of the receiver and use the enum cache if the map match the {cache_type}.
    if (feedback.kind() != ProcessedFeedback::kInsufficient) {
      return NoChange();
    }

    // Ensure that {receiver} is a HeapObject.
    receiver = effect = graph()->NewNode(simplified()->CheckHeapObject(),
                                         receiver, effect, control);
    speculating_object_is_receiver = true;
  }

  // No need to repeat the map check if we can prove that there's no
  // observable side effect between {effect} and {name]. But we always need a
  // map check when {object} is not identical to {receiver}.
  if (!NodeProperties::NoObservableSideEffectBetween(effect, name) ||
      speculating_object_is_receiver) {
    // Check that the {receiver} map is still valid.
    Node* receiver_map = effect =
        graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()),
                         receiver, effect, control);
    Node* check = graph()->NewNode(simplified()->ReferenceEqual(), receiver_map,
                                   cache_type);
    effect =
        graph()->NewNode(simplified()->CheckIf(DeoptimizeReason::kWrongMap),
                         check, effect, control);
  }

  // Load the enum cache indices from the {cache_type}.
  Node* descriptor_array = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapDescriptors()), cache_type,
      effect, control);
  Node* enum_cache = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForDescriptorArrayEnumCache()),
      descriptor_array, effect, control);
  Node* enum_indices = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForEnumCacheIndices()), enum_cache,
      effect, control);

  // Ensure that the {enum_indices} are valid.
  Node* check = graph()->NewNode(
      simplified()->BooleanNot(),
      graph()->NewNode(simplified()->ReferenceEqual(), enum_indices,
                       jsgraph()->EmptyFixedArrayConstant()));
  effect = graph()->NewNode(
      simplified()->CheckIf(DeoptimizeReason::kWrongEnumIndices), check, effect,
      control);

  // Determine the key from the {enum_indices}.
  Node* key = effect = graph()->NewNode(
      simplified()->LoadElement(
          AccessBuilder::ForFixedArrayElement(PACKED_SMI_ELEMENTS)),
      enum_indices, index, effect, control);

  // Load the actual field value.
  Node* value = effect = graph()->NewNode(simplified()->LoadFieldByIndex(),
                                          receiver, key, effect, control);
  ReplaceWithValue(node, value, effect, control);
  return Replace(value);
}

Reduction JSNativeContextSpecialization::ReduceJSLoadProperty(Node* node) {
  JSLoadPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  Node* name = n.key();

  if (name->opcode() == IrOpcode::kJSForInNext) {
    Reduction reduction = ReduceJSLoadPropertyWithEnumeratedKey(node);
    if (reduction.Changed()) return reduction;
  }

  if (!p.feedback().IsValid()) return NoChange();
  Node* value = jsgraph()->Dead();
  return ReducePropertyAccess(node, name, std::nullopt, value,
                              FeedbackSource(p.feedback()), AccessMode::kLoad);
}

Reduction JSNativeContextSpecialization::ReduceJSSetKeyedProperty(Node* node) {
  JSSetKeyedPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, n.key(), std::nullopt, n.value(),
                              FeedbackSource(p.feedback()), AccessMode::kStore);
}

Reduction JSNativeContextSpecialization::ReduceJSDefineKeyedOwnProperty(
    Node* node) {
  JSDefineKeyedOwnPropertyNode n(node);
  PropertyAccess const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, n.key(), std::nullopt, n.value(),
                              FeedbackSource(p.feedback()),
                              AccessMode::kDefine);
}

Node* JSNativeContextSpecialization::InlinePropertyGetterCall(
    Node* receiver, ConvertReceiverMode receiver_mode,
    Node* lookup_start_object, Node* context, Node* frame_state, Node** effect,
    Node** control, ZoneVector<Node*>* if_exceptions,
    PropertyAccessInfo const& access_info) {
  ObjectRef constant = access_info.constant().value();

  if (access_info.IsDictionaryProtoAccessorConstant()) {
    // For fast mode holders we recorded dependencies in BuildPropertyLoad.
    for (const MapRef map : access_info.lookup_start_object_maps()) {
      dependencies()->DependOnConstantInDictionaryPrototypeChain(
          map, access_info.name(), constant, PropertyKind::kAccessor);
    }
  }

  Node* target = jsgraph()->ConstantNoHole(constant, broker());
  // Introduce the call to the getter function.
  Node* value;
  if (constant.IsJSFunction()) {
    Node* feedback = jsgraph()->UndefinedConstant();
    value = *effect = *control = graph()->NewNode(
        jsgraph()->javascript()->Call(JSCallNode::ArityForArgc(0),
                                      CallFrequency(), FeedbackSource(),
                                      receiver_mode),
        target, receiver, feedback, context, frame_state, *effect, *control);
  } else {
    // Disable optimizations for super ICs using API getters, so that we get
    // the correct receiver checks.
    if (receiver != lookup_start_object) {
      return nullptr;
    }
    Node* api_holder = access_info.api_holder().has_value()
                           ? jsgraph()->ConstantNoHole(
                                 access_info.api_holder().value(), broker())
                           : receiver;
    value = InlineApiCall(receiver, api_holder, frame_state, nullptr, effect,
                          control, constant.AsFunctionTemplateInfo());
  }
  // Remember to rewire the IfException edge if this is inside a try-block.
  if (if_exceptions != nullptr) {
    // Create the appropriate IfException/IfSuccess projections.
    Node* const if_exception =
        graph()->NewNode(common()->IfException(), *control, *effect);
    Node* const if_success = graph()->NewNode(common()->IfSuccess(), *control);
    if_exceptions->push_back(if_exception);
    *control = if_success;
  }
  return value;
}

void JSNativeContextSpecialization::InlinePropertySetterCall(
    Node* receiver, Node* value, Node* context, Node* frame_state,
    Node** effect, Node** control, ZoneVector<Node*>* if_exceptions,
    PropertyAccessInfo const& access_info) {
  ObjectRef constant = access_info.constant().value();
  Node* target = jsgraph()->ConstantNoHole(constant, broker());
  // Introduce the call to the setter function.
  if (constant.IsJSFunction()) {
    Node* feedback = jsgraph()->UndefinedConstant();
    *effect = *control = graph()->NewNode(
        jsgraph()->javascript()->Call(JSCallNode::ArityForArgc(1),
                                      CallFrequency(), FeedbackSource(),
                                      ConvertReceiverMode::kNotNullOrUndefined),
        target, receiver, value, feedback, context, frame_state, *effect,
        *control);
  } else {
    Node* api_holder = access_info.api_holder().has_value()
                           ? jsgraph()->ConstantNoHole(
                                 access_info.api_holder().value(), broker())
                           : receiver;
    InlineApiCall(receiver, api_holder, frame_state, value, effect, control,
                  constant.AsFunctionTemplateInfo());
  }
  // Remember to rewire the IfException edge if this is inside a try-block.
  if (if_exceptions != nullptr) {
    // Create the appropriate IfException/IfSuccess projections.
    Node* const if_exception =
        graph()->NewNode(common()->IfException(), *control, *effect);
    Node* const if_success = graph()->NewNode(common()->IfSuccess(), *control);
    if_exceptions->push_back(if_exception);
    *control = if_success;
  }
}

Node* JSNativeContextSpecialization::InlineApiCall(
    Node* receiver, Node* api_holder, Node* frame_state, Node* value,
    Node** effect, Node** control,
    FunctionTemplateInfoRef function_template_info) {
  compiler::OptionalObjectRef maybe_callback_data =
      function_template_info.callback_data(broker());
  // Check if the function has an associated C++ code to execute.
  if (!maybe_callback_data.has_value()) {
    // TODO(ishell): consider generating "return undefined" for empty function
    // instead of failing.
    TRACE_BROKER_MISSING(broker(), "call code for function template info "
                                       << function_template_info);
    return nullptr;
  }

  // Only setters have a value.
  int const argc = value == nullptr ? 0 : 1;
  // The builtin always expects the receiver as the first param on the stack.
  bool no_profiling = broker()->dependencies()->DependOnNoProfilingProtector();
  Callable call_api_callback = Builtins::CallableFor(
      isolate(), no_profiling ? Builtin::kCallApiCallbackOptimizedNoProfiling
                              : Builtin::kCallApiCallbackOptimized);
  CallInterfaceDescriptor call_interface_descriptor =
      call_api_callback.descriptor();
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), call_interface_descriptor,
      call_interface_descriptor.GetStackParameterCount() + argc +
          1 /* implicit receiver */,
      CallDescriptor::kNeedsFrameState);

  Node* func_templ =
      jsgraph()->HeapConstantNoHole(function_template_info.object());
  ApiFunction function(function_template_info.callback(broker()));
  Node* function_reference =
      graph()->NewNode(common()->ExternalConstant(ExternalReference::Create(
          &function, ExternalReference::DIRECT_API_CALL)));
  Node* code = jsgraph()->HeapConstantNoHole(call_api_callback.code());

  // Add CallApiCallbackStub's register argument as well.
  Node* context = jsgraph()->ConstantNoHole(native_context(), broker());
  Node* inputs[11] = {
      code,       function_reference, jsgraph()->ConstantNoHole(argc),
      func_templ, api_holder,         receiver};
  int index = 6 + argc;
  inputs[index++] = context;
  inputs[index++] = frame_state;
  inputs[index++] = *effect;
  inputs[index++] = *control;
  // This needs to stay here because of the edge case described in
  // http://crbug.com/675648.
  if (value != nullptr) {
    inputs[6] = value;
  }

  return *effect = *control =
             graph()->NewNode(common()->Call(call_descriptor), index, inputs);
}

std::optional<JSNativeContextSpecialization::ValueEffectControl>
JSNativeContextSpecialization::BuildPropertyLoad(
    Node* lookup_start_object, Node* receiver, Node* context, Node* frame_state,
    Node* effect, Node* control, NameRef name, ZoneVector<Node*>* if_exceptions,
    PropertyAccessInfo const& access_info) {
  // Determine actual holder and perform prototype chain checks.
  OptionalJSObjectRef holder = access_info.holder();
  if (holder.has_value() && !access_info.HasDictionaryHolder()) {
    dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype,
        holder.value());
  }

  // Generate the actual property access.
  Node* value;
  if (access_info.IsNotFound()) {
    value = jsgraph()->UndefinedConstant();
  } else if (access_info.IsFastAccessorConstant() ||
             access_info.IsDictionaryProtoAccessorConstant()) {
    ConvertReceiverMode receiver_mode =
        receiver == lookup_start_object
            ? ConvertReceiverMode::kNotNullOrUndefined
            : ConvertReceiverMode::kAny;
    value = InlinePropertyGetterCall(
        receiver, receiver_mode, lookup_start_object, context, frame_state,
        &effect, &control, if_exceptions, access_info);
  } else if (access_info.IsModuleExport()) {
    Node* cell = jsgraph()->ConstantNoHole(
        access_info.constant().value().AsCell(), broker());
    value = effect =
        graph()->NewNode(simplified()->LoadField(AccessBuilder::ForCellValue()),
                         cell, effect, control);
  } else if (access_info.IsStringLength()) {
    DCHECK_EQ(receiver, lookup_start_object);
    value = graph()->NewNode(simplified()->StringLength(), receiver);
  } else {
    DCHECK(access_info.IsDataField() || access_info.IsFastDataConstant() ||
           access_info.IsDictionaryProtoDataConstant());
    PropertyAccessBuilder access_builder(jsgraph(), broker());
    if (access_info.IsDictionaryProtoDataConstant()) {
      auto maybe_value =
          access_builder.FoldLoadDictPrototypeConstant(access_info);
      if (!maybe_value) return {};
      value = maybe_value.value();
    } else {
      value = access_builder.BuildLoadDataField(
          name, access_info, lookup_start_object, &effect, &control);
    }
  }
  if (value != nullptr) {
    return ValueEffectControl(value, effect, control);
  }
  return std::optional<ValueEffectControl>();
}

JSNativeContextSpecialization::ValueEffectControl
JSNativeContextSpecialization::BuildPropertyTest(
    Node* effect, Node* control, PropertyAccessInfo const& access_info) {
  // TODO(v8:11457) Support property tests for dictionary mode protoypes.
  DCHECK(!access_info.HasDictionaryHolder());

  // Determine actual holder and perform prototype chain checks.
  OptionalJSObjectRef holder = access_info.holder();
  if (holder.has_value()) {
    dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype,
        holder.value());
  }

  return ValueEffectControl(
      jsgraph()->BooleanConstant(!access_info.IsNotFound()), effect, control);
}

std::optional<JSNativeContextSpecialization::ValueEffectControl>
JSNativeContextSpecialization::BuildPropertyAccess(
    Node* lookup_start_object, Node* receiver, Node* value, Node* context,
    Node* frame_state, Node* effect, Node* control, NameRef name,
    ZoneVector<Node*>* if_exceptions, PropertyAccessInfo const& access_info,
    AccessMode access_mode) {
  switch (access_mode) {
    case AccessMode::kLoad:
      return BuildPropertyLoad(lookup_start_object, receiver, context,
                               frame_state, effect, control, name,
                               if_exceptions, access_info);
    case AccessMode::kStore:
    case AccessMode::kStoreInLiteral:
    case AccessMode::kDefine:
      DCHECK_EQ(receiver, lookup_start_object);
      return BuildPropertyStore(receiver, value, context, frame_state, effect,
                                control, name, if_exceptions, access_info,
                                access_mode);
    case AccessMode::kHas:
      DCHECK_EQ(receiver, lookup_start_object);
      return BuildPropertyTest(effect, control, access_info);
  }
  UNREACHABLE();
}

JSNativeContextSpecialization::ValueEffectControl
JSNativeContextSpecialization::BuildPropertyStore(
    Node* receiver, Node* value, Node* context, Node* frame_state, Node* effect,
    Node* control, NameRef name, ZoneVector<Node*>* if_exceptions,
    PropertyAccessInfo const& access_info, AccessMode access_mode) {
  // Determine actual holder and perform prototype chain checks.
  PropertyAccessBuilder access_builder(jsgraph(), broker());
  OptionalJSObjectRef holder = access_info.holder();
  if (holder.has_value()) {
    DCHECK_NE(AccessMode::kStoreInLiteral, access_mode);
    DCHECK_NE(AccessMode::kDefine, access_mode);
    dependencies()->DependOnStablePrototypeChains(
        access_info.lookup_start_object_maps(), kStartAtPrototype,
        holder.value());
  }

  DCHECK(!access_info.IsNotFound());

  // Generate the actual property access.
  if (access_info.IsFastAccessorConstant()) {
    InlinePropertySetterCall(receiver, value, context, frame_state, &effect,
                             &control, if_exceptions, access_info);
  } else {
    DCHECK(access_info.IsDataField() || access_info.IsFastDataConstant());
    DCHECK(access_mode == AccessMode::kStore ||
           access_mode == AccessMode::kStoreInLiteral ||
           access_mode == AccessMode::kDefine);
    FieldIndex const field_index = access_info.field_index();
    Type const field_type = access_info.field_type();
    MachineRepresentation const field_representation =
        PropertyAccessBuilder::ConvertRepresentation(
            access_info.field_representation());
    Node* storage = receiver;
    if (!field_index.is_inobject()) {
      storage = effect = graph()->NewNode(
          simplified()->LoadField(
              AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer()),
          storage, effect, control);
    }
    if (access_info.IsFastDataConstant() && access_mode == AccessMode::kStore &&
        !access_info.HasTransitionMap()) {
      Node* deoptimize = graph()->NewNode(
          simplified()->CheckIf(DeoptimizeReason::kStoreToConstant),
          jsgraph()->FalseConstant(), effect, control);
      return ValueEffectControl(jsgraph()->UndefinedConstant(), deoptimize,
                                control);
    }
    FieldAccess field_access = {
        kTaggedBase,
        field_index.offset(),
        name.object(),
        OptionalMapRef(),
        field_type,
        MachineType::TypeForRepresentation(field_representation),
        kFullWriteBarrier,
        "BuildPropertyStore",
        access_info.GetConstFieldInfo(),
        access_mode == AccessMode::kStoreInLiteral};

    switch (field_representation) {
      case MachineRepresentation::kFloat64: {
        value = effect =
            graph()->NewNode(simplified()->CheckNumber(FeedbackSource()), value,
                             effect, control);
        if (access_info.HasTransitionMap()) {
          // Allocate a HeapNumber for the new property.
          AllocationBuilder a(jsgraph(), broker(), effect, control);
          a.Allocate(sizeof(HeapNumber), AllocationType::kYoung,
                     Type::OtherInternal());
          a.Store(AccessBuilder::ForMap(), broker()->heap_number_map());
          FieldAccess value_field_access = AccessBuilder::ForHeapNumberValue();
          value_field_access.const_field_info = field_access.const_field_info;
          a.Store(value_field_access, value);
          value = effect = a.Finish();

          field_access.type = Type::Any();
          field_access.machine_type = MachineType::TaggedPointer();
          field_access.write_barrier_kind = kPointerWriteBarrier;
        } else {
          // We just store directly to the HeapNumber.
          FieldAccess const storage_access = {
              kTaggedBase,
              field_index.offset(),
              name.object(),
              OptionalMapRef(),
              Type::OtherInternal(),
              MachineType::TaggedPointer(),
              kPointerWriteBarrier,
              "BuildPropertyStore",
              access_info.GetConstFieldInfo(),
              access_mode == AccessMode::kStoreInLiteral};
          storage = effect =
              graph()->NewNode(simplified()->LoadField(storage_access), storage,
                               effect, control);
          FieldAccess value_field_access = AccessBuilder::ForHeapNumberValue();
          value_field_access.const_field_info = field_access.const_field_info;
          value_field_access.is_store_in_literal =
              field_access.is_store_in_literal;
          field_access = value_field_access;
        }
        break;
      }
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTagged:
        if (field_representation == MachineRepresentation::kTaggedSigned) {
          value = effect = graph()->NewNode(
              simplified()->CheckSmi(FeedbackSource()), value, effect, control);
          field_access.write_barrier_kind = kNoWriteBarrier;

        } else if (field_representation ==
                   MachineRepresentation::kTaggedPointer) {
          OptionalMapRef field_map = access_info.field_map();
          if (field_map.has_value()) {
            // Emit a map check for the value.
            effect = graph()->NewNode(
                simplified()->CheckMaps(CheckMapsFlag::kNone,
                                        ZoneRefSet<Map>(*field_map)),
                value, effect, control);
          } else {
            // Ensure that {value} is a HeapObject.
            value = effect = graph()->NewNode(simplified()->CheckHeapObject(),
                                              value, effect, control);
          }
          field_access.write_barrier_kind = kPointerWriteBarrier;

        } else {
          DCHECK(field_representation == MachineRepresentation::kTagged);
        }
        break;
      case MachineRepresentation::kNone:
      case MachineRepresentation::kBit:
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
      case MachineRepresentation::kProtectedPointer:
      case MachineRepresentation::kIndirectPointer:
      case MachineRepresentation::kSandboxedPointer:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord64:
      case MachineRepresentation::kFloat16:
      case MachineRepresentation::kFloat32:
      case MachineRepresentation::kSimd128:
      case MachineRepresentation::kSimd256:
      case MachineRepresentation::kMapWord:
        UNREACHABLE();
    }
    // Check if we need to perform a transitioning store.
    OptionalMapRef transition_map = access_info.transition_map();
    if (transition_map.has_value()) {
      // Check if we need to grow the properties backing store
      // with this transitioning store.
      MapRef transition_map_ref = transition_map.value();
      MapRef original_map = transition_map_ref.GetBackPointer(broker()).AsMap();
      if (!field_index.is_inobject()) {
        // If slack tracking ends after this compilation started but before it's
        // finished, then we could {original_map} could be out-of-sync with
        // {transition_map_ref}. In particular, its UnusedPropertyFields could
        // be non-zero, which would lead us to not extend the property backing
        // store, while the underlying Map has actually zero
        // UnusedPropertyFields. Thus, we install a dependency on {orininal_map}
        // now, so that if such a situation happens, we'll throw away the code.
        dependencies()->DependOnNoSlackTrackingChange(original_map);
      }
      if (original_map.UnusedPropertyFields() == 0) {
        DCHECK(!field_index.is_inobject());

        // Reallocate the properties {storage}.
        storage = effect = BuildExtendPropertiesBackingStore(
            original_map, storage, effect, control);

        // Perform the actual store.
        effect = graph()->NewNode(simplified()->StoreField(field_access),
                                  storage, value, effect, control);

        // Atomically switch to the new properties below.
        field_access = AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer();
        value = storage;
        storage = receiver;
      }
      effect = graph()->NewNode(
          common()->BeginRegion(RegionObservability::kObservable), effect);
      effect = graph()->NewNode(
          simplified()->StoreField(AccessBuilder::ForMap()), receiver,
          jsgraph()->ConstantNoHole(transition_map_ref, broker()), effect,
          control);
      effect = graph()->NewNode(simplified()->StoreField(field_access), storage,
                                value, effect, control);
      effect = graph()->NewNode(common()->FinishRegion(),
                                jsgraph()->UndefinedConstant(), effect);
    } else {
      // Regular non-transitioning field store.
      effect = graph()->NewNode(simplified()->StoreField(field_access), storage,
                                value, effect, control);
    }
  }

  return ValueEffectControl(value, effect, control);
}

Reduction
JSNativeContextSpecialization::ReduceJSDefineKeyedOwnPropertyInLiteral(
    Node* node) {
  JSDefineKeyedOwnPropertyInLiteralNode n(node);
  FeedbackParameter const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();

  NumberMatcher mflags(n.flags());
  CHECK(mflags.HasResolvedValue());
  DefineKeyedOwnPropertyInLiteralFlags cflags(mflags.ResolvedValue());
  if (cflags & DefineKeyedOwnPropertyInLiteralFlag::kSetFunctionName)
    return NoChange();

  return ReducePropertyAccess(node, n.name(), std::nullopt, n.value(),
                              FeedbackSource(p.feedback()),
                              AccessMode::kStoreInLiteral);
}

Reduction JSNativeContextSpecialization::ReduceJSStoreInArrayLiteral(
    Node* node) {
  JSStoreInArrayLiteralNode n(node);
  FeedbackParameter const& p = n.Parameters();
  if (!p.feedback().IsValid()) return NoChange();
  return ReducePropertyAccess(node, n.index(), std::nullopt, n.value(),
                              FeedbackSource(p.feedback()),
                              AccessMode::kStoreInLiteral);
}

Reduction JSNativeContextSpecialization::ReduceJSToObject(Node* node) {
  DCHECK_EQ(IrOpcode::kJSToObject, node->opcode());
  Node* receiver = NodeProperties::GetValueInput(node, 0);
  Effect effect{NodeProperties::GetEffectInput(node)};

  MapInference inference(broker(), receiver, effect);
  if (!inference.HaveMaps() || !inference.AllOfInstanceTypesAreJSReceiver()) {
    return NoChange();
  }

  ReplaceWithValue(node, receiver, effect);
  return Replace(receiver);
}

JSNativeContextSpecialization::ValueEffectControl
JSNativeContextSpecialization::BuildElementAccess(
    Node* receiver, Node* index, Node* value, Node* effect, Node* control,
    Node* context, ElementAccessInfo const& access_info,
    KeyedAccessMode const& keyed_mode) {
  // TODO(bmeurer): We currently specialize based on elements kind. We should
  // also be able to properly support strings and other JSObjects here.
  ElementsKind elements_kind = access_info.elements_kind();
  ZoneVector<MapRef> const& receiver_maps =
      access_info.lookup_start_object_maps();

  if (IsTypedArrayElementsKind(elements_kind) ||
      IsRabGsabTypedArrayElementsKind(elements_kind)) {
    return BuildElementAccessForTypedArrayOrRabGsabTypedArray(
        receiver, index, value, effect, control, context, elements_kind,
        keyed_mode);
  }

  // Load the elements for the {receiver}.
  Node* elements = effect = graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForJSObjectElements()), receiver,
      effect, control);

  // Don't try to store to a copy-on-write backing store (unless supported by
  // the store mode).
  if (IsAnyStore(keyed_mode.access_mode()) &&
      IsSmiOrObjectElementsKind(elements_kind) &&
      !StoreModeHandlesCOW(keyed_mode.store_mode())) {
    effect = graph()->NewNode(
        simplified()->CheckMaps(CheckMapsFlag::kNone,
                                ZoneRefSet<Map>(broker()->fixed_array_map())),
        elements, effect, control);
  }

  // Check if the {receiver} is a JSArray.
  bool receiver_is_jsarray = HasOnlyJSArrayMaps(broker(), receiver_maps);

  // Load the length of the {receiver}.
  Node* length = effect =
      receiver_is_jsarray
          ? graph()->NewNode(
                simplified()->LoadField(
                    AccessBuilder::ForJSArrayLength(elements_kind)),
                receiver, effect, control)
          : graph()->NewNode(
                simplified()->LoadField(AccessBuilder::ForFixedArrayLength()),
                elements, effect, control);

  // Check if we might need to grow the {elements} backing store.
  if (keyed_mode.IsStore() && StoreModeCanGrow(keyed_mode.store_mode())) {
    // For growing stores we validate the {index} below.
  } else if (keyed_mode.IsLoad() &&
             LoadModeHandlesOOB(keyed_mode.load_mode()) &&
             CanTreatHoleAsUndefined(receiver_maps)) {
    // Check that the {index} is a valid array index, we do the actual
    // bounds check below and just skip the store below if it's out of
    // bounds for the {receiver}.
    index = effect = graph()->NewNode(
        simplified()->CheckBounds(FeedbackSource(),
                                  CheckBoundsFlag::kConvertStringAndMinusZero),
        index, jsgraph()->ConstantNoHole(Smi::kMaxValue), effect, control);
  } else {
    // Check that the {index} is in the valid range for the {receiver}.
    index = effect = graph()->NewNode(
        simplified()->CheckBounds(FeedbackSource(),
                                  CheckBoundsFlag::kConvertStringAndMinusZero),
        index, length, effect, control);
  }

  // Compute the element access.
  Type element_type = Type::NonInternal();
  MachineType element_machine_type = MachineType::AnyTagged();
  if (IsDoubleElementsKind(elements_kind)) {
    element_type = Type::Number();
    element_machine_type = MachineType::Float64();
  } else if (IsSmiElementsKind(elements_kind)) {
    element_type = Type::SignedSmall();
    element_machine_type = MachineType::TaggedSigned();
  }
  ElementAccess element_access = {kTaggedBase, FixedArray::kHeaderSize,
                                  element_type, element_machine_type,
                                  kFullWriteBarrier};

  // Access the actual element.
  if (keyed_mode.access_mode() == AccessMode::kLoad) {
    // Compute the real element access type, which includes the hole in case
    // of holey backing stores.
    if (IsHoleyElementsKind(elements_kind)) {
      element_access.type =
          Type::Union(element_type, Type::Hole(), graph()->zone());
    }
    if (elements_kind == HOLEY_ELEMENTS ||
        elements_kind == HOLEY_SMI_ELEMENTS) {
      element_access.machine_type = MachineType::AnyTagged();
    }

    // Check if we can return undefined for out-of-bounds loads.
    if (LoadModeHandlesOOB(keyed_mode.load_mode()) &&
        CanTreatHoleAsUndefined(receiver_maps)) {
      Node* check =
          graph()->NewNode(simplified()->NumberLessThan(), index, length);
      Node* branch =
          graph()->NewNode(common()->Branch(BranchHint::kTrue), check, control);

      Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
      Node* etrue = effect;
      Node* vtrue;
      {
        // Do a real bounds check against {length}. This is in order to
        // protect against a potential typer bug leading to the elimination of
        // the NumberLessThan above.
        if (v8_flags.turbo_typer_hardening) {
          index = etrue =
              graph()->NewNode(simplified()->CheckBounds(
                                   FeedbackSource(),
                                   CheckBoundsFlag::kConvertStringAndMinusZero |
                                       CheckBoundsFlag::kAbortOnOutOfBounds),
                               index, length, etrue, if_true);
        }

        // Perform the actual load
        vtrue = etrue =
            graph()->NewNode(simplified()->LoadElement(element_access),
                             elements, index, etrue, if_true);

        // Handle loading from holey backing stores correctly by mapping
        // the hole to undefined.
        if (elements_kind == HOLEY_ELEMENTS ||
            elements_kind == HOLEY_SMI_ELEMENTS) {
          // Turn the hole into undefined.
          vtrue = graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(),
                                   vtrue);
        } else if (elements_kind == HOLEY_DOUBLE_ELEMENTS) {
          // Return the signaling NaN hole directly if all uses are
          // truncating.
          if (LoadModeHandlesHoles(keyed_mode.load_mode())) {
            vtrue = graph()->NewNode(simplified()->ChangeFloat64HoleToTagged(),
                                     vtrue);
          } else {
            vtrue = etrue = graph()->NewNode(
                simplified()->CheckFloat64Hole(
                    CheckFloat64HoleMode::kAllowReturnHole, FeedbackSource()),
                vtrue, etrue, if_true);
          }
        }
      }

      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* efalse = effect;
      Node* vfalse;
      {
        // Materialize undefined for out-of-bounds loads.
        vfalse = jsgraph()->UndefinedConstant();
      }

      control = graph()->NewNode(common()->Merge(2), if_true, if_false);
      effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
      value = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               vtrue, vfalse, control);
    } else {
      // Perform the actual load.
      value = effect =
          graph()->NewNode(simplified()->LoadElement(element_access), elements,
                           index, effect, control);

      // Handle loading from holey backing stores correctly, by either mapping
      // the hole to undefined if possible, or deoptimizing otherwise.
      if (elements_kind == HOLEY_ELEMENTS ||
          elements_kind == HOLEY_SMI_ELEMENTS) {
        // Check if we are allowed to turn the hole into undefined.
        if (CanTreatHoleAsUndefined(receiver_maps)) {
          // Turn the hole into undefined.
          value = graph()->NewNode(simplified()->ConvertTaggedHoleToUndefined(),
                                   value);
        } else {
          // Bailout if we see the hole.
          value = effect = graph()->NewNode(simplified()->CheckNotTaggedHole(),
                                            value, effect, control);
        }
      } else if (elements_kind == HOLEY_DOUBLE_ELEMENTS) {
        // Perform the hole check on the result.
        // Check if we are allowed to return the hole directly.
        if (CanTreatHoleAsUndefined(receiver_maps)) {
          if (LoadModeHandlesHoles(keyed_mode.load_mode())) {
            // Return the signaling NaN hole directly if all uses are
            // truncating.
            value = graph()->NewNode(simplified()->ChangeFloat64HoleToTagged(),
                                     value);
          } else {
            value = effect = graph()->NewNode(
                simplified()->CheckFloat64Hole(
                    CheckFloat64HoleMode::kAllowReturnHole, FeedbackSource()),
                value, effect, control);
          }
        } else {
          value = effect = graph()->NewNode(
              simplified()->CheckFloat64Hole(
                  CheckFloat64HoleMode::kNeverReturnHole, FeedbackSource()),
              value, effect, control);
        }
      }
    }
  } else if (keyed_mode.access_mode() == AccessMode::kHas) {
    // For packed arrays with NoElementsProctector valid, a bound check
    // is equivalent to HasProperty.
    value = effect = graph()->NewNode(simplified()->SpeculativeNumberLessThan(
                                          NumberOperationHint::kSignedSmall),
                                      index, length, effect, control);
    if (IsHoleyElementsKind(elements_kind)) {
      // If the index is in bounds, do a load and hole check.

      Node* branch = graph()->NewNode(common()->Branch(), value, control);

      Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
      Node* efalse = effect;
      Node* vfalse = jsgraph()->FalseConstant();

      element_access.type =
          Type::Union(element_type, Type::Hole(), graph()->zone());

      if (elements_kind == HOLEY_ELEMENTS ||
          elements_kind == HOLEY_SMI_ELEMENTS) {
        element_access.machine_type = MachineType::AnyTagged();
      }

      Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
      Node* etrue = effect;

      Node* checked = etrue = graph()->NewNode(
          simplified()->CheckBounds(
              FeedbackSource(), CheckBoundsFlag::kConvertStringAndMinusZero),
          index, length, etrue, if_true);

      Node* element = etrue =
          graph()->NewNode(simplified()->LoadElement(element_access), elements,
                           checked, etrue, if_true);

      Node* vtrue;
      if (CanTreatHoleAsUndefined(receiver_maps)) {
        if (elements_kind == HOLEY_ELEMENTS ||
            elements_kind == HOLEY_SMI_ELEMENTS) {
          // Check if we are allowed to turn the hole into undefined.
          // Turn the hole into undefined.
          vtrue = graph()->NewNode(simplified()->ReferenceEqual(), element,
                                   jsgraph()->TheHoleConstant());
        } else {
          vtrue =
              graph()->NewNode(simplified()->NumberIsFloat64Hole(), element);
        }

        // has == !IsHole
        vtrue = graph()->NewNode(simplified()->BooleanNot(), vtrue);
      } else {
        if (elements_kind == HOLEY_ELEMENTS ||
            elements_kind == HOLEY_SMI_ELEMENTS) {
          // Bailout if we see the hole.
          etrue = graph()->NewNode(simplified()->CheckNotTaggedHole(), element,
                                   etrue, if_true);
        } else {
          etrue = graph()->NewNode(
              simplified()->CheckFloat64Hole(
                  CheckFloat64HoleMode::kNeverReturnHole, FeedbackSource()),
              element, etrue, if_true);
        }

        vtrue = jsgraph()->TrueConstant();
      }

      control = graph()->NewNode(common()->Merge(2), if_true, if_false);
      effect = graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
      value = graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                               vtrue, vfalse, control);
    }
  } else {
    DCHECK(keyed_mode.access_mode() == AccessMode::kStore ||
           keyed_mode.access_mode() == AccessMode::kStoreInLiteral ||
           keyed_mode.access_mode() == AccessMode::kDefine);

    if (IsSmiElementsKind(elements_kind)) {
      value = effect = graph()->NewNode(
          simplified()->CheckSmi(FeedbackSource()), value, effect, control);
    } else if (IsDoubleElementsKind(elements_kind)) {
      value = effect = graph()->NewNode(
          simplified()->CheckNumber(FeedbackSource()), value, effect, control);
      // Make sure we do not store signalling NaNs into double arrays.
      value = graph()->NewNode(simplified()->NumberSilenceNaN(), value);
    }

    // Ensure that copy-on-write backing store is writable.
    if (IsSmiOrObjectElementsKind(elements_kind) &&
        keyed_mode.store_mode() == KeyedAccessStoreMode::kHandleCOW) {
      elements = effect =
          graph()->NewNode(simplified()->EnsureWritableFastElements(), receiver,
                           elements, effect, control);
    } else if (StoreModeCanGrow(keyed_mode.store_mode())) {
      // Determine the length of the {elements} backing store.
      Node* elements_length = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForFixedArrayLength()),
          elements, effect, control);

      // Validate the {index} depending on holeyness:
      //
      // For HOLEY_*_ELEMENTS the {index} must not exceed the {elements}
      // backing store capacity plus the maximum allowed gap, as otherwise
      // the (potential) backing store growth would normalize and thus
      // the elements kind of the {receiver} would change to slow mode.
      //
      // For JSArray PACKED_*_ELEMENTS the {index} must be within the range
      // [0,length+1[ to be valid. In case {index} equals {length},
      // the {receiver} will be extended, but kept packed.
      //
      // Non-JSArray PACKED_*_ELEMENTS always grow by adding holes because they
      // lack the magical length property, which requires a map transition.
      // So we can assume that this did not happen if we did not see this map.
      Node* limit =
          IsHoleyElementsKind(elements_kind)
              ? graph()->NewNode(simplified()->NumberAdd(), elements_length,
                                 jsgraph()->ConstantNoHole(JSObject::kMaxGap))
          : receiver_is_jsarray
              ? graph()->NewNode(simplified()->NumberAdd(), length,
                                 jsgraph()->OneConstant())
              : elements_length;
      index = effect = graph()->NewNode(
          simplified()->CheckBounds(
              FeedbackSource(), CheckBoundsFlag::kConvertStringAndMinusZero),
          index, limit, effect, control);

      // Grow {elements} backing store if necessary.
      GrowFastElementsMode mode =
          IsDoubleElementsKind(elements_kind)
              ? GrowFastElementsMode::kDoubleElements
              : GrowFastElementsMode::kSmiOrObjectElements;
      elements = effect = graph()->NewNode(
          simplified()->MaybeGrowFastElements(mode, FeedbackSource()), receiver,
          elements, index, elements_length, effect, control);

      // If we didn't grow {elements}, it might still be COW, in which case we
      // copy it now.
      if (IsSmiOrObjectElementsKind(elements_kind) &&
          keyed_mode.store_mode() == KeyedAccessStoreMode::kGrowAndHandleCOW) {
        elements = effect =
            graph()->NewNode(simplified()->EnsureWritableFastElements(),
                             receiver, elements, effect, control);
      }

      // Also update the "length" property if {receiver} is a JSArray.
      if (receiver_is_jsarray) {
        Node* check =
            graph()->NewNode(simplified()->NumberLessThan(), index, length);
        Node* branch = graph()->NewNode(common()->Branch(), check, control);

        Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
        Node* etrue = effect;
        {
          // We don't need to do anything, the {index} is within
          // the valid bounds for the JSArray {receiver}.
        }

        Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
        Node* efalse = effect;
        {
          // Update the JSArray::length field. Since this is observable,
          // there must be no other check after this.
          Node* new_length = graph()->NewNode(simplified()->NumberAdd(), index,
                                              jsgraph()->OneConstant());
          efalse = graph()->NewNode(
              simplified()->StoreField(
                  AccessBuilder::ForJSArrayLength(elements_kind)),
              receiver, new_length, efalse, if_false);
        }

        control = graph()->NewNode(common()->Merge(2), if_true, if_false);
        effect =
            graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
      }
    }

    // Perform the actual element access.
    effect = graph()->NewNode(simplified()->StoreElement(element_access),
                              elements, index, value, effect, control);
  }

  return ValueEffectControl(value, effect, control);
}

JSNativeContextSpecialization::ValueEffectControl
JSNativeContextSpecialization::
    BuildElementAccessForTypedArrayOrRabGsabTypedArray(
        Node* receiver, Node* index, Node* value, Node* effect, Node* control,
        Node* context, ElementsKind elements_kind,
        KeyedAccessMode const& keyed_mode) {
  DCHECK(IsTypedArrayElementsKind(elements_kind) ||
         IsRabGsabTypedArrayElementsKind(elements_kind));
  // AccessMode::kDefine is not handled here. Optimization should be skipped by
  // caller.
  DCHECK(keyed_mode.access_mode() != AccessMode::kDefine);

  Node* buffer_or_receiver = receiver;
  Node* length;
  Node* base_pointer;
  Node* external_pointer;

  // Check if we can constant-fold information about the {receiver} (e.g.
  // for asm.js-like code patterns).
  OptionalJSTypedArrayRef typed_array =
      GetTypedArrayConstant(broker(), receiver);
  if (typed_array.has_value() &&
      // TODO(v8:11111): Add support for rab/gsab here.
      !IsRabGsabTypedArrayElementsKind(elements_kind)) {
    if (typed_array->map(broker()).elements_kind() != elements_kind) {
      // This case should never be reachable at runtime.
      JSGraphAssembler assembler(broker(), jsgraph_, zone(),
                                 BranchSemantics::kJS,
                                 [this](Node* n) { this->Revisit(n); });
      assembler.InitializeEffectControl(effect, control);
      assembler.Unreachable();
      ReleaseEffectAndControlFromAssembler(&assembler);
      Node* dead = jsgraph_->Dead();
      return ValueEffectControl{dead, dead, dead};
    } else {
      length =
          jsgraph()->ConstantNoHole(static_cast<double>(typed_array->length()));

      DCHECK(!typed_array->is_on_heap());
      // Load the (known) data pointer for the {receiver} and set
      // {base_pointer} and {external_pointer} to the values that will allow
      // to generate typed element accesses using the known data pointer. The
      // data pointer might be invalid if the {buffer} was detached, so we
      // need to make sure that any access is properly guarded.
      base_pointer = jsgraph()->ZeroConstant();
      external_pointer = jsgraph()->PointerConstant(typed_array->data_ptr());
    }
  } else {
    // Load the {receiver}s length.
    JSGraphAssembler assembler(broker(), jsgraph_, zone(), BranchSemantics::kJS,
                               [this](Node* n) { this->Revisit(n); });
    assembler.InitializeEffectControl(effect, control);
    length = assembler.TypedArrayLength(
        TNode<JSTypedArray>::UncheckedCast(receiver), {elements_kind},
        TNode<Context>::UncheckedCast(context));
    std::tie(effect, control) =
        ReleaseEffectAndControlFromAssembler(&assembler);

    // Load the base pointer for the {receiver}. This will always be Smi
    // zero unless we allow on-heap TypedArrays, which is only the case
    // for Chrome. Node and Electron both set this limit to 0. Setting
    // the base to Smi zero here allows the EffectControlLinearizer to
    // optimize away the tricky part of the access later.
    if (JSTypedArray::kMaxSizeInHeap == 0) {
      base_pointer = jsgraph()->ZeroConstant();
    } else {
      base_pointer = effect = graph()->NewNode(
          simplified()->LoadField(AccessBuilder::ForJSTypedArrayBasePointer()),
          receiver, effect, control);
    }

    // Load the external pointer for the {receiver}.
    external_pointer = effect =
        graph()->NewNode(simplified()->LoadField(
                             AccessBuilder::ForJSTypedArrayExternalPointer()),
                         receiver, effect, control);
  }

  // See if we can skip the detaching check.
  if (!dependencies()->DependOnArrayBufferDetachingProtector()) {
    // Load the buffer for the {receiver}.
    Node* buffer =
        typed_array.has_value()
            ? jsgraph()->ConstantNoHole(typed_array->buffer(broker()), broker())
            : (effect = graph()->NewNode(
                   simplified()->LoadField(
                       AccessBuilder::ForJSArrayBufferViewBuffer()),
                   receiver, effect, control));

    // Deopt if the {buffer} was detached.
    // Note: A detached buffer leads to megamorphic feedback.
    Node* buffer_bit_field = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForJSArrayBufferBitField()),
        buffer, effect, control);
    Node* check = graph()->NewNode(
        simplified()->NumberEqual(),
        graph()->NewNode(
            simplified()->NumberBitwiseAnd(), buffer_bit_field,
            jsgraph()->ConstantNoHole(JSArrayBuffer::WasDetachedBit::kMask)),
        jsgraph()->ZeroConstant());
    effect = graph()->NewNode(
        simplified()->CheckIf(DeoptimizeReason::kArrayBufferWasDetached), check,
        effect, control);

    // Retain the {buffer} instead of {receiver} to reduce live ranges.
    buffer_or_receiver = buffer;
  }

  enum Situation { kBoundsCheckDone, kHandleOOB_SmiAndRangeCheckComputed };
  Situation situation;
  TNode<BoolT> check;
  if ((keyed_mode.IsLoad() && LoadModeHandlesOOB(keyed_mode.load_mode())) ||
      (keyed_mode.IsStore() &&
       StoreModeIgnoresTypeArrayOOB(keyed_mode.store_mode()))) {
    // Only check that the {index} is in SignedSmall range. We do the actual
    // bounds check below and just skip the property access if it's out of
    // bounds for the {receiver}.
    index = effect = graph()->NewNode(simplified()->CheckSmi(FeedbackSource()),
                                      index, effect, control);
    TNode<Boolean> compare_length = TNode<Boolean>::UncheckedCast(
        graph()->NewNode(simplified()->NumberLessThan(), index, length));

    JSGraphAssembler assembler(broker(), jsgraph_, zone(), BranchSemantics::kJS,
                               [this](Node* n) { this->Revisit(n); });
    assembler.InitializeEffectControl(effect, control);
    TNode<BoolT> check_less_than_length =
        assembler.EnterMachineGraph<BoolT>(compare_length, UseInfo::Bool());
    TNode<Int32T> index_int32 = assembler.EnterMachineGraph<Int32T>(
        TNode<Smi>::UncheckedCast(index), UseInfo::TruncatingWord32());
    TNode<BoolT> check_non_negative =
        assembler.Int32LessThanOrEqual(assembler.Int32Constant(0), index_int32);
    check = TNode<BoolT>::UncheckedCast(
        assembler.Word32And(check_less_than_length, check_non_negative));
    std::tie(effect, control) =
        ReleaseEffectAndControlFromAssembler(&assembler);

    situation = kHandleOOB_SmiAndRangeCheckComputed;
  } else {
    // Check that the {index} is in the valid range for the {receiver}.
    index = effect = graph()->NewNode(
        simplified()->CheckBounds(FeedbackSource(),
                                  CheckBoundsFlag::kConvertStringAndMinusZero),
        index, length, effect, control);
    situation = kBoundsCheckDone;
  }

  // Access the actual element.
  ExternalArrayType external_array_type =
      GetArrayTypeFromElementsKind(elements_kind);
  DCHECK_NE(external_array_type, ExternalArrayType::kExternalFloat16Array);
  switch (keyed_mode.access_mode()) {
    case AccessMode::kLoad: {
      // Check if we can return undefined for out-of-bounds loads.
      if (situation == kHandleOOB_SmiAndRangeCheckComputed) {
        DCHECK_NE(check, nullptr);
        Node* branch = graph()->NewNode(
            common()->Branch(BranchHint::kTrue, BranchSemantics::kMachine),
            check, control);

        Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
        Node* etrue = effect;
        Node* vtrue;
        {
          // Do a real bounds check against {length}. This is in order to
          // protect against a potential typer bug leading to the elimination
          // of the NumberLessThan above.
          if (v8_flags.turbo_typer_hardening) {
            index = etrue = graph()->NewNode(
                simplified()->CheckBounds(
                    FeedbackSource(),
                    CheckBoundsFlag::kConvertStringAndMinusZero |
                        CheckBoundsFlag::kAbortOnOutOfBounds),
                index, length, etrue, if_true);
          }

          // Perform the actual load
          vtrue = etrue = graph()->NewNode(
              simplified()->LoadTypedElement(external_array_type),
              buffer_or_receiver, base_pointer, external_pointer, index, etrue,
              if_true);
        }

        Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
        Node* efalse = effect;
        Node* vfalse;
        {
          // Materialize undefined for out-of-bounds loads.
          vfalse = jsgraph()->UndefinedConstant();
        }

        control = graph()->NewNode(common()->Merge(2), if_true, if_false);
        effect =
            graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
        value =
            graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                             vtrue, vfalse, control);
      } else {
        // Perform the actual load.
        DCHECK_EQ(kBoundsCheckDone, situation);
        value = effect = graph()->NewNode(
            simplified()->LoadTypedElement(external_array_type),
            buffer_or_receiver, base_pointer, external_pointer, index, effect,
            control);
      }
      break;
    }
    case AccessMode::kStoreInLiteral:
    case AccessMode::kDefine:
      UNREACHABLE();
    case AccessMode::kStore: {
      if (external_array_type == kExternalBigInt64Array ||
          external_array_type == kExternalBigUint64Array) {
        value = effect = graph()->NewNode(
            simplified()->SpeculativeToBigInt(BigIntOperationHint::kBigInt,
                                              FeedbackSource()),
            value, effect, control);
      } else {
        // Ensure that the {value} is actually a Number or an Oddball,
        // and truncate it to a Number appropriately.
        // TODO(panq): Eliminate the deopt loop introduced by the speculation.
        value = effect = graph()->NewNode(
            simplified()->SpeculativeToNumber(
                NumberOperationHint::kNumberOrOddball, FeedbackSource()),
            value, effect, control);
      }

      // Introduce the appropriate truncation for {value}. Currently we
      // only need to do this for ClamedUint8Array {receiver}s, as the
      // other truncations are implicit in the StoreTypedElement, but we
      // might want to change that at some point.
      if (external_array_type == kExternalUint8ClampedArray) {
        value = graph()->NewNode(simplified()->NumberToUint8Clamped(), value);
      }

      if (situation == kHandleOOB_SmiAndRangeCheckComputed) {
        // We have to detect OOB stores and handle them without deopt (by
        // simply not performing them).
        DCHECK_NE(check, nullptr);
        Node* branch = graph()->NewNode(
            common()->Branch(BranchHint::kTrue, BranchSemantics::kMachine),
            check, control);

        Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
        Node* etrue = effect;
        {
          // Do a real bounds check against {length}. This is in order to
          // protect against a potential typer bug leading to the elimination
          // of the NumberLessThan above.
          if (v8_flags.turbo_typer_hardening) {
            index = etrue = graph()->NewNode(
                simplified()->CheckBounds(
                    FeedbackSource(),
                    CheckBoundsFlag::kConvertStringAndMinusZero |
                        CheckBoundsFlag::kAbortOnOutOfBounds),
                index, length, etrue, if_true);
          }

          // Perform the actual store.
          etrue = graph()->NewNode(
              simplified()->StoreTypedElement(external_array_type),
              buffer_or_receiver, base_pointer, external_pointer, index, value,
              etrue, if_true);
        }

        Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
        Node* efalse = effect;
        {
          // Just ignore the out-of-bounds write.
        }

        control = graph()->NewNode(common()->Merge(2), if_true, if_false);
        effect =
            graph()->NewNode(common()->EffectPhi(2), etrue, efalse, control);
      } else {
        // Perform the actual store
        DCHECK_EQ(kBoundsCheckDone, situation);
        effect = graph()->NewNode(
            simplified()->StoreTypedElement(external_array_type),
            buffer_or_receiver, base_pointer, external_pointer, index, value,
            effect, control);
      }
      break;
    }
    case AccessMode::kHas:
      if (situation == kHandleOOB_SmiAndRangeCheckComputed) {
        DCHECK_NE(check, nullptr);
        JSGraphAssembler assembler(broker(), jsgraph_, zone(),
                                   BranchSemantics::kJS,
                                   [this](Node* n) { this->Revisit(n); });
        assembler.InitializeEffectControl(effect, control);
        value = assembler.MachineSelectIf<Boolean>(check)
                    .Then([&]() { return assembler.TrueConstant(); })
                    .Else([&]() { return assembler.FalseConstant(); })
                    .ExpectTrue()
                    .Value();
        std::tie(effect, control) =
            ReleaseEffectAndControlFromAssembler(&assembler);
      } else {
        DCHECK_EQ(kBoundsCheckDone, situation);
        // For has-property on a typed array, all we need is a bounds check.
        value = jsgraph()->TrueConstant();
      }
      break;
  }

  return ValueEffectControl(value, effect, control);
}

Node* JSNativeContextSpecialization::BuildIndexedStringLoad(
    Node* receiver, Node* index, Node* length, Node** effect, Node** control,
    KeyedAccessLoadMode load_mode) {
  if (LoadModeHandlesOOB(load_mode) &&
      dependencies()->DependOnNoElementsProtector()) {
    // Ensure that the {index} is a valid String length.
    index = *effect = graph()->NewNode(
        simplified()->CheckBounds(FeedbackSource(),
                                  CheckBoundsFlag::kConvertStringAndMinusZero),
        index, jsgraph()->ConstantNoHole(String::kMaxLength), *effect,
        *control);

    // Load the single character string from {receiver} or yield
    // undefined if the {index} is not within the valid bounds.
    Node* check =
        graph()->NewNode(simplified()->NumberLessThan(), index, length);
    Node* branch =
        graph()->NewNode(common()->Branch(BranchHint::kTrue), check, *control);

    Node* if_true = graph()->NewNode(common()->IfTrue(), branch);
    // Do a real bounds check against {length}. This is in order to protect
    // against a potential typer bug leading to the elimination of the
    // NumberLessThan above.
    Node* etrue = *effect;
    if (v8_flags.turbo_typer_hardening) {
      etrue = index = graph()->NewNode(
          simplified()->CheckBounds(
              FeedbackSource(), CheckBoundsFlag::kConvertStringAndMinusZero |
                                    CheckBoundsFlag::kAbortOnOutOfBounds),
          index, length, etrue, if_true);
    }
    Node* vtrue = etrue = graph()->NewNode(simplified()->StringCharCodeAt(),
                                           receiver, index, etrue, if_true);
    vtrue = graph()->NewNode(simplified()->StringFromSingleCharCode(), vtrue);

    Node* if_false = graph()->NewNode(common()->IfFalse(), branch);
    Node* vfalse = jsgraph()->UndefinedConstant();

    *control = graph()->NewNode(common()->Merge(2), if_true, if_false);
    *effect =
        graph()->NewNode(common()->EffectPhi(2), etrue, *effect, *control);
    return graph()->NewNode(common()->Phi(MachineRepresentation::kTagged, 2),
                            vtrue, vfalse, *control);
  } else {
    // Ensure that {index} is less than {receiver} length.
    index = *effect = graph()->NewNode(
        simplified()->CheckBounds(FeedbackSource(),
                                  CheckBoundsFlag::kConvertStringAndMinusZero),
        index, length, *effect, *control);

    // Return the character from the {receiver} as single character string.
    Node* value = *effect = graph()->NewNode(
        simplified()->StringCharCodeAt(), receiver, index, *effect, *control);
    value = graph()->NewNode(simplified()->StringFromSingleCharCode(), value);
    return value;
  }
}

Node* JSNativeContextSpecialization::BuildExtendPropertiesBackingStore(
    MapRef map, Node* properties, Node* effect, Node* control) {
  // TODO(bmeurer/jkummerow): Property deletions can undo map transitions
  // while keeping the backing store around, meaning that even though the
  // map might believe that objects have no unused property fields, there
  // might actually be some. It would be nice to not create a new backing
  // store in that case (i.e. when properties->length() >= new_length).
  // However, introducing branches and Phi nodes here would make it more
  // difficult for escape analysis to get rid of the backing stores used
  // for intermediate states of chains of property additions. That makes
  // it unclear what the best approach is here.
  DCHECK_EQ(map.UnusedPropertyFields(), 0);
  int length = map.NextFreePropertyIndex() - map.GetInObjectProperties();
  // Under normal circumstances, NextFreePropertyIndex() will always be larger
  // than GetInObjectProperties(). However, an attacker able to corrupt heap
  // memory can break this invariant, in which case we'll get confused here,
  // potentially causing a sandbox violation. This CHECK defends against that.
  SBXCHECK_GE(length, 0);
  int new_length = length + JSObject::kFieldsAdded;
  // Collect the field values from the {properties}.
  ZoneVector<Node*> values(zone());
  values.reserve(new_length);
  for (int i = 0; i < length; ++i) {
    Node* value = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForFixedArraySlot(i)),
        properties, effect, control);
    values.push_back(value);
  }
  // Initialize the new fields to undefined.
  for (int i = 0; i < JSObject::kFieldsAdded; ++i) {
    values.push_back(jsgraph()->UndefinedConstant());
  }

  // Compute new length and hash.
  Node* hash;
  if (length == 0) {
    hash = graph()->NewNode(
        common()->Select(MachineRepresentation::kTaggedSigned),
        graph()->NewNode(simplified()->ObjectIsSmi(), properties), properties,
        jsgraph()->SmiConstant(PropertyArray::kNoHashSentinel));
    hash = effect = graph()->NewNode(common()->TypeGuard(Type::SignedSmall()),
                                     hash, effect, control);
    hash = graph()->NewNode(
        simplified()->NumberShiftLeft(), hash,
        jsgraph()->ConstantNoHole(PropertyArray::HashField::kShift));
  } else {
    hash = effect = graph()->NewNode(
        simplified()->LoadField(AccessBuilder::ForPropertyArrayLengthAndHash()),
        properties, effect, control);
    hash = graph()->NewNode(
        simplified()->NumberBitwiseAnd(), hash,
        jsgraph()->ConstantNoHole(PropertyArray::HashField::kMask));
  }
  Node* new_length_and_hash =
      graph()->NewNode(simplified()->NumberBitwiseOr(),
                       jsgraph()->ConstantNoHole(new_length), hash);
  // TDOO(jarin): Fix the typer to infer tighter bound for NumberBitwiseOr.
  new_length_and_hash = effect =
      graph()->NewNode(common()->TypeGuard(Type::SignedSmall()),
                       new_length_and_hash, effect, control);

  // Allocate and initialize the new properties.
  AllocationBuilder a(jsgraph(), broker(), effect, control);
  a.Allocate(PropertyArray::SizeFor(new_length), AllocationType::kYoung,
             Type::OtherInternal());
  a.Store(AccessBuilder::ForMap(), jsgraph()->PropertyArrayMapConstant());
  a.Store(AccessBuilder::ForPropertyArrayLengthAndHash(), new_length_and_hash);
  for (int i = 0; i < new_length; ++i) {
    a.Store(AccessBuilder::ForFixedArraySlot(i), values[i]);
  }
  return a.Finish();
}

Node* JSNativeContextSpecialization::BuildCheckEqualsName(NameRef name,
                                                          Node* value,
                                                          Node* effect,
                                                          Node* control) {
  DCHECK(name.IsUniqueName());
  Operator const* const op =
      name.IsSymbol() ? simplified()->CheckEqualsSymbol()
                      : simplified()->CheckEqualsInternalizedString();
  return graph()->NewNode(op, jsgraph()->ConstantNoHole(name, broker()), value,
                          effect, control);
}

bool JSNativeContextSpecialization::CanTreatHoleAsUndefined(
    ZoneVector<MapRef> const& receiver_maps) {
  // Check if all {receiver_maps} have one of the initial Array.prototype
  // or Object.prototype objects as their prototype (in any of the current
  // native contexts, as the global Array protector works isolate-wide).
  for (MapRef receiver_map : receiver_maps) {
    ObjectRef receiver_prototype = receiver_map.prototype(broker());
    if (!receiver_prototype.IsJSObject() ||
        !broker()->IsArrayOrObjectPrototype(receiver_prototype.AsJSObject())) {
      return false;
    }
  }

  // Check if the array prototype chain is intact.
  return dependencies()->DependOnNoElementsProtector();
}

bool JSNativeContextSpecialization::InferMaps(Node* object, Effect effect,
                                              ZoneVector<MapRef>* maps) const {
  ZoneRefSet<Map> map_set;
  NodeProperties::InferMapsResult result =
      NodeProperties::InferMapsUnsafe(broker(), object, effect, &map_set);
  if (result == NodeProperties::kReliableMaps) {
    for (MapRef map : map_set) {
      maps->push_back(map);
    }
    return true;
  } else if (result == NodeProperties::kUnreliableMaps) {
    // For untrusted maps, we can still use the information
    // if the maps are stable.
    for (MapRef map : map_set) {
      if (!map.is_stable()) return false;
    }
    for (MapRef map : map_set) {
      maps->push_back(map);
    }
    return true;
  }
  return false;
}

OptionalMapRef JSNativeContextSpecialization::InferRootMap(Node* object) const {
  HeapObjectMatcher m(object);
  if (m.HasResolvedValue()) {
    MapRef map = m.Ref(broker()).map(broker());
    return map.FindRootMap(broker());
  } else if (m.IsJSCreate()) {
    OptionalMapRef initial_map =
        NodeProperties::GetJSCreateMap(broker(), object);
    if (initial_map.has_value()) {
      DCHECK(initial_map->equals(initial_map->FindRootMap(broker())));
      return *initial_map;
    }
  }
  return std::nullopt;
}

Node* JSNativeContextSpecialization::BuildLoadPrototypeFromObject(
    Node* object, Node* effect, Node* control) {
  Node* map = effect =
      graph()->NewNode(simplified()->LoadField(AccessBuilder::ForMap()), object,
                       effect, control);
  return graph()->NewNode(
      simplified()->LoadField(AccessBuilder::ForMapPrototype()), map, effect,
      control);
}

std::pair<Node*, Node*>
JSNativeContextSpecialization::ReleaseEffectAndControlFromAssembler(
    JSGraphAssembler* gasm) {
  auto catch_scope = gasm->catch_scope();
  DCHECK(catch_scope->is_outermost());

  if (catch_scope->has_handler() &&
      catch_scope->has_exceptional_control_flow()) {
    TNode<Object> handler_exception;
    Effect handler_effect{nullptr};
    Control handler_control{nullptr};
    gasm->catch_scope()->MergeExceptionalPaths(
        &handler_exception, &handler_effect, &handler_control);

    ReplaceWithValue(gasm->outermost_handler(), handler_exception,
                     handler_effect, handler_control);
  }

  return {gasm->effect(), gasm->control()};
}

Graph* JSNativeContextSpecialization::graph() const {
  return jsgraph()->graph();
}

Isolate* JSNativeContextSpecialization::isolate() const {
  return jsgraph()->isolate();
}

Factory* JSNativeContextSpecialization::factory() const {
  return isolate()->factory();
}

CommonOperatorBuilder* JSNativeContextSpecialization::common() const {
  return jsgraph()->common();
}

JSOperatorBuilder* JSNativeContextSpecialization::javascript() const {
  return jsgraph()->javascript();
}

SimplifiedOperatorBuilder* JSNativeContextSpecialization::simplified() const {
  return jsgraph()->simplified();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
