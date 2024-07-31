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

#include <cstdint>
#include <mutex>  // NOLINT(build/c++11)
#include <vector>

#include "absl/base/config.h"
#include "absl/base/internal/cycleclock.h"
#include "absl/base/internal/spinlock.h"
#include "absl/base/no_destructor.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "absl/synchronization/mutex.h"
#include "benchmark/benchmark.h"

namespace {

void BM_Mutex(benchmark::State& state) {
  static absl::NoDestructor<absl::Mutex> mu;
  for (auto _ : state) {
    absl::MutexLock lock(mu.get());
  }
}
BENCHMARK(BM_Mutex)->UseRealTime()->Threads(1)->ThreadPerCpu();

void BM_ReaderLock(benchmark::State& state) {
  static absl::NoDestructor<absl::Mutex> mu;
  for (auto _ : state) {
    absl::ReaderMutexLock lock(mu.get());
  }
}
BENCHMARK(BM_ReaderLock)->UseRealTime()->Threads(1)->ThreadPerCpu();

void BM_TryLock(benchmark::State& state) {
  absl::Mutex mu;
  for (auto _ : state) {
    if (mu.TryLock()) {
      mu.Unlock();
    }
  }
}
BENCHMARK(BM_TryLock);

void BM_ReaderTryLock(benchmark::State& state) {
  static absl::NoDestructor<absl::Mutex> mu;
  for (auto _ : state) {
    if (mu->ReaderTryLock()) {
      mu->ReaderUnlock();
    }
  }
}
BENCHMARK(BM_ReaderTryLock)->UseRealTime()->Threads(1)->ThreadPerCpu();

static void DelayNs(int64_t ns, int* data) {
  int64_t end = absl::base_internal::CycleClock::Now() +
                ns * absl::base_internal::CycleClock::Frequency() / 1e9;
  while (absl::base_internal::CycleClock::Now() < end) {
    ++(*data);
    benchmark::DoNotOptimize(*data);
  }
}

template <typename MutexType>
class RaiiLocker {
 public:
  explicit RaiiLocker(MutexType* mu) : mu_(mu) { mu_->Lock(); }
  ~RaiiLocker() { mu_->Unlock(); }
 private:
  MutexType* mu_;
};

template <>
class RaiiLocker<std::mutex> {
 public:
  explicit RaiiLocker(std::mutex* mu) : mu_(mu) { mu_->lock(); }
  ~RaiiLocker() { mu_->unlock(); }
 private:
  std::mutex* mu_;
};

// RAII object to change the Mutex priority of the running thread.
class ScopedThreadMutexPriority {
 public:
  explicit ScopedThreadMutexPriority(int priority) {
    absl::base_internal::ThreadIdentity* identity =
        absl::synchronization_internal::GetOrCreateCurrentThreadIdentity();
    identity->per_thread_synch.priority = priority;
    // Bump next_priority_read_cycles to the infinite future so that the
    // implementation doesn't re-read the thread's actual scheduler priority
    // and replace our temporary scoped priority.
    identity->per_thread_synch.next_priority_read_cycles =
        std::numeric_limits<int64_t>::max();
  }
  ~ScopedThreadMutexPriority() {
    // Reset the "next priority read time" back to the infinite past so that
    // the next time the Mutex implementation wants to know this thread's
    // priority, it re-reads it from the OS instead of using our overridden
    // priority.
    absl::synchronization_internal::GetOrCreateCurrentThreadIdentity()
        ->per_thread_synch.next_priority_read_cycles =
        std::numeric_limits<int64_t>::min();
  }
};

void BM_MutexEnqueue(benchmark::State& state) {
  // In the "multiple priorities" variant of the benchmark, one of the
  // threads runs with Mutex priority 0 while the rest run at elevated priority.
  // This benchmarks the performance impact of the presence of a low priority
  // waiter when a higher priority waiter adds itself of the queue
  // (b/175224064).
  //
  // NOTE: The actual scheduler priority is not modified in this benchmark:
  // all of the threads get CPU slices with the same priority. Only the
  // Mutex queueing behavior is modified.
  const bool multiple_priorities = state.range(0);
  ScopedThreadMutexPriority priority_setter(
      (multiple_priorities && state.thread_index() != 0) ? 1 : 0);

  struct Shared {
    absl::Mutex mu;
    std::atomic<int> looping_threads{0};
    std::atomic<int> blocked_threads{0};
    std::atomic<bool> thread_has_mutex{false};
  };
  static absl::NoDestructor<Shared> shared;

  // Set up 'blocked_threads' to count how many threads are currently blocked
  // in Abseil synchronization code.
  //
  // NOTE: Blocking done within the Google Benchmark library itself (e.g.
  // the barrier which synchronizes threads entering and exiting the benchmark
  // loop) does _not_ get registered in this counter. This is because Google
  // Benchmark uses its own synchronization primitives based on std::mutex, not
  // Abseil synchronization primitives. If at some point the benchmark library
  // merges into Abseil, this code may break.
  absl::synchronization_internal::PerThreadSem::SetThreadBlockedCounter(
      &shared->blocked_threads);

  // The benchmark framework may run several iterations in the same process,
  // reusing the same static-initialized 'shared' object. Given the semantics
  // of the members, here, we expect everything to be reset to zero by the
  // end of any iteration. Assert that's the case, just to be sure.
  ABSL_RAW_CHECK(
      shared->looping_threads.load(std::memory_order_relaxed) == 0 &&
          shared->blocked_threads.load(std::memory_order_relaxed) == 0 &&
          !shared->thread_has_mutex.load(std::memory_order_relaxed),
      "Shared state isn't zeroed at start of benchmark iteration");

  static constexpr int kBatchSize = 1000;
  while (state.KeepRunningBatch(kBatchSize)) {
    shared->looping_threads.fetch_add(1);
    for (int i = 0; i < kBatchSize; i++) {
      {
        absl::MutexLock l(&shared->mu);
        shared->thread_has_mutex.store(true, std::memory_order_relaxed);
        // Spin until all other threads are either out of the benchmark loop
        // or blocked on the mutex. This ensures that the mutex queue is kept
        // at its maximal length to benchmark the performance of queueing on
        // a highly contended mutex.
        while (shared->looping_threads.load(std::memory_order_relaxed) -
                   shared->blocked_threads.load(std::memory_order_relaxed) !=
               1) {
        }
        shared->thread_has_mutex.store(false);
      }
      // Spin until some other thread has acquired the mutex before we block
      // again. This ensures that we always go through the slow (queueing)
      // acquisition path rather than reacquiring the mutex we just released.
      while (!shared->thread_has_mutex.load(std::memory_order_relaxed) &&
             shared->looping_threads.load(std::memory_order_relaxed) > 1) {
      }
    }
    // The benchmark framework uses a barrier to ensure that all of the threads
    // complete their benchmark loop together before any of the threads exit
    // the loop. So, we need to remove ourselves from the "looping threads"
    // counter here before potentially blocking on that barrier. Otherwise,
    // another thread spinning above might wait forever for this thread to
    // block on the mutex while we in fact are waiting to exit.
    shared->looping_threads.fetch_add(-1);
  }
  absl::synchronization_internal::PerThreadSem::SetThreadBlockedCounter(
      nullptr);
}

BENCHMARK(BM_MutexEnqueue)
    ->Threads(4)
    ->Threads(64)
    ->Threads(128)
    ->Threads(512)
    ->ArgName("multiple_priorities")
    ->Arg(false)
    ->Arg(true);

template <typename MutexType>
void BM_Contended(benchmark::State& state) {
  int priority = state.thread_index() % state.range(1);
  ScopedThreadMutexPriority priority_setter(priority);

  struct Shared {
    MutexType mu;
    int data = 0;
  };
  static absl::NoDestructor<Shared> shared;
  int local = 0;
  for (auto _ : state) {
    // Here we model both local work outside of the critical section as well as
    // some work inside of the critical section. The idea is to capture some
    // more or less realisitic contention levels.
    // If contention is too low, the benchmark won't measure anything useful.
    // If contention is unrealistically high, the benchmark will favor
    // bad mutex implementations that block and otherwise distract threads
    // from the mutex and shared state for as much as possible.
    // To achieve this amount of local work is multiplied by number of threads
    // to keep ratio between local work and critical section approximately
    // equal regardless of number of threads.
    DelayNs(100 * state.threads(), &local);
    RaiiLocker<MutexType> locker(&shared->mu);
    DelayNs(state.range(0), &shared->data);
  }
}
void SetupBenchmarkArgs(benchmark::internal::Benchmark* bm,
                        bool do_test_priorities) {
  const int max_num_priorities = do_test_priorities ? 2 : 1;
  bm->UseRealTime()
      // ThreadPerCpu poorly handles non-power-of-two CPU counts.
      ->Threads(1)
      ->Threads(2)
      ->Threads(4)
      ->Threads(6)
      ->Threads(8)
      ->Threads(12)
      ->Threads(16)
      ->Threads(24)
      ->Threads(32)
      ->Threads(48)
      ->Threads(64)
      ->Threads(96)
      ->Threads(128)
      ->Threads(192)
      ->Threads(256)
      ->ArgNames({"cs_ns", "num_prios"});
  // Some empirically chosen amounts of work in critical section.
  // 1 is low contention, 2000 is high contention and few values in between.
  for (int critical_section_ns : {1, 20, 50, 200, 2000}) {
    for (int num_priorities = 1; num_priorities <= max_num_priorities;
         num_priorities++) {
      bm->ArgPair(critical_section_ns, num_priorities);
    }
  }
}

BENCHMARK_TEMPLATE(BM_Contended, absl::Mutex)
    ->Apply([](benchmark::internal::Benchmark* bm) {
      SetupBenchmarkArgs(bm, /*do_test_priorities=*/true);
    });

BENCHMARK_TEMPLATE(BM_Contended, absl::base_internal::SpinLock)
    ->Apply([](benchmark::internal::Benchmark* bm) {
      SetupBenchmarkArgs(bm, /*do_test_priorities=*/false);
    });

BENCHMARK_TEMPLATE(BM_Contended, std::mutex)
    ->Apply([](benchmark::internal::Benchmark* bm) {
      SetupBenchmarkArgs(bm, /*do_test_priorities=*/false);
    });

// Measure the overhead of conditions on mutex release (when they must be
// evaluated).  Mutex has (some) support for equivalence classes allowing
// Conditions with the same function/argument to potentially not be multiply
// evaluated.
//
// num_classes==0 is used for the special case of every waiter being distinct.
void BM_ConditionWaiters(benchmark::State& state) {
  int num_classes = state.range(0);
  int num_waiters = state.range(1);

  struct Helper {
    static void Waiter(absl::BlockingCounter* init, absl::Mutex* m, int* p) {
      init->DecrementCount();
      m->LockWhen(absl::Condition(
          static_cast<bool (*)(int*)>([](int* v) { return *v == 0; }), p));
      m->Unlock();
    }
  };

  if (num_classes == 0) {
    // No equivalence classes.
    num_classes = num_waiters;
  }

  absl::BlockingCounter init(num_waiters);
  absl::Mutex mu;
  std::vector<int> equivalence_classes(num_classes, 1);

  // Must be declared last to be destroyed first.
  absl::synchronization_internal::ThreadPool pool(num_waiters);

  for (int i = 0; i < num_waiters; i++) {
    // Mutex considers Conditions with the same function and argument
    // to be equivalent.
    pool.Schedule([&, i] {
      Helper::Waiter(&init, &mu, &equivalence_classes[i % num_classes]);
    });
  }
  init.Wait();

  for (auto _ : state) {
    mu.Lock();
    mu.Unlock();  // Each unlock requires Condition evaluation for our waiters.
  }

  mu.Lock();
  for (int i = 0; i < num_classes; i++) {
    equivalence_classes[i] = 0;
  }
  mu.Unlock();
}

// Some configurations have higher thread limits than others.
#if defined(__linux__) && !defined(ABSL_HAVE_THREAD_SANITIZER)
constexpr int kMaxConditionWaiters = 8192;
#else
constexpr int kMaxConditionWaiters = 1024;
#endif
BENCHMARK(BM_ConditionWaiters)->RangePair(0, 2, 1, kMaxConditionWaiters);

}  // namespace
