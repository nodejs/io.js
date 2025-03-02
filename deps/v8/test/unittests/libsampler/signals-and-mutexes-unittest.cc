// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>

#include "src/base/platform/mutex.h"
#include "src/base/platform/platform.h"
#include "src/base/platform/time.h"
#include "src/base/utils/random-number-generator.h"
#include "src/libsampler/sampler.h"  // for USE_SIGNALS
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {

using SignalAndMutexTest = TestWithContext;
namespace sampler {

#ifdef USE_SIGNALS

void HandleProfilerSignal(int signal, siginfo_t*, void*) {
  CHECK_EQ(SIGPROF, signal);
}

struct sigaction old_signal_handler;

void InstallSignalHandler() {
  struct sigaction sa;
  sa.sa_sigaction = &HandleProfilerSignal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_SIGINFO | SA_ONSTACK;
  CHECK_EQ(0, sigaction(SIGPROF, &sa, &old_signal_handler));
}

static void RestoreSignalHandler() {
  sigaction(SIGPROF, &old_signal_handler, nullptr);
}

TEST_F(SignalAndMutexTest, SignalsPlusMutexes) {
  static constexpr int kNumMutexes = 1024;
  // 10us * 10000 = 100ms
  static constexpr auto kSleepBetweenSamples =
      base::TimeDelta::FromMicroseconds(10);
  static constexpr size_t kNumSamples = 10000;

  // Keep a set of all currently running threads. This is filled by the threads
  // themselves, because we need to get their pthread id.
  base::Mutex threads_mutex;
  std::set<pthread_t> threads_to_sample;
  auto SendSignalToThreads = [&threads_mutex, &threads_to_sample] {
    base::MutexGuard guard(&threads_mutex);
    for (pthread_t tid : threads_to_sample) {
      pthread_kill(tid, SIGPROF);
    }
  };
  auto AddThreadToSample = [&threads_mutex, &threads_to_sample](pthread_t tid) {
    base::MutexGuard guard(&threads_mutex);
    CHECK(threads_to_sample.insert(tid).second);
  };
  auto RemoveThreadToSample = [&threads_mutex,
                               &threads_to_sample](pthread_t tid) {
    base::MutexGuard guard(&threads_mutex);
    CHECK_EQ(1, threads_to_sample.erase(tid));
  };

  // The sampling threads periodically sends a SIGPROF to all running threads.
  class SamplingThread : public base::Thread {
   public:
    explicit SamplingThread(std::atomic<size_t>* remaining_samples,
                            std::function<void()> send_signal_to_threads)
        : Thread(base::Thread::Options{"SamplingThread"}),
          remaining_samples_(remaining_samples),
          send_signal_to_threads_(std::move(send_signal_to_threads)) {}

    void Run() override {
      DCHECK_LT(0, remaining_samples_->load(std::memory_order_relaxed));
      while (remaining_samples_->fetch_sub(1, std::memory_order_relaxed) > 1) {
        send_signal_to_threads_();
        base::OS::Sleep(kSleepBetweenSamples);
      }
      DCHECK_EQ(0, remaining_samples_->load(std::memory_order_relaxed));
    }

   private:
    std::atomic<size_t>* const remaining_samples_;
    const std::function<void()> send_signal_to_threads_;
  };

  // These threads repeatedly lock and unlock a shared mutex, both in shared and
  // exclusive mode. This should not deadlock.
  class MutexTestThread : public base::Thread {
   public:
    MutexTestThread(base::SpinningMutex* mutexes,
                    std::atomic<size_t>* remaining_samples, int64_t rng_seed,
                    std::function<void(pthread_t)> add_thread_to_sample,
                    std::function<void(pthread_t)> remove_thread_to_sample)
        : Thread(Thread::Options{"SharedMutexTestThread"}),
          mutexes_(mutexes),
          remaining_samples_(remaining_samples),
          rng_(rng_seed),
          add_thread_to_sample_(add_thread_to_sample),
          remove_thread_to_sample_(remove_thread_to_sample) {}

    void Run() override {
      add_thread_to_sample_(pthread_self());
      while (remaining_samples_->load(std::memory_order_relaxed) > 0) {
        size_t idx = rng_.NextInt(kNumMutexes);
        base::SpinningMutex* mutex = &mutexes_[idx];
        base::SpinningMutexGuard guard{mutex};
      }
      remove_thread_to_sample_(pthread_self());
    }

   private:
    base::SpinningMutex* mutexes_;
    std::atomic<size_t>* remaining_samples_;
    base::RandomNumberGenerator rng_;
    std::function<void(pthread_t)> add_thread_to_sample_;
    std::function<void(pthread_t)> remove_thread_to_sample_;
  };

  std::atomic<size_t> remaining_samples{kNumSamples};
  base::SpinningMutex mutexes[kNumMutexes];

  InstallSignalHandler();

  auto* rng = i_isolate()->random_number_generator();

  // First start the mutex threads, then the sampling thread.
  std::vector<std::unique_ptr<MutexTestThread>> threads(4);
  for (auto& thread : threads) {
    thread = std::make_unique<MutexTestThread>(
        mutexes, &remaining_samples, rng->NextInt64(), AddThreadToSample,
        RemoveThreadToSample);
    CHECK(thread->Start());
  }

  SamplingThread sampling_thread(&remaining_samples, SendSignalToThreads);
  CHECK(sampling_thread.Start());

  // Wait for the sampling thread to be done. The mutex threads should finish
  // shortly after.
  sampling_thread.Join();
  for (auto& thread : threads) thread->Join();

  RestoreSignalHandler();
}

#endif  // USE_SIGNALS

}  // namespace sampler
}  // namespace v8
