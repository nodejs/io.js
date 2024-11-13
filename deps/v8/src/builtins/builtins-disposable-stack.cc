// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/casting.h"
#include "src/objects/contexts.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/js-function.h"

namespace v8 {
namespace internal {

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack
BUILTIN(DisposableStackConstructor) {
  const char* const kMethodName = "DisposableStack";
  HandleScope scope(isolate);

  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*args.new_target(), isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 2. Let disposableStack be ? OrdinaryCreateFromConstructor(NewTarget,
  //    "%DisposableStack.prototype%", « [[DisposableState]],
  //    [[DisposeCapability]] »).
  DirectHandle<Map> map;
  Handle<JSFunction> target = args.target();
  Handle<JSReceiver> new_target = Cast<JSReceiver>(args.new_target());

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, map, JSFunction::GetDerivedMap(isolate, target, new_target));

  DirectHandle<JSSyncDisposableStack> disposable_stack =
      isolate->factory()->NewJSSyncDisposableStack(map);
  // 3. Set disposableStack.[[DisposableState]] to pending.
  // 4. Set disposableStack.[[DisposeCapability]] to NewDisposeCapability().
  JSDisposableStackBase::InitializeJSDisposableStackBase(isolate,
                                                         disposable_stack);
  // 5. Return disposableStack.
  return *disposable_stack;
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.use
BUILTIN(DisposableStackPrototypeUse) {
  const char* const kMethodName = "DisposableStack.prototype.use";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);
  Handle<Object> value = args.at(1);

  // use(value) does nothing when the value is null or undefined, so return
  // early.
  if (IsNullOrUndefined(*value)) {
    return *value;
  }

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  Handle<Object> method;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, method,
      JSDisposableStackBase::CheckValueAndGetDisposeMethod(
          isolate, value, DisposeMethodHint::kSyncDispose));

  // 4. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  //    value, sync-dispose).
  JSDisposableStackBase::Add(isolate, disposable_stack, value, method,
                             DisposeMethodCallType::kValueIsReceiver,
                             DisposeMethodHint::kSyncDispose);

  // 5. Return value.
  return *value;
}

BUILTIN(DisposableStackPrototypeDispose) {
  const char* const kMethodName = "DisposableStack.prototype.dispose";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, return undefined.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // 4. Set disposableStack.[[DisposableState]] to disposed.
  // Will be done in DisposeResources call.

  // 5. Return ? DisposeResources(disposableStack.[[DisposeCapability]],
  //    NormalCompletion(undefined)).
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      JSDisposableStackBase::DisposeResources(
          isolate, disposable_stack, MaybeHandle<Object>(),
          DisposableStackResourcesType::kAllSync));

  return *result;
}

BUILTIN(DisposableStackPrototypeGetDisposed) {
  const char* const kMethodName = "get DisposableStack.prototype.disposed";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, return true.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    return ReadOnlyRoots(isolate).true_value();
  }
  // 4. Otherwise, return false.
  return ReadOnlyRoots(isolate).false_value();
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.adopt
BUILTIN(DisposableStackPrototypeAdopt) {
  const char* const kMethodName = "DisposableStack.prototype.adopt";
  HandleScope scope(isolate);
  DirectHandle<Object> value = args.at(1);
  Handle<Object> on_dispose = args.at(2);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. If IsCallable(onDispose) is false, throw a TypeError exception.
  if (!IsCallable(*on_dispose)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, on_dispose));
  }

  // 5. Let closure be a new Abstract Closure with no parameters that captures
  //    value and onDispose and performs the following steps when called:
  //      a. Return ? Call(onDispose, undefined, « value »).
  // 6. Let F be CreateBuiltinFunction(closure, 0, "", « »).
  // 7. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  //    undefined, sync-dispose, F).
  // Instead of creating an abstract closure and a function, we pass
  // DisposeMethodCallType::kArgument so at the time of disposal, the value will
  // be passed as the argument to the method.
  JSDisposableStackBase::Add(isolate, disposable_stack, value, on_dispose,
                             DisposeMethodCallType::kValueIsArgument,
                             DisposeMethodHint::kSyncDispose);

  // 8. Return value.
  return *value;
}

// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-disposablestack.prototype.defer
BUILTIN(DisposableStackPrototypeDefer) {
  const char* const kMethodName = "DisposableStack.prototype.defer";
  HandleScope scope(isolate);
  Handle<Object> on_dispose = args.at(1);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  // ReferenceError exception.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. If IsCallable(onDispose) is false, throw a TypeError exception.
  if (!IsCallable(*on_dispose)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, on_dispose));
  }

  // 5. Perform ? AddDisposableResource(disposableStack.[[DisposeCapability]],
  // undefined, sync-dispose, onDispose).
  JSDisposableStackBase::Add(
      isolate, disposable_stack,
      ReadOnlyRoots(isolate).undefined_value_handle(), on_dispose,
      DisposeMethodCallType::kValueIsReceiver, DisposeMethodHint::kSyncDispose);

  // 6. Return undefined.
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(DisposableStackPrototypeMove) {
  const char* const kMethodName = "DisposableStack.prototype.move";
  HandleScope scope(isolate);

  // 1. Let disposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(disposableStack, [[DisposableState]]).
  CHECK_RECEIVER(JSSyncDisposableStack, disposable_stack, kMethodName);

  // 3. If disposableStack.[[DisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. Let newDisposableStack be ?
  //    OrdinaryCreateFromConstructor(%DisposableStack%,
  //    "%DisposableStack.prototype%", « [[DisposableState]],
  //     [[DisposeCapability]] »).
  // 5. Set newDisposableStack.[[DisposableState]] to pending.

  Tagged<JSFunction> constructor_function =
      Cast<JSFunction>(isolate->native_context()->get(
          Context::JS_DISPOSABLE_STACK_FUNCTION_INDEX));
  DirectHandle<Map> map(constructor_function->initial_map(), isolate);

  DirectHandle<JSSyncDisposableStack> new_disposable_stack =
      isolate->factory()->NewJSSyncDisposableStack(map);

  // 6. Set newDisposableStack.[[DisposeCapability]] to
  //    disposableStack.[[DisposeCapability]].
  new_disposable_stack->set_stack(disposable_stack->stack());
  new_disposable_stack->set_length(disposable_stack->length());
  new_disposable_stack->set_state(DisposableStackState::kPending);

  // 7. Set disposableStack.[[DisposeCapability]] to NewDisposeCapability().
  disposable_stack->set_stack(ReadOnlyRoots(isolate).empty_fixed_array());
  disposable_stack->set_length(0);

  // 8. Set disposableStack.[[DisposableState]] to disposed.
  disposable_stack->set_state(DisposableStackState::kDisposed);

  // 9. Return newDisposableStack.
  return *new_disposable_stack;
}

}  // namespace internal
}  // namespace v8
