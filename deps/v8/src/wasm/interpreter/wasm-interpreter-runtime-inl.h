// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_INL_H_
#define V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_INL_H_

#include "src/execution/arguments-inl.h"
#include "src/objects/objects-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-inl.h"
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#include "src/wasm/wasm-objects.h"

namespace v8 {
namespace internal {
namespace wasm {

inline Address WasmInterpreterRuntime::EffectiveAddress(uint64_t index) const {
  Handle<WasmTrustedInstanceData> trusted_data = wasm_trusted_instance_data();
  DCHECK_GE(std::numeric_limits<uintptr_t>::max(),
            trusted_data->memory0_size());
  DCHECK_GE(trusted_data->memory0_size(), index);
  // Compute the effective address of the access, making sure to condition
  // the index even in the in-bounds case.
  return reinterpret_cast<Address>(trusted_data->memory0_start()) + index;
}

inline bool WasmInterpreterRuntime::BoundsCheckMemRange(
    uint64_t index, uint64_t* size, Address* out_address) const {
  Handle<WasmTrustedInstanceData> trusted_data = wasm_trusted_instance_data();
  DCHECK_GE(std::numeric_limits<uintptr_t>::max(),
            trusted_data->memory0_size());
  if (!base::ClampToBounds<uint64_t>(index, size,
                                     trusted_data->memory0_size())) {
    return false;
  }
  *out_address = EffectiveAddress(index);
  return true;
}

inline uint8_t* WasmInterpreterRuntime::GetGlobalAddress(uint32_t index) {
  DCHECK_LT(index, module_->globals.size());
  return global_addresses_[index];
}

inline Handle<Object> WasmInterpreterRuntime::GetGlobalRef(
    uint32_t index) const {
  // This function assumes that it is executed in a HandleScope.
  const wasm::WasmGlobal& global = module_->globals[index];
  DCHECK(global.type.is_reference());
  Tagged<FixedArray> global_buffer;  // The buffer of the global.
  uint32_t global_index = 0;         // The index into the buffer.
  std::tie(global_buffer, global_index) =
      wasm_trusted_instance_data()->GetGlobalBufferAndIndex(global);
  return Handle<Object>(global_buffer->get(global_index), isolate_);
}

inline void WasmInterpreterRuntime::SetGlobalRef(uint32_t index,
                                                 Handle<Object> ref) const {
  // This function assumes that it is executed in a HandleScope.
  const wasm::WasmGlobal& global = module_->globals[index];
  DCHECK(global.type.is_reference());
  Tagged<FixedArray> global_buffer;  // The buffer of the global.
  uint32_t global_index = 0;         // The index into the buffer.
  std::tie(global_buffer, global_index) =
      wasm_trusted_instance_data()->GetGlobalBufferAndIndex(global);
  global_buffer->set(global_index, *ref);
}

inline void WasmInterpreterRuntime::InitMemoryAddresses() {
  memory_start_ = wasm_trusted_instance_data()->memory0_start();
}

inline uint64_t WasmInterpreterRuntime::MemorySize() const {
  return wasm_trusted_instance_data()->memory0_size() / kWasmPageSize;
}

inline bool WasmInterpreterRuntime::IsMemory64() const {
  return !module_->memories.empty() && module_->memories[0].is_memory64;
}

inline size_t WasmInterpreterRuntime::GetMemorySize() const {
  return wasm_trusted_instance_data()->memory0_size();
}

inline void WasmInterpreterRuntime::DataDrop(uint32_t index) {
  wasm_trusted_instance_data()->data_segment_sizes()->set(index, 0);
}

inline void WasmInterpreterRuntime::ElemDrop(uint32_t index) {
  wasm_trusted_instance_data()->element_segments()->set(
      index, *isolate_->factory()->empty_fixed_array());
}

inline WasmBytecode* WasmInterpreterRuntime::GetFunctionBytecode(
    uint32_t func_index) {
  return codemap_->GetFunctionBytecode(func_index);
}

inline bool WasmInterpreterRuntime::IsNullTypecheck(
    const WasmRef obj, const ValueType obj_type) const {
  return IsNull(isolate_, obj, obj_type);
}

// static
inline Tagged<Object> WasmInterpreterRuntime::GetNullValue(
    const ValueType obj_type) const {
  if (obj_type == kWasmExternRef || obj_type == kWasmNullExternRef) {
    return *isolate_->factory()->null_value();
  } else {
    return *isolate_->factory()->wasm_null();
  }
}

// static
inline bool WasmInterpreterRuntime::IsNull(Isolate* isolate, const WasmRef obj,
                                           const ValueType obj_type) {
  if (obj_type == kWasmExternRef || obj_type == kWasmNullExternRef) {
    return i::IsNull(*obj, isolate);
  } else {
    return i::IsWasmNull(*obj, isolate);
  }
}

inline bool WasmInterpreterRuntime::IsRefNull(Handle<Object> object) const {
  // This function assumes that it is executed in a HandleScope.
  return i::IsNull(*object, isolate_) || IsWasmNull(*object, isolate_);
}

inline Handle<Object> WasmInterpreterRuntime::GetFunctionRef(
    uint32_t index) const {
  // This function assumes that it is executed in a HandleScope.
  return WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate_, wasm_trusted_instance_data(), index);
}

inline const ArrayType* WasmInterpreterRuntime::GetArrayType(
    uint32_t array_index) const {
  return module_->array_type(array_index);
}

inline WasmRef WasmInterpreterRuntime::GetWasmArrayRefElement(
    Tagged<WasmArray> array, uint32_t index) const {
  return WasmArray::GetElement(isolate_, handle(array, isolate_), index);
}

inline Handle<WasmTrustedInstanceData>
WasmInterpreterRuntime::wasm_trusted_instance_data() const {
  return handle(instance_object_->trusted_data(isolate_), isolate_);
}

inline WasmInterpreterThread::State InterpreterHandle::ContinueExecution(
    WasmInterpreterThread* thread, bool called_from_js) {
  return interpreter_.ContinueExecution(thread, called_from_js);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_INTERPRETER_WASM_INTERPRETER_RUNTIME_INL_H_
