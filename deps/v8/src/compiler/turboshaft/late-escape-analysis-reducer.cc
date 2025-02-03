// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/late-escape-analysis-reducer.h"

namespace v8::internal::compiler::turboshaft {

void LateEscapeAnalysisAnalyzer::Run() {
  CollectUsesAndAllocations();
  FindRemovableAllocations();
}

void LateEscapeAnalysisAnalyzer::RecordAllocateUse(OpIndex alloc, OpIndex use) {
  auto [it, new_entry] = alloc_uses_.try_emplace(alloc, phase_zone_);
  auto& uses = it->second;
  if (new_entry) {
    uses.reserve(graph_.Get(alloc).saturated_use_count.Get());
  }
  uses.push_back(use);
}

// Collects the Allocate Operations and their uses.
void LateEscapeAnalysisAnalyzer::CollectUsesAndAllocations() {
  for (auto& op : graph_.AllOperations()) {
    if (ShouldSkipOperation(op)) continue;
    OpIndex op_index = graph_.Index(op);
    for (OpIndex input : op.inputs()) {
      if (graph_.Get(input).Is<AllocateOp>()) {
        RecordAllocateUse(input, op_index);
      }
    }
    if (op.Is<AllocateOp>()) {
      allocs_.push_back(op_index);
    }
  }
}

void LateEscapeAnalysisAnalyzer::FindRemovableAllocations() {
  while (!allocs_.empty()) {
    OpIndex current_alloc = allocs_.back();
    allocs_.pop_back();

    if (ShouldSkipOperation(graph_.Get(current_alloc))) {
      // We are re-visiting an allocation that we've actually already removed.
      continue;
    }

    if (!AllocationIsEscaping(current_alloc)) {
      MarkToRemove(current_alloc);
    }
  }
}

bool LateEscapeAnalysisAnalyzer::AllocationIsEscaping(OpIndex alloc) {
  if (alloc_uses_.find(alloc) == alloc_uses_.end()) return false;
  for (OpIndex use : alloc_uses_.at(alloc)) {
    if (EscapesThroughUse(alloc, use)) return true;
  }
  // We haven't found any non-store use
  return false;
}

// Returns true if {using_op_idx} is an operation that forces {alloc} to be
// emitted.
bool LateEscapeAnalysisAnalyzer::EscapesThroughUse(OpIndex alloc,
                                                   OpIndex using_op_idx) {
  if (ShouldSkipOperation(graph_.Get(alloc))) {
    // {using_op_idx} is an Allocate itself, which has been removed.
    return false;
  }
  const Operation& op = graph_.Get(using_op_idx);
  if (const StoreOp* store_op = op.TryCast<StoreOp>()) {
    // A StoreOp only makes {alloc} escape if it uses {alloc} as the {value} or
    // the {index}. Put otherwise, StoreOp makes {alloc} escape if it writes
    // {alloc}, but not if it writes **to** {alloc}.
    return store_op->value() == alloc;
  }
  return true;
}

void LateEscapeAnalysisAnalyzer::MarkToRemove(OpIndex alloc) {
  if (ShouldSkipOptimizationStep()) return;
  graph_.KillOperation(alloc);
  if (alloc_uses_.find(alloc) == alloc_uses_.end()) {
    return;
  }

  // The uses of {alloc} should also be skipped.
  for (OpIndex use : alloc_uses_.at(alloc)) {
    const StoreOp& store = graph_.Get(use).Cast<StoreOp>();
    if (graph_.Get(store.value()).Is<AllocateOp>()) {
      // This store was storing the result of an allocation. Because we now
      // removed this store, we might be able to remove the other allocation
      // as well.
      allocs_.push_back(store.value());
    }
    graph_.KillOperation(use);
  }
}

}  // namespace v8::internal::compiler::turboshaft
