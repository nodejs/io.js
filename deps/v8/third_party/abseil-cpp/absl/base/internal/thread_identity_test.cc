// Copyright 2017 The Abseil Authors.
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

#include "absl/base/internal/thread_identity.h"

#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/macros.h"
#include "absl/base/thread_annotations.h"
#include "absl/synchronization/internal/per_thread_sem.h"
#include "absl/synchronization/mutex.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace base_internal {
namespace {

ABSL_CONST_INIT static absl::base_internal::SpinLock map_lock(
    absl::kConstInit, base_internal::SCHEDULE_KERNEL_ONLY);
ABSL_CONST_INIT static int num_identities_reused ABSL_GUARDED_BY(map_lock);

static const void* const kCheckNoIdentity = reinterpret_cast<void*>(1);

static void TestThreadIdentityCurrent(const void* assert_no_identity) {
  ThreadIdentity* identity;

  // We have to test this conditionally, because if the test framework relies
  // on Abseil, then some previous action may have already allocated an
  // identity.
  if (assert_no_identity == kCheckNoIdentity) {
    identity = CurrentThreadIdentityIfPresent();
    EXPECT_TRUE(identity == nullptr);
  }

  identity = synchronization_internal::GetOrCreateCurrentThreadIdentity();
  EXPECT_TRUE(identity != nullptr);
  ThreadIdentity* identity_no_init;
  identity_no_init = CurrentThreadIdentityIfPresent();
  EXPECT_TRUE(identity == identity_no_init);

  // Check that per_thread_synch is correctly aligned.
  EXPECT_EQ(0, reinterpret_cast<intptr_t>(&identity->per_thread_synch) %
                   PerThreadSynch::kAlignment);
  EXPECT_EQ(identity, identity->per_thread_synch.thread_identity());

  absl::base_internal::SpinLockHolder l(&map_lock);
  num_identities_reused++;
}

TEST(ThreadIdentityTest, BasicIdentityWorks) {
  // This tests for the main() thread.
  TestThreadIdentityCurrent(nullptr);
}

TEST(ThreadIdentityTest, BasicIdentityWorksThreaded) {
  // Now try the same basic test with multiple threads being created and
  // destroyed.  This makes sure that:
  // - New threads are created without a ThreadIdentity.
  // - We re-allocate ThreadIdentity objects from the free-list.
  // - If a thread implementation chooses to recycle threads, that
  //   correct re-initialization occurs.
  static const int kNumLoops = 3;
  static const int kNumThreads = 32;
  for (int iter = 0; iter < kNumLoops; iter++) {
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; ++i) {
      threads.push_back(
          std::thread(TestThreadIdentityCurrent, kCheckNoIdentity));
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }

  // We should have recycled ThreadIdentity objects above; while (external)
  // library threads allocating their own identities may preclude some
  // reuse, we should have sufficient repetitions to exclude this.
  absl::base_internal::SpinLockHolder l(&map_lock);
  EXPECT_LT(kNumThreads, num_identities_reused);
}

TEST(ThreadIdentityTest, ReusedThreadIdentityMutexTest) {
  // This test repeatedly creates and joins a series of threads, each of
  // which acquires and releases shared Mutex locks. This verifies
  // Mutex operations work correctly under a reused
  // ThreadIdentity. Note that the most likely failure mode of this
  // test is a crash or deadlock.
  static const int kNumLoops = 10;
  static const int kNumThreads = 12;
  static const int kNumMutexes = 3;
  static const int kNumLockLoops = 5;

  Mutex mutexes[kNumMutexes];
  for (int iter = 0; iter < kNumLoops; ++iter) {
    std::vector<std::thread> threads;
    for (int thread = 0; thread < kNumThreads; ++thread) {
      threads.push_back(std::thread([&]() {
        for (int l = 0; l < kNumLockLoops; ++l) {
          for (int m = 0; m < kNumMutexes; ++m) {
            MutexLock lock(&mutexes[m]);
          }
        }
      }));
    }
    for (auto& thread : threads) {
      thread.join();
    }
  }
}

}  // namespace
}  // namespace base_internal
ABSL_NAMESPACE_END
}  // namespace absl
