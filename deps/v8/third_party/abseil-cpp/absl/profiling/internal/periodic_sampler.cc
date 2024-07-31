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

#include "absl/profiling/internal/periodic_sampler.h"

#include <atomic>

#include "absl/profiling/internal/exponential_biased.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace profiling_internal {

int64_t PeriodicSamplerBase::GetExponentialBiased(int period) noexcept {
  return rng_.GetStride(period);
}

bool PeriodicSamplerBase::SubtleConfirmSample() noexcept {
  int current_period = period();

  // Deal with period case 0 (always off) and 1 (always on)
  if (ABSL_PREDICT_FALSE(current_period < 2)) {
    stride_ = 0;
    return current_period == 1;
  }

  // Check if this is the first call to Sample()
  if (ABSL_PREDICT_FALSE(stride_ == 1)) {
    stride_ = static_cast<uint64_t>(-GetExponentialBiased(current_period));
    if (static_cast<int64_t>(stride_) < -1) {
      ++stride_;
      return false;
    }
  }

  stride_ = static_cast<uint64_t>(-GetExponentialBiased(current_period));
  return true;
}

}  // namespace profiling_internal
ABSL_NAMESPACE_END
}  // namespace absl
