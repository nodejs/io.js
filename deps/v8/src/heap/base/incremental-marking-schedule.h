// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_
#define V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_

#include <atomic>
#include <memory>
#include <optional>

#include "src/base/platform/time.h"

namespace heap::base {

// Incremental marking schedule that assumes a fixed time window for scheduling
// incremental marking steps.
//
// Usage:
// 1. NotifyIncrementalMarkingStart()
// 2. Any combination of:
//   -> UpdateMutatorThreadMarkedBytes(mutator_marked_bytes)
//   -> AddConcurrentlyMarkedBytes(concurrently_marked_bytes_delta)
//   -> MarkSynchronously(GetNextIncrementalStepDuration(estimated_live_size))
class V8_EXPORT_PRIVATE IncrementalMarkingSchedule final {
 public:
  struct StepInfo final {
    size_t mutator_marked_bytes = 0;
    size_t concurrent_marked_bytes = 0;
    size_t estimated_live_bytes = 0;
    size_t expected_marked_bytes = 0;
    v8::base::TimeDelta elapsed_time;

    size_t marked_bytes() const {
      return mutator_marked_bytes + concurrent_marked_bytes;
    }
    // Returns the schedule delta in bytes. Positive and negative delta values
    // indicate that the marked bytes are ahead and behind the expected
    // schedule, respectively.
    int64_t scheduled_delta_bytes() const {
      return static_cast<int64_t>(marked_bytes()) - expected_marked_bytes;
    }

    // Returns whether the schedule is behind the expectation.
    bool is_behind_expectation() const { return scheduled_delta_bytes() < 0; }
  };

  // Estimated walltime duration of incremental marking per GC cycle. This value
  // determines how the mutator thread will try to catch up on incremental
  // marking steps.
  static constexpr v8::base::TimeDelta kEstimatedMarkingTime =
      v8::base::TimeDelta::FromMilliseconds(500);

  // Minimum number of bytes that should be marked during an incremental
  // marking step.
  static constexpr size_t kDefaultMinimumMarkedBytesPerIncrementalStep =
      64 * 1024;

  // Step size used when no progress is being made. This step size should allow
  // for finalizing marking.
  static constexpr size_t kStepSizeWhenNotMakingProgress = 64 * 1024;

  static std::unique_ptr<IncrementalMarkingSchedule>
  CreateWithDefaultMinimumMarkedBytesPerStep(bool predictable_schedule = false);
  static std::unique_ptr<IncrementalMarkingSchedule>
  CreateWithZeroMinimumMarkedBytesPerStep(bool predictable_schedule = false);

  IncrementalMarkingSchedule(const IncrementalMarkingSchedule&) = delete;
  IncrementalMarkingSchedule& operator=(const IncrementalMarkingSchedule&) =
      delete;

  // Notifies the schedule that incremental marking has been started.
  void NotifyIncrementalMarkingStart();

  // Updates the mutator marked bytes. Must be called from the thread owning the
  // schedule. The schedule supports marked bytes being adjusted downwards,
  // i.e., going backwards in the schedule.
  void UpdateMutatorThreadMarkedBytes(size_t);

  // Adds concurrently marked bytes. May be called from any thread. Not required
  // to be complete, i.e., it is okay to not report bytes already marked for the
  // schedule.
  void AddConcurrentlyMarkedBytes(size_t);

  // Returns the reported overall marked bytes including those marked by the
  // mutator and concurrently.
  size_t GetOverallMarkedBytes() const;

  // Returns the reported concurrently marked bytes. Only as accurate as
  // `AddConcurrentlyMarkedBytes()` is.
  size_t GetConcurrentlyMarkedBytes() const;

  // Computes the next step duration based on reported marked bytes and the
  // current `estimated_live_bytes`.
  size_t GetNextIncrementalStepDuration(size_t estimated_live_bytes);

  // Returns the step info for the current step. This function is most useful
  // after calling `GetNextIncrementalStepDuration()` to report scheduling
  // details.
  const StepInfo GetCurrentStepInfo() const;

  // Returns whether locally cached ephemerons should be flushed and made
  // available globally. Will only return true once every
  // `kEphemeronPairsFlushingRatioIncrements` percent of overall marked bytes.
  bool ShouldFlushEphemeronPairs();

  // The minimum marked bytes per step. This is a lower bound for all the step
  // sizes returned from `GetNextIncrementalStepDuration()`.
  size_t min_marked_bytes_per_step() const {
    return min_marked_bytes_per_step_;
  }

  // Sets the elapsed time for testing purposes. Is reset after calling
  // `GetNextIncrementalStepDuration()`.
  void SetElapsedTimeForTesting(v8::base::TimeDelta);

 private:
  static constexpr double kEphemeronPairsFlushingRatioIncrements = 0.25;

  IncrementalMarkingSchedule(size_t minimum_marked_bytes_per_step,
                             bool predictable_schedule);

  v8::base::TimeDelta GetElapsedTime();

  v8::base::TimeTicks incremental_marking_start_time_;
  size_t mutator_thread_marked_bytes_ = 0;
  std::atomic_size_t concurrently_marked_bytes_{0};
  size_t last_estimated_live_bytes_ = 0;
  double ephemeron_pairs_flushing_ratio_target_ = 0.25;
  StepInfo current_step_;
  const size_t min_marked_bytes_per_step_;
  const bool predictable_schedule_ = false;
  std::optional<v8::base::TimeDelta> elapsed_time_override_;
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_INCREMENTAL_MARKING_SCHEDULE_H_
