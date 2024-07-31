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

#include "absl/profiling/internal/periodic_sampler.h"
#include "benchmark/benchmark.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {
namespace {

template <typename Sampler>
void BM_Sample(Sampler* sampler, benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(sampler);
    benchmark::DoNotOptimize(sampler->Sample());
  }
}

template <typename Sampler>
void BM_SampleMinunumInlined(Sampler* sampler, benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(sampler);
    if (ABSL_PREDICT_FALSE(sampler->SubtleMaybeSample())) {
      benchmark::DoNotOptimize(sampler->SubtleConfirmSample());
    }
  }
}

void BM_PeriodicSampler_TinySample(benchmark::State& state) {
  struct Tag {};
  PeriodicSampler<Tag, 10> sampler;
  BM_Sample(&sampler, state);
}
BENCHMARK(BM_PeriodicSampler_TinySample);

void BM_PeriodicSampler_ShortSample(benchmark::State& state) {
  struct Tag {};
  PeriodicSampler<Tag, 1024> sampler;
  BM_Sample(&sampler, state);
}
BENCHMARK(BM_PeriodicSampler_ShortSample);

void BM_PeriodicSampler_LongSample(benchmark::State& state) {
  struct Tag {};
  PeriodicSampler<Tag, 1024 * 1024> sampler;
  BM_Sample(&sampler, state);
}
BENCHMARK(BM_PeriodicSampler_LongSample);

void BM_PeriodicSampler_LongSampleMinunumInlined(benchmark::State& state) {
  struct Tag {};
  PeriodicSampler<Tag, 1024 * 1024> sampler;
  BM_SampleMinunumInlined(&sampler, state);
}
BENCHMARK(BM_PeriodicSampler_LongSampleMinunumInlined);

void BM_PeriodicSampler_Disabled(benchmark::State& state) {
  struct Tag {};
  PeriodicSampler<Tag, 0> sampler;
  BM_Sample(&sampler, state);
}
BENCHMARK(BM_PeriodicSampler_Disabled);

}  // namespace
}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
