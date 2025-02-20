// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_LOCKED_QUEUE_H_
#define V8_UTILS_LOCKED_QUEUE_H_

#include <atomic>

#include "src/base/platform/platform.h"

namespace v8 {
namespace internal {

// Simple lock-based unbounded size queue (multi producer; multi consumer) based
// on "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue
// Algorithms" by M. Scott and M. Michael.
// See:
// https://www.cs.rochester.edu/research/synchronization/pseudocode/queues.html
template <typename Record>
class LockedQueue final {
 public:
  inline LockedQueue();
  LockedQueue(const LockedQueue&) = delete;
  LockedQueue& operator=(const LockedQueue&) = delete;
  inline ~LockedQueue();
  inline void Enqueue(Record record);
  inline bool Dequeue(Record* record);
  inline bool IsEmpty() const;
  inline bool Peek(Record* record) const;
  inline size_t size() const;

 private:
  struct Node;

  mutable base::SpinningMutex head_mutex_;
  base::SpinningMutex tail_mutex_;
  Node* head_;
  Node* tail_;
  std::atomic<size_t> size_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_LOCKED_QUEUE_H_
