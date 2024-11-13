// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/gc-invoker.h"

#include <optional>

#include "include/cppgc/platform.h"
#include "src/heap/cppgc/heap.h"
#include "test/unittests/heap/cppgc/test-platform.h"
#include "testing/gmock/include/gmock/gmock-matchers.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc::internal {

namespace {

class MockGarbageCollector : public GarbageCollector {
 public:
  MOCK_METHOD(void, CollectGarbage, (GCConfig), (override));
  MOCK_METHOD(void, StartIncrementalGarbageCollection, (GCConfig), (override));
  MOCK_METHOD(size_t, epoch, (), (const, override));
  MOCK_METHOD(std::optional<EmbedderStackState>, overridden_stack_state, (),
              (const, override));
  MOCK_METHOD(void, set_override_stack_state, (EmbedderStackState), (override));
  MOCK_METHOD(void, clear_overridden_stack_state, (), (override));
#ifdef V8_ENABLE_ALLOCATION_TIMEOUT
  MOCK_METHOD(std::optional<int>, UpdateAllocationTimeout, (), (override));
#endif  // V8_ENABLE_ALLOCATION_TIMEOUT
};

class MockTaskRunner : public cppgc::TaskRunner {
 public:
  MOCK_METHOD(void, PostTaskImpl,
              (std::unique_ptr<cppgc::Task>, const SourceLocation&),
              (override));
  MOCK_METHOD(void, PostNonNestableTaskImpl,
              (std::unique_ptr<cppgc::Task>, const SourceLocation&),
              (override));
  MOCK_METHOD(void, PostDelayedTaskImpl,
              (std::unique_ptr<cppgc::Task>, double, const SourceLocation&),
              (override));
  MOCK_METHOD(void, PostNonNestableDelayedTaskImpl,
              (std::unique_ptr<cppgc::Task>, double, const SourceLocation&),
              (override));
  MOCK_METHOD(void, PostIdleTaskImpl,
              (std::unique_ptr<cppgc::IdleTask>, const SourceLocation&),
              (override));

  bool IdleTasksEnabled() override { return true; }
  bool NonNestableTasksEnabled() const override { return true; }
  bool NonNestableDelayedTasksEnabled() const override { return true; }
};

class MockPlatform : public cppgc::Platform {
 public:
  explicit MockPlatform(std::shared_ptr<TaskRunner> runner)
      : runner_(std::move(runner)),
        tracing_controller_(std::make_unique<TracingController>()) {}

  PageAllocator* GetPageAllocator() override { return nullptr; }
  double MonotonicallyIncreasingTime() override { return 0.0; }

  std::shared_ptr<TaskRunner> GetForegroundTaskRunner() override {
    return runner_;
  }

  TracingController* GetTracingController() override {
    return tracing_controller_.get();
  }

 private:
  std::shared_ptr<TaskRunner> runner_;
  std::unique_ptr<TracingController> tracing_controller_;
};

}  // namespace

TEST(GCInvokerTest, PrecideGCIsInvokedSynchronously) {
  MockPlatform platform(nullptr);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc, CollectGarbage(::testing::Field(
                      &GCConfig::stack_state, StackState::kNoHeapPointers)));
  invoker.CollectGarbage(GCConfig::PreciseAtomicConfig());
}

TEST(GCInvokerTest, ConservativeGCIsInvokedSynchronouslyWhenSupported) {
  MockPlatform platform(nullptr);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kSupportsConservativeStackScan);
  EXPECT_CALL(
      gc, CollectGarbage(::testing::Field(
              &GCConfig::stack_state, StackState::kMayContainHeapPointers)));
  invoker.CollectGarbage(GCConfig::ConservativeAtomicConfig());
}

TEST(GCInvokerTest, ConservativeGCIsScheduledAsPreciseGCViaPlatform) {
  std::shared_ptr<cppgc::TaskRunner> runner =
      std::shared_ptr<cppgc::TaskRunner>(new MockTaskRunner());
  MockPlatform platform(runner);
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc, epoch).WillOnce(::testing::Return(0));
  EXPECT_CALL(*static_cast<MockTaskRunner*>(runner.get()),
              PostNonNestableTaskImpl(::testing::_, ::testing::_));
  invoker.CollectGarbage(GCConfig::ConservativeAtomicConfig());
}

TEST(GCInvokerTest, ConservativeGCIsInvokedAsPreciseGCViaPlatform) {
  testing::TestPlatform platform;
  MockGarbageCollector gc;
  GCInvoker invoker(&gc, &platform,
                    cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc, epoch).WillRepeatedly(::testing::Return(0));
  EXPECT_CALL(gc, CollectGarbage);
  invoker.CollectGarbage(GCConfig::ConservativeAtomicConfig());
  platform.RunAllForegroundTasks();
}

TEST(GCInvokerTest, IncrementalGCIsStarted) {
  // Since StartIncrementalGarbageCollection doesn't scan the stack, support for
  // conservative stack scanning should not matter.
  MockPlatform platform(nullptr);
  MockGarbageCollector gc;
  // Conservative stack scanning supported.
  GCInvoker invoker_with_support(
      &gc, &platform,
      cppgc::Heap::StackSupport::kSupportsConservativeStackScan);
  EXPECT_CALL(
      gc, StartIncrementalGarbageCollection(::testing::Field(
              &GCConfig::stack_state, StackState::kMayContainHeapPointers)));
  invoker_with_support.StartIncrementalGarbageCollection(
      GCConfig::ConservativeIncrementalConfig());
  // Conservative stack scanning *not* supported.
  GCInvoker invoker_without_support(
      &gc, &platform, cppgc::Heap::StackSupport::kNoConservativeStackScan);
  EXPECT_CALL(gc,
              StartIncrementalGarbageCollection(::testing::Field(
                  &GCConfig::stack_state, StackState::kMayContainHeapPointers)))
      .Times(0);
  invoker_without_support.StartIncrementalGarbageCollection(
      GCConfig::ConservativeIncrementalConfig());
}

}  // namespace cppgc::internal
