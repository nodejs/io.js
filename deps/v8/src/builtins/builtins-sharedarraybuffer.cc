// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/common/globals.h"
#include "src/execution/futex-emulation.h"
#include "src/heap/factory.h"
#include "src/logging/counters.h"
#include "src/numbers/conversions-inl.h"
#include "src/objects/js-array-buffer-inl.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

// See builtins-arraybuffer.cc for implementations of
// SharedArrayBuffer.prototype.byteLength and SharedArrayBuffer.prototype.slice

// https://tc39.es/ecma262/#sec-atomics.islockfree
inline bool AtomicIsLockFree(double size) {
  // According to the standard, 1, 2, and 4 byte atomics are supposed to be
  // 'lock free' on every platform. 'Lock free' means that all possible uses of
  // those atomics guarantee forward progress for the agent cluster (i.e. all
  // threads in contrast with a single thread).
  //
  // This property is often, but not always, aligned with whether atomic
  // accesses are implemented with software locks such as mutexes.
  //
  // V8 has lock free atomics for all sizes on all supported first-class
  // architectures: ia32, x64, ARM32 variants, and ARM64. Further, this property
  // is depended upon by WebAssembly, which prescribes that all atomic accesses
  // are always lock free.
  return size == 1 || size == 2 || size == 4 || size == 8;
}

// https://tc39.es/ecma262/#sec-atomics.islockfree
BUILTIN(AtomicsIsLockFree) {
  HandleScope scope(isolate);
  Handle<Object> size = args.atOrUndefined(isolate, 1);
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, size,
                                     Object::ToNumber(isolate, size));
  return *isolate->factory()->ToBoolean(
      AtomicIsLockFree(Object::Number(*size)));
}

// https://tc39.es/ecma262/#sec-validatesharedintegertypedarray
V8_WARN_UNUSED_RESULT MaybeHandle<JSTypedArray> ValidateIntegerTypedArray(
    Isolate* isolate, Handle<Object> object, const char* method_name,
    bool only_int32_and_big_int64 = false) {
  if (IsJSTypedArray(*object)) {
    Handle<JSTypedArray> typed_array = Handle<JSTypedArray>::cast(object);

    if (typed_array->IsDetachedOrOutOfBounds()) {
      THROW_NEW_ERROR(
          isolate,
          NewTypeError(
              MessageTemplate::kDetachedOperation,
              isolate->factory()->NewStringFromAsciiChecked(method_name)),
          JSTypedArray);
    }

    if (only_int32_and_big_int64) {
      if (typed_array->type() == kExternalInt32Array ||
          typed_array->type() == kExternalBigInt64Array) {
        return typed_array;
      }
    } else {
      if (typed_array->type() != kExternalFloat32Array &&
          typed_array->type() != kExternalFloat64Array &&
          typed_array->type() != kExternalUint8ClampedArray)
        return typed_array;
    }
  }

  THROW_NEW_ERROR(
      isolate,
      NewTypeError(only_int32_and_big_int64
                       ? MessageTemplate::kNotInt32OrBigInt64TypedArray
                       : MessageTemplate::kNotIntegerTypedArray,
                   object),
      JSTypedArray);
}

// https://tc39.es/ecma262/#sec-validateatomicaccess
// ValidateAtomicAccess( typedArray, requestIndex )
V8_WARN_UNUSED_RESULT Maybe<size_t> ValidateAtomicAccess(
    Isolate* isolate, Handle<JSTypedArray> typed_array,
    Handle<Object> request_index) {
  Handle<Object> access_index_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, access_index_obj,
      Object::ToIndex(isolate, request_index,
                      MessageTemplate::kInvalidAtomicAccessIndex),
      Nothing<size_t>());

  size_t access_index;
  size_t typed_array_length = typed_array->GetLength();
  if (!TryNumberToSize(*access_index_obj, &access_index) ||
      access_index >= typed_array_length) {
    isolate->Throw(*isolate->factory()->NewRangeError(
        MessageTemplate::kInvalidAtomicAccessIndex));
    return Nothing<size_t>();
  }
  return Just<size_t>(access_index);
}

namespace {

inline size_t GetAddress64(size_t index, size_t byte_offset) {
  return (index << 3) + byte_offset;
}

inline size_t GetAddress32(size_t index, size_t byte_offset) {
  return (index << 2) + byte_offset;
}

}  // namespace

// ES #sec-atomics.notify
// Atomics.notify( typedArray, index, count )
BUILTIN(AtomicsNotify) {
  // TODO(clemensb): This builtin only allocates (an exception) in the case of
  // an error; we could try to avoid allocating the HandleScope in the non-error
  // case.
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> count = args.atOrUndefined(isolate, 3);

  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta,
      ValidateIntegerTypedArray(isolate, array, "Atomics.notify", true));

  // 2. Let i be ? ValidateAtomicAccess(typedArray, index).
  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return ReadOnlyRoots(isolate).exception();
  size_t i = maybe_index.FromJust();

  // 3. If count is undefined, let c be +∞.
  // 4. Else,
  //   a. Let intCount be ? ToInteger(count).
  //   b. Let c be max(intCount, 0).
  uint32_t c;
  if (IsUndefined(*count, isolate)) {
    c = kMaxUInt32;
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, count,
                                       Object::ToInteger(isolate, count));
    double count_double = Object::Number(*count);
    if (count_double < 0) {
      count_double = 0;
    } else if (count_double > kMaxUInt32) {
      count_double = kMaxUInt32;
    }
    c = static_cast<uint32_t>(count_double);
  }

  // Steps 5-9 performed in FutexEmulation::Wake.

  // 10. If IsSharedArrayBuffer(buffer) is false, return 0.
  Handle<JSArrayBuffer> array_buffer = sta->GetBuffer();

  if (V8_UNLIKELY(!array_buffer->is_shared())) {
    return Smi::zero();
  }

  // Steps 11-17 performed in FutexEmulation::Wake.
  size_t wake_addr;
  if (sta->type() == kExternalBigInt64Array) {
    wake_addr = GetAddress64(i, sta->byte_offset());
  } else {
    DCHECK(sta->type() == kExternalInt32Array);
    wake_addr = GetAddress32(i, sta->byte_offset());
  }
  int num_waiters_woken = FutexEmulation::Wake(*array_buffer, wake_addr, c);
  return Smi::FromInt(num_waiters_woken);
}

Tagged<Object> DoWait(Isolate* isolate, FutexEmulation::WaitMode mode,
                      Handle<Object> array, Handle<Object> index,
                      Handle<Object> value, Handle<Object> timeout) {
  // 1. Let buffer be ? ValidateIntegerTypedArray(typedArray, true).
  Handle<JSTypedArray> sta;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, sta,
      ValidateIntegerTypedArray(isolate, array, "Atomics.wait", true));

  // 2. If IsSharedArrayBuffer(buffer) is false, throw a TypeError exception.
  if (V8_UNLIKELY(!sta->GetBuffer()->is_shared())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kNotSharedTypedArray, array));
  }

  // 3. Let i be ? ValidateAtomicAccess(typedArray, index).
  Maybe<size_t> maybe_index = ValidateAtomicAccess(isolate, sta, index);
  if (maybe_index.IsNothing()) return ReadOnlyRoots(isolate).exception();
  size_t i = maybe_index.FromJust();

  // 4. Let arrayTypeName be typedArray.[[TypedArrayName]].
  // 5. If arrayTypeName is "BigInt64Array", let v be ? ToBigInt64(value).
  // 6. Otherwise, let v be ? ToInt32(value).
  if (sta->type() == kExternalBigInt64Array) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                       BigInt::FromObject(isolate, value));
  } else {
    DCHECK(sta->type() == kExternalInt32Array);
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, value,
                                       Object::ToInt32(isolate, value));
  }

  // 7. Let q be ? ToNumber(timeout).
  // 8. If q is NaN, let t be +∞, else let t be max(q, 0).
  double timeout_number;
  if (IsUndefined(*timeout, isolate)) {
    timeout_number = Object::Number(ReadOnlyRoots(isolate).infinity_value());
  } else {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, timeout,
                                       Object::ToNumber(isolate, timeout));
    timeout_number = Object::Number(*timeout);
    if (std::isnan(timeout_number))
      timeout_number = Object::Number(ReadOnlyRoots(isolate).infinity_value());
    else if (timeout_number < 0)
      timeout_number = 0;
  }

  // 9. If mode is sync, then
  //   a. Let B be AgentCanSuspend().
  //   b. If B is false, throw a TypeError exception.
  if (mode == FutexEmulation::WaitMode::kSync &&
      !isolate->allow_atomics_wait()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kAtomicsOperationNotAllowed,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Atomics.wait")));
  }

  Handle<JSArrayBuffer> array_buffer = sta->GetBuffer();

  if (sta->type() == kExternalBigInt64Array) {
    return FutexEmulation::WaitJs64(
        isolate, mode, array_buffer, GetAddress64(i, sta->byte_offset()),
        Handle<BigInt>::cast(value)->AsInt64(), timeout_number);
  } else {
    DCHECK(sta->type() == kExternalInt32Array);
    return FutexEmulation::WaitJs32(isolate, mode, array_buffer,
                                    GetAddress32(i, sta->byte_offset()),
                                    NumberToInt32(*value), timeout_number);
  }
}

// https://tc39.es/ecma262/#sec-atomics.wait
// Atomics.wait( typedArray, index, value, timeout )
BUILTIN(AtomicsWait) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);
  Handle<Object> timeout = args.atOrUndefined(isolate, 4);

  return DoWait(isolate, FutexEmulation::WaitMode::kSync, array, index, value,
                timeout);
}

BUILTIN(AtomicsWaitAsync) {
  HandleScope scope(isolate);
  Handle<Object> array = args.atOrUndefined(isolate, 1);
  Handle<Object> index = args.atOrUndefined(isolate, 2);
  Handle<Object> value = args.atOrUndefined(isolate, 3);
  Handle<Object> timeout = args.atOrUndefined(isolate, 4);

  return DoWait(isolate, FutexEmulation::WaitMode::kAsync, array, index, value,
                timeout);
}

}  // namespace internal
}  // namespace v8
