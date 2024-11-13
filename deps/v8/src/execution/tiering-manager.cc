// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/tiering-manager.h"

#include <optional>

#include "src/base/platform/platform.h"
#include "src/baseline/baseline.h"
#include "src/codegen/assembler.h"
#include "src/codegen/compilation-cache.h"
#include "src/codegen/compiler.h"
#include "src/codegen/pending-optimization-table.h"
#include "src/common/globals.h"
#include "src/diagnostics/code-tracer.h"
#include "src/execution/execution.h"
#include "src/execution/frames-inl.h"
#include "src/flags/flags.h"
#include "src/handles/global-handles.h"
#include "src/init/bootstrapper.h"
#include "src/interpreter/interpreter.h"
#include "src/objects/code-kind.h"
#include "src/objects/code.h"
#include "src/tracing/trace-event.h"

#ifdef V8_ENABLE_SPARKPLUG
#include "src/baseline/baseline-batch-compiler.h"
#endif  // V8_ENABLE_SPARKPLUG

namespace v8 {
namespace internal {

#define OPTIMIZATION_REASON_LIST(V)   \
  V(DoNotOptimize, "do not optimize") \
  V(HotAndStable, "hot and stable")

enum class OptimizationReason : uint8_t {
#define OPTIMIZATION_REASON_CONSTANTS(Constant, message) k##Constant,
  OPTIMIZATION_REASON_LIST(OPTIMIZATION_REASON_CONSTANTS)
#undef OPTIMIZATION_REASON_CONSTANTS
};

char const* OptimizationReasonToString(OptimizationReason reason) {
  static char const* reasons[] = {
#define OPTIMIZATION_REASON_TEXTS(Constant, message) message,
      OPTIMIZATION_REASON_LIST(OPTIMIZATION_REASON_TEXTS)
#undef OPTIMIZATION_REASON_TEXTS
  };
  size_t const index = static_cast<size_t>(reason);
  DCHECK_LT(index, arraysize(reasons));
  return reasons[index];
}

#undef OPTIMIZATION_REASON_LIST

std::ostream& operator<<(std::ostream& os, OptimizationReason reason) {
  return os << OptimizationReasonToString(reason);
}

class OptimizationDecision {
 public:
  static constexpr OptimizationDecision Maglev() {
    // TODO(v8:7700): Consider using another reason here.
    return {OptimizationReason::kHotAndStable, CodeKind::MAGLEV,
            ConcurrencyMode::kConcurrent};
  }
  static constexpr OptimizationDecision TurbofanHotAndStable() {
    return {OptimizationReason::kHotAndStable, CodeKind::TURBOFAN,
            ConcurrencyMode::kConcurrent};
  }
  static constexpr OptimizationDecision DoNotOptimize() {
    return {OptimizationReason::kDoNotOptimize,
            // These values don't matter but we have to pass something.
            CodeKind::TURBOFAN, ConcurrencyMode::kConcurrent};
  }

  constexpr bool should_optimize() const {
    return optimization_reason != OptimizationReason::kDoNotOptimize;
  }

  OptimizationReason optimization_reason;
  CodeKind code_kind;
  ConcurrencyMode concurrency_mode;

 private:
  OptimizationDecision() = default;
  constexpr OptimizationDecision(OptimizationReason optimization_reason,
                                 CodeKind code_kind,
                                 ConcurrencyMode concurrency_mode)
      : optimization_reason(optimization_reason),
        code_kind(code_kind),
        concurrency_mode(concurrency_mode) {}
};
// Since we pass by value:
static_assert(sizeof(OptimizationDecision) <= kInt32Size);

namespace {

void TraceInOptimizationQueue(Tagged<JSFunction> function,
                              CodeKind current_code_kind) {
  if (v8_flags.trace_opt_verbose) {
    PrintF("[not marking function %s (%s) for optimization: already queued]\n",
           function->DebugNameCStr().get(),
           CodeKindToString(current_code_kind));
  }
}

void TraceHeuristicOptimizationDisallowed(Tagged<JSFunction> function) {
  if (v8_flags.trace_opt_verbose) {
    PrintF(
        "[not marking function %s for optimization: marked with "
        "%%PrepareFunctionForOptimization for manual optimization]\n",
        function->DebugNameCStr().get());
  }
}

void TraceRecompile(Isolate* isolate, Tagged<JSFunction> function,
                    OptimizationDecision d) {
  if (v8_flags.trace_opt) {
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(), "[marking ");
    ShortPrint(function, scope.file());
    PrintF(scope.file(), " for optimization to %s, %s, reason: %s",
           CodeKindToString(d.code_kind), ToString(d.concurrency_mode),
           OptimizationReasonToString(d.optimization_reason));
    PrintF(scope.file(), "]\n");
  }
}

}  // namespace

void TraceManualRecompile(Tagged<JSFunction> function, CodeKind code_kind,
                          ConcurrencyMode concurrency_mode) {
  if (v8_flags.trace_opt) {
    PrintF("[manually marking ");
    ShortPrint(function);
    PrintF(" for optimization to %s, %s]\n", CodeKindToString(code_kind),
           ToString(concurrency_mode));
  }
}

void TieringManager::Optimize(Tagged<JSFunction> function,
                              OptimizationDecision d) {
  DCHECK(d.should_optimize());
  TraceRecompile(isolate_, function, d);
  function->MarkForOptimization(isolate_, d.code_kind, d.concurrency_mode);
}

void TieringManager::MarkForTurboFanOptimization(Tagged<JSFunction> function) {
  Optimize(function, OptimizationDecision::TurbofanHotAndStable());
}

namespace {

// Returns true when |function| should be enqueued for sparkplug compilation for
// the first time.
bool FirstTimeTierUpToSparkplug(Isolate* isolate, Tagged<JSFunction> function) {
  return !function->has_feedback_vector() ||
         // We request sparkplug even in the presence of a fbv, if we are
         // running ignition and haven't enqueued the function for sparkplug
         // batch compilation yet. This ensures we tier-up to sparkplug when the
         // feedback vector is allocated eagerly (e.g. for logging function
         // events; see JSFunction::InitializeFeedbackCell()).
         (function->ActiveTierIsIgnition(isolate) &&
          CanCompileWithBaseline(isolate, function->shared()) &&
          !function->shared()->sparkplug_compiled());
}

bool TieringOrTieredUpToOptimizedTier(Tagged<FeedbackVector> vector) {
  return !IsNone(vector->tiering_state()) || vector->maybe_has_maglev_code() ||
         vector->maybe_has_maglev_osr_code() ||
         vector->maybe_has_turbofan_code() ||
         vector->maybe_has_turbofan_osr_code();
}

bool TiersUpToMaglev(CodeKind code_kind) {
  // TODO(v8:7700): Flip the UNLIKELY when appropriate.
  return V8_UNLIKELY(maglev::IsMaglevEnabled()) &&
         CodeKindIsUnoptimizedJSFunction(code_kind);
}

bool TiersUpToMaglev(std::optional<CodeKind> code_kind) {
  return code_kind.has_value() && TiersUpToMaglev(code_kind.value());
}

int InterruptBudgetFor(std::optional<CodeKind> code_kind,
                       TieringState tiering_state,
                       CachedTieringDecision cached_tiering_decision,
                       int bytecode_length) {
  if (IsRequestTurbofan(tiering_state) ||
      (code_kind.has_value() && code_kind.value() == CodeKind::TURBOFAN)) {
    return v8_flags.invocation_count_for_osr * bytecode_length;
  }
  // TODO(olivf) In case we are currently executing below Maglev and have
  // CodeKind::MAGLEV waiting we should also OSR. But currently we cannot know
  // if this helper is called from Maglev code or below.
  if (maglev::IsMaglevOsrEnabled() && IsRequestMaglev(tiering_state)) {
    return v8_flags.invocation_count_for_maglev_osr * bytecode_length;
  }
  if (TiersUpToMaglev(code_kind) && tiering_state == TieringState::kNone) {
    if (v8_flags.profile_guided_optimization &&
        (cached_tiering_decision == CachedTieringDecision::kEarlyMaglev ||
         cached_tiering_decision == CachedTieringDecision::kEarlyTurbofan)) {
      return v8_flags.invocation_count_for_early_optimization * bytecode_length;
    }
    return v8_flags.invocation_count_for_maglev * bytecode_length;
  }
  return v8_flags.invocation_count_for_turbofan * bytecode_length;
}

}  // namespace

// static
int TieringManager::InterruptBudgetFor(
    Isolate* isolate, Tagged<JSFunction> function,
    std::optional<CodeKind> override_active_tier) {
  DCHECK(function->shared()->is_compiled());
  const int bytecode_length =
      function->shared()->GetBytecodeArray(isolate)->length();

  if (FirstTimeTierUpToSparkplug(isolate, function)) {
    return bytecode_length * v8_flags.invocation_count_for_feedback_allocation;
  }

  DCHECK(function->has_feedback_vector());
  if (bytecode_length > v8_flags.max_optimized_bytecode_size) {
    // Decrease times of interrupt budget underflow, the reason of not setting
    // to INT_MAX is the interrupt budget may overflow when doing add
    // operation for forward jump.
    return INT_MAX / 2;
  }
  return ::i::InterruptBudgetFor(
      override_active_tier ? override_active_tier
                           : function->GetActiveTier(isolate),
      function->tiering_state(), function->shared()->cached_tiering_decision(),
      bytecode_length);
}

namespace {

void TrySetOsrUrgency(Isolate* isolate, Tagged<JSFunction> function,
                      int osr_urgency) {
  Tagged<SharedFunctionInfo> shared = function->shared();
  if (V8_UNLIKELY(!v8_flags.use_osr)) return;
  if (V8_UNLIKELY(shared->optimization_disabled())) return;

  // We've passed all checks - bump the OSR urgency.

  Tagged<FeedbackVector> fv = function->feedback_vector();
  if (V8_UNLIKELY(v8_flags.trace_osr)) {
    CodeTracer::Scope scope(isolate->GetCodeTracer());
    PrintF(scope.file(),
           "[OSR - setting osr urgency. function: %s, old urgency: %d, new "
           "urgency: %d]\n",
           function->DebugNameCStr().get(), fv->osr_urgency(), osr_urgency);
  }

  DCHECK_GE(osr_urgency, fv->osr_urgency());  // Never lower urgency here.
  fv->set_osr_urgency(osr_urgency);
}

void TryIncrementOsrUrgency(Isolate* isolate, Tagged<JSFunction> function) {
  int old_urgency = function->feedback_vector()->osr_urgency();
  int new_urgency = std::min(old_urgency + 1, FeedbackVector::kMaxOsrUrgency);
  TrySetOsrUrgency(isolate, function, new_urgency);
}

void TryRequestOsrAtNextOpportunity(Isolate* isolate,
                                    Tagged<JSFunction> function) {
  TrySetOsrUrgency(isolate, function, FeedbackVector::kMaxOsrUrgency);
}

}  // namespace

void TieringManager::RequestOsrAtNextOpportunity(Tagged<JSFunction> function) {
  DisallowGarbageCollection no_gc;
  TryRequestOsrAtNextOpportunity(isolate_, function);
}

void TieringManager::MaybeOptimizeFrame(Tagged<JSFunction> function,
                                        CodeKind current_code_kind) {
  const TieringState tiering_state =
      function->feedback_vector()->tiering_state();
  const bool osr_in_progress =
      function->feedback_vector()->osr_tiering_in_progress();
  // Attenzione! Update this constant in case the condition below changes.
  static_assert(kTieringStateInProgressBlocksTierup);
  if (V8_UNLIKELY(IsInProgress(tiering_state)) ||
      V8_UNLIKELY(osr_in_progress)) {
    if (v8_flags.concurrent_recompilation_front_running &&
        IsRequestTurbofan(tiering_state)) {
      // TODO(olivf): In the case of Maglev we tried a queue with two
      // priorities, but it seems not actually beneficial. More
      // investigation is needed.
      isolate_->IncreaseConcurrentOptimizationPriority(CodeKind::TURBOFAN,
                                                       function->shared());
    }
    // Note: This effectively disables further tiering actions (e.g. OSR, or
    // tiering up into Maglev) for the function while it is being compiled.
    TraceInOptimizationQueue(function, current_code_kind);
    return;
  }

  if (V8_UNLIKELY(v8_flags.testing_d8_test_runner) &&
      ManualOptimizationTable::IsMarkedForManualOptimization(isolate_,
                                                             function)) {
    TraceHeuristicOptimizationDisallowed(function);
    return;
  }

  // TODO(v8:7700): Consider splitting this up for Maglev/Turbofan.
  if (V8_UNLIKELY(function->shared()->optimization_disabled())) return;

  if (V8_UNLIKELY(v8_flags.always_osr)) {
    TryRequestOsrAtNextOpportunity(isolate_, function);
    // Continue below and do a normal optimized compile as well.
  }

  const bool maglev_osr = maglev::IsMaglevOsrEnabled();
  // Baseline OSR uses a separate mechanism and must not be considered here,
  // therefore we limit to kOptimizedJSFunctionCodeKindsMask.
  if (IsRequestTurbofan(tiering_state) ||
      (maglev_osr && IsRequestMaglev(tiering_state)) ||
      (current_code_kind < CodeKind::TURBOFAN &&
       function->HasAvailableCodeKind(isolate_, CodeKind::TURBOFAN)) ||
      (maglev_osr && current_code_kind < CodeKind::MAGLEV &&
       function->HasAvailableCodeKind(isolate_, CodeKind::MAGLEV))) {
    if (V8_UNLIKELY(maglev_osr && current_code_kind == CodeKind::MAGLEV &&
                    (!v8_flags.osr_from_maglev ||
                     isolate_->EfficiencyModeEnabledForTiering() ||
                     isolate_->BatterySaverModeEnabled()))) {
      return;
    }

    // OSR kicks in only once we've previously decided to tier up, but we are
    // still in a lower-tier frame (this implies a long-running loop).
    TryIncrementOsrUrgency(isolate_, function);

    // Return unconditionally and don't run through the optimization decision
    // again; we've already decided to tier up previously.
    return;
  }

  DCHECK(!IsRequestTurbofan(tiering_state));
  DCHECK(!function->HasAvailableCodeKind(isolate_, CodeKind::TURBOFAN));
  OptimizationDecision d =
      ShouldOptimize(function->feedback_vector(), current_code_kind);
  // We might be stuck in a baseline frame that wants to tier up to Maglev, but
  // is in a loop, and can't OSR, because Maglev doesn't have OSR. Allow it to
  // skip over Maglev by re-checking ShouldOptimize as if we were in Maglev.
  if (!isolate_->EfficiencyModeEnabledForTiering() && !maglev_osr &&
      d.should_optimize() && d.code_kind == CodeKind::MAGLEV) {
    bool is_marked_for_maglev_optimization =
        IsRequestMaglev(tiering_state) ||
        function->HasAvailableCodeKind(isolate_, CodeKind::MAGLEV);
    if (is_marked_for_maglev_optimization) {
      d = ShouldOptimize(function->feedback_vector(), CodeKind::MAGLEV);
    }
  }

  if (isolate_->EfficiencyModeEnabledForTiering() &&
      d.code_kind != CodeKind::TURBOFAN) {
    d.concurrency_mode = ConcurrencyMode::kSynchronous;
  }

  if (d.should_optimize()) Optimize(function, d);
}

OptimizationDecision TieringManager::ShouldOptimize(
    Tagged<FeedbackVector> feedback_vector, CodeKind current_code_kind) {
  Tagged<SharedFunctionInfo> shared = feedback_vector->shared_function_info();
  if (current_code_kind == CodeKind::TURBOFAN) {
    return OptimizationDecision::DoNotOptimize();
  }

  if (TiersUpToMaglev(current_code_kind) &&
      shared->PassesFilter(v8_flags.maglev_filter) &&
      !shared->maglev_compilation_failed()) {
    if (v8_flags.profile_guided_optimization &&
        shared->cached_tiering_decision() ==
            CachedTieringDecision::kEarlyTurbofan) {
      return OptimizationDecision::TurbofanHotAndStable();
    }
    return OptimizationDecision::Maglev();
  }

  if (V8_UNLIKELY(!v8_flags.turbofan ||
                  !shared->PassesFilter(v8_flags.turbo_filter) ||
                  (v8_flags.efficiency_mode_disable_turbofan &&
                   isolate_->EfficiencyModeEnabledForTiering()) ||
                  isolate_->BatterySaverModeEnabled())) {
    return OptimizationDecision::DoNotOptimize();
  }

  if (isolate_->EfficiencyModeEnabledForTiering() &&
      v8_flags.efficiency_mode_delay_turbofan &&
      feedback_vector->invocation_count() <
          v8_flags.efficiency_mode_delay_turbofan) {
    return OptimizationDecision::DoNotOptimize();
  }

  Tagged<BytecodeArray> bytecode = shared->GetBytecodeArray(isolate_);
  if (bytecode->length() > v8_flags.max_optimized_bytecode_size) {
    return OptimizationDecision::DoNotOptimize();
  }

  return OptimizationDecision::TurbofanHotAndStable();
}

void TieringManager::NotifyICChanged(Tagged<FeedbackVector> vector) {
  CodeKind code_kind = vector->has_optimized_code()
                           ? vector->optimized_code(isolate_)->kind()
                       : vector->shared_function_info()->HasBaselineCode()
                           ? CodeKind::BASELINE
                           : CodeKind::INTERPRETED_FUNCTION;
  if (code_kind == CodeKind::INTERPRETED_FUNCTION &&
      CanCompileWithBaseline(isolate_, vector->shared_function_info()) &&
      !vector->shared_function_info()->sparkplug_compiled()) {
    // Don't delay tier-up if we haven't tiered up to baseline yet, but will --
    // baseline code is feedback independent.
    return;
  }

  OptimizationDecision decision = ShouldOptimize(vector, code_kind);
  if (decision.should_optimize()) {
    Tagged<SharedFunctionInfo> shared = vector->shared_function_info();
    int bytecode_length = shared->GetBytecodeArray(isolate_)->length();
    Tagged<FeedbackCell> cell = vector->parent_feedback_cell();
    int invocations = v8_flags.minimum_invocations_after_ic_update;
    int bytecodes = std::min(bytecode_length, (kMaxInt >> 1) / invocations);
    int new_budget = invocations * bytecodes;
    int current_budget = cell->interrupt_budget();
    if (v8_flags.profile_guided_optimization &&
        shared->cached_tiering_decision() == CachedTieringDecision::kPending) {
      if (TieringOrTieredUpToOptimizedTier(vector)) {
        shared->set_cached_tiering_decision(CachedTieringDecision::kNormal);
      } else {
        // Record how many invocation count were consumed before the last IC
        // change.
        int new_invocation_count_before_stable;
        if (vector->interrupt_budget_reset_by_ic_change()) {
          // Initial interrupt budget is
          // v8_flags.minimum_invocations_after_ic_update * bytecodes
          int new_consumed_budget = new_budget - current_budget;
          new_invocation_count_before_stable =
              vector->invocation_count_before_stable(kRelaxedLoad) +
              std::ceil(static_cast<float>(new_consumed_budget) / bytecodes);
        } else {
          // Initial interrupt budget is
          // v8_flags.invocation_count_for_{maglev|turbofan} * bytecodes
          int total_consumed_budget =
              (maglev::IsMaglevEnabled()
                   ? v8_flags.invocation_count_for_maglev
                   : v8_flags.invocation_count_for_turbofan) *
                  bytecodes -
              current_budget;
          new_invocation_count_before_stable =
              std::ceil(static_cast<float>(total_consumed_budget) / bytecodes);
        }
        DCHECK_LT(v8_flags.invocation_count_for_early_optimization,
                  FeedbackVector::kInvocationCountBeforeStableDeoptSentinel);
        if (new_invocation_count_before_stable >
            v8_flags.invocation_count_for_early_optimization) {
          shared->set_cached_tiering_decision(CachedTieringDecision::kNormal);
        } else {
          vector->set_invocation_count_before_stable(
              new_invocation_count_before_stable, kRelaxedStore);
        }
      }
    }
    if (!v8_flags.profile_guided_optimization ||
        shared->cached_tiering_decision() == CachedTieringDecision::kPending ||
        shared->cached_tiering_decision() == CachedTieringDecision::kNormal) {
      if (new_budget > current_budget) {
        if (v8_flags.trace_opt_verbose) {
          PrintF("[delaying optimization of %s, IC changed]\n",
                 shared->DebugNameCStr().get());
        }
        vector->set_interrupt_budget_reset_by_ic_change(true);
        cell->set_interrupt_budget(new_budget);
      }
    }
  }
}

TieringManager::OnInterruptTickScope::OnInterruptTickScope() {
  TRACE_EVENT0(TRACE_DISABLED_BY_DEFAULT("v8.compile"),
               "V8.MarkCandidatesForOptimization");
}

void TieringManager::OnInterruptTick(DirectHandle<JSFunction> function,
                                     CodeKind code_kind) {
  IsCompiledScope is_compiled_scope(
      function->shared()->is_compiled_scope(isolate_));

  // Remember whether the function had a vector at this point. This is relevant
  // later since the configuration 'Ignition without a vector' can be
  // considered a tier on its own. We begin tiering up to tiers higher than
  // Sparkplug only when reaching this point *with* a feedback vector.
  const bool had_feedback_vector = function->has_feedback_vector();
  const bool first_time_tiered_up_to_sparkplug =
      FirstTimeTierUpToSparkplug(isolate_, *function);
  // We don't want to trigger GC in the middle of OSR, so do not build a
  // baseline code for such case.
  const bool maybe_had_optimized_osr_code =
      had_feedback_vector &&
      function->feedback_vector()->maybe_has_optimized_osr_code();
  const bool compile_sparkplug =
      CanCompileWithBaseline(isolate_, function->shared()) &&
      function->ActiveTierIsIgnition(isolate_) && !maybe_had_optimized_osr_code;

  // Ensure that the feedback vector has been allocated.
  if (!had_feedback_vector) {
    if (compile_sparkplug) {
      // Mark the function as compiled with sparkplug before the feedback vector
      // is created to initialize the interrupt budget for the next tier.
      function->shared()->set_sparkplug_compiled(true);
    }
    JSFunction::CreateAndAttachFeedbackVector(isolate_, function,
                                              &is_compiled_scope);
    DCHECK(is_compiled_scope.is_compiled());
    // Also initialize the invocation count here. This is only really needed for
    // OSR. When we OSR functions with lazy feedback allocation we want to have
    // a non zero invocation count so we can inline functions.
    function->feedback_vector()->set_invocation_count(1, kRelaxedStore);
  }

  DCHECK(function->has_feedback_vector());
  DCHECK(function->shared()->is_compiled());
  DCHECK(function->shared()->HasBytecodeArray());

  // TODO(jgruber): Consider integrating this into a linear tiering system
  // controlled by TieringState in which the order is always
  // Ignition-Sparkplug-Turbofan, and only a single tierup is requested at
  // once.
  // It's unclear whether this is possible and/or makes sense - for example,
  // batching compilation can introduce arbitrary latency between the SP
  // compile request and fulfillment, which doesn't work with strictly linear
  // tiering.
  if (compile_sparkplug) {
#ifdef V8_ENABLE_SPARKPLUG
    if (v8_flags.baseline_batch_compilation) {
      isolate_->baseline_batch_compiler()->EnqueueFunction(function);
    } else {
      IsCompiledScope is_compiled_scope(
          function->shared()->is_compiled_scope(isolate_));
      Compiler::CompileBaseline(isolate_, function, Compiler::CLEAR_EXCEPTION,
                                &is_compiled_scope);
    }
#else
    UNREACHABLE();
#endif  // V8_ENABLE_SPARKPLUG
  }

  // We only tier up beyond sparkplug if we already had a feedback vector.
  if (first_time_tiered_up_to_sparkplug) {
    // If we didn't have a feedback vector, the interrupt budget has already
    // been set by JSFunction::CreateAndAttachFeedbackVector, so no need to
    // set it again.
    if (had_feedback_vector) {
      function->shared()->set_sparkplug_compiled(true);
      function->SetInterruptBudget(isolate_);
    }
    return;
  }

  // Don't tier up if Turbofan is disabled.
  // TODO(jgruber): Update this for a multi-tier world.
  if (V8_UNLIKELY(!isolate_->use_optimizer())) {
    function->SetInterruptBudget(isolate_);
    return;
  }

  // --- We've decided to proceed for now. ---

  DisallowGarbageCollection no_gc;
  OnInterruptTickScope scope;
  Tagged<JSFunction> function_obj = *function;

  MaybeOptimizeFrame(function_obj, code_kind);

  // Make sure to set the interrupt budget after maybe starting an optimization,
  // so that the interrupt budget size takes into account tiering state.
  DCHECK(had_feedback_vector);
  function->SetInterruptBudget(isolate_);
}

}  // namespace internal
}  // namespace v8
