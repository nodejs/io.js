// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/execution/isolate.h"
#include "src/handles/maybe-handles.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/js-objects.h"
#include "src/objects/js-promise-inl.h"
#include "src/objects/js-promise.h"
#include "src/objects/objects.h"
#include "src/roots/roots.h"

namespace v8 {
namespace internal {

BUILTIN(AsyncDisposableStackOnFulfilled) {
  HandleScope scope(isolate);

  DirectHandle<JSDisposableStackBase> stack(
      Cast<JSDisposableStackBase>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposableStackContextSlots::kStack))),
      isolate);
  Handle<JSPromise> promise(
      Cast<JSPromise>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposableStackContextSlots::
              kOuterPromise))),
      isolate);

  MAYBE_RETURN(JSAsyncDisposableStack::NextDisposeAsyncIteration(isolate, stack,
                                                                 promise),
               ReadOnlyRoots(isolate).exception());
  return ReadOnlyRoots(isolate).undefined_value();
}

BUILTIN(AsyncDisposableStackOnRejected) {
  HandleScope scope(isolate);

  Handle<JSDisposableStackBase> stack(
      Cast<JSDisposableStackBase>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposableStackContextSlots::kStack))),
      isolate);
  Handle<JSPromise> promise(
      Cast<JSPromise>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposableStackContextSlots::
              kOuterPromise))),
      isolate);

  Handle<Object> rejection_error = args.at(1);
  DCHECK(isolate->is_catchable_by_javascript(*rejection_error));
  JSDisposableStackBase::HandleErrorInDisposal(isolate, stack, rejection_error);

  MAYBE_RETURN(JSAsyncDisposableStack::NextDisposeAsyncIteration(isolate, stack,
                                                                 promise),
               ReadOnlyRoots(isolate).exception());
  return ReadOnlyRoots(isolate).undefined_value();
}

// Part of
// https://tc39.es/proposal-explicit-resource-management/#sec-getdisposemethod
BUILTIN(AsyncDisposeFromSyncDispose) {
  HandleScope scope(isolate);
  // 1. If hint is async-dispose
  //   b. If GetMethod(V, @@asyncDispose) is undefined,
  //    i. If GetMethod(V, @@dispose) is not undefined, then
  //      1. Let closure be a new Abstract Closure with no parameters that
  //      captures method and performs the following steps when called:
  //        a. Let O be the this value.
  //        b. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> promise = isolate->factory()->NewJSPromise();

  //        c. Let result be Completion(Call(method, O)).
  Handle<JSFunction> sync_method = Handle<JSFunction>(
      Cast<JSFunction>(isolate->context()->get(static_cast<int>(
          JSDisposableStackBase::AsyncDisposeFromSyncDisposeContextSlots::
              kMethod))),
      isolate);

  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  try_catch.SetVerbose(false);
  try_catch.SetCaptureMessage(false);

  MaybeHandle<Object> result = Execution::Call(
      isolate, sync_method, ReadOnlyRoots(isolate).undefined_value_handle(), 0,
      nullptr);

  Handle<Object> result_handle;

  if (result.ToHandle(&result_handle)) {
    //        e. Perform ? Call(promiseCapability.[[Resolve]], undefined, «
    //        undefined »).
    JSPromise::Resolve(promise, result_handle).ToHandleChecked();
  } else {
    Tagged<Object> exception = isolate->exception();
    if (!isolate->is_catchable_by_javascript(exception)) {
      return {};
    }
    //        d. IfAbruptRejectPromise(result, promiseCapability).
    DCHECK(try_catch.HasCaught());
    JSPromise::Reject(promise, handle(exception, isolate));
  }

  //        f. Return promiseCapability.[[Promise]].
  return *promise;
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack
BUILTIN(AsyncDisposableStackConstructor) {
  const char kMethodName[] = "AsyncDisposableStack";
  HandleScope scope(isolate);

  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (!IsJSReceiver(*args.new_target(), isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  kMethodName)));
  }

  // 2. Let asyncDisposableStack be ? OrdinaryCreateFromConstructor(NewTarget,
  //    "%AsyncDisposableStack.prototype%", « [[AsyncDisposableState]],
  //    [[DisposeCapability]] »).
  DirectHandle<Map> map;
  Handle<JSFunction> target = args.target();
  Handle<JSReceiver> new_target = Cast<JSReceiver>(args.new_target());

  DCHECK_EQ(*target,
            target->native_context()->js_async_disposable_stack_function());

  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, map, JSFunction::GetDerivedMap(isolate, target, new_target));

  DirectHandle<JSAsyncDisposableStack> async_disposable_stack =
      isolate->factory()->NewJSAsyncDisposableStack(map);
  // 3. Set asyncDisposableStack.[[AsyncDisposableState]] to pending.
  // 4. Set asyncDisposableStack.[[DisposeCapability]] to
  // NewDisposeCapability().
  JSDisposableStackBase::InitializeJSDisposableStackBase(
      isolate, async_disposable_stack);
  // 5. Return asyncDisposableStack.
  return *async_disposable_stack;
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack.prototype.use
BUILTIN(AsyncDisposableStackPrototypeUse) {
  const char kMethodName[] = "AsyncDisposableStack.prototype.use";
  HandleScope scope(isolate);

  // 1. Let asyncDisposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(asyncDisposableStack,
  // [[AsyncDisposableState]]).
  CHECK_RECEIVER(JSAsyncDisposableStack, async_disposable_stack, kMethodName);
  Handle<Object> value = args.at(1);

  // 3. If asyncDisposableStack.[[AsyncDisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (async_disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. Perform ?
  // AddDisposableResource(asyncDisposableStack.[[DisposeCapability]],
  // value, async-dispose).
  Handle<Object> method;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, method,
      JSDisposableStackBase::CheckValueAndGetDisposeMethod(
          isolate, value, DisposeMethodHint::kAsyncDispose));

  JSDisposableStackBase::Add(
      isolate, async_disposable_stack,
      (IsNullOrUndefined(*value)
           ? ReadOnlyRoots(isolate).undefined_value_handle()
           : value),
      method, DisposeMethodCallType::kValueIsReceiver,
      DisposeMethodHint::kAsyncDispose);

  // 5. Return value.
  return *value;
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack.prototype.disposeAsync
BUILTIN(AsyncDisposableStackPrototypeDisposeAsync) {
  HandleScope scope(isolate);

  // 1. Let asyncDisposableStack be the this value.
  Handle<Object> receiver = args.receiver();

  // 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Handle<JSPromise> promise = isolate->factory()->NewJSPromise();

  // 3. If asyncDisposableStack does not have an [[AsyncDisposableState]]
  // internal slot, then
  if (!IsJSAsyncDisposableStack(*receiver)) {
    //    a. Perform ! Call(promiseCapability.[[Reject]], undefined, « a newly
    //    created TypeError object »).
    JSPromise::Reject(promise,
                      isolate->factory()->NewTypeError(
                          MessageTemplate::kNotAnAsyncDisposableStack));
    //   b. Return promiseCapability.[[Promise]].
    return *promise;
  }

  Handle<JSAsyncDisposableStack> async_disposable_stack =
      Cast<JSAsyncDisposableStack>(receiver);

  // 4. If asyncDisposableStack.[[AsyncDisposableState]] is disposed, then
  if (async_disposable_stack->state() == DisposableStackState::kDisposed) {
    //    a. Perform ! Call(promiseCapability.[[Resolve]], undefined, «
    //    undefined »).
    JSPromise::Resolve(
        promise, handle(ReadOnlyRoots(isolate).undefined_value(), isolate))
        .ToHandleChecked();
    //    b. Return promiseCapability.[[Promise]].
    return *promise;
  }

  // 5. Set asyncDisposableStack.[[AsyncDisposableState]] to disposed.
  async_disposable_stack->set_state(DisposableStackState::kDisposed);

  // 6. Let result be
  //   DisposeResources(asyncDisposableStack.[[DisposeCapability]],
  //   NormalCompletion(undefined)).
  // 7. IfAbruptRejectPromise(result, promiseCapability).
  // 8. Perform ! Call(promiseCapability.[[Resolve]], undefined, « result
  // »).
  // 9. Return promiseCapability.[[Promise]].
  MAYBE_RETURN(JSAsyncDisposableStack::NextDisposeAsyncIteration(
                   isolate, async_disposable_stack, promise),
               ReadOnlyRoots(isolate).exception());
  return *promise;
}

// https://tc39.es/proposal-explicit-resource-management/#sec-get-asyncdisposablestack.prototype.disposed
BUILTIN(AsyncDisposableStackPrototypeGetDisposed) {
  const char kMethodName[] = "get AsyncDisposableStack.prototype.disposed";
  HandleScope scope(isolate);

  // 1. Let AsyncdisposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(asyncDisposableStack,
  // [[AsyncDisposableState]]).
  CHECK_RECEIVER(JSAsyncDisposableStack, async_disposable_stack, kMethodName);

  // 3. If AsyncdisposableStack.[[AsyncDisposableState]] is disposed, return
  // true.
  // 4. Otherwise, return false.
  return *(isolate->factory()->ToBoolean(async_disposable_stack->state() ==
                                         DisposableStackState::kDisposed));
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack.prototype.adopt
BUILTIN(AsyncDisposableStackPrototypeAdopt) {
  const char kMethodName[] = "AsyncDisposableStack.prototype.adopt";
  HandleScope scope(isolate);
  Handle<Object> value = args.at(1);
  Handle<Object> on_dispose_async = args.at(2);

  // 1. Let asyncDisposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(asyncDisposableStack,
  // [[AsyncDisposableState]]).
  CHECK_RECEIVER(JSAsyncDisposableStack, async_disposable_stack, kMethodName);

  // 3. If asyncDisposableStack.[[AsyncDisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (async_disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. If IsCallable(onDisposeAsync) is false, throw a TypeError exception.
  if (!IsCallable(*on_dispose_async)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, on_dispose_async));
  }

  // 5. Let closure be a new Abstract Closure with no parameters that captures
  //    value and onDisposeAsync and performs the following steps when called:
  //      a. Return ? Call(onDisposeAsync, undefined, « value »).
  // 6. Let F be CreateBuiltinFunction(closure, 0, "", « »).
  // 7. Perform ?
  // AddDisposableResource(asyncDisposableStack.[[DisposeCapability]],
  //    undefined, async-dispose, F).
  // Instead of creating an abstract closure and a function, we pass
  // DisposeMethodCallType::kArgument so at the time of disposal, the value will
  // be passed as the argument to the method.
  JSDisposableStackBase::Add(isolate, async_disposable_stack, value,
                             on_dispose_async,
                             DisposeMethodCallType::kValueIsArgument,
                             DisposeMethodHint::kAsyncDispose);

  // 8. Return value.
  return *value;
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack.prototype.defer
BUILTIN(AsyncDisposableStackPrototypeDefer) {
  const char kMethodName[] = "AsyncDisposableStack.prototype.defer";
  HandleScope scope(isolate);
  Handle<Object> on_dispose_async = args.at(1);

  // 1. Let asyncDisposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(asyncDisposableStack,
  // [[AsyncDisposableState]]).
  CHECK_RECEIVER(JSAsyncDisposableStack, async_disposable_stack, kMethodName);

  // 3. If asyncDisposableStack.[[AsyncDisposableState]] is disposed, throw a
  // ReferenceError exception.
  if (async_disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. If IsCallable(onDisposeAsync) is false, throw a TypeError exception.
  if (!IsCallable(*on_dispose_async)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, on_dispose_async));
  }

  // 5. Perform ?
  // AddDisposableResource(asyncDisposableStack.[[DisposeCapability]],
  // undefined, async-dispose, onDisposeAsync).
  JSDisposableStackBase::Add(isolate, async_disposable_stack,
                             ReadOnlyRoots(isolate).undefined_value_handle(),
                             on_dispose_async,
                             DisposeMethodCallType::kValueIsReceiver,
                             DisposeMethodHint::kAsyncDispose);

  // 6. Return undefined.
  return ReadOnlyRoots(isolate).undefined_value();
}

// https://tc39.es/proposal-explicit-resource-management/#sec-asyncdisposablestack.prototype.move
BUILTIN(AsyncDisposableStackPrototypeMove) {
  const char kMethodName[] = "AsyncDisposableStack.prototype.move";
  HandleScope scope(isolate);

  // 1. Let asyncDisposableStack be the this value.
  // 2. Perform ? RequireInternalSlot(asyncDisposableStack,
  // [[AsyncDisposableState]]).
  CHECK_RECEIVER(JSAsyncDisposableStack, async_disposable_stack, kMethodName);

  // 3. If asyncDisposableStack.[[AsyncDisposableState]] is disposed, throw a
  //    ReferenceError exception.
  if (async_disposable_stack->state() == DisposableStackState::kDisposed) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewReferenceError(
            MessageTemplate::kDisposableStackIsDisposed,
            isolate->factory()->NewStringFromAsciiChecked(kMethodName)));
  }

  // 4. Let newAsyncDisposableStack be ?
  //    OrdinaryCreateFromConstructor(%AsyncDisposableStack%,
  //    "%AsyncDisposableStack.prototype%", « [[AsyncDisposableState]],
  //     [[DisposeCapability]] »).
  // 5. Set newAsyncDisposableStack.[[AsyncDisposableState]] to pending.

  Tagged<JSFunction> constructor_function =
      Cast<JSFunction>(isolate->native_context()->get(
          Context::JS_ASYNC_DISPOSABLE_STACK_FUNCTION_INDEX));
  DirectHandle<Map> map(constructor_function->initial_map(), isolate);

  DirectHandle<JSAsyncDisposableStack> new_async_disposable_stack =
      isolate->factory()->NewJSAsyncDisposableStack(map);

  // 6. Set newAsyncDisposableStack.[[DisposeCapability]] to
  //    asyncDisposableStack.[[DisposeCapability]].
  new_async_disposable_stack->set_stack(async_disposable_stack->stack());
  new_async_disposable_stack->set_length(async_disposable_stack->length());
  new_async_disposable_stack->set_state(DisposableStackState::kPending);
  new_async_disposable_stack->set_error(
      *(isolate->factory()->uninitialized_value()));

  // 7. Set asyncDisposableStack.[[DisposeCapability]] to
  // NewDisposeCapability().
  async_disposable_stack->set_stack(ReadOnlyRoots(isolate).empty_fixed_array());
  async_disposable_stack->set_length(0);
  async_disposable_stack->set_error(
      *(isolate->factory()->uninitialized_value()));

  // 8. Set disposableStack.[[DisposableState]] to disposed.
  async_disposable_stack->set_state(DisposableStackState::kDisposed);

  // 9. Return newDisposableStack.
  return *new_async_disposable_stack;
}

}  // namespace internal
}  // namespace v8
