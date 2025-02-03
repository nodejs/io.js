// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-in-js-inlining-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/wasm-in-js-inlining-reducer-inl.h"
#include "src/compiler/turboshaft/wasm-lowering-reducer.h"

namespace v8::internal::compiler::turboshaft {

void WasmInJSInliningPhase::Run(PipelineData* data, Zone* temp_zone) {
  UnparkedScopeIfNeeded scope(data->broker(), DEBUG_BOOL);

  // We need the `WasmLoweringReducer` for lowering, e.g., `global.get` etc.
  // TODO(dlehmann,353475584): Add Wasm GC (typed) optimizations also, see
  // `WasmGCTypedOptimizationReducer`.
  // This might need a separate phase due to the analysis in the input graph,
  // which is expensive, which is why we should enable this only conditionally.
  CopyingPhase<WasmInJSInliningReducer, WasmLoweringReducer>::Run(data,
                                                                  temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
