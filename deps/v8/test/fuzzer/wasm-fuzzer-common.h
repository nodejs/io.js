// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_FUZZER_COMMON_H_
#define WASM_FUZZER_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>

#include "src/wasm/module-decoder.h"
#include "src/wasm/wasm-module-builder.h"

namespace v8::internal::wasm::fuzzing {

// A default value for {max_executed_instructions} in {ExecuteAgainstReference}.
#ifdef USE_SIMULATOR
constexpr int kDefaultMaxFuzzerExecutedInstructions = 16'000;
#else
constexpr int kDefaultMaxFuzzerExecutedInstructions = 1'000'000;
#endif

// First creates a reference module fully compiled with Liftoff, with
// instrumentation to stop after a given number of steps and to record any
// nondeterminism while executing. If execution finishes within {max_steps},
// {module_object} is instantiated, its "main" function is executed, and the
// result is compared against the reference execution. If non-determinism was
// detected during the reference execution, the result is allowed to differ.
void ExecuteAgainstReference(Isolate* isolate,
                             Handle<WasmModuleObject> module_object,
                             int32_t max_executed_instructions);

void GenerateTestCase(Isolate* isolate, ModuleWireBytes wire_bytes,
                      bool compiles);

// On the first call, enables all staged wasm features and experimental features
// that are ready for fuzzing. All subsequent calls are no-ops. This avoids race
// conditions with threads reading the flags. Fuzzers are executed in their own
// process anyway, so this should not interfere with anything.
void EnableExperimentalWasmFeatures(v8::Isolate* isolate);

class WasmExecutionFuzzer {
 public:
  virtual ~WasmExecutionFuzzer() = default;
  void FuzzWasmModule(base::Vector<const uint8_t> data,
                      bool require_valid = false);

  virtual size_t max_input_size() const { return 512; }

 protected:
  virtual bool GenerateModule(Isolate* isolate, Zone* zone,
                              base::Vector<const uint8_t> data,
                              ZoneBuffer* buffer) = 0;
};

}  // namespace v8::internal::wasm::fuzzing

#endif  // WASM_FUZZER_COMMON_H_
