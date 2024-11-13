// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/machine-lowering-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/dataview-lowering-reducer.h"
#include "src/compiler/turboshaft/fast-api-call-lowering-reducer.h"
#include "src/compiler/turboshaft/js-generic-lowering-reducer.h"
#include "src/compiler/turboshaft/machine-lowering-reducer-inl.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/select-lowering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"

namespace v8::internal::compiler::turboshaft {

void MachineLoweringPhase::Run(PipelineData* data, Zone* temp_zone) {
  // TODO(dmercadier): It would make sense to run JSGenericLoweringReducer
  // during SimplifiedLowering. However, SimplifiedLowering is currently WIP,
  // and it would be better to not tie the Maglev graph builder to
  // SimplifiedLowering just yet, so I'm hijacking MachineLoweringPhase to run
  // JSGenericLoweringReducer without requiring a whole phase just for that.
  CopyingPhase<JSGenericLoweringReducer, DataViewLoweringReducer,
               MachineLoweringReducer, FastApiCallLoweringReducer,
               SelectLoweringReducer,
               MachineOptimizationReducer>::Run(data, temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
