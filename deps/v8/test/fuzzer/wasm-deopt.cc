// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8-context.h"
#include "include/v8-exception.h"
#include "include/v8-isolate.h"
#include "include/v8-local-handle.h"
#include "src/base/vector.h"
#include "src/execution/isolate.h"
#include "src/objects/property-descriptor.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/fuzzing/random-module-generation.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-feature-flags.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/common/flag-utils.h"
#include "test/common/wasm/wasm-module-runner.h"
#include "test/fuzzer/fuzzer-support.h"
#include "test/fuzzer/wasm-fuzzer-common.h"

// This fuzzer fuzzes deopts.
// It generates a main function accepting a call target. The call target is then
// used in a call_ref or call_indirect. The fuzzer runs the program in a
// reference run to collect expected results.
// Then it performs the same run on a new module optimizing the module after
// every target, causing emission of deopt nodes and potentially triggering
// deopts. Note that if the code containing the speculative call is unreachable
// or not inlined, the fuzzer won't generate a deopt node and won't perform a
// deopt.

// Pseudo code of a minimal wasm module that the fuzzer could generate:
//
// int global0 = 0;
// Table table = [callee0, callee1];
//
// int callee0(int a, int b) {
//   return a + b;
// }
//
// int callee1(int a, int b) {
//   return a * b;
// }
//
// int inlinee(int a, int b) {
//   auto callee = table.get(global0);
//   return call_ref(auto_callee)(a, b);
// }
//
// int main(int callee_index) {
//   global0 = callee_index;
//   return inlinee(1, 2);
// }

// The fuzzer then performs the following test:
//   assertEquals(expected_val0, main(0)); // Collects feedback.
//   %WasmTierUpFunction(main);
//   assertEquals(expected_val1, main(1)); // Potentially triggers deopt.

namespace v8::internal::wasm::fuzzing {

namespace {

using ExecutionResult = std::variant<int, std::string /*exception*/>;

std::ostream& operator<<(std::ostream& out, const ExecutionResult& result) {
  std::visit([&out](auto&& val) { out << val; }, result);
  return out;
}

class NearHeapLimitCallbackScope {
 public:
  explicit NearHeapLimitCallbackScope(Isolate* isolate) : isolate_(isolate) {
    isolate_->heap()->AddNearHeapLimitCallback(Callback, this);
  }

  ~NearHeapLimitCallbackScope() {
    isolate_->heap()->RemoveNearHeapLimitCallback(Callback, initial_limit_);
  }

  bool heap_limit_reached() const { return heap_limit_reached_; }

 private:
  static size_t Callback(void* raw_data, size_t current_limit,
                         size_t initial_limit) {
    NearHeapLimitCallbackScope* data =
        reinterpret_cast<NearHeapLimitCallbackScope*>(raw_data);
    data->heap_limit_reached_ = true;
    data->isolate_->TerminateExecution();
    data->initial_limit_ = initial_limit;
    // Return a slightly raised limit, just to make it to the next
    // interrupt check point, where execution will terminate.
    return initial_limit * 1.25;
  }

  Isolate* isolate_;
  bool heap_limit_reached_ = false;
  size_t initial_limit_ = 0;
};

std::vector<ExecutionResult> PerformReferenceRun(
    const std::vector<std::string>& callees, ModuleWireBytes wire_bytes,
    WasmEnabledFeatures enabled_features, bool valid, Isolate* isolate) {
  std::vector<ExecutionResult> results;
  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  ErrorThrower thrower(isolate, "WasmFuzzerSyncCompileReference");

  int32_t max_steps = kDefaultMaxFuzzerExecutedInstructions;
  int32_t nondeterminism = 0;

  Handle<WasmModuleObject> module_object = CompileReferenceModule(
      isolate, wire_bytes.module_bytes(), &max_steps, &nondeterminism);

  thrower.Reset();
  CHECK(!isolate->has_exception());

  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();

  auto arguments = base::OwnedVector<Handle<Object>>::New(1);

  NearHeapLimitCallbackScope near_heap_limit(isolate);
  for (uint32_t i = 0; i < callees.size(); ++i) {
    arguments[0] = handle(Smi::FromInt(i), isolate);
    std::unique_ptr<const char[]> exception;
    int32_t result = testing::CallWasmFunctionForTesting(
        isolate, instance, "main", arguments.as_vector(), &exception);
    // Reached max steps, do not try to execute the test module as it might
    // never terminate.
    if (max_steps < 0) break;
    // If there is nondeterminism, we cannot guarantee the behavior of the test
    // module, and in particular it may not terminate.
    if (nondeterminism != 0) break;
    // Similar to max steps reached, also discard modules that need too much
    // memory.
    if (near_heap_limit.heap_limit_reached()) {
      isolate->CancelTerminateExecution();
      break;
    }

    if (exception) {
      isolate->CancelTerminateExecution();
      if (strcmp(exception.get(),
                 "RangeError: Maximum call stack size exceeded") == 0) {
        // There was a stack overflow, which may happen nondeterministically. We
        // cannot guarantee the behavior of the test module, and in particular
        // it may not terminate.
        break;
      }
      results.emplace_back(exception.get());
    } else {
      results.emplace_back(result);
    }
  }
  thrower.Reset();
  isolate->clear_exception();
  return results;
}

int FuzzIt(base::Vector<const uint8_t> data) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  GetTypeCanonicalizer()->EmptyStorageForTesting();
  i_isolate->heap()->ClearWasmCanonicalRttsForTesting();

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  //  We switch it to synchronous mode to avoid the nondeterminism of background
  //  jobs finishing at random times.
  FlagScope<bool> sync_tier_up_scope(&v8_flags.wasm_sync_tier_up, true);
  // Enable the experimental features we want to fuzz. (Note that
  // EnableExperimentalWasmFeatures only enables staged features.)
  FlagScope<bool> deopt_scope(&v8_flags.wasm_deopt, true);
  FlagScope<bool> inlining_indirect(&v8_flags.wasm_inlining_call_indirect,
                                    true);
  // Make inlining more aggressive.
  FlagScope<bool> ignore_call_counts_scope(
      &v8_flags.wasm_inlining_ignore_call_counts, true);
  FlagScope<size_t> inlining_budget(&v8_flags.wasm_inlining_budget,
                                    v8_flags.wasm_inlining_budget * 5);
  FlagScope<size_t> inlining_size(&v8_flags.wasm_inlining_max_size,
                                  v8_flags.wasm_inlining_max_size * 5);
  FlagScope<size_t> inlining_factor(&v8_flags.wasm_inlining_factor,
                                    v8_flags.wasm_inlining_factor * 5);
  // Force new instruction selection.
  FlagScope<bool> new_isel(
      &v8_flags.turboshaft_wasm_instruction_selection_staged, true);

  EnableExperimentalWasmFeatures(isolate);

  v8::TryCatch try_catch(isolate);
  HandleScope scope(i_isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  std::vector<std::string> callees;
  std::vector<std::string> inlinees;
  base::Vector<const uint8_t> buffer =
      GenerateWasmModuleForDeopt(&zone, data, callees, inlinees);

  testing::SetupIsolateForWasmModule(i_isolate);
  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  bool valid = GetWasmEngine()->SyncValidate(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), wire_bytes);

  if (v8_flags.wasm_fuzzer_gen_test) {
    GenerateTestCase(i_isolate, wire_bytes, valid);
  }

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), &thrower,
      wire_bytes);
  if (!valid) {
    FATAL("Generated module should validate, but got: %s\n",
          thrower.error_msg());
  }

  std::vector<ExecutionResult> reference_results = PerformReferenceRun(
      callees, wire_bytes, enabled_features, valid, i_isolate);

  if (reference_results.empty()) {
    // If the first run already included non-determinism, there isn't any value
    // in even compiling it (as this fuzzer focusses on executing deopts).
    // Return -1 to not add this case to the corpus.
    return -1;
  }

  Handle<WasmModuleObject> module_object = compiled.ToHandleChecked();
  Handle<WasmInstanceObject> instance =
      GetWasmEngine()
          ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
          .ToHandleChecked();

  DirectHandle<WasmExportedFunction> main_function =
      testing::GetExportedFunction(i_isolate, instance, "main")
          .ToHandleChecked();
  int function_to_optimize =
      main_function->shared()->wasm_exported_function_data()->function_index();
  // As the main function has a fixed signature, it doesn't provide great
  // coverage to always optimize and deopt the main function. Instead by only
  // optimizing an inner wasm function, there can be a large amount of
  // parameters with all kinds of types.
  if (!inlinees.empty() && (data.last() & 1)) {
    function_to_optimize--;
  }

  int deopt_count_begin = GetWasmEngine()->GetDeoptsExecutedCount();
  int deopt_count_previous_iteration = deopt_count_begin;
  size_t num_callees = reference_results.size();
  for (uint32_t i = 0; i < num_callees; ++i) {
    auto arguments = base::OwnedVector<Handle<Object>>::New(1);
    arguments[0] = handle(Smi::FromInt(i), i_isolate);
    std::unique_ptr<const char[]> exception;
    int32_t result_value = testing::CallWasmFunctionForTesting(
        i_isolate, instance, "main", arguments.as_vector(), &exception);
    ExecutionResult actual_result;
    if (exception) {
      actual_result = exception.get();
    } else {
      actual_result = result_value;
    }
    if (actual_result != reference_results[i]) {
      std::cerr << "Different results vs. reference run for callee "
                << callees[i] << ": \nReference: " << reference_results[i]
                << "\nActual: " << actual_result << std::endl;
      CHECK_EQ(actual_result, reference_results[i]);
      UNREACHABLE();
    }

    int deopt_count = GetWasmEngine()->GetDeoptsExecutedCount();
    if (i != 0 && deopt_count == deopt_count_previous_iteration) {
      // No deopt triggered. Skip the rest of the run as it won't provide
      // meaningful coverage for the deoptimizer.
      // Return -1 to prevent adding this case to the corpus if not a single
      // deopt was executed.
      return deopt_count == deopt_count_begin ? -1 : 0;
    }
    deopt_count_previous_iteration = deopt_count;

    TierUpNowForTesting(i_isolate, instance->trusted_data(i_isolate),
                        function_to_optimize);
  }

  return 0;
}

}  // anonymous namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  return FuzzIt({data, size});
}

}  // namespace v8::internal::wasm::fuzzing
