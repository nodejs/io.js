// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Concurrent, independent sorts for generating more memory traffic and testing
// scalability when bandwidth-limited. If you want to use multiple threads for
// a single sort, you can use ips4o and integrate vqsort by calling it from
// `baseCaseSort` and increasing `IPS4OML_BASE_CASE_SIZE` to say 8192.

#include <stdint.h>
#include <stdio.h>

#include <condition_variable>  //NOLINT
#include <functional>
#include <mutex>   //NOLINT
#include <thread>  //NOLINT
#include <vector>

#include "hwy/timer.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/sort/bench_parallel.cc"  //NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// After foreach_target
#include "hwy/contrib/sort/algo-inl.h"
#include "hwy/contrib/sort/result-inl.h"
#include "hwy/aligned_allocator.h"
// Last
#include "hwy/tests/test_util-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

class ThreadPool {
 public:
  // Starts the given number of worker threads and blocks until they are ready.
  explicit ThreadPool(
      const size_t num_threads = std::thread::hardware_concurrency())
      : num_threads_(num_threads) {
    HWY_ASSERT(num_threads_ > 0);
    threads_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
      threads_.emplace_back(ThreadFunc, this, i);
    }

    WorkersReadyBarrier();
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator&(const ThreadPool&) = delete;

  // Waits for all threads to exit.
  ~ThreadPool() {
    StartWorkers(kWorkerExit);

    for (std::thread& thread : threads_) {
      thread.join();
    }
  }

  size_t NumThreads() const { return threads_.size(); }

  template <class Func>
  void RunOnThreads(size_t max_threads, const Func& func) {
    task_ = &CallClosure<Func>;
    data_ = &func;
    StartWorkers(max_threads);
    WorkersReadyBarrier();
  }

 private:
  // After construction and between calls to Run, workers are "ready", i.e.
  // waiting on worker_start_cv_. They are "started" by sending a "command"
  // and notifying all worker_start_cv_ waiters. (That is why all workers
  // must be ready/waiting - otherwise, the notification will not reach all of
  // them and the main thread waits in vain for them to report readiness.)
  using WorkerCommand = uint64_t;

  static constexpr WorkerCommand kWorkerWait = ~1ULL;
  static constexpr WorkerCommand kWorkerExit = ~2ULL;

  // Calls a closure (lambda with captures).
  template <class Closure>
  static void CallClosure(const void* f, size_t thread) {
    (*reinterpret_cast<const Closure*>(f))(thread);
  }

  void WorkersReadyBarrier() {
    std::unique_lock<std::mutex> lock(mutex_);
    // Typically only a single iteration.
    while (workers_ready_ != threads_.size()) {
      workers_ready_cv_.wait(lock);
    }
    workers_ready_ = 0;

    // Safely handle spurious worker wakeups.
    worker_start_command_ = kWorkerWait;
  }

  // Precondition: all workers are ready.
  void StartWorkers(const WorkerCommand worker_command) {
    std::unique_lock<std::mutex> lock(mutex_);
    worker_start_command_ = worker_command;
    // Workers will need this lock, so release it before they wake up.
    lock.unlock();
    worker_start_cv_.notify_all();
  }

  static void ThreadFunc(ThreadPool* self, size_t thread) {
    // Until kWorkerExit command received:
    for (;;) {
      std::unique_lock<std::mutex> lock(self->mutex_);
      // Notify main thread that this thread is ready.
      if (++self->workers_ready_ == self->num_threads_) {
        self->workers_ready_cv_.notify_one();
      }
    RESUME_WAIT:
      // Wait for a command.
      self->worker_start_cv_.wait(lock);
      const WorkerCommand command = self->worker_start_command_;
      switch (command) {
        case kWorkerWait:    // spurious wakeup:
          goto RESUME_WAIT;  // lock still held, avoid incrementing ready.
        case kWorkerExit:
          return;  // exits thread
        default:
          break;
      }

      lock.unlock();
      // Command is the maximum number of threads that should run the task.
      HWY_ASSERT(command < self->NumThreads());
      if (thread < command) {
        self->task_(self->data_, thread);
      }
    }
  }

  const size_t num_threads_;

  // Unmodified after ctor, but cannot be const because we call thread::join().
  std::vector<std::thread> threads_;

  std::mutex mutex_;  // guards both cv and their variables.
  std::condition_variable workers_ready_cv_;
  size_t workers_ready_ = 0;
  std::condition_variable worker_start_cv_;
  WorkerCommand worker_start_command_;

  // Written by main thread, read by workers (after mutex lock/unlock).
  std::function<void(const void*, size_t)> task_;  // points to CallClosure
  const void* data_;                               // points to caller's Func
};

template <class Traits>
void RunWithoutVerify(Traits st, const Dist dist, const size_t num_keys,
                      const Algo algo, SharedState& shared, size_t thread) {
  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  using Order = typename Traits::Order;
  const size_t num_lanes = num_keys * st.LanesPerKey();
  auto aligned = hwy::AllocateAligned<LaneType>(num_lanes);

  (void)GenerateInput(dist, aligned.get(), num_lanes);

  const Timestamp t0;
  Run(algo, reinterpret_cast<KeyType*>(aligned.get()), num_keys, shared, thread,
      /*k_keys=*/0, Order());
  HWY_ASSERT(aligned[0] < aligned[num_lanes - 1]);
}

void BenchParallel() {
  // Not interested in benchmark results for other targets on x86
  if (HWY_ARCH_X86 &&
      (HWY_TARGET != HWY_AVX2 && HWY_TARGET != HWY_AVX3 &&
       HWY_TARGET != HWY_AVX3_ZEN4 && HWY_TARGET != HWY_AVX3_SPR)) {
    return;
  }

  ThreadPool pool;
  const size_t NT = pool.NumThreads();

  detail::SharedTraits<detail::TraitsLane<detail::OrderAscending<int64_t>>> st;
  using KeyType = typename decltype(st)::KeyType;
  const size_t num_keys = size_t{100} * 1000 * 1000;

#if HAVE_IPS4O
  const Algo algo = Algo::kIPS4O;
#else
  const Algo algo = Algo::kVQSort;
#endif
  const Dist dist = Dist::kUniform32;

  SharedState shared;

  std::vector<SortResult> results;
  for (size_t nt = 1; nt < NT; nt += HWY_MAX(1, NT / 16)) {
    Timestamp t0;
    // Default capture because MSVC wants algo/dist but clang does not.
    pool.RunOnThreads(nt, [=, &shared](size_t thread) {
      RunWithoutVerify(st, dist, num_keys, algo, shared, thread);
    });
    const double sec = SecondsSince(t0);
    results.emplace_back(algo, dist, num_keys, nt, sec, sizeof(KeyType),
                         st.KeyString());
    results.back().Print();
  }
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace hwy {
namespace {
HWY_BEFORE_TEST(BenchParallel);
HWY_EXPORT_AND_TEST_P(BenchParallel, BenchParallel);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy

#endif  // HWY_ONCE
