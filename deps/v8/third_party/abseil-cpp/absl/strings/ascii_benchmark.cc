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

#include "absl/strings/ascii.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <array>
#include <random>

#include "benchmark/benchmark.h"

namespace {

std::array<unsigned char, 256> MakeShuffledBytes() {
  std::array<unsigned char, 256> bytes;
  for (size_t i = 0; i < 256; ++i) bytes[i] = static_cast<unsigned char>(i);
  std::random_device rd;
  std::seed_seq seed({rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()});
  std::mt19937 g(seed);
  std::shuffle(bytes.begin(), bytes.end(), g);
  return bytes;
}

template <typename Function>
void AsciiBenchmark(benchmark::State& state, Function f) {
  std::array<unsigned char, 256> bytes = MakeShuffledBytes();
  size_t sum = 0;
  for (auto _ : state) {
    for (unsigned char b : bytes) sum += f(b) ? 1 : 0;
  }
  // Make a copy of `sum` before calling `DoNotOptimize` to make sure that `sum`
  // can be put in a CPU register and not degrade performance in the loop above.
  size_t sum2 = sum;
  benchmark::DoNotOptimize(sum2);
  state.SetBytesProcessed(state.iterations() * bytes.size());
}

using StdAsciiFunction = int (*)(int);
template <StdAsciiFunction f>
void BM_Ascii(benchmark::State& state) {
  AsciiBenchmark(state, f);
}

using AbslAsciiIsFunction = bool (*)(unsigned char);
template <AbslAsciiIsFunction f>
void BM_Ascii(benchmark::State& state) {
  AsciiBenchmark(state, f);
}

using AbslAsciiToFunction = char (*)(unsigned char);
template <AbslAsciiToFunction f>
void BM_Ascii(benchmark::State& state) {
  AsciiBenchmark(state, f);
}

inline char Noop(unsigned char b) { return static_cast<char>(b); }

BENCHMARK_TEMPLATE(BM_Ascii, Noop);
BENCHMARK_TEMPLATE(BM_Ascii, std::isalpha);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isalpha);
BENCHMARK_TEMPLATE(BM_Ascii, std::isdigit);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isdigit);
BENCHMARK_TEMPLATE(BM_Ascii, std::isalnum);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isalnum);
BENCHMARK_TEMPLATE(BM_Ascii, std::isspace);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isspace);
BENCHMARK_TEMPLATE(BM_Ascii, std::ispunct);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_ispunct);
BENCHMARK_TEMPLATE(BM_Ascii, std::isblank);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isblank);
BENCHMARK_TEMPLATE(BM_Ascii, std::iscntrl);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_iscntrl);
BENCHMARK_TEMPLATE(BM_Ascii, std::isxdigit);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isxdigit);
BENCHMARK_TEMPLATE(BM_Ascii, std::isprint);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isprint);
BENCHMARK_TEMPLATE(BM_Ascii, std::isgraph);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isgraph);
BENCHMARK_TEMPLATE(BM_Ascii, std::isupper);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isupper);
BENCHMARK_TEMPLATE(BM_Ascii, std::islower);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_islower);
BENCHMARK_TEMPLATE(BM_Ascii, isascii);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_isascii);
BENCHMARK_TEMPLATE(BM_Ascii, std::tolower);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_tolower);
BENCHMARK_TEMPLATE(BM_Ascii, std::toupper);
BENCHMARK_TEMPLATE(BM_Ascii, absl::ascii_toupper);

static void BM_StrToLower(benchmark::State& state) {
  const int size = state.range(0);
  std::string s(size, 'X');
  for (auto _ : state) {
    benchmark::DoNotOptimize(s);
    std::string res = absl::AsciiStrToLower(s);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_StrToLower)
    ->DenseRange(0, 32)
    ->RangeMultiplier(2)
    ->Range(64, 1 << 26);

static void BM_StrToUpper(benchmark::State& state) {
  const int size = state.range(0);
  std::string s(size, 'x');
  for (auto _ : state) {
    benchmark::DoNotOptimize(s);
    std::string res = absl::AsciiStrToUpper(s);
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_StrToUpper)
    ->DenseRange(0, 32)
    ->RangeMultiplier(2)
    ->Range(64, 1 << 26);

static void BM_StrToUpperFromRvalref(benchmark::State& state) {
  const size_t size = static_cast<size_t>(state.range(0));
  std::string s(size, 'X');
  for (auto _ : state) {
    benchmark::DoNotOptimize(s);
    std::string res = absl::AsciiStrToUpper(std::string(s));
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_StrToUpperFromRvalref)
    ->DenseRange(0, 32)
    ->RangeMultiplier(2)
    ->Range(64, 1 << 26);

static void BM_StrToLowerFromRvalref(benchmark::State& state) {
  const size_t size = static_cast<size_t>(state.range(0));
  std::string s(size, 'x');
  for (auto _ : state) {
    benchmark::DoNotOptimize(s);
    std::string res = absl::AsciiStrToLower(std::string(s));
    benchmark::DoNotOptimize(res);
  }
}
BENCHMARK(BM_StrToLowerFromRvalref)
    ->DenseRange(0, 32)
    ->RangeMultiplier(2)
    ->Range(64, 1 << 26);

}  // namespace
