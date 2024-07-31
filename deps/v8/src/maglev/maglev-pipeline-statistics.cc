// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/maglev/maglev-pipeline-statistics.h"

#include "src/compiler/zone-stats.h"
#include "src/objects/js-function-inl.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {
namespace maglev {

constexpr char MaglevPipelineStatistics::kTraceCategory[];

MaglevPipelineStatistics::MaglevPipelineStatistics(
    maglev::MaglevCompilationInfo* info,
    std::shared_ptr<CompilationStatistics> compilation_stats,
    compiler::ZoneStats* zone_stats)
    : Base(info->zone(), zone_stats, compilation_stats, CodeKind::MAGLEV) {
  set_function_name(info->toplevel_function()->shared()->DebugNameCStr().get());
}

MaglevPipelineStatistics::~MaglevPipelineStatistics() {
  if (Base::InPhaseKind()) EndPhaseKind();
}

void MaglevPipelineStatistics::BeginPhaseKind(const char* name) {
  if (Base::InPhaseKind()) EndPhaseKind();
  Base::BeginPhaseKind(name);
  TRACE_EVENT_BEGIN1(kTraceCategory, name, "kind",
                     CodeKindToString(code_kind()));
}

void MaglevPipelineStatistics::EndPhaseKind() {
  CompilationStatistics::BasicStats diff;
  Base::EndPhaseKind(&diff);
  TRACE_EVENT_END2(kTraceCategory, phase_kind_name(), "kind",
                   CodeKindToString(code_kind()), "stats",
                   TRACE_STR_COPY(diff.AsJSON().c_str()));
}

void MaglevPipelineStatistics::BeginPhase(const char* name) {
  Base::BeginPhase(name);
  TRACE_EVENT_BEGIN1(kTraceCategory, phase_name(), "kind",
                     CodeKindToString(code_kind()));
}

void MaglevPipelineStatistics::EndPhase() {
  CompilationStatistics::BasicStats diff;
  Base::EndPhase(&diff);
  TRACE_EVENT_END2(kTraceCategory, phase_name(), "kind",
                   CodeKindToString(code_kind()), "stats",
                   TRACE_STR_COPY(diff.AsJSON().c_str()));
}

}  // namespace maglev
}  // namespace internal
}  // namespace v8
