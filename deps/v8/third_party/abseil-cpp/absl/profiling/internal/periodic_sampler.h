// Copyright 2019 The Abseil Authors.
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

#ifndef ABSL_PROFILING_INTERNAL_PERIODIC_SAMPLER_H_
#define ABSL_PROFILING_INTERNAL_PERIODIC_SAMPLER_H_

#include <stdint.h>

#include <atomic>

#include "absl/base/optimization.h"
#include "absl/profiling/internal/exponential_biased.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

// PeriodicSamplerBase provides the basic period sampler implementation.
//
// This is the base class for the templated PeriodicSampler class, which holds
// a global std::atomic value identified by a user defined tag, such that
// each specific PeriodSampler implementation holds its own global period.
//
// PeriodicSamplerBase is thread-compatible except where stated otherwise.
class PeriodicSamplerBase {
 public:
  // PeriodicSamplerBase is trivial / copyable / movable / destructible.
  PeriodicSamplerBase() = default;
  PeriodicSamplerBase(PeriodicSamplerBase&&) = default;
  PeriodicSamplerBase(const PeriodicSamplerBase&) = default;

  // Returns true roughly once every `period` calls. This is established by a
  // randomly picked `stride` that is counted down on each call to `Sample`.
  // This stride is picked such that the probability of `Sample()` returning
  // true is 1 in `period`.
  inline bool Sample() noexcept;

  // The below methods are intended for optimized use cases where the
  // size of the inlined fast path code is highly important. Applications
  // should use the `Sample()` method unless they have proof that their
  // specific use case requires the optimizations offered by these methods.
  //
  // An example of such a use case is SwissTable sampling. All sampling checks
  // are in inlined SwissTable methods, and the number of call sites is huge.
  // In this case, the inlined code size added to each translation unit calling
  // SwissTable methods is non-trivial.
  //
  // The `SubtleMaybeSample()` function spuriously returns true even if the
  // function should not be sampled, applications MUST match each call to
  // 'SubtleMaybeSample()' returning true with a `SubtleConfirmSample()` call,
  // and use the result of the latter as the sampling decision.
  // In other words: the code should logically be equivalent to:
  //
  //    if (SubtleMaybeSample() && SubtleConfirmSample()) {
  //      // Sample this call
  //    }
  //
  // In the 'inline-size' optimized case, the `SubtleConfirmSample()` call can
  // be placed out of line, for example, the typical use case looks as follows:
  //
  //   // --- frobber.h -----------
  //   void FrobberSampled();
  //
  //   inline void FrobberImpl() {
  //     // ...
  //   }
  //
  //   inline void Frobber() {
  //     if (ABSL_PREDICT_FALSE(sampler.SubtleMaybeSample())) {
  //       FrobberSampled();
  //     } else {
  //       FrobberImpl();
  //     }
  //   }
  //
  //   // --- frobber.cc -----------
  //   void FrobberSampled() {
  //     if (!sampler.SubtleConfirmSample())) {
  //       // Spurious false positive
  //       FrobberImpl();
  //       return;
  //     }
  //
  //     // Sampled execution
  //     // ...
  //   }
  inline bool SubtleMaybeSample() noexcept;
  bool SubtleConfirmSample() noexcept;

 protected:
  // We explicitly don't use a virtual destructor as this class is never
  // virtually destroyed, and it keeps the class trivial, which avoids TLS
  // prologue and epilogue code for our TLS instances.
  ~PeriodicSamplerBase() = default;

  // Returns the next stride for our sampler.
  // This function is virtual for testing purposes only.
  virtual int64_t GetExponentialBiased(int period) noexcept;

 private:
  // Returns the current period of this sampler. Thread-safe.
  virtual int period() const noexcept = 0;

  // Keep and decrement stride_ as an unsigned integer, but compare the value
  // to zero casted as a signed int. clang and msvc do not create optimum code
  // if we use signed for the combined decrement and sign comparison.
  //
  // Below 3 alternative options, all compiles generate the best code
  // using the unsigned increment <---> signed int comparison option.
  //
  // Option 1:
  //   int64_t stride_;
  //   if (ABSL_PREDICT_TRUE(++stride_ < 0)) { ... }
  //
  //   GCC   x64 (OK) : https://gcc.godbolt.org/z/R5MzzA
  //   GCC   ppc (OK) : https://gcc.godbolt.org/z/z7NZAt
  //   Clang x64 (BAD): https://gcc.godbolt.org/z/t4gPsd
  //   ICC   x64 (OK) : https://gcc.godbolt.org/z/rE6s8W
  //   MSVC  x64 (OK) : https://gcc.godbolt.org/z/ARMXqS
  //
  // Option 2:
  //   int64_t stride_ = 0;
  //   if (ABSL_PREDICT_TRUE(--stride_ >= 0)) { ... }
  //
  //   GCC   x64 (OK) : https://gcc.godbolt.org/z/jSQxYK
  //   GCC   ppc (OK) : https://gcc.godbolt.org/z/VJdYaA
  //   Clang x64 (BAD): https://gcc.godbolt.org/z/Xm4NjX
  //   ICC   x64 (OK) : https://gcc.godbolt.org/z/4snaFd
  //   MSVC  x64 (BAD): https://gcc.godbolt.org/z/BgnEKE
  //
  // Option 3:
  //   uint64_t stride_;
  //   if (ABSL_PREDICT_TRUE(static_cast<int64_t>(++stride_) < 0)) { ... }
  //
  //   GCC   x64 (OK) : https://gcc.godbolt.org/z/bFbfPy
  //   GCC   ppc (OK) : https://gcc.godbolt.org/z/S9KkUE
  //   Clang x64 (OK) : https://gcc.godbolt.org/z/UYzRb4
  //   ICC   x64 (OK) : https://gcc.godbolt.org/z/ptTNfD
  //   MSVC  x64 (OK) : https://gcc.godbolt.org/z/76j4-5
  uint64_t stride_ = 0;
  absl::profiling_internal::ExponentialBiased rng_;
};

inline bool PeriodicSamplerBase::SubtleMaybeSample() noexcept {
  // See comments on `stride_` for the unsigned increment / signed compare.
  if (ABSL_PREDICT_TRUE(static_cast<int64_t>(++stride_) < 0)) {
    return false;
  }
  return true;
}

inline bool PeriodicSamplerBase::Sample() noexcept {
  return ABSL_PREDICT_FALSE(SubtleMaybeSample()) ? SubtleConfirmSample()
                                                 : false;
}

// PeriodicSampler is a concreted periodic sampler implementation.
// The user provided Tag identifies the implementation, and is required to
// isolate the global state of this instance from other instances.
//
// Typical use case:
//
//   struct HashTablezTag {};
//   thread_local PeriodicSampler<HashTablezTag, 100> sampler;
//
//   void HashTableSamplingLogic(...) {
//     if (sampler.Sample()) {
//       HashTableSlowSamplePath(...);
//     }
//   }
//
template <typename Tag, int default_period = 0>
class PeriodicSampler final : public PeriodicSamplerBase {
 public:
  ~PeriodicSampler() = default;

  int period() const noexcept final {
    return period_.load(std::memory_order_relaxed);
  }

  // Sets the global period for this sampler. Thread-safe.
  // Setting a period of 0 disables the sampler, i.e., every call to Sample()
  // will return false. Setting a period of 1 puts the sampler in 'always on'
  // mode, i.e., every call to Sample() returns true.
  static void SetGlobalPeriod(int period) {
    period_.store(period, std::memory_order_relaxed);
  }

 private:
  static std::atomic<int> period_;
};

template <typename Tag, int default_period>
std::atomic<int> PeriodicSampler<Tag, default_period>::period_(default_period);

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_PROFILING_INTERNAL_PERIODIC_SAMPLER_H_
