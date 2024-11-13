// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PIPELINES_H_
#define V8_COMPILER_TURBOSHAFT_PIPELINES_H_

#include <optional>

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/register-allocator-verifier.h"
#include "src/compiler/basic-block-instrumentor.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/turboshaft/block-instrumentation-phase.h"
#include "src/compiler/turboshaft/build-graph-phase.h"
#include "src/compiler/turboshaft/code-elimination-and-simplification-phase.h"
#include "src/compiler/turboshaft/debug-feature-lowering-phase.h"
#include "src/compiler/turboshaft/decompression-optimization-phase.h"
#include "src/compiler/turboshaft/instruction-selection-phase.h"
#include "src/compiler/turboshaft/loop-peeling-phase.h"
#include "src/compiler/turboshaft/loop-unrolling-phase.h"
#include "src/compiler/turboshaft/machine-lowering-phase.h"
#include "src/compiler/turboshaft/maglev-graph-building-phase.h"
#include "src/compiler/turboshaft/optimize-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/register-allocation-phase.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/simplified-lowering-phase.h"
#include "src/compiler/turboshaft/store-store-elimination-phase.h"
#include "src/compiler/turboshaft/tracing.h"
#include "src/compiler/turboshaft/type-assertions-phase.h"
#include "src/compiler/turboshaft/typed-optimizations-phase.h"

namespace v8::internal::compiler::turboshaft {

inline constexpr char kTempZoneName[] = "temp-zone";

class Pipeline {
 public:
  explicit Pipeline(PipelineData* data) : data_(data) {}

  PipelineData* data() const { return data_; }
  void BeginPhaseKind(const char* phase_kind_name) {
    if (auto statistics = data()->pipeline_statistics()) {
      statistics->BeginPhaseKind(phase_kind_name);
    }
  }
  void EndPhaseKind() {
    if (auto statistics = data()->pipeline_statistics()) {
      statistics->EndPhaseKind();
    }
  }

  template <CONCEPT(TurboshaftPhase) Phase, typename... Args>
  auto Run(Args&&... args) {
    // Setup run scope.
    PhaseScope phase_scope(data_->pipeline_statistics(), Phase::phase_name());
    ZoneWithName<Phase::kPhaseName> temp_zone(data_->zone_stats(),
                                              Phase::phase_name());
    NodeOriginTable::PhaseScope origin_scope(data_->node_origins(),
                                             Phase::phase_name());
#ifdef V8_RUNTIME_CALL_STATS
    RuntimeCallTimerScope runtime_call_timer_scope(data_->runtime_call_stats(),
                                                   Phase::kRuntimeCallCounterId,
                                                   Phase::kCounterMode);
#endif

    Phase phase;
    using result_t =
        decltype(phase.Run(data_, temp_zone, std::forward<Args>(args)...));
    if constexpr (std::is_same_v<result_t, void>) {
      phase.Run(data_, temp_zone, std::forward<Args>(args)...);
      if constexpr (produces_printable_graph<Phase>::value) {
        PrintGraph(temp_zone, Phase::phase_name());
      }
      return;
    } else {
      auto result = phase.Run(data_, temp_zone, std::forward<Args>(args)...);
      if constexpr (produces_printable_graph<Phase>::value) {
        PrintGraph(temp_zone, Phase::phase_name());
      }
      return result;
    }
    UNREACHABLE();
  }

  void PrintGraph(Zone* zone, const char* phase_name) {
    CodeTracer* code_tracer = nullptr;
    if (data_->info()->trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      code_tracer = data_->GetCodeTracer();
      DCHECK_NOT_NULL(code_tracer);
    }
    PrintTurboshaftGraph(data_, zone, code_tracer, phase_name);
  }

  void TraceSequence(const char* phase_name) {
    if (info()->trace_turbo_json()) {
      UnparkedScopeIfNeeded scope(data()->broker());
      AllowHandleDereference allow_deref;
      TurboJsonFile json_of(info(), std::ios_base::app);
      json_of
          << "{\"name\":\"" << phase_name << "\",\"type\":\"sequence\""
          << ",\"blocks\":" << InstructionSequenceAsJSON{data()->sequence()}
          << ",\"register_allocation\":{"
          << RegisterAllocationDataAsJSON{*(data()->register_allocation_data()),
                                          *(data()->sequence())}
          << "}},\n";
    }
    if (info()->trace_turbo_graph()) {
      UnparkedScopeIfNeeded scope(data()->broker());
      AllowHandleDereference allow_deref;
      CodeTracer::StreamScope tracing_scope(data()->GetCodeTracer());
      tracing_scope.stream()
          << "----- Instruction sequence " << phase_name << " -----\n"
          << *data()->sequence();
    }
  }

  bool CreateGraphWithMaglev() {
    UnparkedScopeIfNeeded unparked_scope(data_->broker());

    BeginPhaseKind("V8.TFGraphCreation");
    turboshaft::Tracing::Scope tracing_scope(data_->info());
    std::optional<BailoutReason> bailout =
        Run<turboshaft::MaglevGraphBuildingPhase>();
    EndPhaseKind();

    if (bailout.has_value()) {
      data_->info()->AbortOptimization(bailout.value());
      return false;
    }

    return true;
  }

  bool CreateGraphFromTurbofan(compiler::TFPipelineData* turbofan_data,
                               Linkage* linkage) {
    CHECK_IMPLIES(!v8_flags.disable_optimizing_compilers, v8_flags.turboshaft);

    UnparkedScopeIfNeeded scope(data_->broker(),
                                v8_flags.turboshaft_trace_reduction ||
                                    v8_flags.turboshaft_trace_emitted);

    turboshaft::Tracing::Scope tracing_scope(data_->info());

    DCHECK(!v8_flags.turboshaft_from_maglev);
    if (std::optional<BailoutReason> bailout =
            Run<turboshaft::BuildGraphPhase>(turbofan_data, linkage)) {
      info()->AbortOptimization(*bailout);
      return false;
    }

    return true;
  }

  bool OptimizeTurboshaftGraph(Linkage* linkage) {
    UnparkedScopeIfNeeded scope(data_->broker(),
                                v8_flags.turboshaft_trace_reduction ||
                                    v8_flags.turboshaft_trace_emitted);

    turboshaft::Tracing::Scope tracing_scope(data_->info());

    if (v8_flags.turboshaft_frontend) {
      Run<turboshaft::SimplifiedLoweringPhase>();
    }

    Run<turboshaft::MachineLoweringPhase>();

    // TODO(dmercadier): find a way to merge LoopPeeling and LoopUnrolling. It's
    // not currently possible for 2 reasons. First, LoopPeeling reduces the
    // number of iteration of a loop, thus invalidating LoopUnrolling's
    // analysis. This could probably be worked around fairly easily though.
    // Second, LoopPeeling has to emit the non-peeled header of peeled loops, in
    // order to fix their loop phis (because their 1st input should be replace
    // by their 2nd input coming from the peeled iteration), but LoopUnrolling
    // has to be triggered before emitting the loop header. This could be fixed
    // by changing LoopUnrolling start unrolling after the 1st header has been
    // emitted, but this would also require updating CloneSubgraph.
    if (v8_flags.turboshaft_loop_peeling) {
      Run<turboshaft::LoopPeelingPhase>();
    }

    if (v8_flags.turboshaft_loop_unrolling) {
      Run<turboshaft::LoopUnrollingPhase>();
    }

    if (v8_flags.turbo_store_elimination) {
      Run<turboshaft::StoreStoreEliminationPhase>();
    }

    Run<turboshaft::OptimizePhase>();

    if (v8_flags.turboshaft_typed_optimizations) {
      Run<turboshaft::TypedOptimizationsPhase>();
    }

    if (v8_flags.turboshaft_assert_types) {
      Run<turboshaft::TypeAssertionsPhase>();
    }

    // Perform dead code elimination, reduce stack checks, simplify loads on
    // platforms where required, ...
    Run<turboshaft::CodeEliminationAndSimplificationPhase>();

#ifdef V8_ENABLE_DEBUG_CODE
    if (V8_UNLIKELY(v8_flags.turboshaft_enable_debug_features)) {
      // This phase has to run very late to allow all previous phases to use
      // debug features.
      Run<turboshaft::DebugFeatureLoweringPhase>();
    }
#endif  // V8_ENABLE_DEBUG_CODE

    return true;
  }

  void PrepareForInstructionSelection(
      const ProfileDataFromFile* profile = nullptr) {
    if (V8_UNLIKELY(data()->pipeline_kind() == TurboshaftPipelineKind::kCSA ||
                    data()->pipeline_kind() ==
                        TurboshaftPipelineKind::kTSABuiltin)) {
      if (profile) {
        Run<ProfileApplicationPhase>(profile);
      }

      if (v8_flags.reorder_builtins &&
          Builtins::IsBuiltinId(info()->builtin())) {
        UnparkedScopeIfNeeded unparked_scope(data()->broker());
        BasicBlockCallGraphProfiler::StoreCallGraph(info(), data()->graph());
      }

      if (v8_flags.turbo_profiling) {
        UnparkedScopeIfNeeded unparked_scope(data()->broker());

        // Basic block profiling disables concurrent compilation, so handle
        // deref is fine.
        AllowHandleDereference allow_handle_dereference;
        const size_t block_count = data()->graph().block_count();
        BasicBlockProfilerData* profiler_data =
            BasicBlockProfiler::Get()->NewData(block_count);

        // Set the function name.
        profiler_data->SetFunctionName(info()->GetDebugName());
        // Capture the schedule string before instrumentation.
        if (v8_flags.turbo_profiling_verbose) {
          std::ostringstream os;
          os << data()->graph();
          profiler_data->SetSchedule(os);
        }

        info()->set_profiler_data(profiler_data);

        Run<BlockInstrumentationPhase>();
      } else {
        // We run an empty copying phase to make sure that we have the same
        // control flow as when taking the profile.
        ZoneWithName<kTempZoneName> temp_zone(data()->zone_stats(),
                                              kTempZoneName);
        CopyingPhase<>::Run(data(), temp_zone);
      }
    }

    // DecompressionOptimization has to run as the last phase because it
    // constructs an (slightly) invalid graph that mixes Tagged and Compressed
    // representations.
    Run<DecompressionOptimizationPhase>();

    Run<SpecialRPOSchedulingPhase>();
  }

  [[nodiscard]] bool SelectInstructions(Linkage* linkage) {
    auto call_descriptor = linkage->GetIncomingDescriptor();

    // Depending on which code path led us to this function, the frame may or
    // may not have been initialized. If it hasn't yet, initialize it now.
    if (!data_->frame()) {
      data_->InitializeFrameData(call_descriptor);
    }

    // Select and schedule instructions covering the scheduled graph.
    CodeTracer* code_tracer = nullptr;
    if (info()->trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      code_tracer = data_->GetCodeTracer();
    }

    if (std::optional<BailoutReason> bailout = Run<InstructionSelectionPhase>(
            call_descriptor, linkage, code_tracer)) {
      data_->info()->AbortOptimization(*bailout);
      EndPhaseKind();
      return false;
    }

    return true;

    // TODO(nicohartmann@): We might need to provide this.
    // if (info()->trace_turbo_json()) {
    //   UnparkedScopeIfNeeded scope(turbofan_data->broker());
    //   AllowHandleDereference allow_deref;
    //   TurboCfgFile tcf(isolate());
    //   tcf << AsC1V("CodeGen", turbofan_data->schedule(),
    //                turbofan_data->source_positions(),
    //                turbofan_data->sequence());

    //   std::ostringstream source_position_output;
    //   // Output source position information before the graph is deleted.
    //   if (data_->source_positions() != nullptr) {
    //     data_->source_positions()->PrintJson(source_position_output);
    //   } else {
    //     source_position_output << "{}";
    //   }
    //   source_position_output << ",\n\"nodeOrigins\" : ";
    //   data_->node_origins()->PrintJson(source_position_output);
    //   data_->set_source_position_output(source_position_output.str());
    // }
  }

  bool AllocateRegisters(CallDescriptor* call_descriptor) {
    BeginPhaseKind("V8.TFRegisterAllocation");

    bool run_verifier = v8_flags.turbo_verify_allocation;

    // Allocate registers.
    const RegisterConfiguration* config = RegisterConfiguration::Default();
    std::unique_ptr<const RegisterConfiguration> restricted_config;
    if (call_descriptor->HasRestrictedAllocatableRegisters()) {
      RegList registers = call_descriptor->AllocatableRegisters();
      DCHECK_LT(0, registers.Count());
      restricted_config.reset(
          RegisterConfiguration::RestrictGeneralRegisters(registers));
      config = restricted_config.get();
    }
    AllocateRegisters(config, call_descriptor, run_verifier);

    // Verify the instruction sequence has the same hash in two stages.
    VerifyGeneratedCodeIsIdempotent();

    Run<FrameElisionPhase>();

    // TODO(mtrofin): move this off to the register allocator.
    bool generate_frame_at_start =
        data_->sequence()->instruction_blocks().front()->must_construct_frame();
    // Optimimize jumps.
    if (v8_flags.turbo_jt) {
      Run<JumpThreadingPhase>(generate_frame_at_start);
    }

    EndPhaseKind();

    return true;
  }

  bool MayHaveUnverifiableGraph() const {
    // TODO(nicohartmann): Are there any graph which are still verifiable?
    return true;
  }

  void VerifyGeneratedCodeIsIdempotent() {
    JumpOptimizationInfo* jump_opt = data()->jump_optimization_info();
    if (jump_opt == nullptr) return;

    InstructionSequence* code = data()->sequence();
    int instruction_blocks = code->InstructionBlockCount();
    int virtual_registers = code->VirtualRegisterCount();
    size_t hash_code =
        base::hash_combine(instruction_blocks, virtual_registers);
    for (Instruction* instr : *code) {
      hash_code = base::hash_combine(hash_code, instr->opcode(),
                                     instr->InputCount(), instr->OutputCount());
    }
    for (int i = 0; i < virtual_registers; i++) {
      hash_code = base::hash_combine(hash_code, code->GetRepresentation(i));
    }
    if (jump_opt->is_collecting()) {
      jump_opt->hash_code = hash_code;
    } else {
      CHECK_EQ(hash_code, jump_opt->hash_code);
    }
  }

  void AllocateRegisters(const RegisterConfiguration* config,
                         CallDescriptor* call_descriptor, bool run_verifier) {
    // Don't track usage for this zone in compiler stats.
    std::unique_ptr<Zone> verifier_zone;
    RegisterAllocatorVerifier* verifier = nullptr;
    if (run_verifier) {
      AccountingAllocator* allocator = data()->allocator();
      DCHECK_NOT_NULL(allocator);
      verifier_zone.reset(
          new Zone(allocator, kRegisterAllocatorVerifierZoneName));
      verifier = verifier_zone->New<RegisterAllocatorVerifier>(
          verifier_zone.get(), config, data()->sequence(), data()->frame());
    }

#ifdef DEBUG
    data_->sequence()->ValidateEdgeSplitForm();
    data_->sequence()->ValidateDeferredBlockEntryPaths();
    data_->sequence()->ValidateDeferredBlockExitPaths();
#endif

    data_->InitializeRegisterComponent(config, call_descriptor);

    Run<MeetRegisterConstraintsPhase>();
    Run<ResolvePhisPhase>();
    Run<BuildLiveRangesPhase>();
    Run<BuildLiveRangeBundlesPhase>();

    TraceSequence("before register allocation");
    if (verifier != nullptr) {
      CHECK(!data_->register_allocation_data()->ExistsUseWithoutDefinition());
      CHECK(data_->register_allocation_data()
                ->RangesDefinedInDeferredStayInDeferred());
    }

    if (data_->info()->trace_turbo_json() && !MayHaveUnverifiableGraph()) {
      TurboCfgFile tcf(data_->isolate());
      tcf << AsC1VRegisterAllocationData("PreAllocation",
                                         data_->register_allocation_data());
    }

    Run<AllocateGeneralRegistersPhase<LinearScanAllocator>>();

    if (data_->sequence()->HasFPVirtualRegisters()) {
      Run<AllocateFPRegistersPhase<LinearScanAllocator>>();
    }

    if (data_->sequence()->HasSimd128VirtualRegisters() &&
        (kFPAliasing == AliasingKind::kIndependent)) {
      Run<AllocateSimd128RegistersPhase<LinearScanAllocator>>();
    }

    Run<DecideSpillingModePhase>();
    Run<AssignSpillSlotsPhase>();
    Run<CommitAssignmentPhase>();

    // TODO(chromium:725559): remove this check once
    // we understand the cause of the bug. We keep just the
    // check at the end of the allocation.
    if (verifier != nullptr) {
      verifier->VerifyAssignment("Immediately after CommitAssignmentPhase.");
    }

    Run<ConnectRangesPhase>();

    Run<ResolveControlFlowPhase>();

    Run<PopulateReferenceMapsPhase>();

    if (v8_flags.turbo_move_optimization) {
      Run<OptimizeMovesPhase>();
    }

    TraceSequence("after register allocation");

    if (verifier != nullptr) {
      verifier->VerifyAssignment("End of regalloc pipeline.");
      verifier->VerifyGapMoves();
    }

    if (data_->info()->trace_turbo_json() && !MayHaveUnverifiableGraph()) {
      TurboCfgFile tcf(data_->isolate());
      tcf << AsC1VRegisterAllocationData("CodeGen",
                                         data_->register_allocation_data());
    }

    data()->ClearRegisterComponent();
  }

  void AssembleCode(Linkage* linkage) {
    BeginPhaseKind("V8.TFCodeGeneration");
    data()->InitializeCodeGenerator(linkage);

    UnparkedScopeIfNeeded unparked_scope(data()->broker());

    Run<AssembleCodePhase>();
    if (info()->trace_turbo_json()) {
      TurboJsonFile json_of(info(), std::ios_base::app);
      json_of
          << "{\"name\":\"code generation\"" << ", \"type\":\"instructions\""
          << InstructionStartsAsJSON{&data()->code_generator()->instr_starts()}
          << TurbolizerCodeOffsetsInfoAsJSON{
                 &data()->code_generator()->offsets_info()};
      json_of << "},\n";
    }

    data()->ClearInstructionComponent();
    EndPhaseKind();
  }

  MaybeHandle<Code> GenerateCode(CallDescriptor* call_descriptor) {
    Linkage linkage(call_descriptor);
    PrepareForInstructionSelection();
    if (!SelectInstructions(&linkage)) {
      return MaybeHandle<Code>();
    }
    AllocateRegisters(linkage.GetIncomingDescriptor());
    AssembleCode(&linkage);
    return FinalizeCode();
  }

  MaybeHandle<Code> GenerateCode(
      Linkage* linkage, std::shared_ptr<OsrHelper> osr_helper = {},
      JumpOptimizationInfo* jump_optimization_info = nullptr,
      const ProfileDataFromFile* profile = nullptr, int initial_graph_hash = 0);

  void RecreateTurbofanGraph(compiler::TFPipelineData* turbofan_data,
                             Linkage* linkage);

  OptimizedCompilationInfo* info() { return data_->info(); }

  MaybeHandle<Code> FinalizeCode(bool retire_broker = true) {
    BeginPhaseKind("V8.TFFinalizeCode");
    if (data_->broker() && retire_broker) {
      data_->broker()->Retire();
    }
    Run<FinalizeCodePhase>();

    MaybeHandle<Code> maybe_code = data_->code();
    Handle<Code> code;
    if (!maybe_code.ToHandle(&code)) {
      return maybe_code;
    }

    data_->info()->SetCode(code);
    PrintCode(data_->isolate(), code, data_->info());

    // Functions with many inline candidates are sensitive to correct call
    // frequency feedback and should therefore not be tiered up early.
    if (v8_flags.profile_guided_optimization &&
        info()->could_not_inline_all_candidates()) {
      info()->shared_info()->set_cached_tiering_decision(
          CachedTieringDecision::kNormal);
    }

    if (info()->trace_turbo_json()) {
      TurboJsonFile json_of(info(), std::ios_base::app);

      json_of << "{\"name\":\"disassembly\",\"type\":\"disassembly\""
              << BlockStartsAsJSON{&data_->code_generator()->block_starts()}
              << "\"data\":\"";
#ifdef ENABLE_DISASSEMBLER
      std::stringstream disassembly_stream;
      code->Disassemble(nullptr, disassembly_stream, data_->isolate());
      std::string disassembly_string(disassembly_stream.str());
      for (const auto& c : disassembly_string) {
        json_of << AsEscapedUC16ForJSON(c);
      }
#endif  // ENABLE_DISASSEMBLER
      json_of << "\"}\n],\n";
      json_of << "\"nodePositions\":";
      // TODO(nicohartmann): We should try to always provide source positions.
      json_of << (data_->source_position_output().empty()
                      ? "{}"
                      : data_->source_position_output())
              << ",\n";
      JsonPrintAllSourceWithPositions(json_of, data_->info(), data_->isolate());
      if (info()->has_bytecode_array()) {
        json_of << ",\n";
        JsonPrintAllBytecodeSources(json_of, info());
      }
      json_of << "\n}";
    }
    if (info()->trace_turbo_json() || info()->trace_turbo_graph()) {
      CodeTracer::StreamScope tracing_scope(data_->GetCodeTracer());
      tracing_scope.stream()
          << "---------------------------------------------------\n"
          << "Finished compiling method " << info()->GetDebugName().get()
          << " using TurboFan" << std::endl;
    }
    EndPhaseKind();
    return code;
  }

  bool CommitDependencies(Handle<Code> code) {
    return data_->depedencies() == nullptr ||
           data_->depedencies()->Commit(code);
  }

 private:
  PipelineData* data_;
};

class BuiltinPipeline : public Pipeline {
 public:
  explicit BuiltinPipeline(PipelineData* data) : Pipeline(data) {}

  void OptimizeBuiltin();
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PIPELINES_H_
