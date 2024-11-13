
// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_DATA_INL_H_
#define V8_COMPILER_PIPELINE_DATA_INL_H_

#include <optional>

#include "src/builtins/profile-data-reader.h"
#include "src/codegen/assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/phase.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/schedule.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/zone-with-name.h"
#include "src/compiler/typer.h"
#include "src/compiler/zone-stats.h"
#include "src/execution/isolate.h"
#include "src/handles/handles-inl.h"
#include "src/objects/objects-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif

namespace v8::internal::compiler {

inline Maybe<OuterContext> GetModuleContext(OptimizedCompilationInfo* info) {
  Tagged<Context> current = info->closure()->context();
  size_t distance = 0;
  while (!IsNativeContext(*current)) {
    if (current->IsModuleContext()) {
      return Just(OuterContext(
          info->CanonicalHandle(current, current->GetIsolate()), distance));
    }
    current = current->previous();
    distance++;
  }
  return Nothing<OuterContext>();
}

class TFPipelineData {
 public:
  // For main entry point.
  TFPipelineData(ZoneStats* zone_stats, Isolate* isolate,
                 OptimizedCompilationInfo* info,
                 TurbofanPipelineStatistics* pipeline_statistics)
      : isolate_(isolate),
        allocator_(isolate->allocator()),
        info_(info),
        debug_name_(info_->GetDebugName()),
        may_have_unverifiable_graph_(v8_flags.turboshaft),
        zone_stats_(zone_stats),
        pipeline_statistics_(pipeline_statistics),
        graph_zone_(zone_stats_, kGraphZoneName, kCompressGraphZone),
        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        instruction_zone_(instruction_zone_scope_.zone()),
        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        codegen_zone_(codegen_zone_scope_.zone()),
        broker_(new JSHeapBroker(isolate_, info_->zone(),
                                 info_->trace_heap_broker(),
                                 info->code_kind())),
        register_allocation_zone_scope_(zone_stats_,
                                        kRegisterAllocationZoneName),
        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        assembler_options_(AssemblerOptions::Default(isolate)) {
    PhaseScope scope(pipeline_statistics, "V8.TFInitPipelineData");
    graph_ = graph_zone_->New<Graph>(graph_zone_);
    source_positions_ = graph_zone_->New<SourcePositionTable>(graph_);
    node_origins_ = info->trace_turbo_json()
                        ? graph_zone_->New<NodeOriginTable>(graph_)
                        : nullptr;
    simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
    machine_ = graph_zone_->New<MachineOperatorBuilder>(
        graph_zone_, MachineType::PointerRepresentation(),
        InstructionSelector::SupportedMachineOperatorFlags(),
        InstructionSelector::AlignmentRequirements());
    common_ = graph_zone_->New<CommonOperatorBuilder>(graph_zone_);
    javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
    jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_, javascript_,
                                         simplified_, machine_);
    observe_node_manager_ =
        info->node_observer()
            ? graph_zone_->New<ObserveNodeManager>(graph_zone_)
            : nullptr;
    dependencies_ = info_->zone()->New<CompilationDependencies>(broker_.get(),
                                                                info_->zone());
  }

#if V8_ENABLE_WEBASSEMBLY
  // For WebAssembly compile entry point.
  TFPipelineData(ZoneStats* zone_stats, wasm::WasmEngine* wasm_engine,
                 OptimizedCompilationInfo* info, MachineGraph* mcgraph,
                 TurbofanPipelineStatistics* pipeline_statistics,
                 SourcePositionTable* source_positions,
                 NodeOriginTable* node_origins,
                 const AssemblerOptions& assembler_options)
      : isolate_(nullptr),
        wasm_engine_(wasm_engine),
        allocator_(wasm_engine->allocator()),
        info_(info),
        debug_name_(info_->GetDebugName()),
        may_have_unverifiable_graph_(v8_flags.turboshaft_wasm),
        zone_stats_(zone_stats),
        pipeline_statistics_(pipeline_statistics),
        graph_zone_(zone_stats_, kGraphZoneName, kCompressGraphZone),
        graph_(mcgraph->graph()),
        source_positions_(source_positions),
        node_origins_(node_origins),
        machine_(mcgraph->machine()),
        common_(mcgraph->common()),
        mcgraph_(mcgraph),
        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        instruction_zone_(instruction_zone_scope_.zone()),
        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        codegen_zone_(codegen_zone_scope_.zone()),
        register_allocation_zone_scope_(zone_stats_,
                                        kRegisterAllocationZoneName),
        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        assembler_options_(assembler_options) {
    simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
    javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
    jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_, javascript_,
                                         simplified_, machine_);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // For CodeStubAssembler and machine graph testing entry point.
  TFPipelineData(ZoneStats* zone_stats, OptimizedCompilationInfo* info,
                 Isolate* isolate, AccountingAllocator* allocator, Graph* graph,
                 JSGraph* jsgraph, Schedule* schedule,
                 SourcePositionTable* source_positions,
                 NodeOriginTable* node_origins, JumpOptimizationInfo* jump_opt,
                 const AssemblerOptions& assembler_options,
                 const ProfileDataFromFile* profile_data)
      : isolate_(isolate),
#if V8_ENABLE_WEBASSEMBLY
        // TODO(clemensb): Remove this field, use GetWasmEngine directly
        // instead.
        wasm_engine_(wasm::GetWasmEngine()),
#endif  // V8_ENABLE_WEBASSEMBLY
        allocator_(allocator),
        info_(info),
        debug_name_(info_->GetDebugName()),
        zone_stats_(zone_stats),
        graph_zone_(zone_stats_, kGraphZoneName, kCompressGraphZone),
        graph_(graph),
        source_positions_(source_positions),
        node_origins_(node_origins),
        schedule_(schedule),
        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        instruction_zone_(instruction_zone_scope_.zone()),
        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        codegen_zone_(codegen_zone_scope_.zone()),
        register_allocation_zone_scope_(zone_stats_,
                                        kRegisterAllocationZoneName),
        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        jump_optimization_info_(jump_opt),
        assembler_options_(assembler_options),
        profile_data_(profile_data) {
    if (jsgraph) {
      jsgraph_ = jsgraph;
      simplified_ = jsgraph->simplified();
      machine_ = jsgraph->machine();
      common_ = jsgraph->common();
      javascript_ = jsgraph->javascript();
    } else if (graph_) {
      simplified_ = graph_zone_->New<SimplifiedOperatorBuilder>(graph_zone_);
      machine_ = graph_zone_->New<MachineOperatorBuilder>(
          graph_zone_, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements());
      common_ = graph_zone_->New<CommonOperatorBuilder>(graph_zone_);
      javascript_ = graph_zone_->New<JSOperatorBuilder>(graph_zone_);
      jsgraph_ = graph_zone_->New<JSGraph>(isolate_, graph_, common_,
                                           javascript_, simplified_, machine_);
    }
  }

  // For register allocation testing entry point.
  TFPipelineData(ZoneStats* zone_stats, OptimizedCompilationInfo* info,
                 Isolate* isolate, InstructionSequence* sequence)
      : isolate_(isolate),
        allocator_(isolate->allocator()),
        info_(info),
        debug_name_(info_->GetDebugName()),
        zone_stats_(zone_stats),
        graph_zone_(zone_stats_, kGraphZoneName, kCompressGraphZone),
        instruction_zone_scope_(zone_stats_, kInstructionZoneName),
        instruction_zone_(sequence->zone()),
        sequence_(sequence),
        codegen_zone_scope_(zone_stats_, kCodegenZoneName),
        codegen_zone_(codegen_zone_scope_.zone()),
        register_allocation_zone_scope_(zone_stats_,
                                        kRegisterAllocationZoneName),
        register_allocation_zone_(register_allocation_zone_scope_.zone()),
        assembler_options_(AssemblerOptions::Default(isolate)) {}

  ~TFPipelineData() {
    // Must happen before zones are destroyed.
    delete code_generator_;
    code_generator_ = nullptr;
    DeleteTyper();
    DeleteRegisterAllocationZone();
    DeleteInstructionZone();
    DeleteCodegenZone();
    DeleteGraphZone();
  }

  TFPipelineData(const TFPipelineData&) = delete;
  TFPipelineData& operator=(const TFPipelineData&) = delete;

  Isolate* isolate() const { return isolate_; }
  AccountingAllocator* allocator() const { return allocator_; }
  OptimizedCompilationInfo* info() const { return info_; }
  ZoneStats* zone_stats() const { return zone_stats_; }
  CompilationDependencies* dependencies() const { return dependencies_; }
  TurbofanPipelineStatistics* pipeline_statistics() {
    return pipeline_statistics_;
  }
  OsrHelper* osr_helper() { return osr_helper_.get(); }
  std::shared_ptr<OsrHelper> osr_helper_ptr() const { return osr_helper_; }

  bool verify_graph() const { return verify_graph_; }
  void set_verify_graph(bool value) { verify_graph_ = value; }

  MaybeHandle<Code> code() { return code_; }
  void set_code(MaybeHandle<Code> code) {
    DCHECK(code_.is_null());
    code_ = code;
  }

  CodeGenerator* code_generator() const { return code_generator_; }

  // RawMachineAssembler generally produces graphs which cannot be verified.
  bool MayHaveUnverifiableGraph() const { return may_have_unverifiable_graph_; }

  Zone* graph_zone() { return graph_zone_; }
  Graph* graph() const { return graph_; }
  void set_graph(Graph* graph) { graph_ = graph; }
  template <typename T>
  using GraphZonePointer = turboshaft::ZoneWithNamePointer<T, kGraphZoneName>;
  void InitializeWithGraphZone(
      turboshaft::ZoneWithName<kGraphZoneName> graph_zone,
      GraphZonePointer<SourcePositionTable> source_positions,
      GraphZonePointer<NodeOriginTable> node_origins,
      size_t node_count_hint = 0) {
    // Delete the old zone first.
    DeleteGraphZone();

    // Take ownership of the new zone and the existing pointers.
    graph_zone_ = std::move(graph_zone);
    source_positions_ = source_positions;
    node_origins_ = node_origins;

    // Allocate a new graph and schedule.
    graph_ = graph_zone_.New<Graph>(graph_zone_);
    schedule_ = graph_zone_.New<Schedule>(graph_zone_, node_count_hint);

    // Initialize node builders.
    javascript_ = graph_zone_.New<JSOperatorBuilder>(graph_zone_);
    common_ = graph_zone_.New<CommonOperatorBuilder>(graph_zone_);
    simplified_ = graph_zone_.New<SimplifiedOperatorBuilder>(graph_zone_);
    machine_ = graph_zone_.New<MachineOperatorBuilder>(
        graph_zone_, MachineType::PointerRepresentation(),
        InstructionSelector::SupportedMachineOperatorFlags(),
        InstructionSelector::AlignmentRequirements());
  }
  turboshaft::ZoneWithName<kGraphZoneName> ReleaseGraphZone() {
    turboshaft::ZoneWithName<kGraphZoneName> temp = std::move(graph_zone_);
    // Call `DeleteGraphZone` to reset all pointers. The actual zone is not
    // released because we moved it away.
    DeleteGraphZone();
    return temp;
  }
  SourcePositionTable* source_positions() const { return source_positions_; }
  void set_source_positions(SourcePositionTable* source_positions) {
    source_positions_ = source_positions;
  }
  NodeOriginTable* node_origins() const { return node_origins_; }
  void set_node_origins(NodeOriginTable* node_origins) {
    node_origins_ = node_origins;
  }
  MachineOperatorBuilder* machine() const { return machine_; }
  SimplifiedOperatorBuilder* simplified() const { return simplified_; }
  CommonOperatorBuilder* common() const { return common_; }
  JSOperatorBuilder* javascript() const { return javascript_; }
  JSGraph* jsgraph() const { return jsgraph_; }
  MachineGraph* mcgraph() const { return mcgraph_; }
  Handle<NativeContext> native_context() const {
    return handle(info()->native_context(), isolate());
  }
  Handle<JSGlobalObject> global_object() const {
    return handle(info()->global_object(), isolate());
  }

  JSHeapBroker* broker() const { return broker_.get(); }
  std::shared_ptr<JSHeapBroker> broker_ptr() { return broker_; }

  Schedule* schedule() const { return schedule_; }
  void set_schedule(Schedule* schedule) {
    DCHECK(!schedule_);
    schedule_ = schedule;
  }
  void reset_schedule() { schedule_ = nullptr; }

  ObserveNodeManager* observe_node_manager() const {
    return observe_node_manager_;
  }

  Zone* instruction_zone() const { return instruction_zone_; }
  Zone* codegen_zone() const { return codegen_zone_; }
  InstructionSequence* sequence() const { return sequence_; }
  Frame* frame() const { return frame_; }

  Zone* register_allocation_zone() const { return register_allocation_zone_; }

  RegisterAllocationData* register_allocation_data() const {
    return register_allocation_data_;
  }

  std::string const& source_position_output() const {
    return source_position_output_;
  }
  void set_source_position_output(std::string const& source_position_output) {
    source_position_output_ = source_position_output;
  }

  JumpOptimizationInfo* jump_optimization_info() const {
    return jump_optimization_info_;
  }

  const AssemblerOptions& assembler_options() const {
    return assembler_options_;
  }

  void ChooseSpecializationContext() {
    if (info()->function_context_specializing()) {
      DCHECK(info()->has_context());
      specialization_context_ = Just(OuterContext(
          info()->CanonicalHandle(info()->context(), isolate()), 0));
    } else {
      specialization_context_ = GetModuleContext(info());
    }
  }

  Maybe<OuterContext> specialization_context() const {
    return specialization_context_;
  }

  size_t* address_of_max_unoptimized_frame_height() {
    return &max_unoptimized_frame_height_;
  }
  size_t max_unoptimized_frame_height() const {
    return max_unoptimized_frame_height_;
  }
  size_t* address_of_max_pushed_argument_count() {
    return &max_pushed_argument_count_;
  }
  size_t max_pushed_argument_count() const {
    return max_pushed_argument_count_;
  }

  CodeTracer* GetCodeTracer() const {
#if V8_ENABLE_WEBASSEMBLY
    if (wasm_engine_) return wasm_engine_->GetCodeTracer();
#endif  // V8_ENABLE_WEBASSEMBLY
    return isolate_->GetCodeTracer();
  }

  Typer* CreateTyper() {
    DCHECK_NULL(typer_);
    typer_ =
        new Typer(broker(), typer_flags_, graph(), &info()->tick_counter());
    return typer_;
  }

  void AddTyperFlag(Typer::Flag flag) {
    DCHECK_NULL(typer_);
    typer_flags_ |= flag;
  }

  void DeleteTyper() {
    delete typer_;
    typer_ = nullptr;
  }

  void DeleteGraphZone() {
    graph_ = nullptr;
    source_positions_ = nullptr;
    node_origins_ = nullptr;
    simplified_ = nullptr;
    machine_ = nullptr;
    common_ = nullptr;
    javascript_ = nullptr;
    jsgraph_ = nullptr;
    mcgraph_ = nullptr;
    schedule_ = nullptr;
    graph_zone_.Destroy();
  }

  void DeleteInstructionZone() {
    if (instruction_zone_ == nullptr) return;
    instruction_zone_scope_.Destroy();
    instruction_zone_ = nullptr;
    sequence_ = nullptr;
  }

  void DeleteCodegenZone() {
    if (codegen_zone_ == nullptr) return;
    codegen_zone_scope_.Destroy();
    codegen_zone_ = nullptr;
    dependencies_ = nullptr;
    broker_.reset();
    broker_ = nullptr;
    frame_ = nullptr;
  }

  void DeleteRegisterAllocationZone() {
    if (register_allocation_zone_ == nullptr) return;
    register_allocation_zone_scope_.Destroy();
    register_allocation_zone_ = nullptr;
    register_allocation_data_ = nullptr;
  }

  void InitializeInstructionSequence(const CallDescriptor* call_descriptor) {
    DCHECK_NULL(sequence_);
    InstructionBlocks* instruction_blocks =
        InstructionSequence::InstructionBlocksFor(instruction_zone(),
                                                  schedule());
    sequence_ = instruction_zone()->New<InstructionSequence>(
        isolate(), instruction_zone(), instruction_blocks);
    if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
      sequence_->instruction_blocks()[0]->mark_needs_frame();
    } else {
      DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
    }
  }

  void InitializeFrameData(CallDescriptor* call_descriptor) {
    DCHECK_NULL(frame_);
    int fixed_frame_size = 0;
    if (call_descriptor != nullptr) {
      fixed_frame_size =
          call_descriptor->CalculateFixedFrameSize(info()->code_kind());
    }
    frame_ = codegen_zone()->New<Frame>(fixed_frame_size, codegen_zone());
    if (osr_helper_) osr_helper()->SetupFrame(frame());
  }

  void InitializeRegisterAllocationData(const RegisterConfiguration* config,
                                        CallDescriptor* call_descriptor) {
    DCHECK_NULL(register_allocation_data_);
    register_allocation_data_ =
        register_allocation_zone()->New<RegisterAllocationData>(
            config, register_allocation_zone(), frame(), sequence(),
            &info()->tick_counter(), debug_name());
  }

  void InitializeOsrHelper() {
    DCHECK_NULL(osr_helper_);
    osr_helper_ = std::make_shared<OsrHelper>(info());
  }

  void set_start_source_position(int position) {
    DCHECK_EQ(start_source_position_, kNoSourcePosition);
    start_source_position_ = position;
  }

  int start_source_position() const { return start_source_position_; }

  void InitializeCodeGenerator(Linkage* linkage) {
    DCHECK_NULL(code_generator_);
#if V8_ENABLE_WEBASSEMBLY
    assembler_options_.is_wasm =
        this->info()->IsWasm() || this->info()->IsWasmBuiltin();
#endif
    std::optional<OsrHelper> osr_helper;
    if (osr_helper_) osr_helper = *osr_helper_;
    code_generator_ = new CodeGenerator(
        codegen_zone(), frame(), linkage, sequence(), info(), isolate(),
        std::move(osr_helper), start_source_position_, jump_optimization_info_,
        assembler_options(), info_->builtin(), max_unoptimized_frame_height(),
        max_pushed_argument_count(),
        v8_flags.trace_turbo_stack_accesses ? debug_name_.get() : nullptr);
  }

  void BeginPhaseKind(const char* phase_kind_name) {
    if (pipeline_statistics() != nullptr) {
      pipeline_statistics()->BeginPhaseKind(phase_kind_name);
    }
  }

  void EndPhaseKind() {
    if (pipeline_statistics() != nullptr) {
      pipeline_statistics()->EndPhaseKind();
    }
  }

  const char* debug_name() const { return debug_name_.get(); }

  const ProfileDataFromFile* profile_data() const { return profile_data_; }
  void set_profile_data(const ProfileDataFromFile* profile_data) {
    profile_data_ = profile_data;
  }

  // RuntimeCallStats that is only available during job execution but not
  // finalization.
  // TODO(delphick): Currently even during execution this can be nullptr, due to
  // JSToWasmWrapperCompilationUnit::Execute. Once a table can be extracted
  // there, this method can DCHECK that it is never nullptr.
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void set_runtime_call_stats(RuntimeCallStats* stats) {
    runtime_call_stats_ = stats;
  }

  // Used to skip the "wasm-inlining" phase when there are no JS-to-Wasm calls.
  bool has_js_wasm_calls() const { return has_js_wasm_calls_; }
  void set_has_js_wasm_calls(bool has_js_wasm_calls) {
    has_js_wasm_calls_ = has_js_wasm_calls;
  }

#if V8_ENABLE_WEBASSEMBLY
  const wasm::WasmModule* wasm_module_for_inlining() const {
    return wasm_module_for_inlining_;
  }
  void set_wasm_module_for_inlining(const wasm::WasmModule* module) {
    wasm_module_for_inlining_ = module;
  }
#endif

 private:
  Isolate* const isolate_;
#if V8_ENABLE_WEBASSEMBLY
  wasm::WasmEngine* const wasm_engine_ = nullptr;
  // The wasm module to be used for inlining wasm functions into JS.
  // The first module wins and inlining of different modules into the same
  // JS function is not supported. This is necessary because the wasm
  // instructions use module-specific (non-canonicalized) type indices.
  const wasm::WasmModule* wasm_module_for_inlining_ = nullptr;
#endif  // V8_ENABLE_WEBASSEMBLY
  AccountingAllocator* const allocator_;
  OptimizedCompilationInfo* const info_;
  std::unique_ptr<char[]> debug_name_;
  bool may_have_unverifiable_graph_ = true;
  ZoneStats* const zone_stats_;
  TurbofanPipelineStatistics* pipeline_statistics_ = nullptr;
  bool verify_graph_ = false;
  int start_source_position_ = kNoSourcePosition;
  std::shared_ptr<OsrHelper> osr_helper_;
  MaybeHandle<Code> code_;
  CodeGenerator* code_generator_ = nullptr;
  Typer* typer_ = nullptr;
  Typer::Flags typer_flags_ = Typer::kNoFlags;

  // All objects in the following group of fields are allocated in graph_zone_.
  // They are all set to nullptr when the graph_zone_ is destroyed.
  turboshaft::ZoneWithName<kGraphZoneName> graph_zone_;
  Graph* graph_ = nullptr;
  SourcePositionTable* source_positions_ = nullptr;
  NodeOriginTable* node_origins_ = nullptr;
  SimplifiedOperatorBuilder* simplified_ = nullptr;
  MachineOperatorBuilder* machine_ = nullptr;
  CommonOperatorBuilder* common_ = nullptr;
  JSOperatorBuilder* javascript_ = nullptr;
  JSGraph* jsgraph_ = nullptr;
  MachineGraph* mcgraph_ = nullptr;
  Schedule* schedule_ = nullptr;
  ObserveNodeManager* observe_node_manager_ = nullptr;
  std::unique_ptr<turboshaft::PipelineData> ts_data_;

  // All objects in the following group of fields are allocated in
  // instruction_zone_. They are all set to nullptr when the instruction_zone_
  // is destroyed.
  ZoneStats::Scope instruction_zone_scope_;
  Zone* instruction_zone_;
  InstructionSequence* sequence_ = nullptr;

  // All objects in the following group of fields are allocated in
  // codegen_zone_. They are all set to nullptr when the codegen_zone_
  // is destroyed.
  ZoneStats::Scope codegen_zone_scope_;
  Zone* codegen_zone_;
  CompilationDependencies* dependencies_ = nullptr;
  std::shared_ptr<JSHeapBroker> broker_;
  Frame* frame_ = nullptr;

  // All objects in the following group of fields are allocated in
  // register_allocation_zone_. They are all set to nullptr when the zone is
  // destroyed.
  ZoneStats::Scope register_allocation_zone_scope_;
  Zone* register_allocation_zone_;
  RegisterAllocationData* register_allocation_data_ = nullptr;

  // Source position output for --trace-turbo.
  std::string source_position_output_;

  JumpOptimizationInfo* jump_optimization_info_ = nullptr;
  AssemblerOptions assembler_options_;
  Maybe<OuterContext> specialization_context_ = Nothing<OuterContext>();

  // The maximal combined height of all inlined frames in their unoptimized
  // state, and the maximal number of arguments pushed during function calls.
  // Calculated during instruction selection, applied during code generation.
  size_t max_unoptimized_frame_height_ = 0;
  size_t max_pushed_argument_count_ = 0;

  RuntimeCallStats* runtime_call_stats_ = nullptr;
  const ProfileDataFromFile* profile_data_ = nullptr;

  bool has_js_wasm_calls_ = false;
};

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_PIPELINE_DATA_INL_H_
