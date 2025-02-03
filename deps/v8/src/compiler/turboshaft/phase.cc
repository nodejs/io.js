// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/phase.h"

#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/graph-visualizer.h"
#include "src/diagnostics/code-tracer.h"
#include "src/utils/ostreams.h"
#ifdef V8_ENABLE_WEBASSEMBLY
#include "src/wasm/wasm-engine.h"
#endif

namespace v8::internal::compiler::turboshaft {

void PipelineData::InitializeRegisterComponent(
    const RegisterConfiguration* config, CallDescriptor* call_descriptor) {
  DCHECK(!register_component_.has_value());
  register_component_.emplace(zone_stats());
  auto& zone = register_component_->zone;
  register_component_->allocation_data = zone.New<RegisterAllocationData>(
      config, zone, frame(), sequence(), &info()->tick_counter(),
      debug_name_.get());
}

AccountingAllocator* PipelineData::allocator() const {
  if (isolate_) return isolate_->allocator();
#ifdef V8_ENABLE_WEBASSEMBLY
  if (auto e = wasm::GetWasmEngine()) {
    return e->allocator();
  }
#endif
  return nullptr;
}

void PrintTurboshaftGraph(PipelineData* data, Zone* temp_zone,
                          CodeTracer* code_tracer, const char* phase_name) {
  if (data->info()->trace_turbo_json()) {
    UnparkedScopeIfNeeded scope(data->broker());
    AllowHandleDereference allow_deref;
    turboshaft::Graph& graph = data->graph();

    TurboJsonFile json_of(data->info(), std::ios_base::app);
    PrintTurboshaftGraphForTurbolizer(json_of, graph, phase_name,
                                      data->node_origins(), temp_zone);
  }

  if (data->info()->trace_turbo_graph()) {
    DCHECK(code_tracer);
    UnparkedScopeIfNeeded scope(data->broker());
    AllowHandleDereference allow_deref;

    CodeTracer::StreamScope tracing_scope(code_tracer);
    tracing_scope.stream() << "\n----- " << phase_name << " -----\n"
                           << data->graph();
  }
}

void PrintTurboshaftGraphForTurbolizer(std::ofstream& stream,
                                       const Graph& graph,
                                       const char* phase_name,
                                       NodeOriginTable* node_origins,
                                       Zone* temp_zone) {
  stream << "{\"name\":\"" << phase_name
         << "\",\"type\":\"turboshaft_graph\",\"data\":"
         << AsJSON(graph, node_origins, temp_zone) << "},\n";

  PrintTurboshaftCustomDataPerOperation(
      stream, "Properties", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        const auto& op = graph.Get(index);
        op.PrintOptions(stream);
        return true;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Types", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        turboshaft::Type type = graph.operation_types()[index];
        if (!type.IsInvalid() && !type.IsNone()) {
          type.PrintTo(stream);
          return true;
        }
        return false;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Representations", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        const Operation& op = graph.Get(index);
        stream << PrintCollection(op.outputs_rep());
        return true;
      });
  PrintTurboshaftCustomDataPerOperation(
      stream, "Use Count (saturated)", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::OpIndex index) -> bool {
        stream << static_cast<int>(graph.Get(index).saturated_use_count.Get());
        return true;
      });
#ifdef DEBUG
  PrintTurboshaftCustomDataPerBlock(
      stream, "Type Refinements", graph,
      [](std::ostream& stream, const turboshaft::Graph& graph,
         turboshaft::BlockIndex index) -> bool {
        const std::vector<std::pair<turboshaft::OpIndex, turboshaft::Type>>&
            refinements = graph.block_type_refinement()[index];
        if (refinements.empty()) return false;
        stream << "\\n";
        for (const auto& [op, type] : refinements) {
          stream << op << " : " << type << "\\n";
        }
        return true;
      });
#endif  // DEBUG
}

CodeTracer* PipelineData::GetCodeTracer() const {
#if V8_ENABLE_WEBASSEMBLY
  if (info_->IsWasm() || info_->IsWasmBuiltin()) {
    return wasm::GetWasmEngine()->GetCodeTracer();
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  DCHECK_NOT_NULL(isolate_);
  return isolate_->GetCodeTracer();
}

}  // namespace v8::internal::compiler::turboshaft
