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

#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

#include "benchmark/benchmark.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"
#include "absl/strings/numbers.h"
#include "absl/strings/string_view.h"

namespace {

template <typename T>
void BM_FastIntToBuffer(benchmark::State& state) {
  const int inc = state.range(0);
  char buf[absl::numbers_internal::kFastToBufferSize];
  // Use the unsigned type to increment to take advantage of well-defined
  // modular arithmetic.
  typename std::make_unsigned<T>::type x = 0;
  for (auto _ : state) {
    absl::numbers_internal::FastIntToBuffer(static_cast<T>(x), buf);
    x += inc;
  }
}
BENCHMARK_TEMPLATE(BM_FastIntToBuffer, int32_t)->Range(0, 1 << 15);
BENCHMARK_TEMPLATE(BM_FastIntToBuffer, int64_t)->Range(0, 1 << 30);

// Creates an integer that would be printed as `num_digits` repeated 7s in the
// given `base`. `base` must be greater than or equal to 8.
int64_t RepeatedSevens(int num_digits, int base) {
  ABSL_RAW_CHECK(base >= 8, "");
  int64_t num = 7;
  while (--num_digits) num = base * num + 7;
  return num;
}

void BM_safe_strto32_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str(digits, '7');  // valid in octal, decimal and hex
  int32_t value = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        absl::numbers_internal::safe_strto32_base(str, &value, base));
  }
  ABSL_RAW_CHECK(value == RepeatedSevens(digits, base), "");
}
BENCHMARK(BM_safe_strto32_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(1, 16)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(2, 16)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(4, 16)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(8, 16)
    ->ArgPair(10, 8)
    ->ArgPair(9, 10);

void BM_safe_strto64_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str(digits, '7');  // valid in octal, decimal and hex
  int64_t value = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        absl::numbers_internal::safe_strto64_base(str, &value, base));
  }
  ABSL_RAW_CHECK(value == RepeatedSevens(digits, base), "");
}
BENCHMARK(BM_safe_strto64_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(1, 16)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(2, 16)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(4, 16)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(8, 16)
    ->ArgPair(16, 8)
    ->ArgPair(16, 10)
    ->ArgPair(16, 16);

void BM_safe_strtou32_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str(digits, '7');  // valid in octal, decimal and hex
  uint32_t value = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        absl::numbers_internal::safe_strtou32_base(str, &value, base));
  }
  ABSL_RAW_CHECK(value == RepeatedSevens(digits, base), "");
}
BENCHMARK(BM_safe_strtou32_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(1, 16)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(2, 16)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(4, 16)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(8, 16)
    ->ArgPair(10, 8)
    ->ArgPair(9, 10);

void BM_safe_strtou64_string(benchmark::State& state) {
  const int digits = state.range(0);
  const int base = state.range(1);
  std::string str(digits, '7');  // valid in octal, decimal and hex
  uint64_t value = 0;
  for (auto _ : state) {
    benchmark::DoNotOptimize(
        absl::numbers_internal::safe_strtou64_base(str, &value, base));
  }
  ABSL_RAW_CHECK(value == RepeatedSevens(digits, base), "");
}
BENCHMARK(BM_safe_strtou64_string)
    ->ArgPair(1, 8)
    ->ArgPair(1, 10)
    ->ArgPair(1, 16)
    ->ArgPair(2, 8)
    ->ArgPair(2, 10)
    ->ArgPair(2, 16)
    ->ArgPair(4, 8)
    ->ArgPair(4, 10)
    ->ArgPair(4, 16)
    ->ArgPair(8, 8)
    ->ArgPair(8, 10)
    ->ArgPair(8, 16)
    ->ArgPair(16, 8)
    ->ArgPair(16, 10)
    ->ArgPair(16, 16);

// Returns a vector of `num_strings` strings. Each string represents a
// floating point number with `num_digits` digits before the decimal point and
// another `num_digits` digits after.
std::vector<std::string> MakeFloatStrings(int num_strings, int num_digits) {
  // For convenience, use a random number generator to generate the test data.
  // We don't actually need random properties, so use a fixed seed.
  std::minstd_rand0 rng(1);
  std::uniform_int_distribution<int> random_digit('0', '9');

  std::vector<std::string> float_strings(num_strings);
  for (std::string& s : float_strings) {
    s.reserve(2 * num_digits + 1);
    for (int i = 0; i < num_digits; ++i) {
      s.push_back(static_cast<char>(random_digit(rng)));
    }
    s.push_back('.');
    for (int i = 0; i < num_digits; ++i) {
      s.push_back(static_cast<char>(random_digit(rng)));
    }
  }
  return float_strings;
}

template <typename StringType>
StringType GetStringAs(const std::string& s) {
  return static_cast<StringType>(s);
}
template <>
const char* GetStringAs<const char*>(const std::string& s) {
  return s.c_str();
}

template <typename StringType>
std::vector<StringType> GetStringsAs(const std::vector<std::string>& strings) {
  std::vector<StringType> result;
  result.reserve(strings.size());
  for (const std::string& s : strings) {
    result.push_back(GetStringAs<StringType>(s));
  }
  return result;
}

template <typename T>
void BM_SimpleAtof(benchmark::State& state) {
  const int num_strings = state.range(0);
  const int num_digits = state.range(1);
  std::vector<std::string> backing_strings =
      MakeFloatStrings(num_strings, num_digits);
  std::vector<T> inputs = GetStringsAs<T>(backing_strings);
  float value;
  for (auto _ : state) {
    for (const T& input : inputs) {
      benchmark::DoNotOptimize(absl::SimpleAtof(input, &value));
    }
  }
}
BENCHMARK_TEMPLATE(BM_SimpleAtof, absl::string_view)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);
BENCHMARK_TEMPLATE(BM_SimpleAtof, const char*)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);
BENCHMARK_TEMPLATE(BM_SimpleAtof, std::string)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);

template <typename T>
void BM_SimpleAtod(benchmark::State& state) {
  const int num_strings = state.range(0);
  const int num_digits = state.range(1);
  std::vector<std::string> backing_strings =
      MakeFloatStrings(num_strings, num_digits);
  std::vector<T> inputs = GetStringsAs<T>(backing_strings);
  double value;
  for (auto _ : state) {
    for (const T& input : inputs) {
      benchmark::DoNotOptimize(absl::SimpleAtod(input, &value));
    }
  }
}
BENCHMARK_TEMPLATE(BM_SimpleAtod, absl::string_view)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);
BENCHMARK_TEMPLATE(BM_SimpleAtod, const char*)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);
BENCHMARK_TEMPLATE(BM_SimpleAtod, std::string)
    ->ArgPair(10, 1)
    ->ArgPair(10, 2)
    ->ArgPair(10, 4)
    ->ArgPair(10, 8);

void BM_FastHexToBufferZeroPad16(benchmark::State& state) {
  absl::BitGen rng;
  std::vector<uint64_t> nums;
  nums.resize(1000);
  auto min = std::numeric_limits<uint64_t>::min();
  auto max = std::numeric_limits<uint64_t>::max();
  for (auto& num : nums) {
    num = absl::LogUniform(rng, min, max);
  }

  char buf[16];
  while (state.KeepRunningBatch(nums.size())) {
    for (auto num : nums) {
      auto digits = absl::numbers_internal::FastHexToBufferZeroPad16(num, buf);
      benchmark::DoNotOptimize(digits);
      benchmark::DoNotOptimize(buf);
    }
  }
}
BENCHMARK(BM_FastHexToBufferZeroPad16);

}  // namespace
