// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_MAGLEV_MAGLEV_PIPELINE_STATISTICS_H_
#define V8_MAGLEV_MAGLEV_PIPELINE_STATISTICS_H_

#ifdef V8_ENABLE_MAGLEV

#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/zone-stats.h"
#include "src/diagnostics/compilation-statistics.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/tracing/trace-event.h"

namespace v8 {
namespace internal {
namespace maglev {

class MaglevPipelineStatistics : public compiler::PipelineStatisticsBase,
                                 public Malloced {
 public:
  MaglevPipelineStatistics(maglev::MaglevCompilationInfo* info,
                           std::shared_ptr<CompilationStatistics> stats,
                           compiler::ZoneStats* zone_stats);
  ~MaglevPipelineStatistics();
  MaglevPipelineStatistics(const MaglevPipelineStatistics&) = delete;
  MaglevPipelineStatistics& operator=(const MaglevPipelineStatistics&) = delete;

  static constexpr char kTraceCategory[] =
      TRACE_DISABLED_BY_DEFAULT("v8.maglev");

  void BeginPhaseKind(const char* name);
  void EndPhaseKind();
  void BeginPhase(const char* name);
  void EndPhase();
};

}  // namespace maglev
}  // namespace internal
}  // namespace v8

#endif  // V8_ENABLE_MAGLEV

#endif  // V8_MAGLEV_MAGLEV_PIPELINE_STATISTICS_H_
