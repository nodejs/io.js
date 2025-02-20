// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_FUZZER_COMMON_H_
#define WASM_FUZZER_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "src/wasm/wasm-features.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal {
class StdoutStream;
class WasmInstanceObject;
namespace wasm {
class ZoneBuffer;
}
}  // namespace v8::internal

namespace v8::internal::wasm::fuzzing {

// A default value for {max_executed_instructions} in {ExecuteAgainstReference}.
#ifdef USE_SIMULATOR
constexpr int kDefaultMaxFuzzerExecutedInstructions = 16'000;
#else
constexpr int kDefaultMaxFuzzerExecutedInstructions = 1'000'000;
#endif

CompileTimeImports CompileTimeImportsForFuzzing();

// First creates a reference module fully compiled with Liftoff, with
// instrumentation to stop after a given number of steps and to record any
// nondeterminism while executing. If execution finishes within {max_steps},
// {module_object} is instantiated, its "main" function is executed, and the
// result is compared against the reference execution. If non-determinism was
// detected during the reference execution, the result is allowed to differ.
void ExecuteAgainstReference(Isolate* isolate,
                             Handle<WasmModuleObject> module_object,
                             int32_t max_executed_instructions);

Handle<WasmModuleObject> CompileReferenceModule(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    int32_t* max_steps);

void GenerateTestCase(Isolate* isolate, ModuleWireBytes wire_bytes,
                      bool compiles);
void GenerateTestCase(StdoutStream& os, Isolate* isolate,
                      ModuleWireBytes wire_bytes, bool compiles,
                      bool emit_call_main, std::string_view extra_args);

// Validate a module containing a few wasm-gc types. This can be done to
// prepulate the TypeCanonicalizer with a few canonical types, so that a
// module-specific type index is more likely to be different from its canonical
// type index.
void AddDummyTypesToTypeCanonicalizer(Isolate* isolate, Zone* zone);

// On the first call, enables all staged wasm features and experimental features
// that are ready for fuzzing. All subsequent calls are no-ops. This avoids race
// conditions with threads reading the flags. Fuzzers are executed in their own
// process anyway, so this should not interfere with anything.
void EnableExperimentalWasmFeatures(v8::Isolate* isolate);

// Clear the type canonicalizer storage and re-add the dummy types.
void ResetTypeCanonicalizer(v8::Isolate* isolate, Zone* zone);

constexpr int kMaxFuzzerInputSize = 512;

class WasmExecutionFuzzer {
 public:
  virtual ~WasmExecutionFuzzer() = default;
  void FuzzWasmModule(base::Vector<const uint8_t> data,
                      bool require_valid = false);

  virtual size_t max_input_size() const { return kMaxFuzzerInputSize; }

 protected:
  virtual bool GenerateModule(Isolate* isolate, Zone* zone,
                              base::Vector<const uint8_t> data,
                              ZoneBuffer* buffer) = 0;
};

}  // namespace v8::internal::wasm::fuzzing

#endif  // WASM_FUZZER_COMMON_H_
