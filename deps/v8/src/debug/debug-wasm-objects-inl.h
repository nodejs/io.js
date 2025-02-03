// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEBUG_DEBUG_WASM_OBJECTS_INL_H_
#define V8_DEBUG_DEBUG_WASM_OBJECTS_INL_H_

#include "src/debug/debug-wasm-objects.h"
#include "src/objects/js-objects-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/debug/debug-wasm-objects-tq-inl.inc"

OBJECT_CONSTRUCTORS_IMPL(WasmValueObject, JSObject)

ACCESSORS(WasmValueObject, type, Tagged<String>, kTypeOffset)
ACCESSORS(WasmValueObject, value, Tagged<Object>, kValueOffset)

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_DEBUG_DEBUG_WASM_OBJECTS_INL_H_
