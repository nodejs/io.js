// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/graph-visualizer.h"

#include "src/base/small-vector.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/turboshaft/graph-visualizer.h"

namespace v8::internal::compiler::turboshaft {

JSONTurboshaftGraphWriter::JSONTurboshaftGraphWriter(
    std::ostream& os, const Graph& turboshaft_graph, NodeOriginTable* origins,
    Zone* zone)
    : os_(os),
      zone_(zone),
      turboshaft_graph_(turboshaft_graph),
      origins_(origins) {}

void JSONTurboshaftGraphWriter::Print() {
  os_ << "{\n\"nodes\":[";
  PrintNodes();
  os_ << "\n],\n\"edges\":[";
  PrintEdges();
  os_ << "\n],\n\"blocks\":[";
  PrintBlocks();
  os_ << "\n]}";
}

void JSONTurboshaftGraphWriter::PrintNodes() {
  bool first = true;
  for (const Block& block : turboshaft_graph_.blocks()) {
    for (const Operation& op : turboshaft_graph_.operations(block)) {
      OpIndex index = turboshaft_graph_.Index(op);
      if (!first) os_ << ",\n";
      first = false;
      os_ << "{\"id\":" << index.id() << ",";
      os_ << "\"title\":\"" << OpcodeName(op.opcode) << "\",";
      os_ << "\"block_id\":" << block.index().id() << ",";
      os_ << "\"op_effects\":\"" << op.Effects() << "\"";
      if (origins_) {
        NodeOrigin origin = origins_->GetNodeOrigin(index.id());
        if (origin.IsKnown()) {
          os_ << ", \"origin\":" << AsJSON(origin);
        }
      }
      SourcePosition position = turboshaft_graph_.source_positions()[index];
      if (position.IsKnown()) {
        os_ << ", \"sourcePosition\":" << compiler::AsJSON(position);
      }
      os_ << "}";
    }
  }
}

void JSONTurboshaftGraphWriter::PrintEdges() {
  bool first = true;
  for (const Block& block : turboshaft_graph_.blocks()) {
    for (const Operation& op : turboshaft_graph_.operations(block)) {
      int target_id = turboshaft_graph_.Index(op).id();
      base::SmallVector<OpIndex, 32> inputs{op.inputs()};
      // Reorder the inputs to correspond to the order used in constructor and
      // assembler functions.
      if (auto* store = op.TryCast<StoreOp>()) {
        if (store->index().valid()) {
          DCHECK_EQ(store->input_count, 3);
          inputs = {store->base(), store->index().value_or_invalid(),
                    store->value()};
        }
      }
      for (OpIndex input : inputs) {
        if (!first) os_ << ",\n";
        first = false;
        os_ << "{\"source\":" << input.id() << ",";
        os_ << "\"target\":" << target_id << "}";
      }
    }
  }
}

void JSONTurboshaftGraphWriter::PrintBlocks() {
  bool first_block = true;
  for (const Block& block : turboshaft_graph_.blocks()) {
    if (!first_block) os_ << ",\n";
    first_block = false;
    os_ << "{\"id\":" << block.index().id() << ",";
    os_ << "\"type\":\"" << block.kind() << "\",";
    os_ << "\"predecessors\":[";
    bool first_predecessor = true;
    for (const Block* pred : block.Predecessors()) {
      if (!first_predecessor) os_ << ", ";
      first_predecessor = false;
      os_ << pred->index().id();
    }
    os_ << "]}";
  }
}

std::ostream& operator<<(std::ostream& os, const TurboshaftGraphAsJSON& ad) {
  JSONTurboshaftGraphWriter writer(os, ad.turboshaft_graph, ad.origins,
                                   ad.temp_zone);
  writer.Print();
  return os;
}

void PrintTurboshaftCustomDataPerOperation(
    std::ofstream& stream, const char* data_name, const Graph& graph,
    std::function<bool(std::ostream&, const Graph&, OpIndex)> printer) {
  DCHECK(printer);
  stream << "{\"name\":\"" << data_name
         << "\", \"type\":\"turboshaft_custom_data\", "
            "\"data_target\":\"operations\", \"data\":[";
  bool first = true;
  for (auto index : graph.AllOperationIndices()) {
    std::stringstream sstream;
    if (printer(sstream, graph, index)) {
      stream << (first ? "\n" : ",\n") << "{\"key\":" << index.id()
             << ", \"value\":\"" << sstream.str() << "\"}";
      first = false;
    }
  }
  stream << "]},\n";
}

void PrintTurboshaftCustomDataPerBlock(
    std::ofstream& stream, const char* data_name, const Graph& graph,
    std::function<bool(std::ostream&, const Graph&, BlockIndex)> printer) {
  DCHECK(printer);
  stream << "{\"name\":\"" << data_name
         << "\", \"type\":\"turboshaft_custom_data\", "
            "\"data_target\":\"blocks\", \"data\":[";
  bool first = true;
  for (const Block& block : graph.blocks()) {
    std::stringstream sstream;
    BlockIndex index = block.index();
    if (printer(sstream, graph, index)) {
      stream << (first ? "\n" : ",\n") << "{\"key\":" << index.id()
             << ", \"value\":\"" << sstream.str() << "\"}";
      first = false;
    }
  }
  stream << "]},\n";
}

}  // namespace v8::internal::compiler::turboshaft
