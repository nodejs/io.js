// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/store-store-elimination-phase.h"

#include "src/compiler/turboshaft/branch-elimination-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/late-load-elimination-reducer.h"
#include "src/compiler/turboshaft/loop-unrolling-reducer.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/store-store-elimination-reducer-inl.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/numbers/conversions-inl.h"

namespace v8::internal::compiler::turboshaft {

void StoreStoreEliminationPhase::Run(PipelineData* data, Zone* temp_zone) {
  turboshaft::CopyingPhase<
      LoopStackCheckElisionReducer, StoreStoreEliminationReducer,
      LateLoadEliminationReducer, MachineOptimizationReducer,
      BranchEliminationReducer, ValueNumberingReducer>::Run(data, temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
