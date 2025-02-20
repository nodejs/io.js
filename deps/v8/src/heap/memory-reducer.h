// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MEMORY_REDUCER_H_
#define V8_HEAP_MEMORY_REDUCER_H_

#include "include/v8-platform.h"
#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

namespace heap {
class HeapTester;
}  // namespace heap

class Heap;


// The goal of the MemoryReducer class is to detect transition of the mutator
// from high allocation phase to low allocation phase and to collect potential
// garbage created in the high allocation phase.
//
// The class implements an automaton with the following states and transitions.
//
// States:
// - DONE <last_gc_time_ms>
// - WAIT <started_gcs> <next_gc_start_ms> <last_gc_time_ms>
// - RUN <started_gcs> <last_gc_time_ms>
// The <started_gcs> is an integer in range from 0..kMaxNumberOfGCs that stores
// the number of GCs initiated by the MemoryReducer since it left the DONE
// state.
// The <next_gc_start_ms> is a double that stores the earliest time the next GC
// can be initiated by the MemoryReducer.
// The <last_gc_start_ms> is a double that stores the time of the last full GC.
// The DONE state means that the MemoryReducer is not active.
// The WAIT state means that the MemoryReducer is waiting for mutator allocation
// rate to drop. The check for the allocation rate happens in the timer task
// callback. If the allocation rate does not drop in watchdog_delay_ms since
// the last GC then transition to the RUN state is forced.
// The RUN state means that the MemoryReducer started incremental marking and is
// waiting for it to finish. Incremental marking steps are performed as usual
// in the idle notification and in the mutator.
//
// Transitions:
// DONE t -> WAIT 0 (now_ms + long_delay_ms) t' happens:
//     - on context disposal.
//     - at the end of mark-compact GC initiated by the mutator.
// This signals that there is potential garbage to be collected.
//
// WAIT n x t -> WAIT n (now_ms + long_delay_ms) t' happens:
//     - on mark-compact GC initiated by the mutator,
//     - in the timer callback if the mutator allocation rate is high or
//       incremental GC is in progress or (now_ms - t < watchdog_delay_ms)
//
// WAIT n x t -> WAIT (n+1) t happens:
//     - on background idle notification, which signals that we can start
//       incremental marking even if the allocation rate is high.
// The MemoryReducer starts incremental marking on this transition but still
// has a pending timer task.
//
// WAIT n x t -> DONE t happens:
//     - in the timer callback if n >= kMaxNumberOfGCs.
//
// WAIT n x t -> RUN (n+1) t happens:
//     - in the timer callback if the mutator allocation rate is low
//       and now_ms >= x and there is no incremental GC in progress.
//     - in the timer callback if (now_ms - t > watchdog_delay_ms) and
//       and now_ms >= x and there is no incremental GC in progress.
// The MemoryReducer starts incremental marking on this transition.
//
// RUN n t -> DONE now_ms happens:
//     - at end of the incremental GC initiated by the MemoryReducer if
//       (n > 1 and there is no more garbage to be collected) or
//       n == kMaxNumberOfGCs.
// RUN n t -> WAIT n (now_ms + short_delay_ms) now_ms happens:
//     - at end of the incremental GC initiated by the MemoryReducer if
//       (n == 1 or there is more garbage to be collected) and
//       n < kMaxNumberOfGCs.
//
// now_ms is the current time,
// t' is t if the current event is not a GC event and is now_ms otherwise,
// long_delay_ms, short_delay_ms, and watchdog_delay_ms are constants.
class V8_EXPORT_PRIVATE MemoryReducer {
 public:
  enum Id { kUninit, kDone, kWait, kRun };

  class State {
   public:
    static State CreateUninitialized() { return {kUninit, 0, 0, 0, 0}; }

    static State CreateDone(double last_gc_time_ms, size_t committed_memory) {
      return {kDone, 0, 0, last_gc_time_ms, committed_memory};
    }

    static State CreateWait(int started_gcs, double next_gc_time_ms,
                            double last_gc_time_ms) {
      return {kWait, started_gcs, next_gc_time_ms, last_gc_time_ms, 0};
    }

    static State CreateRun(int started_gcs) {
      return {kRun, started_gcs, 0, 0, 0};
    }

    Id id() const { return id_; }

    int started_gcs() const {
      DCHECK(id() == kWait || id() == kRun);
      return started_gcs_;
    }

    double next_gc_start_ms() const {
      DCHECK_EQ(id(), kWait);
      return next_gc_start_ms_;
    }

    double last_gc_time_ms() const {
      DCHECK(id() == kWait || id() == kDone || id() == kUninit);
      return last_gc_time_ms_;
    }

    size_t committed_memory_at_last_run() const {
      DCHECK(id() == kUninit || id() == kDone);
      return committed_memory_at_last_run_;
    }

   private:
    State(Id action, int started_gcs, double next_gc_start_ms,
          double last_gc_time_ms, size_t committed_memory_at_last_run)
        : id_(action),
          started_gcs_(started_gcs),
          next_gc_start_ms_(next_gc_start_ms),
          last_gc_time_ms_(last_gc_time_ms),
          committed_memory_at_last_run_(committed_memory_at_last_run) {}

    Id id_;
    int started_gcs_;
    double next_gc_start_ms_;
    double last_gc_time_ms_;
    size_t committed_memory_at_last_run_;
  };

  enum EventType { kTimer, kMarkCompact, kPossibleGarbage };

  struct Event {
    EventType type;
    double time_ms;
    size_t committed_memory;
    bool next_gc_likely_to_collect_more;
    bool should_start_incremental_gc;
    bool can_start_incremental_gc;
    bool is_frozen;
  };

  explicit MemoryReducer(Heap* heap);
  MemoryReducer(const MemoryReducer&) = delete;
  MemoryReducer& operator=(const MemoryReducer&) = delete;
  // Callbacks.
  void NotifyMarkCompact(size_t committed_memory_before);
  void NotifyPossibleGarbage();
  // The step function that computes the next state from the current state and
  // the incoming event.
  static State Step(const State& state, const Event& event);
  // Posts a timer task that will call NotifyTimer after the given delay.
  void ScheduleTimer(double delay_ms);
  void TearDown();
  static const int kLongDelayMs;
  static const int kShortDelayMs;
  static const int kWatchdogDelayMs;
  // The committed memory has to increase by at least this factor since the
  // last run in order to trigger a new run after mark-compact.
  static const double kCommittedMemoryFactor;
  // The committed memory has to increase by at least this amount since the
  // last run in order to trigger a new run after mark-compact.
  static const size_t kCommittedMemoryDelta;

  Heap* heap() { return heap_; }

  bool ShouldGrowHeapSlowly() { return state_.id() == kDone; }

  static int MaxNumberOfGCs();

  static bool IsFrozen(const Heap* heap);

 private:
  class TimerTask : public v8::internal::CancelableTask {
   public:
    explicit TimerTask(MemoryReducer* memory_reducer);
    TimerTask(const TimerTask&) = delete;
    TimerTask& operator=(const TimerTask&) = delete;

   private:
    // v8::internal::CancelableTask overrides.
    void RunInternal() override;
    MemoryReducer* memory_reducer_;
  };

  void NotifyTimer(const Event& event);

  static bool WatchdogGC(const State& state, const Event& event);

  Heap* heap_;
  std::shared_ptr<v8::TaskRunner> taskrunner_;
  State state_;
  unsigned int js_calls_counter_;
  double js_calls_sample_time_ms_;
  int start_delay_ms_ = false;

  // Used in cctest.
  friend class heap::HeapTester;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MEMORY_REDUCER_H_
