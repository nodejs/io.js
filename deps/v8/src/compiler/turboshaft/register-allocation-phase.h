// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_

#include "src/compiler/backend/frame-elider.h"
#include "src/compiler/backend/jump-threading.h"
#include "src/compiler/backend/move-optimizer.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/turboshaft/block-instrumentation-reducer.h"
#include "src/compiler/turboshaft/copying-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"

namespace v8::internal::compiler::turboshaft {

struct MeetRegisterConstraintsPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(MeetRegisterConstraints)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    ConstraintBuilder builder(data->register_allocation_data());
    builder.MeetRegisterConstraints();
  }
};

struct ResolvePhisPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(ResolvePhis)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    ConstraintBuilder builder(data->register_allocation_data());
    builder.ResolvePhis();
  }
};

struct BuildLiveRangesPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(BuildLiveRanges)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    LiveRangeBuilder builder(data->register_allocation_data(), temp_zone);
    builder.BuildLiveRanges();
  }
};

struct BuildLiveRangeBundlesPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(BuildLiveRangeBundles)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    BundleBuilder builder(data->register_allocation_data());
    builder.BuildBundles();
  }
};

template <typename RegAllocator>
struct AllocateGeneralRegistersPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(AllocateGeneralRegisters)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kGeneral, temp_zone);
    allocator.AllocateRegisters();
  }
};

template <typename RegAllocator>
struct AllocateFPRegistersPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(AllocateFPRegisters)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kDouble, temp_zone);
    allocator.AllocateRegisters();
  }
};

template <typename RegAllocator>
struct AllocateSimd128RegistersPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(AllocateSimd128Registers)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    RegAllocator allocator(data->register_allocation_data(),
                           RegisterKind::kSimd128, temp_zone);
    allocator.AllocateRegisters();
  }
};

struct DecideSpillingModePhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(DecideSpillingMode)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.DecideSpillingMode();
  }
};

struct AssignSpillSlotsPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(AssignSpillSlots)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.AssignSpillSlots();
  }
};

struct CommitAssignmentPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(CommitAssignment)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    OperandAssigner assigner(data->register_allocation_data());
    assigner.CommitAssignment();
  }
};

struct PopulateReferenceMapsPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(PopulateReferenceMaps)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    ReferenceMapPopulator populator(data->register_allocation_data());
    populator.PopulateReferenceMaps();
  }
};

struct ConnectRangesPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(ConnectRanges)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    LiveRangeConnector connector(data->register_allocation_data());
    connector.ConnectRanges(temp_zone);
  }
};

struct ResolveControlFlowPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(ResolveControlFlow)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    LiveRangeConnector connector(data->register_allocation_data());
    connector.ResolveControlFlow(temp_zone);
  }
};

struct OptimizeMovesPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(OptimizeMoves)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    MoveOptimizer move_optimizer(temp_zone, data->sequence());
    move_optimizer.Run();
  }
};

struct FrameElisionPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(FrameElision)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
#if V8_ENABLE_WEBASSEMBLY
    const bool is_wasm_to_js =
        data->info()->code_kind() == CodeKind::WASM_TO_JS_FUNCTION ||
        data->info()->builtin() == Builtin::kWasmToJsWrapperCSA;
#else
    const bool is_wasm_to_js = false;
#endif
    FrameElider(data->sequence(), false, is_wasm_to_js).Run();
  }
};

struct JumpThreadingPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(JumpThreading)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone, bool frame_at_start) {
    ZoneVector<RpoNumber> result(temp_zone);
    if (JumpThreading::ComputeForwarding(temp_zone, &result, data->sequence(),
                                         frame_at_start)) {
      JumpThreading::ApplyForwarding(temp_zone, result, data->sequence());
    }
  }
};

struct AssembleCodePhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(AssembleCode)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    CodeGenerator* code_generator = data->code_generator();
    DCHECK_NOT_NULL(code_generator);
    code_generator->AssembleCode();
  }
};

struct FinalizeCodePhase {
  DECL_TURBOSHAFT_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS_WITH_LEGACY_NAME(
      FinalizeCode)
  static constexpr bool kOutputIsTraceableGraph = false;

  void Run(PipelineData* data, Zone* temp_zone) {
    CodeGenerator* code_generator = data->code_generator();
    DCHECK_NOT_NULL(code_generator);
    data->set_code(code_generator->FinalizeCode());
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_REGISTER_ALLOCATION_PHASE_H_
