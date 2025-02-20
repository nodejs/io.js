// Copyright 2019 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "hwy/nanobenchmark.h"

#include <stddef.h>
#include <stdio.h>

#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util-inl.h"

namespace hwy {
namespace {

// Governs duration of test; avoid timeout in debug builds.
#if HWY_IS_DEBUG_BUILD
constexpr size_t kMaxEvals = 3;
#else
constexpr size_t kMaxEvals = 4;
#endif

FuncOutput Div(const void*, FuncInput in) {
  // Here we're measuring the throughput because benchmark invocations are
  // independent. Any dividend will do; the divisor is nonzero.
  return 0xFFFFF / in;
}

template <size_t N>
void MeasureDiv(const FuncInput (&inputs)[N]) {
  printf("Measuring integer division (output on final two lines)\n");
  Result results[N];
  Params params;
  params.max_evals = kMaxEvals;
  const size_t num_results = Measure(&Div, nullptr, inputs, N, results, params);
  for (size_t i = 0; i < num_results; ++i) {
    printf("%5d: %6.2f ticks; MAD=%4.2f%%\n",
           static_cast<int>(results[i].input), results[i].ticks,
           results[i].variability * 100.0);
  }
}

RandomState rng;

// A function whose runtime depends on rng.
FuncOutput Random(const void* /*arg*/, FuncInput in) {
  const size_t r = rng() & 0xF;
  FuncOutput ret = static_cast<FuncOutput>(in);
  for (size_t i = 0; i < r; ++i) {
    ret /= ((rng() & 1) + 2);
  }
  return ret;
}

// Ensure the measured variability is high.
template <size_t N>
void MeasureRandom(const FuncInput (&inputs)[N]) {
  Result results[N];
  Params p;
  p.max_evals = kMaxEvals;
  p.verbose = false;
  const size_t num_results = Measure(&Random, nullptr, inputs, N, results, p);
  for (size_t i = 0; i < num_results; ++i) {
    NANOBENCHMARK_CHECK(results[i].variability > 1E-3);
  }
}

TEST(NanobenchmarkTest, RunTest) {
  const int unpredictable = Unpredictable1();  // == 1, unknown to compiler.
  static const FuncInput inputs[] = {static_cast<FuncInput>(unpredictable) + 2,
                                     static_cast<FuncInput>(unpredictable + 9)};

  MeasureDiv(inputs);
  MeasureRandom(inputs);
}

}  // namespace
}  // namespace hwy

HWY_TEST_MAIN();
