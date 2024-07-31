// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/wasm-revec-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/wasm-revec-reducer.h"

namespace v8::internal::compiler::turboshaft {

void WasmRevecPhase::Run(Zone* temp_zone) {
  WasmRevecAnalyzer analyzer(temp_zone, PipelineData::Get().graph());

  if (analyzer.ShouldReduce()) {
    PipelineData::Get().set_wasm_revec_analyzer(&analyzer);
    UnparkedScopeIfNeeded scope(PipelineData::Get().broker(),
                                v8_flags.turboshaft_trace_reduction);
    CopyingPhase<WasmRevecReducer>::Run(temp_zone);
    PipelineData::Get().clear_wasm_revec_analyzer();
  }
}

}  // namespace v8::internal::compiler::turboshaft
