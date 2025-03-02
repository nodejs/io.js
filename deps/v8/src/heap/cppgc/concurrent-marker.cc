// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/concurrent-marker.h"

#include "include/cppgc/platform.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/marking-state.h"
#include "src/heap/cppgc/marking-visitor.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

namespace {

static constexpr double kMarkingScheduleRatioBeforeConcurrentPriorityIncrease =
    0.5;

static constexpr size_t kDefaultDeadlineCheckInterval = 750u;

template <StatsCollector::ConcurrentScopeId scope_id,
          size_t kDeadlineCheckInterval = kDefaultDeadlineCheckInterval,
          typename WorklistLocal, typename Callback>
bool DrainWorklistWithYielding(
    JobDelegate* job_delegate, StatsCollector* stats_collector,
    ConcurrentMarkingState& marking_state,
    heap::base::IncrementalMarkingSchedule& incremental_marking_schedule,
    WorklistLocal& worklist_local, Callback callback) {
  return DrainWorklistWithPredicate<kDeadlineCheckInterval>(
      [&incremental_marking_schedule, &marking_state, job_delegate]() {
        incremental_marking_schedule.AddConcurrentlyMarkedBytes(
            marking_state.RecentlyMarkedBytes());
        return job_delegate->ShouldYield();
      },
      [stats_collector]() {
        return StatsCollector::DisabledConcurrentScope(stats_collector,
                                                       scope_id);
      },
      worklist_local, callback);
}

size_t WorkSizeForConcurrentMarking(MarkingWorklists& marking_worklists) {
  return marking_worklists.marking_worklist()->Size() +
         marking_worklists.write_barrier_worklist()->Size() +
         marking_worklists.previously_not_fully_constructed_worklist()->Size();
}

// Checks whether worklists' global pools hold any segment a concurrent marker
// can steal. This is called before the concurrent marker holds any Locals, so
// no need to check local segments.
bool HasWorkForConcurrentMarking(MarkingWorklists& marking_worklists) {
  return !marking_worklists.marking_worklist()->IsEmpty() ||
         !marking_worklists.write_barrier_worklist()->IsEmpty() ||
         !marking_worklists.previously_not_fully_constructed_worklist()
              ->IsEmpty();
}

class ConcurrentMarkingTask final : public v8::JobTask {
 public:
  explicit ConcurrentMarkingTask(ConcurrentMarkerBase&);

  void Run(JobDelegate* delegate) final;

  size_t GetMaxConcurrency(size_t) const final;

 private:
  void ProcessWorklists(JobDelegate*, ConcurrentMarkingState&, Visitor&);

  const ConcurrentMarkerBase& concurrent_marker_;
};

ConcurrentMarkingTask::ConcurrentMarkingTask(
    ConcurrentMarkerBase& concurrent_marker)
    : concurrent_marker_(concurrent_marker) {}

void ConcurrentMarkingTask::Run(JobDelegate* job_delegate) {
  StatsCollector::EnabledConcurrentScope stats_scope(
      concurrent_marker_.heap().stats_collector(),
      StatsCollector::kConcurrentMark);
  if (!HasWorkForConcurrentMarking(concurrent_marker_.marking_worklists()))
    return;
  ConcurrentMarkingState concurrent_marking_state(
      concurrent_marker_.heap(), concurrent_marker_.marking_worklists(),
      concurrent_marker_.heap().compactor().compaction_worklists());
  std::unique_ptr<Visitor> concurrent_marking_visitor =
      concurrent_marker_.CreateConcurrentMarkingVisitor(
          concurrent_marking_state);
  ProcessWorklists(job_delegate, concurrent_marking_state,
                   *concurrent_marking_visitor);
  concurrent_marker_.incremental_marking_schedule().AddConcurrentlyMarkedBytes(
      concurrent_marking_state.RecentlyMarkedBytes());
  concurrent_marking_state.Publish();
}

size_t ConcurrentMarkingTask::GetMaxConcurrency(
    size_t current_worker_count) const {
  return WorkSizeForConcurrentMarking(concurrent_marker_.marking_worklists()) +
         current_worker_count;
}

void ConcurrentMarkingTask::ProcessWorklists(
    JobDelegate* job_delegate, ConcurrentMarkingState& concurrent_marking_state,
    Visitor& concurrent_marking_visitor) {
  StatsCollector* stats_collector = concurrent_marker_.heap().stats_collector();
  do {
    if (!DrainWorklistWithYielding<
            StatsCollector::kConcurrentMarkProcessNotFullyconstructedWorklist>(
            job_delegate, stats_collector, concurrent_marking_state,
            concurrent_marker_.incremental_marking_schedule(),
            concurrent_marking_state
                .previously_not_fully_constructed_worklist(),
            [&concurrent_marking_state,
             &concurrent_marking_visitor](HeapObjectHeader* header) {
              BasePage::FromPayload(header)->SynchronizedLoad();
              concurrent_marking_state.AccountMarkedBytes(*header);
              DynamicallyTraceMarkedObject<AccessMode::kAtomic>(
                  concurrent_marking_visitor, *header);
            })) {
      return;
    }
    if (!DrainWorklistWithYielding<
            StatsCollector::kConcurrentMarkProcessMarkingWorklist>(
            job_delegate, stats_collector, concurrent_marking_state,
            concurrent_marker_.incremental_marking_schedule(),
            concurrent_marking_state.marking_worklist(),
            [&concurrent_marking_state, &concurrent_marking_visitor](
                const MarkingWorklists::MarkingItem& item) {
              BasePage::FromPayload(item.base_object_payload)
                  ->SynchronizedLoad();
              const HeapObjectHeader& header =
                  HeapObjectHeader::FromObject(item.base_object_payload);
              DCHECK(!header.IsInConstruction<AccessMode::kAtomic>());
              DCHECK(header.IsMarked<AccessMode::kAtomic>());
              concurrent_marking_state.AccountMarkedBytes(header);
              item.callback(&concurrent_marking_visitor,
                            item.base_object_payload);
            })) {
      return;
    }
    if (!DrainWorklistWithYielding<
            StatsCollector::kConcurrentMarkProcessWriteBarrierWorklist>(
            job_delegate, stats_collector, concurrent_marking_state,
            concurrent_marker_.incremental_marking_schedule(),
            concurrent_marking_state.write_barrier_worklist(),
            [&concurrent_marking_state,
             &concurrent_marking_visitor](HeapObjectHeader* header) {
              BasePage::FromPayload(header)->SynchronizedLoad();
              concurrent_marking_state.AccountMarkedBytes(*header);
              DynamicallyTraceMarkedObject<AccessMode::kAtomic>(
                  concurrent_marking_visitor, *header);
            })) {
      return;
    }
    if (!DrainWorklistWithYielding<
            StatsCollector::kConcurrentMarkProcessEphemeronWorklist>(
            job_delegate, stats_collector, concurrent_marking_state,
            concurrent_marker_.incremental_marking_schedule(),
            concurrent_marking_state.ephemeron_pairs_for_processing_worklist(),
            [&concurrent_marking_state, &concurrent_marking_visitor](
                const MarkingWorklists::EphemeronPairItem& item) {
              concurrent_marking_state.ProcessEphemeron(
                  item.key, item.value, item.value_desc,
                  concurrent_marking_visitor);
            })) {
      return;
    }
  } while (
      !concurrent_marking_state.marking_worklist().IsLocalAndGlobalEmpty());
}

}  // namespace

ConcurrentMarkerBase::ConcurrentMarkerBase(
    HeapBase& heap, MarkingWorklists& marking_worklists,
    heap::base::IncrementalMarkingSchedule& incremental_marking_schedule,
    cppgc::Platform* platform)
    : heap_(heap),
      marking_worklists_(marking_worklists),
      incremental_marking_schedule_(incremental_marking_schedule),
      platform_(platform) {}

void ConcurrentMarkerBase::Start() {
  DCHECK(platform_);
  concurrent_marking_handle_ =
      platform_->PostJob(v8::TaskPriority::kUserVisible,
                         std::make_unique<ConcurrentMarkingTask>(*this));
}

bool ConcurrentMarkerBase::Join() {
  if (!concurrent_marking_handle_ || !concurrent_marking_handle_->IsValid())
    return false;

  concurrent_marking_handle_->Join();
  return true;
}

bool ConcurrentMarkerBase::Cancel() {
  if (!concurrent_marking_handle_ || !concurrent_marking_handle_->IsValid())
    return false;

  concurrent_marking_handle_->Cancel();
  return true;
}

bool ConcurrentMarkerBase::IsActive() const {
  return concurrent_marking_handle_ && concurrent_marking_handle_->IsValid();
}

ConcurrentMarkerBase::~ConcurrentMarkerBase() {
  CHECK_IMPLIES(concurrent_marking_handle_,
                !concurrent_marking_handle_->IsValid());
}

void ConcurrentMarkerBase::NotifyIncrementalMutatorStepCompleted() {
  DCHECK(concurrent_marking_handle_);
  if (HasWorkForConcurrentMarking(marking_worklists_)) {
    // Notifies the scheduler that max concurrency might have increased.
    // This will adjust the number of markers if necessary.
    IncreaseMarkingPriorityIfNeeded();
    concurrent_marking_handle_->NotifyConcurrencyIncrease();
  }
}

void ConcurrentMarkerBase::NotifyOfWorkIfNeeded(cppgc::TaskPriority priority) {
  if (HasWorkForConcurrentMarking(marking_worklists_)) {
    concurrent_marking_handle_->UpdatePriority(priority);
    concurrent_marking_handle_->NotifyConcurrencyIncrease();
  }
}

void ConcurrentMarkerBase::IncreaseMarkingPriorityIfNeeded() {
  if (!concurrent_marking_handle_->UpdatePriorityEnabled()) return;
  if (concurrent_marking_priority_increased_) return;
  // If concurrent tasks aren't executed, it might delay GC finalization.
  // As long as GC is active so is the write barrier, which incurs a
  // performance cost. Marking is estimated to take overall
  // |MarkingSchedulingOracle::kEstimatedMarkingTimeMs|. If
  // concurrent marking tasks have not reported any progress (i.e. the
  // concurrently marked bytes count as not changed) in over
  // |kMarkingScheduleRatioBeforeConcurrentPriorityIncrease| of
  // that expected duration, we increase the concurrent task priority
  // for the duration of the current GC. This is meant to prevent the
  // GC from exceeding it's expected end time.
  size_t current_concurrently_marked_bytes_ =
      incremental_marking_schedule_.GetConcurrentlyMarkedBytes();
  if (current_concurrently_marked_bytes_ > last_concurrently_marked_bytes_) {
    last_concurrently_marked_bytes_ = current_concurrently_marked_bytes_;
    last_concurrently_marked_bytes_update_ = v8::base::TimeTicks::Now();
  } else if ((v8::base::TimeTicks::Now() -
              last_concurrently_marked_bytes_update_)
                 .InMilliseconds() >
             kMarkingScheduleRatioBeforeConcurrentPriorityIncrease *
                 heap::base::IncrementalMarkingSchedule::kEstimatedMarkingTime
                     .InMillisecondsF()) {
    concurrent_marking_handle_->UpdatePriority(
        cppgc::TaskPriority::kUserBlocking);
    concurrent_marking_priority_increased_ = true;
  }
}

std::unique_ptr<Visitor> ConcurrentMarker::CreateConcurrentMarkingVisitor(
    ConcurrentMarkingState& marking_state) const {
  return std::make_unique<ConcurrentMarkingVisitor>(heap(), marking_state);
}

}  // namespace internal
}  // namespace cppgc
