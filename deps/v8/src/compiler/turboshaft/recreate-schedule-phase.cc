// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/recreate-schedule-phase.h"

#include "src/compiler/pipeline-data-inl.h"

namespace v8::internal::compiler::turboshaft {

RecreateScheduleResult RecreateSchedulePhase::Run(
    PipelineData* data, Zone* temp_zone,
    compiler::TFPipelineData* turbofan_data, Linkage* linkage) {
  const size_t node_count_estimate =
      static_cast<size_t>(1.1 * data->graph().op_id_count());

  turbofan_data->InitializeWithGraphZone(
      std::move(data->graph_zone()), data->source_positions(),
      data->node_origins(), node_count_estimate);

  auto result = RecreateSchedule(data, turbofan_data,
                                 linkage->GetIncomingDescriptor(), temp_zone);

  // Delete GraphComponent because its content is now owned by {turbofan_data}.
  data->ClearGraphComponent();

  return result;
}

}  // namespace v8::internal::compiler::turboshaft
