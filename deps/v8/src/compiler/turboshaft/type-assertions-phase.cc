// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/type-assertions-phase.h"

#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/assert-types-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/type-inference-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"

namespace v8::internal::compiler::turboshaft {

void TypeAssertionsPhase::Run(PipelineData* data, Zone* temp_zone) {
  UnparkedScopeIfNeeded scope(data->broker());

  turboshaft::TypeInferenceReducerArgs::Scope typing_args{
      turboshaft::TypeInferenceReducerArgs::InputGraphTyping::kPrecise,
      turboshaft::TypeInferenceReducerArgs::OutputGraphTyping::
          kPreserveFromInputGraph};

  turboshaft::CopyingPhase<turboshaft::AssertTypesReducer,
                           turboshaft::ValueNumberingReducer,
                           turboshaft::TypeInferenceReducer>::Run(data,
                                                                  temp_zone);
}

}  // namespace v8::internal::compiler::turboshaft
