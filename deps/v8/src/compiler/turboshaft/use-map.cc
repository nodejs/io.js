// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/use-map.h"

#include "src/compiler/turboshaft/graph.h"

namespace v8::internal::compiler::turboshaft {

UseMap::UseMap(const Graph& graph, Zone* zone, FunctionType filter)
    : table_(graph.op_id_count(), zone, &graph),
      uses_(zone),
      saturated_uses_(zone) {
  ZoneVector<std::pair<OpIndex, OpIndex>> delayed_phi_uses(zone);

  // We preallocate for 2 uses per operation.
  uses_.reserve(graph.op_id_count() * 2);

  // We skip {offset:0} to use {offset == 0} as uninitialized.
  uint32_t offset = 1;
  for (uint32_t index = 0; index < graph.block_count(); ++index) {
    BlockIndex block_index(index);
    const Block& block = graph.Get(block_index);

    auto block_ops = graph.OperationIndices(block);
    for (OpIndex op_index : block_ops) {
      const Operation& op = graph.Get(op_index);
      // When we see a definition, we allocate space in the {uses_}.
      DCHECK_EQ(table_[op_index].offset, 0);
      DCHECK_EQ(table_[op_index].count, 0);

      if (op.saturated_use_count.IsSaturated()) {
        table_[op_index].offset =
            -static_cast<int32_t>(saturated_uses_.size()) - 1;
        saturated_uses_.emplace_back(zone);
        saturated_uses_.back().reserve(std::numeric_limits<uint8_t>::max());
      } else {
        table_[op_index].offset = offset;
        offset += op.saturated_use_count.Get();
        uses_.resize(offset);
      }

      if (filter(op, zone)) continue;

      if (block.IsLoop()) {
        if (op.Is<PhiOp>()) {
          DCHECK_EQ(op.input_count, 2);
          DCHECK_EQ(PhiOp::kLoopPhiBackEdgeIndex, 1);
          AddUse(&graph, op.input(0), op_index);
          // Delay back edge of loop Phis.
          delayed_phi_uses.emplace_back(op.input(1), op_index);
          continue;
        }
      }

      // Add uses.
      for (OpIndex input_index : op.inputs()) {
        AddUse(&graph, input_index, op_index);
      }
    }
  }

  for (auto [input_index, op_index] : delayed_phi_uses) {
    AddUse(&graph, input_index, op_index);
  }
}

base::Vector<const OpIndex> UseMap::uses(OpIndex index) const {
  DCHECK(index.valid());
  int32_t offset = table_[index].offset;
  uint32_t count = table_[index].count;
  DCHECK_NE(offset, 0);
  if (V8_LIKELY(offset > 0)) {
    return base::Vector<const OpIndex>(uses_.data() + offset, count);
  } else {
    DCHECK_EQ(count, saturated_uses_[-offset - 1].size());
    return base::Vector<const OpIndex>(saturated_uses_[-offset - 1].data(),
                                       count);
  }
}

void UseMap::AddUse(const Graph* graph, OpIndex node, OpIndex use) {
  int32_t input_offset = table_[node].offset;
  uint32_t& input_count = table_[node].count;
  DCHECK_NE(input_offset, 0);
  if (V8_LIKELY(input_offset > 0)) {
    DCHECK_LT(input_count, graph->Get(node).saturated_use_count.Get());
    DCHECK(!uses_[input_offset + input_count].valid());
    uses_[input_offset + input_count] = use;
  } else {
    ZoneVector<OpIndex>& uses = saturated_uses_[-input_offset - 1];
    DCHECK_EQ(uses.size(), input_count);
    uses.emplace_back(use);
  }
  ++input_count;
}

}  // namespace v8::internal::compiler::turboshaft
