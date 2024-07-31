// Copyright 2018 The Abseil Authors.
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

#include "absl/synchronization/internal/graphcycles.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/internal/raw_logging.h"

namespace {

void BM_StressTest(benchmark::State& state) {
  const int num_nodes = state.range(0);
  while (state.KeepRunningBatch(num_nodes)) {
    absl::synchronization_internal::GraphCycles g;
    std::vector<absl::synchronization_internal::GraphId> nodes(num_nodes);
    for (int i = 0; i < num_nodes; i++) {
      nodes[i] = g.GetId(reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
    for (int i = 0; i < num_nodes; i++) {
      int end = std::min(num_nodes, i + 5);
      for (int j = i + 1; j < end; j++) {
        ABSL_RAW_CHECK(g.InsertEdge(nodes[i], nodes[j]), "");
      }
    }
  }
}
BENCHMARK(BM_StressTest)->Range(2048, 1048576);

}  // namespace
