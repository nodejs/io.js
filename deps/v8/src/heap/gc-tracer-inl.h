// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_GC_TRACER_INL_H_
#define V8_HEAP_GC_TRACER_INL_H_

#include "src/base/logging.h"
#include "src/base/platform/platform.h"
#include "src/execution/isolate.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"

namespace v8 {
namespace internal {

constexpr GCTracer::IncrementalInfos& GCTracer::IncrementalInfos::operator+=(
    base::TimeDelta delta) {
  steps++;
  duration += delta;
  if (delta > longest_step) {
    longest_step = delta;
  }
  return *this;
}

GCTracer::Scope::Scope(GCTracer* tracer, ScopeId scope, ThreadKind thread_kind)
    : tracer_(tracer),
      scope_(scope),
      thread_kind_(thread_kind),
      start_time_(base::TimeTicks::Now()) {
  DCHECK_IMPLIES(thread_kind_ == ThreadKind::kMain,
                 tracer_->heap_->IsMainThread());

#ifdef V8_RUNTIME_CALL_STATS
  if (V8_LIKELY(!TracingFlags::is_runtime_stats_enabled())) return;
  if (thread_kind_ == ThreadKind::kMain) {
    runtime_stats_ = tracer_->heap_->isolate_->counters()->runtime_call_stats();
    runtime_stats_->Enter(&timer_, GCTracer::RCSCounterFromScope(scope));
  } else {
    runtime_call_stats_scope_.emplace(
        tracer->worker_thread_runtime_call_stats());
    runtime_stats_ = runtime_call_stats_scope_->Get();
    runtime_stats_->Enter(&timer_, GCTracer::RCSCounterFromScope(scope));
  }
#endif  // defined(V8_RUNTIME_CALL_STATS)
}

GCTracer::Scope::~Scope() {
  const base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
  tracer_->AddScopeSample(scope_, duration);

  if (thread_kind_ == ThreadKind::kMain) {
    if (scope_ == ScopeId::MC_INCREMENTAL ||
        scope_ == ScopeId::MC_INCREMENTAL_START ||
        scope_ == ScopeId::MC_INCREMENTAL_FINALIZE) {
      auto* long_task_stats =
          tracer_->heap_->isolate_->GetCurrentLongTaskStats();
      long_task_stats->gc_full_incremental_wall_clock_duration_us +=
          duration.InMicroseconds();
    }
  }

#ifdef V8_RUNTIME_CALL_STATS
  if (V8_LIKELY(runtime_stats_ == nullptr)) return;
  runtime_stats_->Leave(&timer_);
#endif  // defined(V8_RUNTIME_CALL_STATS)
}

constexpr const char* GCTracer::Scope::Name(ScopeId id) {
#define CASE(scope)  \
  case Scope::scope: \
    return "V8.GC_" #scope;
  switch (id) {
    TRACER_SCOPES(CASE)
    TRACER_BACKGROUND_SCOPES(CASE)
    default:
      return nullptr;
  }
#undef CASE
}

constexpr bool GCTracer::Scope::NeedsYoungEpoch(ScopeId id) {
#define CASE(scope)  \
  case Scope::scope: \
    return true;
  switch (id) {
    TRACER_YOUNG_EPOCH_SCOPES(CASE)
    default:
      return false;
  }
#undef CASE
}

constexpr int GCTracer::Scope::IncrementalOffset(ScopeId id) {
  DCHECK_LE(FIRST_INCREMENTAL_SCOPE, id);
  DCHECK_GE(LAST_INCREMENTAL_SCOPE, id);
  return id - FIRST_INCREMENTAL_SCOPE;
}

constexpr bool GCTracer::Event::IsYoungGenerationEvent(Type type) {
  DCHECK_NE(Type::START, type);
  return type == Type::SCAVENGER || type == Type::MINOR_MARK_SWEEPER ||
         type == Type::INCREMENTAL_MINOR_MARK_SWEEPER;
}

CollectionEpoch GCTracer::CurrentEpoch(Scope::ScopeId id) const {
  return Scope::NeedsYoungEpoch(id) ? epoch_young_ : epoch_full_;
}

double GCTracer::current_scope(Scope::ScopeId id) const {
  DCHECK_GT(Scope::NUMBER_OF_SCOPES, id);
  return current_.scopes[id].InMillisecondsF();
}

constexpr const GCTracer::IncrementalInfos& GCTracer::incremental_scope(
    Scope::ScopeId id) const {
  return incremental_scopes_[Scope::IncrementalOffset(id)];
}

void GCTracer::AddScopeSample(Scope::ScopeId id, base::TimeDelta duration) {
  if (Scope::FIRST_INCREMENTAL_SCOPE <= id &&
      id <= Scope::LAST_INCREMENTAL_SCOPE) {
    incremental_scopes_[Scope::IncrementalOffset(id)] += duration;
  } else if (Scope::FIRST_BACKGROUND_SCOPE <= id &&
             id <= Scope::LAST_BACKGROUND_SCOPE) {
    base::MutexGuard guard(&background_scopes_mutex_);
    background_scopes_[id] += duration;
  } else {
    DCHECK_GT(Scope::NUMBER_OF_SCOPES, id);
    current_.scopes[id] += duration;
  }
}

#ifdef V8_RUNTIME_CALL_STATS
WorkerThreadRuntimeCallStats* GCTracer::worker_thread_runtime_call_stats() {
  return heap_->isolate_->counters()->worker_thread_runtime_call_stats();
}

RuntimeCallCounterId GCTracer::RCSCounterFromScope(Scope::ScopeId id) {
  static_assert(Scope::FIRST_SCOPE == Scope::MC_INCREMENTAL);
  return static_cast<RuntimeCallCounterId>(
      static_cast<int>(RuntimeCallCounterId::kGC_MC_INCREMENTAL) +
      static_cast<int>(id));
}
#endif  // defined(V8_RUNTIME_CALL_STATS)

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_GC_TRACER_INL_H_
