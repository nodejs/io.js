// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_DOUBLY_THREADED_LIST_H_
#define V8_BASE_DOUBLY_THREADED_LIST_H_

#include "src/base/compiler-specific.h"
#include "src/base/iterator.h"
#include "src/base/logging.h"

namespace v8::base {

template <typename T>
struct DoublyThreadedListTraits {
  static T** prev(T t) { return t->prev(); }
  static T* next(T t) { return t->next(); }
  static bool non_empty(T t) { return t != nullptr; }
};

// `DoublyThreadedList` is an intrusive doubly-linked list that threads through
// its nodes, somewhat like `v8::base::ThreadedList`.
//
// Of interest is the fact that instead of having regular next/prev pointers,
// nodes have a regular "next" pointer, but their "prev" pointer contains the
// address of the "next" of the previous element. This way, removing an element
// doesn't require special treatment for the head of the list, and does not
// even require to know the head of the list.
template <class T, class DTLTraits = DoublyThreadedListTraits<T>>
class DoublyThreadedList {
 public:
  // Since C++17, it is possible to have a sentinel end-iterator that is not an
  // iterator itself.
  class end_iterator {};

  class iterator : public base::iterator<std::forward_iterator_tag, T> {
   public:
    explicit iterator(T head) : curr_(head) {}

    T operator*() { return curr_; }

    iterator& operator++() {
      DCHECK(DTLTraits::non_empty(curr_));
      curr_ = *DTLTraits::next(curr_);
      return *this;
    }

    iterator operator++(int) {
      DCHECK(DTLTraits::non_empty(curr_));
      iterator tmp(*this);
      operator++();
      return tmp;
    }

    bool operator==(end_iterator) { return !DTLTraits::non_empty(curr_); }
    bool operator!=(end_iterator) { return DTLTraits::non_empty(curr_); }

   private:
    friend DoublyThreadedList;
    T curr_;
  };

  // Removes `x` from the list. Iterators that are currently on `x` are
  // invalidated. To remove while iterating, use RemoveAt.
  static void Remove(T x) {
    if (*DTLTraits::prev(x) == nullptr) {
      DCHECK(empty(*DTLTraits::next(x)));
      // {x} already removed from the list.
      return;
    }
    T** prev = DTLTraits::prev(x);
    T* next = DTLTraits::next(x);
    **prev = *next;
    if (DTLTraits::non_empty(*next)) *DTLTraits::prev(*next) = *prev;
    *DTLTraits::prev(x) = nullptr;
    *DTLTraits::next(x) = {};
  }

  DoublyThreadedList() = default;

  // Defining move constructor so that when resizing container, the prev pointer
  // of the next(head_) doesn't point to the old head_ but rather to the new
  // one.
  DoublyThreadedList(DoublyThreadedList&& other) V8_NOEXCEPT {
    head_ = other.head_;
    if (DTLTraits::non_empty(head_)) {
      *DTLTraits::prev(head_) = &head_;
    }
    other.head_ = {};
  }

  // Add `x` at the beginning of the list. `x` will not be visible to any
  // existing iterator. Does not invalidate any existing iterator.
  void PushFront(T x) {
    DCHECK(empty(*DTLTraits::next(x)));
    DCHECK_EQ(*DTLTraits::prev(x), nullptr);
    *DTLTraits::next(x) = head_;
    *DTLTraits::prev(x) = &head_;
    if (DTLTraits::non_empty(head_)) {
      *DTLTraits::prev(head_) = DTLTraits::next(x);
    }
    head_ = x;
  }

  T Front() const {
    DCHECK(!empty());
    return *begin();
  }

  void PopFront() {
    DCHECK(!empty());
    Remove(Front());
  }

  bool empty() const { return !DTLTraits::non_empty(head_); }

  iterator begin() const { return iterator{head_}; }
  end_iterator end() const { return end_iterator{}; }

  // Removes the element at `it`, and make `it` point to the next element.
  // Iterators on the same element as `it` are invalidated. Other iterators are
  // not affected.
  iterator RemoveAt(iterator& it) {
    DCHECK(DTLTraits::non_empty(it.curr_));
    T curr = *it;
    T next = *DTLTraits::next(curr);
    Remove(curr);
    return iterator{next};
  }

  bool ContainsSlow(T needle) const {
    for (T element : *this) {
      if (element == needle) {
        return true;
      }
    }
    return false;
  }

 private:
  static bool empty(T x) { return !DTLTraits::non_empty(x); }
  T head_{};
};

}  // namespace v8::base

#endif  // V8_BASE_DOUBLY_THREADED_LIST_H_
