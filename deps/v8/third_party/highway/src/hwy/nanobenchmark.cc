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

#include <stdio.h>
#include <stdlib.h>
#include <time.h>  // clock_gettime

#include <algorithm>  // std::sort, std::find_if
#include <numeric>    // std::iota
#include <random>
#include <vector>

#include "hwy/robust_statistics.h"
#include "hwy/timer-inl.h"
#include "hwy/timer.h"

namespace hwy {
namespace {
namespace timer = hwy::HWY_NAMESPACE::timer;

static const timer::Ticks timer_resolution = platform::TimerResolution();

// Estimates the expected value of "lambda" values with a variable number of
// samples until the variability "rel_mad" is less than "max_rel_mad".
template <class Lambda>
timer::Ticks SampleUntilStable(const double max_rel_mad, double* rel_mad,
                               const Params& p, const Lambda& lambda) {
  // Choose initial samples_per_eval based on a single estimated duration.
  timer::Ticks t0 = timer::Start();
  lambda();
  timer::Ticks t1 = timer::Stop();  // Caller checks HaveTimerStop
  timer::Ticks est = t1 - t0;
  static const double ticks_per_second = platform::InvariantTicksPerSecond();
  const size_t ticks_per_eval =
      static_cast<size_t>(ticks_per_second * p.seconds_per_eval);
  size_t samples_per_eval = est == 0
                                ? p.min_samples_per_eval
                                : static_cast<size_t>(ticks_per_eval / est);
  samples_per_eval = HWY_MAX(samples_per_eval, p.min_samples_per_eval);

  std::vector<timer::Ticks> samples;
  samples.reserve(1 + samples_per_eval);
  samples.push_back(est);

  // Percentage is too strict for tiny differences, so also allow a small
  // absolute "median absolute deviation".
  const timer::Ticks max_abs_mad = (timer_resolution + 99) / 100;
  *rel_mad = 0.0;  // ensure initialized

  for (size_t eval = 0; eval < p.max_evals; ++eval, samples_per_eval *= 2) {
    samples.reserve(samples.size() + samples_per_eval);
    for (size_t i = 0; i < samples_per_eval; ++i) {
      t0 = timer::Start();
      lambda();
      t1 = timer::Stop();  // Caller checks HaveTimerStop
      samples.push_back(t1 - t0);
    }

    if (samples.size() >= p.min_mode_samples) {
      est = robust_statistics::Mode(samples.data(), samples.size());
    } else {
      // For "few" (depends also on the variance) samples, Median is safer.
      est = robust_statistics::Median(samples.data(), samples.size());
    }
    NANOBENCHMARK_CHECK(est != 0);

    // Median absolute deviation (mad) is a robust measure of 'variability'.
    const timer::Ticks abs_mad = robust_statistics::MedianAbsoluteDeviation(
        samples.data(), samples.size(), est);
    *rel_mad = static_cast<double>(abs_mad) / static_cast<double>(est);

    if (*rel_mad <= max_rel_mad || abs_mad <= max_abs_mad) {
      if (p.verbose) {
        printf("%6d samples => %5d (abs_mad=%4d, rel_mad=%4.2f%%)\n",
               static_cast<int>(samples.size()), static_cast<int>(est),
               static_cast<int>(abs_mad), *rel_mad * 100.0);
      }
      return est;
    }
  }

  if (p.verbose) {
    printf("WARNING: rel_mad=%4.2f%% still exceeds %4.2f%% after %6d samples\n",
           *rel_mad * 100.0, max_rel_mad * 100.0,
           static_cast<int>(samples.size()));
  }
  return est;
}

using InputVec = std::vector<FuncInput>;

// Returns vector of unique input values.
InputVec UniqueInputs(const FuncInput* inputs, const size_t num_inputs) {
  InputVec unique(inputs, inputs + num_inputs);
  std::sort(unique.begin(), unique.end());
  unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
  return unique;
}

// Returns how often we need to call func for sufficient precision.
size_t NumSkip(const Func func, const uint8_t* arg, const InputVec& unique,
               const Params& p) {
  // Min elapsed ticks for any input.
  timer::Ticks min_duration = ~timer::Ticks(0);

  for (const FuncInput input : unique) {
    double rel_mad;
    const timer::Ticks total = SampleUntilStable(
        p.target_rel_mad, &rel_mad, p,
        [func, arg, input]() { PreventElision(func(arg, input)); });
    min_duration = HWY_MIN(min_duration, total - timer_resolution);
  }

  // Number of repetitions required to reach the target resolution.
  const size_t max_skip = p.precision_divisor;
  // Number of repetitions given the estimated duration.
  const size_t num_skip =
      min_duration == 0
          ? 0
          : static_cast<size_t>((max_skip + min_duration - 1) / min_duration);
  if (p.verbose) {
    printf("res=%d max_skip=%d min_dur=%d num_skip=%d\n",
           static_cast<int>(timer_resolution), static_cast<int>(max_skip),
           static_cast<int>(min_duration), static_cast<int>(num_skip));
  }
  return num_skip;
}

// Replicates inputs until we can omit "num_skip" occurrences of an input.
InputVec ReplicateInputs(const FuncInput* inputs, const size_t num_inputs,
                         const size_t num_unique, const size_t num_skip,
                         const Params& p) {
  InputVec full;
  if (num_unique == 1) {
    full.assign(p.subset_ratio * num_skip, inputs[0]);
    return full;
  }

  full.reserve(p.subset_ratio * num_skip * num_inputs);
  for (size_t i = 0; i < p.subset_ratio * num_skip; ++i) {
    full.insert(full.end(), inputs, inputs + num_inputs);
  }
  std::mt19937 rng;
  std::shuffle(full.begin(), full.end(), rng);
  return full;
}

// Copies the "full" to "subset" in the same order, but with "num_skip"
// randomly selected occurrences of "input_to_skip" removed.
void FillSubset(const InputVec& full, const FuncInput input_to_skip,
                const size_t num_skip, InputVec* subset) {
  const size_t count =
      static_cast<size_t>(std::count(full.begin(), full.end(), input_to_skip));
  // Generate num_skip random indices: which occurrence to skip.
  std::vector<uint32_t> omit(count);
  std::iota(omit.begin(), omit.end(), 0);
  // omit[] is the same on every call, but that's OK because they identify the
  // Nth instance of input_to_skip, so the position within full[] differs.
  std::mt19937 rng;
  std::shuffle(omit.begin(), omit.end(), rng);
  omit.resize(num_skip);
  std::sort(omit.begin(), omit.end());

  uint32_t occurrence = ~0u;  // 0 after preincrement
  size_t idx_omit = 0;        // cursor within omit[]
  size_t idx_subset = 0;      // cursor within *subset
  for (const FuncInput next : full) {
    if (next == input_to_skip) {
      ++occurrence;
      // Haven't removed enough already
      if (idx_omit < num_skip) {
        // This one is up for removal
        if (occurrence == omit[idx_omit]) {
          ++idx_omit;
          continue;
        }
      }
    }
    if (idx_subset < subset->size()) {
      (*subset)[idx_subset++] = next;
    }
  }
  NANOBENCHMARK_CHECK(idx_subset == subset->size());
  NANOBENCHMARK_CHECK(idx_omit == omit.size());
  NANOBENCHMARK_CHECK(occurrence == count - 1);
}

// Returns total ticks elapsed for all inputs.
timer::Ticks TotalDuration(const Func func, const uint8_t* arg,
                           const InputVec* inputs, const Params& p,
                           double* max_rel_mad) {
  double rel_mad;
  const timer::Ticks duration =
      SampleUntilStable(p.target_rel_mad, &rel_mad, p, [func, arg, inputs]() {
        for (const FuncInput input : *inputs) {
          PreventElision(func(arg, input));
        }
      });
  *max_rel_mad = HWY_MAX(*max_rel_mad, rel_mad);
  return duration;
}

// (Nearly) empty Func for measuring timer overhead/resolution.
HWY_NOINLINE FuncOutput EmptyFunc(const void* /*arg*/, const FuncInput input) {
  return input;
}

// Returns overhead of accessing inputs[] and calling a function; this will
// be deducted from future TotalDuration return values.
timer::Ticks Overhead(const uint8_t* arg, const InputVec* inputs,
                      const Params& p) {
  double rel_mad;
  // Zero tolerance because repeatability is crucial and EmptyFunc is fast.
  return SampleUntilStable(0.0, &rel_mad, p, [arg, inputs]() {
    for (const FuncInput input : *inputs) {
      PreventElision(EmptyFunc(arg, input));
    }
  });
}

}  // namespace

HWY_DLLEXPORT int Unpredictable1() { return timer::Start() != ~0ULL; }

HWY_DLLEXPORT size_t Measure(const Func func, const uint8_t* arg,
                             const FuncInput* inputs, const size_t num_inputs,
                             Result* results, const Params& p) {
  NANOBENCHMARK_CHECK(num_inputs != 0);

  char cpu100[100];
  if (!platform::HaveTimerStop(cpu100)) {
    fprintf(stderr, "CPU '%s' does not support RDTSCP, skipping benchmark.\n",
            cpu100);
    return 0;
  }

  const InputVec& unique = UniqueInputs(inputs, num_inputs);

  const size_t num_skip = NumSkip(func, arg, unique, p);  // never 0
  if (num_skip == 0) return 0;  // NumSkip already printed error message
  // (slightly less work on x86 to cast from signed integer)
  const float mul = 1.0f / static_cast<float>(static_cast<int>(num_skip));

  const InputVec& full =
      ReplicateInputs(inputs, num_inputs, unique.size(), num_skip, p);
  InputVec subset(full.size() - num_skip);

  const timer::Ticks overhead = Overhead(arg, &full, p);
  const timer::Ticks overhead_skip = Overhead(arg, &subset, p);
  if (overhead < overhead_skip) {
    fprintf(stderr, "Measurement failed: overhead %d < %d\n",
            static_cast<int>(overhead), static_cast<int>(overhead_skip));
    return 0;
  }

  if (p.verbose) {
    printf("#inputs=%5d,%5d overhead=%5d,%5d\n", static_cast<int>(full.size()),
           static_cast<int>(subset.size()), static_cast<int>(overhead),
           static_cast<int>(overhead_skip));
  }

  double max_rel_mad = 0.0;
  const timer::Ticks total = TotalDuration(func, arg, &full, p, &max_rel_mad);

  for (size_t i = 0; i < unique.size(); ++i) {
    FillSubset(full, unique[i], num_skip, &subset);
    const timer::Ticks total_skip =
        TotalDuration(func, arg, &subset, p, &max_rel_mad);

    if (total < total_skip) {
      fprintf(stderr, "Measurement failed: total %f < %f\n",
              static_cast<double>(total), static_cast<double>(total_skip));
      return 0;
    }

    const timer::Ticks duration =
        (total - overhead) - (total_skip - overhead_skip);
    results[i].input = unique[i];
    results[i].ticks = static_cast<float>(duration) * mul;
    results[i].variability = static_cast<float>(max_rel_mad);
  }

  return unique.size();
}

}  // namespace hwy
