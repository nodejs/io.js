// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/libplatform/default-platform.h"
#include "src/base/platform/semaphore.h"
#include "src/base/platform/time.h"
#include "testing/gmock/include/gmock/gmock.h"

using testing::InSequence;
using testing::StrictMock;

namespace v8 {
namespace platform {
namespace default_platform_unittest {

namespace {

struct MockTask : public Task {
  // See issue v8:8185
  ~MockTask() /* override */ { Die(); }
  MOCK_METHOD(void, Run, (), (override));
  MOCK_METHOD(void, Die, ());
};

struct MockIdleTask : public IdleTask {
  // See issue v8:8185
  ~MockIdleTask() /* override */ { Die(); }
  MOCK_METHOD(void, Run, (double deadline_in_seconds), (override));
  MOCK_METHOD(void, Die, ());
};

class DefaultPlatformWithMockTime : public DefaultPlatform {
 public:
  explicit DefaultPlatformWithMockTime(int thread_pool_size = 0)
      : DefaultPlatform(thread_pool_size, IdleTaskSupport::kEnabled, nullptr) {
    mock_time_ = 0.0;
    SetTimeFunctionForTesting([]() { return mock_time_; });
  }
  void IncreaseTime(double seconds) { mock_time_ += seconds; }

 private:
  static double mock_time_;
};

double DefaultPlatformWithMockTime::mock_time_ = 0.0;

template <typename Platform>
class PlatformTest : public ::testing::Test {
 public:
  Isolate* isolate() { return reinterpret_cast<Isolate*>(dummy_); }

  Platform* platform() { return &platform_; }

  std::shared_ptr<TaskRunner> task_runner() {
    if (!task_runner_) {
      task_runner_ = platform_.GetForegroundTaskRunner(
          isolate(), TaskPriority::kUserBlocking);
    }
    DCHECK_NOT_NULL(task_runner_);
    return task_runner_;
  }

  // These methods take ownership of the task. Tests might still reference them,
  // if the tasks are expected to still exist.
  void CallOnForegroundThread(Task* task) {
    task_runner()->PostTask(std::unique_ptr<Task>(task));
  }
  void CallNonNestableOnForegroundThread(Task* task) {
    task_runner()->PostNonNestableTask(std::unique_ptr<Task>(task));
  }
  void CallDelayedOnForegroundThread(Task* task, double delay_in_seconds) {
    task_runner()->PostDelayedTask(std::unique_ptr<Task>(task),
                                   delay_in_seconds);
  }
  void CallIdleOnForegroundThread(IdleTask* task) {
    task_runner()->PostIdleTask(std::unique_ptr<IdleTask>(task));
  }

  bool PumpMessageLoop() { return platform_.PumpMessageLoop(isolate()); }

 private:
  Platform platform_;
  InSequence in_sequence_;
  std::shared_ptr<TaskRunner> task_runner_;

  int dummy_ = 0;
};

class DefaultPlatformTest : public PlatformTest<DefaultPlatform> {};
class DefaultPlatformTestWithMockTime
    : public PlatformTest<DefaultPlatformWithMockTime> {};

}  // namespace

TEST_F(DefaultPlatformTest, PumpMessageLoop) {
  EXPECT_FALSE(platform()->PumpMessageLoop(isolate()));

  StrictMock<MockTask>* task = new StrictMock<MockTask>;
  CallOnForegroundThread(task);
  EXPECT_CALL(*task, Run());
  EXPECT_CALL(*task, Die());
  EXPECT_TRUE(PumpMessageLoop());
  EXPECT_FALSE(PumpMessageLoop());
}

TEST_F(DefaultPlatformTest, PumpMessageLoopWithTaskRunner) {
  std::shared_ptr<TaskRunner> taskrunner = platform()->GetForegroundTaskRunner(
      isolate(), TaskPriority::kUserBlocking);
  EXPECT_FALSE(PumpMessageLoop());

  StrictMock<MockTask>* task = new StrictMock<MockTask>;
  taskrunner->PostTask(std::unique_ptr<Task>(task));
  EXPECT_CALL(*task, Run());
  EXPECT_CALL(*task, Die());
  EXPECT_TRUE(PumpMessageLoop());
  EXPECT_FALSE(PumpMessageLoop());
}

TEST_F(DefaultPlatformTest, PumpMessageLoopNested) {
  EXPECT_FALSE(PumpMessageLoop());

  StrictMock<MockTask>* nestable_task1 = new StrictMock<MockTask>;
  StrictMock<MockTask>* non_nestable_task2 = new StrictMock<MockTask>;
  StrictMock<MockTask>* nestable_task3 = new StrictMock<MockTask>;
  StrictMock<MockTask>* non_nestable_task4 = new StrictMock<MockTask>;
  CallOnForegroundThread(nestable_task1);
  CallNonNestableOnForegroundThread(non_nestable_task2);
  CallOnForegroundThread(nestable_task3);
  CallNonNestableOnForegroundThread(non_nestable_task4);

  // Nestable tasks are FIFO; non-nestable tasks are FIFO. A task being
  // non-nestable may cause it to be executed later, but not earlier.
  EXPECT_CALL(*nestable_task1, Run).WillOnce([this]() {
    EXPECT_TRUE(PumpMessageLoop());
  });
  EXPECT_CALL(*nestable_task3, Run());
  EXPECT_CALL(*nestable_task3, Die());
  EXPECT_CALL(*nestable_task1, Die());
  EXPECT_TRUE(PumpMessageLoop());
  EXPECT_CALL(*non_nestable_task2, Run());
  EXPECT_CALL(*non_nestable_task2, Die());
  EXPECT_TRUE(PumpMessageLoop());
  EXPECT_CALL(*non_nestable_task4, Run());
  EXPECT_CALL(*non_nestable_task4, Die());
  EXPECT_TRUE(PumpMessageLoop());

  EXPECT_FALSE(PumpMessageLoop());
}

TEST_F(DefaultPlatformTestWithMockTime, PumpMessageLoopDelayed) {
  EXPECT_FALSE(PumpMessageLoop());

  StrictMock<MockTask>* task1 = new StrictMock<MockTask>;
  StrictMock<MockTask>* task2 = new StrictMock<MockTask>;
  CallDelayedOnForegroundThread(task2, 100);
  CallDelayedOnForegroundThread(task1, 10);

  EXPECT_FALSE(PumpMessageLoop());

  platform()->IncreaseTime(11);
  EXPECT_CALL(*task1, Run());
  EXPECT_CALL(*task1, Die());
  EXPECT_TRUE(PumpMessageLoop());

  EXPECT_FALSE(PumpMessageLoop());

  platform()->IncreaseTime(90);
  EXPECT_CALL(*task2, Run());
  EXPECT_CALL(*task2, Die());
  EXPECT_TRUE(PumpMessageLoop());
}

TEST_F(DefaultPlatformTestWithMockTime, PumpMessageLoopNoStarvation) {
  EXPECT_FALSE(PumpMessageLoop());

  StrictMock<MockTask>* task1 = new StrictMock<MockTask>;
  StrictMock<MockTask>* task2 = new StrictMock<MockTask>;
  StrictMock<MockTask>* task3 = new StrictMock<MockTask>;
  CallOnForegroundThread(task1);
  CallDelayedOnForegroundThread(task2, 10);
  platform()->IncreaseTime(11);

  EXPECT_CALL(*task1, Run());
  EXPECT_CALL(*task1, Die());
  EXPECT_TRUE(PumpMessageLoop());

  CallOnForegroundThread(task3);

  EXPECT_CALL(*task2, Run());
  EXPECT_CALL(*task2, Die());
  EXPECT_TRUE(PumpMessageLoop());
  EXPECT_CALL(*task3, Run());
  EXPECT_CALL(*task3, Die());
  EXPECT_TRUE(PumpMessageLoop());
}

TEST_F(DefaultPlatformTestWithMockTime,
       PendingDelayedTasksAreDestroyedOnShutdown) {
  StrictMock<MockTask>* task = new StrictMock<MockTask>;
  CallDelayedOnForegroundThread(task, 10);
  EXPECT_CALL(*task, Die());
}

TEST_F(DefaultPlatformTestWithMockTime, RunIdleTasks) {
  StrictMock<MockIdleTask>* task = new StrictMock<MockIdleTask>;
  CallIdleOnForegroundThread(task);
  EXPECT_CALL(*task, Run(42.0 + 23.0));
  EXPECT_CALL(*task, Die());
  platform()->IncreaseTime(23.0);
  platform()->RunIdleTasks(isolate(), 42.0);
}

TEST_F(DefaultPlatformTestWithMockTime,
       PendingIdleTasksAreDestroyedOnShutdown) {
  StrictMock<MockIdleTask>* task = new StrictMock<MockIdleTask>;
  CallIdleOnForegroundThread(task);
  EXPECT_CALL(*task, Die());
}

namespace {

class TestBackgroundTask : public Task {
 public:
  explicit TestBackgroundTask(base::Semaphore* sem, bool* executed)
      : sem_(sem), executed_(executed) {}

  ~TestBackgroundTask() override { Die(); }
  MOCK_METHOD(void, Die, ());

  void Run() override {
    *executed_ = true;
    sem_->Signal();
  }

 private:
  base::Semaphore* sem_;
  bool* executed_;
};

}  // namespace

TEST(CustomDefaultPlatformTest, RunBackgroundTask) {
  DefaultPlatform platform(1);

  base::Semaphore sem(0);
  bool task_executed = false;
  StrictMock<TestBackgroundTask>* task =
      new StrictMock<TestBackgroundTask>(&sem, &task_executed);
  EXPECT_CALL(*task, Die());
  platform.PostTaskOnWorkerThread(TaskPriority::kUserVisible,
                                  std::unique_ptr<Task>(task));
  EXPECT_TRUE(sem.WaitFor(base::TimeDelta::FromSeconds(1)));
  EXPECT_TRUE(task_executed);
}

TEST(CustomDefaultPlatformTest, PostForegroundTaskAfterPlatformTermination) {
  std::shared_ptr<TaskRunner> foreground_taskrunner;
  {
    DefaultPlatformWithMockTime platform(1);

    int dummy;
    Isolate* isolate = reinterpret_cast<Isolate*>(&dummy);

    foreground_taskrunner =
        platform.GetForegroundTaskRunner(isolate, TaskPriority::kUserBlocking);
  }
  // It should still be possible to post foreground tasks, even when the
  // platform does not exist anymore.
  StrictMock<MockTask>* task1 = new StrictMock<MockTask>;
  EXPECT_CALL(*task1, Die());
  foreground_taskrunner->PostTask(std::unique_ptr<Task>(task1));

  StrictMock<MockTask>* task2 = new StrictMock<MockTask>;
  EXPECT_CALL(*task2, Die());
  foreground_taskrunner->PostDelayedTask(std::unique_ptr<Task>(task2), 10);

  StrictMock<MockIdleTask>* task3 = new StrictMock<MockIdleTask>;
  EXPECT_CALL(*task3, Die());
  foreground_taskrunner->PostIdleTask(std::unique_ptr<IdleTask>(task3));
}

}  // namespace default_platform_unittest
}  // namespace platform
}  // namespace v8
