// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LIST_H_
#define V8_HEAP_LIST_H_

#include <atomic>

#include "src/base/logging.h"

namespace v8 {
namespace internal {
namespace heap {

template <class T>
class List {
 public:
  List() = default;
  List(List&& other) V8_NOEXCEPT : front_(std::exchange(other.front_, nullptr)),
                                   back_(std::exchange(other.back_, nullptr)),
                                   size_(std::exchange(other.size_, 0)) {}
  List& operator=(List&& other) V8_NOEXCEPT {
    front_ = std::exchange(other.front_, nullptr);
    back_ = std::exchange(other.back_, nullptr);
    size_ = std::exchange(other.size_, 0);
    return *this;
  }

  void PushBack(T* element) {
    DCHECK(!element->list_node().next());
    DCHECK(!element->list_node().prev());
    if (back_) {
      DCHECK(front_);
      InsertAfter(element, back_);
    } else {
      AddFirstElement(element);
    }
    size_++;
  }

  void PushFront(T* element) {
    DCHECK(!element->list_node().next());
    DCHECK(!element->list_node().prev());
    if (front_) {
      DCHECK(back_);
      InsertBefore(element, front_);
    } else {
      AddFirstElement(element);
    }
    size_++;
  }

  void Remove(T* element) {
    DCHECK(Contains(element));
    if (back_ == element) {
      back_ = element->list_node().prev();
    }
    if (front_ == element) {
      front_ = element->list_node().next();
    }
    T* next = element->list_node().next();
    T* prev = element->list_node().prev();
    if (next) next->list_node().set_prev(prev);
    if (prev) prev->list_node().set_next(next);
    element->list_node().set_prev(nullptr);
    element->list_node().set_next(nullptr);
    size_--;
  }

  bool Contains(const T* element) const {
    const T* it = front_;
    while (it) {
      if (it == element) return true;
      it = it->list_node().next();
    }
    return false;
  }

  bool Empty() const {
    DCHECK_EQ(size_ == 0, !front_);
    DCHECK_EQ(size_ == 0, !back_);
    return size_ == 0;
  }

  T* front() { return front_; }
  T* back() { return back_; }

  const T* front() const { return front_; }
  const T* back() const { return back_; }

  size_t size() const { return size_; }

 private:
  void AddFirstElement(T* element) {
    DCHECK(!back_);
    DCHECK(!front_);
    DCHECK(!element->list_node().next());
    DCHECK(!element->list_node().prev());
    element->list_node().set_prev(nullptr);
    element->list_node().set_next(nullptr);
    front_ = element;
    back_ = element;
  }

  void InsertAfter(T* element, T* other) {
    T* other_next = other->list_node().next();
    element->list_node().set_next(other_next);
    element->list_node().set_prev(other);
    other->list_node().set_next(element);
    if (other_next)
      other_next->list_node().set_prev(element);
    else
      back_ = element;
  }

  void InsertBefore(T* element, T* other) {
    T* other_prev = other->list_node().prev();
    element->list_node().set_next(other);
    element->list_node().set_prev(other_prev);
    other->list_node().set_prev(element);
    if (other_prev) {
      other_prev->list_node().set_next(element);
    } else {
      front_ = element;
    }
  }

  T* front_{nullptr};
  T* back_{nullptr};
  size_t size_{0};
};

template <class T>
class ListNode {
 public:
  ListNode() { Initialize(); }

  T* next() { return next_; }
  T* prev() { return prev_; }

  const T* next() const { return next_; }
  const T* prev() const { return prev_; }

  void Initialize() {
    next_ = nullptr;
    prev_ = nullptr;
  }

 private:
  void set_next(T* next) { next_ = next; }
  void set_prev(T* prev) { prev_ = prev; }

  T* next_;
  T* prev_;

  friend class List<T>;
};
}  // namespace heap
}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LIST_H_
