// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_PHASE_H_

#include <optional>
#include <type_traits>

#include "src/base/contextual.h"
#include "src/base/template-meta-programming/functional.h"
#include "src/codegen/assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/common/globals.h"
#include "src/compiler/access-info.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/osr.h"
#include "src/compiler/phase.h"
#include "src/compiler/turboshaft/builtin-compiler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/compiler/turboshaft/zone-with-name.h"
#include "src/logging/runtime-call-stats.h"
#include "src/zone/accounting-allocator.h"
#include "src/zone/zone.h"

#define DECL_TURBOSHAFT_PHASE_CONSTANTS_IMPL(Name, CallStatsName)             \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(CallStatsName, PhaseKind::kTurboshaft, \
                                       RuntimeCallStats::kThreadSpecific)     \
  static constexpr char kPhaseName[] = "V8.TF" #CallStatsName;                \
  static void AssertTurboshaftPhase() {                                       \
    static_assert(TurboshaftPhase<Name##Phase>);                              \
  }

#define DECL_TURBOSHAFT_PHASE_CONSTANTS(Name) \
  DECL_TURBOSHAFT_PHASE_CONSTANTS_IMPL(Name, Turboshaft##Name)
#define DECL_TURBOSHAFT_PHASE_CONSTANTS_WITH_LEGACY_NAME(Name) \
  DECL_TURBOSHAFT_PHASE_CONSTANTS_IMPL(Name, Name)

#define DECL_TURBOSHAFT_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS_WITH_LEGACY_NAME( \
    Name)                                                                      \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Name, PhaseKind::kTurboshaft,           \
                                       RuntimeCallStats::kExact)               \
  static constexpr char kPhaseName[] = "V8.TF" #Name;                          \
  static void AssertTurboshaftPhase() {                                        \
    static_assert(TurboshaftPhase<Name##Phase>);                               \
  }

namespace v8::internal::compiler {
class RegisterAllocationData;
class Schedule;
class TurbofanPipelineStatistics;
}  // namespace v8::internal::compiler

namespace v8::internal::compiler::turboshaft {

class PipelineData;

template <typename Phase>
struct HasProperRunMethod {
  using parameters = base::tmp::call_parameters_t<decltype(&Phase::Run)>;
  static_assert(
      base::tmp::length_v<parameters> >= 2,
      "Phase::Run needs at least two parameters (PipelineData* and Zone*)");
  using parameter0 = base::tmp::element_t<parameters, 0>;
  using parameter1 = base::tmp::element_t<parameters, 1>;
  static constexpr bool value = std::is_same_v<parameter0, PipelineData*> &&
                                std::is_same_v<parameter1, Zone*>;
};

template <typename Phase, typename... Args>
concept TurboshaftPhase =
    HasProperRunMethod<Phase>::value &&
    requires(Phase p) { p.kKind == PhaseKind::kTurboshaft; };

template <typename Phase>
concept TurbofanPhase = requires(Phase p) { p.kKind == PhaseKind::kTurbofan; };

template <typename Phase>
concept CompilerPhase = TurboshaftPhase<Phase> || TurbofanPhase<Phase>;

namespace detail {
template <typename, typename = void>
struct produces_printable_graph_impl : std::true_type {};

template <typename P>
struct produces_printable_graph_impl<
    P, std::void_t<decltype(P::kOutputIsTraceableGraph)>>
    : std::bool_constant<P::kOutputIsTraceableGraph> {};

#ifdef HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS
template <base::tmp::StringLiteral ZoneName>
#else
template <auto ZoneName>
#endif
struct ComponentWithZone {
  template <typename T>
  using Pointer = ZoneWithNamePointer<T, ZoneName>;

  explicit ComponentWithZone(ZoneStats* zone_stats)
      : zone(zone_stats,
#ifdef HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS
             ZoneName.c_str()
#else
             ZONE_NAME
#endif
        ) {
  }
  explicit ComponentWithZone(ZoneWithName<ZoneName> existing_zone)
      : zone(std::move(existing_zone)) {}

  ZoneWithName<ZoneName> zone;
};

struct BuiltinComponent {
  const CallDescriptor* call_descriptor;
  std::optional<BytecodeHandlerData> bytecode_handler_data;

  BuiltinComponent(const CallDescriptor* call_descriptor,
                   std::optional<BytecodeHandlerData> bytecode_handler_data)
      : call_descriptor(call_descriptor),
        bytecode_handler_data(std::move(bytecode_handler_data)) {}
};

struct GraphComponent : public ComponentWithZone<kGraphZoneName> {
  using ComponentWithZone::ComponentWithZone;

  Pointer<Graph> graph = nullptr;
  Pointer<SourcePositionTable> source_positions = nullptr;
  Pointer<NodeOriginTable> node_origins = nullptr;
  bool graph_has_special_rpo = false;
};

struct CodegenComponent : public ComponentWithZone<kCodegenZoneName> {
  using ComponentWithZone::ComponentWithZone;

  Pointer<Frame> frame = nullptr;
  std::unique_ptr<CodeGenerator> code_generator;
  Pointer<CompilationDependency> dependencies = nullptr;
  // TODO(nicohartmann): Make {osr_helper} an optional once TurboFan's
  // PipelineData is gone.
  std::shared_ptr<OsrHelper> osr_helper;
  JumpOptimizationInfo* jump_optimization_info = nullptr;
  size_t max_unoptimized_frame_height = 0;
  size_t max_pushed_argument_count = 0;
};

struct InstructionComponent : public ComponentWithZone<kInstructionZoneName> {
  using ComponentWithZone::ComponentWithZone;

  Pointer<InstructionSequence> sequence = nullptr;
};

struct RegisterComponent
    : public ComponentWithZone<kRegisterAllocationZoneName> {
  using ComponentWithZone::ComponentWithZone;

  Pointer<RegisterAllocationData> allocation_data = nullptr;
};
}  // namespace detail

template <typename P>
struct produces_printable_graph
    : public detail::produces_printable_graph_impl<P> {};

enum class TurboshaftPipelineKind { kJS, kWasm, kCSA, kTSABuiltin, kJSToWasm };

class LoopUnrollingAnalyzer;
class WasmRevecAnalyzer;

class V8_EXPORT_PRIVATE PipelineData {
  using BuiltinComponent = detail::BuiltinComponent;
  using GraphComponent = detail::GraphComponent;
  using CodegenComponent = detail::CodegenComponent;
  using InstructionComponent = detail::InstructionComponent;
  using RegisterComponent = detail::RegisterComponent;

 public:
  explicit PipelineData(ZoneStats* zone_stats,
                        TurboshaftPipelineKind pipeline_kind, Isolate* isolate,
                        OptimizedCompilationInfo* info,
                        const AssemblerOptions& assembler_options,
                        int start_source_position = kNoSourcePosition)
      : zone_stats_(zone_stats),
        compilation_zone_(zone_stats, kCompilationZoneName),
        pipeline_kind_(pipeline_kind),
        isolate_(isolate),
        info_(info),
        debug_name_(info_ ? info_->GetDebugName() : std::unique_ptr<char[]>{}),
        start_source_position_(start_source_position),
        assembler_options_(assembler_options) {
#if V8_ENABLE_WEBASSEMBLY
    if (info != nullptr) {
      DCHECK_EQ(assembler_options_.is_wasm,
                info->IsWasm() || info->IsWasmBuiltin());
    }
#endif
  }

  void InitializeBrokerAndDependencies(std::shared_ptr<JSHeapBroker> broker,
                                       CompilationDependencies* dependencies) {
    DCHECK_NULL(broker_.get());
    DCHECK_NULL(dependencies_);
    DCHECK_NOT_NULL(broker);
    DCHECK_NOT_NULL(dependencies);
    broker_ = std::move(broker);
    dependencies_ = dependencies;
  }

  void InitializeBuiltinComponent(
      const CallDescriptor* call_descriptor,
      std::optional<BytecodeHandlerData> bytecode_handler_data = {}) {
    DCHECK(!builtin_component_.has_value());
    builtin_component_.emplace(call_descriptor,
                               std::move(bytecode_handler_data));
  }

  void InitializeGraphComponent(SourcePositionTable* source_positions) {
    DCHECK(!graph_component_.has_value());
    graph_component_.emplace(zone_stats_);
    auto& zone = graph_component_->zone;
    graph_component_->graph = zone.New<Graph>(zone);
    graph_component_->source_positions =
        GraphComponent::Pointer<SourcePositionTable>(source_positions);
    if (info_ && info_->trace_turbo_json()) {
      graph_component_->node_origins = zone.New<NodeOriginTable>(zone);
    }
  }

  void InitializeGraphComponentWithGraphZone(
      ZoneWithName<kGraphZoneName> graph_zone,
      ZoneWithNamePointer<SourcePositionTable, kGraphZoneName> source_positions,
      ZoneWithNamePointer<NodeOriginTable, kGraphZoneName> node_origins) {
    DCHECK(!graph_component_.has_value());
    graph_component_.emplace(std::move(graph_zone));
    auto& zone = graph_component_->zone;
    graph_component_->graph = zone.New<Graph>(zone);
    graph_component_->source_positions = source_positions;
    graph_component_->node_origins = node_origins;
    if (!graph_component_->node_origins && info_ && info_->trace_turbo_json()) {
      graph_component_->node_origins = zone.New<NodeOriginTable>(zone);
    }
  }

  void ClearGraphComponent() {
    DCHECK(graph_component_.has_value());
    graph_component_.reset();
  }

  void InitializeCodegenComponent(
      std::shared_ptr<OsrHelper> osr_helper,
      JumpOptimizationInfo* jump_optimization_info = nullptr) {
    DCHECK(!codegen_component_.has_value());
    codegen_component_.emplace(zone_stats_);
    codegen_component_->osr_helper = std::move(osr_helper);
    codegen_component_->jump_optimization_info = jump_optimization_info;
  }

  void ClearCodegenComponent() {
    DCHECK(codegen_component_.has_value());
    codegen_component_.reset();
  }

  void InitializeCodeGenerator(Linkage* linkage) {
    DCHECK(codegen_component_.has_value());
    CodegenComponent& cg = *codegen_component_;
    DCHECK_NULL(codegen_component_->code_generator);
#if V8_ENABLE_WEBASSEMBLY
    DCHECK_EQ(assembler_options_.is_wasm,
              info()->IsWasm() || info()->IsWasmBuiltin());
#endif
    std::optional<OsrHelper> osr_helper;
    if (cg.osr_helper) osr_helper = *cg.osr_helper;
    cg.code_generator = std::make_unique<CodeGenerator>(
        cg.zone, cg.frame, linkage, sequence(), info_, isolate_,
        std::move(osr_helper), start_source_position_,
        cg.jump_optimization_info, assembler_options_, info_->builtin(),
        cg.max_unoptimized_frame_height, cg.max_pushed_argument_count,
        v8_flags.trace_turbo_stack_accesses ? debug_name_.get() : nullptr);
  }

  void InitializeInstructionComponent(const CallDescriptor* call_descriptor) {
    DCHECK(!instruction_component_.has_value());
    instruction_component_.emplace(zone_stats());
    auto& zone = instruction_component_->zone;
    InstructionBlocks* instruction_blocks =
        InstructionSequence::InstructionBlocksFor(zone, graph());
    instruction_component_->sequence =
        zone.New<InstructionSequence>(isolate(), zone, instruction_blocks);
    if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
      instruction_component_->sequence->instruction_blocks()[0]
          ->mark_needs_frame();
    } else {
      DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
    }
  }

  void ClearInstructionComponent() {
    DCHECK(instruction_component_.has_value());
    instruction_component_.reset();
  }

  void InitializeRegisterComponent(const RegisterConfiguration* config,
                                   CallDescriptor* call_descriptor);

  void ClearRegisterComponent() {
    DCHECK(register_component_.has_value());
    register_component_.reset();
  }

  AccountingAllocator* allocator() const;
  ZoneStats* zone_stats() const { return zone_stats_; }
  TurboshaftPipelineKind pipeline_kind() const { return pipeline_kind_; }
  Isolate* isolate() const { return isolate_; }
  OptimizedCompilationInfo* info() const { return info_; }
  const char* debug_name() const { return debug_name_.get(); }
  JSHeapBroker* broker() const { return broker_.get(); }
  CompilationDependencies* depedencies() const { return dependencies_; }
  const AssemblerOptions& assembler_options() const {
    return assembler_options_;
  }
  JumpOptimizationInfo* jump_optimization_info() {
    if (!codegen_component_.has_value()) return nullptr;
    return codegen_component_->jump_optimization_info;
  }
  const CallDescriptor* builtin_call_descriptor() const {
    DCHECK(builtin_component_.has_value());
    return builtin_component_->call_descriptor;
  }
  std::optional<BytecodeHandlerData>& bytecode_handler_data() {
    DCHECK(builtin_component_.has_value());
    return builtin_component_->bytecode_handler_data;
  }

  bool has_graph() const {
    DCHECK_IMPLIES(graph_component_.has_value(),
                   graph_component_->graph != nullptr);
    return graph_component_.has_value();
  }
  ZoneWithName<kGraphZoneName>& graph_zone() { return graph_component_->zone; }
  turboshaft::Graph& graph() const { return *graph_component_->graph; }
  GraphComponent::Pointer<SourcePositionTable> source_positions() const {
    return graph_component_->source_positions;
  }
  GraphComponent::Pointer<NodeOriginTable> node_origins() const {
    if (!graph_component_.has_value()) return nullptr;
    return graph_component_->node_origins;
  }
  RegisterAllocationData* register_allocation_data() const {
    return register_component_->allocation_data;
  }
  ZoneWithName<kRegisterAllocationZoneName>& register_allocation_zone() {
    return register_component_->zone;
  }
  CodeGenerator* code_generator() const {
    return codegen_component_->code_generator.get();
  }
  void set_code(MaybeIndirectHandle<Code> code) {
    DCHECK(code_.is_null());
    code_ = code;
  }
  MaybeIndirectHandle<Code> code() const { return code_; }
  InstructionSequence* sequence() const {
    return instruction_component_->sequence;
  }
  Frame* frame() const { return codegen_component_->frame; }
  CodeTracer* GetCodeTracer() const;
  size_t& max_unoptimized_frame_height() {
    return codegen_component_->max_unoptimized_frame_height;
  }
  size_t& max_pushed_argument_count() {
    return codegen_component_->max_pushed_argument_count;
  }
  RuntimeCallStats* runtime_call_stats() const { return runtime_call_stats_; }
  void set_runtime_call_stats(RuntimeCallStats* stats) {
    runtime_call_stats_ = stats;
  }

  // The {compilation_zone} outlives the entire compilation pipeline. It is
  // shared between all phases (including code gen where the graph zone is gone
  // already).
  ZoneWithName<kCompilationZoneName>& compilation_zone() {
    return compilation_zone_;
  }

  TurbofanPipelineStatistics* pipeline_statistics() const {
    return pipeline_statistics_;
  }
  void set_pipeline_statistics(
      TurbofanPipelineStatistics* pipeline_statistics) {
    pipeline_statistics_ = pipeline_statistics;
  }

#if V8_ENABLE_WEBASSEMBLY
  // Module-specific signature: type indices are only valid in the WasmModule*
  // they belong to.
  const wasm::FunctionSig* wasm_module_sig() const { return wasm_module_sig_; }

  // Canonicalized (module-independent) signature.
  const wasm::CanonicalSig* wasm_canonical_sig() const {
    return wasm_canonical_sig_;
  }

  const wasm::WasmModule* wasm_module() const { return wasm_module_; }

  bool wasm_shared() const { return wasm_shared_; }

  void SetIsWasmFunction(const wasm::WasmModule* module,
                         const wasm::FunctionSig* sig, bool shared) {
    wasm_module_ = module;
    wasm_module_sig_ = sig;
    wasm_shared_ = shared;
    DCHECK(pipeline_kind() == TurboshaftPipelineKind::kWasm ||
           pipeline_kind() == TurboshaftPipelineKind::kJSToWasm);
  }

  void SetIsWasmWrapper(const wasm::CanonicalSig* sig) {
    wasm_canonical_sig_ = sig;
    DCHECK(pipeline_kind() == TurboshaftPipelineKind::kWasm ||
           pipeline_kind() == TurboshaftPipelineKind::kJSToWasm);
  }

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
  WasmRevecAnalyzer* wasm_revec_analyzer() const {
    DCHECK_NOT_NULL(wasm_revec_analyzer_);
    return wasm_revec_analyzer_;
  }

  void set_wasm_revec_analyzer(WasmRevecAnalyzer* wasm_revec_analyzer) {
    DCHECK_NULL(wasm_revec_analyzer_);
    wasm_revec_analyzer_ = wasm_revec_analyzer;
  }

  void clear_wasm_revec_analyzer() { wasm_revec_analyzer_ = nullptr; }
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY

  bool is_wasm() const {
    return pipeline_kind() == TurboshaftPipelineKind::kWasm ||
           pipeline_kind() == TurboshaftPipelineKind::kJSToWasm;
  }
  bool is_js_to_wasm() const {
    return pipeline_kind() == TurboshaftPipelineKind::kJSToWasm;
  }

  void InitializeFrameData(CallDescriptor* call_descriptor) {
    DCHECK(codegen_component_.has_value());
    DCHECK_NULL(codegen_component_->frame);
    int fixed_frame_size = 0;
    if (call_descriptor != nullptr) {
      fixed_frame_size =
          call_descriptor->CalculateFixedFrameSize(info()->code_kind());
    }
    codegen_component_->frame = codegen_component_->zone.New<Frame>(
        fixed_frame_size, codegen_component_->zone);
    if (codegen_component_->osr_helper) {
      codegen_component_->osr_helper->SetupFrame(codegen_component_->frame);
    }
  }

  void set_source_position_output(std::string source_position_output) {
    source_position_output_ = std::move(source_position_output);
  }
  std::string source_position_output() const { return source_position_output_; }

  bool graph_has_special_rpo() const {
    return graph_component_->graph_has_special_rpo;
  }
  void set_graph_has_special_rpo() {
    graph_component_->graph_has_special_rpo = true;
  }

 private:
  ZoneStats* zone_stats_;
  // The {compilation_zone_} outlives the entire compilation pipeline. It is
  // shared between all phases (including code gen where the graph zone is gone
  // already).
  ZoneWithName<kCompilationZoneName> compilation_zone_;
  TurboshaftPipelineKind pipeline_kind_;
  Isolate* const isolate_ = nullptr;
  OptimizedCompilationInfo* info_ = nullptr;
  std::unique_ptr<char[]> debug_name_;
  // TODO(nicohartmann): Use unique_ptr once TurboFan's pipeline data is gone.
  std::shared_ptr<JSHeapBroker> broker_;
  TurbofanPipelineStatistics* pipeline_statistics_ = nullptr;
  CompilationDependencies* dependencies_ = nullptr;
  int start_source_position_ = kNoSourcePosition;
  const AssemblerOptions assembler_options_;
  MaybeIndirectHandle<Code> code_;
  std::string source_position_output_;
  RuntimeCallStats* runtime_call_stats_ = nullptr;
  // Components
  std::optional<BuiltinComponent> builtin_component_;
  std::optional<GraphComponent> graph_component_;
  std::optional<CodegenComponent> codegen_component_;
  std::optional<InstructionComponent> instruction_component_;
  std::optional<RegisterComponent> register_component_;

#if V8_ENABLE_WEBASSEMBLY
  // TODO(14108): Consider splitting wasm members into its own WasmPipelineData
  // if we need many of them.
  const wasm::FunctionSig* wasm_module_sig_ = nullptr;
  const wasm::CanonicalSig* wasm_canonical_sig_ = nullptr;
  const wasm::WasmModule* wasm_module_ = nullptr;
  bool wasm_shared_ = false;
#ifdef V8_ENABLE_WASM_SIMD256_REVEC

  WasmRevecAnalyzer* wasm_revec_analyzer_ = nullptr;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY
};

void PrintTurboshaftGraph(PipelineData* data, Zone* temp_zone,
                          CodeTracer* code_tracer, const char* phase_name);
void PrintTurboshaftGraphForTurbolizer(std::ofstream& stream,
                                       const Graph& graph,
                                       const char* phase_name,
                                       NodeOriginTable* node_origins,
                                       Zone* temp_zone);

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PHASE_H_
