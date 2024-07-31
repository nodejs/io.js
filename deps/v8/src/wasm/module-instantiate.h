// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MODULE_INSTANTIATE_H_
#define V8_WASM_MODULE_INSTANTIATE_H_

#include <stdint.h>

#include "src/base/optional.h"
#include "src/common/message-template.h"
#include "src/objects/code-kind.h"
#include "src/wasm/wasm-value.h"
#include "src/wasm/well-known-imports.h"

namespace v8 {
namespace internal {

class FixedArray;
class JSArrayBuffer;
class WasmModuleObject;
class WasmInstanceObject;
class WasmTrustedInstanceData;
class Zone;

namespace wasm {
class ErrorThrower;
enum Suspend : bool;
struct WasmModule;

// Calls to Wasm imports are handled in several different ways, depending on the
// type of the target function/callable and whether the signature matches the
// argument arity.
// TODO(jkummerow): Merge kJSFunctionArity{Match,Mismatch}, we don't really
// need the distinction any more.
enum class ImportCallKind : uint8_t {
  kLinkError,                // static Wasm->Wasm type error
  kRuntimeTypeError,         // runtime Wasm->JS type error
  kWasmToCapi,               // fast Wasm->C-API call
  kWasmToJSFastApi,          // fast Wasm->JS Fast API C call
  kWasmToWasm,               // fast Wasm->Wasm call
  kJSFunctionArityMatch,     // fast Wasm->JS call
  kJSFunctionArityMismatch,  // Wasm->JS, needs adapter frame
  // Math functions imported from JavaScript that are intrinsified
  kFirstMathIntrinsic,
  kF64Acos = kFirstMathIntrinsic,
  kF64Asin,
  kF64Atan,
  kF64Cos,
  kF64Sin,
  kF64Tan,
  kF64Exp,
  kF64Log,
  kF64Atan2,
  kF64Pow,
  kF64Ceil,
  kF64Floor,
  kF64Sqrt,
  kF64Min,
  kF64Max,
  kF64Abs,
  kF32Min,
  kF32Max,
  kF32Abs,
  kF32Ceil,
  kF32Floor,
  kF32Sqrt,
  kF32ConvertF64,
  kLastMathIntrinsic = kF32ConvertF64,
  // For everything else, there's the call builtin.
  kUseCallBuiltin
};

constexpr ImportCallKind kDefaultImportCallKind =
    ImportCallKind::kJSFunctionArityMatch;

// Resolves which import call wrapper is required for the given JS callable.
// Provides the kind of wrapper needed, the ultimate target callable, and the
// suspender object if applicable. Note that some callables (e.g. a
// {WasmExportedFunction} or {WasmJSFunction}) just wrap another target, which
// is why the ultimate target is provided as well.
class WasmImportData {
 public:
  V8_EXPORT_PRIVATE WasmImportData(
      Handle<WasmTrustedInstanceData> trusted_instance_data, int func_index,
      Handle<JSReceiver> callable, const wasm::FunctionSig* sig,
      uint32_t expected_canonical_type_index, WellKnownImport preknown_import);

  ImportCallKind kind() const { return kind_; }
  WellKnownImport well_known_status() const { return well_known_status_; }
  Suspend suspend() const { return suspend_; }
  Handle<JSReceiver> callable() const { return callable_; }

 private:
  ImportCallKind ComputeKind(
      Handle<WasmTrustedInstanceData> trusted_instance_data, int func_index,
      const wasm::FunctionSig* expected_sig,
      uint32_t expected_canonical_type_index, WellKnownImport preknown_import);

  ImportCallKind kind_;
  WellKnownImport well_known_status_{WellKnownImport::kGeneric};
  Suspend suspend_{false};
  Handle<JSReceiver> callable_;
};

MaybeHandle<WasmInstanceObject> InstantiateToInstanceObject(
    Isolate* isolate, ErrorThrower* thrower,
    Handle<WasmModuleObject> module_object, MaybeHandle<JSReceiver> imports,
    MaybeHandle<JSArrayBuffer> memory);

// Initializes a segment at index {segment_index} of the segment array of
// {instance}. If successful, returns the empty {Optional}, otherwise an
// {Optional} that contains the error message. Exits early if the segment is
// already initialized.
base::Optional<MessageTemplate> InitializeElementSegment(
    Zone* zone, Isolate* isolate,
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    uint32_t segment_index);

V8_EXPORT_PRIVATE void CreateMapForType(
    Isolate* isolate, const WasmModule* module, int type_index,
    Handle<WasmInstanceObject> instance_object, Handle<FixedArray> maps);

// A union tagged on the code-kind for wrapper graph building data. The rest of
// the wrapper compilation pipeline is independent of the code kind.
struct WrapperCompilationInfo {
  CodeKind code_kind;
  union {
    bool is_import;
    struct {
      wasm::ImportCallKind import_kind;
      int expected_arity;
      wasm::Suspend suspend;
    } import_info;
  };
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_INSTANTIATE_H_
