// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_MODULE_RUNNER_H_
#define V8_WASM_MODULE_RUNNER_H_

#include "src/execution/isolate.h"
#include "src/objects/objects.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-result.h"

namespace v8 {
namespace internal {
namespace wasm {
namespace testing {

// Returns a MaybeHandle to the JsToWasm wrapper of the wasm function exported
// with the given name by the provided instance.
MaybeHandle<WasmExportedFunction> GetExportedFunction(
    Isolate* isolate, DirectHandle<WasmInstanceObject> instance,
    const char* name);

// Call an exported wasm function by name. Returns -1 if the export does not
// exist or throws an error. Errors are cleared from the isolate before
// returning. {exception} is set to a string representation of the exception (if
// set and an exception occurs).
int32_t CallWasmFunctionForTesting(
    Isolate* isolate, DirectHandle<WasmInstanceObject> instance,
    const char* name, base::Vector<const DirectHandle<Object>> args,
    std::unique_ptr<const char[]>* exception = nullptr);

// Decode, verify, and run the function labeled "main" in the
// given encoded module. The module should have no imports.
int32_t CompileAndRunWasmModule(Isolate* isolate, const uint8_t* module_start,
                                const uint8_t* module_end);

// Decode and compile the given module with no imports.
MaybeHandle<WasmModuleObject> CompileForTesting(
    Isolate* isolate, ErrorThrower* thrower, base::Vector<const uint8_t> bytes);

// Decode, compile, and instantiate the given module with no imports.
MaybeHandle<WasmInstanceObject> CompileAndInstantiateForTesting(
    Isolate* isolate, ErrorThrower* thrower, base::Vector<const uint8_t> bytes);

// Generate an array of default arguments for the given signature, to be used
// when calling compiled code.
DirectHandleVector<Object> MakeDefaultArguments(Isolate* isolate,
                                                const FunctionSig* sig);

// Install function map, module symbol for testing
void SetupIsolateForWasmModule(Isolate* isolate);

}  // namespace testing
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_MODULE_RUNNER_H_
