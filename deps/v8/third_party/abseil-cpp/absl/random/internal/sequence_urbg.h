// Copyright 2017 The Abseil Authors.
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

#ifndef ABSL_RANDOM_INTERNAL_SEQUENCE_URBG_H_
#define ABSL_RANDOM_INTERNAL_SEQUENCE_URBG_H_

#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace random_internal {

// `sequence_urbg` is a simple random number generator which meets the
// requirements of [rand.req.urbg], and is solely for testing absl
// distributions.
class sequence_urbg {
 public:
  using result_type = uint64_t;

  static constexpr result_type(min)() {
    return (std::numeric_limits<result_type>::min)();
  }
  static constexpr result_type(max)() {
    return (std::numeric_limits<result_type>::max)();
  }

  sequence_urbg(std::initializer_list<result_type> data) : i_(0), data_(data) {}
  void reset() { i_ = 0; }

  result_type operator()() { return data_[i_++ % data_.size()]; }

  size_t invocations() const { return i_; }

 private:
  size_t i_;
  std::vector<result_type> data_;
};

}  // namespace random_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_RANDOM_INTERNAL_SEQUENCE_URBG_H_
