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

#ifndef ABSL_CONTAINER_INTERNAL_TEST_INSTANCE_TRACKER_H_
#define ABSL_CONTAINER_INTERNAL_TEST_INSTANCE_TRACKER_H_

#include <cstdlib>
#include <ostream>

#include "absl/types/compare.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace test_internal {

// A type that counts number of occurrences of the type, the live occurrences of
// the type, as well as the number of copies, moves, swaps, and comparisons that
// have occurred on the type. This is used as a base class for the copyable,
// copyable+movable, and movable types below that are used in actual tests. Use
// InstanceTracker in tests to track the number of instances.
class BaseCountedInstance {
 public:
  explicit BaseCountedInstance(int x) : value_(x) {
    ++num_instances_;
    ++num_live_instances_;
  }
  BaseCountedInstance(const BaseCountedInstance& x)
      : value_(x.value_), is_live_(x.is_live_) {
    ++num_instances_;
    if (is_live_) ++num_live_instances_;
    ++num_copies_;
  }
  BaseCountedInstance(BaseCountedInstance&& x)
      : value_(x.value_), is_live_(x.is_live_) {
    x.is_live_ = false;
    ++num_instances_;
    ++num_moves_;
  }
  ~BaseCountedInstance() {
    --num_instances_;
    if (is_live_) --num_live_instances_;
  }

  BaseCountedInstance& operator=(const BaseCountedInstance& x) {
    value_ = x.value_;
    if (is_live_) --num_live_instances_;
    is_live_ = x.is_live_;
    if (is_live_) ++num_live_instances_;
    ++num_copies_;
    return *this;
  }
  BaseCountedInstance& operator=(BaseCountedInstance&& x) {
    value_ = x.value_;
    if (is_live_) --num_live_instances_;
    is_live_ = x.is_live_;
    x.is_live_ = false;
    ++num_moves_;
    return *this;
  }

  bool operator==(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ == x.value_;
  }

  bool operator!=(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ != x.value_;
  }

  bool operator<(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ < x.value_;
  }

  bool operator>(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ > x.value_;
  }

  bool operator<=(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ <= x.value_;
  }

  bool operator>=(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ >= x.value_;
  }

  absl::weak_ordering compare(const BaseCountedInstance& x) const {
    ++num_comparisons_;
    return value_ < x.value_
               ? absl::weak_ordering::less
               : value_ == x.value_ ? absl::weak_ordering::equivalent
                                    : absl::weak_ordering::greater;
  }

  int value() const {
    if (!is_live_) std::abort();
    return value_;
  }

  friend std::ostream& operator<<(std::ostream& o,
                                  const BaseCountedInstance& v) {
    return o << "[value:" << v.value() << "]";
  }

  // Implementation of efficient swap() that counts swaps.
  static void SwapImpl(
      BaseCountedInstance& lhs,    // NOLINT(runtime/references)
      BaseCountedInstance& rhs) {  // NOLINT(runtime/references)
    using std::swap;
    swap(lhs.value_, rhs.value_);
    swap(lhs.is_live_, rhs.is_live_);
    ++BaseCountedInstance::num_swaps_;
  }

 private:
  friend class InstanceTracker;

  int value_;

  // Indicates if the value is live, ie it hasn't been moved away from.
  bool is_live_ = true;

  // Number of instances.
  static int num_instances_;

  // Number of live instances (those that have not been moved away from.)
  static int num_live_instances_;

  // Number of times that BaseCountedInstance objects were moved.
  static int num_moves_;

  // Number of times that BaseCountedInstance objects were copied.
  static int num_copies_;

  // Number of times that BaseCountedInstance objects were swapped.
  static int num_swaps_;

  // Number of times that BaseCountedInstance objects were compared.
  static int num_comparisons_;
};

// Helper to track the BaseCountedInstance instance counters. Expects that the
// number of instances and live_instances are the same when it is constructed
// and when it is destructed.
class InstanceTracker {
 public:
  InstanceTracker()
      : start_instances_(BaseCountedInstance::num_instances_),
        start_live_instances_(BaseCountedInstance::num_live_instances_) {
    ResetCopiesMovesSwaps();
  }
  ~InstanceTracker() {
    if (instances() != 0) std::abort();
    if (live_instances() != 0) std::abort();
  }

  // Returns the number of BaseCountedInstance instances both containing valid
  // values and those moved away from compared to when the InstanceTracker was
  // constructed
  int instances() const {
    return BaseCountedInstance::num_instances_ - start_instances_;
  }

  // Returns the number of live BaseCountedInstance instances compared to when
  // the InstanceTracker was constructed
  int live_instances() const {
    return BaseCountedInstance::num_live_instances_ - start_live_instances_;
  }

  // Returns the number of moves on BaseCountedInstance objects since
  // construction or since the last call to ResetCopiesMovesSwaps().
  int moves() const { return BaseCountedInstance::num_moves_ - start_moves_; }

  // Returns the number of copies on BaseCountedInstance objects since
  // construction or the last call to ResetCopiesMovesSwaps().
  int copies() const {
    return BaseCountedInstance::num_copies_ - start_copies_;
  }

  // Returns the number of swaps on BaseCountedInstance objects since
  // construction or the last call to ResetCopiesMovesSwaps().
  int swaps() const { return BaseCountedInstance::num_swaps_ - start_swaps_; }

  // Returns the number of comparisons on BaseCountedInstance objects since
  // construction or the last call to ResetCopiesMovesSwaps().
  int comparisons() const {
    return BaseCountedInstance::num_comparisons_ - start_comparisons_;
  }

  // Resets the base values for moves, copies, comparisons, and swaps to the
  // current values, so that subsequent Get*() calls for moves, copies,
  // comparisons, and swaps will compare to the situation at the point of this
  // call.
  void ResetCopiesMovesSwaps() {
    start_moves_ = BaseCountedInstance::num_moves_;
    start_copies_ = BaseCountedInstance::num_copies_;
    start_swaps_ = BaseCountedInstance::num_swaps_;
    start_comparisons_ = BaseCountedInstance::num_comparisons_;
  }

 private:
  int start_instances_;
  int start_live_instances_;
  int start_moves_;
  int start_copies_;
  int start_swaps_;
  int start_comparisons_;
};

// Copyable, not movable.
class CopyableOnlyInstance : public BaseCountedInstance {
 public:
  explicit CopyableOnlyInstance(int x) : BaseCountedInstance(x) {}
  CopyableOnlyInstance(const CopyableOnlyInstance& rhs) = default;
  CopyableOnlyInstance& operator=(const CopyableOnlyInstance& rhs) = default;

  friend void swap(CopyableOnlyInstance& lhs, CopyableOnlyInstance& rhs) {
    BaseCountedInstance::SwapImpl(lhs, rhs);
  }

  static bool supports_move() { return false; }
};

// Copyable and movable.
class CopyableMovableInstance : public BaseCountedInstance {
 public:
  explicit CopyableMovableInstance(int x) : BaseCountedInstance(x) {}
  CopyableMovableInstance(const CopyableMovableInstance& rhs) = default;
  CopyableMovableInstance(CopyableMovableInstance&& rhs) = default;
  CopyableMovableInstance& operator=(const CopyableMovableInstance& rhs) =
      default;
  CopyableMovableInstance& operator=(CopyableMovableInstance&& rhs) = default;

  friend void swap(CopyableMovableInstance& lhs, CopyableMovableInstance& rhs) {
    BaseCountedInstance::SwapImpl(lhs, rhs);
  }

  static bool supports_move() { return true; }
};

// Only movable, not default-constructible.
class MovableOnlyInstance : public BaseCountedInstance {
 public:
  explicit MovableOnlyInstance(int x) : BaseCountedInstance(x) {}
  MovableOnlyInstance(MovableOnlyInstance&& other) = default;
  MovableOnlyInstance& operator=(MovableOnlyInstance&& other) = default;

  friend void swap(MovableOnlyInstance& lhs, MovableOnlyInstance& rhs) {
    BaseCountedInstance::SwapImpl(lhs, rhs);
  }

  static bool supports_move() { return true; }
};

}  // namespace test_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_TEST_INSTANCE_TRACKER_H_
