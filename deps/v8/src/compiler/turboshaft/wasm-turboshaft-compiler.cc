// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-turboshaft-compiler.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turbofan-graph-visualizer.h"
// TODO(14108): Remove.
#include "src/compiler/wasm-compiler.h"
#include "src/wasm/wasm-engine.h"

namespace v8::internal::compiler::turboshaft {

wasm::WasmCompilationResult ExecuteTurboshaftWasmCompilation(
    wasm::CompilationEnv* env, compiler::WasmCompilationData& data,
    wasm::WasmDetectedFeatures* detected, Counters* counters) {
  wasm::WasmCompilationResult result =
      Pipeline::GenerateWasmCode(env, data, detected, counters);
  if (result.failed()) return {};

  DCHECK_EQ(wasm::ExecutionTier::kTurbofan, result.result_tier);
  DCHECK_NULL(result.assumptions);
  result.assumptions = std::move(data.assumptions);
  DCHECK_IMPLIES(result.assumptions, !result.assumptions->empty());
  return result;
}

}  // namespace v8::internal::compiler::turboshaft
