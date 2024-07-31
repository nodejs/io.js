// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_ALLOCATION_OBSERVER_H_
#define V8_HEAP_ALLOCATION_OBSERVER_H_

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "src/common/globals.h"

namespace v8 {
namespace internal {

// Observer for allocations that is aware of LAB-based allocation.
class AllocationObserver {
 public:
  static constexpr intptr_t kNotUsingFixedStepSize = -1;
  explicit AllocationObserver(intptr_t step_size) : step_size_(step_size) {}
  virtual ~AllocationObserver() = default;
  AllocationObserver(const AllocationObserver&) = delete;
  AllocationObserver& operator=(const AllocationObserver&) = delete;

 protected:
  // Called when at least `step_size_` bytes have been allocated. `soon_object`
  // points to the uninitialized memory that has just been allocated and is the
  // result for a request of `size` bytes.
  //
  // Some caveats:
  // 1. `soon_object` will be nullptr in cases zwhere the allocation returns a
  //    filler object, which is e.g. needed at page boundaries.
  // 2. `soon_object`  may actually be the first object in an
  //    allocation-folding group. In such a case size is the size of the group
  //    rather than the first object.
  // 3. `size` is the requested size at the time of allocation. Right-trimming
  //    may change the object size dynamically.
  virtual void Step(int bytes_allocated, Address soon_object, size_t size) = 0;

  // Subclasses can override this method to make step size dynamic.
  virtual intptr_t GetNextStepSize() {
    DCHECK_NE(kNotUsingFixedStepSize, step_size_);
    return step_size_;
  }

 private:
  const intptr_t step_size_;

  friend class AllocationCounter;
};

// A global allocation counter observers can be added to.
class AllocationCounter final {
 public:
  AllocationCounter() = default;

  // Adds an observer. May be called from `AllocationObserver::Step()`.
  V8_EXPORT_PRIVATE void AddAllocationObserver(AllocationObserver* observer);

  // Removes an observer. May be called from `AllocationObserver::Step()`.
  V8_EXPORT_PRIVATE void RemoveAllocationObserver(AllocationObserver* observer);

  // Advances forward by `allocated` bytes. Does not invoke any observers.
  V8_EXPORT_PRIVATE void AdvanceAllocationObservers(size_t allocated);

  // Invokes observers via `AllocationObserver::Step()` and computes new step
  // sizes. Does not advance the current allocation counter.
  V8_EXPORT_PRIVATE void InvokeAllocationObservers(Address soon_object,
                                                   size_t object_size,
                                                   size_t aligned_object_size);

  bool IsStepInProgress() const { return step_in_progress_; }

  size_t NextBytes() const {
    if (observers_.empty()) return SIZE_MAX;
    return next_counter_ - current_counter_;
  }

#if DEBUG
  bool HasAllocationObservers() const {
    return !observers_.empty() || !pending_added_.empty() ||
           !pending_removed_.empty();
  }
#endif  // DEBUG

 private:
  struct AllocationObserverCounter final {
    AllocationObserverCounter(AllocationObserver* observer, size_t prev_counter,
                              size_t next_counter)
        : observer_(observer),
          prev_counter_(prev_counter),
          next_counter_(next_counter) {}

    AllocationObserver* observer_;
    size_t prev_counter_;
    size_t next_counter_;
  };

  std::vector<AllocationObserverCounter> observers_;
  std::vector<AllocationObserverCounter> pending_added_;
  std::unordered_set<AllocationObserver*> pending_removed_;

  size_t current_counter_ = 0;
  size_t next_counter_ = 0;
  bool step_in_progress_ = false;
};

class V8_EXPORT_PRIVATE V8_NODISCARD PauseAllocationObserversScope {
 public:
  explicit PauseAllocationObserversScope(Heap* heap);
  ~PauseAllocationObserversScope();
  PauseAllocationObserversScope(const PauseAllocationObserversScope&) = delete;
  PauseAllocationObserversScope& operator=(
      const PauseAllocationObserversScope&) = delete;

 private:
  Heap* heap_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_ALLOCATION_OBSERVER_H_
