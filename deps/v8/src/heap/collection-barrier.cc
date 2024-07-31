// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/collection-barrier.h"

#include "src/base/platform/mutex.h"
#include "src/base/platform/time.h"
#include "src/common/globals.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/heap-inl.h"
#include "src/heap/heap.h"
#include "src/heap/local-heap-inl.h"
#include "src/heap/parked-scope.h"

namespace v8 {
namespace internal {

CollectionBarrier::CollectionBarrier(
    Heap* heap, std::shared_ptr<v8::TaskRunner> foreground_task_runner)
    : heap_(heap), foreground_task_runner_(foreground_task_runner) {}

bool CollectionBarrier::WasGCRequested() {
  return collection_requested_.load();
}

bool CollectionBarrier::TryRequestGC() {
  base::MutexGuard guard(&mutex_);
  if (shutdown_requested_) return false;
  bool was_already_requested = collection_requested_.exchange(true);

  if (!was_already_requested) {
    CHECK(!timer_.IsStarted());
    timer_.Start();
  }

  return true;
}

class BackgroundCollectionInterruptTask : public CancelableTask {
 public:
  explicit BackgroundCollectionInterruptTask(Heap* heap)
      : CancelableTask(heap->isolate()), heap_(heap) {}

  ~BackgroundCollectionInterruptTask() override = default;
  BackgroundCollectionInterruptTask(const BackgroundCollectionInterruptTask&) =
      delete;
  BackgroundCollectionInterruptTask& operator=(
      const BackgroundCollectionInterruptTask&) = delete;

 private:
  // v8::internal::CancelableTask overrides.
  void RunInternal() override {
    // In case multi-cage pointer compression mode is enabled ensure that
    // current thread's cage base values are properly initialized.
    PtrComprCageAccessScope ptr_compr_cage_access_scope(heap_->isolate());
    heap_->CheckCollectionRequested();
  }

  Heap* heap_;
};

void CollectionBarrier::NotifyShutdownRequested() {
  base::MutexGuard guard(&mutex_);
  if (timer_.IsStarted()) timer_.Stop();
  shutdown_requested_ = true;
  cv_wakeup_.NotifyAll();
}

void CollectionBarrier::ResumeThreadsAwaitingCollection() {
  base::MutexGuard guard(&mutex_);
  DCHECK(!timer_.IsStarted());
  collection_requested_.store(false);
  block_for_collection_ = false;
  collection_performed_ = true;
  cv_wakeup_.NotifyAll();
}

void CollectionBarrier::CancelCollectionAndResumeThreads() {
  base::MutexGuard guard(&mutex_);
  if (timer_.IsStarted()) timer_.Stop();
  collection_requested_.store(false);
  block_for_collection_ = false;
  collection_performed_ = false;
  cv_wakeup_.NotifyAll();
}

bool CollectionBarrier::AwaitCollectionBackground(LocalHeap* local_heap) {
  bool first_thread;

  {
    // Update flag before parking this thread, this guarantees that the flag is
    // set before the next GC.
    base::MutexGuard guard(&mutex_);
    if (shutdown_requested_) return false;

    // Collection was cancelled by the main thread.
    if (!collection_requested_.load()) return false;

    first_thread = !block_for_collection_;
    block_for_collection_ = true;
    CHECK(timer_.IsStarted());
  }

  // The first thread needs to activate the stack guard and post the task.
  if (first_thread) {
    Isolate* isolate = heap_->isolate();
    ExecutionAccess access(isolate);
    isolate->stack_guard()->RequestGC();

    foreground_task_runner_->PostTask(
        std::make_unique<BackgroundCollectionInterruptTask>(heap_));
  }

  bool collection_performed = false;
  local_heap->BlockWhileParked([this, &collection_performed]() {
    base::MutexGuard guard(&mutex_);

    while (block_for_collection_) {
      if (shutdown_requested_) {
        collection_performed = false;
        return;
      }
      cv_wakeup_.Wait(&mutex_);
    }

    // Collection may have been cancelled while blocking for it.
    collection_performed = collection_performed_;
  });

  return collection_performed;
}

void CollectionBarrier::StopTimeToCollectionTimer() {
  if (collection_requested_.load()) {
    base::MutexGuard guard(&mutex_);
    // The first thread that requests the GC, starts the timer first and *then*
    // parks itself. Since we are in a safepoint here, the timer is always
    // initialized here already.
    CHECK(timer_.IsStarted());
    base::TimeDelta delta = timer_.Elapsed();
    TRACE_EVENT_INSTANT1(TRACE_DISABLED_BY_DEFAULT("v8.gc"),
                         "V8.GC.TimeToCollectionOnBackground",
                         TRACE_EVENT_SCOPE_THREAD, "duration",
                         delta.InMillisecondsF());
    heap_->isolate()
        ->counters()
        ->gc_time_to_collection_on_background()
        ->AddTimedSample(delta);
    timer_.Stop();
  }
}

}  // namespace internal
}  // namespace v8
