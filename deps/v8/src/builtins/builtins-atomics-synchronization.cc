// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/js-atomics-synchronization-inl.h"

namespace v8 {
namespace internal {
namespace {
base::Optional<base::TimeDelta> GetTimeoutDelta(Handle<Object> timeout_obj) {
  double ms = Object::Number(*timeout_obj);
  if (!std::isnan(ms)) {
    if (ms < 0) ms = 0;
    if (ms <= static_cast<double>(std::numeric_limits<int64_t>::max())) {
      return base::TimeDelta::FromMilliseconds(static_cast<int64_t>(ms));
    }
  }
  return base::nullopt;
}

// TODO(lpardosixtos): Consider making and caching a canonical map for this
// result object, like we do for the iterator result object.
Handle<JSObject> CreateResultObject(Isolate* isolate, Handle<Object> value,
                                    bool success) {
  Handle<JSObject> result =
      isolate->factory()->NewJSObject(isolate->object_function());
  Handle<Object> success_value = isolate->factory()->ToBoolean(success);
  JSObject::AddProperty(isolate, result, "value", value,
                        PropertyAttributes::NONE);
  JSObject::AddProperty(isolate, result, "success", success_value,
                        PropertyAttributes::NONE);
  return result;
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
  Handle<JSAtomicsMutex> js_mutex = Handle<JSAtomicsMutex>::cast(js_mutex_obj);
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
  Handle<JSAtomicsMutex> js_mutex = Handle<JSAtomicsMutex>::cast(js_mutex_obj);
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
  Handle<JSObject> result =
      CreateResultObject(isolate, callback_result, success);
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
  Handle<JSAtomicsMutex> js_mutex = Handle<JSAtomicsMutex>::cast(js_mutex_obj);
  Handle<Object> run_under_lock = args.atOrUndefined(isolate, 2);
  if (!IsCallable(*run_under_lock)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotCallable, run_under_lock));
  }

  Handle<Object> timeout_obj = args.atOrUndefined(isolate, 3);
  base::Optional<base::TimeDelta> timeout;
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
  Handle<JSObject> result =
      CreateResultObject(isolate, callback_result, success);
  return *result;
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

  base::Optional<base::TimeDelta> timeout = base::nullopt;
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

  Handle<JSAtomicsCondition> js_condition =
      Handle<JSAtomicsCondition>::cast(js_condition_obj);
  Handle<JSAtomicsMutex> js_mutex = Handle<JSAtomicsMutex>::cast(js_mutex_obj);

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
    double count_double = Object::Number(*count_obj);
    if (count_double < 0) {
      count_double = 0;
    } else if (count_double > JSAtomicsCondition::kAllWaiters) {
      count_double = JSAtomicsCondition::kAllWaiters;
    }
    count = static_cast<uint32_t>(count_double);
  }

  Handle<JSAtomicsCondition> js_condition =
      Handle<JSAtomicsCondition>::cast(js_condition_obj);
  return *isolate->factory()->NewNumberFromUint(
      JSAtomicsCondition::Notify(isolate, js_condition, count));
}

}  // namespace internal
}  // namespace v8
