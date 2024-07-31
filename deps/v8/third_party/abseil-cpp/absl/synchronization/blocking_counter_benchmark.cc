// Copyright 2021 The Abseil Authors.
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

#include <limits>

#include "absl/base/no_destructor.h"
#include "absl/synchronization/blocking_counter.h"
#include "absl/synchronization/internal/thread_pool.h"
#include "benchmark/benchmark.h"

namespace {

void BM_BlockingCounter_SingleThread(benchmark::State& state) {
  for (auto _ : state) {
    int iterations = state.range(0);
    absl::BlockingCounter counter{iterations};
    for (int i = 0; i < iterations; ++i) {
      counter.DecrementCount();
    }
    counter.Wait();
  }
}
BENCHMARK(BM_BlockingCounter_SingleThread)
    ->ArgName("iterations")
    ->Arg(2)
    ->Arg(4)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256);

void BM_BlockingCounter_DecrementCount(benchmark::State& state) {
  static absl::NoDestructor<absl::BlockingCounter> counter(
      std::numeric_limits<int>::max());
  for (auto _ : state) {
    counter->DecrementCount();
  }
}
BENCHMARK(BM_BlockingCounter_DecrementCount)
    ->Threads(2)
    ->Threads(4)
    ->Threads(6)
    ->Threads(8)
    ->Threads(10)
    ->Threads(12)
    ->Threads(16)
    ->Threads(32)
    ->Threads(64)
    ->Threads(128);

void BM_BlockingCounter_Wait(benchmark::State& state) {
  int num_threads = state.range(0);
  absl::synchronization_internal::ThreadPool pool(num_threads);
  for (auto _ : state) {
    absl::BlockingCounter counter{num_threads};
    pool.Schedule([num_threads, &counter, &pool]() {
      for (int i = 0; i < num_threads; ++i) {
        pool.Schedule([&counter]() { counter.DecrementCount(); });
      }
    });
    counter.Wait();
  }
}
BENCHMARK(BM_BlockingCounter_Wait)
    ->ArgName("threads")
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128);

}  // namespace
