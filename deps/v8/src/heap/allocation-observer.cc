// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/allocation-observer.h"

#include <algorithm>

#include "src/heap/heap.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

void AllocationCounter::AddAllocationObserver(AllocationObserver* observer) {
#if DEBUG
  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [observer](const AllocationObserverCounter& aoc) {
                           return aoc.observer_ == observer;
                         });
  DCHECK_EQ(observers_.end(), it);
#endif

  if (step_in_progress_) {
    pending_added_.push_back(AllocationObserverCounter(observer, 0, 0));
    return;
  }

  intptr_t step_size = observer->GetNextStepSize();
  size_t observer_next_counter = current_counter_ + step_size;

  observers_.push_back(AllocationObserverCounter(observer, current_counter_,
                                                 observer_next_counter));

  if (observers_.size() == 1) {
    DCHECK_EQ(current_counter_, next_counter_);
    next_counter_ = observer_next_counter;
  } else {
    size_t missing_bytes = next_counter_ - current_counter_;
    next_counter_ = current_counter_ +
                    std::min(static_cast<intptr_t>(missing_bytes), step_size);
  }
}

void AllocationCounter::RemoveAllocationObserver(AllocationObserver* observer) {
  auto it = std::find_if(observers_.begin(), observers_.end(),
                         [observer](const AllocationObserverCounter& aoc) {
                           return aoc.observer_ == observer;
                         });
  DCHECK_NE(observers_.end(), it);

  if (step_in_progress_) {
    DCHECK_EQ(pending_removed_.count(observer), 0);
    pending_removed_.insert(observer);
    return;
  }

  observers_.erase(it);

  if (observers_.empty()) {
    current_counter_ = next_counter_ = 0;
  } else {
    size_t step_size = 0;

    for (AllocationObserverCounter& observer_counter : observers_) {
      size_t left_in_step = observer_counter.next_counter_ - current_counter_;
      DCHECK_GT(left_in_step, 0);
      step_size = step_size ? std::min(step_size, left_in_step) : left_in_step;
    }

    next_counter_ = current_counter_ + step_size;
  }
}

void AllocationCounter::AdvanceAllocationObservers(size_t allocated) {
  if (observers_.empty()) return;
  DCHECK(!step_in_progress_);
  DCHECK_LT(allocated, next_counter_ - current_counter_);
  current_counter_ += allocated;
}

void AllocationCounter::InvokeAllocationObservers(Address soon_object,
                                                  size_t object_size,
                                                  size_t aligned_object_size) {
  if (observers_.empty()) return;
  DCHECK(!step_in_progress_);
  DCHECK_GE(aligned_object_size, next_counter_ - current_counter_);
  DCHECK(soon_object);
  bool step_run = false;
  step_in_progress_ = true;
  size_t step_size = 0;

  DCHECK(pending_added_.empty());
  DCHECK(pending_removed_.empty());

  for (AllocationObserverCounter& aoc : observers_) {
    if (aoc.next_counter_ - current_counter_ <= aligned_object_size) {
      {
        DisallowGarbageCollection no_gc;
        aoc.observer_->Step(
            static_cast<int>(current_counter_ - aoc.prev_counter_), soon_object,
            object_size);
      }
      size_t observer_step_size = aoc.observer_->GetNextStepSize();

      aoc.prev_counter_ = current_counter_;
      aoc.next_counter_ =
          current_counter_ + aligned_object_size + observer_step_size;
      step_run = true;
    }

    size_t left_in_step = aoc.next_counter_ - current_counter_;
    step_size = step_size ? std::min(step_size, left_in_step) : left_in_step;
  }

  CHECK(step_run);

  // Now process newly added allocation observers.
  for (AllocationObserverCounter& aoc : pending_added_) {
    DCHECK_EQ(0, aoc.next_counter_);
    size_t observer_step_size = aoc.observer_->GetNextStepSize();
    aoc.prev_counter_ = current_counter_;
    aoc.next_counter_ =
        current_counter_ + aligned_object_size + observer_step_size;

    DCHECK_NE(step_size, 0);
    step_size = std::min(step_size, aligned_object_size + observer_step_size);

    observers_.push_back(aoc);
  }

  pending_added_.clear();

  if (!pending_removed_.empty()) {
    observers_.erase(
        std::remove_if(observers_.begin(), observers_.end(),
                       [this](const AllocationObserverCounter& aoc) {
                         return pending_removed_.count(aoc.observer_) != 0;
                       }),
        observers_.end());
    pending_removed_.clear();

    // Some observers were removed, recalculate step size.
    step_size = 0;
    for (AllocationObserverCounter& aoc : observers_) {
      size_t left_in_step = aoc.next_counter_ - current_counter_;
      step_size = step_size ? std::min(step_size, left_in_step) : left_in_step;
    }

    if (observers_.empty()) {
      next_counter_ = current_counter_ = 0;
      step_in_progress_ = false;
      return;
    }
  }

  next_counter_ = current_counter_ + step_size;
  step_in_progress_ = false;
}

PauseAllocationObserversScope::PauseAllocationObserversScope(Heap* heap)
    : heap_(heap) {
  DCHECK_EQ(heap->gc_state(), Heap::NOT_IN_GC);
  heap->allocator()->PauseAllocationObservers();
  heap_->pause_allocation_observers_depth_++;
}

PauseAllocationObserversScope::~PauseAllocationObserversScope() {
  heap_->pause_allocation_observers_depth_--;
  heap_->allocator()->ResumeAllocationObservers();
}

}  // namespace internal
}  // namespace v8
