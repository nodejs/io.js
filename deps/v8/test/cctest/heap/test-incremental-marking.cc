// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "src/heap/safepoint.h"

#ifdef __linux__
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <utility>

#include "src/handles/global-handles.h"
#include "src/heap/gc-tracer.h"
#include "src/heap/incremental-marking.h"
#include "src/heap/spaces.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-utils.h"

using v8::IdleTask;
using v8::Task;
using v8::Isolate;

namespace v8 {
namespace internal {
namespace heap {

class MockPlatform : public TestPlatform {
 public:
  MockPlatform() : taskrunner_(new MockTaskRunner()) {}
  ~MockPlatform() override {
    for (auto& task : worker_tasks_) {
      CcTest::default_platform()->CallOnWorkerThread(std::move(task));
    }
    worker_tasks_.clear();
  }

  std::shared_ptr<v8::TaskRunner> GetForegroundTaskRunner(
      v8::Isolate* isolate) override {
    return taskrunner_;
  }

  void PostTaskOnWorkerThreadImpl(TaskPriority priority,
                                  std::unique_ptr<Task> task,
                                  const SourceLocation& location) override {
    worker_tasks_.push_back(std::move(task));
  }

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return false; }

  bool PendingTask() { return taskrunner_->PendingTask(); }

  void PerformTask() { taskrunner_->PerformTask(); }

 private:
  class MockTaskRunner : public v8::TaskRunner {
   public:
    void PostTaskImpl(std::unique_ptr<v8::Task> task,
                      const SourceLocation& location) override {
      task_ = std::move(task);
    }

    void PostNonNestableTaskImpl(std::unique_ptr<Task> task,
                                 const SourceLocation& location) override {
      PostTask(std::move(task));
    }

    void PostDelayedTaskImpl(std::unique_ptr<Task> task,
                             double delay_in_seconds,
                             const SourceLocation& location) override {
      PostTask(std::move(task));
    }

    void PostNonNestableDelayedTaskImpl(
        std::unique_ptr<Task> task, double delay_in_seconds,
        const SourceLocation& location) override {
      PostTask(std::move(task));
    }

    void PostIdleTaskImpl(std::unique_ptr<IdleTask> task,
                          const SourceLocation& location) override {
      UNREACHABLE();
    }

    bool IdleTasksEnabled() override { return false; }
    bool NonNestableTasksEnabled() const override { return true; }
    bool NonNestableDelayedTasksEnabled() const override { return true; }

    bool PendingTask() { return task_ != nullptr; }

    void PerformTask() {
      std::unique_ptr<Task> task = std::move(task_);
      task->Run();
    }

   private:
    std::unique_ptr<Task> task_;
  };

  std::shared_ptr<MockTaskRunner> taskrunner_;
  std::vector<std::unique_ptr<Task>> worker_tasks_;
};

TEST_WITH_PLATFORM(IncrementalMarkingUsingTasks, MockPlatform) {
  if (!i::v8_flags.incremental_marking) return;
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.
  v8_flags.stress_incremental_marking = false;
  v8::Isolate* isolate = CcTest::isolate();
  {
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = CcTest::NewContext(isolate);
    v8::Context::Scope context_scope(context);
    Isolate* i_isolate = reinterpret_cast<i::Isolate*>(isolate);
    Heap* heap = i_isolate->heap();

    i::heap::SimulateFullSpace(heap->old_space());
    i::IncrementalMarking* marking = heap->incremental_marking();
    marking->Stop();
    {
      IsolateSafepointScope scope(heap);
      heap->tracer()->StartCycle(
          GarbageCollector::MARK_COMPACTOR, GarbageCollectionReason::kTesting,
          "collector cctest", GCTracer::MarkingType::kIncremental);
      marking->Start(GarbageCollector::MARK_COMPACTOR,
                     i::GarbageCollectionReason::kTesting);
    }
    CHECK(marking->IsMajorMarking());
    while (marking->IsMajorMarking()) {
      platform.PerformTask();
    }
    CHECK(marking->IsStopped());
  }
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
