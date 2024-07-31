// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/cppgc/platform.h"
#include "test/benchmarks/cpp/cppgc/benchmark_utils.h"
#include "third_party/google_benchmark_chrome/src/include/benchmark/benchmark.h"

// Expanded macro BENCHMARK_MAIN() to allow per-process setup.
int main(int argc, char** argv) {
  cppgc::internal::testing::BenchmarkWithHeap::InitializeProcess();
  // Contents of BENCHMARK_MAIN().
  {
    ::benchmark::Initialize(&argc, argv);
    if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    ::benchmark::RunSpecifiedBenchmarks();
    ::benchmark::Shutdown();
  }
  cppgc::internal::testing::BenchmarkWithHeap::ShutdownProcess();
  return 0;
}
