// Copyright 2018 The Abseil Authors.
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

#include "absl/time/clock.h"

#if !defined(_WIN32)
#include <sys/time.h>
#else
#include <winsock2.h>
#endif  // _WIN32
#include <cstdio>

#include "absl/base/internal/cycleclock.h"
#include "benchmark/benchmark.h"

namespace {

void BM_Clock_Now_AbslTime(benchmark::State& state) {
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::Now());
  }
}
BENCHMARK(BM_Clock_Now_AbslTime);

void BM_Clock_Now_GetCurrentTimeNanos(benchmark::State& state) {
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::GetCurrentTimeNanos());
  }
}
BENCHMARK(BM_Clock_Now_GetCurrentTimeNanos);

void BM_Clock_Now_AbslTime_ToUnixNanos(benchmark::State& state) {
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::ToUnixNanos(absl::Now()));
  }
}
BENCHMARK(BM_Clock_Now_AbslTime_ToUnixNanos);

void BM_Clock_Now_CycleClock(benchmark::State& state) {
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::base_internal::CycleClock::Now());
  }
}
BENCHMARK(BM_Clock_Now_CycleClock);

#if !defined(_WIN32)
static void BM_Clock_Now_gettimeofday(benchmark::State& state) {
  struct timeval tv;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(gettimeofday(&tv, nullptr));
  }
}
BENCHMARK(BM_Clock_Now_gettimeofday);

static void BM_Clock_Now_clock_gettime(benchmark::State& state) {
  struct timespec ts;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(clock_gettime(CLOCK_REALTIME, &ts));
  }
}
BENCHMARK(BM_Clock_Now_clock_gettime);
#endif  // _WIN32

}  // namespace
