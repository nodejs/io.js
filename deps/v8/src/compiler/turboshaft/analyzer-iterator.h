// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_
#define V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"

namespace v8::internal::compiler::turboshaft {

// AnalyzerIterator provides methods to iterate forward a Graph in a way that is
// efficient for the SnapshotTable: blocks that are close in the graphs will be
// visited somewhat consecutively (which means that the SnapshotTable shouldn't
// have to travel far).
//
// To understand why this is important, consider the following graph:
//
//                          B1 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B27---
//                          v
//                          B2 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B26---
//                          v
//                          B3 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B25---
//                          v
//                         ...
//
// If we iterate its blocks in increasing ID order, then we'll visit B1, B2,
// B3... and only afterwards will we visit the Backedges. If said backedges can
// update the loop headers snapshots, then when visiting B25, we'll decide to
// revisit starting from B3, and will revisit everything after, then same thing
// for B26 after which we'll start over from B2 (and thus even revisit B3 and
// B25), etc, leading to a quadratic (in the number of blocks) analysis.
//
// Instead, the visitation order offered by AnalyzerIterator is a BFS in the
// dominator tree (ie, after visiting a node, AnalyzerIterator visit the nodes
// it dominates), with an subtlety for loops: when a node dominates multiple
// nodes, successors that are in the same loop as the current node are visited
// before nodes that are in outer loops.
// In the example above, the visitation order would thus be B1, B27, B2, B26,
// B3, B25.
//
// The MarkLoopForRevisit method can be used when visiting a backedge to
// instruct AnalyzerIterator that the loop to which this backedge belongs should
// be revisited. All of the blocks of this loop will then be revisited.
//
// Implementation details for revisitation of loops:
//
// In order to avoid visiting loop exits (= blocks whose dominator is in a loop
// but which aren't themselves in the loop) multiple times, the stack of Blocks
// to visit contains pairs of "block, generation". Additionally, we have a
// global {current_generation_} counter, which is incremented when we revisit a
// loop. When visiting a block, we record in {visited_} that it has been visited
// at {current_generation_}. When we pop a block from the stack and its
// "generation" field is less than what is recorded in {visited_}, then we skip
// it. On the other hand, if its "generation" field is greater than the one
// recorded in {visited_}, it means that we've revisited a loop since the last
// time we visited this block, so we should revisit it as well.

class V8_EXPORT_PRIVATE AnalyzerIterator {
 public:
  AnalyzerIterator(Zone* phase_zone, const Graph& graph,
                   const LoopFinder& loop_finder)
      : graph_(graph),
        loop_finder_(loop_finder),
        visited_(graph.block_count(), kNotVisitedGeneration, phase_zone),
        stack_(phase_zone) {
    stack_.push_back({&graph.StartBlock(), kGenerationForFirstVisit});
  }

  bool HasNext() const {
    DCHECK_IMPLIES(!stack_.empty(), !IsOutdated(stack_.back()));
    return !stack_.empty();
  }
  const Block* Next();
  // Schedule the loop pointed to by the current block (as a backedge)
  // to be revisited on the next iteration.
  void MarkLoopForRevisit();
  // Schedule the loop pointed to by the current block (as a backedge) to be
  // revisited on the next iteration but skip the loop header.
  void MarkLoopForRevisitSkipHeader();

 private:
  struct StackNode {
    const Block* block;
    uint64_t generation;
  };
  static constexpr uint64_t kNotVisitedGeneration = 0;
  static constexpr uint64_t kGenerationForFirstVisit = 1;

  void PopOutdated();
  bool IsOutdated(StackNode node) const {
    return visited_[node.block->index()] >= node.generation;
  }

  const Graph& graph_;
  const LoopFinder& loop_finder_;

  uint64_t current_generation_ = kGenerationForFirstVisit;

  // The last block returned by Next.
  StackNode curr_ = {nullptr, 0};

  // {visited_} maps BlockIndex to the generation they were visited with. If a
  // Block has been visited with a generation `n`, then we never want to revisit
  // it with a generation `k` when `k <= n`.
  FixedBlockSidetable<uint64_t> visited_;

  // The stack of blocks that are left to visit. We maintain the invariant that
  // the .back() of {stack_} is never out-dated (ie, its generation is always
  // greater than the generation for its node recorded in {visited_}), so that
  // "Next" can simply check whether {stack_} is empty or not.
  ZoneVector<StackNode> stack_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_
