// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_FUNCTION_COMPILER_H_
#define V8_WASM_FUNCTION_COMPILER_H_

#include <memory>

#include "src/codegen/assembler.h"
#include "src/codegen/code-desc.h"
#include "src/codegen/compiler.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/wasm-deopt-data.h"
#include "src/wasm/wasm-limits.h"
#include "src/wasm/wasm-module.h"
#include "src/wasm/wasm-tier.h"

namespace v8 {
namespace internal {

class Counters;
class TurbofanCompilationJob;

namespace wasm {

class NativeModule;
class WasmCode;
class WasmEngine;
struct WasmFunction;

// Stores assumptions that a Wasm compilation job made while executing,
// so they can be checked for continued validity when the job finishes.
class AssumptionsJournal {
 public:
  AssumptionsJournal() = default;

  void RecordAssumption(uint32_t func_index, WellKnownImport status) {
    imports_.push_back(std::make_pair(func_index, status));
  }

  const std::vector<std::pair<uint32_t, WellKnownImport>>& import_statuses() {
    return imports_;
  }

  bool empty() const { return imports_.empty(); }

 private:
  // This is not particularly efficient, but it's probably good enough.
  // For most compilations, this won't hold any entries. If it does
  // hold entries, their number is expected to be small, because most
  // functions don't call many imports, and many imports won't be
  // specially recognized.
  std::vector<std::pair<uint32_t, WellKnownImport>> imports_;
};

struct WasmCompilationResult {
 public:
  MOVE_ONLY_WITH_DEFAULT_CONSTRUCTORS(WasmCompilationResult);

  enum Kind : int8_t {
    kFunction,
    kWasmToJsWrapper,
#if V8_ENABLE_DRUMBRAKE
    kInterpreterEntry,
#endif  // V8_ENABLE_DRUMBRAKE
  };

  bool succeeded() const { return code_desc.buffer != nullptr; }
  bool failed() const { return !succeeded(); }
  explicit operator bool() const { return succeeded(); }

  CodeDesc code_desc;
  std::unique_ptr<AssemblerBuffer> instr_buffer;
  uint32_t frame_slot_count = 0;
  uint32_t ool_spill_count = 0;
  uint32_t tagged_parameter_slots = 0;
  base::OwnedVector<uint8_t> source_positions;
  base::OwnedVector<uint8_t> inlining_positions;
  base::OwnedVector<uint8_t> protected_instructions_data;
  base::OwnedVector<uint8_t> deopt_data;
  std::unique_ptr<AssumptionsJournal> assumptions;
  std::unique_ptr<LiftoffFrameDescriptionForDeopt> liftoff_frame_descriptions;
  int func_index = kAnonymousFuncIndex;
  ExecutionTier result_tier = ExecutionTier::kNone;
  Kind kind = kFunction;
  ForDebugging for_debugging = kNotForDebugging;
  bool frame_has_feedback_slot = false;
};

class V8_EXPORT_PRIVATE WasmCompilationUnit final {
 public:
  WasmCompilationUnit(int index, ExecutionTier tier, ForDebugging for_debugging)
      : func_index_(index), tier_(tier), for_debugging_(for_debugging) {
    DCHECK_IMPLIES(for_debugging != ForDebugging::kNotForDebugging,
                   tier_ == ExecutionTier::kLiftoff);
  }

  WasmCompilationResult ExecuteCompilation(CompilationEnv*,
                                           const WireBytesStorage*, Counters*,
                                           WasmDetectedFeatures* detected);

  ExecutionTier tier() const { return tier_; }
  ForDebugging for_debugging() const { return for_debugging_; }
  int func_index() const { return func_index_; }

  static void CompileWasmFunction(Counters*, NativeModule*,
                                  WasmDetectedFeatures* detected,
                                  const WasmFunction*, ExecutionTier);

 private:
  WasmCompilationResult ExecuteFunctionCompilation(
      CompilationEnv*, const WireBytesStorage*, Counters*,
      WasmDetectedFeatures* detected);

  WasmCompilationResult ExecuteImportWrapperCompilation(CompilationEnv*);

  int func_index_;
  ExecutionTier tier_;
  ForDebugging for_debugging_;
};

// {WasmCompilationUnit} should be trivially copyable and small enough so we can
// efficiently pass it by value.
ASSERT_TRIVIALLY_COPYABLE(WasmCompilationUnit);
static_assert(sizeof(WasmCompilationUnit) <= 2 * kSystemPointerSize);

class V8_EXPORT_PRIVATE JSToWasmWrapperCompilationUnit final {
 public:
  JSToWasmWrapperCompilationUnit(Isolate* isolate, const FunctionSig* sig,
                                 uint32_t canonical_sig_index,
                                 const wasm::WasmModule* module,
                                 WasmEnabledFeatures enabled_features);
  ~JSToWasmWrapperCompilationUnit();

  // Allow move construction and assignment, for putting units in a std::vector.
  JSToWasmWrapperCompilationUnit(JSToWasmWrapperCompilationUnit&&)
      V8_NOEXCEPT = default;
  JSToWasmWrapperCompilationUnit& operator=(JSToWasmWrapperCompilationUnit&&)
      V8_NOEXCEPT = default;

  Isolate* isolate() const { return isolate_; }

  void Execute();
  Handle<Code> Finalize();

  const FunctionSig* sig() const { return sig_; }
  uint32_t canonical_sig_index() const { return canonical_sig_index_; }

  // Run a compilation unit synchronously.
  static Handle<Code> CompileJSToWasmWrapper(Isolate* isolate,
                                             const FunctionSig* sig,
                                             uint32_t canonical_sig_index,
                                             const WasmModule* module);

 private:
  // Wrapper compilation is bound to an isolate. Concurrent accesses to the
  // isolate (during the "Execute" phase) must be audited carefully, i.e. we
  // should only access immutable information (like the root table). The isolate
  // is guaranteed to be alive when this unit executes.
  Isolate* isolate_;
  const FunctionSig* sig_;
  uint32_t canonical_sig_index_;
  std::unique_ptr<OptimizedCompilationJob> job_;
};

inline bool CanUseGenericJsToWasmWrapper(const WasmModule* module,
                                         const FunctionSig* sig) {
#if (V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_ARM64 || V8_TARGET_ARCH_IA32 ||  \
     V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_PPC64 || \
     V8_TARGET_ARCH_LOONG64)
  // We don't use the generic wrapper for asm.js, because it creates invalid
  // stack traces.
  return !is_asmjs_module(module) && v8_flags.wasm_generic_wrapper &&
         IsJSCompatibleSignature(sig);
#else
  return false;
#endif
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_FUNCTION_COMPILER_H_
