// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_DEBUG_DEBUG_WASM_OBJECTS_H_
#define V8_DEBUG_DEBUG_WASM_OBJECTS_H_

#include <memory>

#include "src/objects/js-objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace debug {
class ScopeIterator;
}  // namespace debug

namespace internal {
namespace wasm {
class WasmValue;
}  // namespace wasm

#include "torque-generated/src/debug/debug-wasm-objects-tq.inc"

class ArrayList;
class WasmFrame;
class WasmInstanceObject;
#if V8_ENABLE_DRUMBRAKE
class WasmInterpreterEntryFrame;
#endif  // V8_ENABLE_DRUMBRAKE
class WasmModuleObject;
class WasmTableObject;

class WasmValueObject : public JSObject {
 public:
  DECL_ACCESSORS(type, Tagged<String>)
  DECL_ACCESSORS(value, Tagged<Object>)

  // Dispatched behavior.
  DECL_PRINTER(WasmValueObject)
  DECL_VERIFIER(WasmValueObject)

// Layout description.
#define WASM_VALUE_FIELDS(V)   \
  V(kTypeOffset, kTaggedSize)  \
  V(kValueOffset, kTaggedSize) \
  V(kSize, 0)
  DEFINE_FIELD_OFFSET_CONSTANTS(JSObject::kHeaderSize, WASM_VALUE_FIELDS)
#undef WASM_VALUE_FIELDS

  // Indices of in-object properties.
  static constexpr int kTypeIndex = 0;
  static constexpr int kValueIndex = 1;

  static Handle<WasmValueObject> New(Isolate* isolate,
                                     DirectHandle<String> type,
                                     DirectHandle<Object> value);
  static Handle<WasmValueObject> New(Isolate* isolate,
                                     const wasm::WasmValue& value,
                                     Handle<WasmModuleObject> module);

  OBJECT_CONSTRUCTORS(WasmValueObject, JSObject);
};

Handle<JSObject> GetWasmDebugProxy(WasmFrame* frame);

std::unique_ptr<debug::ScopeIterator> GetWasmScopeIterator(WasmFrame* frame);

#if V8_ENABLE_DRUMBRAKE
std::unique_ptr<debug::ScopeIterator> GetWasmInterpreterScopeIterator(
    WasmInterpreterEntryFrame* frame);
#endif  // V8_ENABLE_DRUMBRAKE

Handle<String> GetWasmFunctionDebugName(
    Isolate* isolate, DirectHandle<WasmTrustedInstanceData> instance_data,
    uint32_t func_index);

Handle<ArrayList> AddWasmInstanceObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    Handle<WasmInstanceObject> instance);

Handle<ArrayList> AddWasmModuleObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    DirectHandle<WasmModuleObject> module_object);

Handle<ArrayList> AddWasmTableObjectInternalProperties(
    Isolate* isolate, Handle<ArrayList> result,
    DirectHandle<WasmTableObject> table);

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_DEBUG_DEBUG_WASM_OBJECTS_H_
