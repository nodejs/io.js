// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/instruction-selection-phase.h"

#include <optional>

#include "src/builtins/profile-data-reader.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/pipeline.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/sidetable.h"
#include "src/diagnostics/code-tracer.h"
#include "src/utils/sparse-bit-vector.h"

namespace v8::internal::compiler::turboshaft {

namespace {

void TraceSequence(OptimizedCompilationInfo* info,
                   InstructionSequence* sequence, JSHeapBroker* broker,
                   CodeTracer* code_tracer, const char* phase_name) {
  if (info->trace_turbo_json()) {
    UnparkedScopeIfNeeded scope(broker);
    AllowHandleDereference allow_deref;
    TurboJsonFile json_of(info, std::ios_base::app);
    json_of << "{\"name\":\"" << phase_name << "\",\"type\":\"sequence\""
            << ",\"blocks\":" << InstructionSequenceAsJSON{sequence}
            << ",\"register_allocation\":{"
            << "\"fixed_double_live_ranges\": {}"
            << ",\"fixed_live_ranges\": {}"
            << ",\"live_ranges\": {}"
            << "}},\n";
  }
  if (info->trace_turbo_graph()) {
    UnparkedScopeIfNeeded scope(broker);
    AllowHandleDereference allow_deref;
    CodeTracer::StreamScope tracing_scope(code_tracer);
    tracing_scope.stream() << "----- Instruction sequence " << phase_name
                           << " -----\n"
                           << *sequence;
  }
}

}  // namespace

ZoneVector<uint32_t> TurboshaftSpecialRPONumberer::ComputeSpecialRPO() {
  ZoneVector<SpecialRPOStackFrame> stack(zone());
  ZoneVector<Backedge> backedges(zone());
  // Determined empirically on a large Wasm module. Since they are allocated
  // only once per function compilation, the memory usage is not critical.
  stack.reserve(64);
  backedges.reserve(32);
  size_t num_loops = 0;

  auto Push = [&](const Block* block) {
    auto succs = SuccessorBlocks(*block, *graph_);
    stack.emplace_back(block, 0, std::move(succs));
    set_rpo_number(block, kBlockOnStack);
  };

  const Block* entry = &graph_->StartBlock();

  // Find correct insertion point within existing order.
  const Block* order = nullptr;

  Push(&graph_->StartBlock());

  while (!stack.empty()) {
    SpecialRPOStackFrame& frame = stack.back();

    if (frame.index < frame.successors.size()) {
      // Process the next successor.
      const Block* succ = frame.successors[frame.index++];
      if (rpo_number(succ) == kBlockVisited1) continue;
      if (rpo_number(succ) == kBlockOnStack) {
        // The successor is on the stack, so this is a backedge (cycle).
        DCHECK_EQ(frame.index - 1, 0);
        backedges.emplace_back(frame.block, frame.index - 1);
        // Assign a new loop number to the header.
        DCHECK(!has_loop_number(succ));
        set_loop_number(succ, num_loops++);
      } else {
        // Push the successor onto the stack.
        DCHECK_EQ(rpo_number(succ), kBlockUnvisited);
        Push(succ);
      }
    } else {
      // Finished with all successors; pop the stack and add the block.
      order = PushFront(order, frame.block);
      set_rpo_number(frame.block, kBlockVisited1);
      stack.pop_back();
    }
  }

  // If no loops were encountered, then the order we computed was correct.
  if (num_loops == 0) return ComputeBlockPermutation(entry);

  // Otherwise, compute the loop information from the backedges in order
  // to perform a traversal that groups loop bodies together.
  ComputeLoopInfo(num_loops, backedges);

  // Initialize the "loop stack". We assume that the entry cannot be a loop
  // header.
  CHECK(!has_loop_number(entry));
  LoopInfo* loop = nullptr;
  order = nullptr;

  // Perform an iterative post-order traversal, visiting loop bodies before
  // edges that lead out of loops. Visits each block once, but linking loop
  // sections together is linear in the loop size, so overall is
  // O(|B| + max(loop_depth) * max(|loop|))
  DCHECK(stack.empty());
  Push(&graph_->StartBlock());
  while (!stack.empty()) {
    SpecialRPOStackFrame& frame = stack.back();
    const Block* block = frame.block;
    const Block* succ = nullptr;

    if (frame.index < frame.successors.size()) {
      // Process the next normal successor.
      succ = frame.successors[frame.index++];
    } else if (has_loop_number(block)) {
      // Process additional outgoing edges from the loop header.
      if (rpo_number(block) == kBlockOnStack) {
        // Finish the loop body the first time the header is left on the
        // stack.
        DCHECK_NOT_NULL(loop);
        DCHECK_EQ(loop->header, block);
        loop->start = PushFront(order, block);
        order = loop->end;
        set_rpo_number(block, kBlockVisited2);
        // Pop the loop stack and continue visiting outgoing edges within
        // the context of the outer loop, if any.
        loop = loop->prev;
        // We leave the loop header on the stack; the rest of this iteration
        // and later iterations will go through its outgoing edges list.
      }

      // Use the next outgoing edge if there are any.
      size_t outgoing_index = frame.index - frame.successors.size();
      LoopInfo* info = &loops_[loop_number(block)];
      DCHECK_NE(loop, info);
      if (block != entry && outgoing_index < info->outgoing.size()) {
        succ = info->outgoing[outgoing_index];
        ++frame.index;
      }
    }

    if (succ != nullptr) {
      // Process the next successor.
      if (rpo_number(succ) == kBlockOnStack) continue;
      if (rpo_number(succ) == kBlockVisited2) continue;
      DCHECK_EQ(kBlockVisited1, rpo_number(succ));
      if (loop != nullptr && !loop->members->Contains(succ->index().id())) {
        // The successor is not in the current loop or any nested loop.
        // Add it to the outgoing edges of this loop and visit it later.
        loop->AddOutgoing(zone(), succ);
      } else {
        // Push the successor onto the stack.
        Push(succ);
        if (has_loop_number(succ)) {
          // Push the inner loop onto the loop stack.
          DCHECK_LT(loop_number(succ), num_loops);
          LoopInfo* next = &loops_[loop_number(succ)];
          next->end = order;
          next->prev = loop;
          loop = next;
        }
      }
    } else {
      // Finish with all successors of the current block.
      if (has_loop_number(block)) {
        // If we are going to pop a loop header, then add its entire body.
        LoopInfo* info = &loops_[loop_number(block)];
        for (const Block* b = info->start; true;
             b = block_data_[b->index()].rpo_next) {
          if (block_data_[b->index()].rpo_next == info->end) {
            PushFront(order, b);
            info->end = order;
            break;
          }
        }
        order = info->start;
      } else {
        // Pop a single node off the stack and add it to the order.
        order = PushFront(order, block);
        set_rpo_number(block, kBlockVisited2);
      }
      stack.pop_back();
    }
  }

  return ComputeBlockPermutation(entry);
}

// Computes loop membership from the backedges of the control flow graph.
void TurboshaftSpecialRPONumberer::ComputeLoopInfo(
    size_t num_loops, ZoneVector<Backedge>& backedges) {
  ZoneVector<const Block*> stack(zone());

  // Extend loop information vector.
  loops_.resize(num_loops, LoopInfo{});

  // Compute loop membership starting from backedges.
  // O(max(loop_depth) * |loop|)
  for (auto [backedge, header_index] : backedges) {
    const Block* header = SuccessorBlocks(*backedge, *graph_)[header_index];
    DCHECK(header->IsLoop());
    size_t loop_num = loop_number(header);
    DCHECK_NULL(loops_[loop_num].header);
    loops_[loop_num].header = header;
    loops_[loop_num].members = zone()->New<SparseBitVector>(zone());

    if (backedge != header) {
      // As long as the header doesn't have a backedge to itself,
      // Push the member onto the queue and process its predecessors.
      DCHECK(!loops_[loop_num].members->Contains(backedge->index().id()));
      loops_[loop_num].members->Add(backedge->index().id());
      stack.push_back(backedge);
    }

    // Propagate loop membership backwards. All predecessors of M up to the
    // loop header H are members of the loop too. O(|blocks between M and H|).
    while (!stack.empty()) {
      const Block* block = stack.back();
      stack.pop_back();
      for (const Block* pred : block->PredecessorsIterable()) {
        if (pred != header) {
          if (!loops_[loop_num].members->Contains(pred->index().id())) {
            loops_[loop_num].members->Add(pred->index().id());
            stack.push_back(pred);
          }
        }
      }
    }
  }
}

ZoneVector<uint32_t> TurboshaftSpecialRPONumberer::ComputeBlockPermutation(
    const Block* entry) {
  ZoneVector<uint32_t> result(graph_->block_count(), zone());
  size_t i = 0;
  for (const Block* b = entry; b; b = block_data_[b->index()].rpo_next) {
    result[i++] = b->index().id();
  }
  DCHECK_EQ(i, graph_->block_count());
  return result;
}

void PropagateDeferred(Graph& graph) {
  graph.StartBlock().set_custom_data(
      0, Block::CustomDataKind::kDeferredInSchedule);
  for (Block& block : graph.blocks()) {
    const Block* predecessor = block.LastPredecessor();
    if (predecessor == nullptr) {
      continue;
    } else if (block.IsLoop()) {
      // We only consider the forward edge for loop headers.
      predecessor = predecessor->NeighboringPredecessor();
      DCHECK_NOT_NULL(predecessor);
      DCHECK_EQ(predecessor->NeighboringPredecessor(), nullptr);
      block.set_custom_data(predecessor->get_custom_data(
                                Block::CustomDataKind::kDeferredInSchedule),
                            Block::CustomDataKind::kDeferredInSchedule);
    } else if (predecessor->NeighboringPredecessor() == nullptr) {
      // This block has only a single predecessor. Due to edge-split form, those
      // are the only blocks that can be the target of a branch-like op which
      // might potentially provide a BranchHint to defer this block.
      const bool is_deferred =
          predecessor->get_custom_data(
              Block::CustomDataKind::kDeferredInSchedule) ||
          IsUnlikelySuccessor(predecessor, &block, graph);
      block.set_custom_data(is_deferred,
                            Block::CustomDataKind::kDeferredInSchedule);
    } else {
      block.set_custom_data(true, Block::CustomDataKind::kDeferredInSchedule);
      for (; predecessor; predecessor = predecessor->NeighboringPredecessor()) {
        // If there is a single predecessor that is not deferred, then block is
        // also not deferred.
        if (!predecessor->get_custom_data(
                Block::CustomDataKind::kDeferredInSchedule)) {
          block.set_custom_data(false,
                                Block::CustomDataKind::kDeferredInSchedule);
          break;
        }
      }
    }
  }
}

void ProfileApplicationPhase::Run(PipelineData* data, Zone* temp_zone,
                                  const ProfileDataFromFile* profile) {
  Graph& graph = data->graph();
  for (auto& op : graph.AllOperations()) {
    if (BranchOp* branch = op.TryCast<BranchOp>()) {
      uint32_t true_block_id = branch->if_true->index().id();
      uint32_t false_block_id = branch->if_false->index().id();
      BranchHint hint = profile->GetHint(true_block_id, false_block_id);
      if (hint != BranchHint::kNone) {
        // We update the hint in-place.
        branch->hint = hint;
      }
    }
  }
}

void SpecialRPOSchedulingPhase::Run(PipelineData* data, Zone* temp_zone) {
  Graph& graph = data->graph();

  // Compute special RPO order....
  TurboshaftSpecialRPONumberer numberer(graph, temp_zone);
  if (!data->graph_has_special_rpo()) {
    auto schedule = numberer.ComputeSpecialRPO();
    graph.ReorderBlocks(base::VectorOf(schedule));
    data->set_graph_has_special_rpo();
  }

  // Determine deferred blocks.
  PropagateDeferred(graph);
}

std::optional<BailoutReason> InstructionSelectionPhase::Run(
    PipelineData* data, Zone* temp_zone, const CallDescriptor* call_descriptor,
    Linkage* linkage, CodeTracer* code_tracer) {
  Graph& graph = data->graph();

  // Initialize an instruction sequence.
  data->InitializeInstructionComponent(call_descriptor);

  // Run the actual instruction selection.
  InstructionSelector selector = InstructionSelector::ForTurboshaft(
      temp_zone, graph.op_id_count(), linkage, data->sequence(), &graph,
      data->frame(),
      data->info()->switch_jump_table()
          ? InstructionSelector::kEnableSwitchJumpTable
          : InstructionSelector::kDisableSwitchJumpTable,
      &data->info()->tick_counter(), data->broker(),
      &data->max_unoptimized_frame_height(), &data->max_pushed_argument_count(),
      data->info()->source_positions()
          ? InstructionSelector::kAllSourcePositions
          : InstructionSelector::kCallSourcePositions,
      InstructionSelector::SupportedFeatures(),
      v8_flags.turbo_instruction_scheduling
          ? InstructionSelector::kEnableScheduling
          : InstructionSelector::kDisableScheduling,
      data->assembler_options().enable_root_relative_access
          ? InstructionSelector::kEnableRootsRelativeAddressing
          : InstructionSelector::kDisableRootsRelativeAddressing,
      data->info()->trace_turbo_json()
          ? InstructionSelector::kEnableTraceTurboJson
          : InstructionSelector::kDisableTraceTurboJson);
  if (std::optional<BailoutReason> bailout = selector.SelectInstructions()) {
    return bailout;
  }
  TraceSequence(data->info(), data->sequence(), data->broker(), code_tracer,
                "after instruction selection");
  return std::nullopt;
}

}  // namespace v8::internal::compiler::turboshaft
