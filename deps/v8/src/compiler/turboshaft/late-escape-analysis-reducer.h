// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_

#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/utils.h"
#include "src/zone/zone-containers.h"
#include "src/zone/zone.h"

namespace v8::internal::compiler::turboshaft {

// LateEscapeAnalysis removes allocation that have no uses besides the stores
// initializing the object.

class LateEscapeAnalysisAnalyzer {
 public:
  LateEscapeAnalysisAnalyzer(Graph& graph, Zone* zone)
      : graph_(graph), phase_zone_(zone), alloc_uses_(zone), allocs_(zone) {}

  void Run();

 private:
  void RecordAllocateUse(OpIndex alloc, OpIndex use);

  void CollectUsesAndAllocations();
  void FindRemovableAllocations();
  bool AllocationIsEscaping(OpIndex alloc);
  bool EscapesThroughUse(OpIndex alloc, OpIndex using_op_idx);
  void MarkToRemove(OpIndex alloc);

  Graph& graph_;
  Zone* phase_zone_;

  // {alloc_uses_} records all the uses of each AllocateOp.
  ZoneAbslFlatHashMap<OpIndex, ZoneVector<OpIndex>> alloc_uses_;
  // {allocs_} is filled with all of the AllocateOp of the graph, and then
  // iterated upon to determine which allocations can be removed and which
  // cannot.
  ZoneVector<OpIndex> allocs_;
};

template <class Next>
class LateEscapeAnalysisReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(LateEscapeAnalysis)

  void Analyze() {
    analyzer_.Run();
    Next::Analyze();
  }

 private:
  LateEscapeAnalysisAnalyzer analyzer_{Asm().modifiable_input_graph(),
                                       Asm().phase_zone()};
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_LATE_ESCAPE_ANALYSIS_REDUCER_H_
