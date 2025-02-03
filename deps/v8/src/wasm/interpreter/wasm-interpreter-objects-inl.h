// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_INL_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_INL_H_

#include "src/execution/isolate-utils-inl.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/cell.h"
#include "src/objects/heap-number.h"
#include "src/objects/objects-inl.h"
#include "src/objects/tagged-field-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-objects.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {

// static
inline Tagged<WasmInstanceObject> WasmInterpreterObject::get_wasm_instance(
    Tagged<Tuple2> interpreter_object) {
  return Cast<WasmInstanceObject>(interpreter_object->value1());
}
// static
inline void WasmInterpreterObject::set_wasm_instance(
    Tagged<Tuple2> interpreter_object,
    Tagged<WasmInstanceObject> wasm_instance) {
  return interpreter_object->set_value1(wasm_instance);
}

// static
inline Tagged<Object> WasmInterpreterObject::get_interpreter_handle(
    Tagged<Tuple2> interpreter_object) {
  return interpreter_object->value2();
}

// static
inline void WasmInterpreterObject::set_interpreter_handle(
    Tagged<Tuple2> interpreter_object, Tagged<Object> interpreter_handle) {
  DCHECK(IsForeign(interpreter_handle));
  return interpreter_object->set_value2(interpreter_handle);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_OBJECTS_INL_H_
