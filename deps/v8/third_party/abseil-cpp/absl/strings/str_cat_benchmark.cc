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

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <tuple>
#include <utility>

#include "benchmark/benchmark.h"
#include "absl/random/log_uniform_int_distribution.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"

namespace {

const char kStringOne[] = "Once Upon A Time, ";
const char kStringTwo[] = "There was a string benchmark";

// We want to include negative numbers in the benchmark, so this function
// is used to count 0, 1, -1, 2, -2, 3, -3, ...
inline int IncrementAlternatingSign(int i) {
  return i > 0 ? -i : 1 - i;
}

void BM_Sum_By_StrCat(benchmark::State& state) {
  int i = 0;
  char foo[100];
  for (auto _ : state) {
    // NOLINTNEXTLINE(runtime/printf)
    strcpy(foo, absl::StrCat(kStringOne, i, kStringTwo, i * 65536ULL).c_str());
    int sum = 0;
    for (char* f = &foo[0]; *f != 0; ++f) {
      sum += *f;
    }
    benchmark::DoNotOptimize(sum);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_Sum_By_StrCat);

void BM_StrCat_By_snprintf(benchmark::State& state) {
  int i = 0;
  char on_stack[1000];
  for (auto _ : state) {
    snprintf(on_stack, sizeof(on_stack), "%s %s:%d", kStringOne, kStringTwo, i);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_snprintf);

void BM_StrCat_By_Strings(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result =
        std::string(kStringOne) + " " + kStringTwo + ":" + absl::StrCat(i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_Strings);

void BM_StrCat_By_StringOpPlus(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = kStringOne;
    result += " ";
    result += kStringTwo;
    result += ":";
    result += absl::StrCat(i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_StringOpPlus);

void BM_StrCat_By_StrCat(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = absl::StrCat(kStringOne, " ", kStringTwo, ":", i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_StrCat_By_StrCat);

void BM_HexCat_By_StrCat(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result =
        absl::StrCat(kStringOne, " ", absl::Hex(int64_t{i} + 0x10000000));
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_HexCat_By_StrCat);

void BM_HexCat_By_Substitute(benchmark::State& state) {
  int i = 0;
  for (auto _ : state) {
    std::string result = absl::Substitute(
        "$0 $1", kStringOne, reinterpret_cast<void*>(int64_t{i} + 0x10000000));
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_HexCat_By_Substitute);

void BM_FloatToString_By_StrCat(benchmark::State& state) {
  int i = 0;
  float foo = 0.0f;
  for (auto _ : state) {
    std::string result = absl::StrCat(foo += 1.001f, " != ", int64_t{i});
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_FloatToString_By_StrCat);

void BM_DoubleToString_By_SixDigits(benchmark::State& state) {
  int i = 0;
  double foo = 0.0;
  for (auto _ : state) {
    std::string result =
        absl::StrCat(absl::SixDigits(foo += 1.001), " != ", int64_t{i});
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}
BENCHMARK(BM_DoubleToString_By_SixDigits);

template <typename Table, size_t... Index>
void BM_StrAppendImpl(benchmark::State& state, Table table, size_t total_bytes,
                      std::index_sequence<Index...>) {
  for (auto s : state) {
    const size_t table_size = table.size();
    size_t i = 0;
    std::string result;
    while (result.size() < total_bytes) {
      absl::StrAppend(&result, std::get<Index>(table[i])...);
      benchmark::DoNotOptimize(result);
      ++i;
      i -= i >= table_size ? table_size : 0;
    }
  }
}

template <typename Array>
void BM_StrAppend(benchmark::State& state, Array&& table) {
  const size_t total_bytes = state.range(0);
  const int chunks_at_a_time = state.range(1);

  switch (chunks_at_a_time) {
    case 1:
      return BM_StrAppendImpl(state, std::forward<Array>(table), total_bytes,
                              std::make_index_sequence<1>());
    case 2:
      return BM_StrAppendImpl(state, std::forward<Array>(table), total_bytes,
                              std::make_index_sequence<2>());
    case 4:
      return BM_StrAppendImpl(state, std::forward<Array>(table), total_bytes,
                              std::make_index_sequence<4>());
    case 8:
      return BM_StrAppendImpl(state, std::forward<Array>(table), total_bytes,
                              std::make_index_sequence<8>());
    default:
      std::abort();
  }
}

void BM_StrAppendStr(benchmark::State& state) {
  using T = absl::string_view;
  using Row = std::tuple<T, T, T, T, T, T, T, T>;
  constexpr absl::string_view kChunk = "0123456789";
  Row row = {kChunk, kChunk, kChunk, kChunk, kChunk, kChunk, kChunk, kChunk};
  return BM_StrAppend(state, std::array<Row, 1>({row}));
}

template <typename T>
void BM_StrAppendInt(benchmark::State& state) {
  absl::BitGen rng;
  absl::log_uniform_int_distribution<T> dist;
  std::array<std::tuple<T, T, T, T, T, T, T, T>, (1 << 7)> table;
  for (size_t i = 0; i < table.size(); ++i) {
    table[i] = {dist(rng), dist(rng), dist(rng), dist(rng),
                dist(rng), dist(rng), dist(rng), dist(rng)};
  }
  return BM_StrAppend(state, table);
}

template <typename B>
void StrAppendConfig(B* benchmark) {
  for (int bytes : {10, 100, 1000, 10000}) {
    for (int chunks : {1, 2, 4, 8}) {
      // Only add the ones that divide properly. Otherwise we are over counting.
      if (bytes % (10 * chunks) == 0) {
        benchmark->Args({bytes, chunks});
      }
    }
  }
}

BENCHMARK(BM_StrAppendStr)->Apply(StrAppendConfig);
BENCHMARK(BM_StrAppendInt<int64_t>)->Apply(StrAppendConfig);
BENCHMARK(BM_StrAppendInt<uint64_t>)->Apply(StrAppendConfig);
BENCHMARK(BM_StrAppendInt<int32_t>)->Apply(StrAppendConfig);
BENCHMARK(BM_StrAppendInt<uint32_t>)->Apply(StrAppendConfig);

template <typename... Chunks>
void BM_StrCatImpl(benchmark::State& state,
                      Chunks... chunks) {
  for (auto s : state) {
    std::string result = absl::StrCat(chunks...);
    benchmark::DoNotOptimize(result);
  }
}

void BM_StrCat(benchmark::State& state) {
  const int chunks_at_a_time = state.range(0);
  const absl::string_view kChunk = "0123456789";

  switch (chunks_at_a_time) {
    case 1:
      return BM_StrCatImpl(state, kChunk);
    case 2:
      return BM_StrCatImpl(state, kChunk, kChunk);
    case 3:
      return BM_StrCatImpl(state, kChunk, kChunk, kChunk);
    case 4:
      return BM_StrCatImpl(state, kChunk, kChunk, kChunk, kChunk);
    default:
      std::abort();
  }
}

BENCHMARK(BM_StrCat)->Arg(1)->Arg(2)->Arg(3)->Arg(4);

void BM_StrCat_int(benchmark::State& state) {
  int i = 0;
  for (auto s : state) {
    std::string result = absl::StrCat(i);
    benchmark::DoNotOptimize(result);
    i = IncrementAlternatingSign(i);
  }
}

BENCHMARK(BM_StrCat_int);

}  // namespace
