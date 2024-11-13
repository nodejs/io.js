// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/interpreter/wasm-interpreter-objects.h"

#include "src/objects/heap-object-inl.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-objects-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#include "src/wasm/wasm-objects-inl.h"

namespace v8 {
namespace internal {

// static
Handle<Tuple2> WasmInterpreterObject::New(Handle<WasmInstanceObject> instance) {
  DCHECK(v8_flags.wasm_jitless);
  Isolate* isolate = instance->GetIsolate();
  Factory* factory = isolate->factory();
  Handle<WasmTrustedInstanceData> trusted_data =
      handle(instance->trusted_data(isolate), isolate);
  DCHECK(!trusted_data->has_interpreter_object());
  Handle<Tuple2> interpreter_object = factory->NewTuple2(
      instance, factory->undefined_value(), AllocationType::kOld);
  trusted_data->set_interpreter_object(*interpreter_object);
  return interpreter_object;
}

// static
bool WasmInterpreterObject::RunInterpreter(
    Isolate* isolate, Address frame_pointer,
    Handle<WasmInstanceObject> instance, int func_index,
    const std::vector<wasm::WasmValue>& argument_values,
    std::vector<wasm::WasmValue>& return_values) {
  DCHECK_LE(0, func_index);

  wasm::WasmInterpreterThread* thread =
      wasm::WasmInterpreterThread::GetCurrentInterpreterThread(isolate);
  DCHECK_NOT_NULL(thread);

  // Assume an instance can run in only one thread.
  Handle<Tuple2> interpreter_object =
      WasmTrustedInstanceData::GetInterpreterObject(instance);
  wasm::InterpreterHandle* handle =
      wasm::GetOrCreateInterpreterHandle(isolate, interpreter_object);

  return handle->Execute(thread, frame_pointer,
                         static_cast<uint32_t>(func_index), argument_values,
                         return_values);
}

// static
bool WasmInterpreterObject::RunInterpreter(Isolate* isolate,
                                           Address frame_pointer,
                                           Handle<WasmInstanceObject> instance,
                                           int func_index,
                                           uint8_t* interpreter_sp) {
  DCHECK_LE(0, func_index);

  wasm::WasmInterpreterThread* thread =
      wasm::WasmInterpreterThread::GetCurrentInterpreterThread(isolate);
  DCHECK_NOT_NULL(thread);

  // Assume an instance can run in only one thread.
  Handle<Tuple2> interpreter_object =
      WasmTrustedInstanceData::GetInterpreterObject(instance);
  wasm::InterpreterHandle* handle =
      wasm::GetInterpreterHandle(isolate, interpreter_object);

  return handle->Execute(thread, frame_pointer,
                         static_cast<uint32_t>(func_index), interpreter_sp);
}

// static
std::vector<WasmInterpreterStackEntry>
WasmInterpreterObject::GetInterpretedStack(Tagged<Tuple2> interpreter_object,
                                           Address frame_pointer) {
  Tagged<Object> handle_obj = get_interpreter_handle(interpreter_object);
  DCHECK(!IsUndefined(handle_obj));
  return Cast<Managed<wasm::InterpreterHandle>>(handle_obj)
      ->raw()
      ->GetInterpretedStack(frame_pointer);
}

// static
int WasmInterpreterObject::GetFunctionIndex(Tagged<Tuple2> interpreter_object,
                                            Address frame_pointer, int index) {
  Tagged<Object> handle_obj = get_interpreter_handle(interpreter_object);
  DCHECK(!IsUndefined(handle_obj));
  return Cast<Managed<wasm::InterpreterHandle>>(handle_obj)
      ->raw()
      ->GetFunctionIndex(frame_pointer, index);
}

}  // namespace internal
}  // namespace v8
