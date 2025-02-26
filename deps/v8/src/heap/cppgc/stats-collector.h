// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_STATS_COLLECTOR_H_
#define V8_HEAP_CPPGC_STATS_COLLECTOR_H_

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <atomic>
#include <vector>

#include "include/cppgc/platform.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/platform/time.h"
#include "src/heap/cppgc/garbage-collector.h"
#include "src/heap/cppgc/metric-recorder.h"
#include "src/heap/cppgc/trace-event.h"

namespace cppgc {
namespace internal {

// Histogram scopes contribute to histogram as well as to traces and metrics.
// Other scopes contribute only to traces and metrics.
#define CPPGC_FOR_ALL_HISTOGRAM_SCOPES(V) \
  V(AtomicMark)                           \
  V(AtomicWeak)                           \
  V(AtomicCompact)                        \
  V(AtomicSweep)                          \
  V(IncrementalMark)                      \
  V(IncrementalSweep)

#define CPPGC_FOR_ALL_SCOPES(V)             \
  V(Unmark)                                 \
  V(MarkIncrementalStart)                   \
  V(MarkIncrementalFinalize)                \
  V(MarkAtomicPrologue)                     \
  V(MarkAtomicEpilogue)                     \
  V(MarkTransitiveClosure)                  \
  V(MarkTransitiveClosureWithDeadline)      \
  V(MarkFlushEphemerons)                    \
  V(MarkOnAllocation)                       \
  V(MarkProcessBailOutObjects)              \
  V(MarkProcessMarkingWorklist)             \
  V(MarkProcessRetraceWorklist)             \
  V(MarkProcessWriteBarrierWorklist)        \
  V(MarkProcessNotFullyconstructedWorklist) \
  V(MarkProcessEphemerons)                  \
  V(MarkVisitRoots)                         \
  V(MarkVisitNotFullyConstructedObjects)    \
  V(MarkVisitPersistents)                   \
  V(MarkVisitCrossThreadPersistents)        \
  V(MarkVisitStack)                         \
  V(MarkVisitRememberedSets)                \
  V(WeakContainerCallbacksProcessing)       \
  V(CustomCallbacksProcessing)              \
  V(SweepEmptyPages)                        \
  V(SweepFinish)                            \
  V(SweepFinalizeEmptyPages)                \
  V(SweepFinalizeSweptPages)                \
  V(SweepFinishIfOutOfWork)                 \
  V(SweepInvokePreFinalizers)               \
  V(SweepInLowPriorityTask)                 \
  V(SweepInTask)                            \
  V(SweepInTaskForStatistics)               \
  V(SweepOnAllocation)                      \
  V(SweepPages)

#define CPPGC_FOR_ALL_HISTOGRAM_CONCURRENT_SCOPES(V) \
  V(ConcurrentMark)                                  \
  V(ConcurrentSweep)                                 \
  V(ConcurrentWeakCallback)

#define CPPGC_FOR_ALL_CONCURRENT_SCOPES(V)            \
  V(ConcurrentMarkProcessEphemeronWorklist)           \
  V(ConcurrentMarkProcessMarkingWorklist)             \
  V(ConcurrentMarkProcessNotFullyconstructedWorklist) \
  V(ConcurrentMarkProcessWriteBarrierWorklist)

// Sink for various time and memory statistics.
class V8_EXPORT_PRIVATE StatsCollector final {
  using IsForcedGC = GCConfig::IsForcedGC;

 public:
  using MarkingType = GCConfig::MarkingType;
  using SweepingType = GCConfig::SweepingType;

#if defined(CPPGC_DECLARE_ENUM)
  static_assert(false, "CPPGC_DECLARE_ENUM macro is already defined");
#endif

  enum ScopeId {
#define CPPGC_DECLARE_ENUM(name) k##name,
    CPPGC_FOR_ALL_HISTOGRAM_SCOPES(CPPGC_DECLARE_ENUM)
        kNumHistogramScopeIds,
    CPPGC_FOR_ALL_SCOPES(CPPGC_DECLARE_ENUM)
#undef CPPGC_DECLARE_ENUM
        kNumScopeIds,
  };

  enum ConcurrentScopeId {
#define CPPGC_DECLARE_ENUM(name) k##name,
    CPPGC_FOR_ALL_HISTOGRAM_CONCURRENT_SCOPES(CPPGC_DECLARE_ENUM)
        kNumHistogramConcurrentScopeIds,
    CPPGC_FOR_ALL_CONCURRENT_SCOPES(CPPGC_DECLARE_ENUM)
#undef CPPGC_DECLARE_ENUM
        kNumConcurrentScopeIds
  };

  // POD to hold interesting data accumulated during a garbage collection cycle.
  //
  // The event is always fully populated when looking at previous events but
  // may only be partially populated when looking at the current event.
  struct Event final {
    V8_EXPORT_PRIVATE explicit Event();

    v8::base::TimeDelta scope_data[kNumHistogramScopeIds];
    v8::base::AtomicWord concurrent_scope_data[kNumHistogramConcurrentScopeIds]{
        0};

    size_t epoch = -1;
    CollectionType collection_type = CollectionType::kMajor;
    MarkingType marking_type = MarkingType::kAtomic;
    SweepingType sweeping_type = SweepingType::kAtomic;
    IsForcedGC is_forced_gc = IsForcedGC::kNotForced;
    // Marked bytes collected during marking.
    size_t marked_bytes = 0;
    size_t object_size_before_sweep_bytes = -1;
    size_t memory_size_before_sweep_bytes = -1;
  };

 private:
#if defined(CPPGC_CASE)
  static_assert(false, "CPPGC_CASE macro is already defined");
#endif

  constexpr static const char* GetScopeName(ScopeId id, CollectionType type) {
    switch (id) {
#define CPPGC_CASE(name)                                   \
  case k##name:                                            \
    return type == CollectionType::kMajor ? "CppGC." #name \
                                          : "CppGC." #name ".Minor";
      CPPGC_FOR_ALL_HISTOGRAM_SCOPES(CPPGC_CASE)
      CPPGC_FOR_ALL_SCOPES(CPPGC_CASE)
#undef CPPGC_CASE
      default:
        return nullptr;
    }
  }

  constexpr static const char* GetScopeName(ConcurrentScopeId id,
                                            CollectionType type) {
    switch (id) {
#define CPPGC_CASE(name)                                   \
  case k##name:                                            \
    return type == CollectionType::kMajor ? "CppGC." #name \
                                          : "CppGC." #name ".Minor";
      CPPGC_FOR_ALL_HISTOGRAM_CONCURRENT_SCOPES(CPPGC_CASE)
      CPPGC_FOR_ALL_CONCURRENT_SCOPES(CPPGC_CASE)
#undef CPPGC_CASE
      default:
        return nullptr;
    }
  }

  enum TraceCategory { kEnabled, kDisabled };
  enum ScopeContext { kMutatorThread, kConcurrentThread };

  // Trace a particular scope. Will emit a trace event and record the time in
  // the corresponding StatsCollector.
  template <TraceCategory trace_category, ScopeContext scope_category>
  class V8_NODISCARD InternalScope {
    using ScopeIdType = std::conditional_t<scope_category == kMutatorThread,
                                           ScopeId, ConcurrentScopeId>;

   public:
    template <typename... Args>
    InternalScope(StatsCollector* stats_collector, ScopeIdType scope_id,
                  Args... args)
        : stats_collector_(stats_collector),
          start_time_(v8::base::TimeTicks::Now()),
          scope_id_(scope_id) {
      DCHECK_LE(0, scope_id_);
      DCHECK_LT(static_cast<int>(scope_id_),
                scope_category == kMutatorThread
                    ? static_cast<int>(kNumScopeIds)
                    : static_cast<int>(kNumConcurrentScopeIds));
      DCHECK_NE(static_cast<int>(scope_id_),
                scope_category == kMutatorThread
                    ? static_cast<int>(kNumHistogramScopeIds)
                    : static_cast<int>(kNumHistogramConcurrentScopeIds));
      StartTrace(args...);
    }

    ~InternalScope() {
      StopTrace();
      IncreaseScopeTime();
    }

    InternalScope(const InternalScope&) = delete;
    InternalScope& operator=(const InternalScope&) = delete;

    void DecreaseStartTimeForTesting(v8::base::TimeDelta delta) {
      start_time_ -= delta;
    }

   private:
    void* operator new(size_t, void*) = delete;
    void* operator new(size_t) = delete;

    inline constexpr static const char* TraceCategory();

    template <typename... Args>
    inline void StartTrace(Args... args);
    inline void StopTrace();

    inline void StartTraceImpl();
    template <typename Value1>
    inline void StartTraceImpl(const char* k1, Value1 v1);
    template <typename Value1, typename Value2>
    inline void StartTraceImpl(const char* k1, Value1 v1, const char* k2,
                               Value2 v2);
    inline void StopTraceImpl();

    inline void IncreaseScopeTime();

    StatsCollector* const stats_collector_;
    v8::base::TimeTicks start_time_;
    const ScopeIdType scope_id_;
  };

 public:
  using DisabledScope = InternalScope<kDisabled, kMutatorThread>;
  using EnabledScope = InternalScope<kEnabled, kMutatorThread>;
  using DisabledConcurrentScope = InternalScope<kDisabled, kConcurrentThread>;
  using EnabledConcurrentScope = InternalScope<kEnabled, kConcurrentThread>;

  // Observer for allocated object size. May e.g. be used to implement heap
  // growing heuristics. Observers may register/unregister observers at any
  // time when being invoked.
  class AllocationObserver {
   public:
    // Called after observing at least
    // StatsCollector::kAllocationThresholdBytes changed bytes through
    // allocation or explicit free. Reports both, negative and positive
    // increments, to allow observer to decide whether absolute values or only
    // the deltas is interesting.
    //
    // May trigger GC.
    virtual void AllocatedObjectSizeIncreased(size_t) {}
    virtual void AllocatedObjectSizeDecreased(size_t) {}

    // Called when the exact size of allocated object size is known. In
    // practice, this is after marking when marked bytes == allocated bytes.
    //
    // Must not trigger GC synchronously.
    virtual void ResetAllocatedObjectSize(size_t) {}

    // Called upon allocating/releasing chunks of memory (e.g. pages) that can
    // contain objects.
    //
    // Must not trigger GC.
    virtual void AllocatedSizeIncreased(size_t) {}
    virtual void AllocatedSizeDecreased(size_t) {}
  };

  // Observers are implemented using virtual calls. Avoid notifications below
  // reasonably interesting sizes.
  static constexpr size_t kAllocationThresholdBytes = 1024;

  explicit StatsCollector(Platform*);
  StatsCollector(const StatsCollector&) = delete;
  StatsCollector& operator=(const StatsCollector&) = delete;

  void RegisterObserver(AllocationObserver*);
  void UnregisterObserver(AllocationObserver*);

  void NotifyAllocation(size_t);
  void NotifyExplicitFree(size_t);
  // Safepoints should only be invoked when garbage collections are possible.
  // This is necessary as increments and decrements are reported as close to
  // their actual allocation/reclamation as possible.
  void NotifySafePointForConservativeCollection();

  void NotifySafePointForTesting();

  // Indicates a new garbage collection cycle. The phase is optional and is only
  // used for major GC when generational GC is enabled.
  void NotifyUnmarkingStarted(CollectionType);
  // Indicates a new minor garbage collection cycle or a major, if generational
  // GC is not enabled.
  void NotifyMarkingStarted(CollectionType, MarkingType, IsForcedGC);
  // Indicates that marking of the current garbage collection cycle is
  // completed.
  void NotifyMarkingCompleted(size_t marked_bytes);
  // Indicates the end of a garbage collection cycle. This means that sweeping
  // is finished at this point.
  void NotifySweepingCompleted(SweepingType);

  size_t allocated_memory_size() const;
  // Size of live objects in bytes  on the heap. Based on the most recent marked
  // bytes and the bytes allocated since last marking.
  size_t allocated_object_size() const;

  // Returns the overall marked bytes count, i.e. if young generation is
  // enabled, it returns the accumulated number. Should not be called during
  // marking.
  size_t marked_bytes() const;

  // Returns the marked bytes for the current cycle. Should only be called
  // within GC cycle.
  size_t marked_bytes_on_current_cycle() const;

  // Returns the overall duration of the most recent marking phase. Should not
  // be called during marking.
  v8::base::TimeDelta marking_time() const;

  double GetRecentAllocationSpeedInBytesPerMs() const;

  const Event& GetPreviousEventForTesting() const { return previous_; }

  void NotifyAllocatedMemory(int64_t);
  void NotifyFreedMemory(int64_t);

  void IncrementDiscardedMemory(size_t);
  void DecrementDiscardedMemory(size_t);
  void ResetDiscardedMemory();
  size_t discarded_memory_size() const;
  size_t resident_memory_size() const;

  void SetMetricRecorder(std::unique_ptr<MetricRecorder> histogram_recorder) {
    metric_recorder_ = std::move(histogram_recorder);
  }

  MetricRecorder* GetMetricRecorder() const { return metric_recorder_.get(); }

 private:
  enum class GarbageCollectionState : uint8_t {
    kNotRunning,
    kUnmarking,
    kMarking,
    kSweeping
  };

  void RecordHistogramSample(ScopeId, v8::base::TimeDelta);
  void RecordHistogramSample(ConcurrentScopeId, v8::base::TimeDelta) {}

  // Invokes |callback| for all registered observers.
  template <typename Callback>
  void ForAllAllocationObservers(Callback callback);

  void AllocatedObjectSizeSafepointImpl();

  // Allocated bytes since the end of marking. These bytes are reset after
  // marking as they are accounted in marked_bytes then. May be negative in case
  // an object was explicitly freed that was marked as live in the previous
  // cycle.
  int64_t allocated_bytes_since_end_of_marking_ = 0;
  v8::base::TimeTicks time_of_last_end_of_marking_ = v8::base::TimeTicks::Now();
  // Counters for allocation and free. The individual values are never negative
  // but their delta may be because of the same reason the overall
  // allocated_bytes_since_end_of_marking_ may be negative. Keep integer
  // arithmetic for simplicity.
  int64_t allocated_bytes_since_safepoint_ = 0;
  int64_t explicitly_freed_bytes_since_safepoint_ = 0;
#ifdef CPPGC_VERIFY_HEAP
  // Tracks live bytes for overflows.
  size_t tracked_live_bytes_ = 0;
#endif  // CPPGC_VERIFY_HEAP

  // The number of bytes marked so far. For young generation (with sticky bits)
  // keeps track of marked bytes across multiple GC cycles.
  size_t marked_bytes_so_far_ = 0;

  int64_t memory_allocated_bytes_ = 0;
  int64_t memory_freed_bytes_since_end_of_marking_ = 0;
  std::atomic<size_t> discarded_bytes_{0};

  // vector to allow fast iteration of observers. Register/Unregisters only
  // happens on startup/teardown.
  std::vector<AllocationObserver*> allocation_observers_;
  bool allocation_observer_deleted_ = false;

  GarbageCollectionState gc_state_ = GarbageCollectionState::kNotRunning;

  // The event being filled by the current GC cycle between NotifyMarkingStarted
  // and NotifySweepingFinished.
  Event current_;
  // The previous GC event which is populated at NotifySweepingFinished.
  Event previous_;

  std::unique_ptr<MetricRecorder> metric_recorder_;

  // |platform_| is used by the TRACE_EVENT_* macros.
  Platform* platform_;
};

template <typename Callback>
void StatsCollector::ForAllAllocationObservers(Callback callback) {
  // Iterate using indices to allow push_back() of new observers.
  for (size_t i = 0; i < allocation_observers_.size(); ++i) {
    auto* observer = allocation_observers_[i];
    if (observer) {
      callback(observer);
    }
  }
  if (allocation_observer_deleted_) {
    allocation_observers_.erase(
        std::remove(allocation_observers_.begin(), allocation_observers_.end(),
                    nullptr),
        allocation_observers_.end());
    allocation_observer_deleted_ = false;
  }
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
constexpr const char*
StatsCollector::InternalScope<trace_category, scope_category>::TraceCategory() {
  switch (trace_category) {
    case kEnabled:
      return "cppgc";
    case kDisabled:
      return TRACE_DISABLED_BY_DEFAULT("cppgc");
  }
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
template <typename... Args>
void StatsCollector::InternalScope<trace_category, scope_category>::StartTrace(
    Args... args) {
  // Top level scopes that contribute to histogram should always be enabled.
  DCHECK_IMPLIES(static_cast<int>(scope_id_) <
                     (scope_category == kMutatorThread
                          ? static_cast<int>(kNumHistogramScopeIds)
                          : static_cast<int>(kNumHistogramConcurrentScopeIds)),
                 trace_category == StatsCollector::TraceCategory::kEnabled);
  StartTraceImpl(args...);
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
void StatsCollector::InternalScope<trace_category,
                                   scope_category>::StopTrace() {
  StopTraceImpl();
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
void StatsCollector::InternalScope<trace_category,
                                   scope_category>::StartTraceImpl() {
  TRACE_EVENT_BEGIN0(
      TraceCategory(),
      GetScopeName(scope_id_, stats_collector_->current_.collection_type));
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
template <typename Value1>
void StatsCollector::InternalScope<
    trace_category, scope_category>::StartTraceImpl(const char* k1, Value1 v1) {
  TRACE_EVENT_BEGIN1(
      TraceCategory(),
      GetScopeName(scope_id_, stats_collector_->current_.collection_type), k1,
      v1);
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
template <typename Value1, typename Value2>
void StatsCollector::InternalScope<
    trace_category, scope_category>::StartTraceImpl(const char* k1, Value1 v1,
                                                    const char* k2, Value2 v2) {
  TRACE_EVENT_BEGIN2(
      TraceCategory(),
      GetScopeName(scope_id_, stats_collector_->current_.collection_type), k1,
      v1, k2, v2);
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
void StatsCollector::InternalScope<trace_category,
                                   scope_category>::StopTraceImpl() {
  TRACE_EVENT_END2(
      TraceCategory(),
      GetScopeName(scope_id_, stats_collector_->current_.collection_type),
      "epoch", stats_collector_->current_.epoch, "forced",
      stats_collector_->current_.is_forced_gc == IsForcedGC::kForced);
}

template <StatsCollector::TraceCategory trace_category,
          StatsCollector::ScopeContext scope_category>
void StatsCollector::InternalScope<trace_category,
                                   scope_category>::IncreaseScopeTime() {
  DCHECK_NE(GarbageCollectionState::kNotRunning, stats_collector_->gc_state_);
  // Only record top level scopes.
  if (static_cast<int>(scope_id_) >=
      (scope_category == kMutatorThread
           ? static_cast<int>(kNumHistogramScopeIds)
           : static_cast<int>(kNumHistogramConcurrentScopeIds)))
    return;
  v8::base::TimeDelta time = v8::base::TimeTicks::Now() - start_time_;
  if (scope_category == StatsCollector::ScopeContext::kMutatorThread) {
    stats_collector_->current_.scope_data[scope_id_] += time;
    if (stats_collector_->metric_recorder_)
      stats_collector_->RecordHistogramSample(scope_id_, time);
    return;
  }
  // scope_category == StatsCollector::ScopeContext::kConcurrentThread
  using AtomicWord = v8::base::AtomicWord;
  const int64_t us = time.InMicroseconds();
  v8::base::Relaxed_AtomicIncrement(
      &stats_collector_->current_.concurrent_scope_data[scope_id_],
      static_cast<AtomicWord>(us));
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_STATS_COLLECTOR_H_
