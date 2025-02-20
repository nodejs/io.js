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

#include "absl/strings/escaping.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>

#include "benchmark/benchmark.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/internal/escaping_test_common.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace {

void BM_CUnescapeHexString(benchmark::State& state) {
  std::string src;
  for (int i = 0; i < 50; i++) {
    src += "\\x55";
  }
  for (auto _ : state) {
    std::string dest;
    benchmark::DoNotOptimize(src);
    bool result = absl::CUnescape(src, &dest);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(dest);
  }
}
BENCHMARK(BM_CUnescapeHexString);

void BM_WebSafeBase64Escape_string(benchmark::State& state) {
  std::string raw;
  for (int i = 0; i < 10; ++i) {
    for (const auto& test_set : absl::strings_internal::base64_strings()) {
      raw += std::string(test_set.plaintext);
    }
  }
  for (auto _ : state) {
    std::string escaped;
    benchmark::DoNotOptimize(raw);
    absl::WebSafeBase64Escape(raw, &escaped);
    benchmark::DoNotOptimize(escaped);
  }
}
BENCHMARK(BM_WebSafeBase64Escape_string);

void BM_HexStringToBytes(benchmark::State& state) {
  const int size = state.range(0);
  std::string input, output;
  for (int i = 0; i < size; ++i) input += "1c";
  for (auto _ : state) {
    benchmark::DoNotOptimize(input);
    bool result = absl::HexStringToBytes(input, &output);
    benchmark::DoNotOptimize(result);
    benchmark::DoNotOptimize(output);
  }
}
BENCHMARK(BM_HexStringToBytes)->Range(1, 1 << 8);

void BM_HexStringToBytes_Fail(benchmark::State& state) {
  std::string binary;
  absl::string_view hex_input1 = "1c2f003";
  absl::string_view hex_input2 = "1c2f0032f40123456789abcdef**";
  for (auto _ : state) {
    benchmark::DoNotOptimize(hex_input1);
    bool result1 = absl::HexStringToBytes(hex_input1, &binary);
    benchmark::DoNotOptimize(result1);
    benchmark::DoNotOptimize(binary);
    benchmark::DoNotOptimize(hex_input2);
    bool result2 = absl::HexStringToBytes(hex_input2, &binary);
    benchmark::DoNotOptimize(result2);
    benchmark::DoNotOptimize(binary);
  }
}
BENCHMARK(BM_HexStringToBytes_Fail);

// Used for the CEscape benchmarks
const char kStringValueNoEscape[] = "1234567890";
const char kStringValueSomeEscaped[] = "123\n56789\xA1";
const char kStringValueMostEscaped[] = "\xA1\xA2\ny\xA4\xA5\xA6z\b\r";

void CEscapeBenchmarkHelper(benchmark::State& state, const char* string_value,
                            int max_len) {
  std::string src;
  while (src.size() < max_len) {
    absl::StrAppend(&src, string_value);
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(src);
    std::string result = absl::CEscape(src);
    benchmark::DoNotOptimize(result);
  }
}

void BM_CEscape_NoEscape(benchmark::State& state) {
  CEscapeBenchmarkHelper(state, kStringValueNoEscape, state.range(0));
}
BENCHMARK(BM_CEscape_NoEscape)->Range(1, 1 << 14);

void BM_CEscape_SomeEscaped(benchmark::State& state) {
  CEscapeBenchmarkHelper(state, kStringValueSomeEscaped, state.range(0));
}
BENCHMARK(BM_CEscape_SomeEscaped)->Range(1, 1 << 14);

void BM_CEscape_MostEscaped(benchmark::State& state) {
  CEscapeBenchmarkHelper(state, kStringValueMostEscaped, state.range(0));
}
BENCHMARK(BM_CEscape_MostEscaped)->Range(1, 1 << 14);

}  // namespace
