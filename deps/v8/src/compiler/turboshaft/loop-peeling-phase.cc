// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/loop-peeling-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/loop-peeling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/numbers/conversions-inl.h"

namespace v8::internal::compiler::turboshaft {

void LoopPeelingPhase::Run(PipelineData* data, Zone* temp_zone) {
  // Note that for wasm-gc it is relevant that the MachineOptimizationReducer is
  // run prior to other phases. Any attempt to skip the loop peeling phase (e.g.
  // if no loops are present) should evaluate how to run the
  // MachineOptimizationReducer then.
  turboshaft::CopyingPhase<turboshaft::LoopPeelingReducer,
                           turboshaft::MachineOptimizationReducer,
                           turboshaft::ValueNumberingReducer>::Run(data,
                                                                   temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
