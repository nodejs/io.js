// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/fuzzer/wasm/fuzzer-common.h"

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
                                              int32_t* max_steps) {
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
                                      .set_detect_nondeterminism(true));
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
    int32_t* max_steps) {
  // Create the native module.
  std::shared_ptr<NativeModule> native_module;
  constexpr bool kNoVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  WasmDetectedFeatures detected_features;
  ModuleResult module_res =
      DecodeWasmModule(enabled_features, wire_bytes, kNoVerifyFunctions,
                       ModuleOrigin::kWasmOrigin, &detected_features);
  CHECK(module_res.ok());
  std::shared_ptr<WasmModule> module = std::move(module_res).value();
  CHECK_NOT_NULL(module);
  CompileTimeImports compile_imports = CompileTimeImportsForFuzzing();
  WasmError imports_error = ValidateAndSetBuiltinImports(
      module.get(), wire_bytes, compile_imports, &detected_features);
  CHECK(!imports_error.has_error());  // The module was compiled before.
  native_module = GetWasmEngine()->NewNativeModule(
      isolate, enabled_features, detected_features,
      CompileTimeImportsForFuzzing(), module, 0);
  native_module->SetWireBytes(base::OwnedCopyOf(wire_bytes));
  // The module is known to be valid as this point (it was compiled by the
  // caller before).
  module->set_all_functions_validated();

  // The value is -3 so that it is different than the compilation ID of actual
  // compilations, different than the sentinel value of the CompilationState
  // (-1) and the value used by native module deserialization (-2).
  const int dummy_fuzzing_compilation_id = -3;
  native_module->compilation_state()->set_compilation_id(
      dummy_fuzzing_compilation_id);
  InitializeCompilationForTesting(native_module.get());

  // Compile all functions with Liftoff.
  CompileAllFunctionsForReferenceExecution(native_module.get(), max_steps);

  // Create the module object.
  constexpr base::Vector<const char> kNoSourceUrl;
  DirectHandle<Script> script =
      GetWasmEngine()->GetOrCreateScript(isolate, native_module, kNoSourceUrl);
  TypeCanonicalizer::PrepareForCanonicalTypeId(isolate,
                                               module->MaxCanonicalTypeIndex());
  return WasmModuleObject::New(isolate, std::move(native_module), script);
}

void ExecuteAgainstReference(Isolate* isolate,
                             Handle<WasmModuleObject> module_object,
                             int32_t max_executed_instructions) {
  // We do not instantiate the module if there is a start function, because a
  // start function can contain an infinite loop which we cannot handle.
  if (module_object->module()->start_function_index >= 0) return;

  int32_t max_steps = max_executed_instructions;

  HandleScope handle_scope(isolate);  // Avoid leaking handles.
  Zone reference_module_zone(isolate->allocator(), "wasm reference module");
  Handle<WasmModuleObject> module_ref = CompileReferenceModule(
      isolate, module_object->native_module()->wire_bytes(), &max_steps);
  DirectHandle<WasmInstanceObject> instance_ref;

  // Before execution, there should be no dangling nondeterminism registered on
  // the engine.
  DCHECK(!WasmEngine::had_nondeterminism());

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
  DirectHandle<WasmExportedFunction> main_function;
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
  auto compiled_args = testing::MakeDefaultArguments(isolate, sig);
  std::unique_ptr<const char[]> exception_ref;
  int32_t result_ref = testing::CallWasmFunctionForTesting(
      isolate, instance_ref, "main", base::VectorOf(compiled_args),
      &exception_ref);
  bool execute = true;
  // Reached max steps, do not try to execute the test module as it might
  // never terminate.
  if (max_steps < 0) execute = false;
  // If there is nondeterminism, we cannot guarantee the behavior of the test
  // module, and in particular it may not terminate.
  if (WasmEngine::clear_nondeterminism()) execute = false;
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
  DirectHandle<WasmInstanceObject> instance;
  {
    ErrorThrower thrower(isolate, "ExecuteAgainstReference (second)");
    // We instantiated before, so the second instantiation must also succeed.
    if (!GetWasmEngine()
             ->SyncInstantiate(isolate, &thrower, module_object, {},
                               {})  // no imports & memory
             .ToHandle(&instance)) {
      DCHECK(thrower.error());
      // The only reason to fail the second instantiation should be OOM.
      if (strstr(thrower.error_msg(), "Out of memory")) {
        // The initial memory size might be too large for instantiation
        // (especially on 32 bit systems), therefore do not treat it as a fuzzer
        // failure.
        return;
      }
      FATAL("Second instantiation failed unexpectedly: %s",
            thrower.error_msg());
    }
    DCHECK(!thrower.error());
  }

  std::unique_ptr<const char[]> exception;
  int32_t result = testing::CallWasmFunctionForTesting(
      isolate, instance, "main", base::VectorOf(compiled_args), &exception);

  // Also the second run can hit nondeterminism which was not hit before (when
  // growing memory). In that case, do not compare results.
  // TODO(384781857): Due to nondeterminism, the second run could even not
  // terminate. If this happens often enough we should do something about this.
  if (WasmEngine::clear_nondeterminism()) return;

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
  StdoutStream os;
  GenerateTestCase(os, isolate, wire_bytes, compiles, false, "");
  os.flush();
}

void GenerateTestCase(StdoutStream& os, Isolate* isolate,
                      ModuleWireBytes wire_bytes, bool compiles,
                      bool emit_call_main, std::string_view extra_flags) {
  // Libfuzzer sometimes runs a test twice (for detecting memory leaks), and in
  // this case we do not want multiple outputs by this function.
  // Similarly if we explicitly execute the same test multiple times (via
  // `-runs=N`).
  static std::atomic<bool> did_output_before{false};
  if (did_output_before.exchange(true)) return;

  constexpr bool kVerifyFunctions = false;
  auto enabled_features = WasmEnabledFeatures::FromIsolate(isolate);
  WasmDetectedFeatures unused_detected_features;
  ModuleResult module_res = DecodeWasmModule(
      enabled_features, wire_bytes.module_bytes(), kVerifyFunctions,
      ModuleOrigin::kWasmOrigin, &unused_detected_features);
  CHECK_WITH_MSG(module_res.ok(), module_res.error().message().c_str());
  WasmModule* module = module_res.value().get();
  CHECK_NOT_NULL(module);

  AccountingAllocator allocator;
  Zone zone(&allocator, "constant expression zone");

  MultiLineStringBuilder out;
  NamesProvider names(module, wire_bytes.module_bytes());
  MjsunitModuleDis disassembler(out, module, &names, wire_bytes, &allocator,
                                !compiles);
  disassembler.PrintModule(extra_flags, emit_call_main);
  const bool offsets = false;  // Not supported by MjsunitModuleDis.
  out.WriteTo(os, offsets);
}

namespace {
std::vector<uint8_t> CreateDummyModuleWireBytes(Zone* zone) {
  // Build a simple module with a few types to pre-populate the type
  // canonicalizer.
  WasmModuleBuilder builder(zone);
  const bool is_final = true;
  builder.AddRecursiveTypeGroup(0, 2);
  builder.AddArrayType(zone->New<ArrayType>(kWasmF32, true), is_final);
  StructType::Builder struct_builder(zone, 2);
  struct_builder.AddField(kWasmI64, false);
  struct_builder.AddField(kWasmExternRef, false);
  builder.AddStructType(struct_builder.Build(), !is_final);
  FunctionSig::Builder sig_builder(zone, 1, 0);
  sig_builder.AddReturn(kWasmI32);
  builder.AddSignature(sig_builder.Get(), is_final);
  ZoneBuffer buffer{zone};
  builder.WriteTo(&buffer);
  return std::vector<uint8_t>(buffer.begin(), buffer.end());
}
}  // namespace

void AddDummyTypesToTypeCanonicalizer(Isolate* isolate, Zone* zone) {
  const size_t type_count = GetTypeCanonicalizer()->GetCurrentNumberOfTypes();
  testing::SetupIsolateForWasmModule(isolate);
  // Cache (and leak) the wire bytes, so they don't need to be rebuilt on each
  // run.
  static const std::vector<uint8_t> wire_bytes =
      CreateDummyModuleWireBytes(zone);
  const bool is_valid = GetWasmEngine()->SyncValidate(
      isolate, WasmEnabledFeatures(), CompileTimeImportsForFuzzing(),
      base::VectorOf(wire_bytes));
  CHECK(is_valid);
  // As the types are reset on each run by the fuzzer, the validation should
  // have added new types to the TypeCanonicalizer.
  CHECK_GT(GetTypeCanonicalizer()->GetCurrentNumberOfTypes(), type_count);
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

void ResetTypeCanonicalizer(v8::Isolate* isolate, Zone* zone) {
  v8::internal::Isolate* i_isolate =
      reinterpret_cast<v8::internal::Isolate*>(isolate);

  // Make sure that there are no NativeModules left referencing the canonical
  // types. Collecting NativeModules can require two rounds of GC.
  for (int i = 0; i < 2 && GetWasmEngine()->NativeModuleCount() != 0; i++) {
    // We need to invoke GC without stack, otherwise the native module may
    // survive.
    DisableConservativeStackScanningScopeForTesting no_stack_scanning(
        i_isolate->heap());
    isolate->RequestGarbageCollectionForTesting(
        v8::Isolate::kFullGarbageCollection);
  }
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  TypeCanonicalizer::ClearWasmCanonicalTypesForTesting(i_isolate);
  AddDummyTypesToTypeCanonicalizer(i_isolate, zone);
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
  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  // We explicitly enable staged WebAssembly features here to increase fuzzer
  // coverage. For libfuzzer fuzzers it is not possible that the fuzzer enables
  // the flag by itself.
  EnableExperimentalWasmFeatures(isolate);

  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  ResetTypeCanonicalizer(isolate, &zone);

  // Clear any exceptions from a prior run.
  if (i_isolate->has_exception()) {
    i_isolate->clear_exception();
  }

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);

  ZoneBuffer buffer(&zone);

  // The first byte specifies some internal configuration, like which function
  // is compiled with which compiler, and other flags.
  uint8_t configuration_byte = data.empty() ? 0 : data[0];
  if (!data.empty()) data += 1;

  // Derive the compiler configuration for the first four functions from the
  // configuration byte, to choose for each function between:
  // 0: TurboFan
  // 1: Liftoff
  // 2: Liftoff for debugging
  uint8_t tier_mask = 0;
  uint8_t debug_mask = 0;
  for (int i = 0; i < 4; ++i, configuration_byte /= 3) {
    int compiler_config = configuration_byte % 3;
    tier_mask |= (compiler_config == 0) << i;
    debug_mask |= (compiler_config == 2) << i;
  }

  if (!GenerateModule(i_isolate, &zone, data, &buffer)) {
    return;
  }

  testing::SetupIsolateForWasmModule(i_isolate);

  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());

  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);

  bool valid = GetWasmEngine()->SyncValidate(i_isolate, enabled_features,
                                             CompileTimeImportsForFuzzing(),
                                             wire_bytes.module_bytes());

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

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled_module = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), &thrower,
      base::OwnedCopyOf(buffer));
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
