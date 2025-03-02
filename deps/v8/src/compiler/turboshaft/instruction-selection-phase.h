// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_INSTRUCTION_SELECTION_PHASE_H_
#define V8_COMPILER_TURBOSHAFT_INSTRUCTION_SELECTION_PHASE_H_

#include <optional>

#include "src/compiler/turboshaft/phase.h"

namespace v8::internal {
class ProfileDataFromFile;
class SparseBitVector;
}

namespace v8::internal::compiler::turboshaft {

// Compute the special reverse-post-order block ordering, which is essentially
// a RPO of the graph where loop bodies are contiguous. Properties:
// 1. If block A is a predecessor of B, then A appears before B in the order,
//    unless B is a loop header and A is in the loop headed at B
//    (i.e. A -> B is a backedge).
// => If block A dominates block B, then A appears before B in the order.
// => If block A is a loop header, A appears before all blocks in the loop
//    headed at A.
// 2. All loops are contiguous in the order (i.e. no intervening blocks that
//    do not belong to the loop.)
// Note a simple RPO traversal satisfies (1) but not (2).
// TODO(nicohartmann@): Investigate faster and simpler alternatives.
class V8_EXPORT_PRIVATE TurboshaftSpecialRPONumberer {
 public:
  // Numbering for BasicBlock::rpo_number for this block traversal:
  static const int kBlockOnStack = -2;
  static const int kBlockVisited1 = -3;
  static const int kBlockVisited2 = -4;
  static const int kBlockUnvisited = -1;

  using Backedge = std::pair<const Block*, size_t>;

  struct SpecialRPOStackFrame {
    const Block* block = nullptr;
    size_t index = 0;
    base::SmallVector<Block*, 4> successors;

    SpecialRPOStackFrame(const Block* block, size_t index,
                         base::SmallVector<Block*, 4> successors)
        : block(block), index(index), successors(std::move(successors)) {}
  };

  struct LoopInfo {
    const Block* header;
    base::SmallVector<Block const*, 4> outgoing;
    SparseBitVector* members;
    LoopInfo* prev;
    const Block* end;
    const Block* start;

    void AddOutgoing(Zone* zone, const Block* block) {
      outgoing.push_back(block);
    }
  };

  struct BlockData {
    static constexpr size_t kNoLoopNumber = std::numeric_limits<size_t>::max();
    int32_t rpo_number = kBlockUnvisited;
    size_t loop_number = kNoLoopNumber;
    const Block* rpo_next = nullptr;
  };

  TurboshaftSpecialRPONumberer(const Graph& graph, Zone* zone)
      : graph_(&graph), block_data_(graph.block_count(), zone), loops_(zone) {}

  ZoneVector<uint32_t> ComputeSpecialRPO();

 private:
  void ComputeLoopInfo(size_t num_loops, ZoneVector<Backedge>& backedges);
  ZoneVector<uint32_t> ComputeBlockPermutation(const Block* entry);

  int32_t rpo_number(const Block* block) const {
    return block_data_[block->index()].rpo_number;
  }

  void set_rpo_number(const Block* block, int32_t rpo_number) {
    block_data_[block->index()].rpo_number = rpo_number;
  }

  bool has_loop_number(const Block* block) const {
    return block_data_[block->index()].loop_number != BlockData::kNoLoopNumber;
  }

  size_t loop_number(const Block* block) const {
    DCHECK(has_loop_number(block));
    return block_data_[block->index()].loop_number;
  }

  void set_loop_number(const Block* block, size_t loop_number) {
    block_data_[block->index()].loop_number = loop_number;
  }

  const Block* PushFront(const Block* head, const Block* block) {
    block_data_[block->index()].rpo_next = head;
    return block;
  }

  Zone* zone() const { return loops_.zone(); }

  const Graph* graph_;
  FixedBlockSidetable<BlockData> block_data_;
  ZoneVector<LoopInfo> loops_;
};

V8_EXPORT_PRIVATE void PropagateDeferred(Graph& graph);

struct ProfileApplicationPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(ProfileApplication)

  void Run(PipelineData* data, Zone* temp_zone,
           const ProfileDataFromFile* profile);
};

struct SpecialRPOSchedulingPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(SpecialRPOScheduling)

  void Run(PipelineData* data, Zone* temp_zone);
};

struct InstructionSelectionPhase {
  DECL_TURBOSHAFT_PHASE_CONSTANTS(InstructionSelection)
  static constexpr bool kOutputIsTraceableGraph = false;

  std::optional<BailoutReason> Run(PipelineData* data, Zone* temp_zone,
                                   const CallDescriptor* call_descriptor,
                                   Linkage* linkage, CodeTracer* code_tracer);
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_INSTRUCTION_SELECTION_PHASE_H_
