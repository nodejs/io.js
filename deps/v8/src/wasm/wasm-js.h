// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_WASM_JS_H_
#define V8_WASM_WASM_JS_H_

#include <memory>

#include "src/common/globals.h"

namespace v8 {
class Value;
template <typename T>
class FunctionCallbackInfo;
class WasmStreaming;
}  // namespace v8

namespace v8::internal {

namespace wasm {
class CompilationResultResolver;
class StreamingDecoder;

V8_EXPORT_PRIVATE std::unique_ptr<WasmStreaming> StartStreamingForTesting(
    Isolate*, std::shared_ptr<wasm::CompilationResultResolver>);

#define WASM_JS_EXTERNAL_REFERENCE_LIST(V) \
  V(WebAssemblyCompile)                    \
  V(WebAssemblyException)                  \
  V(WebAssemblyExceptionGetArg)            \
  V(WebAssemblyExceptionIs)                \
  V(WebAssemblyGlobal)                     \
  V(WebAssemblyGlobalGetValue)             \
  V(WebAssemblyGlobalSetValue)             \
  V(WebAssemblyGlobalValueOf)              \
  V(WebAssemblyInstance)                   \
  V(WebAssemblyInstanceGetExports)         \
  V(WebAssemblyInstantiate)                \
  V(WebAssemblyMemory)                     \
  V(WebAssemblyMemoryGetBuffer)            \
  V(WebAssemblyMemoryGrow)                 \
  V(WebAssemblyModule)                     \
  V(WebAssemblyModuleCustomSections)       \
  V(WebAssemblyModuleExports)              \
  V(WebAssemblyModuleImports)              \
  V(WebAssemblyTable)                      \
  V(WebAssemblyTableGet)                   \
  V(WebAssemblyTableGetLength)             \
  V(WebAssemblyTableGrow)                  \
  V(WebAssemblyTableSet)                   \
  V(WebAssemblyTag)                        \
  V(WebAssemblySuspending)                 \
  V(WebAssemblyValidate)

#define DECL_WASM_JS_EXTERNAL_REFERENCE(Name) \
  V8_EXPORT_PRIVATE void Name(const v8::FunctionCallbackInfo<v8::Value>& info);
WASM_JS_EXTERNAL_REFERENCE_LIST(DECL_WASM_JS_EXTERNAL_REFERENCE)
#undef DECL_WASM_JS_EXTERNAL_REFERENCE
}  // namespace wasm

// Exposes a WebAssembly API to JavaScript through the V8 API.
class WasmJs {
 public:
  // Creates all API objects before the snapshot is serialized.
  V8_EXPORT_PRIVATE static void PrepareForSnapshot(Isolate* isolate);

  // Finalizes API object setup:
  // - installs the WebAssembly object on the global object, if requested; and
  // - creates API objects and properties that depend on runtime-enabled flags.
  V8_EXPORT_PRIVATE static void Install(Isolate* isolate,
                                        bool exposed_on_global_object);
  V8_EXPORT_PRIVATE static void InstallModule(Isolate* isolate,
                                              Handle<JSObject> webassembly);

  V8_EXPORT_PRIVATE static void InstallConditionalFeatures(
      Isolate* isolate, Handle<NativeContext> context);

  V8_EXPORT_PRIVATE static bool InstallTypeReflection(
      Isolate* isolate, DirectHandle<NativeContext> context,
      Handle<JSObject> webassembly);

  V8_EXPORT_PRIVATE static bool InstallJSPromiseIntegration(
      Isolate* isolate, DirectHandle<NativeContext> context,
      Handle<JSObject> webassembly);
};

}  // namespace v8::internal

#endif  // V8_WASM_WASM_JS_H_
