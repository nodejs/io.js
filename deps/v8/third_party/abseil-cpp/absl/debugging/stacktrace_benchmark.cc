// Copyright 2022 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/optimization.h"
#include "absl/debugging/stacktrace.h"
#include "benchmark/benchmark.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace {

static constexpr int kMaxStackDepth = 100;
static constexpr int kCacheSize = (1 << 16);
void* pcs[kMaxStackDepth];

ABSL_ATTRIBUTE_NOINLINE void func(benchmark::State& state, int x, int depth) {
  if (x <= 0) {
    // Touch a significant amount of memory so that the stack is likely to be
    // not cached in the L1 cache.
    state.PauseTiming();
    int* arr = new int[kCacheSize];
    for (int i = 0; i < kCacheSize; ++i) benchmark::DoNotOptimize(arr[i] = 100);
    delete[] arr;
    state.ResumeTiming();
    benchmark::DoNotOptimize(absl::GetStackTrace(pcs, depth, 0));
    return;
  }
  ABSL_BLOCK_TAIL_CALL_OPTIMIZATION();
  func(state, --x, depth);
}

void BM_GetStackTrace(benchmark::State& state) {
  int depth = state.range(0);
  for (auto s : state) {
    func(state, depth, depth);
  }
}

BENCHMARK(BM_GetStackTrace)->DenseRange(10, kMaxStackDepth, 10);
}  // namespace
ABSL_NAMESPACE_END
}  // namespace absl
