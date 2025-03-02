// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UTILS_LOCKED_QUEUE_INL_H_
#define V8_UTILS_LOCKED_QUEUE_INL_H_

#include "src/base/atomic-utils.h"
#include "src/utils/allocation.h"
#include "src/utils/locked-queue.h"

namespace v8 {
namespace internal {

template <typename Record>
struct LockedQueue<Record>::Node : Malloced {
  Node() : next(nullptr) {}
  Record value;
  base::AtomicValue<Node*> next;
};

template <typename Record>
inline LockedQueue<Record>::LockedQueue() {
  head_ = new Node();
  CHECK_NOT_NULL(head_);
  tail_ = head_;
  size_ = 0;
}

template <typename Record>
inline LockedQueue<Record>::~LockedQueue() {
  // Destroy all remaining nodes. Note that we do not destroy the actual values.
  Node* old_node = nullptr;
  Node* cur_node = head_;
  while (cur_node != nullptr) {
    old_node = cur_node;
    cur_node = cur_node->next.Value();
    delete old_node;
  }
}

template <typename Record>
inline void LockedQueue<Record>::Enqueue(Record record) {
  Node* n = new Node();
  CHECK_NOT_NULL(n);
  n->value = std::move(record);
  {
    base::SpinningMutexGuard guard(&tail_mutex_);
    size_++;
    tail_->next.SetValue(n);
    tail_ = n;
  }
}

template <typename Record>
inline bool LockedQueue<Record>::Dequeue(Record* record) {
  Node* old_head = nullptr;
  {
    base::SpinningMutexGuard guard(&head_mutex_);
    old_head = head_;
    Node* const next_node = head_->next.Value();
    if (next_node == nullptr) return false;
    *record = std::move(next_node->value);
    head_ = next_node;
    size_t old_size = size_.fetch_sub(1);
    USE(old_size);
    DCHECK_GT(old_size, 0);
  }
  delete old_head;
  return true;
}

template <typename Record>
inline bool LockedQueue<Record>::IsEmpty() const {
  base::SpinningMutexGuard guard(&head_mutex_);
  return head_->next.Value() == nullptr;
}

template <typename Record>
inline bool LockedQueue<Record>::Peek(Record* record) const {
  base::SpinningMutexGuard guard(&head_mutex_);
  Node* const next_node = head_->next.Value();
  if (next_node == nullptr) return false;
  *record = next_node->value;
  return true;
}

template <typename Record>
inline size_t LockedQueue<Record>::size() const {
  return size_;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_UTILS_LOCKED_QUEUE_INL_H_
