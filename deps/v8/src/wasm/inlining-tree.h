// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_WASM_INLINING_TREE_H_
#define V8_WASM_INLINING_TREE_H_

#include <cstdint>
#include <queue>
#include <vector>

#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-module.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::wasm {

// Represents a tree of inlining decisions.
// A node in the tree represents a function frame, and `function_calls_`
// represent all direct/call_ref function calls in this frame. Each element of
// `function_calls_` is itself a `Vector` of `InliningTree`s, corresponding to
// the different speculative candidates for a call_ref; for a direct call, it
// has a single element. If an transitive element of `function_calls_` has its `
// `is_inlined_` field set, it should be inlined into the caller.
class InliningTree : public ZoneObject {
 public:
  using CasesPerCallSite = base::Vector<InliningTree*>;

  InliningTree(Zone* zone, const WasmModule* module, uint32_t function_index,
               int call_count, int wire_byte_size,
               uint32_t topmost_caller_index, uint32_t caller_index,
               int feedback_slot, int the_case, uint32_t depth)
      : zone_(zone),
        module_(module),
        function_index_(function_index),
        call_count_(call_count),
        wire_byte_size_(wire_byte_size),
        depth_(depth),
        topmost_caller_index_(topmost_caller_index),
        caller_index_(caller_index),
        feedback_slot_(feedback_slot),
        case_(the_case) {}

  int64_t score() const {
    // Note that the zero-point is arbitrary. Functions with negative score
    // can still get inlined.
    constexpr int count_factor = 2;
    constexpr int size_factor = 3;
    return int64_t{call_count_} * count_factor -
           int64_t{wire_byte_size_} * size_factor;
  }

  static constexpr int kMaxInlinedCount = 60;

  // Recursively expand the tree by expanding this node and children nodes etc.
  // Nodes are prioritized by their `score`. Expansion continues until
  // `kMaxInlinedCount` nodes are expanded or `budget` (in wire-bytes size) is
  // depleted.
  void FullyExpand(const size_t initial_graph_size);
  base::Vector<CasesPerCallSite> function_calls() { return function_calls_; }
  bool feedback_found() { return feedback_found_; }
  bool is_inlined() { return is_inlined_; }
  uint32_t function_index() { return function_index_; }

 private:
  // Mark this function call as inline and initialize `function_calls_` based
  // on the `module_->type_feedback`.
  void Inline();
  bool SmallEnoughToInline(size_t initial_graph_size,
                           size_t inlined_wire_byte_count);

  // TODO(14108): Do not store these in every tree node.
  Zone* zone_;
  const WasmModule* module_;

  uint32_t function_index_;
  int call_count_;
  int wire_byte_size_;
  bool is_inlined_ = false;
  bool feedback_found_ = false;

  base::Vector<CasesPerCallSite> function_calls_{};

  // Limit the nesting depth of inlining. Inlining decisions are based on call
  // counts. A small function with high call counts that is called recursively
  // would be inlined until all budget is used.
  // TODO(14108): This still might not lead to ideal results. Other options
  // could be explored like penalizing nested inlinees.
  static constexpr uint32_t kMaxInliningNestingDepth = 7;
  uint32_t depth_;

  // For tracing.
  // TODO(14108): Do not store all of these in every tree node.
  uint32_t topmost_caller_index_;
  uint32_t caller_index_;
  int feedback_slot_;
  int case_;
};

void InliningTree::Inline() {
  is_inlined_ = true;
  auto feedback =
      module_->type_feedback.feedback_for_function.find(function_index_);
  if (feedback != module_->type_feedback.feedback_for_function.end() &&
      feedback->second.feedback_vector.size() ==
          feedback->second.call_targets.size()) {
    std::vector<CallSiteFeedback>& type_feedback =
        feedback->second.feedback_vector;
    feedback_found_ = true;
    function_calls_ =
        zone_->AllocateVector<CasesPerCallSite>(type_feedback.size());
    for (size_t i = 0; i < type_feedback.size(); i++) {
      function_calls_[i] =
          zone_->AllocateVector<InliningTree*>(type_feedback[i].num_cases());
      for (int the_case = 0; the_case < type_feedback[i].num_cases();
           the_case++) {
        uint32_t callee_index = type_feedback[i].function_index(the_case);
        // TODO(jkummerow): Experiment with propagating relative call counts
        // into the nested InliningTree, and weighting scores there accordingly.
        function_calls_[i][the_case] = zone_->New<InliningTree>(
            zone_, module_, callee_index, type_feedback[i].call_count(the_case),
            module_->functions[callee_index].code.length(),
            topmost_caller_index_, function_index_, static_cast<int>(i),
            the_case, depth_ + 1);
      }
    }
  }
}

struct TreeNodeOrdering {
  bool operator()(InliningTree* t1, InliningTree* t2) {
    return t1->score() < t2->score();
  }
};

void InliningTree::FullyExpand(const size_t initial_graph_size) {
  DCHECK_EQ(this->function_index_, this->topmost_caller_index_);
  size_t inlined_wire_byte_count = 0;
  std::priority_queue<InliningTree*, std::vector<InliningTree*>,
                      TreeNodeOrdering>
      queue;
  queue.push(this);
  int inlined_count = 0;
  base::SharedMutexGuard<base::kShared> mutex_guard(
      &module_->type_feedback.mutex);
  while (!queue.empty() && inlined_count < kMaxInlinedCount) {
    InliningTree* top = queue.top();
    if (v8_flags.trace_wasm_inlining) {
      if (top != this) {
        PrintF(
            "[function %d: in function %d, considering call #%d, case #%d, to "
            "function %d... ",
            top->topmost_caller_index_, top->caller_index_, top->feedback_slot_,
            static_cast<int>(top->case_),
            static_cast<int>(top->function_index_));
      } else {
        PrintF("[function %d: expanding topmost caller... ",
               top->topmost_caller_index_);
      }
    }
    queue.pop();
    if (top->function_index_ < module_->num_imported_functions) {
      if (v8_flags.trace_wasm_inlining && top != this) {
        PrintF("imported function]\n");
      }
      continue;
    }

    int min_count_for_inlining = v8_flags.wasm_inlining_ignore_call_counts
                                     ? 0
                                     : top->wire_byte_size_ / 2;
    if (top != this && top->wire_byte_size_ >= 12 &&
        (top->call_count_ < min_count_for_inlining)) {
      if (v8_flags.trace_wasm_inlining) {
        PrintF("not called often enough]\n");
      }
      continue;
    }

    if (!top->SmallEnoughToInline(initial_graph_size,
                                  inlined_wire_byte_count)) {
      if (v8_flags.trace_wasm_inlining && top != this) {
        PrintF("not enough inlining budget]\n");
      }
      continue;
    }
    if (v8_flags.trace_wasm_inlining && top != this) {
      PrintF("decided to inline! ");
    }
    top->Inline();
    inlined_count++;
    inlined_wire_byte_count += top->wire_byte_size_;
    if (top->feedback_found()) {
      if (top->depth_ < kMaxInliningNestingDepth) {
        if (v8_flags.trace_wasm_inlining) PrintF("queueing callees]\n");
        for (CasesPerCallSite cases : top->function_calls_) {
          for (InliningTree* call : cases) {
            if (call != nullptr) {
              queue.push(call);
            }
          }
        }
      } else if (v8_flags.trace_wasm_inlining) {
        PrintF("max inlining depth reached]\n");
      }
    } else {
      if (v8_flags.trace_wasm_inlining) PrintF("feedback not found]\n");
    }
  }
  if (v8_flags.trace_wasm_inlining && !queue.empty()) {
    PrintF("[function %d: too many inlining candidates, stopping...]\n",
           this->topmost_caller_index_);
  }
}

// Returns true if there is still enough budget left to inline the current
// candidate given the initial graph size and the already inlined wire bytes.
// TODO(mliedtke): The upper_budget calculation only depends on the module, not
// on the callsite / callee. Consider moving this to a more central place and
// propagating the information along the inlining tree.
bool InliningTree::SmallEnoughToInline(size_t initial_graph_size,
                                       size_t inlined_wire_byte_count) {
  if (wire_byte_size_ > static_cast<int>(v8_flags.wasm_inlining_max_size)) {
    return false;
  }
  // For tiny functions, let's be a bit more generous.
  if (wire_byte_size_ < 12) {
    if (inlined_wire_byte_count > 100) {
      inlined_wire_byte_count -= 100;
    } else {
      inlined_wire_byte_count = 0;
    }
  }
  // For small-ish functions, the inlining budget is defined by the larger of
  // 1) the wasm_inlining_budget and
  // 2) the wasm_inlining_factor * initial_graph_size.
  // Inlining a little bit should always be fine even for tiny functions (1),
  // otherwise (2) makes sure that the budget scales in relation with the
  // original function size to limit the compile time regressions caused by
  // inlining.
  size_t budget_small_function =
      std::max<size_t>(v8_flags.wasm_inlining_min_budget,
                       v8_flags.wasm_inlining_factor * initial_graph_size);
  // For large-ish functions, the inlining budget is mainly defined by the
  // wasm_inlining_budget.
  size_t upper_budget = v8_flags.wasm_inlining_budget;
  double small_function_percentage =
      module_->num_small_functions * 100.0 / module_->num_declared_functions;
  if (small_function_percentage < 50) {
    // If there are few small functions, it indicates that the toolchain already
    // performed significant inlining. Reduce the budget significantly as
    // inlining has a diminishing ROI.

    // We also apply a linear progression of the budget in the interval [25, 50]
    // for the small_function_percentage. This progression is just added to
    // prevent performance cliffs (e.g. when just performing a sharp cutoff at
    // the 50% point) and not based on actual data.
    double smallishness = std::max(25.0, small_function_percentage) - 25.0;
    size_t lower_budget = upper_budget / 10;
    double step = (upper_budget - lower_budget) / 25.0;
    upper_budget = lower_budget + smallishness * step;
  }
  // Independent of the wasm_inlining_budget, for large functions we should
  // still allow some inlining which is why 10% of the graph size is the minimal
  // budget even for large functions larger than the upper_budget.
  size_t budget_large_function =
      std::max<size_t>(upper_budget, initial_graph_size * 1.1);
  size_t total_size = initial_graph_size + inlined_wire_byte_count +
                      static_cast<size_t>(wire_byte_size_);
  return total_size <
         std::min<size_t>(budget_small_function, budget_large_function);
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_INLINING_TREE_H_
