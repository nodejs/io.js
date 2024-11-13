// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/local-heap.h"

#include <optional>

#include "src/base/platform/condition-variable.h"
#include "src/base/platform/mutex.h"
#include "src/heap/heap.h"
#include "src/heap/parked-scope.h"
#include "src/heap/safepoint.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using LocalHeapTest = TestWithIsolate;

TEST_F(LocalHeapTest, Initialize) {
  Heap* heap = i_isolate()->heap();
  heap->safepoint()->AssertMainThreadIsOnlyThread();
}

TEST_F(LocalHeapTest, Current) {
  Heap* heap = i_isolate()->heap();

  CHECK_NULL(LocalHeap::Current());

  {
    LocalHeap lh(heap, ThreadKind::kMain);
    lh.SetUpMainThreadForTesting();
    CHECK_NULL(LocalHeap::Current());
  }

  CHECK_NULL(LocalHeap::Current());

  {
    LocalHeap lh(heap, ThreadKind::kMain);
    lh.SetUpMainThreadForTesting();
    CHECK_NULL(LocalHeap::Current());
  }

  CHECK_NULL(LocalHeap::Current());
}

namespace {
class BackgroundThread final : public v8::base::Thread {
 public:
  explicit BackgroundThread(Heap* heap)
      : v8::base::Thread(base::Thread::Options("BackgroundThread")),
        heap_(heap) {}

  void Run() override {
    CHECK_NULL(LocalHeap::Current());
    {
      LocalHeap lh(heap_, ThreadKind::kBackground);
      CHECK_EQ(&lh, LocalHeap::Current());
    }
    CHECK_NULL(LocalHeap::Current());
  }

  Heap* heap_;
};
}  // anonymous namespace

TEST_F(LocalHeapTest, CurrentBackground) {
  Heap* heap = i_isolate()->heap();
  CHECK_NULL(LocalHeap::Current());
  {
    LocalHeap lh(heap, ThreadKind::kMain);
    lh.SetUpMainThreadForTesting();
    auto thread = std::make_unique<BackgroundThread>(heap);
    CHECK(thread->Start());
    CHECK_NULL(LocalHeap::Current());
    thread->Join();
    CHECK_NULL(LocalHeap::Current());
  }
  CHECK_NULL(LocalHeap::Current());
}

namespace {

class GCEpilogue {
 public:
  static void Callback(void* data) {
    reinterpret_cast<GCEpilogue*>(data)->was_invoked_ = true;
  }

  void NotifyStarted() {
    base::LockGuard<base::Mutex> lock_guard(&mutex_);
    started_ = true;
    cv_.NotifyOne();
  }

  void WaitUntilStarted() {
    base::LockGuard<base::Mutex> lock_guard(&mutex_);
    while (!started_) {
      cv_.Wait(&mutex_);
    }
  }
  void RequestStop() {
    base::LockGuard<base::Mutex> lock_guard(&mutex_);
    stop_requested_ = true;
  }

  bool StopRequested() {
    base::LockGuard<base::Mutex> lock_guard(&mutex_);
    return stop_requested_;
  }

  bool WasInvoked() { return was_invoked_; }

 private:
  bool was_invoked_ = false;
  bool started_ = false;
  bool stop_requested_ = false;
  base::Mutex mutex_;
  base::ConditionVariable cv_;
};

class BackgroundThreadForGCEpilogue final : public v8::base::Thread {
 public:
  explicit BackgroundThreadForGCEpilogue(Heap* heap, bool parked,
                                         GCEpilogue* epilogue)
      : v8::base::Thread(base::Thread::Options("BackgroundThread")),
        heap_(heap),
        parked_(parked),
        epilogue_(epilogue) {}

  void Run() override {
    LocalHeap lh(heap_, ThreadKind::kBackground);
    std::optional<UnparkedScope> unparked_scope;
    if (!parked_) {
      unparked_scope.emplace(&lh);
    }
    {
      std::optional<UnparkedScope> nested_unparked_scope;
      if (parked_) nested_unparked_scope.emplace(&lh);
      lh.AddGCEpilogueCallback(&GCEpilogue::Callback, epilogue_);
    }
    epilogue_->NotifyStarted();
    while (!epilogue_->StopRequested()) {
      lh.Safepoint();
    }
    {
      std::optional<UnparkedScope> nested_unparked_scope;
      if (parked_) nested_unparked_scope.emplace(&lh);
      lh.RemoveGCEpilogueCallback(&GCEpilogue::Callback, epilogue_);
    }
  }

  Heap* heap_;
  bool parked_;
  GCEpilogue* epilogue_;
};

}  // anonymous namespace

TEST_F(LocalHeapTest, GCEpilogue) {
  Heap* heap = i_isolate()->heap();
  LocalHeap* lh = heap->main_thread_local_heap();
  std::array<GCEpilogue, 3> epilogue;
  lh->AddGCEpilogueCallback(&GCEpilogue::Callback, &epilogue[0]);
  auto thread1 =
      std::make_unique<BackgroundThreadForGCEpilogue>(heap, true, &epilogue[1]);
  auto thread2 = std::make_unique<BackgroundThreadForGCEpilogue>(heap, false,
                                                                 &epilogue[2]);
  CHECK(thread1->Start());
  CHECK(thread2->Start());
  epilogue[1].WaitUntilStarted();
  epilogue[2].WaitUntilStarted();
  InvokeAtomicMajorGC(i_isolate());
  epilogue[1].RequestStop();
  epilogue[2].RequestStop();
  thread1->Join();
  thread2->Join();
  lh->RemoveGCEpilogueCallback(&GCEpilogue::Callback, &epilogue[0]);
  for (auto& e : epilogue) {
    CHECK(e.WasInvoked());
  }
}

}  // namespace internal
}  // namespace v8
