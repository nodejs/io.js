// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-unrolling-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/loop-unrolling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/numbers/conversions-inl.h"

namespace v8::internal::compiler::turboshaft {

void LoopUnrollingPhase::Run(Zone* temp_zone) {
  LoopUnrollingAnalyzer analyzer(temp_zone, &PipelineData::Get().graph());
  if (analyzer.CanUnrollAtLeastOneLoop()) {
    PipelineData::Get().set_loop_unrolling_analyzer(&analyzer);
    turboshaft::CopyingPhase<turboshaft::LoopUnrollingReducer,
                             turboshaft::MachineOptimizationReducer,
                             turboshaft::ValueNumberingReducer>::Run(temp_zone);
    PipelineData::Get().clear_loop_unrolling_analyzer();
  }
}

}  // namespace v8::internal::compiler::turboshaft
