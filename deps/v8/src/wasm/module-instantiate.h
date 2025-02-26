// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_MODULE_INSTANTIATE_H_
#define V8_WASM_MODULE_INSTANTIATE_H_

#include <stdint.h>

#include <optional>

#include "src/common/message-template.h"
#include "src/objects/code-kind.h"
#include "src/wasm/wasm-value.h"
#include "src/wasm/well-known-imports.h"

namespace v8 {
namespace internal {

class FixedArray;
class JSArrayBuffer;
class WasmFunctionData;
class WasmModuleObject;
class WasmInstanceObject;
class WasmTrustedInstanceData;
class Zone;

namespace wasm {
class ErrorThrower;
enum Suspend : int { kSuspend, kNoSuspend };
// kStressSwitch: switch to a secondary stack, but without the JSPI semantics:
// do not handle async imports and do not return a Promise. For testing only.
enum Promise : int { kPromise, kNoPromise, kStressSwitch };
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
class ResolvedWasmImport {
 public:
  // TODO(clemensb): We should only need one of {sig} and {expected_sig_id};
  // currently we can't efficiently translate between them.
  V8_EXPORT_PRIVATE ResolvedWasmImport(
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int func_index, Handle<JSReceiver> callable,
      const wasm::CanonicalSig* sig, CanonicalTypeIndex expected_sig_id,
      WellKnownImport preknown_import);

  ImportCallKind kind() const { return kind_; }
  WellKnownImport well_known_status() const { return well_known_status_; }
  Suspend suspend() const { return suspend_; }
  Handle<JSReceiver> callable() const { return callable_; }
  // Avoid reading function data from the result of `callable()`, because it
  // might have been corrupted in the meantime (in a compromised sandbox).
  // Instead, use this cached copy.
  Handle<WasmFunctionData> trusted_function_data() const {
    return trusted_function_data_;
  }

 private:
  void SetCallable(Isolate* isolate, Tagged<JSReceiver> callable);
  void SetCallable(Isolate* isolate, Handle<JSReceiver> callable);

  ImportCallKind ComputeKind(
      DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
      int func_index, const wasm::CanonicalSig* expected_sig,
      CanonicalTypeIndex expected_canonical_type_index,
      WellKnownImport preknown_import);

  ImportCallKind kind_;
  WellKnownImport well_known_status_{WellKnownImport::kGeneric};
  Suspend suspend_{kNoSuspend};
  Handle<JSReceiver> callable_;
  Handle<WasmFunctionData> trusted_function_data_;
};

MaybeHandle<WasmInstanceObject> InstantiateToInstanceObject(
    Isolate* isolate, ErrorThrower* thrower,
    Handle<WasmModuleObject> module_object, MaybeHandle<JSReceiver> imports,
    MaybeHandle<JSArrayBuffer> memory);

// Initializes a segment at index {segment_index} of the segment array of
// {instance}. If successful, returns the empty {Optional}, otherwise an
// {Optional} that contains the error message. Exits early if the segment is
// already initialized.
std::optional<MessageTemplate> InitializeElementSegment(
    Zone* zone, Isolate* isolate,
    Handle<WasmTrustedInstanceData> trusted_instance_data,
    Handle<WasmTrustedInstanceData> shared_trusted_instance_data,
    uint32_t segment_index);

V8_EXPORT_PRIVATE void CreateMapForType(
    Isolate* isolate, const WasmModule* module, ModuleTypeIndex type_index,
    Handle<FixedArray> maybe_shared_maps);

// Wrapper information required for graph building.
struct WrapperCompilationInfo {
  CodeKind code_kind;
  // For wasm-js wrappers only:
  wasm::ImportCallKind import_kind = kDefaultImportCallKind;
  int expected_arity = 0;
  wasm::Suspend suspend = kNoSuspend;
};

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_INSTANTIATE_H_
