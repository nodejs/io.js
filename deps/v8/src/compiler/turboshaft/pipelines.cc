// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/pipelines.h"

#include "src/compiler/pipeline-data-inl.h"
#include "src/compiler/turboshaft/csa-optimize-phase.h"
#include "src/compiler/turboshaft/debug-feature-lowering-phase.h"
#include "src/compiler/turboshaft/instruction-selection-normalization-reducer.h"
#include "src/compiler/turboshaft/load-store-simplification-reducer.h"
#include "src/compiler/turboshaft/stack-check-lowering-reducer.h"

namespace v8::internal::compiler::turboshaft {

void SimplificationAndNormalizationPhase::Run(PipelineData* data,
                                              Zone* temp_zone) {
  CopyingPhase<LoadStoreSimplificationReducer,
               InstructionSelectionNormalizationReducer>::Run(data, temp_zone);
}

[[nodiscard]] bool Pipeline::GenerateCode(
    Linkage* linkage, std::shared_ptr<OsrHelper> osr_helper,
    JumpOptimizationInfo* jump_optimization_info,
    const ProfileDataFromFile* profile, int initial_graph_hash) {
  // Run code generation. If we optimize jumps, we repeat this a second time.
  data()->InitializeCodegenComponent(osr_helper, jump_optimization_info);

  // Perform instruction selection and register allocation.
  PrepareForInstructionSelection(profile);
  CHECK(SelectInstructions(linkage));
  CHECK(AllocateRegisters(linkage->GetIncomingDescriptor()));
  AssembleCode(linkage);

  if (v8_flags.turbo_profiling) {
    info()->profiler_data()->SetHash(initial_graph_hash);
  }

  if (jump_optimization_info && jump_optimization_info->is_optimizable()) {
    // Reset data for a second run of instruction selection.
    data()->ClearCodegenComponent();
    jump_optimization_info->set_optimizing();

    // Perform instruction selection and register allocation.
    data()->InitializeCodegenComponent(osr_helper, jump_optimization_info);
    if (!SelectInstructions(linkage)) {
      return false;
    }
    AllocateRegisters(linkage->GetIncomingDescriptor());
    // Generate the final machine code.
    AssembleCode(linkage);
  }
  return true;
}

void BuiltinPipeline::OptimizeBuiltin() {
  Tracing::Scope tracing_scope(data()->info());

  Run<CsaEarlyMachineOptimizationPhase>();
  Run<CsaLoadEliminationPhase>();
  Run<CsaLateEscapeAnalysisPhase>();
  Run<CsaBranchEliminationPhase>();
  Run<CsaOptimizePhase>();

  if (v8_flags.turboshaft_enable_debug_features) {
    Run<DebugFeatureLoweringPhase>();
  }

  Run<CodeEliminationAndSimplificationPhase>();
}

}  // namespace v8::internal::compiler::turboshaft
