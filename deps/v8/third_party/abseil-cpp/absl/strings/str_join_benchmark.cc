//
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

#include "absl/strings/str_join.h"

#include <string>
#include <vector>
#include <utility>

#include "benchmark/benchmark.h"

namespace {

void BM_Join2_Strings(benchmark::State& state) {
  const int string_len = state.range(0);
  const int num_strings = state.range(1);
  const std::string s(string_len, 'x');
  const std::vector<std::string> v(num_strings, s);
  for (auto _ : state) {
    std::string s = absl::StrJoin(v, "-");
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_Join2_Strings)
    ->ArgPair(1 << 0, 1 << 3)
    ->ArgPair(1 << 10, 1 << 3)
    ->ArgPair(1 << 13, 1 << 3)
    ->ArgPair(1 << 0, 1 << 10)
    ->ArgPair(1 << 10, 1 << 10)
    ->ArgPair(1 << 13, 1 << 10)
    ->ArgPair(1 << 0, 1 << 13)
    ->ArgPair(1 << 10, 1 << 13)
    ->ArgPair(1 << 13, 1 << 13);

void BM_Join2_Ints(benchmark::State& state) {
  const int num_ints = state.range(0);
  const std::vector<int> v(num_ints, 42);
  for (auto _ : state) {
    std::string s = absl::StrJoin(v, "-");
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_Join2_Ints)->Range(0, 1 << 13);

void BM_Join2_KeysAndValues(benchmark::State& state) {
  const int string_len = state.range(0);
  const int num_pairs = state.range(1);
  const std::string s(string_len, 'x');
  const std::vector<std::pair<std::string, int>> v(num_pairs,
                                                   std::make_pair(s, 42));
  for (auto _ : state) {
    std::string s = absl::StrJoin(v, ",", absl::PairFormatter("="));
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_Join2_KeysAndValues)
    ->ArgPair(1 << 0, 1 << 3)
    ->ArgPair(1 << 10, 1 << 3)
    ->ArgPair(1 << 13, 1 << 3)
    ->ArgPair(1 << 0, 1 << 10)
    ->ArgPair(1 << 10, 1 << 10)
    ->ArgPair(1 << 13, 1 << 10)
    ->ArgPair(1 << 0, 1 << 13)
    ->ArgPair(1 << 10, 1 << 13)
    ->ArgPair(1 << 13, 1 << 13);

void BM_JoinStreamable(benchmark::State& state) {
  const int string_len = state.range(0);
  const int num_strings = state.range(1);
  const std::vector<std::string> v(num_strings, std::string(string_len, 'x'));
  for (auto _ : state) {
    std::string s = absl::StrJoin(v, "", absl::StreamFormatter());
    benchmark::DoNotOptimize(s);
  }
}
BENCHMARK(BM_JoinStreamable)
    ->ArgPair(0, 0)
    ->ArgPair(16, 1)
    ->ArgPair(256, 1)
    ->ArgPair(16, 16)
    ->ArgPair(256, 16)
    ->ArgPair(16, 256)
    ->ArgPair(256, 256);

}  // namespace
