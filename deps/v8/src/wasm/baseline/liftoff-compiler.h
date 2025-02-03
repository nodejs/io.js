// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
#define V8_WASM_BASELINE_LIFTOFF_COMPILER_H_

#include "src/wasm/function-compiler.h"

namespace v8 {
namespace internal {

class AccountingAllocator;
class Counters;

namespace wasm {

struct CompilationEnv;
class DebugSideTable;
struct FunctionBody;
class WasmDetectedFeatures;

// Note: If this list changes, also the histogram "V8.LiftoffBailoutReasons"
// on the chromium side needs to be updated.
// Deprecating entries is always fine. Repurposing works if you don't care about
// temporary mix-ups. Increasing the number of reasons {kNumBailoutReasons} is
// more tricky, and might require introducing a new (updated) histogram.
enum LiftoffBailoutReason : int8_t {
  // Nothing actually failed.
  kSuccess = 0,
  // Compilation failed, but not because of Liftoff.
  kDecodeError = 1,
  // Liftoff is not implemented on that architecture.
  kUnsupportedArchitecture = 2,
  // More complex code would be needed because a CPU feature is not present.
  kMissingCPUFeature = 3,
  // Liftoff does not implement a complex (and rare) instruction.
  kComplexOperation = 4,
  // Unimplemented proposals:
  kSimd = 5,
  kRefTypes = 6,
  kExceptionHandling = 7,
  kMultiValue = 8,
  kTailCall = 9,
  kAtomics = 10,
  kBulkMemory = 11,
  kNonTrappingFloatToInt = 12,
  kGC = 13,
  kRelaxedSimd = 14,
  // A little gap, for forward compatibility.
  // Any other reason (use rarely; introduce new reasons if this spikes).
  kOtherReason = 20,
  // Marker:
  kNumBailoutReasons
};

struct LiftoffOptions {
  int func_index = -1;
  ForDebugging for_debugging = kNotForDebugging;
  Counters* counters = nullptr;
  WasmDetectedFeatures* detected_features = nullptr;
  base::Vector<const int> breakpoints = {};
  std::unique_ptr<DebugSideTable>* debug_sidetable = nullptr;
  int dead_breakpoint = 0;
  int32_t* max_steps = nullptr;
  int32_t* nondeterminism = nullptr;

  // Check that all non-optional fields have been initialized.
  bool is_initialized() const { return func_index >= 0; }

  // We keep the macro as small as possible by offloading the actual DCHECK and
  // assignment to another function. This makes debugging easier.
#define SETTER(field)                                               \
  LiftoffOptions& set_##field(decltype(field) new_value) {          \
    return Set<decltype(field)>(&LiftoffOptions::field, new_value); \
  }

  SETTER(func_index)
  SETTER(for_debugging)
  SETTER(counters)
  SETTER(detected_features)
  SETTER(breakpoints)
  SETTER(debug_sidetable)
  SETTER(dead_breakpoint)
  SETTER(max_steps)
  SETTER(nondeterminism)

#undef SETTER

 private:
  template <typename T>
  LiftoffOptions& Set(T LiftoffOptions::*field_ptr, T new_value) {
    // The field must still have its default value (set each field only once).
    DCHECK_EQ(this->*field_ptr, LiftoffOptions{}.*field_ptr);
    this->*field_ptr = new_value;
    return *this;
  }
};

V8_EXPORT_PRIVATE WasmCompilationResult ExecuteLiftoffCompilation(
    CompilationEnv*, const FunctionBody&, const LiftoffOptions&);

V8_EXPORT_PRIVATE std::unique_ptr<DebugSideTable> GenerateLiftoffDebugSideTable(
    const WasmCode*);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
