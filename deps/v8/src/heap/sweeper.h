// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_SWEEPER_H_
#define V8_HEAP_SWEEPER_H_

#include <limits>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "src/base/platform/condition-variable.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/pretenuring-handler.h"
#include "src/heap/slot-set.h"
#include "src/tasks/cancelable-task.h"

namespace v8 {
namespace internal {

class MutablePageMetadata;
class NonAtomicMarkingState;
class PageMetadata;
class LargePageMetadata;
class PagedSpaceBase;
class Space;

enum class FreeSpaceTreatmentMode { kIgnoreFreeSpace, kZapFreeSpace };

class Sweeper {
 public:
  // When the scope is entered, the concurrent sweeping tasks
  // are preempted and are not looking at the heap objects, concurrent sweeping
  // is resumed when the scope is exited.
  class V8_NODISCARD PauseMajorSweepingScope {
   public:
    explicit PauseMajorSweepingScope(Sweeper* sweeper);
    ~PauseMajorSweepingScope();

   private:
    Sweeper* const sweeper_;
    const bool resume_on_exit_;
  };

  using SweepingList = std::vector<PageMetadata*>;
  using SweptList = std::vector<PageMetadata*>;

  enum class SweepingMode { kEagerDuringGC, kLazyOrConcurrent };

  // LocalSweeper holds local data structures required for sweeping and is used
  // to initiate sweeping and promoted page iteration on multiple threads. Each
  // thread should holds its own LocalSweeper. Once sweeping is done, all
  // LocalSweepers should be finalized on the main thread.
  //
  // LocalSweeper is not thread-safe and should not be concurrently by several
  // threads. The exceptions to this rule are allocations during parallel
  // evacuation and from concurrent allocators. In practice the data structures
  // in LocalSweeper are only actively used for new space sweeping. Since
  // parallel evacuators and concurrent allocators never try to allocate in new
  // space, they will never contribute to new space sweeping and thus can use
  // the main thread's local sweeper without risk of data races.
  class LocalSweeper final {
   public:
    explicit LocalSweeper(Sweeper* sweeper) : sweeper_(sweeper) {
      DCHECK_NOT_NULL(sweeper_);
    }
    ~LocalSweeper() = default;

    // Returns true if any swept pages can be allocated on.
    bool ParallelSweepSpace(
        AllocationSpace identity, SweepingMode sweeping_mode,
        uint32_t max_pages = std::numeric_limits<uint32_t>::max());
    // Intended to be called either with a JobDelegate from a job. Returns true
    // if iteration is finished.
    bool ContributeAndWaitForPromotedPagesIteration(JobDelegate* delegate);
    bool ContributeAndWaitForPromotedPagesIteration();

   private:
    void ParallelSweepPage(PageMetadata* page, AllocationSpace identity,
                           SweepingMode sweeping_mode);

    bool ParallelIteratePromotedPages(JobDelegate* delegate);
    bool ParallelIteratePromotedPages();
    void ParallelIteratePromotedPage(MutablePageMetadata* page);

    template <typename ShouldYieldCallback>
    bool ContributeAndWaitForPromotedPagesIterationImpl(
        ShouldYieldCallback should_yield_callback);
    template <typename ShouldYieldCallback>
    bool ParallelIteratePromotedPagesImpl(
        ShouldYieldCallback should_yield_callback);

    Sweeper* const sweeper_;

    friend class Sweeper;
  };

  explicit Sweeper(Heap* heap);
  ~Sweeper();

  bool major_sweeping_in_progress() const {
    return major_sweeping_state_.in_progress();
  }
  bool minor_sweeping_in_progress() const {
    return minor_sweeping_state_.in_progress();
  }
  bool sweeping_in_progress() const {
    return minor_sweeping_in_progress() || major_sweeping_in_progress();
  }
  bool sweeping_in_progress_for_space(AllocationSpace space) const {
    if (space == NEW_SPACE) return minor_sweeping_in_progress();
    return major_sweeping_in_progress();
  }

  void TearDown();

  void AddPage(AllocationSpace space, PageMetadata* page);
  void AddNewSpacePage(PageMetadata* page);
  void AddPromotedPage(MutablePageMetadata* chunk);

  // Returns true if any swept pages can be allocated on.
  bool ParallelSweepSpace(
      AllocationSpace identity, SweepingMode sweeping_mode,
      uint32_t max_pages = std::numeric_limits<uint32_t>::max());

  void EnsurePageIsSwept(PageMetadata* page);
  void WaitForPageToBeSwept(PageMetadata* page);

  // After calling this function sweeping is considered to be in progress
  // and the main thread can sweep lazily, but the background sweeper tasks
  // are not running yet.
  void StartMajorSweeping();
  void StartMinorSweeping();
  void InitializeMajorSweeping();
  void InitializeMinorSweeping();
  V8_EXPORT_PRIVATE void StartMajorSweeperTasks();
  V8_EXPORT_PRIVATE void StartMinorSweeperTasks();

  // Finishes all major sweeping tasks/work without changing the sweeping state.
  void FinishMajorJobs();
  // Finishes all major sweeping tasks/work and resets sweeping state to NOT in
  // progress.
  void EnsureMajorCompleted();

  // Finishes all minor sweeping tasks/work without changing the sweeping state.
  void FinishMinorJobs();
  // Finishes all minor sweeping tasks/work and resets sweeping state to NOT in
  // progress.
  void EnsureMinorCompleted();

  bool AreMinorSweeperTasksRunning() const;
  bool AreMajorSweeperTasksRunning() const;

  bool UsingMajorSweeperTasks() const;

  PageMetadata* GetSweptPageSafe(PagedSpaceBase* space);
  SweptList GetAllSweptPagesSafe(PagedSpaceBase* space);

  bool IsSweepingDoneForSpace(AllocationSpace space) const;

  GCTracer::Scope::ScopeId GetTracingScope(AllocationSpace space,
                                           bool is_joining_thread);

  bool IsIteratingPromotedPages() const;
  void ContributeAndWaitForPromotedPagesIteration();

  bool ShouldRefillFreelistForSpace(AllocationSpace space) const;

  void SweepEmptyNewSpacePage(PageMetadata* page);

  uint64_t GetTraceIdForFlowEvent(GCTracer::Scope::ScopeId scope_id) const;

#if DEBUG
  // Can only be called on the main thread when no tasks are running.
  bool HasUnsweptPagesForMajorSweeping() const;
#endif  // DEBUG

 private:
  NonAtomicMarkingState* marking_state() const { return marking_state_; }

  void RawSweep(PageMetadata* p,
                FreeSpaceTreatmentMode free_space_treatment_mode,
                SweepingMode sweeping_mode, bool should_reduce_memory);

  void AddPageImpl(AllocationSpace space, PageMetadata* page);

  class ConcurrentMajorSweeper;
  class ConcurrentMinorSweeper;

  class MajorSweeperJob;
  class MinorSweeperJob;

  static constexpr int kNumberOfSweepingSpaces =
      LAST_SWEEPABLE_SPACE - FIRST_SWEEPABLE_SPACE + 1;

  template <typename Callback>
  void ForAllSweepingSpaces(Callback callback) const {
    if (v8_flags.minor_ms) {
      callback(NEW_SPACE);
    }
    callback(OLD_SPACE);
    callback(CODE_SPACE);
    callback(SHARED_SPACE);
    callback(TRUSTED_SPACE);
  }

  // Helper function for RawSweep. Depending on the FreeListRebuildingMode and
  // FreeSpaceTreatmentMode this function may add the free memory to a free
  // list, make the memory iterable, clear it, and return the free memory to
  // the operating system.
  size_t FreeAndProcessFreedMemory(
      Address free_start, Address free_end, PageMetadata* page, Space* space,
      FreeSpaceTreatmentMode free_space_treatment_mode,
      bool should_reduce_memory);

  // Helper function for RawSweep. Handle remembered set entries in the freed
  // memory which require clearing.
  void CleanupRememberedSetEntriesForFreedMemory(
      Address free_start, Address free_end, PageMetadata* page,
      bool record_free_ranges, TypedSlotSet::FreeRangesMap* free_ranges_map,
      SweepingMode sweeping_mode);

  // Helper function for RawSweep. Clears invalid typed slots in the given free
  // ranges.
  void CleanupTypedSlotsInFreeMemory(
      PageMetadata* page, const TypedSlotSet::FreeRangesMap& free_ranges_map,
      SweepingMode sweeping_mode);

  // Helper function for RawSweep. Clears the mark bits and ensures consistency
  // of live bytes.
  void ClearMarkBitsAndHandleLivenessStatistics(PageMetadata* page,
                                                size_t live_bytes);

  size_t ConcurrentMinorSweepingPageCount();
  size_t ConcurrentMajorSweepingPageCount();

  PageMetadata* GetSweepingPageSafe(AllocationSpace space);
  MutablePageMetadata* GetPromotedPageSafe();
  bool TryRemoveSweepingPageSafe(AllocationSpace space, PageMetadata* page);
  bool TryRemovePromotedPageSafe(MutablePageMetadata* chunk);

  void PrepareToBeSweptPage(AllocationSpace space, PageMetadata* page);
  void PrepareToBeIteratedPromotedPage(PageMetadata* page);

  static bool IsValidSweepingSpace(AllocationSpace space) {
    return space >= FIRST_SWEEPABLE_SPACE && space <= LAST_SWEEPABLE_SPACE;
  }

  static int GetSweepSpaceIndex(AllocationSpace space) {
    DCHECK(IsValidSweepingSpace(space));
    return space - FIRST_SWEEPABLE_SPACE;
  }

  void NotifyPromotedPageIterationFinished(MutablePageMetadata* chunk);
  void NotifyPromotedPagesIterationFinished();

  void AddSweptPage(PageMetadata* page, AllocationSpace identity);

  enum class SweepingScope { kMinor, kMajor };
  template <SweepingScope scope>
  class SweepingState {
    using ConcurrentSweeper =
        typename std::conditional<scope == SweepingScope::kMinor,
                                  ConcurrentMinorSweeper,
                                  ConcurrentMajorSweeper>::type;
    using SweeperJob =
        typename std::conditional<scope == SweepingScope::kMinor,
                                  MinorSweeperJob, MajorSweeperJob>::type;

   public:
    explicit SweepingState(Sweeper* sweeper);
    ~SweepingState();

    void InitializeSweeping();
    void StartSweeping();
    void StartConcurrentSweeping();
    void StopConcurrentSweeping();
    void FinishSweeping();
    void JoinSweeping();

    bool HasValidJob() const;
    bool HasActiveJob() const;

    bool in_progress() const { return in_progress_; }
    bool should_reduce_memory() const { return should_reduce_memory_; }
    std::vector<ConcurrentSweeper>& concurrent_sweepers() {
      return concurrent_sweepers_;
    }

    void Pause();
    void Resume();

    uint64_t trace_id() const { return trace_id_; }
    uint64_t background_trace_id() const { return background_trace_id_; }

   private:
    Sweeper* sweeper_;
    // Main thread can finalize sweeping, while background threads allocation
    // slow path checks this flag to see whether it could support concurrent
    // sweeping.
    std::atomic<bool> in_progress_{false};
    std::unique_ptr<JobHandle> job_handle_;
    std::vector<ConcurrentSweeper> concurrent_sweepers_;
    uint64_t trace_id_ = 0;
    uint64_t background_trace_id_ = 0;
    bool should_reduce_memory_ = false;
  };

  Heap* const heap_;
  NonAtomicMarkingState* const marking_state_;
  base::Mutex mutex_;
  base::ConditionVariable cv_page_swept_;
  SweptList swept_list_[kNumberOfSweepingSpaces];
  SweepingList sweeping_list_[kNumberOfSweepingSpaces];
  std::atomic<bool> has_sweeping_work_[kNumberOfSweepingSpaces]{false};
  std::atomic<bool> has_swept_pages_[kNumberOfSweepingSpaces]{false};
  std::vector<MutablePageMetadata*> sweeping_list_for_promoted_page_iteration_;
  LocalSweeper main_thread_local_sweeper_;
  SweepingState<SweepingScope::kMajor> major_sweeping_state_{this};
  SweepingState<SweepingScope::kMinor> minor_sweeping_state_{this};

  // The following fields are used for maintaining an order between iterating
  // promoted pages and sweeping array buffer extensions.
  size_t promoted_pages_for_iteration_count_ = 0;
  std::atomic<size_t> iterated_promoted_pages_count_{0};
  base::Mutex promoted_pages_iteration_notification_mutex_;
  base::ConditionVariable promoted_pages_iteration_notification_variable_;
  std::atomic<bool> promoted_page_iteration_in_progress_{false};
};

template <typename ShouldYieldCallback>
bool Sweeper::LocalSweeper::ContributeAndWaitForPromotedPagesIterationImpl(
    ShouldYieldCallback should_yield_callback) {
  if (!sweeper_->sweeping_in_progress()) return true;
  if (!sweeper_->IsIteratingPromotedPages()) return true;
  if (!ParallelIteratePromotedPagesImpl(should_yield_callback)) return false;
  base::MutexGuard guard(
      &sweeper_->promoted_pages_iteration_notification_mutex_);
  // Check again that iteration is not yet finished.
  if (!sweeper_->IsIteratingPromotedPages()) return true;
  if (should_yield_callback()) {
    return false;
  }
  sweeper_->promoted_pages_iteration_notification_variable_.Wait(
      &sweeper_->promoted_pages_iteration_notification_mutex_);
  return true;
}

template <typename ShouldYieldCallback>
bool Sweeper::LocalSweeper::ParallelIteratePromotedPagesImpl(
    ShouldYieldCallback should_yield_callback) {
  while (!should_yield_callback()) {
    MutablePageMetadata* chunk = sweeper_->GetPromotedPageSafe();
    if (chunk == nullptr) return true;
    ParallelIteratePromotedPage(chunk);
  }
  return false;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_SWEEPER_H_
