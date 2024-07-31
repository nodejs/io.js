// Copyright 2019 The Abseil Authors.
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

#include <stddef.h>

#include <string>

#include "absl/container/fixed_array.h"
#include "benchmark/benchmark.h"

namespace {

// For benchmarking -- simple class with constructor and destructor that
// set an int to a constant..
class SimpleClass {
 public:
  SimpleClass() : i(3) {}
  ~SimpleClass() { i = 0; }

 private:
  int i;
};

template <typename C, size_t stack_size>
void BM_FixedArray(benchmark::State& state) {
  const int size = state.range(0);
  for (auto _ : state) {
    absl::FixedArray<C, stack_size> fa(size);
    benchmark::DoNotOptimize(fa.data());
  }
}
BENCHMARK_TEMPLATE(BM_FixedArray, char, absl::kFixedArrayUseDefault)
    ->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, char, 0)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, char, 1)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, char, 16)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, char, 256)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, char, 65536)->Range(0, 1 << 16);

BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, absl::kFixedArrayUseDefault)
    ->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, 0)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, 1)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, 16)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, 256)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, SimpleClass, 65536)->Range(0, 1 << 16);

BENCHMARK_TEMPLATE(BM_FixedArray, std::string, absl::kFixedArrayUseDefault)
    ->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, std::string, 0)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, std::string, 1)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, std::string, 16)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, std::string, 256)->Range(0, 1 << 16);
BENCHMARK_TEMPLATE(BM_FixedArray, std::string, 65536)->Range(0, 1 << 16);

}  // namespace
