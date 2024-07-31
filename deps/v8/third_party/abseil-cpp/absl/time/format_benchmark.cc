// Copyright 2018 The Abseil Authors.
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

#include <cstddef>
#include <string>

#include "absl/time/internal/test_util.h"
#include "absl/time/time.h"
#include "benchmark/benchmark.h"

namespace {

namespace {
const char* const kFormats[] = {
    absl::RFC1123_full,     // 0
    absl::RFC1123_no_wday,  // 1
    absl::RFC3339_full,     // 2
    absl::RFC3339_sec,      // 3
    "%Y-%m-%d%ET%H:%M:%S",  // 4
    "%Y-%m-%d",             // 5
};
const int kNumFormats = sizeof(kFormats) / sizeof(kFormats[0]);
}  // namespace

void BM_Format_FormatTime(benchmark::State& state) {
  const std::string fmt = kFormats[state.range(0)];
  state.SetLabel(fmt);
  const absl::TimeZone lax =
      absl::time_internal::LoadTimeZone("America/Los_Angeles");
  const absl::Time t =
      absl::FromCivil(absl::CivilSecond(1977, 6, 28, 9, 8, 7), lax) +
      absl::Nanoseconds(1);
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::FormatTime(fmt, t, lax).length());
  }
}
BENCHMARK(BM_Format_FormatTime)->DenseRange(0, kNumFormats - 1);

void BM_Format_ParseTime(benchmark::State& state) {
  const std::string fmt = kFormats[state.range(0)];
  state.SetLabel(fmt);
  const absl::TimeZone lax =
      absl::time_internal::LoadTimeZone("America/Los_Angeles");
  absl::Time t = absl::FromCivil(absl::CivilSecond(1977, 6, 28, 9, 8, 7), lax) +
                 absl::Nanoseconds(1);
  const std::string when = absl::FormatTime(fmt, t, lax);
  std::string err;
  while (state.KeepRunning()) {
    benchmark::DoNotOptimize(absl::ParseTime(fmt, when, lax, &t, &err));
  }
}
BENCHMARK(BM_Format_ParseTime)->DenseRange(0, kNumFormats - 1);

}  // namespace
