// Copyright 2018 The Abseil Authors.
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

#ifndef ABSL_CONTAINER_INTERNAL_TRACKED_H_
#define ABSL_CONTAINER_INTERNAL_TRACKED_H_

#include <stddef.h>

#include <memory>
#include <utility>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// A class that tracks its copies and moves so that it can be queried in tests.
template <class T>
class Tracked {
 public:
  Tracked() {}
  // NOLINTNEXTLINE(runtime/explicit)
  Tracked(const T& val) : val_(val) {}
  Tracked(const Tracked& that)
      : val_(that.val_),
        num_moves_(that.num_moves_),
        num_copies_(that.num_copies_) {
    ++(*num_copies_);
  }
  Tracked(Tracked&& that)
      : val_(std::move(that.val_)),
        num_moves_(std::move(that.num_moves_)),
        num_copies_(std::move(that.num_copies_)) {
    ++(*num_moves_);
  }
  Tracked& operator=(const Tracked& that) {
    val_ = that.val_;
    num_moves_ = that.num_moves_;
    num_copies_ = that.num_copies_;
    ++(*num_copies_);
  }
  Tracked& operator=(Tracked&& that) {
    val_ = std::move(that.val_);
    num_moves_ = std::move(that.num_moves_);
    num_copies_ = std::move(that.num_copies_);
    ++(*num_moves_);
  }

  const T& val() const { return val_; }

  friend bool operator==(const Tracked& a, const Tracked& b) {
    return a.val_ == b.val_;
  }
  friend bool operator!=(const Tracked& a, const Tracked& b) {
    return !(a == b);
  }

  size_t num_copies() { return *num_copies_; }
  size_t num_moves() { return *num_moves_; }

 private:
  T val_;
  std::shared_ptr<size_t> num_moves_ = std::make_shared<size_t>(0);
  std::shared_ptr<size_t> num_copies_ = std::make_shared<size_t>(0);
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_TRACKED_H_
