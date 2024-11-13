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
//
// -----------------------------------------------------------------------------
// File: container.h
// -----------------------------------------------------------------------------
//
// This header file provides Container-based versions of algorithmic functions
// within the C++ standard library. The following standard library sets of
// functions are covered within this file:
//
//   * Algorithmic <iterator> functions
//   * Algorithmic <numeric> functions
//   * <algorithm> functions
//
// The standard library functions operate on iterator ranges; the functions
// within this API operate on containers, though many return iterator ranges.
//
// All functions within this API are named with a `c_` prefix. Calls such as
// `absl::c_xx(container, ...) are equivalent to std:: functions such as
// `std::xx(std::begin(cont), std::end(cont), ...)`. Functions that act on
// iterators but not conceptually on iterator ranges (e.g. `std::iter_swap`)
// have no equivalent here.
//
// For template parameter and variable naming, `C` indicates the container type
// to which the function is applied, `Pred` indicates the predicate object type
// to be used by the function and `T` indicates the applicable element type.

#ifndef ABSL_ALGORITHM_CONTAINER_H_
#define ABSL_ALGORITHM_CONTAINER_H_

#include <algorithm>
#include <cassert>
#include <iterator>
#include <numeric>
#include <random>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/algorithm/algorithm.h"
#include "absl/base/config.h"
#include "absl/base/macros.h"
#include "absl/base/nullability.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_algorithm_internal {

// NOTE: it is important to defer to ADL lookup for building with C++ modules,
// especially for headers like <valarray> which are not visible from this file
// but specialize std::begin and std::end.
using std::begin;
using std::end;

// The type of the iterator given by begin(c) (possibly std::begin(c)).
// ContainerIter<const vector<T>> gives vector<T>::const_iterator,
// while ContainerIter<vector<T>> gives vector<T>::iterator.
template <typename C>
using ContainerIter = decltype(begin(std::declval<C&>()));

// An MSVC bug involving template parameter substitution requires us to use
// decltype() here instead of just std::pair.
template <typename C1, typename C2>
using ContainerIterPairType =
    decltype(std::make_pair(ContainerIter<C1>(), ContainerIter<C2>()));

template <typename C>
using ContainerDifferenceType = decltype(std::distance(
    std::declval<ContainerIter<C>>(), std::declval<ContainerIter<C>>()));

template <typename C>
using ContainerPointerType =
    typename std::iterator_traits<ContainerIter<C>>::pointer;

// container_algorithm_internal::c_begin and
// container_algorithm_internal::c_end are abbreviations for proper ADL
// lookup of std::begin and std::end, i.e.
//   using std::begin;
//   using std::end;
//   std::foo(begin(c), end(c));
// becomes
//   std::foo(container_algorithm_internal::c_begin(c),
//            container_algorithm_internal::c_end(c));
// These are meant for internal use only.

template <typename C>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17 ContainerIter<C> c_begin(C& c) {
  return begin(c);
}

template <typename C>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17 ContainerIter<C> c_end(C& c) {
  return end(c);
}

template <typename T>
struct IsUnorderedContainer : std::false_type {};

template <class Key, class T, class Hash, class KeyEqual, class Allocator>
struct IsUnorderedContainer<
    std::unordered_map<Key, T, Hash, KeyEqual, Allocator>> : std::true_type {};

template <class Key, class Hash, class KeyEqual, class Allocator>
struct IsUnorderedContainer<std::unordered_set<Key, Hash, KeyEqual, Allocator>>
    : std::true_type {};

}  // namespace container_algorithm_internal

// PUBLIC API

//------------------------------------------------------------------------------
// Abseil algorithm.h functions
//------------------------------------------------------------------------------

// c_linear_search()
//
// Container-based version of absl::linear_search() for performing a linear
// search within a container.
template <typename C, typename EqualityComparable>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_linear_search(
    const C& c, EqualityComparable&& value) {
  return linear_search(container_algorithm_internal::c_begin(c),
                       container_algorithm_internal::c_end(c),
                       std::forward<EqualityComparable>(value));
}

//------------------------------------------------------------------------------
// <iterator> algorithms
//------------------------------------------------------------------------------

// c_distance()
//
// Container-based version of the <iterator> `std::distance()` function to
// return the number of elements within a container.
template <typename C>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerDifferenceType<const C>
    c_distance(const C& c) {
  return std::distance(container_algorithm_internal::c_begin(c),
                       container_algorithm_internal::c_end(c));
}

//------------------------------------------------------------------------------
// <algorithm> Non-modifying sequence operations
//------------------------------------------------------------------------------

// c_all_of()
//
// Container-based version of the <algorithm> `std::all_of()` function to
// test if all elements within a container satisfy a condition.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_all_of(const C& c, Pred&& pred) {
  return std::all_of(container_algorithm_internal::c_begin(c),
                     container_algorithm_internal::c_end(c),
                     std::forward<Pred>(pred));
}

// c_any_of()
//
// Container-based version of the <algorithm> `std::any_of()` function to
// test if any element in a container fulfills a condition.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_any_of(const C& c, Pred&& pred) {
  return std::any_of(container_algorithm_internal::c_begin(c),
                     container_algorithm_internal::c_end(c),
                     std::forward<Pred>(pred));
}

// c_none_of()
//
// Container-based version of the <algorithm> `std::none_of()` function to
// test if no elements in a container fulfill a condition.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_none_of(const C& c, Pred&& pred) {
  return std::none_of(container_algorithm_internal::c_begin(c),
                      container_algorithm_internal::c_end(c),
                      std::forward<Pred>(pred));
}

// c_for_each()
//
// Container-based version of the <algorithm> `std::for_each()` function to
// apply a function to a container's elements.
template <typename C, typename Function>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 decay_t<Function> c_for_each(C&& c,
                                                                 Function&& f) {
  return std::for_each(container_algorithm_internal::c_begin(c),
                       container_algorithm_internal::c_end(c),
                       std::forward<Function>(f));
}

// c_find()
//
// Container-based version of the <algorithm> `std::find()` function to find
// the first element containing the passed value within a container value.
template <typename C, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<C>
    c_find(C& c, T&& value) {
  return std::find(container_algorithm_internal::c_begin(c),
                   container_algorithm_internal::c_end(c),
                   std::forward<T>(value));
}

// c_contains()
//
// Container-based version of the <algorithm> `std::ranges::contains()` C++23
// function to search a container for a value.
template <typename Sequence, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_contains(const Sequence& sequence,
                                                    T&& value) {
  return absl::c_find(sequence, std::forward<T>(value)) !=
         container_algorithm_internal::c_end(sequence);
}

// c_find_if()
//
// Container-based version of the <algorithm> `std::find_if()` function to find
// the first element in a container matching the given condition.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<C>
    c_find_if(C& c, Pred&& pred) {
  return std::find_if(container_algorithm_internal::c_begin(c),
                      container_algorithm_internal::c_end(c),
                      std::forward<Pred>(pred));
}

// c_find_if_not()
//
// Container-based version of the <algorithm> `std::find_if_not()` function to
// find the first element in a container not matching the given condition.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<C>
    c_find_if_not(C& c, Pred&& pred) {
  return std::find_if_not(container_algorithm_internal::c_begin(c),
                          container_algorithm_internal::c_end(c),
                          std::forward<Pred>(pred));
}

// c_find_end()
//
// Container-based version of the <algorithm> `std::find_end()` function to
// find the last subsequence within a container.
template <typename Sequence1, typename Sequence2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence1>
    c_find_end(Sequence1& sequence, Sequence2& subsequence) {
  return std::find_end(container_algorithm_internal::c_begin(sequence),
                       container_algorithm_internal::c_end(sequence),
                       container_algorithm_internal::c_begin(subsequence),
                       container_algorithm_internal::c_end(subsequence));
}

// Overload of c_find_end() for using a predicate evaluation other than `==` as
// the function's test condition.
template <typename Sequence1, typename Sequence2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence1>
    c_find_end(Sequence1& sequence, Sequence2& subsequence,
               BinaryPredicate&& pred) {
  return std::find_end(container_algorithm_internal::c_begin(sequence),
                       container_algorithm_internal::c_end(sequence),
                       container_algorithm_internal::c_begin(subsequence),
                       container_algorithm_internal::c_end(subsequence),
                       std::forward<BinaryPredicate>(pred));
}

// c_find_first_of()
//
// Container-based version of the <algorithm> `std::find_first_of()` function to
// find the first element within the container that is also within the options
// container.
template <typename C1, typename C2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<C1>
    c_find_first_of(C1& container, C2& options) {
  return std::find_first_of(container_algorithm_internal::c_begin(container),
                            container_algorithm_internal::c_end(container),
                            container_algorithm_internal::c_begin(options),
                            container_algorithm_internal::c_end(options));
}

// Overload of c_find_first_of() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename C1, typename C2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<C1>
    c_find_first_of(C1& container, C2& options, BinaryPredicate&& pred) {
  return std::find_first_of(container_algorithm_internal::c_begin(container),
                            container_algorithm_internal::c_end(container),
                            container_algorithm_internal::c_begin(options),
                            container_algorithm_internal::c_end(options),
                            std::forward<BinaryPredicate>(pred));
}

// c_adjacent_find()
//
// Container-based version of the <algorithm> `std::adjacent_find()` function to
// find equal adjacent elements within a container.
template <typename Sequence>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence>
    c_adjacent_find(Sequence& sequence) {
  return std::adjacent_find(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence));
}

// Overload of c_adjacent_find() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename Sequence, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence>
    c_adjacent_find(Sequence& sequence, BinaryPredicate&& pred) {
  return std::adjacent_find(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence),
                            std::forward<BinaryPredicate>(pred));
}

// c_count()
//
// Container-based version of the <algorithm> `std::count()` function to count
// values that match within a container.
template <typename C, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerDifferenceType<const C>
    c_count(const C& c, T&& value) {
  return std::count(container_algorithm_internal::c_begin(c),
                    container_algorithm_internal::c_end(c),
                    std::forward<T>(value));
}

// c_count_if()
//
// Container-based version of the <algorithm> `std::count_if()` function to
// count values matching a condition within a container.
template <typename C, typename Pred>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerDifferenceType<const C>
    c_count_if(const C& c, Pred&& pred) {
  return std::count_if(container_algorithm_internal::c_begin(c),
                       container_algorithm_internal::c_end(c),
                       std::forward<Pred>(pred));
}

// c_mismatch()
//
// Container-based version of the <algorithm> `std::mismatch()` function to
// return the first element where two ordered containers differ. Applies `==` to
// the first N elements of `c1` and `c2`, where N = min(size(c1), size(c2)).
template <typename C1, typename C2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIterPairType<C1, C2>
    c_mismatch(C1& c1, C2& c2) {
  return std::mismatch(container_algorithm_internal::c_begin(c1),
                       container_algorithm_internal::c_end(c1),
                       container_algorithm_internal::c_begin(c2),
                       container_algorithm_internal::c_end(c2));
}

// Overload of c_mismatch() for using a predicate evaluation other than `==` as
// the function's test condition. Applies `pred`to the first N elements of `c1`
// and `c2`, where N = min(size(c1), size(c2)).
template <typename C1, typename C2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIterPairType<C1, C2>
    c_mismatch(C1& c1, C2& c2, BinaryPredicate pred) {
  return std::mismatch(container_algorithm_internal::c_begin(c1),
                       container_algorithm_internal::c_end(c1),
                       container_algorithm_internal::c_begin(c2),
                       container_algorithm_internal::c_end(c2), pred);
}

// c_equal()
//
// Container-based version of the <algorithm> `std::equal()` function to
// test whether two containers are equal.
template <typename C1, typename C2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_equal(const C1& c1, const C2& c2) {
  return std::equal(container_algorithm_internal::c_begin(c1),
                    container_algorithm_internal::c_end(c1),
                    container_algorithm_internal::c_begin(c2),
                    container_algorithm_internal::c_end(c2));
}

// Overload of c_equal() for using a predicate evaluation other than `==` as
// the function's test condition.
template <typename C1, typename C2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_equal(const C1& c1, const C2& c2,
                                                 BinaryPredicate&& pred) {
  return std::equal(container_algorithm_internal::c_begin(c1),
                    container_algorithm_internal::c_end(c1),
                    container_algorithm_internal::c_begin(c2),
                    container_algorithm_internal::c_end(c2),
                    std::forward<BinaryPredicate>(pred));
}

// c_is_permutation()
//
// Container-based version of the <algorithm> `std::is_permutation()` function
// to test whether a container is a permutation of another.
template <typename C1, typename C2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_is_permutation(const C1& c1,
                                                          const C2& c2) {
  return std::is_permutation(container_algorithm_internal::c_begin(c1),
                             container_algorithm_internal::c_end(c1),
                             container_algorithm_internal::c_begin(c2),
                             container_algorithm_internal::c_end(c2));
}

// Overload of c_is_permutation() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename C1, typename C2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_is_permutation(
    const C1& c1, const C2& c2, BinaryPredicate&& pred) {
  return std::is_permutation(container_algorithm_internal::c_begin(c1),
                             container_algorithm_internal::c_end(c1),
                             container_algorithm_internal::c_begin(c2),
                             container_algorithm_internal::c_end(c2),
                             std::forward<BinaryPredicate>(pred));
}

// c_search()
//
// Container-based version of the <algorithm> `std::search()` function to search
// a container for a subsequence.
template <typename Sequence1, typename Sequence2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence1>
    c_search(Sequence1& sequence, Sequence2& subsequence) {
  return std::search(container_algorithm_internal::c_begin(sequence),
                     container_algorithm_internal::c_end(sequence),
                     container_algorithm_internal::c_begin(subsequence),
                     container_algorithm_internal::c_end(subsequence));
}

// Overload of c_search() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename Sequence1, typename Sequence2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence1>
    c_search(Sequence1& sequence, Sequence2& subsequence,
             BinaryPredicate&& pred) {
  return std::search(container_algorithm_internal::c_begin(sequence),
                     container_algorithm_internal::c_end(sequence),
                     container_algorithm_internal::c_begin(subsequence),
                     container_algorithm_internal::c_end(subsequence),
                     std::forward<BinaryPredicate>(pred));
}

// c_contains_subrange()
//
// Container-based version of the <algorithm> `std::ranges::contains_subrange()`
// C++23 function to search a container for a subsequence.
template <typename Sequence1, typename Sequence2>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_contains_subrange(
    Sequence1& sequence, Sequence2& subsequence) {
  return absl::c_search(sequence, subsequence) !=
         container_algorithm_internal::c_end(sequence);
}

// Overload of c_contains_subrange() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename Sequence1, typename Sequence2, typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool c_contains_subrange(
    Sequence1& sequence, Sequence2& subsequence, BinaryPredicate&& pred) {
  return absl::c_search(sequence, subsequence,
                        std::forward<BinaryPredicate>(pred)) !=
         container_algorithm_internal::c_end(sequence);
}

// c_search_n()
//
// Container-based version of the <algorithm> `std::search_n()` function to
// search a container for the first sequence of N elements.
template <typename Sequence, typename Size, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence>
    c_search_n(Sequence& sequence, Size count, T&& value) {
  return std::search_n(container_algorithm_internal::c_begin(sequence),
                       container_algorithm_internal::c_end(sequence), count,
                       std::forward<T>(value));
}

// Overload of c_search_n() for using a predicate evaluation other than
// `==` as the function's test condition.
template <typename Sequence, typename Size, typename T,
          typename BinaryPredicate>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20
    container_algorithm_internal::ContainerIter<Sequence>
    c_search_n(Sequence& sequence, Size count, T&& value,
               BinaryPredicate&& pred) {
  return std::search_n(container_algorithm_internal::c_begin(sequence),
                       container_algorithm_internal::c_end(sequence), count,
                       std::forward<T>(value),
                       std::forward<BinaryPredicate>(pred));
}

//------------------------------------------------------------------------------
// <algorithm> Modifying sequence operations
//------------------------------------------------------------------------------

// c_copy()
//
// Container-based version of the <algorithm> `std::copy()` function to copy a
// container's elements into an iterator.
template <typename InputSequence, typename OutputIterator>
OutputIterator c_copy(const InputSequence& input, OutputIterator output) {
  return std::copy(container_algorithm_internal::c_begin(input),
                   container_algorithm_internal::c_end(input), output);
}

// c_copy_n()
//
// Container-based version of the <algorithm> `std::copy_n()` function to copy a
// container's first N elements into an iterator.
template <typename C, typename Size, typename OutputIterator>
OutputIterator c_copy_n(const C& input, Size n, OutputIterator output) {
  return std::copy_n(container_algorithm_internal::c_begin(input), n, output);
}

// c_copy_if()
//
// Container-based version of the <algorithm> `std::copy_if()` function to copy
// a container's elements satisfying some condition into an iterator.
template <typename InputSequence, typename OutputIterator, typename Pred>
OutputIterator c_copy_if(const InputSequence& input, OutputIterator output,
                         Pred&& pred) {
  return std::copy_if(container_algorithm_internal::c_begin(input),
                      container_algorithm_internal::c_end(input), output,
                      std::forward<Pred>(pred));
}

// c_copy_backward()
//
// Container-based version of the <algorithm> `std::copy_backward()` function to
// copy a container's elements in reverse order into an iterator.
template <typename C, typename BidirectionalIterator>
BidirectionalIterator c_copy_backward(const C& src,
                                      BidirectionalIterator dest) {
  return std::copy_backward(container_algorithm_internal::c_begin(src),
                            container_algorithm_internal::c_end(src), dest);
}

// c_move()
//
// Container-based version of the <algorithm> `std::move()` function to move
// a container's elements into an iterator.
template <typename C, typename OutputIterator>
OutputIterator c_move(C&& src, OutputIterator dest) {
  return std::move(container_algorithm_internal::c_begin(src),
                   container_algorithm_internal::c_end(src), dest);
}

// c_move_backward()
//
// Container-based version of the <algorithm> `std::move_backward()` function to
// move a container's elements into an iterator in reverse order.
template <typename C, typename BidirectionalIterator>
BidirectionalIterator c_move_backward(C&& src, BidirectionalIterator dest) {
  return std::move_backward(container_algorithm_internal::c_begin(src),
                            container_algorithm_internal::c_end(src), dest);
}

// c_swap_ranges()
//
// Container-based version of the <algorithm> `std::swap_ranges()` function to
// swap a container's elements with another container's elements. Swaps the
// first N elements of `c1` and `c2`, where N = min(size(c1), size(c2)).
template <typename C1, typename C2>
container_algorithm_internal::ContainerIter<C2> c_swap_ranges(C1& c1, C2& c2) {
  auto first1 = container_algorithm_internal::c_begin(c1);
  auto last1 = container_algorithm_internal::c_end(c1);
  auto first2 = container_algorithm_internal::c_begin(c2);
  auto last2 = container_algorithm_internal::c_end(c2);

  using std::swap;
  for (; first1 != last1 && first2 != last2; ++first1, (void)++first2) {
    swap(*first1, *first2);
  }
  return first2;
}

// c_transform()
//
// Container-based version of the <algorithm> `std::transform()` function to
// transform a container's elements using the unary operation, storing the
// result in an iterator pointing to the last transformed element in the output
// range.
template <typename InputSequence, typename OutputIterator, typename UnaryOp>
OutputIterator c_transform(const InputSequence& input, OutputIterator output,
                           UnaryOp&& unary_op) {
  return std::transform(container_algorithm_internal::c_begin(input),
                        container_algorithm_internal::c_end(input), output,
                        std::forward<UnaryOp>(unary_op));
}

// Overload of c_transform() for performing a transformation using a binary
// predicate. Applies `binary_op` to the first N elements of `c1` and `c2`,
// where N = min(size(c1), size(c2)).
template <typename InputSequence1, typename InputSequence2,
          typename OutputIterator, typename BinaryOp>
OutputIterator c_transform(const InputSequence1& input1,
                           const InputSequence2& input2, OutputIterator output,
                           BinaryOp&& binary_op) {
  auto first1 = container_algorithm_internal::c_begin(input1);
  auto last1 = container_algorithm_internal::c_end(input1);
  auto first2 = container_algorithm_internal::c_begin(input2);
  auto last2 = container_algorithm_internal::c_end(input2);
  for (; first1 != last1 && first2 != last2;
       ++first1, (void)++first2, ++output) {
    *output = binary_op(*first1, *first2);
  }

  return output;
}

// c_replace()
//
// Container-based version of the <algorithm> `std::replace()` function to
// replace a container's elements of some value with a new value. The container
// is modified in place.
template <typename Sequence, typename T>
void c_replace(Sequence& sequence, const T& old_value, const T& new_value) {
  std::replace(container_algorithm_internal::c_begin(sequence),
               container_algorithm_internal::c_end(sequence), old_value,
               new_value);
}

// c_replace_if()
//
// Container-based version of the <algorithm> `std::replace_if()` function to
// replace a container's elements of some value with a new value based on some
// condition. The container is modified in place.
template <typename C, typename Pred, typename T>
void c_replace_if(C& c, Pred&& pred, T&& new_value) {
  std::replace_if(container_algorithm_internal::c_begin(c),
                  container_algorithm_internal::c_end(c),
                  std::forward<Pred>(pred), std::forward<T>(new_value));
}

// c_replace_copy()
//
// Container-based version of the <algorithm> `std::replace_copy()` function to
// replace a container's elements of some value with a new value  and return the
// results within an iterator.
template <typename C, typename OutputIterator, typename T>
OutputIterator c_replace_copy(const C& c, OutputIterator result, T&& old_value,
                              T&& new_value) {
  return std::replace_copy(container_algorithm_internal::c_begin(c),
                           container_algorithm_internal::c_end(c), result,
                           std::forward<T>(old_value),
                           std::forward<T>(new_value));
}

// c_replace_copy_if()
//
// Container-based version of the <algorithm> `std::replace_copy_if()` function
// to replace a container's elements of some value with a new value based on
// some condition, and return the results within an iterator.
template <typename C, typename OutputIterator, typename Pred, typename T>
OutputIterator c_replace_copy_if(const C& c, OutputIterator result, Pred&& pred,
                                 const T& new_value) {
  return std::replace_copy_if(container_algorithm_internal::c_begin(c),
                              container_algorithm_internal::c_end(c), result,
                              std::forward<Pred>(pred), new_value);
}

// c_fill()
//
// Container-based version of the <algorithm> `std::fill()` function to fill a
// container with some value.
template <typename C, typename T>
void c_fill(C& c, const T& value) {
  std::fill(container_algorithm_internal::c_begin(c),
            container_algorithm_internal::c_end(c), value);
}

// c_fill_n()
//
// Container-based version of the <algorithm> `std::fill_n()` function to fill
// the first N elements in a container with some value.
template <typename C, typename Size, typename T>
void c_fill_n(C& c, Size n, const T& value) {
  std::fill_n(container_algorithm_internal::c_begin(c), n, value);
}

// c_generate()
//
// Container-based version of the <algorithm> `std::generate()` function to
// assign a container's elements to the values provided by the given generator.
template <typename C, typename Generator>
void c_generate(C& c, Generator&& gen) {
  std::generate(container_algorithm_internal::c_begin(c),
                container_algorithm_internal::c_end(c),
                std::forward<Generator>(gen));
}

// c_generate_n()
//
// Container-based version of the <algorithm> `std::generate_n()` function to
// assign a container's first N elements to the values provided by the given
// generator.
template <typename C, typename Size, typename Generator>
container_algorithm_internal::ContainerIter<C> c_generate_n(C& c, Size n,
                                                            Generator&& gen) {
  return std::generate_n(container_algorithm_internal::c_begin(c), n,
                         std::forward<Generator>(gen));
}

// Note: `c_xx()` <algorithm> container versions for `remove()`, `remove_if()`,
// and `unique()` are omitted, because it's not clear whether or not such
// functions should call erase on their supplied sequences afterwards. Either
// behavior would be surprising for a different set of users.

// c_remove_copy()
//
// Container-based version of the <algorithm> `std::remove_copy()` function to
// copy a container's elements while removing any elements matching the given
// `value`.
template <typename C, typename OutputIterator, typename T>
OutputIterator c_remove_copy(const C& c, OutputIterator result,
                             const T& value) {
  return std::remove_copy(container_algorithm_internal::c_begin(c),
                          container_algorithm_internal::c_end(c), result,
                          value);
}

// c_remove_copy_if()
//
// Container-based version of the <algorithm> `std::remove_copy_if()` function
// to copy a container's elements while removing any elements matching the given
// condition.
template <typename C, typename OutputIterator, typename Pred>
OutputIterator c_remove_copy_if(const C& c, OutputIterator result,
                                Pred&& pred) {
  return std::remove_copy_if(container_algorithm_internal::c_begin(c),
                             container_algorithm_internal::c_end(c), result,
                             std::forward<Pred>(pred));
}

// c_unique_copy()
//
// Container-based version of the <algorithm> `std::unique_copy()` function to
// copy a container's elements while removing any elements containing duplicate
// values.
template <typename C, typename OutputIterator>
OutputIterator c_unique_copy(const C& c, OutputIterator result) {
  return std::unique_copy(container_algorithm_internal::c_begin(c),
                          container_algorithm_internal::c_end(c), result);
}

// Overload of c_unique_copy() for using a predicate evaluation other than
// `==` for comparing uniqueness of the element values.
template <typename C, typename OutputIterator, typename BinaryPredicate>
OutputIterator c_unique_copy(const C& c, OutputIterator result,
                             BinaryPredicate&& pred) {
  return std::unique_copy(container_algorithm_internal::c_begin(c),
                          container_algorithm_internal::c_end(c), result,
                          std::forward<BinaryPredicate>(pred));
}

// c_reverse()
//
// Container-based version of the <algorithm> `std::reverse()` function to
// reverse a container's elements.
template <typename Sequence>
void c_reverse(Sequence& sequence) {
  std::reverse(container_algorithm_internal::c_begin(sequence),
               container_algorithm_internal::c_end(sequence));
}

// c_reverse_copy()
//
// Container-based version of the <algorithm> `std::reverse()` function to
// reverse a container's elements and write them to an iterator range.
template <typename C, typename OutputIterator>
OutputIterator c_reverse_copy(const C& sequence, OutputIterator result) {
  return std::reverse_copy(container_algorithm_internal::c_begin(sequence),
                           container_algorithm_internal::c_end(sequence),
                           result);
}

// c_rotate()
//
// Container-based version of the <algorithm> `std::rotate()` function to
// shift a container's elements leftward such that the `middle` element becomes
// the first element in the container.
template <typename C,
          typename Iterator = container_algorithm_internal::ContainerIter<C>>
Iterator c_rotate(C& sequence, Iterator middle) {
  return absl::rotate(container_algorithm_internal::c_begin(sequence), middle,
                      container_algorithm_internal::c_end(sequence));
}

// c_rotate_copy()
//
// Container-based version of the <algorithm> `std::rotate_copy()` function to
// shift a container's elements leftward such that the `middle` element becomes
// the first element in a new iterator range.
template <typename C, typename OutputIterator>
OutputIterator c_rotate_copy(
    const C& sequence,
    container_algorithm_internal::ContainerIter<const C> middle,
    OutputIterator result) {
  return std::rotate_copy(container_algorithm_internal::c_begin(sequence),
                          middle, container_algorithm_internal::c_end(sequence),
                          result);
}

// c_shuffle()
//
// Container-based version of the <algorithm> `std::shuffle()` function to
// randomly shuffle elements within the container using a `gen()` uniform random
// number generator.
template <typename RandomAccessContainer, typename UniformRandomBitGenerator>
void c_shuffle(RandomAccessContainer& c, UniformRandomBitGenerator&& gen) {
  std::shuffle(container_algorithm_internal::c_begin(c),
               container_algorithm_internal::c_end(c),
               std::forward<UniformRandomBitGenerator>(gen));
}

// c_sample()
//
// Container-based version of the <algorithm> `std::sample()` function to
// randomly sample elements from the container without replacement using a
// `gen()` uniform random number generator and write them to an iterator range.
template <typename C, typename OutputIterator, typename Distance,
          typename UniformRandomBitGenerator>
OutputIterator c_sample(const C& c, OutputIterator result, Distance n,
                        UniformRandomBitGenerator&& gen) {
#if defined(__cpp_lib_sample) && __cpp_lib_sample >= 201603L
  return std::sample(container_algorithm_internal::c_begin(c),
                     container_algorithm_internal::c_end(c), result, n,
                     std::forward<UniformRandomBitGenerator>(gen));
#else
  // Fall back to a stable selection-sampling implementation.
  auto first = container_algorithm_internal::c_begin(c);
  Distance unsampled_elements = c_distance(c);
  n = (std::min)(n, unsampled_elements);
  for (; n != 0; ++first) {
    Distance r =
        std::uniform_int_distribution<Distance>(0, --unsampled_elements)(gen);
    if (r < n) {
      *result++ = *first;
      --n;
    }
  }
  return result;
#endif
}

//------------------------------------------------------------------------------
// <algorithm> Partition functions
//------------------------------------------------------------------------------

// c_is_partitioned()
//
// Container-based version of the <algorithm> `std::is_partitioned()` function
// to test whether all elements in the container for which `pred` returns `true`
// precede those for which `pred` is `false`.
template <typename C, typename Pred>
bool c_is_partitioned(const C& c, Pred&& pred) {
  return std::is_partitioned(container_algorithm_internal::c_begin(c),
                             container_algorithm_internal::c_end(c),
                             std::forward<Pred>(pred));
}

// c_partition()
//
// Container-based version of the <algorithm> `std::partition()` function
// to rearrange all elements in a container in such a way that all elements for
// which `pred` returns `true` precede all those for which it returns `false`,
// returning an iterator to the first element of the second group.
template <typename C, typename Pred>
container_algorithm_internal::ContainerIter<C> c_partition(C& c, Pred&& pred) {
  return std::partition(container_algorithm_internal::c_begin(c),
                        container_algorithm_internal::c_end(c),
                        std::forward<Pred>(pred));
}

// c_stable_partition()
//
// Container-based version of the <algorithm> `std::stable_partition()` function
// to rearrange all elements in a container in such a way that all elements for
// which `pred` returns `true` precede all those for which it returns `false`,
// preserving the relative ordering between the two groups. The function returns
// an iterator to the first element of the second group.
template <typename C, typename Pred>
container_algorithm_internal::ContainerIter<C> c_stable_partition(C& c,
                                                                  Pred&& pred) {
  return std::stable_partition(container_algorithm_internal::c_begin(c),
                               container_algorithm_internal::c_end(c),
                               std::forward<Pred>(pred));
}

// c_partition_copy()
//
// Container-based version of the <algorithm> `std::partition_copy()` function
// to partition a container's elements and return them into two iterators: one
// for which `pred` returns `true`, and one for which `pred` returns `false.`

template <typename C, typename OutputIterator1, typename OutputIterator2,
          typename Pred>
std::pair<OutputIterator1, OutputIterator2> c_partition_copy(
    const C& c, OutputIterator1 out_true, OutputIterator2 out_false,
    Pred&& pred) {
  return std::partition_copy(container_algorithm_internal::c_begin(c),
                             container_algorithm_internal::c_end(c), out_true,
                             out_false, std::forward<Pred>(pred));
}

// c_partition_point()
//
// Container-based version of the <algorithm> `std::partition_point()` function
// to return the first element of an already partitioned container for which
// the given `pred` is not `true`.
template <typename C, typename Pred>
container_algorithm_internal::ContainerIter<C> c_partition_point(C& c,
                                                                 Pred&& pred) {
  return std::partition_point(container_algorithm_internal::c_begin(c),
                              container_algorithm_internal::c_end(c),
                              std::forward<Pred>(pred));
}

//------------------------------------------------------------------------------
// <algorithm> Sorting functions
//------------------------------------------------------------------------------

// c_sort()
//
// Container-based version of the <algorithm> `std::sort()` function
// to sort elements in ascending order of their values.
template <typename C>
void c_sort(C& c) {
  std::sort(container_algorithm_internal::c_begin(c),
            container_algorithm_internal::c_end(c));
}

// Overload of c_sort() for performing a `comp` comparison other than the
// default `operator<`.
template <typename C, typename LessThan>
void c_sort(C& c, LessThan&& comp) {
  std::sort(container_algorithm_internal::c_begin(c),
            container_algorithm_internal::c_end(c),
            std::forward<LessThan>(comp));
}

// c_stable_sort()
//
// Container-based version of the <algorithm> `std::stable_sort()` function
// to sort elements in ascending order of their values, preserving the order
// of equivalents.
template <typename C>
void c_stable_sort(C& c) {
  std::stable_sort(container_algorithm_internal::c_begin(c),
                   container_algorithm_internal::c_end(c));
}

// Overload of c_stable_sort() for performing a `comp` comparison other than the
// default `operator<`.
template <typename C, typename LessThan>
void c_stable_sort(C& c, LessThan&& comp) {
  std::stable_sort(container_algorithm_internal::c_begin(c),
                   container_algorithm_internal::c_end(c),
                   std::forward<LessThan>(comp));
}

// c_is_sorted()
//
// Container-based version of the <algorithm> `std::is_sorted()` function
// to evaluate whether the given container is sorted in ascending order.
template <typename C>
bool c_is_sorted(const C& c) {
  return std::is_sorted(container_algorithm_internal::c_begin(c),
                        container_algorithm_internal::c_end(c));
}

// c_is_sorted() overload for performing a `comp` comparison other than the
// default `operator<`.
template <typename C, typename LessThan>
bool c_is_sorted(const C& c, LessThan&& comp) {
  return std::is_sorted(container_algorithm_internal::c_begin(c),
                        container_algorithm_internal::c_end(c),
                        std::forward<LessThan>(comp));
}

// c_partial_sort()
//
// Container-based version of the <algorithm> `std::partial_sort()` function
// to rearrange elements within a container such that elements before `middle`
// are sorted in ascending order.
template <typename RandomAccessContainer>
void c_partial_sort(
    RandomAccessContainer& sequence,
    container_algorithm_internal::ContainerIter<RandomAccessContainer> middle) {
  std::partial_sort(container_algorithm_internal::c_begin(sequence), middle,
                    container_algorithm_internal::c_end(sequence));
}

// Overload of c_partial_sort() for performing a `comp` comparison other than
// the default `operator<`.
template <typename RandomAccessContainer, typename LessThan>
void c_partial_sort(
    RandomAccessContainer& sequence,
    container_algorithm_internal::ContainerIter<RandomAccessContainer> middle,
    LessThan&& comp) {
  std::partial_sort(container_algorithm_internal::c_begin(sequence), middle,
                    container_algorithm_internal::c_end(sequence),
                    std::forward<LessThan>(comp));
}

// c_partial_sort_copy()
//
// Container-based version of the <algorithm> `std::partial_sort_copy()`
// function to sort the elements in the given range `result` within the larger
// `sequence` in ascending order (and using `result` as the output parameter).
// At most min(result.last - result.first, sequence.last - sequence.first)
// elements from the sequence will be stored in the result.
template <typename C, typename RandomAccessContainer>
container_algorithm_internal::ContainerIter<RandomAccessContainer>
c_partial_sort_copy(const C& sequence, RandomAccessContainer& result) {
  return std::partial_sort_copy(container_algorithm_internal::c_begin(sequence),
                                container_algorithm_internal::c_end(sequence),
                                container_algorithm_internal::c_begin(result),
                                container_algorithm_internal::c_end(result));
}

// Overload of c_partial_sort_copy() for performing a `comp` comparison other
// than the default `operator<`.
template <typename C, typename RandomAccessContainer, typename LessThan>
container_algorithm_internal::ContainerIter<RandomAccessContainer>
c_partial_sort_copy(const C& sequence, RandomAccessContainer& result,
                    LessThan&& comp) {
  return std::partial_sort_copy(container_algorithm_internal::c_begin(sequence),
                                container_algorithm_internal::c_end(sequence),
                                container_algorithm_internal::c_begin(result),
                                container_algorithm_internal::c_end(result),
                                std::forward<LessThan>(comp));
}

// c_is_sorted_until()
//
// Container-based version of the <algorithm> `std::is_sorted_until()` function
// to return the first element within a container that is not sorted in
// ascending order as an iterator.
template <typename C>
container_algorithm_internal::ContainerIter<C> c_is_sorted_until(C& c) {
  return std::is_sorted_until(container_algorithm_internal::c_begin(c),
                              container_algorithm_internal::c_end(c));
}

// Overload of c_is_sorted_until() for performing a `comp` comparison other than
// the default `operator<`.
template <typename C, typename LessThan>
container_algorithm_internal::ContainerIter<C> c_is_sorted_until(
    C& c, LessThan&& comp) {
  return std::is_sorted_until(container_algorithm_internal::c_begin(c),
                              container_algorithm_internal::c_end(c),
                              std::forward<LessThan>(comp));
}

// c_nth_element()
//
// Container-based version of the <algorithm> `std::nth_element()` function
// to rearrange the elements within a container such that the `nth` element
// would be in that position in an ordered sequence; other elements may be in
// any order, except that all preceding `nth` will be less than that element,
// and all following `nth` will be greater than that element.
template <typename RandomAccessContainer>
void c_nth_element(
    RandomAccessContainer& sequence,
    container_algorithm_internal::ContainerIter<RandomAccessContainer> nth) {
  std::nth_element(container_algorithm_internal::c_begin(sequence), nth,
                   container_algorithm_internal::c_end(sequence));
}

// Overload of c_nth_element() for performing a `comp` comparison other than
// the default `operator<`.
template <typename RandomAccessContainer, typename LessThan>
void c_nth_element(
    RandomAccessContainer& sequence,
    container_algorithm_internal::ContainerIter<RandomAccessContainer> nth,
    LessThan&& comp) {
  std::nth_element(container_algorithm_internal::c_begin(sequence), nth,
                   container_algorithm_internal::c_end(sequence),
                   std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
// <algorithm> Binary Search
//------------------------------------------------------------------------------

// c_lower_bound()
//
// Container-based version of the <algorithm> `std::lower_bound()` function
// to return an iterator pointing to the first element in a sorted container
// which does not compare less than `value`.
template <typename Sequence, typename T>
container_algorithm_internal::ContainerIter<Sequence> c_lower_bound(
    Sequence& sequence, const T& value) {
  return std::lower_bound(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value);
}

// Overload of c_lower_bound() for performing a `comp` comparison other than
// the default `operator<`.
template <typename Sequence, typename T, typename LessThan>
container_algorithm_internal::ContainerIter<Sequence> c_lower_bound(
    Sequence& sequence, const T& value, LessThan&& comp) {
  return std::lower_bound(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value,
                          std::forward<LessThan>(comp));
}

// c_upper_bound()
//
// Container-based version of the <algorithm> `std::upper_bound()` function
// to return an iterator pointing to the first element in a sorted container
// which is greater than `value`.
template <typename Sequence, typename T>
container_algorithm_internal::ContainerIter<Sequence> c_upper_bound(
    Sequence& sequence, const T& value) {
  return std::upper_bound(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value);
}

// Overload of c_upper_bound() for performing a `comp` comparison other than
// the default `operator<`.
template <typename Sequence, typename T, typename LessThan>
container_algorithm_internal::ContainerIter<Sequence> c_upper_bound(
    Sequence& sequence, const T& value, LessThan&& comp) {
  return std::upper_bound(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value,
                          std::forward<LessThan>(comp));
}

// c_equal_range()
//
// Container-based version of the <algorithm> `std::equal_range()` function
// to return an iterator pair pointing to the first and last elements in a
// sorted container which compare equal to `value`.
template <typename Sequence, typename T>
container_algorithm_internal::ContainerIterPairType<Sequence, Sequence>
c_equal_range(Sequence& sequence, const T& value) {
  return std::equal_range(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value);
}

// Overload of c_equal_range() for performing a `comp` comparison other than
// the default `operator<`.
template <typename Sequence, typename T, typename LessThan>
container_algorithm_internal::ContainerIterPairType<Sequence, Sequence>
c_equal_range(Sequence& sequence, const T& value, LessThan&& comp) {
  return std::equal_range(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence), value,
                          std::forward<LessThan>(comp));
}

// c_binary_search()
//
// Container-based version of the <algorithm> `std::binary_search()` function
// to test if any element in the sorted container contains a value equivalent to
// 'value'.
template <typename Sequence, typename T>
bool c_binary_search(const Sequence& sequence, const T& value) {
  return std::binary_search(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence),
                            value);
}

// Overload of c_binary_search() for performing a `comp` comparison other than
// the default `operator<`.
template <typename Sequence, typename T, typename LessThan>
bool c_binary_search(const Sequence& sequence, const T& value,
                     LessThan&& comp) {
  return std::binary_search(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence),
                            value, std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
// <algorithm> Merge functions
//------------------------------------------------------------------------------

// c_merge()
//
// Container-based version of the <algorithm> `std::merge()` function
// to merge two sorted containers into a single sorted iterator.
template <typename C1, typename C2, typename OutputIterator>
OutputIterator c_merge(const C1& c1, const C2& c2, OutputIterator result) {
  return std::merge(container_algorithm_internal::c_begin(c1),
                    container_algorithm_internal::c_end(c1),
                    container_algorithm_internal::c_begin(c2),
                    container_algorithm_internal::c_end(c2), result);
}

// Overload of c_merge() for performing a `comp` comparison other than
// the default `operator<`.
template <typename C1, typename C2, typename OutputIterator, typename LessThan>
OutputIterator c_merge(const C1& c1, const C2& c2, OutputIterator result,
                       LessThan&& comp) {
  return std::merge(container_algorithm_internal::c_begin(c1),
                    container_algorithm_internal::c_end(c1),
                    container_algorithm_internal::c_begin(c2),
                    container_algorithm_internal::c_end(c2), result,
                    std::forward<LessThan>(comp));
}

// c_inplace_merge()
//
// Container-based version of the <algorithm> `std::inplace_merge()` function
// to merge a supplied iterator `middle` into a container.
template <typename C>
void c_inplace_merge(C& c,
                     container_algorithm_internal::ContainerIter<C> middle) {
  std::inplace_merge(container_algorithm_internal::c_begin(c), middle,
                     container_algorithm_internal::c_end(c));
}

// Overload of c_inplace_merge() for performing a merge using a `comp` other
// than `operator<`.
template <typename C, typename LessThan>
void c_inplace_merge(C& c,
                     container_algorithm_internal::ContainerIter<C> middle,
                     LessThan&& comp) {
  std::inplace_merge(container_algorithm_internal::c_begin(c), middle,
                     container_algorithm_internal::c_end(c),
                     std::forward<LessThan>(comp));
}

// c_includes()
//
// Container-based version of the <algorithm> `std::includes()` function
// to test whether a sorted container `c1` entirely contains another sorted
// container `c2`.
template <typename C1, typename C2>
bool c_includes(const C1& c1, const C2& c2) {
  return std::includes(container_algorithm_internal::c_begin(c1),
                       container_algorithm_internal::c_end(c1),
                       container_algorithm_internal::c_begin(c2),
                       container_algorithm_internal::c_end(c2));
}

// Overload of c_includes() for performing a merge using a `comp` other than
// `operator<`.
template <typename C1, typename C2, typename LessThan>
bool c_includes(const C1& c1, const C2& c2, LessThan&& comp) {
  return std::includes(container_algorithm_internal::c_begin(c1),
                       container_algorithm_internal::c_end(c1),
                       container_algorithm_internal::c_begin(c2),
                       container_algorithm_internal::c_end(c2),
                       std::forward<LessThan>(comp));
}

// c_set_union()
//
// Container-based version of the <algorithm> `std::set_union()` function
// to return an iterator containing the union of two containers; duplicate
// values are not copied into the output.
template <typename C1, typename C2, typename OutputIterator,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_union(const C1& c1, const C2& c2, OutputIterator output) {
  return std::set_union(container_algorithm_internal::c_begin(c1),
                        container_algorithm_internal::c_end(c1),
                        container_algorithm_internal::c_begin(c2),
                        container_algorithm_internal::c_end(c2), output);
}

// Overload of c_set_union() for performing a merge using a `comp` other than
// `operator<`.
template <typename C1, typename C2, typename OutputIterator, typename LessThan,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_union(const C1& c1, const C2& c2, OutputIterator output,
                           LessThan&& comp) {
  return std::set_union(container_algorithm_internal::c_begin(c1),
                        container_algorithm_internal::c_end(c1),
                        container_algorithm_internal::c_begin(c2),
                        container_algorithm_internal::c_end(c2), output,
                        std::forward<LessThan>(comp));
}

// c_set_intersection()
//
// Container-based version of the <algorithm> `std::set_intersection()` function
// to return an iterator containing the intersection of two sorted containers.
template <typename C1, typename C2, typename OutputIterator,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_intersection(const C1& c1, const C2& c2,
                                  OutputIterator output) {
  // In debug builds, ensure that both containers are sorted with respect to the
  // default comparator. std::set_intersection requires the containers be sorted
  // using operator<.
  assert(absl::c_is_sorted(c1));
  assert(absl::c_is_sorted(c2));
  return std::set_intersection(container_algorithm_internal::c_begin(c1),
                               container_algorithm_internal::c_end(c1),
                               container_algorithm_internal::c_begin(c2),
                               container_algorithm_internal::c_end(c2), output);
}

// Overload of c_set_intersection() for performing a merge using a `comp` other
// than `operator<`.
template <typename C1, typename C2, typename OutputIterator, typename LessThan,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_intersection(const C1& c1, const C2& c2,
                                  OutputIterator output, LessThan&& comp) {
  // In debug builds, ensure that both containers are sorted with respect to the
  // default comparator. std::set_intersection requires the containers be sorted
  // using the same comparator.
  assert(absl::c_is_sorted(c1, comp));
  assert(absl::c_is_sorted(c2, comp));
  return std::set_intersection(container_algorithm_internal::c_begin(c1),
                               container_algorithm_internal::c_end(c1),
                               container_algorithm_internal::c_begin(c2),
                               container_algorithm_internal::c_end(c2), output,
                               std::forward<LessThan>(comp));
}

// c_set_difference()
//
// Container-based version of the <algorithm> `std::set_difference()` function
// to return an iterator containing elements present in the first container but
// not in the second.
template <typename C1, typename C2, typename OutputIterator,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_difference(const C1& c1, const C2& c2,
                                OutputIterator output) {
  return std::set_difference(container_algorithm_internal::c_begin(c1),
                             container_algorithm_internal::c_end(c1),
                             container_algorithm_internal::c_begin(c2),
                             container_algorithm_internal::c_end(c2), output);
}

// Overload of c_set_difference() for performing a merge using a `comp` other
// than `operator<`.
template <typename C1, typename C2, typename OutputIterator, typename LessThan,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_difference(const C1& c1, const C2& c2,
                                OutputIterator output, LessThan&& comp) {
  return std::set_difference(container_algorithm_internal::c_begin(c1),
                             container_algorithm_internal::c_end(c1),
                             container_algorithm_internal::c_begin(c2),
                             container_algorithm_internal::c_end(c2), output,
                             std::forward<LessThan>(comp));
}

// c_set_symmetric_difference()
//
// Container-based version of the <algorithm> `std::set_symmetric_difference()`
// function to return an iterator containing elements present in either one
// container or the other, but not both.
template <typename C1, typename C2, typename OutputIterator,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_symmetric_difference(const C1& c1, const C2& c2,
                                          OutputIterator output) {
  return std::set_symmetric_difference(
      container_algorithm_internal::c_begin(c1),
      container_algorithm_internal::c_end(c1),
      container_algorithm_internal::c_begin(c2),
      container_algorithm_internal::c_end(c2), output);
}

// Overload of c_set_symmetric_difference() for performing a merge using a
// `comp` other than `operator<`.
template <typename C1, typename C2, typename OutputIterator, typename LessThan,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C1>::value,
              void>::type,
          typename = typename std::enable_if<
              !container_algorithm_internal::IsUnorderedContainer<C2>::value,
              void>::type>
OutputIterator c_set_symmetric_difference(const C1& c1, const C2& c2,
                                          OutputIterator output,
                                          LessThan&& comp) {
  return std::set_symmetric_difference(
      container_algorithm_internal::c_begin(c1),
      container_algorithm_internal::c_end(c1),
      container_algorithm_internal::c_begin(c2),
      container_algorithm_internal::c_end(c2), output,
      std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
// <algorithm> Heap functions
//------------------------------------------------------------------------------

// c_push_heap()
//
// Container-based version of the <algorithm> `std::push_heap()` function
// to push a value onto a container heap.
template <typename RandomAccessContainer>
void c_push_heap(RandomAccessContainer& sequence) {
  std::push_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence));
}

// Overload of c_push_heap() for performing a push operation on a heap using a
// `comp` other than `operator<`.
template <typename RandomAccessContainer, typename LessThan>
void c_push_heap(RandomAccessContainer& sequence, LessThan&& comp) {
  std::push_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence),
                 std::forward<LessThan>(comp));
}

// c_pop_heap()
//
// Container-based version of the <algorithm> `std::pop_heap()` function
// to pop a value from a heap container.
template <typename RandomAccessContainer>
void c_pop_heap(RandomAccessContainer& sequence) {
  std::pop_heap(container_algorithm_internal::c_begin(sequence),
                container_algorithm_internal::c_end(sequence));
}

// Overload of c_pop_heap() for performing a pop operation on a heap using a
// `comp` other than `operator<`.
template <typename RandomAccessContainer, typename LessThan>
void c_pop_heap(RandomAccessContainer& sequence, LessThan&& comp) {
  std::pop_heap(container_algorithm_internal::c_begin(sequence),
                container_algorithm_internal::c_end(sequence),
                std::forward<LessThan>(comp));
}

// c_make_heap()
//
// Container-based version of the <algorithm> `std::make_heap()` function
// to make a container a heap.
template <typename RandomAccessContainer>
void c_make_heap(RandomAccessContainer& sequence) {
  std::make_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence));
}

// Overload of c_make_heap() for performing heap comparisons using a
// `comp` other than `operator<`
template <typename RandomAccessContainer, typename LessThan>
void c_make_heap(RandomAccessContainer& sequence, LessThan&& comp) {
  std::make_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence),
                 std::forward<LessThan>(comp));
}

// c_sort_heap()
//
// Container-based version of the <algorithm> `std::sort_heap()` function
// to sort a heap into ascending order (after which it is no longer a heap).
template <typename RandomAccessContainer>
void c_sort_heap(RandomAccessContainer& sequence) {
  std::sort_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence));
}

// Overload of c_sort_heap() for performing heap comparisons using a
// `comp` other than `operator<`
template <typename RandomAccessContainer, typename LessThan>
void c_sort_heap(RandomAccessContainer& sequence, LessThan&& comp) {
  std::sort_heap(container_algorithm_internal::c_begin(sequence),
                 container_algorithm_internal::c_end(sequence),
                 std::forward<LessThan>(comp));
}

// c_is_heap()
//
// Container-based version of the <algorithm> `std::is_heap()` function
// to check whether the given container is a heap.
template <typename RandomAccessContainer>
bool c_is_heap(const RandomAccessContainer& sequence) {
  return std::is_heap(container_algorithm_internal::c_begin(sequence),
                      container_algorithm_internal::c_end(sequence));
}

// Overload of c_is_heap() for performing heap comparisons using a
// `comp` other than `operator<`
template <typename RandomAccessContainer, typename LessThan>
bool c_is_heap(const RandomAccessContainer& sequence, LessThan&& comp) {
  return std::is_heap(container_algorithm_internal::c_begin(sequence),
                      container_algorithm_internal::c_end(sequence),
                      std::forward<LessThan>(comp));
}

// c_is_heap_until()
//
// Container-based version of the <algorithm> `std::is_heap_until()` function
// to find the first element in a given container which is not in heap order.
template <typename RandomAccessContainer>
container_algorithm_internal::ContainerIter<RandomAccessContainer>
c_is_heap_until(RandomAccessContainer& sequence) {
  return std::is_heap_until(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence));
}

// Overload of c_is_heap_until() for performing heap comparisons using a
// `comp` other than `operator<`
template <typename RandomAccessContainer, typename LessThan>
container_algorithm_internal::ContainerIter<RandomAccessContainer>
c_is_heap_until(RandomAccessContainer& sequence, LessThan&& comp) {
  return std::is_heap_until(container_algorithm_internal::c_begin(sequence),
                            container_algorithm_internal::c_end(sequence),
                            std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
//  <algorithm> Min/max
//------------------------------------------------------------------------------

// c_min_element()
//
// Container-based version of the <algorithm> `std::min_element()` function
// to return an iterator pointing to the element with the smallest value, using
// `operator<` to make the comparisons.
template <typename Sequence>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIter<Sequence>
    c_min_element(Sequence& sequence) {
  return std::min_element(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence));
}

// Overload of c_min_element() for performing a `comp` comparison other than
// `operator<`.
template <typename Sequence, typename LessThan>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIter<Sequence>
    c_min_element(Sequence& sequence, LessThan&& comp) {
  return std::min_element(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence),
                          std::forward<LessThan>(comp));
}

// c_max_element()
//
// Container-based version of the <algorithm> `std::max_element()` function
// to return an iterator pointing to the element with the largest value, using
// `operator<` to make the comparisons.
template <typename Sequence>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIter<Sequence>
    c_max_element(Sequence& sequence) {
  return std::max_element(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence));
}

// Overload of c_max_element() for performing a `comp` comparison other than
// `operator<`.
template <typename Sequence, typename LessThan>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIter<Sequence>
    c_max_element(Sequence& sequence, LessThan&& comp) {
  return std::max_element(container_algorithm_internal::c_begin(sequence),
                          container_algorithm_internal::c_end(sequence),
                          std::forward<LessThan>(comp));
}

// c_minmax_element()
//
// Container-based version of the <algorithm> `std::minmax_element()` function
// to return a pair of iterators pointing to the elements containing the
// smallest and largest values, respectively, using `operator<` to make the
// comparisons.
template <typename C>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIterPairType<C, C>
    c_minmax_element(C& c) {
  return std::minmax_element(container_algorithm_internal::c_begin(c),
                             container_algorithm_internal::c_end(c));
}

// Overload of c_minmax_element() for performing `comp` comparisons other than
// `operator<`.
template <typename C, typename LessThan>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX17
    container_algorithm_internal::ContainerIterPairType<C, C>
    c_minmax_element(C& c, LessThan&& comp) {
  return std::minmax_element(container_algorithm_internal::c_begin(c),
                             container_algorithm_internal::c_end(c),
                             std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
//  <algorithm> Lexicographical Comparisons
//------------------------------------------------------------------------------

// c_lexicographical_compare()
//
// Container-based version of the <algorithm> `std::lexicographical_compare()`
// function to lexicographically compare (e.g. sort words alphabetically) two
// container sequences. The comparison is performed using `operator<`. Note
// that capital letters ("A-Z") have ASCII values less than lowercase letters
// ("a-z").
template <typename Sequence1, typename Sequence2>
bool c_lexicographical_compare(const Sequence1& sequence1,
                               const Sequence2& sequence2) {
  return std::lexicographical_compare(
      container_algorithm_internal::c_begin(sequence1),
      container_algorithm_internal::c_end(sequence1),
      container_algorithm_internal::c_begin(sequence2),
      container_algorithm_internal::c_end(sequence2));
}

// Overload of c_lexicographical_compare() for performing a lexicographical
// comparison using a `comp` operator instead of `operator<`.
template <typename Sequence1, typename Sequence2, typename LessThan>
bool c_lexicographical_compare(const Sequence1& sequence1,
                               const Sequence2& sequence2, LessThan&& comp) {
  return std::lexicographical_compare(
      container_algorithm_internal::c_begin(sequence1),
      container_algorithm_internal::c_end(sequence1),
      container_algorithm_internal::c_begin(sequence2),
      container_algorithm_internal::c_end(sequence2),
      std::forward<LessThan>(comp));
}

// c_next_permutation()
//
// Container-based version of the <algorithm> `std::next_permutation()` function
// to rearrange a container's elements into the next lexicographically greater
// permutation.
template <typename C>
bool c_next_permutation(C& c) {
  return std::next_permutation(container_algorithm_internal::c_begin(c),
                               container_algorithm_internal::c_end(c));
}

// Overload of c_next_permutation() for performing a lexicographical
// comparison using a `comp` operator instead of `operator<`.
template <typename C, typename LessThan>
bool c_next_permutation(C& c, LessThan&& comp) {
  return std::next_permutation(container_algorithm_internal::c_begin(c),
                               container_algorithm_internal::c_end(c),
                               std::forward<LessThan>(comp));
}

// c_prev_permutation()
//
// Container-based version of the <algorithm> `std::prev_permutation()` function
// to rearrange a container's elements into the next lexicographically lesser
// permutation.
template <typename C>
bool c_prev_permutation(C& c) {
  return std::prev_permutation(container_algorithm_internal::c_begin(c),
                               container_algorithm_internal::c_end(c));
}

// Overload of c_prev_permutation() for performing a lexicographical
// comparison using a `comp` operator instead of `operator<`.
template <typename C, typename LessThan>
bool c_prev_permutation(C& c, LessThan&& comp) {
  return std::prev_permutation(container_algorithm_internal::c_begin(c),
                               container_algorithm_internal::c_end(c),
                               std::forward<LessThan>(comp));
}

//------------------------------------------------------------------------------
// <numeric> algorithms
//------------------------------------------------------------------------------

// c_iota()
//
// Container-based version of the <numeric> `std::iota()` function
// to compute successive values of `value`, as if incremented with `++value`
// after each element is written, and write them to the container.
template <typename Sequence, typename T>
void c_iota(Sequence& sequence, const T& value) {
  std::iota(container_algorithm_internal::c_begin(sequence),
            container_algorithm_internal::c_end(sequence), value);
}

// c_accumulate()
//
// Container-based version of the <numeric> `std::accumulate()` function
// to accumulate the element values of a container to `init` and return that
// accumulation by value.
//
// Note: Due to a language technicality this function has return type
// absl::decay_t<T>. As a user of this function you can casually read
// this as "returns T by value" and assume it does the right thing.
template <typename Sequence, typename T>
decay_t<T> c_accumulate(const Sequence& sequence, T&& init) {
  return std::accumulate(container_algorithm_internal::c_begin(sequence),
                         container_algorithm_internal::c_end(sequence),
                         std::forward<T>(init));
}

// Overload of c_accumulate() for using a binary operations other than
// addition for computing the accumulation.
template <typename Sequence, typename T, typename BinaryOp>
decay_t<T> c_accumulate(const Sequence& sequence, T&& init,
                        BinaryOp&& binary_op) {
  return std::accumulate(container_algorithm_internal::c_begin(sequence),
                         container_algorithm_internal::c_end(sequence),
                         std::forward<T>(init),
                         std::forward<BinaryOp>(binary_op));
}

// c_inner_product()
//
// Container-based version of the <numeric> `std::inner_product()` function
// to compute the cumulative inner product of container element pairs.
//
// Note: Due to a language technicality this function has return type
// absl::decay_t<T>. As a user of this function you can casually read
// this as "returns T by value" and assume it does the right thing.
template <typename Sequence1, typename Sequence2, typename T>
decay_t<T> c_inner_product(const Sequence1& factors1, const Sequence2& factors2,
                           T&& sum) {
  return std::inner_product(container_algorithm_internal::c_begin(factors1),
                            container_algorithm_internal::c_end(factors1),
                            container_algorithm_internal::c_begin(factors2),
                            std::forward<T>(sum));
}

// Overload of c_inner_product() for using binary operations other than
// `operator+` (for computing the accumulation) and `operator*` (for computing
// the product between the two container's element pair).
template <typename Sequence1, typename Sequence2, typename T,
          typename BinaryOp1, typename BinaryOp2>
decay_t<T> c_inner_product(const Sequence1& factors1, const Sequence2& factors2,
                           T&& sum, BinaryOp1&& op1, BinaryOp2&& op2) {
  return std::inner_product(container_algorithm_internal::c_begin(factors1),
                            container_algorithm_internal::c_end(factors1),
                            container_algorithm_internal::c_begin(factors2),
                            std::forward<T>(sum), std::forward<BinaryOp1>(op1),
                            std::forward<BinaryOp2>(op2));
}

// c_adjacent_difference()
//
// Container-based version of the <numeric> `std::adjacent_difference()`
// function to compute the difference between each element and the one preceding
// it and write it to an iterator.
template <typename InputSequence, typename OutputIt>
OutputIt c_adjacent_difference(const InputSequence& input,
                               OutputIt output_first) {
  return std::adjacent_difference(container_algorithm_internal::c_begin(input),
                                  container_algorithm_internal::c_end(input),
                                  output_first);
}

// Overload of c_adjacent_difference() for using a binary operation other than
// subtraction to compute the adjacent difference.
template <typename InputSequence, typename OutputIt, typename BinaryOp>
OutputIt c_adjacent_difference(const InputSequence& input,
                               OutputIt output_first, BinaryOp&& op) {
  return std::adjacent_difference(container_algorithm_internal::c_begin(input),
                                  container_algorithm_internal::c_end(input),
                                  output_first, std::forward<BinaryOp>(op));
}

// c_partial_sum()
//
// Container-based version of the <numeric> `std::partial_sum()` function
// to compute the partial sum of the elements in a sequence and write them
// to an iterator. The partial sum is the sum of all element values so far in
// the sequence.
template <typename InputSequence, typename OutputIt>
OutputIt c_partial_sum(const InputSequence& input, OutputIt output_first) {
  return std::partial_sum(container_algorithm_internal::c_begin(input),
                          container_algorithm_internal::c_end(input),
                          output_first);
}

// Overload of c_partial_sum() for using a binary operation other than addition
// to compute the "partial sum".
template <typename InputSequence, typename OutputIt, typename BinaryOp>
OutputIt c_partial_sum(const InputSequence& input, OutputIt output_first,
                       BinaryOp&& op) {
  return std::partial_sum(container_algorithm_internal::c_begin(input),
                          container_algorithm_internal::c_end(input),
                          output_first, std::forward<BinaryOp>(op));
}

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_ALGORITHM_CONTAINER_H_
