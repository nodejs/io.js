// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_ITERATOR_H_
#define V8_BASE_ITERATOR_H_

#include <iterator>
#include <tuple>
#include <utility>

#include "src/base/logging.h"

namespace v8 {
namespace base {

template <class Category, class Type, class Diff = std::ptrdiff_t,
          class Pointer = Type*, class Reference = Type&>
struct iterator {
  using iterator_category = Category;
  using value_type = Type;
  using difference_type = Diff;
  using pointer = Pointer;
  using reference = Reference;
};

// The intention of the base::iterator_range class is to encapsulate two
// iterators so that the range defined by the iterators can be used like
// a regular STL container (actually only a subset of the full container
// functionality is available usually).
template <typename ForwardIterator>
class iterator_range {
 public:
  using iterator = ForwardIterator;
  using const_iterator = ForwardIterator;
  using pointer = typename std::iterator_traits<iterator>::pointer;
  using reference = typename std::iterator_traits<iterator>::reference;
  using value_type = typename std::iterator_traits<iterator>::value_type;
  using difference_type =
      typename std::iterator_traits<iterator>::difference_type;

  iterator_range() : begin_(), end_() {}
  iterator_range(ForwardIterator begin, ForwardIterator end)
      : begin_(begin), end_(end) {}

  iterator begin() const { return begin_; }
  iterator end() const { return end_; }
  const_iterator cbegin() const { return begin_; }
  const_iterator cend() const { return end_; }
  auto rbegin() const { return std::make_reverse_iterator(end_); }
  auto rend() const { return std::make_reverse_iterator(begin_); }

  bool empty() const { return cbegin() == cend(); }

  // Random Access iterators only.
  reference operator[](difference_type n) { return begin()[n]; }
  difference_type size() const { return cend() - cbegin(); }

 private:
  const_iterator const begin_;
  const_iterator const end_;
};

template <typename ForwardIterator>
auto make_iterator_range(ForwardIterator begin, ForwardIterator end) {
  return iterator_range<ForwardIterator>{begin, end};
}

template <class T>
struct DerefPtrIterator : base::iterator<std::bidirectional_iterator_tag, T> {
  T* const* ptr;

  explicit DerefPtrIterator(T* const* ptr) : ptr(ptr) {}

  T& operator*() const { return **ptr; }
  DerefPtrIterator& operator++() {
    ++ptr;
    return *this;
  }
  DerefPtrIterator& operator--() {
    --ptr;
    return *this;
  }
  bool operator!=(const DerefPtrIterator& other) const {
    return ptr != other.ptr;
  }
  bool operator==(const DerefPtrIterator& other) const {
    return ptr == other.ptr;
  }
};

// {Reversed} returns a container adapter usable in a range-based "for"
// statement for iterating a reversible container in reverse order.
//
// Example:
//
//   std::vector<int> v = ...;
//   for (int i : base::Reversed(v)) {
//     // iterates through v from back to front
//   }
//
// The signature avoids binding to temporaries (T&& / const T&) on purpose. The
// lifetime of a temporary would not extend to a range-based for loop using it.
template <typename T>
auto Reversed(T& t) {
  return make_iterator_range(std::rbegin(t), std::rend(t));
}

// This overload of `Reversed` is safe even when the argument is a temporary,
// because we rely on the wrapped iterators instead of the `iterator_range`
// object itself.
template <typename T>
auto Reversed(const iterator_range<T>& t) {
  return make_iterator_range(std::rbegin(t), std::rend(t));
}

// {IterateWithoutLast} returns a container adapter usable in a range-based
// "for" statement for iterating all elements without the last in a forward
// order. It performs a check whether the container is empty.
//
// Example:
//
//   std::vector<int> v = ...;
//   for (int i : base::IterateWithoutLast(v)) {
//     // iterates through v front to --back
//   }
//
// The signature avoids binding to temporaries, see the remark in {Reversed}.
template <typename T>
auto IterateWithoutLast(T& t) {
  DCHECK_NE(std::begin(t), std::end(t));
  auto new_end = std::end(t);
  return make_iterator_range(std::begin(t), --new_end);
}

template <typename T>
auto IterateWithoutLast(const iterator_range<T>& t) {
  iterator_range<T> range_copy = {t.begin(), t.end()};
  return IterateWithoutLast(range_copy);
}

// TupleIterator is an iterator wrapping around multiple iterators. It is use by
// the `zip` function below to iterate over multiple containers at once.
template <class... Iterators>
class TupleIterator
    : public base::iterator<
          std::bidirectional_iterator_tag,
          std::tuple<typename std::iterator_traits<Iterators>::reference...>> {
 public:
  using value_type =
      std::tuple<typename std::iterator_traits<Iterators>::reference...>;

  explicit TupleIterator(Iterators... its) : its_(its...) {}

  TupleIterator& operator++() {
    std::apply([](auto&... iterators) { (++iterators, ...); }, its_);
    return *this;
  }

  template <class Other>
  bool operator!=(const Other& other) const {
    return not_equal_impl(other, std::index_sequence_for<Iterators...>{});
  }

  value_type operator*() const {
    return std::apply(
        [](auto&... this_iterators) { return value_type{*this_iterators...}; },
        its_);
  }

 private:
  template <class Other, size_t... indices>
  bool not_equal_impl(const Other& other,
                      std::index_sequence<indices...>) const {
    return (... || (std::get<indices>(its_) != std::get<indices>(other.its_)));
  }

  std::tuple<Iterators...> its_;
};

// `zip` creates an iterator_range from multiple containers. It can be used to
// iterate over multiple containers at once. For instance:
//
//    std::vector<int> arr = { 2, 4, 6 };
//    std::set<double> set = { 3.5, 4.5, 5.5 };
//    for (auto [i, d] : base::zip(arr, set)) {
//      std::cout << i << " and " << d << std::endl;
//    }
//
// Prints "2 and 3.5", "4 and 4.5" and "6 and 5.5".
template <class... Containers>
auto zip(Containers&... containers) {
  using TupleIt =
      TupleIterator<decltype(std::declval<Containers>().begin())...>;
  return base::make_iterator_range(TupleIt(containers.begin()...),
                                   TupleIt(containers.end()...));
}

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_ITERATOR_H_
