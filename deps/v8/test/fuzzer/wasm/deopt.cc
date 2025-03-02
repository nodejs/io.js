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
#include "test/fuzzer/wasm/fuzzer-common.h"

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

class EnterDebuggingScope {
 public:
  explicit EnterDebuggingScope(Isolate* isolate) : isolate_(isolate) {
    GetWasmEngine()->EnterDebuggingForIsolate(isolate_);
  }
  ~EnterDebuggingScope() {
    GetWasmEngine()->LeaveDebuggingForIsolate(isolate_);
  }

 private:
  Isolate* isolate_;
};

std::vector<ExecutionResult> PerformReferenceRun(
    const std::vector<std::string>& callees, ModuleWireBytes wire_bytes,
    WasmEnabledFeatures enabled_features, bool valid, Isolate* isolate) {
  std::vector<ExecutionResult> results;
  FlagScope<bool> eager_compile(&v8_flags.wasm_lazy_compilation, false);
  ErrorThrower thrower(isolate, "WasmFuzzerSyncCompileReference");

  int32_t max_steps = kDefaultMaxFuzzerExecutedInstructions;

  // We aren't really debugging but this will prevent tier-up and other
  // "dynamic" behavior that we do not want to trigger during reference
  // execution. This also aligns well with the reference compilation compiling
  // with the kForDebugging liftoff option.
  EnterDebuggingScope debugging_scope(isolate);

  Handle<WasmModuleObject> module_object =
      CompileReferenceModule(isolate, wire_bytes.module_bytes(), &max_steps);

  thrower.Reset();
  CHECK(!isolate->has_exception());

  DirectHandle<WasmInstanceObject> instance;
  if (!GetWasmEngine()
           ->SyncInstantiate(isolate, &thrower, module_object, {}, {})
           .ToHandle(&instance)) {
    CHECK(thrower.error());
    // The only reason to fail the second instantiation should be OOM. This can
    // happen e.g. for memories with a very big initial size especially on 32
    // bit platforms.
    CHECK(strstr(thrower.error_msg(), "Out of memory"));
    return {};
  }

  NearHeapLimitCallbackScope near_heap_limit(isolate);
  for (uint32_t i = 0; i < callees.size(); ++i) {
    // Before execution, there should be no dangling nondeterminism registered
    // on the engine.
    DCHECK(!WasmEngine::had_nondeterminism());

    DirectHandle<Object> arguments[] = {
        direct_handle(Smi::FromInt(i), isolate)};
    std::unique_ptr<const char[]> exception;
    int32_t result = testing::CallWasmFunctionForTesting(
        isolate, instance, "main", base::VectorOf(arguments), &exception);
    // If there is nondeterminism, we cannot guarantee the behavior of the test
    // module, and in particular it may not terminate.
    if (WasmEngine::clear_nondeterminism()) break;
    // Reached max steps, do not try to execute the test module as it might
    // never terminate.
    if (max_steps < 0) break;
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

void ConfigureFlags(v8::Isolate* isolate) {
  struct FlagConfiguration {
    explicit FlagConfiguration(v8::Isolate* isolate) {
      // Disable the NativeModule cache. Different fuzzer iterations should not
      // interact with each other. Rerunning a fuzzer input (e.g. with
      // libfuzzer's "-runs=x" argument) should repeatedly test deoptimizations.
      // When caching the optimized code, only the first run will execute any
      // deopts.
      v8_flags.wasm_native_module_cache = false;
      // We switch it to synchronous mode to avoid the nondeterminism of
      // background jobs finishing at random times.
      v8_flags.wasm_sync_tier_up = true;
      // Enable the experimental features we want to fuzz. (Note that
      // EnableExperimentalWasmFeatures only enables staged features.)
      v8_flags.wasm_deopt = true;
      v8_flags.wasm_inlining_call_indirect = true;
      // Make inlining more aggressive.
      v8_flags.wasm_inlining_ignore_call_counts = true;
      v8_flags.wasm_inlining_budget = v8_flags.wasm_inlining_budget * 5;
      v8_flags.wasm_inlining_max_size = v8_flags.wasm_inlining_max_size * 5;
      v8_flags.wasm_inlining_factor = v8_flags.wasm_inlining_factor * 5;
      // Enable other staged or experimental features and enforce flag
      // implications.
      EnableExperimentalWasmFeatures(isolate);
    }
  };

  static FlagConfiguration config(isolate);
}

int FuzzIt(base::Vector<const uint8_t> data) {
  v8_fuzzer::FuzzerSupport* support = v8_fuzzer::FuzzerSupport::Get();
  v8::Isolate* isolate = support->GetIsolate();

  // Strictly enforce the input size limit as in fuzzer-common.h.
  if (data.size() > kMaxFuzzerInputSize) return 0;

  Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate);
  v8::Isolate::Scope isolate_scope(isolate);

  v8::HandleScope handle_scope(isolate);
  v8::Context::Scope context_scope(support->GetContext());

  ConfigureFlags(isolate);

  v8::TryCatch try_catch(isolate);
  AccountingAllocator allocator;
  Zone zone(&allocator, ZONE_NAME);

  // Clear recursive groups: The fuzzer creates random types in every run. These
  // are saved as recursive groups as part of the type canonicalizer, but types
  // from previous runs just waste memory.
  ResetTypeCanonicalizer(isolate, &zone);

  std::vector<std::string> callees;
  std::vector<std::string> inlinees;
  base::Vector<const uint8_t> buffer =
      GenerateWasmModuleForDeopt(&zone, data, callees, inlinees);
  ModuleWireBytes wire_bytes(buffer.begin(), buffer.end());

  testing::SetupIsolateForWasmModule(i_isolate);
  auto enabled_features = WasmEnabledFeatures::FromIsolate(i_isolate);
  bool valid = GetWasmEngine()->SyncValidate(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), buffer);

  // Choose whether to optimize the main function or the outermost callee.
  // As the main function has a fixed signature, it doesn't provide great
  // coverage to always optimize and deopt the main function. Instead by
  // optimizing an inner wasm function, there can be a large amount of
  // parameters and returns with all kinds of types.
  const bool optimize_main_function =
      inlinees.empty() || data.empty() || !(data.last() & 1);

  if (v8_flags.wasm_fuzzer_gen_test) {
    StdoutStream os;
    std::stringstream flags;
    flags << " --allow-natives-syntax --wasm-deopt "
             "--wasm-inlining-ignore-call-counts --wasm-sync-tier-up "
             "--wasm-inlining-budget="
          << v8_flags.wasm_inlining_budget
          << " --wasm-inlining-max-size=" << v8_flags.wasm_inlining_max_size
          << " --wasm-inlining-factor=" << v8_flags.wasm_inlining_factor;
    // The deopt fuzzer generates a different call sequence.
    bool emit_call_main = false;
    GenerateTestCase(os, i_isolate, wire_bytes, valid, emit_call_main,
                     flags.str());
    // Emit helper.
    os << "let maybeThrows = (fct, ...args) => {\n"
          "  try {\n"
          "    print(fct(...args));\n"
          "  } catch (e) {\n"
          "    print(`caught ${e}`);\n"
          "  }\n"
          "};\n\n";
    // Emit calls for the different call target indices each followed by an
    // explicit tier-up.
    std::string fct("instance.exports.");
    fct.append(optimize_main_function ? "main" : inlinees.back());
    for (size_t i = 0; i < callees.size(); ++i) {
      os << "maybeThrows(instance.exports.main, " << i
         << ");\n"
            "%WasmTierUpFunction("
         << fct << ");\n";
    }
    os.flush();
  }

  ErrorThrower thrower(i_isolate, "WasmFuzzerSyncCompile");
  MaybeHandle<WasmModuleObject> compiled = GetWasmEngine()->SyncCompile(
      i_isolate, enabled_features, CompileTimeImportsForFuzzing(), &thrower,
      base::OwnedCopyOf(buffer));
  if (!valid) {
    FATAL("Generated module should validate, but got: %s\n",
          thrower.error_msg());
  }

  std::vector<ExecutionResult> reference_results = PerformReferenceRun(
      callees, wire_bytes, enabled_features, valid, i_isolate);

  if (reference_results.empty()) {
    // If the first run already included non-determinism, there isn't any value
    // in even compiling it (as this fuzzer focuses on executing deopts).
    // Return -1 to not add this case to the corpus.
    return -1;
  }

  Handle<WasmModuleObject> module_object = compiled.ToHandleChecked();
  DirectHandle<WasmInstanceObject> instance;
  if (!GetWasmEngine()
           ->SyncInstantiate(i_isolate, &thrower, module_object, {}, {})
           .ToHandle(&instance)) {
    DCHECK(thrower.error());
    // The only reason to fail the second instantiation should be OOM. This can
    // happen e.g. for memories with a very big initial size especially on 32
    // bit platforms.
    if (strstr(thrower.error_msg(), "Out of memory")) {
      return -1;  // Return -1 to not add this case to the corpus.
    }
    FATAL("Second instantiation failed unexpectedly: %s", thrower.error_msg());
  }
  DCHECK(!thrower.error());

  DirectHandle<WasmExportedFunction> main_function =
      testing::GetExportedFunction(i_isolate, instance, "main")
          .ToHandleChecked();
  int function_to_optimize =
      main_function->shared()->wasm_exported_function_data()->function_index();
  if (!optimize_main_function) {
    function_to_optimize--;
  }

  int deopt_count_begin = GetWasmEngine()->GetDeoptsExecutedCount();
  int deopt_count_previous_iteration = deopt_count_begin;
  size_t num_callees = reference_results.size();
  for (uint32_t i = 0; i < num_callees; ++i) {
    DirectHandle<Object> arguments[] = {
        direct_handle(Smi::FromInt(i), i_isolate)};
    std::unique_ptr<const char[]> exception;

    DCHECK(!WasmEngine::had_nondeterminism());  // No dangling nondeterminism.
    int32_t result_value = testing::CallWasmFunctionForTesting(
        i_isolate, instance, "main", base::VectorOf(arguments), &exception);
    // Also the second run can hit nondeterminism which was not hit before (when
    // growing memory). In that case, do not compare results.
    // TODO(384781857): Due to nondeterminism, the second run could even not
    // terminate. If this happens often enough we should do something about
    // this.
    if (WasmEngine::clear_nondeterminism()) return -1;

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
