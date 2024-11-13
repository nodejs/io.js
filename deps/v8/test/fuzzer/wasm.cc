// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "include/libplatform/libplatform.h"
#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/execution/isolate-inl.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

namespace v8::internal::wasm::fuzzing {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  // We reduce the maximum memory size and table size of WebAssembly instances
  // to avoid OOMs in the fuzzer.
  v8_flags.wasm_max_mem_pages = 32;
  v8_flags.wasm_max_table_size = 100;

  // Disable lazy compilation to find compiler bugs easier.
  v8_flags.wasm_lazy_compilation = false;

  // Choose one of Liftoff or TurboFan, depending on the size of the input (we
  // can't use a dedicated byte from the input, because we want to be able to
  // pass Wasm modules unmodified to this fuzzer).
  v8_flags.liftoff = size & 1;

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  v8::Isolate::Scope isolate_scope(isolate);

  // Clear any exceptions from a prior run.
  if (i_isolate->has_exception()) {
    i_isolate->clear_exception();
  }

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged/experimental WebAssembly features here to
  // increase fuzzer coverage. For libfuzzer fuzzers it is not possible that the
  // fuzzer enables the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  testing::SetupIsolateForWasmModule(i_isolate);
  ModuleWireBytes wire_bytes(data, data + size);

  HandleScope scope(i_isolate);
  ErrorThrower thrower(i_isolate, "wasm fuzzer");
  Handle<WasmModuleObject> module_object;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  bool compiles = GetWasmEngine()
                      ->SyncCompile(i_isolate, enabled_features,
                                    CompileTimeImports{}, &thrower, wire_bytes)
                      .ToHandle(&module_object);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, compiles);
  }

  if (compiles) {
    ExecuteAgainstReference(i_isolate, module_object,
                            kDefaultMaxFuzzerExecutedInstructions);
  }

  // Pump the message loop and run micro tasks, e.g. GC finalization tasks.
  support->PumpMessageLoop(v8::platform::MessageLoopBehavior::kDoNotWait);
  isolate->PerformMicrotaskCheckpoint();
  return 0;
}

}  // namespace v8::internal::wasm::fuzzing
