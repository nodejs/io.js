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

#include <cstdint>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/optimization.h"
#include "absl/numeric/bits.h"
#include "absl/random/random.h"

namespace absl {
namespace {

template <typename T>
static void BM_bit_width(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));

  absl::BitGen rng;
  std::vector<T> values;
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    values.push_back(absl::Uniform<T>(rng, 0, std::numeric_limits<T>::max()));
  }

  while (state.KeepRunningBatch(static_cast<int64_t>(count))) {
    for (size_t i = 0; i < count; ++i) {
      benchmark::DoNotOptimize(absl::bit_width(values[i]));
    }
  }
}
BENCHMARK_TEMPLATE(BM_bit_width, uint8_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width, uint16_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width, uint32_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width, uint64_t)->Range(1, 1 << 20);

template <typename T>
static void BM_bit_width_nonzero(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));

  absl::BitGen rng;
  std::vector<T> values;
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    values.push_back(absl::Uniform<T>(rng, 1, std::numeric_limits<T>::max()));
  }

  while (state.KeepRunningBatch(static_cast<int64_t>(count))) {
    for (size_t i = 0; i < count; ++i) {
      const T value = values[i];
      ABSL_ASSUME(value > 0);
      benchmark::DoNotOptimize(absl::bit_width(value));
    }
  }
}
BENCHMARK_TEMPLATE(BM_bit_width_nonzero, uint8_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width_nonzero, uint16_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width_nonzero, uint32_t)->Range(1, 1 << 20);
BENCHMARK_TEMPLATE(BM_bit_width_nonzero, uint64_t)->Range(1, 1 << 20);

}  // namespace
}  // namespace absl
