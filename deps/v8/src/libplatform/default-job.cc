// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-job.h"

#include "src/base/bits.h"
#include "src/base/macros.h"

namespace v8 {
namespace platform {
namespace {

// Capped to allow assigning task_ids from a bitfield.
constexpr size_t kMaxWorkersPerJob = 32;

}  // namespace

DefaultJobState::JobDelegate::~JobDelegate() {
  static_assert(kInvalidTaskId >= kMaxWorkersPerJob,
                "kInvalidTaskId must be outside of the range of valid task_ids "
                "[0, kMaxWorkersPerJob)");
  if (task_id_ != kInvalidTaskId) outer_->ReleaseTaskId(task_id_);
}

uint8_t DefaultJobState::JobDelegate::GetTaskId() {
  if (task_id_ == kInvalidTaskId) task_id_ = outer_->AcquireTaskId();
  return task_id_;
}

DefaultJobState::DefaultJobState(Platform* platform,
                                 std::unique_ptr<JobTask> job_task,
                                 TaskPriority priority,
                                 size_t num_worker_threads)
    : platform_(platform),
      job_task_(std::move(job_task)),
      priority_(priority),
      num_worker_threads_(std::min(num_worker_threads, kMaxWorkersPerJob)) {}

DefaultJobState::~DefaultJobState() { DCHECK_EQ(0U, active_workers_); }

void DefaultJobState::NotifyConcurrencyIncrease() {
  if (is_canceled_.load(std::memory_order_relaxed)) return;

  size_t num_tasks_to_post = 0;
  TaskPriority priority;
  {
    base::MutexGuard guard(&mutex_);
    const size_t max_concurrency = CappedMaxConcurrency(active_workers_);
    // Consider |pending_tasks_| to avoid posting too many tasks.
    if (max_concurrency > active_workers_ + pending_tasks_) {
      num_tasks_to_post = max_concurrency - active_workers_ - pending_tasks_;
      pending_tasks_ += num_tasks_to_post;
    }
    priority = priority_;
  }
  // Post additional worker tasks to reach |max_concurrency|.
  for (size_t i = 0; i < num_tasks_to_post; ++i) {
    CallOnWorkerThread(priority, std::make_unique<DefaultJobWorker>(
                                     shared_from_this(), job_task_.get()));
  }
}

uint8_t DefaultJobState::AcquireTaskId() {
  static_assert(kMaxWorkersPerJob <= sizeof(assigned_task_ids_) * 8,
                "TaskId bitfield isn't big enough to fit kMaxWorkersPerJob.");
  uint32_t assigned_task_ids =
      assigned_task_ids_.load(std::memory_order_relaxed);
  DCHECK_LE(v8::base::bits::CountPopulation(assigned_task_ids) + 1,
            kMaxWorkersPerJob);
  uint32_t new_assigned_task_ids = 0;
  uint8_t task_id = 0;
  // memory_order_acquire on success, matched with memory_order_release in
  // ReleaseTaskId() so that operations done by previous threads that had
  // the same task_id become visible to the current thread.
  do {
    // Count trailing one bits. This is the id of the right-most 0-bit in
    // |assigned_task_ids|.
    task_id = v8::base::bits::CountTrailingZeros32(~assigned_task_ids);
    new_assigned_task_ids = assigned_task_ids | (uint32_t(1) << task_id);
  } while (!assigned_task_ids_.compare_exchange_weak(
      assigned_task_ids, new_assigned_task_ids, std::memory_order_acquire,
      std::memory_order_relaxed));
  return task_id;
}

void DefaultJobState::ReleaseTaskId(uint8_t task_id) {
  // memory_order_release to match AcquireTaskId().
  uint32_t previous_task_ids = assigned_task_ids_.fetch_and(
      ~(uint32_t(1) << task_id), std::memory_order_release);
  DCHECK(previous_task_ids & (uint32_t(1) << task_id));
  USE(previous_task_ids);
}

void DefaultJobState::Join() {
  auto WaitForParticipationOpportunity = [this]() -> size_t {
    // Subtract one from active_workers_ since the current thread is not
    // participating yet.
    size_t max_concurrency = CappedMaxConcurrency(active_workers_ - 1);
    // Wait until we can participate in the job.
    while (active_workers_ > max_concurrency && active_workers_ > 1) {
      worker_released_condition_.Wait(&mutex_);
      max_concurrency = CappedMaxConcurrency(active_workers_ - 1);
    }
    DCHECK_LE(0, max_concurrency);
    if (max_concurrency != 0) return max_concurrency;
    // The job is done (max_concurrency dropped to zero).
    DCHECK_EQ(1, active_workers_);
    active_workers_ = 0;
    is_canceled_.store(true, std::memory_order_relaxed);
    return 0;
  };

  size_t num_tasks_to_post = 0;
  {
    base::MutexGuard guard(&mutex_);
    priority_ = TaskPriority::kUserBlocking;
    // Reserve a worker for the joining (current) thread.
    // GetMaxConcurrency() is ignored here, but if necessary we wait below
    // for workers to return so we don't exceed GetMaxConcurrency().
    ++num_worker_threads_;
    ++active_workers_;
    size_t max_concurrency = WaitForParticipationOpportunity();
    if (max_concurrency == 0) return;
    // Compute the number of additional worker tasks to spawn.
    if (max_concurrency > active_workers_ + pending_tasks_) {
      num_tasks_to_post = max_concurrency - active_workers_ - pending_tasks_;
      pending_tasks_ += num_tasks_to_post;
    }
  }
  // Spawn more worker tasks if needed.
  for (size_t i = 0; i < num_tasks_to_post; ++i) {
    CallOnWorkerThread(TaskPriority::kUserBlocking,
                       std::make_unique<DefaultJobWorker>(shared_from_this(),
                                                          job_task_.get()));
  }

  DefaultJobState::JobDelegate delegate(this, true);
  while (true) {
    // Participate in job execution, as one active worker.
    job_task_->Run(&delegate);

    base::MutexGuard guard(&mutex_);
    if (WaitForParticipationOpportunity() == 0) return;
  }
}

void DefaultJobState::CancelAndWait() {
  {
    base::MutexGuard guard(&mutex_);
    is_canceled_.store(true, std::memory_order_relaxed);
    while (active_workers_ > 0) {
      worker_released_condition_.Wait(&mutex_);
    }
  }
}

void DefaultJobState::CancelAndDetach() {
  is_canceled_.store(true, std::memory_order_relaxed);
}

bool DefaultJobState::IsActive() {
  base::MutexGuard guard(&mutex_);
  return job_task_->GetMaxConcurrency(active_workers_) != 0 ||
         active_workers_ != 0;
}

bool DefaultJobState::CanRunFirstTask() {
  base::MutexGuard guard(&mutex_);
  --pending_tasks_;
  if (is_canceled_.load(std::memory_order_relaxed)) return false;
  if (active_workers_ >= CappedMaxConcurrency(active_workers_)) return false;
  // Acquire current worker.
  ++active_workers_;
  return true;
}

bool DefaultJobState::DidRunTask() {
  size_t num_tasks_to_post = 0;
  TaskPriority priority;
  {
    base::MutexGuard guard(&mutex_);
    const size_t max_concurrency = CappedMaxConcurrency(active_workers_ - 1);
    if (is_canceled_.load(std::memory_order_relaxed) ||
        active_workers_ > max_concurrency) {
      // Release current worker and notify.
      --active_workers_;
      worker_released_condition_.NotifyOne();
      return false;
    }
    // Consider |pending_tasks_| to avoid posting too many tasks.
    if (max_concurrency > active_workers_ + pending_tasks_) {
      num_tasks_to_post = max_concurrency - active_workers_ - pending_tasks_;
      pending_tasks_ += num_tasks_to_post;
    }
    priority = priority_;
  }
  // Post additional worker tasks to reach |max_concurrency| in the case that
  // max concurrency increased. This is not strictly necessary, since
  // NotifyConcurrencyIncrease() should eventually be invoked. However, some
  // users of PostJob() batch work and tend to call NotifyConcurrencyIncrease()
  // late. Posting here allows us to spawn new workers sooner.
  for (size_t i = 0; i < num_tasks_to_post; ++i) {
    CallOnWorkerThread(priority, std::make_unique<DefaultJobWorker>(
                                     shared_from_this(), job_task_.get()));
  }
  return true;
}

size_t DefaultJobState::CappedMaxConcurrency(size_t worker_count) const {
  return std::min(job_task_->GetMaxConcurrency(worker_count),
                  num_worker_threads_);
}

void DefaultJobState::CallOnWorkerThread(TaskPriority priority,
                                         std::unique_ptr<Task> task) {
  platform_->PostTaskOnWorkerThread(priority, std::move(task));
}

void DefaultJobState::UpdatePriority(TaskPriority priority) {
  base::MutexGuard guard(&mutex_);
  priority_ = priority;
}

DefaultJobHandle::DefaultJobHandle(std::shared_ptr<DefaultJobState> state)
    : state_(std::move(state)) {}

DefaultJobHandle::~DefaultJobHandle() { DCHECK_EQ(nullptr, state_); }

void DefaultJobHandle::Join() {
  state_->Join();
  state_ = nullptr;
}
void DefaultJobHandle::Cancel() {
  state_->CancelAndWait();
  state_ = nullptr;
}

void DefaultJobHandle::CancelAndDetach() {
  state_->CancelAndDetach();
  state_ = nullptr;
}

bool DefaultJobHandle::IsActive() { return state_->IsActive(); }

void DefaultJobHandle::UpdatePriority(TaskPriority priority) {
  state_->UpdatePriority(priority);
}

}  // namespace platform
}  // namespace v8
