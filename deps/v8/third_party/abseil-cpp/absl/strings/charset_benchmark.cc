// Copyright 2020 The Abseil Authors.
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

#include "benchmark/benchmark.h"
#include "absl/log/check.h"
#include "absl/strings/charset.h"

namespace {

absl::CharSet MakeBenchmarkMap() {
  absl::CharSet m;
  uint32_t x[] = {0x0, 0x1, 0x2, 0x3, 0xf, 0xe, 0xd, 0xc};
  for (uint32_t& t : x) t *= static_cast<uint32_t>(0x11111111UL);
  for (uint32_t i = 0; i < 256; ++i) {
    if ((x[i / 32] >> (i % 32)) & 1) m = m | absl::CharSet::Char(i);
  }
  return m;
}

// Micro-benchmark for Charmap::contains.
static void BM_Contains(benchmark::State& state) {
  // Loop-body replicated 10 times to increase time per iteration.
  // Argument continuously changed to avoid generating common subexpressions.
  // Final CHECK used to discourage unwanted optimization.
  const absl::CharSet benchmark_map = MakeBenchmarkMap();
  unsigned char c = 0;
  int ops = 0;
  for (auto _ : state) {
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
    ops += benchmark_map.contains(c++);
  }
  CHECK_NE(ops, -1);
}
BENCHMARK(BM_Contains);

}  // namespace
