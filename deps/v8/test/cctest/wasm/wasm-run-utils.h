// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WASM_RUN_UTILS_H
#define WASM_RUN_UTILS_H

#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <array>
#include <memory>

#include "src/base/utils/random-number-generator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/int64-lowering.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/node.h"
#include "src/compiler/wasm-compiler.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/canonical-types.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/local-decl-encoder.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-external-refs.h"
#include "src/wasm/wasm-js.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-objects-inl.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes.h"
#include "src/wasm/wasm-tier.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/graph-and-builders.h"
#include "test/common/call-tester.h"
#include "test/common/value-helper.h"
#include "test/common/wasm/flag-utils.h"

namespace v8::internal::wasm {

enum class TestExecutionTier : int8_t {
  kLiftoff = static_cast<int8_t>(ExecutionTier::kLiftoff),
  kTurbofan = static_cast<int8_t>(ExecutionTier::kTurbofan),
  kLiftoffForFuzzing
};
static_assert(
    std::is_same<std::underlying_type<ExecutionTier>::type,
                 std::underlying_type<TestExecutionTier>::type>::value,
    "enum types match");

enum TestingModuleMemoryType { kMemory32, kMemory64 };

using base::ReadLittleEndianValue;
using base::WriteLittleEndianValue;

constexpr uint32_t kMaxFunctions = 10;
constexpr uint32_t kMaxGlobalsSize = 128;
// Don't execute more than 16k steps.
constexpr int kMaxNumSteps = 16 * 1024;

using compiler::CallDescriptor;
using compiler::MachineTypeForC;
using compiler::Node;

// TODO(titzer): check traps more robustly in tests.
// Currently, in tests, we just return 0xDEADBEEF from the function in which
// the trap occurs if the runtime context is not available to throw a JavaScript
// exception.
#define CHECK_TRAP32(x) \
  CHECK_EQ(0xDEADBEEF, (base::bit_cast<uint32_t>(x)) & 0xFFFFFFFF)
#define CHECK_TRAP64(x)        \
  CHECK_EQ(0xDEADBEEFDEADBEEF, \
           (base::bit_cast<uint64_t>(x)) & 0xFFFFFFFFFFFFFFFF)
#define CHECK_TRAP(x) CHECK_TRAP32(x)

#define WASM_WRAPPER_RETURN_VALUE 8754

#define ADD_CODE(vec, ...)                           \
  do {                                               \
    uint8_t __buf[] = {__VA_ARGS__};                 \
    for (size_t __i = 0; __i < sizeof(__buf); __i++) \
      vec.push_back(__buf[__i]);                     \
  } while (false)

// For tests that must manually import a JSFunction with source code.
struct ManuallyImportedJSFunction {
  const FunctionSig* sig;
  Handle<JSFunction> js_function;
};

// Helper Functions.
bool IsSameNan(float expected, float actual);
bool IsSameNan(double expected, double actual);

// A  Wasm module builder. Globals are pre-set, however, memory and code may be
// progressively added by a test. In turn, we piecemeal update the runtime
// objects, i.e. {WasmInstanceObject} and {WasmModuleObject}.
class TestingModuleBuilder {
 public:
  TestingModuleBuilder(Zone*, ModuleOrigin origin, ManuallyImportedJSFunction*,
                       TestExecutionTier, Isolate* isolate);
  ~TestingModuleBuilder();

  uint8_t* AddMemory(uint32_t size, SharedFlag shared = SharedFlag::kNotShared,
                     TestingModuleMemoryType = kMemory32);

  size_t CodeTableLength() const { return native_module_->num_functions(); }

  template <typename T>
  T* AddMemoryElems(uint32_t count,
                    TestingModuleMemoryType mem_type = kMemory32) {
    AddMemory(count * sizeof(T), SharedFlag::kNotShared, mem_type);
    return raw_mem_start<T>();
  }

  template <typename T>
  T* AddGlobal(ValueType type = ValueType::For(MachineTypeForC<T>())) {
    const WasmGlobal* global = AddGlobal(type);
    return reinterpret_cast<T*>(globals_data_ + global->offset);
  }

  // TODO(14034): Allow selecting type finality.
  uint8_t AddSignature(const FunctionSig* sig) {
    const bool is_final = true;
    const bool is_shared = false;
    test_module_->AddSignatureForTesting(sig, kNoSuperType, is_final,
                                         is_shared);
    GetTypeCanonicalizer()->AddRecursiveGroup(test_module_.get(), 1);
    trusted_instance_data_->set_isorecursive_canonical_types(
        test_module_->isorecursive_canonical_type_ids.data());
    size_t size = test_module_->types.size();
    CHECK_GT(127, size);
    return static_cast<uint8_t>(size - 1);
  }

  uint32_t mem_size() const {
    CHECK_EQ(1, test_module_->memories.size());
    return mem0_size_;
  }

  template <typename T>
  T* raw_mem_start() const {
    DCHECK_NOT_NULL(mem0_start_);
    return reinterpret_cast<T*>(mem0_start_);
  }

  template <typename T>
  T* raw_mem_end() const {
    DCHECK_NOT_NULL(mem0_start_);
    return reinterpret_cast<T*>(mem0_start_ + mem0_size_);
  }

  template <typename T>
  T raw_mem_at(int i) {
    DCHECK_NOT_NULL(mem0_start_);
    return ReadMemory(&(reinterpret_cast<T*>(mem0_start_)[i]));
  }

  template <typename T>
  T raw_val_at(int i) {
    return ReadMemory(reinterpret_cast<T*>(mem0_start_ + i));
  }

  template <typename T>
  void WriteMemory(T* p, T val) {
    WriteLittleEndianValue<T>(reinterpret_cast<Address>(p), val);
  }

  template <typename T>
  T ReadMemory(T* p) {
    return ReadLittleEndianValue<T>(reinterpret_cast<Address>(p));
  }

  // Zero-initialize the memory.
  void BlankMemory() {
    uint8_t* raw = raw_mem_start<uint8_t>();
    memset(raw, 0, mem0_size_);
  }

  // Pseudo-randomly initialize the memory.
  void RandomizeMemory(unsigned int seed = 88) {
    uint8_t* raw = raw_mem_start<uint8_t>();
    uint8_t* end = raw_mem_end<uint8_t>();
    v8::base::RandomNumberGenerator rng;
    rng.SetSeed(seed);
    rng.NextBytes(raw, end - raw);
  }

  void SetMaxMemPages(uint32_t maximum_pages) {
    CHECK_EQ(1, test_module_->memories.size());
    test_module_->memories[0].maximum_pages = maximum_pages;
    DCHECK_EQ(trusted_instance_data_->memory_objects()->length(),
              test_module_->memories.size());
    trusted_instance_data_->memory_object(0)->set_maximum_pages(maximum_pages);
  }

  void SetMemoryShared() {
    CHECK_EQ(1, test_module_->memories.size());
    test_module_->memories[0].is_shared = true;
  }

  enum FunctionType { kImport, kWasm };
  uint32_t AddFunction(const FunctionSig* sig, const char* name,
                       FunctionType type);

  // Freezes the signature map of the module and allocates the storage for
  // export wrappers.
  void InitializeWrapperCache();

  // Wrap the code so it can be called as a JS function.
  Handle<JSFunction> WrapCode(uint32_t index);

  // If function_indexes is {nullptr}, the contents of the table will be
  // initialized with null functions.
  void AddIndirectFunctionTable(const uint16_t* function_indexes,
                                uint32_t table_size,
                                ValueType table_type = kWasmFuncRef);

  uint32_t AddBytes(base::Vector<const uint8_t> bytes);

  uint32_t AddException(const FunctionSig* sig);

  uint32_t AddPassiveDataSegment(base::Vector<const uint8_t> bytes);

  WasmFunction* GetFunctionAt(int index) {
    return &test_module_->functions[index];
  }

  Isolate* isolate() const { return isolate_; }
  Handle<WasmInstanceObject> instance_object() const {
    return instance_object_;
  }
  Handle<WasmTrustedInstanceData> trusted_instance_data() const {
    return trusted_instance_data_;
  }
  WasmCode* GetFunctionCode(uint32_t index) const {
    return native_module_->GetCode(index);
  }
  Address globals_start() const {
    return reinterpret_cast<Address>(globals_data_);
  }

  void SetDebugState() {
    native_module_->SetDebugState(kDebugging);
    execution_tier_ = TestExecutionTier::kLiftoff;
  }

  void SwitchToDebug() {
    SetDebugState();
    native_module_->RemoveCompiledCode(
        NativeModule::RemoveFilter::kRemoveNonDebugCode);
  }

  TestExecutionTier test_execution_tier() const { return execution_tier_; }

  ExecutionTier execution_tier() const {
    switch (execution_tier_) {
      case TestExecutionTier::kTurbofan:
        return ExecutionTier::kTurbofan;
      case TestExecutionTier::kLiftoff:
        return ExecutionTier::kLiftoff;
      default:
        UNREACHABLE();
    }
  }

  void set_max_steps(int n) { max_steps_ = n; }
  int* max_steps_ptr() { return &max_steps_; }
  int32_t nondeterminism() { return nondeterminism_; }
  int32_t* non_determinism_ptr() { return &nondeterminism_; }

  void EnableFeature(WasmFeature feature) { enabled_features_.Add(feature); }

 private:
  std::shared_ptr<WasmModule> test_module_;
  Isolate* isolate_;
  WasmFeatures enabled_features_;
  uint32_t global_offset = 0;
  // The TestingModuleBuilder only supports one memory currently.
  uint8_t* mem0_start_ = nullptr;
  uint32_t mem0_size_ = 0;
  uint8_t* globals_data_ = nullptr;
  TestExecutionTier execution_tier_;
  Handle<WasmInstanceObject> instance_object_;
  Handle<WasmTrustedInstanceData> trusted_instance_data_;
  NativeModule* native_module_ = nullptr;
  int32_t max_steps_ = kMaxNumSteps;
  int32_t nondeterminism_ = 0;

  // Data segment arrays that are normally allocated on the instance.
  std::vector<uint8_t> data_segment_data_;
  std::vector<Address> data_segment_starts_;
  std::vector<uint32_t> data_segment_sizes_;

  const WasmGlobal* AddGlobal(ValueType type);

  Handle<WasmInstanceObject> InitInstanceObject();
};

void TestBuildingGraph(Zone* zone, compiler::JSGraph* jsgraph,
                       CompilationEnv* env, const FunctionSig* sig,
                       compiler::SourcePositionTable* source_position_table,
                       const uint8_t* start, const uint8_t* end);

// A helper for compiling wasm functions for testing.
// It contains the internal state for compilation (i.e. TurboFan graph).
class WasmFunctionCompiler {
 public:
  ~WasmFunctionCompiler();

  Isolate* isolate() { return builder_->isolate(); }
  uint32_t function_index() { return function_->func_index; }
  uint32_t sig_index() { return function_->sig_index; }

  void Build(std::initializer_list<const uint8_t> bytes) {
    Build(base::VectorOf(bytes));
  }
  void Build(base::Vector<const uint8_t> bytes);

  uint8_t AllocateLocal(ValueType type) {
    uint32_t index = local_decls_.AddLocals(1, type);
    uint8_t result = static_cast<uint8_t>(index);
    DCHECK_EQ(index, result);
    return result;
  }

  void SetSigIndex(int sig_index) { function_->sig_index = sig_index; }

 private:
  friend class WasmRunnerBase;

  WasmFunctionCompiler(Zone* zone, const FunctionSig* sig,
                       TestingModuleBuilder* builder, const char* name);

  Zone* zone_;
  TestingModuleBuilder* builder_;
  WasmFunction* function_;
  LocalDeclEncoder local_decls_;
};

// A helper class to build a module around Wasm bytecode, generate machine
// code, and run that code.
class WasmRunnerBase : public InitializedHandleScope {
 public:
  WasmRunnerBase(ManuallyImportedJSFunction* maybe_import, ModuleOrigin origin,
                 TestExecutionTier execution_tier, int num_params,
                 Isolate* isolate = nullptr)
      : InitializedHandleScope(isolate),
        zone_(&allocator_, ZONE_NAME, kCompressGraphZone),
        builder_(&zone_, origin, maybe_import, execution_tier, isolate) {}

  // Builds a graph from the given Wasm code and generates the machine
  // code and call wrapper for that graph. This method must not be called
  // more than once.
  void Build(const uint8_t* start, const uint8_t* end) {
    Build(base::VectorOf(start, end - start));
  }
  void Build(std::initializer_list<const uint8_t> bytes) {
    Build(base::VectorOf(bytes));
  }
  void Build(base::Vector<const uint8_t> bytes) {
    CHECK(!compiled_);
    compiled_ = true;
    functions_[0]->Build(bytes);
  }

  // Resets the state for building the next function.
  // The main function called will always be the first function.
  template <typename ReturnType, typename... ParamTypes>
  WasmFunctionCompiler& NewFunction(const char* name = nullptr) {
    return NewFunction(CreateSig<ReturnType, ParamTypes...>(), name);
  }

  // Resets the state for building the next function.
  // The main function called will be the last generated function.
  // Returns the index of the previously built function.
  WasmFunctionCompiler& NewFunction(const FunctionSig* sig,
                                    const char* name = nullptr) {
    functions_.emplace_back(
        new WasmFunctionCompiler(&zone_, sig, &builder_, name));
    uint8_t sig_index = builder().AddSignature(sig);
    functions_.back()->SetSigIndex(sig_index);
    return *functions_.back();
  }

  uint8_t AllocateLocal(ValueType type) {
    return functions_[0]->AllocateLocal(type);
  }

  uint32_t function_index() { return functions_[0]->function_index(); }
  WasmFunction* function() { return functions_[0]->function_; }
  bool possible_nondeterminism() { return possible_nondeterminism_; }
  TestingModuleBuilder& builder() { return builder_; }
  Zone* zone() { return &zone_; }

  void SwitchToDebug() { builder_.SwitchToDebug(); }

  template <typename ReturnType, typename... ParamTypes>
  FunctionSig* CreateSig() {
    return WasmRunnerBase::CreateSig<ReturnType, ParamTypes...>(&zone_);
  }

  template <typename ReturnType, typename... ParamTypes>
  static FunctionSig* CreateSig(Zone* zone) {
    std::array<MachineType, sizeof...(ParamTypes)> param_machine_types{
        {MachineTypeForC<ParamTypes>()...}};
    base::Vector<MachineType> param_vec(param_machine_types.data(),
                                        param_machine_types.size());
    return CreateSig(zone, MachineTypeForC<ReturnType>(), param_vec);
  }

  // TODO(clemensb): Remove, use {CallViaJS} directly.
  void CheckCallApplyViaJS(double expected, uint32_t function_index,
                           Handle<Object>* buffer, int count) {
    MaybeHandle<Object> retval =
        CallViaJS(function_index, base::VectorOf(buffer, count));

    if (retval.is_null()) {
      CHECK_EQ(expected, static_cast<double>(0xDEADBEEF));
    } else {
      Handle<Object> result = retval.ToHandleChecked();
      if (IsSmi(*result)) {
        CHECK_EQ(expected, Smi::ToInt(*result));
      } else {
        CHECK(IsHeapNumber(*result));
        CHECK_DOUBLE_EQ(expected, HeapNumber::cast(*result)->value());
      }
    }
  }

  MaybeHandle<Object> CallViaJS(uint32_t function_index,
                                base::Vector<Handle<Object>> parameters) {
    Isolate* isolate = main_isolate();
    // Save the original context, because CEntry (for runtime calls) will
    // reset / invalidate it when returning.
    SaveContext save_context(isolate);

    if (jsfuncs_.size() <= function_index) {
      jsfuncs_.resize(function_index + 1);
    }
    if (jsfuncs_[function_index].is_null()) {
      jsfuncs_[function_index] = builder_.WrapCode(function_index);
    }
    Handle<JSFunction> jsfunc = jsfuncs_[function_index];
    Handle<Object> global(isolate->context()->global_object(), isolate);
    return Execution::TryCall(
        isolate, jsfunc, global, static_cast<int>(parameters.size()),
        parameters.data(), Execution::MessageHandling::kReport, nullptr);
  }

 private:
  static FunctionSig* CreateSig(Zone* zone, MachineType return_type,
                                base::Vector<MachineType> param_types);

 protected:
  wasm::WasmCodeRefScope code_ref_scope_;
  std::vector<Handle<JSFunction>> jsfuncs_;

  v8::internal::AccountingAllocator allocator_;
  Zone zone_;
  TestingModuleBuilder builder_;
  std::vector<std::unique_ptr<WasmFunctionCompiler>> functions_;
  bool compiled_ = false;
  bool possible_nondeterminism_ = false;
  int32_t main_fn_index_ = 0;

  static void SetThreadInWasmFlag() {
    *reinterpret_cast<int*>(trap_handler::GetThreadInWasmThreadLocalAddress()) =
        true;
  }

  static void ClearThreadInWasmFlag() {
    *reinterpret_cast<int*>(trap_handler::GetThreadInWasmThreadLocalAddress()) =
        false;
  }
};

template <typename T>
inline WasmValue WasmValueInitializer(T value) {
  return WasmValue(value);
}
template <>
inline WasmValue WasmValueInitializer(int8_t value) {
  return WasmValue(static_cast<int32_t>(value));
}
template <>
inline WasmValue WasmValueInitializer(int16_t value) {
  return WasmValue(static_cast<int32_t>(value));
}

template <typename ReturnType, typename... ParamTypes>
class WasmRunner : public WasmRunnerBase {
 public:
  explicit WasmRunner(TestExecutionTier execution_tier,
                      ModuleOrigin origin = kWasmOrigin,
                      ManuallyImportedJSFunction* maybe_import = nullptr,
                      const char* main_fn_name = "main",
                      Isolate* isolate = nullptr)
      : WasmRunnerBase(maybe_import, origin, execution_tier,
                       sizeof...(ParamTypes), isolate) {
    WasmFunctionCompiler& main_fn =
        NewFunction<ReturnType, ParamTypes...>(main_fn_name);
    // Non-zero if there is an import.
    main_fn_index_ = main_fn.function_index();
  }

  template <typename T>
  Handle<Object> MakeParam(T t) {
    Factory* factory = builder_.isolate()->factory();
    if constexpr (std::is_integral_v<T> && std::is_signed_v<T> &&
                  sizeof(T) <= sizeof(int)) {
      return factory->NewNumberFromInt(t);
    }
    if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T> &&
                  sizeof(T) <= sizeof(int)) {
      return factory->NewNumberFromUint(t);
    }
    if constexpr (std::is_same_v<T, int64_t>) {
      return BigInt::FromInt64(builder_.isolate(), t);
    }
    if constexpr (std::is_same_v<T, uint64_t>) {
      return BigInt::FromUint64(builder_.isolate(), t);
    }
    if constexpr (std::is_same_v<T, float>) {
      return factory->NewNumber(t);
    }
    if constexpr (std::is_same_v<T, double>) {
      return factory->NewNumber(t);
    }
    UNIMPLEMENTED();
  }

  ReturnType Call(ParamTypes... p) {
    std::array<Handle<Object>, sizeof...(p)> param_objs = {MakeParam(p)...};
    MaybeHandle<Object> retval =
        CallViaJS(function()->func_index, base::VectorOf(param_objs));

    if (retval.is_null()) {
      return static_cast<ReturnType>(0xDEADBEEFDEADBEEF);
    }

    Handle<Object> result = retval.ToHandleChecked();
    // For int64_t and uint64_t returns we will get a BigInt.
    if constexpr (std::is_integral_v<ReturnType> &&
                  sizeof(ReturnType) == sizeof(int64_t)) {
      CHECK(IsBigInt(*result));
      return BigInt::cast(*result)->AsInt64();
    }

    // Otherwise it must be a number (Smi or HeapNumber).
    CHECK(IsNumber(*result));
    double value = Object::Number(*result);
    // The JS API interprets all Wasm values as signed, hence we cast via the
    // signed equivalent type to avoid undefined behaviour in the casting.
    if constexpr (std::is_integral_v<ReturnType> &&
                  std::is_unsigned_v<ReturnType>) {
      using signed_t = std::make_signed_t<ReturnType>;
      return static_cast<ReturnType>(static_cast<signed_t>(value));
    }
    return static_cast<ReturnType>(value);
  }

  void CheckCallViaJS(double expected, ParamTypes... p) {
    // TODO(clemensb): Inline into callers; use {Call} and {CHECK_EQ} directly.
    ReturnType result = Call(p...);
    if constexpr (std::is_floating_point_v<ReturnType>) {
      if (std::isnan(result)) {
        CHECK(IsSameNan(static_cast<ReturnType>(expected), result));
        return;
      }
    }
    CHECK_EQ(expected, result);
  }

  void CheckCallViaJSTraps(ParamTypes... p) {
    std::array<Handle<Object>, sizeof...(p)> param_objs = {MakeParam(p)...};
    MaybeHandle<Object> retval =
        CallViaJS(function()->func_index, base::VectorOf(param_objs));
    CHECK(retval.is_null());
  }

  void SetMaxSteps(int n) { builder_.set_max_steps(n); }
  bool HasNondeterminism() { return builder_.nondeterminism(); }
};

// A macro to define tests that run in different engine configurations.
#define WASM_EXEC_TEST(name)                                                   \
  void RunWasm_##name(TestExecutionTier execution_tier);                       \
  TEST(RunWasmTurbofan_##name) {                                               \
    RunWasm_##name(TestExecutionTier::kTurbofan);                              \
  }                                                                            \
  TEST(RunWasmLiftoff_##name) { RunWasm_##name(TestExecutionTier::kLiftoff); } \
  void RunWasm_##name(TestExecutionTier execution_tier)

#define UNINITIALIZED_WASM_EXEC_TEST(name)               \
  void RunWasm_##name(TestExecutionTier execution_tier); \
  UNINITIALIZED_TEST(RunWasmTurbofan_##name) {           \
    RunWasm_##name(TestExecutionTier::kTurbofan);        \
  }                                                      \
  UNINITIALIZED_TEST(RunWasmLiftoff_##name) {            \
    RunWasm_##name(TestExecutionTier::kLiftoff);         \
  }                                                      \
  void RunWasm_##name(TestExecutionTier execution_tier)

#define WASM_COMPILED_EXEC_TEST(name)                                          \
  void RunWasm_##name(TestExecutionTier execution_tier);                       \
  TEST(RunWasmTurbofan_##name) {                                               \
    RunWasm_##name(TestExecutionTier::kTurbofan);                              \
  }                                                                            \
  TEST(RunWasmLiftoff_##name) { RunWasm_##name(TestExecutionTier::kLiftoff); } \
  void RunWasm_##name(TestExecutionTier execution_tier)

}  // namespace v8::internal::wasm

#endif
