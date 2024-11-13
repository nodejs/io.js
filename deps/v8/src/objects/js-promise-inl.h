// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_PROMISE_INL_H_
#define V8_OBJECTS_JS_PROMISE_INL_H_

#include "src/objects/js-promise.h"

#include "src/objects/objects-inl.h"  // Needed for write barriers
#include "src/objects/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/js-promise-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(JSPromise)

BOOL_ACCESSORS(JSPromise, flags, has_handler, HasHandlerBit::kShift)
BOOL_ACCESSORS(JSPromise, flags, is_silent, IsSilentBit::kShift)

Tagged<Object> JSPromise::result() const {
  DCHECK_NE(Promise::kPending, status());
  return reactions_or_result();
}

Tagged<Object> JSPromise::reactions() const {
  DCHECK_EQ(Promise::kPending, status());
  return reactions_or_result();
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_PROMISE_INL_H_
