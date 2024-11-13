// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_CSA_OPTIMIZE_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_CSA_OPTIMIZE_PHASE_H_

#include "src/compiler/turboshaft/phase.h"

namespace v8::internal::compiler::turboshaft {

struct CsaEarlyMachineOptimizationPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(CsaEarlyMachineOptimization)

  void Run(PipelineData* data, Zone* temp_zone);
};

struct CsaLoadEliminationPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(CsaLoadElimination)

  void Run(PipelineData* data, Zone* temp_zone);
};

struct CsaLateEscapeAnalysisPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(CsaLateEscapeAnalysis)

  void Run(PipelineData* data, Zone* temp_zone);
};

struct CsaBranchEliminationPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(CsaBranchElimination)

  void Run(PipelineData* data, Zone* temp_zone);
};

struct CsaOptimizePhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(CsaOptimize)

  void Run(PipelineData* data, Zone* temp_zone);
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_CSA_OPTIMIZE_PHASE_H_
