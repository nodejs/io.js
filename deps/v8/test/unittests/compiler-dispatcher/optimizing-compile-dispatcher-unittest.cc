// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler-dispatcher/optimizing-compile-dispatcher.h"

#include <memory>

#include "src/api/api-inl.h"
#include "src/base/atomic-utils.h"
#include "src/base/platform/semaphore.h"
#include "src/codegen/compiler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/handles.h"
#include "src/heap/local-heap.h"
#include "src/objects/objects-inl.h"
#include "src/parsing/parse-info.h"
#include "test/unittests/test-helpers.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using OptimizingCompileDispatcherTest = TestWithNativeContext;

namespace {

class BlockingCompilationJob : public TurbofanCompilationJob {
 public:
  BlockingCompilationJob(Isolate* isolate, Handle<JSFunction> function)
      : TurbofanCompilationJob(isolate, &info_, State::kReadyToExecute),
        shared_(function->shared(), isolate),
        zone_(isolate->allocator(), ZONE_NAME),
        info_(&zone_, isolate, shared_, function, CodeKind::TURBOFAN_JS),
        blocking_(false),
        semaphore_(0) {}
  ~BlockingCompilationJob() override = default;
  BlockingCompilationJob(const BlockingCompilationJob&) = delete;
  BlockingCompilationJob& operator=(const BlockingCompilationJob&) = delete;

  bool IsBlocking() const { return blocking_.Value(); }
  void Signal() { semaphore_.Signal(); }

  // OptimiziedCompilationJob implementation.
  Status PrepareJobImpl(Isolate* isolate) override { UNREACHABLE(); }

  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) override {
    blocking_.SetValue(true);
    semaphore_.Wait();
    blocking_.SetValue(false);
    return SUCCEEDED;
  }

  Status FinalizeJobImpl(Isolate* isolate) override { return SUCCEEDED; }

 private:
  Handle<SharedFunctionInfo> shared_;
  Zone zone_;
  OptimizedCompilationInfo info_;
  base::AtomicValue<bool> blocking_;
  base::Semaphore semaphore_;
};

}  // namespace

TEST_F(OptimizingCompileDispatcherTest, Construct) {
  OptimizingCompileDispatcher dispatcher(i_isolate());
  ASSERT_TRUE(OptimizingCompileDispatcher::Enabled());
  ASSERT_TRUE(dispatcher.IsQueueAvailable());
}

TEST_F(OptimizingCompileDispatcherTest, NonBlockingFlush) {
  Handle<JSFunction> fun =
      RunJS<JSFunction>("function f() { function g() {}; return g;}; f();");
  IsCompiledScope is_compiled_scope;
  ASSERT_TRUE(Compiler::Compile(i_isolate(), fun, Compiler::CLEAR_EXCEPTION,
                                &is_compiled_scope));
  BlockingCompilationJob* job = new BlockingCompilationJob(i_isolate(), fun);

  OptimizingCompileDispatcher dispatcher(i_isolate());
  ASSERT_TRUE(OptimizingCompileDispatcher::Enabled());
  ASSERT_TRUE(dispatcher.IsQueueAvailable());
  std::unique_ptr<TurbofanCompilationJob> compilation_job(job);
  ASSERT_TRUE(dispatcher.TryQueueForOptimization(compilation_job));

  // Busy-wait for the job to run on a background thread.
  while (!job->IsBlocking()) {
  }

  // Should not block.
  dispatcher.Flush(BlockingBehavior::kDontBlock);

  // Unblock the job & finish.
  job->Signal();
  dispatcher.StartTearDown();
  dispatcher.FinishTearDown();
}

}  // namespace internal
}  // namespace v8
