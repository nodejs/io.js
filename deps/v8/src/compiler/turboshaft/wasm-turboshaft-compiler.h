// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_TURBOSHAFT_WASM_TURBOSHAFT_COMPILER_H_
#define V8_COMPILER_TURBOSHAFT_WASM_TURBOSHAFT_COMPILER_H_

#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"

namespace v8::internal::wasm {
struct CompilationEnv;
struct WasmCompilationResult;
class WasmFeatures;
}  // namespace v8::internal::wasm

namespace v8::internal::compiler {
struct WasmCompilationData;

namespace turboshaft {

wasm::WasmCompilationResult ExecuteTurboshaftWasmCompilation(
    wasm::CompilationEnv* env, WasmCompilationData& data,
    wasm::WasmFeatures* detected);

class TurboshaftCompilationJob : public OptimizedCompilationJob {
 public:
  TurboshaftCompilationJob(OptimizedCompilationInfo* compilation_info,
                           State initial_state)
      : OptimizedCompilationJob("Turboshaft", initial_state),
        compilation_info_(compilation_info) {}

  OptimizedCompilationInfo* compilation_info() const {
    return compilation_info_;
  }

 private:
  OptimizedCompilationInfo* const compilation_info_;
};

}  // namespace turboshaft

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_TURBOSHAFT_WASM_TURBOSHAFT_COMPILER_H_
