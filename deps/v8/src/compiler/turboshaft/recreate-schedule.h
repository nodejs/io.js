// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_
#define V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_

#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/node-origin-table.h"

namespace v8::internal {
class Zone;
}
namespace v8::internal::compiler {
class Schedule;
class Graph;
class CallDescriptor;
class TFPipelineData;
}  // namespace v8::internal::compiler
namespace v8::internal::compiler::turboshaft {
class Graph;
class PipelineData;

struct RecreateScheduleResult {
  compiler::Graph* graph;
  Schedule* schedule;
};

RecreateScheduleResult RecreateSchedule(PipelineData* data,
                                        compiler::TFPipelineData* turbofan_data,
                                        CallDescriptor* call_descriptor,
                                        Zone* phase_zone);

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_RECREATE_SCHEDULE_H_
