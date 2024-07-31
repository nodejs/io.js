// Copyright 2017 The Abseil Authors.
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

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/config.h"
#include "absl/numeric/int128.h"

namespace {

constexpr size_t kSampleSize = 1000000;

std::mt19937 MakeRandomEngine() {
  std::random_device r;
  std::seed_seq seed({r(), r(), r(), r(), r(), r(), r(), r()});
  return std::mt19937(seed);
}

template <typename T,
          typename H = typename std::conditional<
              std::numeric_limits<T>::is_signed, int64_t, uint64_t>::type>
std::vector<std::pair<T, T>> GetRandomClass128SampleUniformDivisor() {
  std::vector<std::pair<T, T>> values;
  std::mt19937 random = MakeRandomEngine();
  std::uniform_int_distribution<H> uniform_h;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    T a{absl::MakeUint128(uniform_h(random), uniform_h(random))};
    T b{absl::MakeUint128(uniform_h(random), uniform_h(random))};
    values.emplace_back(std::max(a, b), std::max(T(2), std::min(a, b)));
  }
  return values;
}

template <typename T>
void BM_DivideClass128UniformDivisor(benchmark::State& state) {
  auto values = GetRandomClass128SampleUniformDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first / pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_DivideClass128UniformDivisor, absl::uint128);
BENCHMARK_TEMPLATE(BM_DivideClass128UniformDivisor, absl::int128);

template <typename T>
void BM_RemainderClass128UniformDivisor(benchmark::State& state) {
  auto values = GetRandomClass128SampleUniformDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first % pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_RemainderClass128UniformDivisor, absl::uint128);
BENCHMARK_TEMPLATE(BM_RemainderClass128UniformDivisor, absl::int128);

template <typename T,
          typename H = typename std::conditional<
              std::numeric_limits<T>::is_signed, int64_t, uint64_t>::type>
std::vector<std::pair<T, H>> GetRandomClass128SampleSmallDivisor() {
  std::vector<std::pair<T, H>> values;
  std::mt19937 random = MakeRandomEngine();
  std::uniform_int_distribution<H> uniform_h;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    T a{absl::MakeUint128(uniform_h(random), uniform_h(random))};
    H b{std::max(H{2}, uniform_h(random))};
    values.emplace_back(std::max(a, T(b)), b);
  }
  return values;
}

template <typename T>
void BM_DivideClass128SmallDivisor(benchmark::State& state) {
  auto values = GetRandomClass128SampleSmallDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first / pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_DivideClass128SmallDivisor, absl::uint128);
BENCHMARK_TEMPLATE(BM_DivideClass128SmallDivisor, absl::int128);

template <typename T>
void BM_RemainderClass128SmallDivisor(benchmark::State& state) {
  auto values = GetRandomClass128SampleSmallDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first % pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_RemainderClass128SmallDivisor, absl::uint128);
BENCHMARK_TEMPLATE(BM_RemainderClass128SmallDivisor, absl::int128);

std::vector<std::pair<absl::uint128, absl::uint128>> GetRandomClass128Sample() {
  std::vector<std::pair<absl::uint128, absl::uint128>> values;
  std::mt19937 random = MakeRandomEngine();
  std::uniform_int_distribution<uint64_t> uniform_uint64;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    values.emplace_back(
        absl::MakeUint128(uniform_uint64(random), uniform_uint64(random)),
        absl::MakeUint128(uniform_uint64(random), uniform_uint64(random)));
  }
  return values;
}

void BM_MultiplyClass128(benchmark::State& state) {
  auto values = GetRandomClass128Sample();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first * pair.second);
    }
  }
}
BENCHMARK(BM_MultiplyClass128);

void BM_AddClass128(benchmark::State& state) {
  auto values = GetRandomClass128Sample();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first + pair.second);
    }
  }
}
BENCHMARK(BM_AddClass128);

#ifdef ABSL_HAVE_INTRINSIC_INT128

// Some implementations of <random> do not support __int128 when it is
// available, so we make our own uniform_int_distribution-like type.
template <typename T,
          typename H = typename std::conditional<
              std::is_same<T, __int128>::value, int64_t, uint64_t>::type>
class UniformIntDistribution128 {
 public:
  // NOLINTNEXTLINE: mimicking std::uniform_int_distribution API
  T operator()(std::mt19937& generator) {
    return (static_cast<T>(dist64_(generator)) << 64) | dist64_(generator);
  }

 private:
  std::uniform_int_distribution<H> dist64_;
};

template <typename T,
          typename H = typename std::conditional<
              std::is_same<T, __int128>::value, int64_t, uint64_t>::type>
std::vector<std::pair<T, T>> GetRandomIntrinsic128SampleUniformDivisor() {
  std::vector<std::pair<T, T>> values;
  std::mt19937 random = MakeRandomEngine();
  UniformIntDistribution128<T> uniform_128;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    T a = uniform_128(random);
    T b = uniform_128(random);
    values.emplace_back(std::max(a, b),
                        std::max(static_cast<T>(2), std::min(a, b)));
  }
  return values;
}

template <typename T>
void BM_DivideIntrinsic128UniformDivisor(benchmark::State& state) {
  auto values = GetRandomIntrinsic128SampleUniformDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first / pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_DivideIntrinsic128UniformDivisor, unsigned __int128);
BENCHMARK_TEMPLATE(BM_DivideIntrinsic128UniformDivisor, __int128);

template <typename T>
void BM_RemainderIntrinsic128UniformDivisor(benchmark::State& state) {
  auto values = GetRandomIntrinsic128SampleUniformDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first % pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_RemainderIntrinsic128UniformDivisor, unsigned __int128);
BENCHMARK_TEMPLATE(BM_RemainderIntrinsic128UniformDivisor, __int128);

template <typename T,
          typename H = typename std::conditional<
              std::is_same<T, __int128>::value, int64_t, uint64_t>::type>
std::vector<std::pair<T, H>> GetRandomIntrinsic128SampleSmallDivisor() {
  std::vector<std::pair<T, H>> values;
  std::mt19937 random = MakeRandomEngine();
  UniformIntDistribution128<T> uniform_int128;
  std::uniform_int_distribution<H> uniform_int64;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    T a = uniform_int128(random);
    H b = std::max(H{2}, uniform_int64(random));
    values.emplace_back(std::max(a, static_cast<T>(b)), b);
  }
  return values;
}

template <typename T>
void BM_DivideIntrinsic128SmallDivisor(benchmark::State& state) {
  auto values = GetRandomIntrinsic128SampleSmallDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first / pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_DivideIntrinsic128SmallDivisor, unsigned __int128);
BENCHMARK_TEMPLATE(BM_DivideIntrinsic128SmallDivisor, __int128);

template <typename T>
void BM_RemainderIntrinsic128SmallDivisor(benchmark::State& state) {
  auto values = GetRandomIntrinsic128SampleSmallDivisor<T>();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first % pair.second);
    }
  }
}
BENCHMARK_TEMPLATE(BM_RemainderIntrinsic128SmallDivisor, unsigned __int128);
BENCHMARK_TEMPLATE(BM_RemainderIntrinsic128SmallDivisor, __int128);

std::vector<std::pair<unsigned __int128, unsigned __int128>>
      GetRandomIntrinsic128Sample() {
  std::vector<std::pair<unsigned __int128, unsigned __int128>> values;
  std::mt19937 random = MakeRandomEngine();
  UniformIntDistribution128<unsigned __int128> uniform_uint128;
  values.reserve(kSampleSize);
  for (size_t i = 0; i < kSampleSize; ++i) {
    values.emplace_back(uniform_uint128(random), uniform_uint128(random));
  }
  return values;
}

void BM_MultiplyIntrinsic128(benchmark::State& state) {
  auto values = GetRandomIntrinsic128Sample();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first * pair.second);
    }
  }
}
BENCHMARK(BM_MultiplyIntrinsic128);

void BM_AddIntrinsic128(benchmark::State& state) {
  auto values = GetRandomIntrinsic128Sample();
  while (state.KeepRunningBatch(values.size())) {
    for (const auto& pair : values) {
      benchmark::DoNotOptimize(pair.first + pair.second);
    }
  }
}
BENCHMARK(BM_AddIntrinsic128);

#endif  // ABSL_HAVE_INTRINSIC_INT128

}  // namespace
