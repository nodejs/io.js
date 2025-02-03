// Copyright 2022 The Abseil Authors.
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

#include "absl/base/attributes.h"
#include "absl/base/log_severity.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/internal/flags.h"
#include "absl/log/log.h"
#include "absl/log/log_entry.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/log/vlog_is_on.h"
#include "benchmark/benchmark.h"

namespace {

class NullLogSink : public absl::LogSink {
 public:
  NullLogSink() { absl::AddLogSink(this); }

  ~NullLogSink() override { absl::RemoveLogSink(this); }

  void Send(const absl::LogEntry&) override {}
};

constexpr int x = -1;

void BM_SuccessfulBinaryCheck(benchmark::State& state) {
  int n = 0;
  while (state.KeepRunningBatch(8)) {
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    CHECK_GE(n, x);
    ++n;
  }
  benchmark::DoNotOptimize(n);
}
BENCHMARK(BM_SuccessfulBinaryCheck);

static void BM_SuccessfulUnaryCheck(benchmark::State& state) {
  int n = 0;
  while (state.KeepRunningBatch(8)) {
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    CHECK(n >= x);
    ++n;
  }
  benchmark::DoNotOptimize(n);
}
BENCHMARK(BM_SuccessfulUnaryCheck);

static void BM_DisabledLogOverhead(benchmark::State& state) {
  absl::ScopedStderrThreshold disable_stderr_logging(
      absl::LogSeverityAtLeast::kInfinity);
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(
      absl::LogSeverityAtLeast::kInfinity);
  for (auto _ : state) {
    LOG(INFO);
  }
}
BENCHMARK(BM_DisabledLogOverhead);

static void BM_EnabledLogOverhead(benchmark::State& state) {
  absl::ScopedStderrThreshold stderr_logging(
      absl::LogSeverityAtLeast::kInfinity);
  absl::log_internal::ScopedMinLogLevel scoped_min_log_level(
      absl::LogSeverityAtLeast::kInfo);
  ABSL_ATTRIBUTE_UNUSED NullLogSink null_sink;
  for (auto _ : state) {
    LOG(INFO);
  }
}
BENCHMARK(BM_EnabledLogOverhead);

static void BM_VlogIsOnOverhead(benchmark::State& state) {
  // It would make sense to do this only when state.thread_index == 0,
  // but thread_index is an int on some platforms (e.g. Android) and a
  // function returning an int on others. So we just do it on all threads.
  // TODO(b/152609127): set only if thread_index == 0.
  absl::SetFlag(&FLAGS_v, 0);

  while (state.KeepRunningBatch(10)) {
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 1
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 2
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 3
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 4
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 5
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 6
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 7
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 8
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 9
    benchmark::DoNotOptimize(VLOG_IS_ON(0));  // 10
  }
}
BENCHMARK(BM_VlogIsOnOverhead)->ThreadRange(1, 64);

static void BM_VlogIsNotOnOverhead(benchmark::State& state) {
  // It would make sense to do this only when state.thread_index == 0,
  // but thread_index is an int on some platforms (e.g. Android) and a
  // function returning an int on others. So we just do it on all threads.
  // TODO(b/152609127): set only if thread_index == 0.
  absl::SetFlag(&FLAGS_v, 0);

  while (state.KeepRunningBatch(10)) {
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 1
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 2
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 3
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 4
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 5
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 6
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 7
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 8
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 9
    benchmark::DoNotOptimize(VLOG_IS_ON(1));  // 10
  }
}
BENCHMARK(BM_VlogIsNotOnOverhead)->ThreadRange(1, 64);

static void BM_LogEveryNOverhead(benchmark::State& state) {
  absl::ScopedStderrThreshold disable_stderr_logging(
      absl::LogSeverityAtLeast::kInfinity);
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfinity);
  ABSL_ATTRIBUTE_UNUSED NullLogSink null_sink;

  while (state.KeepRunningBatch(10)) {
    LOG_EVERY_N_SEC(INFO, 10);
    LOG_EVERY_N_SEC(INFO, 20);
    LOG_EVERY_N_SEC(INFO, 30);
    LOG_EVERY_N_SEC(INFO, 40);
    LOG_EVERY_N_SEC(INFO, 50);
    LOG_EVERY_N_SEC(INFO, 60);
    LOG_EVERY_N_SEC(INFO, 70);
    LOG_EVERY_N_SEC(INFO, 80);
    LOG_EVERY_N_SEC(INFO, 90);
    LOG_EVERY_N_SEC(INFO, 100);
  }
}
BENCHMARK(BM_LogEveryNOverhead)->ThreadRange(1, 64);

}  // namespace

