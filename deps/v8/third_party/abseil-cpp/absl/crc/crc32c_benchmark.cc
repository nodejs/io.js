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

#include <string>

#include "absl/crc/crc32c.h"
#include "absl/crc/internal/crc32c.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"

namespace {

std::string TestString(size_t len) {
  std::string result;
  result.reserve(len);
  for (size_t i = 0; i < len; ++i) {
    result.push_back(static_cast<char>(i % 256));
  }
  return result;
}

void BM_Calculate(benchmark::State& state) {
  int len = state.range(0);
  std::string data = TestString(len);
  for (auto s : state) {
    benchmark::DoNotOptimize(data);
    absl::crc32c_t crc = absl::ComputeCrc32c(data);
    benchmark::DoNotOptimize(crc);
  }
}
BENCHMARK(BM_Calculate)->Arg(0)->Arg(1)->Arg(100)->Arg(10000)->Arg(500000);

void BM_Extend(benchmark::State& state) {
  int len = state.range(0);
  std::string extension = TestString(len);
  absl::crc32c_t base = absl::crc32c_t{0xC99465AA};  // CRC32C of "Hello World"
  for (auto s : state) {
    benchmark::DoNotOptimize(base);
    benchmark::DoNotOptimize(extension);
    absl::crc32c_t crc = absl::ExtendCrc32c(base, extension);
    benchmark::DoNotOptimize(crc);
  }
}
BENCHMARK(BM_Extend)->Arg(0)->Arg(1)->Arg(100)->Arg(10000)->Arg(500000)->Arg(
    100 * 1000 * 1000);

// Make working set >> CPU cache size to benchmark prefetches better
void BM_ExtendCacheMiss(benchmark::State& state) {
  int len = state.range(0);
  constexpr int total = 300 * 1000 * 1000;
  std::string extension = TestString(total);
  absl::crc32c_t base = absl::crc32c_t{0xC99465AA};  // CRC32C of "Hello World"
  for (auto s : state) {
    for (int i = 0; i < total; i += len * 2) {
      benchmark::DoNotOptimize(base);
      benchmark::DoNotOptimize(extension);
      absl::crc32c_t crc =
          absl::ExtendCrc32c(base, absl::string_view(&extension[i], len));
      benchmark::DoNotOptimize(crc);
    }
  }
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * total / 2);
}
BENCHMARK(BM_ExtendCacheMiss)->Arg(10)->Arg(100)->Arg(1000)->Arg(100000);

void BM_ExtendByZeroes(benchmark::State& state) {
  absl::crc32c_t base = absl::crc32c_t{0xC99465AA};  // CRC32C of "Hello World"
  int num_zeroes = state.range(0);
  for (auto s : state) {
    benchmark::DoNotOptimize(base);
    absl::crc32c_t crc = absl::ExtendCrc32cByZeroes(base, num_zeroes);
    benchmark::DoNotOptimize(crc);
  }
}
BENCHMARK(BM_ExtendByZeroes)
    ->RangeMultiplier(10)
    ->Range(1, 1000000)
    ->RangeMultiplier(32)
    ->Range(1, 1 << 20);

void BM_UnextendByZeroes(benchmark::State& state) {
  absl::crc32c_t base = absl::crc32c_t{0xdeadbeef};
  int num_zeroes = state.range(0);
  for (auto s : state) {
    benchmark::DoNotOptimize(base);
    absl::crc32c_t crc =
        absl::crc_internal::UnextendCrc32cByZeroes(base, num_zeroes);
    benchmark::DoNotOptimize(crc);
  }
}
BENCHMARK(BM_UnextendByZeroes)
    ->RangeMultiplier(10)
    ->Range(1, 1000000)
    ->RangeMultiplier(32)
    ->Range(1, 1 << 20);

void BM_Concat(benchmark::State& state) {
  int string_b_len = state.range(0);
  std::string string_b = TestString(string_b_len);

  // CRC32C of "Hello World"
  absl::crc32c_t crc_a = absl::crc32c_t{0xC99465AA};
  absl::crc32c_t crc_b = absl::ComputeCrc32c(string_b);

  for (auto s : state) {
    benchmark::DoNotOptimize(crc_a);
    benchmark::DoNotOptimize(crc_b);
    benchmark::DoNotOptimize(string_b_len);
    absl::crc32c_t crc_ab = absl::ConcatCrc32c(crc_a, crc_b, string_b_len);
    benchmark::DoNotOptimize(crc_ab);
  }
}
BENCHMARK(BM_Concat)
    ->RangeMultiplier(10)
    ->Range(1, 1000000)
    ->RangeMultiplier(32)
    ->Range(1, 1 << 20);

void BM_Memcpy(benchmark::State& state) {
  int string_len = state.range(0);

  std::string source = TestString(string_len);
  auto dest = absl::make_unique<char[]>(string_len);

  for (auto s : state) {
    benchmark::DoNotOptimize(source);
    absl::crc32c_t crc =
        absl::MemcpyCrc32c(dest.get(), source.data(), source.size());
    benchmark::DoNotOptimize(crc);
    benchmark::DoNotOptimize(dest);
    benchmark::DoNotOptimize(dest.get());
    benchmark::DoNotOptimize(dest[0]);
  }

  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                          state.range(0));
}
BENCHMARK(BM_Memcpy)->Arg(0)->Arg(1)->Arg(100)->Arg(10000)->Arg(500000);

void BM_RemoveSuffix(benchmark::State& state) {
  int full_string_len = state.range(0);
  int suffix_len = state.range(1);

  std::string full_string = TestString(full_string_len);
  std::string suffix = full_string.substr(
    full_string_len - suffix_len, full_string_len);

  absl::crc32c_t full_string_crc = absl::ComputeCrc32c(full_string);
  absl::crc32c_t suffix_crc = absl::ComputeCrc32c(suffix);

  for (auto s : state) {
    benchmark::DoNotOptimize(full_string_crc);
    benchmark::DoNotOptimize(suffix_crc);
    benchmark::DoNotOptimize(suffix_len);
    absl::crc32c_t crc = absl::RemoveCrc32cSuffix(full_string_crc, suffix_crc,
      suffix_len);
    benchmark::DoNotOptimize(crc);
  }
}
BENCHMARK(BM_RemoveSuffix)
    ->ArgPair(1, 1)
    ->ArgPair(100, 10)
    ->ArgPair(100, 100)
    ->ArgPair(10000, 1)
    ->ArgPair(10000, 100)
    ->ArgPair(10000, 10000)
    ->ArgPair(500000, 1)
    ->ArgPair(500000, 100)
    ->ArgPair(500000, 10000)
    ->ArgPair(500000, 500000);
}  // namespace
