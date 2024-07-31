// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LOOP_FINDER_H_
#define V8_COMPILER_TURBOSHAFT_LOOP_FINDER_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8::internal::compiler::turboshaft {

class V8_EXPORT_PRIVATE LoopFinder {
  // This analyzer finds which loop each Block of a graph belongs to, and
  // computes a list of all of the loops headers.
  //
  // A block is considered to "belong to a loop" if there is a forward-path (ie,
  // without taking backedges) from this block to the backedge of the loop.
  //
  // This analysis runs in O(number of blocks), iterating each block once, and
  // iterating blocks that are in a loop twice.
  //
  // Implementation:
  // LoopFinder::Run walks the blocks of the graph backwards, and when it
  // reaches a LoopHeader, it calls LoopFinder::VisitLoop.
  // LoopFinder::VisitLoop iterates all of the blocks of the loop backwards,
  // starting from the backedge, and stopping upon reaching the loop header. It
  // marks the blocks that don't have a `parent_loops_` set as being part of the
  // current loop (= sets their `parent_loops_` to the current loop header). If
  // it finds a block that already has a `parent_loops_` set, it means that this
  // loop contains an inner loop, so we skip this inner block as set the
  // `has_inner_loops` bit.
  //
  // By iterating the blocks backwards in Run, we are guaranteed that inner
  // loops are visited before their outer loops. Walking the graph forward
  // doesn't work quite as nicely:
  //  - When seeing loop headers for the 1st time, we wouldn't have visited
  //    their inner loops yet.
  //  - If we decided to still iterate forward but to call VisitLoop when
  //    reaching their backedge rather than their header, it would work in most
  //    cases but not all, since the backedge of an outer loop can have a
  //    BlockIndex that is smaller than the one of an inner loop.
 public:
  struct LoopInfo {
    const Block* start = nullptr;
    const Block* end = nullptr;
    bool has_inner_loops = false;
    size_t block_count = 0;  // Number of blocks in this loop
                             // (excluding inner loops)
    size_t op_count = 0;     // Upper bound on the number of operations in this
                             // loop (excluding inner loops). This is computed
                             // using "end - begin" for each block, which can be
                             // more than the number of operations when some
                             // operations are large (like CallOp and
                             // FrameStateOp typically).
  };
  LoopFinder(Zone* phase_zone, const Graph* input_graph)
      : phase_zone_(phase_zone),
        input_graph_(input_graph),
        loop_headers_(input_graph->block_count(), nullptr, phase_zone),
        loop_header_info_(phase_zone),
        queue_(phase_zone) {
    Run();
  }

  const ZoneUnorderedMap<const Block*, LoopInfo>& LoopHeaders() const {
    return loop_header_info_;
  }
  const Block* GetLoopHeader(const Block* block) const {
    return loop_headers_[block->index()];
  }
  LoopInfo GetLoopInfo(const Block* block) const {
    DCHECK(block->IsLoop());
    auto it = loop_header_info_.find(block);
    DCHECK_NE(it, loop_header_info_.end());
    return it->second;
  }

  struct BlockCmp {
    bool operator()(const Block* a, const Block* b) const {
      return a->index().id() < b->index().id();
    }
  };
  ZoneSet<const Block*, BlockCmp> GetLoopBody(const Block* loop_header);

 private:
  void Run();
  LoopInfo VisitLoop(const Block* header);

  Zone* phase_zone_;
  const Graph* input_graph_;

  // Map from block to the loop header of the closest enclosing loop. For loop
  // headers, this map contains the enclosing loop header, rather than the
  // identity.
  // For instance, if a loop B1 contains a loop B2 which contains a block B3,
  // {loop_headers_} will map:
  //   B3 -> B2
  //   B2 -> B1
  //   B1 -> nullptr (if B1 is an outermost loop)
  FixedBlockSidetable<const Block*> loop_headers_;

  // Map from Loop headers to the LoopInfo for their loops. Only Loop blocks
  // have entries in this map.
  ZoneUnorderedMap<const Block*, LoopInfo> loop_header_info_;

  // {queue_} is used in `VisitLoop`, but is declared as a class variable to
  // reuse memory.
  ZoneVector<const Block*> queue_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LOOP_FINDER_H_
