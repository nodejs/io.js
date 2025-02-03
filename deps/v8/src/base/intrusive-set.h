// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_INTRUSIVE_SET_H_
#define V8_BASE_INTRUSIVE_SET_H_

#include <iterator>
#include <limits>
#include <type_traits>

#include "src/base/logging.h"

namespace v8::base {

class IntrusiveSetIndex {
 private:
  template <class T, class GetIntrusiveSetIndex, class Container>
  friend class IntrusiveSet;
  static constexpr size_t kNotInSet = std::numeric_limits<size_t>::max();

  size_t value = kNotInSet;
};

// A set of pointer-like values (`T`) that point to memory containing the
// position inside of the set (`IntrusiveSetIndex`), to allow for O(1) insertion
// and removal without using a hash table. This set is intrusive in the sense
// that elements need to know their position inside of the set by storing an
// `IntrusiveSetIndex` somewhere. In particular, all copies of a `T` value
// should point to the same `IntrusiveSetIndex` instance. `GetIntrusiveSetIndex`
// has to be a functor that produces `IntrusiveSetIndex&` given a `T`. The
// reference has to remain valid and refer to the same memory location while the
// element is in the set and until we finish iterating over the data structure
// if the element is removed during iteration.
//
// Add(T):     amortized O(1)
// Contain(T): O(1)
// Remove(T):  O(1)
template <class T, class GetIntrusiveSetIndex, class Container>
class IntrusiveSet {
 public:
  // This is not needed for soundness, but rather serves as a hint that `T`
  // should be a lightweight pointer-like value.
  static_assert(std::is_trivially_copyable_v<T>);

  explicit IntrusiveSet(Container container,
                        GetIntrusiveSetIndex index_functor = {})
      : elements_(std::move(container)), index_functor_(index_functor) {
    static_assert(std::is_same_v<decltype(index_functor(std::declval<T>())),
                                 IntrusiveSetIndex&>);
  }

  bool Contains(T x) const { return Index(x) != IntrusiveSetIndex::kNotInSet; }

  // Adding elements while iterating is allowed.
  void Add(T x) {
    DCHECK(!Contains(x));
    Index(x) = elements_.size();
    elements_.push_back(x);
  }

  // Removing while iterating is allowed under very specific circumstances. See
  // comment on `IntrusiveSet::iterator`.
  void Remove(T x) {
    DCHECK(Contains(x));
    size_t& index = Index(x);
    DCHECK_EQ(x, elements_[index]);
    Index(elements_.back()) = index;
    elements_[index] = elements_.back();
    index = IntrusiveSetIndex::kNotInSet;
    elements_.pop_back();
  }

  // Since C++17, it is possible to have a sentinel end-iterator that is not an
  // iterator itself.
  class end_iterator {};

  // This iterator supports insertion (newly inserted elements will be visited
  // as part of the iteration) and removal of the current element while
  // iterating. Removing previously visited elements is undefined behavior.
  // ATTENTION! The memory the removed element points to needs to remain alive
  // until the end of the iteration.
  class iterator {
   public:
    explicit iterator(const IntrusiveSet& set) : set_(set) {}
    T operator*() {
      T result = set_.elements_[index_];
      last_index_location_ = &set_.Index(result);
      return result;
    }
    iterator& operator++() {
      // This iterator requires `operator*` being used before `operator++`.
      DCHECK_NOT_NULL(last_index_location_);
      if (index_ < set_.elements_.size() &&
          last_index_location_ == &set_.Index(set_.elements_[index_])) {
        index_++;
      }
      return *this;
    }
    bool operator!=(end_iterator) const {
      return index_ < set_.elements_.size();
    }

   private:
    const IntrusiveSet& set_;
    size_t index_ = 0;
    // If the current element is removed, another element is swapped in to the
    // same position. We notice this by remembering the index memory location of
    // the last retrieved element.
    const size_t* last_index_location_ = nullptr;
  };

  // These iterators are only intended for range-based for loops.
  iterator begin() const { return iterator{*this}; }
  end_iterator end() const { return end_iterator{}; }

 private:
  Container elements_;
  GetIntrusiveSetIndex index_functor_;

  size_t& Index(T x) const { return index_functor_(x).value; }
};

}  // namespace v8::base

#endif  // V8_BASE_INTRUSIVE_SET_H_
