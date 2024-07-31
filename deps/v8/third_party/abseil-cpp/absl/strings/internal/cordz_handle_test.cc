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

#include <random>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/memory/memory.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::ElementsAre;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::SizeIs;

// Local less verbose helper
std::vector<const CordzHandle*> DeleteQueue() {
  return CordzHandle::DiagnosticsGetDeleteQueue();
}

struct CordzHandleDeleteTracker : public CordzHandle {
  bool* deleted;
  explicit CordzHandleDeleteTracker(bool* deleted) : deleted(deleted) {}
  ~CordzHandleDeleteTracker() override { *deleted = true; }
};

TEST(CordzHandleTest, DeleteQueueIsEmpty) {
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
}

TEST(CordzHandleTest, CordzHandleCreateDelete) {
  bool deleted = false;
  auto* handle = new CordzHandleDeleteTracker(&deleted);
  EXPECT_FALSE(handle->is_snapshot());
  EXPECT_TRUE(handle->SafeToDelete());
  EXPECT_THAT(DeleteQueue(), SizeIs(0));

  CordzHandle::Delete(handle);
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_TRUE(deleted);
}

TEST(CordzHandleTest, CordzSnapshotCreateDelete) {
  auto* snapshot = new CordzSnapshot();
  EXPECT_TRUE(snapshot->is_snapshot());
  EXPECT_TRUE(snapshot->SafeToDelete());
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot));
  delete snapshot;
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
}

TEST(CordzHandleTest, CordzHandleCreateDeleteWithSnapshot) {
  bool deleted = false;
  auto* snapshot = new CordzSnapshot();
  auto* handle = new CordzHandleDeleteTracker(&deleted);
  EXPECT_FALSE(handle->SafeToDelete());

  CordzHandle::Delete(handle);
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle, snapshot));
  EXPECT_FALSE(deleted);
  EXPECT_FALSE(handle->SafeToDelete());

  delete snapshot;
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_TRUE(deleted);
}

TEST(CordzHandleTest, MultiSnapshot) {
  bool deleted[3] = {false, false, false};

  CordzSnapshot* snapshot[3];
  CordzHandleDeleteTracker* handle[3];
  for (int i = 0; i < 3; ++i) {
    snapshot[i] = new CordzSnapshot();
    handle[i] = new CordzHandleDeleteTracker(&deleted[i]);
    CordzHandle::Delete(handle[i]);
  }

  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2], handle[1],
                                         snapshot[1], handle[0], snapshot[0]));
  EXPECT_THAT(deleted, ElementsAre(false, false, false));

  delete snapshot[1];
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2], handle[1],
                                         handle[0], snapshot[0]));
  EXPECT_THAT(deleted, ElementsAre(false, false, false));

  delete snapshot[0];
  EXPECT_THAT(DeleteQueue(), ElementsAre(handle[2], snapshot[2]));
  EXPECT_THAT(deleted, ElementsAre(true, true, false));

  delete snapshot[2];
  EXPECT_THAT(DeleteQueue(), SizeIs(0));
  EXPECT_THAT(deleted, ElementsAre(true, true, deleted));
}

TEST(CordzHandleTest, DiagnosticsHandleIsSafeToInspect) {
  CordzSnapshot snapshot1;
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(nullptr));

  auto* handle1 = new CordzHandle();
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));

  CordzHandle::Delete(handle1);
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));

  CordzSnapshot snapshot2;
  auto* handle2 = new CordzHandle();
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle2));
  EXPECT_FALSE(snapshot2.DiagnosticsHandleIsSafeToInspect(handle1));
  EXPECT_TRUE(snapshot2.DiagnosticsHandleIsSafeToInspect(handle2));

  CordzHandle::Delete(handle2);
  EXPECT_TRUE(snapshot1.DiagnosticsHandleIsSafeToInspect(handle1));
}

TEST(CordzHandleTest, DiagnosticsGetSafeToInspectDeletedHandles) {
  EXPECT_THAT(DeleteQueue(), IsEmpty());

  auto* handle = new CordzHandle();
  auto* snapshot1 = new CordzSnapshot();

  // snapshot1 should be able to see handle.
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot1));
  EXPECT_TRUE(snapshot1->DiagnosticsHandleIsSafeToInspect(handle));
  EXPECT_THAT(snapshot1->DiagnosticsGetSafeToInspectDeletedHandles(),
              IsEmpty());

  // This handle will be safe to inspect as long as snapshot1 is alive. However,
  // since only snapshot1 can prove that it's alive, it will be hidden from
  // snapshot2.
  CordzHandle::Delete(handle);

  // This snapshot shouldn't be able to see handle because handle was already
  // sent to Delete.
  auto* snapshot2 = new CordzSnapshot();

  // DeleteQueue elements are LIFO order.
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot2, handle, snapshot1));

  EXPECT_TRUE(snapshot1->DiagnosticsHandleIsSafeToInspect(handle));
  EXPECT_FALSE(snapshot2->DiagnosticsHandleIsSafeToInspect(handle));

  EXPECT_THAT(snapshot1->DiagnosticsGetSafeToInspectDeletedHandles(),
              ElementsAre(handle));
  EXPECT_THAT(snapshot2->DiagnosticsGetSafeToInspectDeletedHandles(),
              IsEmpty());

  CordzHandle::Delete(snapshot1);
  EXPECT_THAT(DeleteQueue(), ElementsAre(snapshot2));

  CordzHandle::Delete(snapshot2);
  EXPECT_THAT(DeleteQueue(), IsEmpty());
}

// Create and delete CordzHandle and CordzSnapshot objects in multiple threads
// so that tsan has some time to chew on it and look for memory problems.
TEST(CordzHandleTest, MultiThreaded) {
  Notification stop;
  static constexpr int kNumThreads = 4;
  // Keep the number of handles relatively small so that the test will naturally
  // transition to an empty delete queue during the test. If there are, say, 100
  // handles, that will virtually never happen. With 10 handles and around 50k
  // iterations in each of 4 threads, the delete queue appears to become empty
  // around 200 times.
  static constexpr int kNumHandles = 10;

  // Each thread is going to pick a random index and atomically swap its
  // CordzHandle with one in handles. This way, each thread can avoid
  // manipulating a CordzHandle that might be operated upon in another thread.
  std::vector<std::atomic<CordzHandle*>> handles(kNumHandles);

  // global bool which is set when any thread did get some 'safe to inspect'
  // handles. On some platforms and OSS tests, we might risk that some pool
  // threads are starved, stalled, or just got a few unlikely random 'handle'
  // coin tosses, so we satisfy this test with simply observing 'some' thread
  // did something meaningful, which should minimize the potential for flakes.
  std::atomic<bool> found_safe_to_inspect(false);

  {
    absl::synchronization_internal::ThreadPool pool(kNumThreads);
    for (int i = 0; i < kNumThreads; ++i) {
      pool.Schedule([&stop, &handles, &found_safe_to_inspect]() {
        std::minstd_rand gen;
        std::uniform_int_distribution<int> dist_type(0, 2);
        std::uniform_int_distribution<int> dist_handle(0, kNumHandles - 1);

        while (!stop.HasBeenNotified()) {
          CordzHandle* handle;
          switch (dist_type(gen)) {
            case 0:
              handle = new CordzHandle();
              break;
            case 1:
              handle = new CordzSnapshot();
              break;
            default:
              handle = nullptr;
              break;
          }
          CordzHandle* old_handle = handles[dist_handle(gen)].exchange(handle);
          if (old_handle != nullptr) {
            std::vector<const CordzHandle*> safe_to_inspect =
                old_handle->DiagnosticsGetSafeToInspectDeletedHandles();
            for (const CordzHandle* handle : safe_to_inspect) {
              // We're in a tight loop, so don't generate too many error
              // messages.
              ASSERT_FALSE(handle->is_snapshot());
            }
            if (!safe_to_inspect.empty()) {
              found_safe_to_inspect.store(true);
            }
            CordzHandle::Delete(old_handle);
          }
        }

        // Have each thread attempt to clean up everything. Some thread will be
        // the last to reach this cleanup code, and it will be guaranteed to
        // clean up everything because nothing remains to create new handles.
        for (auto& h : handles) {
          if (CordzHandle* handle = h.exchange(nullptr)) {
            CordzHandle::Delete(handle);
          }
        }
      });
    }

    // The threads will hammer away.  Give it a little bit of time for tsan to
    // spot errors.
    absl::SleepFor(absl::Seconds(3));
    stop.Notify();
  }

  // Confirm that the test did *something*. This check will be satisfied as
  // long as any thread has deleted a CordzSnapshot object and a non-snapshot
  // CordzHandle was deleted after the CordzSnapshot was created.
  // See also comments on `found_safe_to_inspect`
  EXPECT_TRUE(found_safe_to_inspect.load());
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
