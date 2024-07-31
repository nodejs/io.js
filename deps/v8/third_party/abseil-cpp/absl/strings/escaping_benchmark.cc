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
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>

#include "benchmark/benchmark.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/internal/escaping_test_common.h"
#include "absl/strings/str_cat.h"

namespace {

void BM_CUnescapeHexString(benchmark::State& state) {
  std::string src;
  for (int i = 0; i < 50; i++) {
    src += "\\x55";
  }
  std::string dest;
  for (auto _ : state) {
    absl::CUnescape(src, &dest);
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

  // The actual benchmark loop is tiny...
  std::string escaped;
  for (auto _ : state) {
    absl::WebSafeBase64Escape(raw, &escaped);
  }

  // We want to be sure the compiler doesn't throw away the loop above,
  // and the easiest way to ensure that is to round-trip the results and verify
  // them.
  std::string round_trip;
  absl::WebSafeBase64Unescape(escaped, &round_trip);
  ABSL_RAW_CHECK(round_trip == raw, "");
}
BENCHMARK(BM_WebSafeBase64Escape_string);

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
    absl::CEscape(src);
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
