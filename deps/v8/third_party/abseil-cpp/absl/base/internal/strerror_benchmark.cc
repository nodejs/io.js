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

#include <cerrno>
#include <cstdio>
#include <string>

#include "absl/base/internal/strerror.h"
#include "benchmark/benchmark.h"

namespace {
void BM_AbslStrError(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(absl::base_internal::StrError(ERANGE));
  }
}
BENCHMARK(BM_AbslStrError);
}  // namespace
