// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/wasm-fuzzer-common.h"

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "include/v8-metrics.h"
#include "src/execution/isolate.h"
#include "src/utils/ostreams.h"
#include "src/wasm/baseline/liftoff-compiler.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/function-body-decoder-impl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/module-decoder-impl.h"
#include "src/wasm/module-instantiate.h"
#include "src/wasm/string-builder-multiline.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module-builder.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "tools/wasm/mjsunit-module-disassembler-impl.h"

namespace v8::internal::wasm::fuzzing {

namespace {

void CompileAllFunctionsForReferenceExecution(NativeModule* native_module,
                                              int32_t* max_steps,
                                              int32_t* nondeterminism) {
  const WasmModule* module = native_module->module();
  WasmCodeRefScope code_ref_scope;
  CompilationEnv env = CompilationEnv::ForModule(native_module);
  ModuleWireBytes wire_bytes_accessor{native_module->wire_bytes()};
  for (size_t i = module->num_imported_functions; i < module->functions.size();
       ++i) {
    auto& func = module->functions[i];
    base::Vector<const uint8_t> func_code =
        wire_bytes_accessor.GetFunctionBytes(&func);
    constexpr bool kIsShared = false;
    FunctionBody func_body(func.sig, func.code.offset(), func_code.begin(),
                           func_code.end(), kIsShared);
    auto result =
        ExecuteLiftoffCompilation(&env, func_body,
                                  LiftoffOptions{}
                                      .set_func_index(func.func_index)
                                      .set_for_debugging(kForDebugging)
                                      .set_max_steps(max_steps)
                                      .set_nondeterminism(nondeterminism));
    if (!result.succeeded()) {
      FATAL(
          "Liftoff compilation failed on a valid module. Run with "
          "--trace-wasm-decoder (in a debug build) to see why.");
    }
    native_module->PublishCode(native_module->AddCompiledCode(result));
  }
}

}  // namespace

CompileTimeImports CompileTimeImportsForFuzzing() {
  CompileTimeImports result;
  result.Add(CompileTimeImport::kJsString);
  result.Add(CompileTimeImport::kTextDecoder);
  result.Add(CompileTimeImport::kTextEncoder);
  return result;
}

// Compile a baseline module. We pass a pointer to a max step counter and a
// nondeterminsm flag that are updated during execution by Liftoff.
Handle<WasmModuleObject> CompileReferenceModule(
    Isolate* isolate, base::Vector<const uint8_t> wire_bytes,
    int32_t* max_steps, int32_t* nondeterminism) {
  // Create the native module.
  std::shared_ptr<NativeModule> native_module;
  constexpr bool kNoVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  ModuleResult module_res =
      DecodeWasmModule(enabled_features, wire_bytes, kNoVerifyFunctions,
                       ModuleOrigin::kWasmOrigin);
  CHECK(module_res.ok());
  std::shared_ptr<WasmModule> module = module_res.value();
  CHECK_NOT_NULL(module);
  CompileTimeImports compile_imports = CompileTimeImportsForFuzzing();
  WasmError imports_error =
      ValidateAndSetBuiltinImports(module.get(), wire_bytes, compile_imports);
  CHECK(!imports_error.has_error());  // The module was compiled before.
  native_module = GetWasmEngine()->NewNativeModule(
      isolate, enabled_features, CompileTimeImportsForFuzzing(), module, 0);
  native_module->SetWireBytes(base::OwnedVector<uint8_t>::Of(wire_bytes));
  // The module is known to be valid as this point (it was compiled by the
  // caller before).
  module->set_all_functions_validated();

  // Compile all functions with Liftoff.
  CompileAllFunctionsForReferenceExecution(native_module.get(), max_steps,
                                           nondeterminism);

  // Create the module object.
  constexpr base::Vector<const char> kNoSourceUrl;
  DirectHandle<Script> script =
      GetWasmEngine()->GetOrCreateScript(isolate, native_module, kNoSourceUrl);
  isolate->heap()->EnsureWasmCanonicalRttsSize(module->MaxCanonicalTypeIndex() +
                                               1);
  return WasmModuleObject::New(isolate, std::move(native_module), script);
}

void ExecuteAgainstReference(Isolate* isolate,
                             Handle<WasmModuleObject> module_object,
                             int32_t max_executed_instructions) {
  // We do not instantiate the module if there is a start function, because a
  // start function can contain an infinite loop which we cannot handle.
  if (module_object->module()->start_function_index >= 0) return;

  int32_t max_steps = max_executed_instructions;
  int32_t nondeterminism = 0;

  HandleScope handle_scope(isolate);  // Avoid leaking handles.
  Zone reference_module_zone(isolate->allocator(), "wasm reference module");
  Handle<WasmModuleObject> module_ref = CompileReferenceModule(
      isolate, module_object->native_module()->wire_bytes(), &max_steps,
      &nondeterminism);
  Handle<WasmInstanceObject> instance_ref;

  // Try to instantiate the reference instance, return if it fails.
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference");
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_ref, {},
                               {})  // no imports & memory
             .ToHandle(&instance_ref)) {
      isolate->clear_exception();
      thrower.Reset();  // Ignore errors.
      return;
    }
  }

  // Get the "main" exported function. Do nothing if it does not exist.
  Handle<WasmExportedFunction> main_function;
  if (!testing::GetExportedFunction(isolate, instance_ref, "main")
           .ToHandle(&main_function)) {
    return;
  }

  struct OomCallbackData {
    Isolate* isolate;
    bool heap_limit_reached{false};
    size_t initial_limit{0};
  };
  OomCallbackData oom_callback_data{isolate};
  auto heap_limit_callback = [](void* raw_data, size_t current_limit,
                                size_t initial_limit) -> size_t {
    OomCallbackData* data = reinterpret_cast<OomCallbackData*>(raw_data);
    data->heap_limit_reached = true;
    data->isolate->TerminateExecution();
    data->initial_limit = initial_limit;
    // Return a slightly raised limit, just to make it to the next
    // interrupt check point, where execution will terminate.
    return initial_limit * 1.25;
  };
  isolate->heap()->AddNearHeapLimitCallback(heap_limit_callback,
                                            &oom_callback_data);

  Tagged<WasmExportedFunctionData> func_data =
      main_function->shared()->wasm_exported_function_data();
  const FunctionSig* sig = func_data->instance_data()
                               ->module()
                               ->functions[func_data->function_index()]
                               .sig;
  base::OwnedVector<Handle<Object>> compiled_args =
      testing::MakeDefaultArguments(isolate, sig);
  std::unique_ptr<const char[]> exception_ref;
  int32_t result_ref = testing::CallWasmFunctionForTesting(
      isolate, instance_ref, "main", compiled_args.as_vector(), &exception_ref);
  bool execute = true;
  // Reached max steps, do not try to execute the test module as it might
  // never terminate.
  if (max_steps < 0) execute = false;
  // If there is nondeterminism, we cannot guarantee the behavior of the test
  // module, and in particular it may not terminate.
  if (nondeterminism != 0) execute = false;
  // Similar to max steps reached, also discard modules that need too much
  // memory.
  isolate->heap()->RemoveNearHeapLimitCallback(heap_limit_callback,
                                               oom_callback_data.initial_limit);
  if (oom_callback_data.heap_limit_reached) {
    execute = false;
    isolate->CancelTerminateExecution();
  }

  if (exception_ref) {
    if (strcmp(exception_ref.get(),
               "RangeError: Maximum call stack size exceeded") == 0) {
      // There was a stack overflow, which may happen nondeterministically. We
      // cannot guarantee the behavior of the test module, and in particular it
      // may not terminate.
      execute = false;
    }
  }
  if (!execute) {
    // Before discarding the module, see if Turbofan runs into any DCHECKs.
    TierUpAllForTesting(isolate, instance_ref->trusted_data(isolate));
    return;
  }

  // Instantiate a fresh instance for the actual (non-ref) execution.
  Handle<WasmInstanceObject> instance;
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference (second)");
    // We instantiated before, so the second instantiation must also succeed.
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_object, {},
                               {})  // no imports & memory
             .ToHandle(&instance)) {
      DCHECK(thrower.error());
      // The only reason to fail the second instantiation should be OOM. Make
      // this a proper OOM crash so that ClusterFuzz categorizes it as such.
      if (strstr(thrower.error_msg(), "Out of memory")) {
        V8::FatalProcessOutOfMemory(isolate, "Wasm fuzzer second instantiation",
                                    thrower.error_msg());
      }
      FATAL("Second instantiation failed unexpectedly: %s",
            thrower.error_msg());
    }
    DCHECK(!thrower.error());
  }

  std::unique_ptr<const char[]> exception;
  int32_t result = testing::CallWasmFunctionForTesting(
      isolate, instance, "main", compiled_args.as_vector(), &exception);

  if ((exception_ref != nullptr) != (exception != nullptr)) {
    FATAL("Exception mismatch! Expected: <%s>; got: <%s>",
          exception_ref ? exception_ref.get() : "<no exception>",
          exception ? exception.get() : "<no exception>");
  }

  if (!exception) {
    CHECK_EQ(result_ref, result);
  }
}

void GenerateTestCase(Isolate* isolate, ModuleWireBytes wire_bytes,
                      bool compiles) {
  // Libfuzzer sometimes runs a test twice (for detecting memory leaks), and in
  // this case we do not want multiple outputs by this function.
  // Similarly if we explicitly execute the same test multiple times (via
  // `-runs=N`).
  static std::atomic<bool> did_output_before{false};
  if (did_output_before.exchange(true)) return;

  constexpr bool kVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  ModuleResult module_res =
      DecodeWasmModule(enabled_features, wire_bytes.module_bytes(),
                       kVerifyFunctions, ModuleOrigin::kWasmOrigin);
  CHECK_WITH_MSG(module_res.ok(), module_res.error().message().c_str());
  WasmModule* module = module_res.value().get();
  CHECK_NOT_NULL(module);

  AccountingAllocator allocator;
  Zone zone(&allocator, "constant expression zone");

  MultiLineStringBuilder out;
  NamesProvider names(module, wire_bytes.module_bytes());
  MjsunitModuleDis disassembler(out, module, &names, wire_bytes, &allocator,
                                !compiles);
  disassembler.PrintModule();
  const bool offsets = false;  // Not supported by MjsunitModuleDis.
  StdoutStream os;
  out.WriteTo(os, offsets);
  os.flush();
}

void EnableExperimentalWasmFeatures(v8::Isolate* isolate) {
  struct EnableExperimentalWasmFeatures {
    explicit EnableExperimentalWasmFeatures(v8::Isolate* isolate) {
      // Enable all staged features.
#define ENABLE_STAGED_FEATURES(feat, ...) \
  v8_flags.experimental_wasm_##feat = true;
      FOREACH_WASM_STAGING_FEATURE_FLAG(ENABLE_STAGED_FEATURES)
#undef ENABLE_STAGED_FEATURES

      // Enable non-staged experimental features or other experimental flags
      // that we also want to fuzz, e.g., new optimizations.
      // Note: If you add a Wasm feature here, you will also have to add the
      // respective flag(s) to the mjsunit/wasm/generate-random-module.js test,
      // otherwise that fails on an unsupported feature.
      // You may also want to add the flag(s) to the JS file header in
      // `PrintModule()` of `mjsunit-module-disassembler-impl.h`, to make bugs
      // easier to reproduce with generated mjsunit test cases.

      // See https://crbug.com/335082212.
      v8_flags.wasm_inlining_call_indirect = true;

      // Enforce implications from enabling features.
      FlagList::EnforceFlagImplications();

      // Last, install any conditional features. Implications are handled
      // implicitly.
      isolate->InstallConditionalFeatures(isolate->GetCurrentContext());
    }
  };
  // The compiler will properly synchronize the constructor call.
  static EnableExperimentalWasmFeatures one_time_enable_experimental_features(
      isolate);
}

void WasmExecutionFuzzer::FuzzWasmModule(base::Vector<const uint8_t> data,
                                         bool require_valid) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  // Strictly enforce the input size limit. Note that setting "max_len" on the
  // fuzzer target is not enough, since different fuzzers are used and not all
  // respect that limit.
  if (data.size() > max_input_size()) return;

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);

  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  // Clear any exceptions from a prior run.
  if (i_isolate->has_exception()) {
    i_isolate->clear_exception();
  }

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  ZoneBuffer buffer(&zone);

  // The first byte specifies some internal configuration, like which function
  // is compiled with with compiler, and other flags.
  uint8_t configuration_byte = data.empty() ? 0 : data[0];
  if (!data.empty()) data += 1;

  // Derive the compiler configuration for the first four functions from the
  // configuration byte, to choose for each function between:
  // 0: TurboFan
  // 1: Liftoff
  // 2: Liftoff for debugging
  // 3: Turboshaft
  uint8_t tier_mask = 0;
  uint8_t debug_mask = 0;
  uint8_t turboshaft_mask = 0;
  for (int i = 0; i < 4; ++i, configuration_byte /= 4) {
    int compiler_config = configuration_byte % 4;
    tier_mask |= (compiler_config == 0) << i;
    debug_mask |= (compiler_config == 2) << i;
    turboshaft_mask |= (compiler_config == 3) << i;
  }
  // Enable tierup for all turboshaft functions.
  tier_mask |= turboshaft_mask;

  if (!GenerateModule(i_isolate, &zone, data, &buffer)) {
    return;
  }

  testing::SetupIsolateForWasmModule(i_isolate);

  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  bool valid = GetWasmEngine()->SyncValidate(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  // We want to keep dynamic tiering enabled because that changes the code
  // Liftoff generates as well as optimizing compilers' behavior (especially
  // around inlining). We switch it to synchronous mode to avoid the
  // nondeterminism of background jobs finishing at random times.
  FlagScope<bool> sync_tier_up(&v8_flags.wasm_sync_tier_up, true);
  // The purpose of setting the tier mask (which affects the initial
  // compilation of each function) is to deterministically test a combination
  // of Liftoff and Turbofan.
  FlagScope<int> tier_mask_scope(&v8_flags.wasm_tier_mask_for_testing,
                                 tier_mask);
  FlagScope<int> debug_mask_scope(&v8_flags.wasm_debug_mask_for_testing,
                                  debug_mask);
  FlagScope<int> turboshaft_mask_scope(
      &v8_flags.wasm_turboshaft_mask_for_testing, turboshaft_mask);

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), &thrower,
      wire_bytes);
  CHECK_EQ(valid, !compiled_module.is_null());
  CHECK_EQ(!valid, thrower.error());
  if (require_valid && !valid) {
    FATAL("Generated module should validate, but got: %s", thrower.error_msg());
  }
  thrower.Reset();

  if (valid) {
    ExecuteAgainstReference(i_isolate, compiled_module.ToHandleChecked(),
                            kDefaultMaxFuzzerExecutedInstructions);
  }
}

}  // namespace v8::internal::wasm::fuzzing
