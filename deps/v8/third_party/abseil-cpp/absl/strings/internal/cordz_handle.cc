// Copyright 2019 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "absl/strings/internal/cordz_handle.h"

#include <atomic>

#include "absl/base/internal/raw_logging.h"  // For ABSL_RAW_CHECK
#include "absl/base/no_destructor.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

namespace {

struct Queue {
  Queue() = default;

  absl::Mutex mutex;
  std::atomic<CordzHandle*> dq_tail ABSL_GUARDED_BY(mutex){nullptr};

  // Returns true if this delete queue is empty. This method does not acquire
  // the lock, but does a 'load acquire' observation on the delete queue tail.
  // It is used inside Delete() to check for the presence of a delete queue
  // without holding the lock. The assumption is that the caller is in the
  // state of 'being deleted', and can not be newly discovered by a concurrent
  // 'being constructed' snapshot instance. Practically, this means that any
  // such discovery (`find`, 'first' or 'next', etc) must have proper 'happens
  // before / after' semantics and atomic fences.
  bool IsEmpty() const ABSL_NO_THREAD_SAFETY_ANALYSIS {
    return dq_tail.load(std::memory_order_acquire) == nullptr;
  }
};

static Queue& GlobalQueue() {
  static absl::NoDestructor<Queue> global_queue;
  return *global_queue;
}

}  // namespace

CordzHandle::CordzHandle(bool is_snapshot) : is_snapshot_(is_snapshot) {
  Queue& global_queue = GlobalQueue();
  if (is_snapshot) {
    MutexLock lock(&global_queue.mutex);
    CordzHandle* dq_tail = global_queue.dq_tail.load(std::memory_order_acquire);
    if (dq_tail != nullptr) {
      dq_prev_ = dq_tail;
      dq_tail->dq_next_ = this;
    }
    global_queue.dq_tail.store(this, std::memory_order_release);
  }
}

CordzHandle::~CordzHandle() {
  Queue& global_queue = GlobalQueue();
  if (is_snapshot_) {
    std::vector<CordzHandle*> to_delete;
    {
      MutexLock lock(&global_queue.mutex);
      CordzHandle* next = dq_next_;
      if (dq_prev_ == nullptr) {
        // We were head of the queue, delete every CordzHandle until we reach
        // either the end of the list, or a snapshot handle.
        while (next && !next->is_snapshot_) {
          to_delete.push_back(next);
          next = next->dq_next_;
        }
      } else {
        // Another CordzHandle existed before this one, don't delete anything.
        dq_prev_->dq_next_ = next;
      }
      if (next) {
        next->dq_prev_ = dq_prev_;
      } else {
        global_queue.dq_tail.store(dq_prev_, std::memory_order_release);
      }
    }
    for (CordzHandle* handle : to_delete) {
      delete handle;
    }
  }
}

bool CordzHandle::SafeToDelete() const {
  return is_snapshot_ || GlobalQueue().IsEmpty();
}

void CordzHandle::Delete(CordzHandle* handle) {
  assert(handle);
  if (handle) {
    Queue& queue = GlobalQueue();
    if (!handle->SafeToDelete()) {
      MutexLock lock(&queue.mutex);
      CordzHandle* dq_tail = queue.dq_tail.load(std::memory_order_acquire);
      if (dq_tail != nullptr) {
        handle->dq_prev_ = dq_tail;
        dq_tail->dq_next_ = handle;
        queue.dq_tail.store(handle, std::memory_order_release);
        return;
      }
    }
    delete handle;
  }
}

std::vector<const CordzHandle*> CordzHandle::DiagnosticsGetDeleteQueue() {
  std::vector<const CordzHandle*> handles;
  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  CordzHandle* dq_tail = global_queue.dq_tail.load(std::memory_order_acquire);
  for (const CordzHandle* p = dq_tail; p; p = p->dq_prev_) {
    handles.push_back(p);
  }
  return handles;
}

bool CordzHandle::DiagnosticsHandleIsSafeToInspect(
    const CordzHandle* handle) const {
  if (!is_snapshot_) return false;
  if (handle == nullptr) return true;
  if (handle->is_snapshot_) return false;
  bool snapshot_found = false;
  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  for (const CordzHandle* p = global_queue.dq_tail; p; p = p->dq_prev_) {
    if (p == handle) return !snapshot_found;
    if (p == this) snapshot_found = true;
  }
  ABSL_ASSERT(snapshot_found);  // Assert that 'this' is in delete queue.
  return true;
}

std::vector<const CordzHandle*>
CordzHandle::DiagnosticsGetSafeToInspectDeletedHandles() {
  std::vector<const CordzHandle*> handles;
  if (!is_snapshot()) {
    return handles;
  }

  Queue& global_queue = GlobalQueue();
  MutexLock lock(&global_queue.mutex);
  for (const CordzHandle* p = dq_next_; p != nullptr; p = p->dq_next_) {
    if (!p->is_snapshot()) {
      handles.push_back(p);
    }
  }
  return handles;
}

}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
