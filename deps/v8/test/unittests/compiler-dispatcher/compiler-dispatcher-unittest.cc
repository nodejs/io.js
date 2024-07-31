// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include "include/v8-platform.h"
#include "src/api/api-inl.h"
#include "src/ast/ast-value-factory.h"
#include "src/ast/ast.h"
#include "src/ast/scopes.h"
#include "src/base/platform/condition-variable.h"
#include "src/base/platform/semaphore.h"
#include "src/codegen/compiler.h"
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"
#include "src/flags/flags.h"
#include "src/handles/handles.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "src/parsing/scanner-character-streams.h"
#include "src/zone/zone-list-inl.h"
#include "test/unittests/test-helpers.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

#ifdef DEBUG
#define DEBUG_ASSERT_EQ ASSERT_EQ
#else
#define DEBUG_ASSERT_EQ(...)
#endif

namespace v8 {
namespace internal {

class LazyCompileDispatcherTestFlags {
 public:
  LazyCompileDispatcherTestFlags(const LazyCompileDispatcherTestFlags&) =
      delete;
  LazyCompileDispatcherTestFlags& operator=(
      const LazyCompileDispatcherTestFlags&) = delete;
  static void SetFlagsForTest() {
    CHECK_NULL(save_flags_);
    save_flags_ = new SaveFlags();
    v8_flags.lazy_compile_dispatcher = true;
    FlagList::EnforceFlagImplications();
  }

  static void RestoreFlags() {
    CHECK_NOT_NULL(save_flags_);
    delete save_flags_;
    save_flags_ = nullptr;
  }

 private:
  static SaveFlags* save_flags_;
};

SaveFlags* LazyCompileDispatcherTestFlags::save_flags_ = nullptr;

class LazyCompileDispatcherTest : public TestWithNativeContext {
 public:
  LazyCompileDispatcherTest() = default;
  ~LazyCompileDispatcherTest() override = default;
  LazyCompileDispatcherTest(const LazyCompileDispatcherTest&) = delete;
  LazyCompileDispatcherTest& operator=(const LazyCompileDispatcherTest&) =
      delete;

  static void SetUpTestSuite() {
    LazyCompileDispatcherTestFlags::SetFlagsForTest();
    TestWithNativeContext::SetUpTestSuite();
  }

  static void TearDownTestSuite() {
    TestWithNativeContext::TearDownTestSuite();
    LazyCompileDispatcherTestFlags::RestoreFlags();
  }

  static void EnqueueUnoptimizedCompileJob(LazyCompileDispatcher* dispatcher,
                                           Isolate* isolate,
                                           Handle<SharedFunctionInfo> shared) {
    if (dispatcher->IsEnqueued(shared)) return;
    dispatcher->Enqueue(isolate->main_thread_local_isolate(), shared,
                        test::SourceCharacterStreamForShared(isolate, shared));
  }
};

namespace {

class DeferredPostJob {
 public:
  class DeferredJobHandle final : public JobHandle {
   public:
    explicit DeferredJobHandle(DeferredPostJob* owner) : owner_(owner) {
      owner->deferred_handle_ = this;
    }
    ~DeferredJobHandle() final {
      if (owner_) {
        owner_->deferred_handle_ = nullptr;
      }
    }

    void NotifyConcurrencyIncrease() final {
      DCHECK(!was_cancelled());
      if (real_handle()) {
        real_handle()->NotifyConcurrencyIncrease();
      }
      owner_->NotifyConcurrencyIncrease();
    }
    void Cancel() final {
      set_cancelled();
      if (real_handle()) {
        real_handle()->Cancel();
      }
    }
    void Join() final { UNREACHABLE(); }
    void CancelAndDetach() final { UNREACHABLE(); }
    bool IsActive() final { return real_handle() && real_handle()->IsActive(); }
    bool IsValid() final { return owner_->HandleIsValid(); }

    void ClearOwner() { owner_ = nullptr; }

   private:
    JobHandle* real_handle() { return owner_->real_handle_.get(); }
    bool was_cancelled() { return owner_->was_cancelled_; }
    void set_cancelled() {
      DCHECK(!was_cancelled());
      owner_->was_cancelled_ = true;
    }

    DeferredPostJob* owner_;
  };

  ~DeferredPostJob() {
    if (deferred_handle_) deferred_handle_->ClearOwner();
  }

  std::unique_ptr<JobHandle> CreateJob(TaskPriority priority,
                                       std::unique_ptr<JobTask> job_task) {
    DCHECK_NULL(job_task_);
    job_task_ = std::move(job_task);
    priority_ = priority;
    return std::make_unique<DeferredJobHandle>(this);
  }

  void NotifyConcurrencyIncrease() { do_post_ = true; }

  bool IsPending() { return job_task_ != nullptr; }

  void Clear() { job_task_.reset(); }

  void DoRealPostJob(Platform* platform) {
    if (do_post_)
      real_handle_ = platform->PostJob(priority_, std::move(job_task_));
    else
      real_handle_ = platform->CreateJob(priority_, std::move(job_task_));
    if (was_cancelled_) {
      real_handle_->Cancel();
    }
  }

  void BlockUntilComplete() {
    // Join the handle pointed to by the deferred handle. This invalidates that
    // handle, but LazyCompileDispatcher still wants to be able to cancel the
    // job it posted, so clear the deferred handle to go back to relying on
    // was_cancelled for validity.
    real_handle_->Join();
    real_handle_ = nullptr;
  }

  bool HandleIsValid() {
    return !was_cancelled_ && real_handle_ && real_handle_->IsValid();
  }

 private:
  std::unique_ptr<JobTask> job_task_;
  TaskPriority priority_;

  // Non-owning pointer to the handle returned by PostJob. The handle holds
  // a pointer to this instance, and registers/deregisters itself on
  // constuction/destruction.
  DeferredJobHandle* deferred_handle_ = nullptr;

  std::unique_ptr<JobHandle> real_handle_ = nullptr;
  bool was_cancelled_ = false;
  bool do_post_ = false;
};

class MockPlatform : public v8::Platform {
 public:
  MockPlatform()
      : time_(0.0),
        time_step_(0.0),
        idle_task_(nullptr),
        tracing_controller_(V8::GetCurrentPlatform()->GetTracingController()) {}
  ~MockPlatform() override {
    EXPECT_FALSE(deferred_post_job_.HandleIsValid());
    base::MutexGuard lock(&idle_task_mutex_);
    EXPECT_EQ(idle_task_, nullptr);
  }
  MockPlatform(const MockPlatform&) = delete;
  MockPlatform& operator=(const MockPlatform&) = delete;

  PageAllocator* GetPageAllocator() override { UNIMPLEMENTED(); }

  int NumberOfWorkerThreads() override { return 1; }

  std::shared_ptr<TaskRunner> GetForegroundTaskRunner(
      v8::Isolate* isolate) override {
    return std::make_shared<MockForegroundTaskRunner>(this);
  }

  void PostTaskOnWorkerThreadImpl(TaskPriority priority,
                                  std::unique_ptr<Task> task,
                                  const SourceLocation& location) override {
    UNREACHABLE();
  }

  void PostDelayedTaskOnWorkerThreadImpl(
      TaskPriority priority, std::unique_ptr<Task> task,
      double delay_in_seconds, const SourceLocation& location) override {
    UNREACHABLE();
  }

  bool IdleTasksEnabled(v8::Isolate* isolate) override { return true; }

  std::unique_ptr<JobHandle> CreateJobImpl(
      TaskPriority priority, std::unique_ptr<JobTask> job_task,
      const SourceLocation& location) override {
    return deferred_post_job_.CreateJob(priority, std::move(job_task));
  }

  double MonotonicallyIncreasingTime() override {
    time_ += time_step_;
    return time_;
  }

  double CurrentClockTimeMillis() override {
    return time_ * base::Time::kMillisecondsPerSecond;
  }

  v8::TracingController* GetTracingController() override {
    return tracing_controller_;
  }

  void RunIdleTask(double deadline_in_seconds, double time_step) {
    time_step_ = time_step;
    std::unique_ptr<IdleTask> task;
    {
      base::MutexGuard lock(&idle_task_mutex_);
      task.swap(idle_task_);
    }
    task->Run(deadline_in_seconds);
  }

  bool IdleTaskPending() {
    base::MutexGuard lock(&idle_task_mutex_);
    return idle_task_ != nullptr;
  }

  bool JobTaskPending() { return deferred_post_job_.IsPending(); }

  void RunJobTasksAndBlock(Platform* platform) {
    deferred_post_job_.DoRealPostJob(platform);
    deferred_post_job_.BlockUntilComplete();
  }

  void RunJobTasks(Platform* platform) {
    deferred_post_job_.DoRealPostJob(platform);
  }

  void BlockUntilComplete() { deferred_post_job_.BlockUntilComplete(); }

  void ClearJobs() { deferred_post_job_.Clear(); }

  void ClearIdleTask() {
    base::MutexGuard lock(&idle_task_mutex_);
    CHECK_NOT_NULL(idle_task_);
    idle_task_.reset();
  }

 private:
  class MockForegroundTaskRunner final : public TaskRunner {
   public:
    explicit MockForegroundTaskRunner(MockPlatform* platform)
        : platform_(platform) {}

    void PostTask(std::unique_ptr<v8::Task> task) override { UNREACHABLE(); }

    void PostNonNestableTask(std::unique_ptr<v8::Task> task) override {
      UNREACHABLE();
    }

    void PostDelayedTask(std::unique_ptr<Task> task,
                         double delay_in_seconds) override {
      UNREACHABLE();
    }

    void PostIdleTask(std::unique_ptr<IdleTask> task) override {
      DCHECK(IdleTasksEnabled());
      base::MutexGuard lock(&platform_->idle_task_mutex_);
      ASSERT_TRUE(platform_->idle_task_ == nullptr);
      platform_->idle_task_ = std::move(task);
    }

    bool IdleTasksEnabled() override { return true; }

    bool NonNestableTasksEnabled() const override { return false; }

   private:
    MockPlatform* platform_;
  };

  double time_;
  double time_step_;

  // The posted JobTask.
  DeferredPostJob deferred_post_job_;

  // The posted idle task.
  std::unique_ptr<IdleTask> idle_task_;

  // Protects idle_task_.
  base::Mutex idle_task_mutex_;

  v8::TracingController* tracing_controller_;
};

}  // namespace

TEST_F(LazyCompileDispatcherTest, Construct) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, IsEnqueued) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));

  dispatcher.AbortAll();
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));

  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(platform.JobTaskPending());
}

TEST_F(LazyCompileDispatcherTest, FinishNow) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_TRUE(dispatcher.FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());

  ASSERT_FALSE(platform.IdleTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, CompileAndFinalize) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);
  ASSERT_TRUE(platform.JobTaskPending());

  // Run compile steps.
  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());

  // Since we haven't yet finalized the job, it should be enqueued for
  // finalization and waiting for an idle task.
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_TRUE(platform.IdleTaskPending());
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_FALSE(platform.JobTaskPending());
  ASSERT_FALSE(platform.IdleTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, IdleTaskNoIdleTime) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);

  // Run compile steps.
  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());

  // Job should be ready to finalize.
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 1u);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_TRUE(platform.IdleTaskPending());

  // Grant no idle time and have time advance beyond it in one step.
  platform.RunIdleTask(0.0, 1.0);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_TRUE(platform.IdleTaskPending());

  // Job should be ready to finalize.
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);

  // Now grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.JobTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, IdleTaskSmallIdleTime) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared_1 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_1->is_compiled());
  Handle<SharedFunctionInfo> shared_2 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_2->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_1);
  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_2);

  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);

  // Run compile steps.
  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());

  // Both jobs should be ready to finalize.
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 2u);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared_1, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared_2, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_TRUE(platform.IdleTaskPending());

  // Grant a small anount of idle time and have time advance beyond it in one
  // step.
  platform.RunIdleTask(2.0, 1.0);

  // Only one of the jobs should be finalized.
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 1u);
  if (dispatcher.IsEnqueued(shared_1)) {
    ASSERT_EQ(
        dispatcher.GetJobFor(shared_1, base::MutexGuard(&dispatcher.mutex_))
            ->state,
        LazyCompileDispatcher::Job::State::kReadyToFinalize);
  } else {
    ASSERT_EQ(
        dispatcher.GetJobFor(shared_2, base::MutexGuard(&dispatcher.mutex_))
            ->state,
        LazyCompileDispatcher::Job::State::kReadyToFinalize);
  }
  ASSERT_NE(dispatcher.IsEnqueued(shared_1), dispatcher.IsEnqueued(shared_2));
  ASSERT_NE(shared_1->is_compiled(), shared_2->is_compiled());
  ASSERT_TRUE(platform.IdleTaskPending());

  // Now grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared_1));
  ASSERT_FALSE(dispatcher.IsEnqueued(shared_2));
  ASSERT_TRUE(shared_1->is_compiled());
  ASSERT_TRUE(shared_2->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.JobTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, IdleTaskException) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, 50);

  std::string raw_script("(x) { var a = ");
  for (int i = 0; i < 1000; i++) {
    // Alternate + and - to avoid n-ary operation nodes.
    raw_script += "'x' + 'x' - ";
  }
  raw_script += " 'x'; };";
  test::ScriptResource* script =
      new test::ScriptResource(raw_script.c_str(), strlen(raw_script.c_str()));
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), script);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  // Run compile steps and finalize.
  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(i_isolate()->has_exception());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, FinishNowWithWorkerTask) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);
  ASSERT_NE(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_TRUE(platform.JobTaskPending());

  // This does not block, but races with the FinishNow() call below.
  platform.RunJobTasks(V8::GetCurrentPlatform());

  ASSERT_TRUE(dispatcher.FinishNow(shared));
  // Finishing removes the SFI from the queue.
  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 0u);
  ASSERT_TRUE(shared->is_compiled());
  if (platform.IdleTaskPending()) platform.ClearIdleTask();
  ASSERT_FALSE(platform.JobTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, IdleTaskMultipleJobs) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared_1 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_1->is_compiled());
  Handle<SharedFunctionInfo> shared_2 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_2->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_1);
  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_2);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared_1));
  ASSERT_TRUE(dispatcher.IsEnqueued(shared_2));

  // Run compile steps and finalize.
  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared_1));
  ASSERT_FALSE(dispatcher.IsEnqueued(shared_2));
  ASSERT_TRUE(shared_1->is_compiled());
  ASSERT_TRUE(shared_2->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.JobTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, FinishNowException) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, 50);

  std::string raw_script("(x) { var a = ");
  for (int i = 0; i < 1000; i++) {
    // Alternate + and - to avoid n-ary operation nodes.
    raw_script += "'x' + 'x' - ";
  }
  raw_script += " 'x'; };";
  test::ScriptResource* script =
      new test::ScriptResource(raw_script.c_str(), strlen(raw_script.c_str()));
  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), script);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_FALSE(dispatcher.FinishNow(shared));

  ASSERT_FALSE(dispatcher.IsEnqueued(shared));
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_TRUE(i_isolate()->has_exception());

  i_isolate()->clear_exception();
  ASSERT_FALSE(platform.IdleTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, AbortJobNotStarted) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_FALSE(shared->is_compiled());
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);
  ASSERT_NE(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_TRUE(platform.JobTaskPending());

  dispatcher.AbortJob(shared);

  // Aborting removes the job from the queue.
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, AbortJobAlreadyStarted) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared);

  ASSERT_FALSE(shared->is_compiled());
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);
  ASSERT_NE(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_TRUE(platform.JobTaskPending());

  // Have dispatcher block on the background thread when running the job.
  {
    base::LockGuard<base::Mutex> lock(&dispatcher.mutex_);
    dispatcher.block_for_testing_.SetValue(true);
  }

  // Start background thread and wait until it is about to run the job.
  platform.RunJobTasks(V8::GetCurrentPlatform());
  while (dispatcher.block_for_testing_.Value()) {
  }

  // Now abort while dispatcher is in the middle of running the job.
  dispatcher.AbortJob(shared);

  // Unblock background thread, and wait for job to complete.
  {
    base::LockGuard<base::Mutex> lock(&dispatcher.mutex_);
    dispatcher.semaphore_for_testing_.Signal();
  }
  platform.BlockUntilComplete();

  // Job should have finished running and then been aborted.
  ASSERT_FALSE(shared->is_compiled());
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 1u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 1u);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared, base::MutexGuard(&dispatcher.mutex_))->state,
      LazyCompileDispatcher::Job::State::kAborted);
  ASSERT_FALSE(platform.JobTaskPending());
  ASSERT_TRUE(platform.IdleTaskPending());

  // Runt the pending idle task
  platform.RunIdleTask(1000.0, 0.0);

  // Aborting removes the SFI from the queue.
  ASSERT_FALSE(shared->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.JobTaskPending());
  dispatcher.AbortAll();
}

TEST_F(LazyCompileDispatcherTest, CompileLazyFinishesDispatcherJob) {
  // Use the real dispatcher so that CompileLazy checks the same one for
  // enqueued functions.
  LazyCompileDispatcher* dispatcher = i_isolate()->lazy_compile_dispatcher();

  const char raw_script[] = "function lazy() { return 42; }; lazy;";
  test::ScriptResource* script =
      new test::ScriptResource(raw_script, strlen(raw_script));
  Handle<JSFunction> f = RunJS<JSFunction>(script);
  Handle<SharedFunctionInfo> shared(f->shared(), i_isolate());
  ASSERT_FALSE(shared->is_compiled());

  EnqueueUnoptimizedCompileJob(dispatcher, i_isolate(), shared);

  // Now force the function to run and ensure CompileLazy finished and dequeues
  // it from the dispatcher.
  RunJS("lazy();");
  ASSERT_TRUE(shared->is_compiled());
  ASSERT_FALSE(dispatcher->IsEnqueued(shared));
}

TEST_F(LazyCompileDispatcherTest, CompileLazy2FinishesDispatcherJob) {
  // Use the real dispatcher so that CompileLazy checks the same one for
  // enqueued functions.
  LazyCompileDispatcher* dispatcher = i_isolate()->lazy_compile_dispatcher();

  const char raw_source_2[] = "function lazy2() { return 42; }; lazy2;";
  test::ScriptResource* source_2 =
      new test::ScriptResource(raw_source_2, strlen(raw_source_2));
  Handle<JSFunction> lazy2 = RunJS<JSFunction>(source_2);
  Handle<SharedFunctionInfo> shared_2(lazy2->shared(), i_isolate());
  ASSERT_FALSE(shared_2->is_compiled());

  const char raw_source_1[] = "function lazy1() { return lazy2(); }; lazy1;";
  test::ScriptResource* source_1 =
      new test::ScriptResource(raw_source_1, strlen(raw_source_1));
  Handle<JSFunction> lazy1 = RunJS<JSFunction>(source_1);
  Handle<SharedFunctionInfo> shared_1(lazy1->shared(), i_isolate());
  ASSERT_FALSE(shared_1->is_compiled());

  EnqueueUnoptimizedCompileJob(dispatcher, i_isolate(), shared_1);
  EnqueueUnoptimizedCompileJob(dispatcher, i_isolate(), shared_2);

  ASSERT_TRUE(dispatcher->IsEnqueued(shared_1));
  ASSERT_TRUE(dispatcher->IsEnqueued(shared_2));

  RunJS("lazy1();");
  ASSERT_TRUE(shared_1->is_compiled());
  ASSERT_TRUE(shared_2->is_compiled());
  ASSERT_FALSE(dispatcher->IsEnqueued(shared_1));
  ASSERT_FALSE(dispatcher->IsEnqueued(shared_2));
}

TEST_F(LazyCompileDispatcherTest, CompileMultipleOnBackgroundThread) {
  MockPlatform platform;
  LazyCompileDispatcher dispatcher(i_isolate(), &platform, v8_flags.stack_size);

  Handle<SharedFunctionInfo> shared_1 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_1->is_compiled());

  Handle<SharedFunctionInfo> shared_2 =
      test::CreateSharedFunctionInfo(i_isolate(), nullptr);
  ASSERT_FALSE(shared_2->is_compiled());

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_1);

  EnqueueUnoptimizedCompileJob(&dispatcher, i_isolate(), shared_2);

  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 0u);
  ASSERT_NE(
      dispatcher.GetJobFor(shared_1, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_NE(
      dispatcher.GetJobFor(shared_2, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);

  ASSERT_TRUE(dispatcher.IsEnqueued(shared_1));
  ASSERT_TRUE(dispatcher.IsEnqueued(shared_2));
  ASSERT_FALSE(shared_1->is_compiled());
  ASSERT_FALSE(shared_2->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  ASSERT_TRUE(platform.JobTaskPending());

  platform.RunJobTasksAndBlock(V8::GetCurrentPlatform());

  ASSERT_TRUE(platform.IdleTaskPending());
  ASSERT_FALSE(platform.JobTaskPending());
  DEBUG_ASSERT_EQ(dispatcher.all_jobs_.size(), 2u);
  ASSERT_EQ(dispatcher.pending_background_jobs_.size(), 0u);
  ASSERT_EQ(dispatcher.finalizable_jobs_.size(), 2u);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared_1, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);
  ASSERT_EQ(
      dispatcher.GetJobFor(shared_2, base::MutexGuard(&dispatcher.mutex_))
          ->state,
      LazyCompileDispatcher::Job::State::kReadyToFinalize);

  // Now grant a lot of idle time and freeze time.
  platform.RunIdleTask(1000.0, 0.0);

  ASSERT_FALSE(dispatcher.IsEnqueued(shared_1));
  ASSERT_FALSE(dispatcher.IsEnqueued(shared_2));
  ASSERT_TRUE(shared_1->is_compiled());
  ASSERT_TRUE(shared_2->is_compiled());
  ASSERT_FALSE(platform.IdleTaskPending());
  dispatcher.AbortAll();
}

}  // namespace internal
}  // namespace v8
