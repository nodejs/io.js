// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/objects/js-shared-array-inl.h"

namespace v8 {
namespace internal {

BUILTIN(SharedArrayConstructor) {
  DCHECK(v8_flags.shared_string_table);

  HandleScope scope(isolate);

  Handle<Object> length_arg = args.atOrUndefined(isolate, 1);
  DirectHandle<Object> length_number;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, length_number,
                                     Object::ToInteger(isolate, length_arg));
  if (!IsSmi(*length_number)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kSharedArraySizeOutOfRange));
  }

  int length = Cast<Smi>(*length_number).value();
  if (length < 0 || length > FixedArray::kMaxCapacity) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewRangeError(MessageTemplate::kSharedArraySizeOutOfRange));
  }

  return *isolate->factory()->NewJSSharedArray(args.target(), length);
}

BUILTIN(SharedArrayIsSharedArray) {
  HandleScope scope(isolate);
  return isolate->heap()->ToBoolean(
      IsJSSharedArray(*args.atOrUndefined(isolate, 1)));
}

}  // namespace internal
}  // namespace v8
