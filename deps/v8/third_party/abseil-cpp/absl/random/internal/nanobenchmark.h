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

#ifndef ABSL_RANDOM_INTERNAL_NANOBENCHMARK_H_
#define ABSL_RANDOM_INTERNAL_NANOBENCHMARK_H_

// Benchmarks functions of a single integer argument with realistic branch
// prediction hit rates. Uses a robust estimator to summarize the measurements.
// The precision is about 0.2%.
//
// Examples: see nanobenchmark_test.cc.
//
// Background: Microbenchmarks such as http://github.com/google/benchmark
// can measure elapsed times on the order of a microsecond. Shorter functions
// are typically measured by repeating them thousands of times and dividing
// the total elapsed time by this count. Unfortunately, repetition (especially
// with the same input parameter!) influences the runtime. In time-critical
// code, it is reasonable to expect warm instruction/data caches and TLBs,
// but a perfect record of which branches will be taken is unrealistic.
// Unless the application also repeatedly invokes the measured function with
// the same parameter, the benchmark is measuring something very different -
// a best-case result, almost as if the parameter were made a compile-time
// constant. This may lead to erroneous conclusions about branch-heavy
// algorithms outperforming branch-free alternatives.
//
// Our approach differs in three ways. Adding fences to the timer functions
// reduces variability due to instruction reordering, improving the timer
// resolution to about 40 CPU cycles. However, shorter functions must still
// be invoked repeatedly. For more realistic branch prediction performance,
// we vary the input parameter according to a user-specified distribution.
// Thus, instead of VaryInputs(Measure(Repeat(func))), we change the
// loop nesting to Measure(Repeat(VaryInputs(func))). We also estimate the
// central tendency of the measurement samples with the "half sample mode",
// which is more robust to outliers and skewed data than the mean or median.

// NOTE: for compatibility with multiple translation units compiled with
// distinct flags, avoid #including headers that define functions.

#include <stddef.h>
#include <stdint.h>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal_nanobenchmark {

// Input influencing the function being measured (e.g. number of bytes to copy).
using FuncInput = size_t;

// "Proof of work" returned by Func to ensure the compiler does not elide it.
using FuncOutput = uint64_t;

// Function to measure: either 1) a captureless lambda or function with two
// arguments or 2) a lambda with capture, in which case the first argument
// is reserved for use by MeasureClosure.
using Func = FuncOutput (*)(const void*, FuncInput);

// Internal parameters that determine precision/resolution/measuring time.
struct Params {
  // For measuring timer overhead/resolution. Used in a nested loop =>
  // quadratic time, acceptable because we know timer overhead is "low".
  // constexpr because this is used to define array bounds.
  static constexpr size_t kTimerSamples = 256;

  // Best-case precision, expressed as a divisor of the timer resolution.
  // Larger => more calls to Func and higher precision.
  size_t precision_divisor = 1024;

  // Ratio between full and subset input distribution sizes. Cannot be less
  // than 2; larger values increase measurement time but more faithfully
  // model the given input distribution.
  size_t subset_ratio = 2;

  // Together with the estimated Func duration, determines how many times to
  // call Func before checking the sample variability. Larger values increase
  // measurement time, memory/cache use and precision.
  double seconds_per_eval = 4E-3;

  // The minimum number of samples before estimating the central tendency.
  size_t min_samples_per_eval = 7;

  // The mode is better than median for estimating the central tendency of
  // skewed/fat-tailed distributions, but it requires sufficient samples
  // relative to the width of half-ranges.
  size_t min_mode_samples = 64;

  // Maximum permissible variability (= median absolute deviation / center).
  double target_rel_mad = 0.002;

  // Abort after this many evals without reaching target_rel_mad. This
  // prevents infinite loops.
  size_t max_evals = 9;

  // Retry the measure loop up to this many times.
  size_t max_measure_retries = 2;

  // Whether to print additional statistics to stdout.
  bool verbose = true;
};

// Measurement result for each unique input.
struct Result {
  FuncInput input;

  // Robust estimate (mode or median) of duration.
  float ticks;

  // Measure of variability (median absolute deviation relative to "ticks").
  float variability;
};

// Ensures the thread is running on the specified cpu, and no others.
// Reduces noise due to desynchronized socket RDTSC and context switches.
// If "cpu" is negative, pin to the currently running core.
void PinThreadToCPU(const int cpu = -1);

// Returns tick rate, useful for converting measurements to seconds. Invariant
// means the tick counter frequency is independent of CPU throttling or sleep.
// This call may be expensive, callers should cache the result.
double InvariantTicksPerSecond();

// Precisely measures the number of ticks elapsed when calling "func" with the
// given inputs, shuffled to ensure realistic branch prediction hit rates.
//
// "func" returns a 'proof of work' to ensure its computations are not elided.
// "arg" is passed to Func, or reserved for internal use by MeasureClosure.
// "inputs" is an array of "num_inputs" (not necessarily unique) arguments to
//   "func". The values should be chosen to maximize coverage of "func". This
//   represents a distribution, so a value's frequency should reflect its
//   probability in the real application. Order does not matter; for example, a
//   uniform distribution over [0, 4) could be represented as {3,0,2,1}.
// Returns how many Result were written to "results": one per unique input, or
//   zero if the measurement failed (an error message goes to stderr).
size_t Measure(const Func func, const void* arg, const FuncInput* inputs,
               const size_t num_inputs, Result* results,
               const Params& p = Params());

// Calls operator() of the given closure (lambda function).
template <class Closure>
static FuncOutput CallClosure(const void* f, const FuncInput input) {
  return (*reinterpret_cast<const Closure*>(f))(input);
}

// Same as Measure, except "closure" is typically a lambda function of
// FuncInput -> FuncOutput with a capture list.
template <class Closure>
static inline size_t MeasureClosure(const Closure& closure,
                                    const FuncInput* inputs,
                                    const size_t num_inputs, Result* results,
                                    const Params& p = Params()) {
  return Measure(reinterpret_cast<Func>(&CallClosure<Closure>),
                 reinterpret_cast<const void*>(&closure), inputs, num_inputs,
                 results, p);
}

}  // namespace random_internal_nanobenchmark
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_NANOBENCHMARK_H_
