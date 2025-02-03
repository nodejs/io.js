// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"
#include "src/objects/promise-inl.h"

namespace v8 {
namespace internal {
namespace {

std::optional<base::TimeDelta> GetTimeoutDelta(
    DirectHandle<Object> timeout_obj) {
  double ms = Object::NumberValue(*timeout_obj);
  if (!std::isnan(ms)) {
    if (ms < 0) ms = 0;
    if (ms <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
      return base::TimeDelta::FromMilliseconds(static_cast<int64_t>(ms));
    }
  }
  return std::nullopt;
}

Handle<JSPromise> UnlockAsyncLockedMutexFromPromiseHandler(Isolate* isolate) {
  DirectHandle<Context> context(isolate->context(), isolate);
  DirectHandle<Object> mutex(
      context->get(JSAtomicsMutex::kMutexAsyncContextSlot), isolate);
  Handle<Object> unlock_promise(
      context->get(JSAtomicsMutex::kUnlockedPromiseAsyncContextSlot), isolate);
  DirectHandle<Object> waiter_wrapper_obj(
      context->get(JSAtomicsMutex::kAsyncLockedWaiterAsyncContextSlot),
      isolate);

  auto js_mutex = Cast<JSAtomicsMutex>(mutex);
  auto js_unlock_promise = Cast<JSPromise>(unlock_promise);
  auto async_locked_waiter_wrapper = Cast<Foreign>(waiter_wrapper_obj);
  js_mutex->UnlockAsyncLockedMutex(isolate, async_locked_waiter_wrapper);
  return js_unlock_promise;
}

}  // namespace

BUILTIN(AtomicsMutexConstructor) {
  DCHECK(v8_flags.harmony_struct);
  HandleScope scope(isolate);
  return *isolate->factory()->NewJSAtomicsMutex();
}

BUILTIN(AtomicsMutexLock) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Mutex.lock";
  HandleScope scope(isolate);

  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 1);
  if (!IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }
  Handle<JSAtomicsMutex> js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);
  Handle<Object> run_under_lock = args.atOrUndefined(isolate, 2);
  if (!IsCallable(*run_under_lock)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, run_under_lock));
  }

  // Like Atomics.wait, synchronous locking may block, and so is disallowed on
  // the main thread.
  //
  // This is not a recursive lock, so also throw if recursively locking.
  if (!isolate->allow_atomics_wait() || js_mutex->IsCurrentThreadOwner()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsOperationNotAllowed,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  Handle<Object> result;
  {
    JSAtomicsMutex::LockGuard lock_guard(isolate, js_mutex);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, run_under_lock,
                        isolate->factory()->undefined_value(), 0, nullptr));
  }

  return *result;
}

BUILTIN(AtomicsMutexTryLock) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Mutex.tryLock";
  HandleScope scope(isolate);

  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 1);
  if (!IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }
  Handle<JSAtomicsMutex> js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);
  Handle<Object> run_under_lock = args.atOrUndefined(isolate, 2);
  if (!IsCallable(*run_under_lock)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, run_under_lock));
  }

  Handle<Object> callback_result;
  bool success;
  {
    JSAtomicsMutex::TryLockGuard try_lock_guard(isolate, js_mutex);
    if (try_lock_guard.locked()) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, callback_result,
          Execution::Call(isolate, run_under_lock,
                          isolate->factory()->undefined_value(), 0, nullptr));
      success = true;
    } else {
      callback_result = isolate->factory()->undefined_value();
      success = false;
    }
  }
  DirectHandle<JSObject> result =
      JSAtomicsMutex::CreateResultObject(isolate, callback_result, success);
  return *result;
}

BUILTIN(AtomicsMutexLockWithTimeout) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Mutex.lockWithTimeout";
  HandleScope scope(isolate);

  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 1);
  if (!IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }
  Handle<JSAtomicsMutex> js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);
  Handle<Object> run_under_lock = args.atOrUndefined(isolate, 2);
  if (!IsCallable(*run_under_lock)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, run_under_lock));
  }

  Handle<Object> timeout_obj = args.atOrUndefined(isolate, 3);
  std::optional<base::TimeDelta> timeout;
  if (!IsNumber(*timeout_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIsNotNumber, timeout_obj,
                              Object::TypeOf(isolate, timeout_obj)));
  }
  timeout = GetTimeoutDelta(timeout_obj);

  // Like Atomics.wait, synchronous locking may block, and so is disallowed on
  // the main thread.
  //
  // This is not a recursive lock, so also throw if recursively locking.
  if (!isolate->allow_atomics_wait() || js_mutex->IsCurrentThreadOwner()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsOperationNotAllowed,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  Handle<Object> callback_result;
  bool success;
  {
    JSAtomicsMutex::LockGuard lock_guard(isolate, js_mutex, timeout);
    if (V8_LIKELY(lock_guard.locked())) {
      ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
          isolate, callback_result,
          Execution::Call(isolate, run_under_lock,
                          isolate->factory()->undefined_value(), 0, nullptr));
      success = true;
    } else {
      callback_result = isolate->factory()->undefined_value();
      success = false;
    }
  }
  DirectHandle<JSObject> result =
      JSAtomicsMutex::CreateResultObject(isolate, callback_result, success);
  return *result;
}

BUILTIN(AtomicsMutexLockAsync) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Mutex.lockAsync";
  HandleScope scope(isolate);

  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 1);
  if (!IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }
  Handle<JSAtomicsMutex> js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);
  Handle<Object> run_under_lock = args.atOrUndefined(isolate, 2);
  if (!IsCallable(*run_under_lock)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, run_under_lock));
  }

  Handle<Object> timeout_obj = args.atOrUndefined(isolate, 3);
  std::optional<base::TimeDelta> timeout = std::nullopt;
  if (!IsUndefined(*timeout_obj, isolate)) {
    if (!IsNumber(*timeout_obj)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kIsNotNumber, timeout_obj,
                                Object::TypeOf(isolate, timeout_obj)));
    }
    timeout = GetTimeoutDelta(timeout_obj);
  }

  Handle<JSPromise> result_promise;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result_promise,
      JSAtomicsMutex::LockOrEnqueuePromise(isolate, js_mutex, run_under_lock,
                                           timeout));

  return *result_promise;
}

BUILTIN(AtomicsMutexAsyncUnlockResolveHandler) {
  DCHECK(v8_flags.harmony_struct);
  HandleScope scope(isolate);

  DirectHandle<Object> previous_result = args.atOrUndefined(isolate, 1);
  Handle<JSPromise> js_unlock_promise =
      UnlockAsyncLockedMutexFromPromiseHandler(isolate);

  Handle<JSObject> result =
      JSAtomicsMutex::CreateResultObject(isolate, previous_result, true);
  auto resolve_result = JSPromise::Resolve(js_unlock_promise, result);
  USE(resolve_result);
  return *isolate->factory()->undefined_value();
}

BUILTIN(AtomicsMutexAsyncUnlockRejectHandler) {
  DCHECK(v8_flags.harmony_struct);
  HandleScope scope(isolate);

  Handle<Object> error = args.atOrUndefined(isolate, 1);
  Handle<JSPromise> js_unlock_promise =
      UnlockAsyncLockedMutexFromPromiseHandler(isolate);

  auto reject_result = JSPromise::Reject(js_unlock_promise, error);
  USE(reject_result);
  return *isolate->factory()->undefined_value();
}

BUILTIN(AtomicsConditionConstructor) {
  DCHECK(v8_flags.harmony_struct);
  HandleScope scope(isolate);
  return *isolate->factory()->NewJSAtomicsCondition();
}

BUILTIN(AtomicsConditionWait) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Condition.wait";
  HandleScope scope(isolate);

  Handle<Object> js_condition_obj = args.atOrUndefined(isolate, 1);
  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 2);
  Handle<Object> timeout_obj = args.atOrUndefined(isolate, 3);
  if (!IsJSAtomicsCondition(*js_condition_obj) ||
      !IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  std::optional<base::TimeDelta> timeout = std::nullopt;
  if (!IsUndefined(*timeout_obj, isolate)) {
    if (!IsNumber(*timeout_obj)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kIsNotNumber, timeout_obj,
                                Object::TypeOf(isolate, timeout_obj)));
    }
    timeout = GetTimeoutDelta(timeout_obj);
  }

  if (!isolate->allow_atomics_wait()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsOperationNotAllowed,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  auto js_condition = Cast<JSAtomicsCondition>(js_condition_obj);
  auto js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);

  if (!js_mutex->IsCurrentThreadOwner()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kAtomicsMutexNotOwnedByCurrentThread));
  }

  return isolate->heap()->ToBoolean(
      JSAtomicsCondition::WaitFor(isolate, js_condition, js_mutex, timeout));
}

BUILTIN(AtomicsConditionNotify) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Condition.notify";
  HandleScope scope(isolate);

  Handle<Object> js_condition_obj = args.atOrUndefined(isolate, 1);
  Handle<Object> count_obj = args.atOrUndefined(isolate, 2);
  if (!IsJSAtomicsCondition(*js_condition_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  uint32_t count;
  if (IsUndefined(*count_obj, isolate)) {
    count = JSAtomicsCondition::kAllWaiters;
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, count_obj,
                                       Object::ToInteger(isolate, count_obj));
    double count_double = Object::NumberValue(*count_obj);
    if (count_double < 0) {
      count_double = 0;
    } else if (count_double > JSAtomicsCondition::kAllWaiters) {
      count_double = JSAtomicsCondition::kAllWaiters;
    }
    count = static_cast<uint32_t>(count_double);
  }

  auto js_condition = Cast<JSAtomicsCondition>(js_condition_obj);
  return *isolate->factory()->NewNumberFromUint(
      JSAtomicsCondition::Notify(isolate, js_condition, count));
}

BUILTIN(AtomicsConditionWaitAsync) {
  DCHECK(v8_flags.harmony_struct);
  constexpr char method_name[] = "Atomics.Condition.waitAsync";
  HandleScope scope(isolate);

  Handle<Object> js_condition_obj = args.atOrUndefined(isolate, 1);
  Handle<Object> js_mutex_obj = args.atOrUndefined(isolate, 2);
  if (!IsJSAtomicsCondition(*js_condition_obj) ||
      !IsJSAtomicsMutex(*js_mutex_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  method_name)));
  }

  Handle<Object> timeout_obj = args.atOrUndefined(isolate, 3);
  std::optional<base::TimeDelta> timeout = std::nullopt;
  if (!IsUndefined(*timeout_obj, isolate)) {
    if (!IsNumber(*timeout_obj)) {
      THROW_NEW_ERROR_RETURN_FAILURE(
          isolate, NewTypeError(MessageTemplate::kIsNotNumber, timeout_obj,
                                Object::TypeOf(isolate, timeout_obj)));
    }
    timeout = GetTimeoutDelta(timeout_obj);
  }

  Handle<JSAtomicsCondition> js_condition =
      Cast<JSAtomicsCondition>(js_condition_obj);
  auto js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);

  if (!js_mutex->IsCurrentThreadOwner()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kAtomicsMutexNotOwnedByCurrentThread));
  }

  Handle<JSReceiver> result_promise;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result_promise,
      JSAtomicsCondition::WaitAsync(isolate, js_condition, js_mutex, timeout));
  return *result_promise;
}

BUILTIN(AtomicsConditionAcquireLock) {
  DCHECK(v8_flags.harmony_struct);
  HandleScope scope(isolate);

  DirectHandle<Context> context(isolate->context(), isolate);
  Handle<Object> js_mutex_obj = Handle<Object>(
      context->get(JSAtomicsCondition::kMutexAsyncContextSlot), isolate);
  Handle<JSAtomicsMutex> js_mutex = Cast<JSAtomicsMutex>(js_mutex_obj);
  DirectHandle<JSPromise> lock_promise =
      JSAtomicsMutex::LockAsyncWrapperForWait(isolate, js_mutex);
  return *lock_promise;
}

}  // namespace internal
}  // namespace v8
