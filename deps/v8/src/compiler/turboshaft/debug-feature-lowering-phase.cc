// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/debug-feature-lowering-phase.h"

#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/debug-feature-lowering-reducer.h"

namespace v8::internal::compiler::turboshaft {

void DebugFeatureLoweringPhase::Run(PipelineData* data, Zone* temp_zone) {
#ifdef V8_ENABLE_DEBUG_CODE
  turboshaft::CopyingPhase<turboshaft::DebugFeatureLoweringReducer>::Run(
      data, temp_zone);
#endif
}

}  // namespace v8::internal::compiler::turboshaft
