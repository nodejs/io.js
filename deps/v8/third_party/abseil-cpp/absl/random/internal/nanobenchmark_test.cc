// Copyright 2017 Google Inc. All Rights Reserved.
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

#include "absl/random/internal/nanobenchmark.h"

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_format.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal_nanobenchmark {
namespace {

uint64_t Div(const void*, FuncInput in) {
  // Here we're measuring the throughput because benchmark invocations are
  // independent.
  const int64_t d1 = 0xFFFFFFFFFFll / int64_t(in);  // IDIV
  return d1;
}

template <size_t N>
void MeasureDiv(const FuncInput (&inputs)[N]) {
  Result results[N];
  Params params;
  params.max_evals = 6;  // avoid test timeout
  const size_t num_results = Measure(&Div, nullptr, inputs, N, results, params);
  if (num_results == 0) {
    LOG(WARNING)
        << "WARNING: Measurement failed, should not happen when using "
           "PinThreadToCPU unless the region to measure takes > 1 second.";
    return;
  }
  for (size_t i = 0; i < num_results; ++i) {
    LOG(INFO) << absl::StreamFormat("%5u: %6.2f ticks; MAD=%4.2f%%\n",
                                    results[i].input, results[i].ticks,
                                    results[i].variability * 100.0);
    CHECK_NE(results[i].ticks, 0.0f) << "Zero duration";
  }
}

void RunAll(const int argc, char* argv[]) {
  // Avoid migrating between cores - important on multi-socket systems.
  int cpu = -1;
  if (argc == 2) {
    if (!absl::SimpleAtoi(argv[1], &cpu)) {
      LOG(FATAL) << "The optional argument must be a CPU number >= 0.";
    }
  }
  PinThreadToCPU(cpu);

  // unpredictable == 1 but the compiler doesn't know that.
  const FuncInput unpredictable = argc != 999;
  static const FuncInput inputs[] = {unpredictable * 10, unpredictable * 100};

  MeasureDiv(inputs);
}

}  // namespace
}  // namespace random_internal_nanobenchmark
ABSL_NAMESPACE_END
}  // namespace absl

int main(int argc, char* argv[]) {
  absl::random_internal_nanobenchmark::RunAll(argc, argv);
  return 0;
}
