// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/typed-optimizations-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/type-inference-reducer.h"
#include "src/compiler/turboshaft/typed-optimizations-reducer.h"

namespace v8::internal::compiler::turboshaft {

void TypedOptimizationsPhase::Run(Zone* temp_zone) {
#ifdef DEBUG
  UnparkedScopeIfNeeded scope(PipelineData::Get().broker(),
                              v8_flags.turboshaft_trace_typing);
#endif

  turboshaft::TypeInferenceReducerArgs::Scope typing_args{
      turboshaft::TypeInferenceReducerArgs::InputGraphTyping::kPrecise,
      turboshaft::TypeInferenceReducerArgs::OutputGraphTyping::kNone};

  turboshaft::CopyingPhase<turboshaft::TypedOptimizationsReducer,
                           turboshaft::TypeInferenceReducer>::Run(temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
