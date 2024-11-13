// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_
#define V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_

#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/factory.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-disposable-stack-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSDisposableStackBase)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSSyncDisposableStack)
TQ_OBJECT_CONSTRUCTORS_IMPL(JSAsyncDisposableStack)

BIT_FIELD_ACCESSORS(JSDisposableStackBase, status, state,
                    JSDisposableStackBase::StateBit)
BIT_FIELD_ACCESSORS(JSDisposableStackBase, status, length,
                    JSDisposableStackBase::LengthBits)

inline void JSDisposableStackBase::Add(
    Isolate* isolate, DirectHandle<JSDisposableStackBase> disposable_stack,
    DirectHandle<Object> value, DirectHandle<Object> method,
    DisposeMethodCallType type, DisposeMethodHint hint) {
  DCHECK(!IsUndefined(disposable_stack->stack()));
  int length = disposable_stack->length();
  int stack_type =
      DisposeCallTypeBit::encode(type) | DisposeHintBit::encode(hint);
  DirectHandle<Smi> stack_type_handle(Smi::FromInt(stack_type), isolate);

  Handle<FixedArray> array(disposable_stack->stack(), isolate);
  array = FixedArray::SetAndGrow(isolate, array, length++, value);
  array = FixedArray::SetAndGrow(isolate, array, length++, method);
  array = FixedArray::SetAndGrow(isolate, array, length++, stack_type_handle);

  disposable_stack->set_length(length);
  disposable_stack->set_stack(*array);
}

// part of
// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-createdisposableresource
inline MaybeHandle<Object> JSDisposableStackBase::CheckValueAndGetDisposeMethod(
    Isolate* isolate, Handle<Object> value, DisposeMethodHint hint) {
  // 1. If method is not present, then
  //   a. If V is either null or undefined, then
  //    i. Set V to undefined.
  //    ii. Set method to undefined.
  // We has already returned from the caller if V is null or undefined.
  DCHECK(!IsNullOrUndefined(*value));

  //   b. Else,
  //    i. If V is not an Object, throw a TypeError exception.
  if (!IsJSReceiver(*value)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kExpectAnObjectWithUsing));
  }

  Handle<Object> method;
  if (hint == DisposeMethodHint::kSyncDispose) {
    //   ii. Set method to ? GetDisposeMethod(V, hint).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, method,
        Object::GetProperty(isolate, value,
                            isolate->factory()->dispose_symbol()));
    //   (GetMethod)3. If IsCallable(func) is false, throw a TypeError
    //   exception.
    if (!IsJSFunction(*method)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kNotCallable,
                                   isolate->factory()->dispose_symbol()));
    }

    //   iii. If method is undefined, throw a TypeError exception.
    //   It is already checked in step ii.

  } else if (hint == DisposeMethodHint::kAsyncDispose) {
    // https://tc39.es/proposal-explicit-resource-management/#sec-getdisposemethod
    // 1. If hint is async-dispose, then
    //   a. Let method be ? GetMethod(V, @@asyncDispose).
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, method,
        Object::GetProperty(isolate, value,
                            isolate->factory()->async_dispose_symbol()));
    //   b. If method is undefined, then
    if (IsUndefined(*method)) {
      //    i. Set method to ? GetMethod(V, @@dispose).
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, method,
          Object::GetProperty(isolate, value,
                              isolate->factory()->dispose_symbol()));
      //   (GetMethod)3. If IsCallable(func) is false, throw a TypeError
      //   exception.
      if (!IsJSFunction(*method)) {
        THROW_NEW_ERROR(isolate,
                        NewTypeError(MessageTemplate::kNotCallable,
                                     isolate->factory()->dispose_symbol()));
      }
      //    ii. If method is not undefined, then
      if (!IsUndefined(*method)) {
        //      1. Let closure be a new Abstract Closure with no parameters that
        //      captures method and performs the following steps when called:
        //        a. Let O be the this value.
        //        b. Let promiseCapability be ! NewPromiseCapability(%Promise%).
        //        c. Let result be Completion(Call(method, O)).
        //        d. IfAbruptRejectPromise(result, promiseCapability).
        //        e. Perform ? Call(promiseCapability.[[Resolve]], undefined, «
        //        undefined »).
        //        f. Return promiseCapability.[[Promise]].
        //      2. NOTE: This function is not observable to user code. It is
        //      used to ensure that a Promise returned from a synchronous
        //      @@dispose method will not be awaited and that any exception
        //      thrown will not be thrown synchronously.
        //      3. Return CreateBuiltinFunction(closure, 0, "", « »).

        // (TODO:rezvan): Add `kAsyncFromSyncDispose` to the `DisposeMethodHint`
        // enum and remove the following allocation of adapter clousre.
        Handle<Context> async_dispose_from_sync_dispose_context =
            isolate->factory()->NewBuiltinContext(
                isolate->native_context(),
                static_cast<int>(
                    AsyncDisposeFromSyncDisposeContextSlots::kLength));
        async_dispose_from_sync_dispose_context->set(
            static_cast<int>(AsyncDisposeFromSyncDisposeContextSlots::kMethod),
            *method);

        method =
            Factory::JSFunctionBuilder{
                isolate,
                isolate->factory()
                    ->async_dispose_from_sync_dispose_shared_fun(),
                async_dispose_from_sync_dispose_context}
                .Build();
      }
    }
    //   (GetMethod)3. If IsCallable(func) is false, throw a TypeError
    //   exception.
    if (!IsJSFunction(*method)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kNotCallable,
                                   isolate->factory()->async_dispose_symbol()));
    }
  }
  return method;
}

inline MaybeHandle<Object> HandleErrorInDisposal(
    Isolate* isolate, MaybeHandle<Object> maybe_error) {
  DCHECK(isolate->has_exception());
  Handle<Object> current_error(isolate->exception(), isolate);
  isolate->clear_internal_exception();

  Handle<Object> existing_error;

  //   i. If completion is a throw completion, then
  if (maybe_error.ToHandle(&existing_error)) {
    //    1. Set result to result.[[Value]].
    //    2. Let suppressed be completion.[[Value]].
    //    3. Let error be a newly created SuppressedError object.
    //    4. Perform CreateNonEnumerableDataPropertyOrThrow(error, "error",
    //    result).
    //    5. Perform CreateNonEnumerableDataPropertyOrThrow(error,
    //    "suppressed", suppressed).
    //    6. Set completion to ThrowCompletion(error).
    maybe_error = isolate->factory()->NewSuppressedErrorAtDisposal(
        isolate, current_error, existing_error);

  } else {
    //   ii. Else,
    //    1. Set completion to result.
    maybe_error = current_error;
  }

  return maybe_error;
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_DISPOSABLE_STACK_INL_H_
