// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/simplified-lowering-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/simplified-lowering-reducer.h"

namespace v8::internal::compiler::turboshaft {

void SimplifiedLoweringPhase::Run(PipelineData* data, Zone* temp_zone) {
  CopyingPhase<SimplifiedLoweringReducer>::Run(data, temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
