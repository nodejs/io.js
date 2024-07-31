// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_PHASE_H_

#include <type_traits>

#include "src/base/contextual.h"
#include "src/codegen/assembler.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/phase.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/sidetable.h"

#define DECL_TURBOSHAFT_PHASE_CONSTANTS(Name)                  \
  DECL_PIPELINE_PHASE_CONSTANTS_HELPER(Turboshaft##Name,       \
                                       PhaseKind::kTurboshaft, \
                                       RuntimeCallStats::kThreadSpecific)

namespace v8::internal::compiler {
class Schedule;
}  // namespace v8::internal::compiler

namespace v8::internal::compiler::turboshaft {

template <typename P>
struct produces_printable_graph : public std::true_type {};

enum class TurboshaftPipelineKind { kJS, kWasm, kCSA, kJSToWasm };

class LoopUnrollingAnalyzer;
class WasmRevecAnalyzer;

class V8_EXPORT_PRIVATE PipelineData
    : public base::ContextualClass<PipelineData> {
 public:
  explicit PipelineData(
      TurboshaftPipelineKind pipeline_kind,
      OptimizedCompilationInfo* const& info, Schedule*& schedule,
      Zone*& graph_zone, Zone* shared_zone, JSHeapBroker*& broker,
      Isolate* const& isolate, SourcePositionTable*& source_positions,
      NodeOriginTable*& node_origins, InstructionSequence*& sequence,
      Frame*& frame, AssemblerOptions& assembler_options,
      size_t* address_of_max_unoptimized_frame_height,
      size_t* address_of_max_pushed_argument_count, Zone*& instruction_zone)
      : pipeline_kind_(pipeline_kind),
        info_(info),
        schedule_(schedule),
        graph_zone_(graph_zone),
        shared_zone_(shared_zone),
        broker_(broker),
        isolate_(isolate),
        source_positions_(source_positions),
        node_origins_(node_origins),
        sequence_(sequence),
        frame_(frame),
        assembler_options_(assembler_options),
        address_of_max_unoptimized_frame_height_(
            address_of_max_unoptimized_frame_height),
        address_of_max_pushed_argument_count_(
            address_of_max_pushed_argument_count),
        instruction_zone_(instruction_zone),
        graph_(graph_zone_->New<turboshaft::Graph>(graph_zone_)) {}

  bool has_graph() const { return graph_ != nullptr; }
  turboshaft::Graph& graph() const { return *graph_; }

  TurboshaftPipelineKind pipeline_kind() const { return pipeline_kind_; }
  OptimizedCompilationInfo* info() const { return info_; }
  Schedule* schedule() const { return schedule_; }
  Zone* graph_zone() const { return graph_zone_; }
  // The {shared_zone_} outlives the entire compilation pipeline. It is shared
  // between all phases (including code gen where the graph zone is gone
  // already).
  Zone* shared_zone() const { return shared_zone_; }
  JSHeapBroker* broker() const { return broker_; }
  Isolate* isolate() const { return isolate_; }
  SourcePositionTable* source_positions() const { return source_positions_; }
  NodeOriginTable* node_origins() const { return node_origins_; }
  InstructionSequence* sequence() const { return sequence_; }
  Frame* frame() const { return frame_; }
  AssemblerOptions& assembler_options() const { return assembler_options_; }
  size_t* address_of_max_unoptimized_frame_height() const {
    return address_of_max_unoptimized_frame_height_;
  }
  size_t* address_of_max_pushed_argument_count() const {
    return address_of_max_pushed_argument_count_;
  }
  Zone* instruction_zone() const { return instruction_zone_; }
  CodeTracer* GetCodeTracer() const { return isolate_->GetCodeTracer(); }

#if V8_ENABLE_WEBASSEMBLY
  const wasm::FunctionSig* wasm_sig() const {
    DCHECK(wasm_sig_ != nullptr);
    return wasm_sig_;
  }

  const wasm::WasmModule* wasm_module() const { return wasm_module_; }

  void SetIsWasm(const wasm::WasmModule* module, const wasm::FunctionSig* sig) {
    wasm_module_ = module;
    wasm_sig_ = sig;
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

  void reset_schedule() { schedule_ = nullptr; }

  void InitializeInstructionSequence(const CallDescriptor* call_descriptor) {
    DCHECK_NULL(sequence_);
    InstructionBlocks* instruction_blocks =
        InstructionSequence::InstructionBlocksFor(instruction_zone(), *graph_);
    sequence_ = instruction_zone()->New<InstructionSequence>(
        isolate(), instruction_zone(), instruction_blocks);
    if (call_descriptor && call_descriptor->RequiresFrameAsIncoming()) {
      sequence_->instruction_blocks()[0]->mark_needs_frame();
    } else {
      DCHECK(call_descriptor->CalleeSavedFPRegisters().is_empty());
    }
  }

  void set_loop_unrolling_analyzer(
      LoopUnrollingAnalyzer* loop_unrolling_analyzer) {
    DCHECK_NULL(loop_unrolling_analyzer_);
    loop_unrolling_analyzer_ = loop_unrolling_analyzer;
  }
  void clear_loop_unrolling_analyzer() { loop_unrolling_analyzer_ = nullptr; }
  LoopUnrollingAnalyzer* loop_unrolling_analyzer() const {
    DCHECK_NOT_NULL(loop_unrolling_analyzer_);
    return loop_unrolling_analyzer_;
  }

 private:
  // Turbofan's PipelineData owns most of these objects. We only hold references
  // to them.
  // TODO(v8:12783, nicohartmann@): Change this once Turbofan pipeline is fully
  // replaced.
  TurboshaftPipelineKind pipeline_kind_;
  OptimizedCompilationInfo* const& info_;
  Schedule*& schedule_;
  Zone*& graph_zone_;
  Zone* shared_zone_;
  JSHeapBroker*& broker_;
  Isolate* const& isolate_;
  SourcePositionTable*& source_positions_;
  NodeOriginTable*& node_origins_;
  InstructionSequence*& sequence_;
  Frame*& frame_;
  AssemblerOptions& assembler_options_;
  size_t* address_of_max_unoptimized_frame_height_;
  size_t* address_of_max_pushed_argument_count_;
  Zone*& instruction_zone_;

#if V8_ENABLE_WEBASSEMBLY
  // TODO(14108): Consider splitting wasm members into its own WasmPipelineData
  // if we need many of them.
  const wasm::FunctionSig* wasm_sig_ = nullptr;
  const wasm::WasmModule* wasm_module_ = nullptr;
#ifdef V8_ENABLE_WASM_SIMD256_REVEC

  WasmRevecAnalyzer* wasm_revec_analyzer_ = nullptr;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY

  LoopUnrollingAnalyzer* loop_unrolling_analyzer_ = nullptr;

  turboshaft::Graph* graph_;
};

void PrintTurboshaftGraph(Zone* temp_zone, CodeTracer* code_tracer,
                          const char* phase_name);
void PrintTurboshaftGraphForTurbolizer(std::ofstream& stream,
                                       const Graph& graph,
                                       const char* phase_name,
                                       NodeOriginTable* node_origins,
                                       Zone* temp_zone);

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_PHASE_H_
