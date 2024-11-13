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

#include "src/utils/utils.h"
#include "src/wasm/compilation-environment.h"
#include "src/wasm/wasm-module.h"

namespace v8::internal::wasm {

// Represents a tree of inlining decisions.
// A node in the tree represents a function frame, and `function_calls_`
// represent all direct/call_ref/call_indirect function calls in this frame.
// Each element of `function_calls_` is itself a `Vector` of `InliningTree`s,
// corresponding to the different speculative candidates for a
// call_ref/call_indirect; for a direct call, it has a single element.
// If a transitive element of `function_calls_` has its `is_inlined_` field set,
// it should be inlined into the caller.
// We have this additional datastructure for Turboshaft, since nodes in the
// Turboshaft IR aren't easily expanded incrementally, so all the inlining
// decisions are already made before graph building on this abstracted form of
// the code.
class InliningTree : public ZoneObject {
 private:
  struct Data;

 public:
  using CasesPerCallSite = base::Vector<InliningTree*>;

  static InliningTree* CreateRoot(Zone* zone, const WasmModule* module,
                                  uint32_t function_index) {
    InliningTree* tree = zone->New<InliningTree>(
        zone->New<Data>(zone, module, function_index), function_index,
        0,           // Call count.
        0,           // Wire byte size. `0` causes the root node to always get
                     // expanded, regardless of budget.
        -1, -1, -1,  // Caller, feedback slot, case.
        0            // Inlining depth.
    );
    tree->FullyExpand();
    return tree;
  }

  // This should stay roughly in sync with the full logic below, but not rely
  // on having observed any call counts. Since it therefore can't simulate
  // regular behavior accurately anyway, it may be a very coarse approximation.
  static int NoLiftoffBudget(const WasmModule* module, uint32_t func_index) {
    size_t wirebytes = module->functions[func_index].code.length();
    double scaled = BudgetScaleFactor(module);
    // TODO(jkummerow): When TF is gone, remove this adjustment by folding
    // it into the flag's default value.
    constexpr int kTurboshaftAdjustment = 2;
    int high_growth =
        static_cast<int>(v8_flags.wasm_inlining_factor) + kTurboshaftAdjustment;
    constexpr int kLowestUsefulValue = 2;
    int low_growth = std::max(kLowestUsefulValue, high_growth - 3);
    double max_growth_factor = low_growth * (1 - scaled) + high_growth * scaled;
    return std::max(static_cast<int>(v8_flags.wasm_inlining_min_budget),
                    static_cast<int>(max_growth_factor * wirebytes));
  }

  int64_t score() const {
    // Note that the zero-point is arbitrary. Functions with negative score
    // can still get inlined.
    constexpr int count_factor = 2;
    constexpr int size_factor = 3;
    return int64_t{call_count_} * count_factor -
           int64_t{wire_byte_size_} * size_factor;
  }

  // TODO(dlehmann,manoskouk): We are running into this limit, e.g., for the
  // "argon2-wasm" benchmark.
  // IIUC, this limit is in place because of the encoding of inlining IDs in
  // a 6-bit bitfield in Turboshaft IR, which we should revisit.
  static constexpr int kMaxInlinedCount = 60;

  base::Vector<CasesPerCallSite> function_calls() { return function_calls_; }
  base::Vector<bool> has_non_inlineable_targets() {
    return has_non_inlineable_targets_;
  }
  bool feedback_found() { return feedback_found_; }
  bool is_inlined() { return is_inlined_; }
  uint32_t function_index() { return function_index_; }

 private:
  friend class v8::internal::Zone;  // For `zone->New<InliningTree>`.

  static double BudgetScaleFactor(const WasmModule* module) {
    // If there are few small functions, that indicates that the toolchain
    // already performed significant inlining, so we reduce the budget
    // significantly as further inlining has diminishing benefits.
    // For both major knobs, we apply a smoothened step function based on
    // the module's percentage of small functions (sfp):
    //   sfp <= 25%: use "low" budget
    //   sfp >= 50%: use "high" budget
    //   25% < sfp < 50%: interpolate linearly between both budgets.
    double small_function_percentage =
        module->num_small_functions * 100.0 / module->num_declared_functions;
    if (small_function_percentage <= 25) {
      return 0;
    } else if (small_function_percentage >= 50) {
      return 1;
    } else {
      return (small_function_percentage - 25) / 25;
    }
  }

  struct Data {
    Data(Zone* zone, const WasmModule* module, uint32_t topmost_caller_index)
        : zone(zone),
          module(module),
          topmost_caller_index(topmost_caller_index) {
      double scaled = BudgetScaleFactor(module);
      // We found experimentally that we need to allow a larger growth factor
      // for Turboshaft to achieve similar inlining decisions as in Turbofan;
      // presumably because some functions that have a small wire size of their
      // own still need to be allowed to inline some callees.
      // TODO(jkummerow): When TF is gone, remove this adjustment by folding
      // it into the flag's default value.
      constexpr int kTurboshaftAdjustment = 2;
      int high_growth = static_cast<int>(v8_flags.wasm_inlining_factor) +
                        kTurboshaftAdjustment;
      // A value of 1 would be equivalent to disabling inlining entirely.
      constexpr int kLowestUsefulValue = 2;
      int low_growth = std::max(kLowestUsefulValue, high_growth - 3);
      max_growth_factor = low_growth * (1 - scaled) + high_growth * scaled;
      // The {wasm_inlining_budget} value has been tuned for Turbofan node
      // counts. Turboshaft looks at wire bytes instead, and on average there
      // are about 0.74 TF nodes per wire byte, so we apply a small factor to
      // account for the difference, so we get similar inlining decisions in
      // both compilers.
      // TODO(jkummerow): When TF is gone, remove this factor by folding it
      // into the flag's default value.
      constexpr double kTurboshaftCorrectionFactor = 1.4;
      double high_cap =
          v8_flags.wasm_inlining_budget * kTurboshaftCorrectionFactor;
      double low_cap = high_cap / 10;
      budget_cap = low_cap * (1 - scaled) + high_cap * scaled;
    }

    Zone* zone;
    const WasmModule* module;
    double max_growth_factor;
    size_t budget_cap;
    uint32_t topmost_caller_index;
  };

  InliningTree(Data* shared, uint32_t function_index, int call_count,
               int wire_byte_size, uint32_t caller_index, int feedback_slot,
               int the_case, uint32_t depth)
      : data_(shared),
        function_index_(function_index),
        call_count_(call_count),
        wire_byte_size_(wire_byte_size),
        depth_(depth),
        caller_index_(caller_index),
        feedback_slot_(feedback_slot),
        case_(the_case) {}

  // Recursively expand the tree by expanding this node and children nodes etc.
  // Nodes are prioritized by their `score`. Expansion continues until
  // `kMaxInlinedCount` nodes are expanded or `budget` (in wire-bytes size) is
  // depleted.
  void FullyExpand();

  // Mark this function call as inline and initialize `function_calls_` based
  // on the `module_->type_feedback`.
  void Inline();
  bool SmallEnoughToInline(size_t initial_wire_byte_size,
                           size_t inlined_wire_byte_count);

  Data* data_;
  uint32_t function_index_;
  int call_count_;
  int wire_byte_size_;
  bool is_inlined_ = false;
  bool feedback_found_ = false;

  base::Vector<CasesPerCallSite> function_calls_{};
  base::Vector<bool> has_non_inlineable_targets_{};

  // Limit the nesting depth of inlining. Inlining decisions are based on call
  // counts. A small function with high call counts that is called recursively
  // would be inlined until all budget is used.
  // TODO(14108): This still might not lead to ideal results. Other options
  // could be explored like penalizing nested inlinees.
  static constexpr uint32_t kMaxInliningNestingDepth = 7;
  uint32_t depth_;

  // For tracing.
  uint32_t caller_index_;
  int feedback_slot_;
  int case_;
};

void InliningTree::Inline() {
  is_inlined_ = true;
  auto feedback =
      data_->module->type_feedback.feedback_for_function.find(function_index_);
  if (feedback != data_->module->type_feedback.feedback_for_function.end() &&
      feedback->second.feedback_vector.size() ==
          feedback->second.call_targets.size()) {
    std::vector<CallSiteFeedback>& type_feedback =
        feedback->second.feedback_vector;
    feedback_found_ = true;
    function_calls_ =
        data_->zone->AllocateVector<CasesPerCallSite>(type_feedback.size());
    has_non_inlineable_targets_ =
        data_->zone->AllocateVector<bool>(type_feedback.size());
    for (size_t i = 0; i < type_feedback.size(); i++) {
      function_calls_[i] = data_->zone->AllocateVector<InliningTree*>(
          type_feedback[i].num_cases());
      has_non_inlineable_targets_[i] =
          type_feedback[i].has_non_inlineable_targets();
      for (int the_case = 0; the_case < type_feedback[i].num_cases();
           the_case++) {
        uint32_t callee_index = type_feedback[i].function_index(the_case);
        // TODO(jkummerow): Experiment with propagating relative call counts
        // into the nested InliningTree, and weighting scores there accordingly.
        function_calls_[i][the_case] = data_->zone->New<InliningTree>(
            data_, callee_index, type_feedback[i].call_count(the_case),
            data_->module->functions[callee_index].code.length(),
            function_index_, static_cast<int>(i), the_case, depth_ + 1);
      }
    }
  }
}

struct TreeNodeOrdering {
  bool operator()(InliningTree* t1, InliningTree* t2) {
    // Prefer callees with a higher score, and if the scores are equal,
    // those with a lower function index (to make the queue ordering strict).
    return std::make_pair(t1->score(), t2->function_index()) <
           std::make_pair(t2->score(), t1->function_index());
  }
};

void InliningTree::FullyExpand() {
  DCHECK_EQ(this->function_index_, data_->topmost_caller_index);
  size_t initial_wire_byte_size =
      data_->module->functions[function_index_].code.length();
  size_t inlined_wire_byte_count = 0;
  std::priority_queue<InliningTree*, std::vector<InliningTree*>,
                      TreeNodeOrdering>
      queue;
  queue.push(this);
  int inlined_count = 0;
  base::SharedMutexGuard<base::kShared> mutex_guard(
      &data_->module->type_feedback.mutex);
  while (!queue.empty() && inlined_count < kMaxInlinedCount) {
    InliningTree* top = queue.top();
    if (v8_flags.trace_wasm_inlining) {
      if (top != this) {
        PrintF(
            "[function %d: in function %d, considering call #%d, case #%d, to "
            "function %d (count=%d, size=%d, score=%lld)... ",
            data_->topmost_caller_index, top->caller_index_,
            top->feedback_slot_, static_cast<int>(top->case_),
            static_cast<int>(top->function_index_), top->call_count_,
            top->wire_byte_size_, static_cast<long long>(top->score()));
      } else {
        PrintF("[function %d: expanding topmost caller... ",
               data_->topmost_caller_index);
      }
    }
    queue.pop();
    if (top->function_index_ < data_->module->num_imported_functions) {
      if (v8_flags.trace_wasm_inlining && top != this) {
        PrintF("imported function]\n");
      }
      continue;
    }

    // Key idea: inlining hot calls is good, inlining big functions is bad,
    // so inline when a candidate is "hotter than it is big". Exception:
    // tiny candidates can get inlined regardless of their call count.
    if (top != this && top->wire_byte_size_ >= 12 &&
        !v8_flags.wasm_inlining_ignore_call_counts) {
      if (top->call_count_ < top->wire_byte_size_ / 2) {
        if (v8_flags.trace_wasm_inlining) {
          PrintF("not called often enough]\n");
        }
        continue;
      }
    }

    if (!top->SmallEnoughToInline(initial_wire_byte_size,
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
    // For tiny functions, inlining may actually decrease generated code size
    // because we have one less call and don't need to push arguments, etc.
    // Subtract a little bit from the code size increase, such that inlining
    // these tiny functions doesn't use up any of the budget.
    constexpr int kOneLessCall = 6;  // Guesstimated savings per call.
    inlined_wire_byte_count += std::max(top->wire_byte_size_ - kOneLessCall, 0);

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
           data_->topmost_caller_index);
  }
}

// Returns true if there is still enough budget left to inline the current
// candidate given the initial graph size and the already inlined wire bytes.
bool InliningTree::SmallEnoughToInline(size_t initial_wire_byte_size,
                                       size_t inlined_wire_byte_count) {
  if (wire_byte_size_ > static_cast<int>(v8_flags.wasm_inlining_max_size)) {
    return false;
  }
  // For tiny functions, let's be a bit more generous.
  // TODO(dlehmann): Since we don't use up budget (i.e., increase
  // `inlined_wire_byte_count` see above) for very tiny functions, we might be
  // able to remove/simplify this code in the future.
  if (wire_byte_size_ < 12) {
    if (inlined_wire_byte_count > 100) {
      inlined_wire_byte_count -= 100;
    } else {
      inlined_wire_byte_count = 0;
    }
  }
  // For small-ish functions, the inlining budget is defined by the larger of
  // 1) the wasm_inlining_min_budget and
  // 2) the max_growth_factor * initial_wire_byte_size.
  // Inlining a little bit should always be fine even for tiny functions (1),
  // otherwise (2) makes sure that the budget scales in relation with the
  // original function size, to limit the compile time increase caused by
  // inlining.
  size_t budget_small_function =
      std::max<size_t>(v8_flags.wasm_inlining_min_budget,
                       data_->max_growth_factor * initial_wire_byte_size);

  // For large functions, growing by the same factor would add too much
  // compilation effort, so we also apply a fixed cap. However, independent
  // of the budget cap, for large functions we should still allow a little
  // inlining, which is why we allow 10% of the graph size is the minimal
  // budget even for large functions that exceed the regular budget.
  //
  // Note for future tuning: it might make sense to allow 20% here, and in
  // turn perhaps lower --wasm-inlining-budget. The drawback is that this
  // would allow truly huge functions to grow even bigger; the benefit is
  // that we wouldn't fall off as steep a cliff when hitting the cap.
  size_t budget_large_function =
      std::max<size_t>(data_->budget_cap, initial_wire_byte_size * 1.1);
  size_t total_size = initial_wire_byte_size + inlined_wire_byte_count +
                      static_cast<size_t>(wire_byte_size_);
  if (v8_flags.trace_wasm_inlining) {
    PrintF("budget=min(%zu, %zu), size %zu->%zu ", budget_small_function,
           budget_large_function,
           (initial_wire_byte_size + inlined_wire_byte_count), total_size);
  }
  return total_size <
         std::min<size_t>(budget_small_function, budget_large_function);
}

}  // namespace v8::internal::wasm

#endif  // V8_WASM_INLINING_TREE_H_
