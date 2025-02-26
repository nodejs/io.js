// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BUILTINS_BUILTINS_UTILS_INL_H_
#define V8_BUILTINS_BUILTINS_UTILS_INL_H_

#include "src/builtins/builtins-utils.h"

#include "src/execution/arguments-inl.h"

namespace v8 {
namespace internal {

Handle<Object> BuiltinArguments::atOrUndefined(Isolate* isolate,
                                               int index) const {
  if (index >= length()) {
    return isolate->factory()->undefined_value();
  }
  return at<Object>(index);
}

Handle<JSAny> BuiltinArguments::receiver() const {
  return Handle<JSAny>(address_of_arg_at(kReceiverIndex));
}

Handle<JSFunction> BuiltinArguments::target() const {
  return Handle<JSFunction>(address_of_arg_at(kTargetIndex));
}

Handle<HeapObject> BuiltinArguments::new_target() const {
  return Handle<JSFunction>(address_of_arg_at(kNewTargetIndex));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_BUILTINS_BUILTINS_UTILS_INL_H_
