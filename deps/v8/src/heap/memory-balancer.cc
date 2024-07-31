// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-balancer.h"

#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"

namespace v8 {
namespace internal {

MemoryBalancer::MemoryBalancer(Heap* heap, base::TimeTicks startup_time)
    : heap_(heap), last_measured_at_(startup_time) {}

void MemoryBalancer::RecomputeLimits(size_t embedder_allocation_limit,
                                     base::TimeTicks time) {
  embedder_allocation_limit_ = embedder_allocation_limit;
  last_measured_memory_ = live_memory_after_gc_ =
      heap_->OldGenerationSizeOfObjects();
  last_measured_at_ = time;
  RefreshLimit();
  PostHeartbeatTask();
}

void MemoryBalancer::RefreshLimit() {
  CHECK(major_allocation_rate_.has_value());
  CHECK(major_gc_speed_.has_value());
  const size_t computed_limit =
      live_memory_after_gc_ +
      sqrt(live_memory_after_gc_ * (major_allocation_rate_.value().rate()) /
           (major_gc_speed_.value().rate()) / v8_flags.memory_balancer_c_value);

  // 2 MB of extra space.
  // This allows the heap size to not decay to CurrentSizeOfObject()
  // and prevents GC from triggering if, after a long period of idleness,
  // a small allocation appears.
  constexpr size_t kMinHeapExtraSpace = 2 * MB;
  const size_t minimum_limit = live_memory_after_gc_ + kMinHeapExtraSpace;

  size_t new_limit = std::max<size_t>(minimum_limit, computed_limit);
  new_limit = std::min<size_t>(new_limit, heap_->max_old_generation_size());
  new_limit = std::max<size_t>(new_limit, heap_->min_old_generation_size());

  if (v8_flags.trace_memory_balancer) {
    heap_->isolate()->PrintWithTimestamp(
        "MemoryBalancer: allocation-rate=%.1lfKB/ms gc-speed=%.1lfKB/ms "
        "minium-limit=%.1lfM computed-limit=%.1lfM new-limit=%.1lfM\n",
        major_allocation_rate_.value().rate() / KB,
        major_gc_speed_.value().rate() / KB,
        static_cast<double>(minimum_limit) / MB,
        static_cast<double>(computed_limit) / MB,
        static_cast<double>(new_limit) / MB);
  }

  heap_->SetOldGenerationAndGlobalAllocationLimit(
      new_limit, new_limit + embedder_allocation_limit_);
}

void MemoryBalancer::UpdateGCSpeed(size_t major_gc_bytes,
                                   base::TimeDelta major_gc_duration) {
  if (!major_gc_speed_) {
    major_gc_speed_ = SmoothedBytesAndDuration{
        major_gc_bytes, major_gc_duration.InMillisecondsF()};
  } else {
    major_gc_speed_->Update(major_gc_bytes, major_gc_duration.InMillisecondsF(),
                            kMajorGCDecayRate);
  }
}

void MemoryBalancer::UpdateAllocationRate(
    size_t major_allocation_bytes, base::TimeDelta major_allocation_duration) {
  if (!major_allocation_rate_) {
    major_allocation_rate_ = SmoothedBytesAndDuration{
        major_allocation_bytes, major_allocation_duration.InMillisecondsF()};
  } else {
    major_allocation_rate_->Update(major_allocation_bytes,
                                   major_allocation_duration.InMillisecondsF(),
                                   kMajorAllocationDecayRate);
  }
}

void MemoryBalancer::HeartbeatUpdate() {
  heartbeat_task_started_ = false;
  auto time = base::TimeTicks::Now();
  auto memory = heap_->OldGenerationSizeOfObjects();

  const base::TimeDelta duration = time - last_measured_at_;
  const size_t allocated_bytes =
      memory > last_measured_memory_ ? memory - last_measured_memory_ : 0;
  UpdateAllocationRate(allocated_bytes, duration);

  last_measured_memory_ = memory;
  last_measured_at_ = time;
  RefreshLimit();
  PostHeartbeatTask();
}

void MemoryBalancer::PostHeartbeatTask() {
  if (heartbeat_task_started_) return;
  heartbeat_task_started_ = true;
  heap_->GetForegroundTaskRunner()->PostDelayedTask(
      std::make_unique<HeartbeatTask>(heap_->isolate(), this), 1);
}

HeartbeatTask::HeartbeatTask(Isolate* isolate, MemoryBalancer* mb)
    : CancelableTask(isolate), mb_(mb) {}

void HeartbeatTask::RunInternal() { mb_->HeartbeatUpdate(); }

}  // namespace internal
}  // namespace v8
