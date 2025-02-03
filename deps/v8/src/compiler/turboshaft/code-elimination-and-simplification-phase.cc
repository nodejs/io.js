// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/code-elimination-and-simplification-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/dead-code-elimination-reducer.h"
#include "src/compiler/turboshaft/duplication-optimization-reducer.h"
#include "src/compiler/turboshaft/instruction-selection-normalization-reducer.h"
#include "src/compiler/turboshaft/load-store-simplification-reducer.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/stack-check-lowering-reducer.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/turboshaft/wasm-js-lowering-reducer.h"
#endif

namespace v8::internal::compiler::turboshaft {

void CodeEliminationAndSimplificationPhase::Run(PipelineData* data,
                                                Zone* temp_zone) {
  UnparkedScopeIfNeeded scope(data->broker(), DEBUG_BOOL);

  CopyingPhase<DeadCodeEliminationReducer, StackCheckLoweringReducer,
#if V8_ENABLE_WEBASSEMBLY
               WasmJSLoweringReducer,
#endif
               LoadStoreSimplificationReducer,
               // We make sure that DuplicationOptimizationReducer runs after
               // LoadStoreSimplificationReducer, so that it can optimize
               // Loads/Stores produced by LoadStoreSimplificationReducer
               // (which, for simplificy, doesn't use the Assembler helper
               // methods, but only calls Next::ReduceLoad/Store).
               DuplicationOptimizationReducer,
               InstructionSelectionNormalizationReducer,
               ValueNumberingReducer>::Run(data, temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
