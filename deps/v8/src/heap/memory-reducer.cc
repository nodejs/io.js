// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/memory-reducer.h"

#include "src/flags/flags.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/heap-inl.h"
#include "src/heap/incremental-marking.h"
#include "src/init/v8.h"
#include "src/utils/utils.h"

namespace v8 {
namespace internal {

const int MemoryReducer::kLongDelayMs = 8000;
const int MemoryReducer::kShortDelayMs = 500;
const int MemoryReducer::kWatchdogDelayMs = 100000;
const double MemoryReducer::kCommittedMemoryFactor = 1.1;
const size_t MemoryReducer::kCommittedMemoryDelta = 10 * MB;

MemoryReducer::MemoryReducer(Heap* heap)
    : heap_(heap),
      taskrunner_(heap->GetForegroundTaskRunner()),
      state_(State::CreateUninitialized()),
      js_calls_counter_(0),
      js_calls_sample_time_ms_(0.0) {
  DCHECK(v8_flags.incremental_marking);
  DCHECK(v8_flags.memory_reducer);
}

MemoryReducer::TimerTask::TimerTask(MemoryReducer* memory_reducer)
    : CancelableTask(memory_reducer->heap()->isolate()),
      memory_reducer_(memory_reducer) {}


void MemoryReducer::TimerTask::RunInternal() {
  Heap* heap = memory_reducer_->heap();
  const double time_ms = heap->MonotonicallyIncreasingTimeInMs();
  heap->allocator()->new_space_allocator()->FreeLinearAllocationArea();
  heap->tracer()->SampleAllocation(base::TimeTicks::Now(),
                                   heap->NewSpaceAllocationCounter(),
                                   heap->OldGenerationAllocationCounter(),
                                   heap->EmbedderAllocationCounter());
  const bool low_allocation_rate = heap->HasLowAllocationRate();
  const bool optimize_for_memory = heap->ShouldOptimizeForMemoryUsage();
  if (v8_flags.trace_memory_reducer) {
    heap->isolate()->PrintWithTimestamp(
        "Memory reducer: %s, %s\n",
        low_allocation_rate ? "low alloc" : "high alloc",
        optimize_for_memory ? "background" : "foreground");
  }
  // The memory reducer will start incremental marking if
  // 1) mutator is likely idle: js call rate is low and allocation rate is low.
  // 2) mutator is in background: optimize for memory flag is set.
  const Event event{
      kTimer,
      time_ms,
      heap->CommittedOldGenerationMemory(),
      false,
      low_allocation_rate || optimize_for_memory,
      heap->incremental_marking()->IsStopped() &&
          heap->incremental_marking()->CanBeStarted(),
  };
  memory_reducer_->NotifyTimer(event);
}


void MemoryReducer::NotifyTimer(const Event& event) {
  if (state_.id() != kWait) return;
  DCHECK_EQ(kTimer, event.type);
  state_ = Step(state_, event);
  if (state_.id() == kRun) {
    DCHECK(heap()->incremental_marking()->IsStopped());
    DCHECK(v8_flags.incremental_marking);
    if (v8_flags.trace_memory_reducer) {
      heap()->isolate()->PrintWithTimestamp("Memory reducer: started GC #%d\n",
                                            state_.started_gcs());
    }
    heap()->StartIncrementalMarking(GCFlag::kReduceMemoryFootprint,
                                    GarbageCollectionReason::kMemoryReducer,
                                    kGCCallbackFlagCollectAllExternalMemory);
  } else if (state_.id() == kWait) {
    // Re-schedule the timer.
    ScheduleTimer(state_.next_gc_start_ms() - event.time_ms);
    if (v8_flags.trace_memory_reducer) {
      heap()->isolate()->PrintWithTimestamp(
          "Memory reducer: waiting for %.f ms\n",
          state_.next_gc_start_ms() - event.time_ms);
    }
  }
}

void MemoryReducer::NotifyMarkCompact(size_t committed_memory_before) {
  if (!v8_flags.incremental_marking) return;
  const size_t committed_memory = heap()->CommittedOldGenerationMemory();

  // Trigger one more GC if
  // - this GC decreased committed memory,
  // - there is high fragmentation,
  const MemoryReducer::Event event{
      MemoryReducer::kMarkCompact,
      heap()->MonotonicallyIncreasingTimeInMs(),
      committed_memory,
      (committed_memory_before > committed_memory + MB) ||
          heap()->HasHighFragmentation(),
      false,
      false};
  const State old_state = state_;
  state_ = Step(state_, event);
  if (old_state.id() != kWait && state_.id() == kWait) {
    // If we are transitioning to the WAIT state, start the timer.
    ScheduleTimer(state_.next_gc_start_ms() - event.time_ms);
  }
  if (old_state.id() == kRun && v8_flags.trace_memory_reducer) {
    heap()->isolate()->PrintWithTimestamp(
        "Memory reducer: finished GC #%d (%s)\n", old_state.started_gcs(),
        state_.id() == kWait ? "will do more" : "done");
  }
}

void MemoryReducer::NotifyPossibleGarbage() {
  if (!v8_flags.incremental_marking) return;
  const MemoryReducer::Event event{MemoryReducer::kPossibleGarbage,
                                   heap()->MonotonicallyIncreasingTimeInMs(),
                                   0,
                                   false,
                                   false,
                                   false};
  const Id old_action = state_.id();
  state_ = Step(state_, event);
  if (old_action != kWait && state_.id() == kWait) {
    // If we are transitioning to the WAIT state, start the timer.
    ScheduleTimer(state_.next_gc_start_ms() - event.time_ms);
  }
}

bool MemoryReducer::WatchdogGC(const State& state, const Event& event) {
  return state.last_gc_time_ms() != 0 &&
         event.time_ms > state.last_gc_time_ms() + kWatchdogDelayMs;
}


// For specification of this function see the comment for MemoryReducer class.
MemoryReducer::State MemoryReducer::Step(const State& state,
                                         const Event& event) {
  DCHECK(v8_flags.memory_reducer);
  DCHECK(v8_flags.incremental_marking);

  switch (state.id()) {
    case kUninit:
    case kDone:
      if (event.type == kTimer) {
        return state;
      } else if (event.type == kMarkCompact) {
        if (event.committed_memory <
            std::max(
                static_cast<size_t>(state.committed_memory_at_last_run() *
                                    kCommittedMemoryFactor),
                state.committed_memory_at_last_run() + kCommittedMemoryDelta)) {
          return state;
        } else {
          return State::CreateWait(0, event.time_ms + kLongDelayMs,
                                   event.time_ms);
        }
      } else {
        DCHECK_EQ(kPossibleGarbage, event.type);
        return State::CreateWait(
            0, event.time_ms + v8_flags.gc_memory_reducer_start_delay_ms,
            state.last_gc_time_ms());
      }
    case kWait:
      CHECK_LE(state.started_gcs(), MaxNumberOfGCs());
      switch (event.type) {
        case kPossibleGarbage:
          return state;
        case kTimer:
          if (state.started_gcs() >= MaxNumberOfGCs()) {
            return State::CreateDone(state.last_gc_time_ms(),
                                     event.committed_memory);
          } else if (event.can_start_incremental_gc &&
                     (event.should_start_incremental_gc ||
                      WatchdogGC(state, event))) {
            if (state.next_gc_start_ms() <= event.time_ms) {
              return State::CreateRun(state.started_gcs() + 1);
            } else {
              return state;
            }
          } else {
            return State::CreateWait(state.started_gcs(),
                                     event.time_ms + kLongDelayMs,
                                     state.last_gc_time_ms());
          }
        case kMarkCompact:
          return State::CreateWait(state.started_gcs(),
                                   event.time_ms + kLongDelayMs, event.time_ms);
      }
    case kRun:
      CHECK_LE(state.started_gcs(), MaxNumberOfGCs());
      if (event.type == kMarkCompact) {
        if (state.started_gcs() < MaxNumberOfGCs() &&
            (event.next_gc_likely_to_collect_more ||
             state.started_gcs() == 1)) {
          return State::CreateWait(state.started_gcs(),
                                   event.time_ms + kShortDelayMs,
                                   event.time_ms);
        } else {
          return State::CreateDone(event.time_ms, event.committed_memory);
        }
      } else {
        return state;
      }
  }
  UNREACHABLE();
}

void MemoryReducer::ScheduleTimer(double delay_ms) {
  DCHECK_LT(0, delay_ms);
  if (heap()->IsTearingDown()) return;
  // Leave some room for precision error in task scheduler.
  const double kSlackMs = 100;
  taskrunner_->PostDelayedTask(std::make_unique<MemoryReducer::TimerTask>(this),
                               (delay_ms + kSlackMs) / 1000.0);
}

void MemoryReducer::TearDown() { state_ = State::CreateUninitialized(); }

// static
int MemoryReducer::MaxNumberOfGCs() {
  DCHECK_GT(v8_flags.memory_reducer_gc_count, 0);
  return v8_flags.memory_reducer_gc_count;
}

}  // namespace internal
}  // namespace v8
