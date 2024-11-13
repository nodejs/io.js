// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/stats-collector.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "src/base/atomicops.h"
#include "src/base/logging.h"
#include "src/base/platform/time.h"
#include "src/heap/cppgc/metric-recorder.h"

namespace cppgc {
namespace internal {

// static
constexpr size_t StatsCollector::kAllocationThresholdBytes;

StatsCollector::StatsCollector(Platform* platform) : platform_(platform) {
  USE(platform_);
}

void StatsCollector::RegisterObserver(AllocationObserver* observer) {
  DCHECK_EQ(allocation_observers_.end(),
            std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer));
  allocation_observers_.push_back(observer);
}

void StatsCollector::UnregisterObserver(AllocationObserver* observer) {
  auto it = std::find(allocation_observers_.begin(),
                      allocation_observers_.end(), observer);
  DCHECK_NE(allocation_observers_.end(), it);
  *it = nullptr;
  allocation_observer_deleted_ = true;
}

void StatsCollector::NotifyAllocation(size_t bytes) {
  // The current GC may not have been started. This is ok as recording considers
  // the whole time range between garbage collections.
  allocated_bytes_since_safepoint_ += bytes;
#ifdef CPPGC_VERIFY_HEAP
  DCHECK_GE(tracked_live_bytes_ + bytes, tracked_live_bytes_);
  tracked_live_bytes_ += bytes;
#endif  // CPPGC_VERIFY_HEAP
}

void StatsCollector::NotifyExplicitFree(size_t bytes) {
  // See IncreaseAllocatedObjectSize for lifetime of the counter.
  explicitly_freed_bytes_since_safepoint_ += bytes;
#ifdef CPPGC_VERIFY_HEAP
  DCHECK_GE(tracked_live_bytes_, bytes);
  tracked_live_bytes_ -= bytes;
#endif  // CPPGC_VERIFY_HEAP
}

void StatsCollector::NotifySafePointForConservativeCollection() {
  if (std::abs(allocated_bytes_since_safepoint_ -
               explicitly_freed_bytes_since_safepoint_) >=
      static_cast<int64_t>(kAllocationThresholdBytes)) {
    AllocatedObjectSizeSafepointImpl();
  }
}

void StatsCollector::NotifySafePointForTesting() {
  AllocatedObjectSizeSafepointImpl();
}

void StatsCollector::AllocatedObjectSizeSafepointImpl() {
  allocated_bytes_since_end_of_marking_ +=
      static_cast<int64_t>(allocated_bytes_since_safepoint_) -
      static_cast<int64_t>(explicitly_freed_bytes_since_safepoint_);

  // Save the epoch to avoid clearing counters when a GC happened, see below.
  const auto saved_epoch = current_.epoch;

  // These observer methods may start or finalize GC. In case they trigger a
  // final GC pause, the delta counters are reset there and the following
  // observer calls are called with '0' updates.
  ForAllAllocationObservers([this](AllocationObserver* observer) {
    // Recompute delta here so that a GC finalization is able to clear the
    // delta for other observer calls.
    int64_t delta = allocated_bytes_since_safepoint_ -
                    explicitly_freed_bytes_since_safepoint_;
    if (delta < 0) {
      observer->AllocatedObjectSizeDecreased(static_cast<size_t>(-delta));
    } else {
      observer->AllocatedObjectSizeIncreased(static_cast<size_t>(delta));
    }
  });
  // Only clear the counters when no garbage collection happened. In case of a
  // garbage collection in the callbacks, the counters have been cleared by
  // `NotifyMarkingFinished()`. In addition, atomic sweeping may have already
  // allocated new memory which would be dropped from accounting in case
  // of clearing here.
  if (saved_epoch == current_.epoch) {
    allocated_bytes_since_safepoint_ = 0;
    explicitly_freed_bytes_since_safepoint_ = 0;
  }
}

StatsCollector::Event::Event() {
  static std::atomic<size_t> epoch_counter{0};
  epoch = epoch_counter.fetch_add(1);
}

void StatsCollector::NotifyUnmarkingStarted(CollectionType collection_type) {
  DCHECK_EQ(GarbageCollectionState::kNotRunning, gc_state_);
  DCHECK_EQ(CollectionType::kMajor, collection_type);
  gc_state_ = GarbageCollectionState::kUnmarking;
}

void StatsCollector::NotifyMarkingStarted(CollectionType collection_type,
                                          MarkingType marking_type,
                                          IsForcedGC is_forced_gc) {
  DCHECK_IMPLIES(gc_state_ != GarbageCollectionState::kNotRunning,
                 (gc_state_ == GarbageCollectionState::kUnmarking &&
                  collection_type == CollectionType::kMajor));
  current_.collection_type = collection_type;
  current_.is_forced_gc = is_forced_gc;
  current_.marking_type = marking_type;
  gc_state_ = GarbageCollectionState::kMarking;
}

void StatsCollector::NotifyMarkingCompleted(size_t marked_bytes) {
  DCHECK_EQ(GarbageCollectionState::kMarking, gc_state_);
  gc_state_ = GarbageCollectionState::kSweeping;
  current_.marked_bytes = marked_bytes;
  current_.object_size_before_sweep_bytes =
      marked_bytes_so_far_ + allocated_bytes_since_end_of_marking_ +
      allocated_bytes_since_safepoint_ -
      explicitly_freed_bytes_since_safepoint_;
  allocated_bytes_since_safepoint_ = 0;
  explicitly_freed_bytes_since_safepoint_ = 0;

  if (current_.collection_type == CollectionType::kMajor)
    marked_bytes_so_far_ = 0;
  marked_bytes_so_far_ += marked_bytes;

#ifdef CPPGC_VERIFY_HEAP
  tracked_live_bytes_ = marked_bytes_so_far_;
#endif  // CPPGC_VERIFY_HEAP

  DCHECK_LE(memory_freed_bytes_since_end_of_marking_, memory_allocated_bytes_);
  memory_allocated_bytes_ -= memory_freed_bytes_since_end_of_marking_;
  current_.memory_size_before_sweep_bytes = memory_allocated_bytes_;
  memory_freed_bytes_since_end_of_marking_ = 0;

  ForAllAllocationObservers([this](AllocationObserver* observer) {
    observer->ResetAllocatedObjectSize(marked_bytes_so_far_);
  });

  // HeapGrowing would use the below fields to estimate allocation rate during
  // execution of ResetAllocatedObjectSize.
  allocated_bytes_since_end_of_marking_ = 0;
  time_of_last_end_of_marking_ = v8::base::TimeTicks::Now();
}

double StatsCollector::GetRecentAllocationSpeedInBytesPerMs() const {
  v8::base::TimeTicks current_time = v8::base::TimeTicks::Now();
  DCHECK_LE(time_of_last_end_of_marking_, current_time);
  if (time_of_last_end_of_marking_ == current_time) return 0;
  return allocated_bytes_since_end_of_marking_ /
         (current_time - time_of_last_end_of_marking_).InMillisecondsF();
}

namespace {

int64_t SumPhases(const MetricRecorder::GCCycle::Phases& phases) {
  DCHECK_LE(0, phases.mark_duration_us);
  DCHECK_LE(0, phases.weak_duration_us);
  DCHECK_LE(0, phases.compact_duration_us);
  DCHECK_LE(0, phases.sweep_duration_us);
  return phases.mark_duration_us + phases.weak_duration_us +
         phases.compact_duration_us + phases.sweep_duration_us;
}

MetricRecorder::GCCycle GetCycleEventForMetricRecorder(
    CollectionType type, StatsCollector::MarkingType marking_type,
    StatsCollector::SweepingType sweeping_type, int64_t atomic_mark_us,
    int64_t atomic_weak_us, int64_t atomic_compact_us, int64_t atomic_sweep_us,
    int64_t incremental_mark_us, int64_t incremental_sweep_us,
    int64_t concurrent_mark_us, int64_t concurrent_sweep_us,
    int64_t objects_before_bytes, int64_t objects_after_bytes,
    int64_t objects_freed_bytes, int64_t memory_before_bytes,
    int64_t memory_after_bytes, int64_t memory_freed_bytes) {
  MetricRecorder::GCCycle event;
  event.type = (type == CollectionType::kMajor)
                   ? MetricRecorder::GCCycle::Type::kMajor
                   : MetricRecorder::GCCycle::Type::kMinor;
  // MainThread.Incremental:
  event.main_thread_incremental.mark_duration_us =
      marking_type != StatsCollector::MarkingType::kAtomic ? incremental_mark_us
                                                           : -1;
  event.main_thread_incremental.sweep_duration_us =
      sweeping_type != StatsCollector::SweepingType::kAtomic
          ? incremental_sweep_us
          : -1;
  // MainThread.Atomic:
  event.main_thread_atomic.mark_duration_us = atomic_mark_us;
  event.main_thread_atomic.weak_duration_us = atomic_weak_us;
  event.main_thread_atomic.compact_duration_us = atomic_compact_us;
  event.main_thread_atomic.sweep_duration_us = atomic_sweep_us;
  // MainThread:
  event.main_thread.mark_duration_us =
      event.main_thread_atomic.mark_duration_us + incremental_mark_us;
  event.main_thread.weak_duration_us =
      event.main_thread_atomic.weak_duration_us;
  event.main_thread.compact_duration_us =
      event.main_thread_atomic.compact_duration_us;
  event.main_thread.sweep_duration_us =
      event.main_thread_atomic.sweep_duration_us + incremental_sweep_us;
  // Total:
  event.total.mark_duration_us =
      event.main_thread.mark_duration_us + concurrent_mark_us;
  event.total.weak_duration_us = event.main_thread.weak_duration_us;
  event.total.compact_duration_us = event.main_thread.compact_duration_us;
  event.total.sweep_duration_us =
      event.main_thread.sweep_duration_us + concurrent_sweep_us;
  // Objects:
  event.objects.before_bytes = objects_before_bytes;
  event.objects.after_bytes = objects_after_bytes;
  event.objects.freed_bytes = objects_freed_bytes;
  // Memory:
  event.memory.before_bytes = memory_before_bytes;
  event.memory.after_bytes = memory_after_bytes;
  event.memory.freed_bytes = memory_freed_bytes;
  // Collection Rate:
  if (event.objects.before_bytes == 0) {
    event.collection_rate_in_percent = 0;
  } else {
    event.collection_rate_in_percent =
        static_cast<double>(event.objects.freed_bytes) /
        event.objects.before_bytes;
  }
  // Efficiency:
  if (event.objects.freed_bytes == 0) {
    event.efficiency_in_bytes_per_us = 0;
    event.main_thread_efficiency_in_bytes_per_us = 0;
  } else {
    // Here, SumPhases(event.main_thread) or even SumPhases(event.total) can be
    // zero if the clock resolution is not small enough and the entire GC was
    // very short, so the timed value was zero. This appears to happen on
    // Windows, see crbug.com/1338256 and crbug.com/1339180. In this case, we
    // are only here if the number of freed bytes is nonzero and the division
    // below produces an infinite value.
    event.efficiency_in_bytes_per_us =
        static_cast<double>(event.objects.freed_bytes) / SumPhases(event.total);
    event.main_thread_efficiency_in_bytes_per_us =
        static_cast<double>(event.objects.freed_bytes) /
        SumPhases(event.main_thread);
  }
  return event;
}

}  // namespace

void StatsCollector::NotifySweepingCompleted(SweepingType sweeping_type) {
  DCHECK_EQ(GarbageCollectionState::kSweeping, gc_state_);
  gc_state_ = GarbageCollectionState::kNotRunning;
  current_.sweeping_type = sweeping_type;
  previous_ = std::move(current_);
  current_ = Event();
  DCHECK_IMPLIES(previous_.marking_type == StatsCollector::MarkingType::kAtomic,
                 previous_.scope_data[kIncrementalMark].IsZero());
  DCHECK_IMPLIES(
      previous_.sweeping_type == StatsCollector::SweepingType::kAtomic,
      previous_.scope_data[kIncrementalSweep].IsZero());
  if (metric_recorder_) {
    MetricRecorder::GCCycle event = GetCycleEventForMetricRecorder(
        previous_.collection_type, previous_.marking_type,
        previous_.sweeping_type,
        previous_.scope_data[kAtomicMark].InMicroseconds(),
        previous_.scope_data[kAtomicWeak].InMicroseconds(),
        previous_.scope_data[kAtomicCompact].InMicroseconds(),
        previous_.scope_data[kAtomicSweep].InMicroseconds(),
        previous_.scope_data[kIncrementalMark].InMicroseconds(),
        previous_.scope_data[kIncrementalSweep].InMicroseconds(),
        previous_.concurrent_scope_data[kConcurrentMark],
        previous_.concurrent_scope_data[kConcurrentSweep],
        previous_.object_size_before_sweep_bytes /* objects_before */,
        marked_bytes_so_far_ /* objects_after */,
        previous_.object_size_before_sweep_bytes -
            marked_bytes_so_far_ /* objects_freed */,
        previous_.memory_size_before_sweep_bytes /* memory_before */,
        previous_.memory_size_before_sweep_bytes -
            memory_freed_bytes_since_end_of_marking_ /* memory_after */,
        memory_freed_bytes_since_end_of_marking_ /* memory_freed */);
    metric_recorder_->AddMainThreadEvent(event);
  }
}

size_t StatsCollector::allocated_memory_size() const {
  return memory_allocated_bytes_ - memory_freed_bytes_since_end_of_marking_;
}

size_t StatsCollector::allocated_object_size() const {
  return marked_bytes_so_far_ + allocated_bytes_since_end_of_marking_;
}

size_t StatsCollector::marked_bytes() const {
  DCHECK_NE(GarbageCollectionState::kMarking, gc_state_);
  return marked_bytes_so_far_;
}

size_t StatsCollector::marked_bytes_on_current_cycle() const {
  DCHECK_NE(GarbageCollectionState::kNotRunning, gc_state_);
  return current_.marked_bytes;
}

v8::base::TimeDelta StatsCollector::marking_time() const {
  DCHECK_NE(GarbageCollectionState::kMarking, gc_state_);
  // During sweeping we refer to the current Event as that already holds the
  // correct marking information. In all other phases, the previous event holds
  // the most up-to-date marking information.
  const Event& event =
      gc_state_ == GarbageCollectionState::kSweeping ? current_ : previous_;
  return event.scope_data[kAtomicMark] + event.scope_data[kIncrementalMark] +
         v8::base::TimeDelta::FromMicroseconds(v8::base::Relaxed_Load(
             &event.concurrent_scope_data[kConcurrentMark]));
}

void StatsCollector::NotifyAllocatedMemory(int64_t size) {
  memory_allocated_bytes_ += size;
#ifdef DEBUG
  const auto saved_epoch = current_.epoch;
#endif  // DEBUG
  ForAllAllocationObservers([size](AllocationObserver* observer) {
    observer->AllocatedSizeIncreased(static_cast<size_t>(size));
  });
#ifdef DEBUG
  // AllocatedSizeIncreased() must not trigger GC.
  DCHECK_EQ(saved_epoch, current_.epoch);
#endif  // DEBUG
}

void StatsCollector::NotifyFreedMemory(int64_t size) {
  memory_freed_bytes_since_end_of_marking_ += size;
#ifdef DEBUG
  const auto saved_epoch = current_.epoch;
#endif  // DEBUG
  ForAllAllocationObservers([size](AllocationObserver* observer) {
    observer->AllocatedSizeDecreased(static_cast<size_t>(size));
  });
#ifdef DEBUG
  // AllocatedSizeDecreased() must not trigger GC.
  DCHECK_EQ(saved_epoch, current_.epoch);
#endif  // DEBUG
}

void StatsCollector::IncrementDiscardedMemory(size_t value) {
  const size_t old =
      discarded_bytes_.fetch_add(value, std::memory_order_relaxed);
  DCHECK_GE(old + value, old);
  USE(old);
}

void StatsCollector::DecrementDiscardedMemory(size_t value) {
  const size_t old =
      discarded_bytes_.fetch_sub(value, std::memory_order_relaxed);
  DCHECK_GE(old, old - value);
  USE(old);
}

void StatsCollector::ResetDiscardedMemory() {
  discarded_bytes_.store(0, std::memory_order_relaxed);
}

size_t StatsCollector::discarded_memory_size() const {
  return discarded_bytes_.load(std::memory_order_relaxed);
}

size_t StatsCollector::resident_memory_size() const {
  const auto allocated = allocated_memory_size();
  const auto discarded = discarded_memory_size();
  DCHECK_IMPLIES(allocated == 0, discarded == 0);
  DCHECK_IMPLIES(allocated > 0, allocated > discarded);
  return allocated - discarded;
}

void StatsCollector::RecordHistogramSample(ScopeId scope_id_,
                                           v8::base::TimeDelta time) {
  switch (scope_id_) {
    case kIncrementalMark: {
      MetricRecorder::MainThreadIncrementalMark event{time.InMicroseconds()};
      metric_recorder_->AddMainThreadEvent(event);
      break;
    }
    case kIncrementalSweep: {
      MetricRecorder::MainThreadIncrementalSweep event{time.InMicroseconds()};
      metric_recorder_->AddMainThreadEvent(event);
      break;
    }
    default:
      break;
  }
}

}  // namespace internal
}  // namespace cppgc
