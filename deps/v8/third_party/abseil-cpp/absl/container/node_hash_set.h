// Copyright 2018 The Abseil Authors.
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
// File: node_hash_set.h
// -----------------------------------------------------------------------------
//
// An `absl::node_hash_set<T>` is an unordered associative container designed to
// be a more efficient replacement for `std::unordered_set`. Like
// `unordered_set`, search, insertion, and deletion of set elements can be done
// as an `O(1)` operation. However, `node_hash_set` (and other unordered
// associative containers known as the collection of Abseil "Swiss tables")
// contain other optimizations that result in both memory and computation
// advantages.
//
// In most cases, your default choice for a hash table should be a map of type
// `flat_hash_map` or a set of type `flat_hash_set`. However, if you need
// pointer stability, a `node_hash_set` should be your preferred choice. As
// well, if you are migrating your code from using `std::unordered_set`, a
// `node_hash_set` should be an easy migration. Consider migrating to
// `node_hash_set` and perhaps converting to a more efficient `flat_hash_set`
// upon further review.

#ifndef ABSL_CONTAINER_NODE_HASH_SET_H_
#define ABSL_CONTAINER_NODE_HASH_SET_H_

#include <cstddef>
#include <type_traits>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"  // IWYU pragma: export
#include "absl/container/internal/node_slot_policy.h"
#include "absl/container/internal/raw_hash_set.h"  // IWYU pragma: export
#include "absl/memory/memory.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
template <typename T>
struct NodeHashSetPolicy;
}  // namespace container_internal

// -----------------------------------------------------------------------------
// absl::node_hash_set
// -----------------------------------------------------------------------------
//
// An `absl::node_hash_set<T>` is an unordered associative container which
// has been optimized for both speed and memory footprint in most common use
// cases. Its interface is similar to that of `std::unordered_set<T>` with the
// following notable differences:
//
// * Supports heterogeneous lookup, through `find()`, `operator[]()` and
//   `insert()`, provided that the set is provided a compatible heterogeneous
//   hashing function and equality operator. See below for details.
// * Contains a `capacity()` member function indicating the number of element
//   slots (open, deleted, and empty) within the hash set.
// * Returns `void` from the `erase(iterator)` overload.
//
// By default, `node_hash_set` uses the `absl::Hash` hashing framework.
// All fundamental and Abseil types that support the `absl::Hash` framework have
// a compatible equality operator for comparing insertions into `node_hash_set`.
// If your type is not yet supported by the `absl::Hash` framework, see
// absl/hash/hash.h for information on extending Abseil hashing to user-defined
// types.
//
// Using `absl::node_hash_set` at interface boundaries in dynamically loaded
// libraries (e.g. .dll, .so) is unsupported due to way `absl::Hash` values may
// be randomized across dynamically loaded libraries.
//
// To achieve heterogeneous lookup for custom types either `Hash` and `Eq` type
// parameters can be used or `T` should have public inner types
// `absl_container_hash` and (optionally) `absl_container_eq`. In either case,
// `typename Hash::is_transparent` and `typename Eq::is_transparent` should be
// well-formed. Both types are basically functors:
// * `Hash` should support `size_t operator()(U val) const` that returns a hash
// for the given `val`.
// * `Eq` should support `bool operator()(U lhs, V rhs) const` that returns true
// if `lhs` is equal to `rhs`.
//
// In most cases `T` needs only to provide the `absl_container_hash`. In this
// case `std::equal_to<void>` will be used instead of `eq` part.
//
// Example:
//
//   // Create a node hash set of three strings
//   absl::node_hash_set<std::string> ducks =
//     {"huey", "dewey", "louie"};
//
//  // Insert a new element into the node hash set
//  ducks.insert("donald");
//
//  // Force a rehash of the node hash set
//  ducks.rehash(0);
//
//  // See if "dewey" is present
//  if (ducks.contains("dewey")) {
//    std::cout << "We found dewey!" << std::endl;
//  }
template <class T, class Hash = absl::container_internal::hash_default_hash<T>,
          class Eq = absl::container_internal::hash_default_eq<T>,
          class Alloc = std::allocator<T>>
class node_hash_set
    : public absl::container_internal::raw_hash_set<
          absl::container_internal::NodeHashSetPolicy<T>, Hash, Eq, Alloc> {
  using Base = typename node_hash_set::raw_hash_set;

 public:
  // Constructors and Assignment Operators
  //
  // A node_hash_set supports the same overload set as `std::unordered_set`
  // for construction and assignment:
  //
  // *  Default constructor
  //
  //    // No allocation for the table's elements is made.
  //    absl::node_hash_set<std::string> set1;
  //
  // * Initializer List constructor
  //
  //   absl::node_hash_set<std::string> set2 =
  //       {{"huey"}, {"dewey"}, {"louie"}};
  //
  // * Copy constructor
  //
  //   absl::node_hash_set<std::string> set3(set2);
  //
  // * Copy assignment operator
  //
  //  // Hash functor and Comparator are copied as well
  //  absl::node_hash_set<std::string> set4;
  //  set4 = set3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::node_hash_set<std::string> set5(std::move(set4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::node_hash_set<std::string> set6;
  //   set6 = std::move(set5);
  //
  // * Range constructor
  //
  //   std::vector<std::string> v = {"a", "b"};
  //   absl::node_hash_set<std::string> set7(v.begin(), v.end());
  node_hash_set() {}
  using Base::Base;

  // node_hash_set::begin()
  //
  // Returns an iterator to the beginning of the `node_hash_set`.
  using Base::begin;

  // node_hash_set::cbegin()
  //
  // Returns a const iterator to the beginning of the `node_hash_set`.
  using Base::cbegin;

  // node_hash_set::cend()
  //
  // Returns a const iterator to the end of the `node_hash_set`.
  using Base::cend;

  // node_hash_set::end()
  //
  // Returns an iterator to the end of the `node_hash_set`.
  using Base::end;

  // node_hash_set::capacity()
  //
  // Returns the number of element slots (assigned, deleted, and empty)
  // available within the `node_hash_set`.
  //
  // NOTE: this member function is particular to `absl::node_hash_set` and is
  // not provided in the `std::unordered_set` API.
  using Base::capacity;

  // node_hash_set::empty()
  //
  // Returns whether or not the `node_hash_set` is empty.
  using Base::empty;

  // node_hash_set::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `node_hash_set` under current memory constraints. This value can be thought
  // of the largest value of `std::distance(begin(), end())` for a
  // `node_hash_set<T>`.
  using Base::max_size;

  // node_hash_set::size()
  //
  // Returns the number of elements currently within the `node_hash_set`.
  using Base::size;

  // node_hash_set::clear()
  //
  // Removes all elements from the `node_hash_set`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  //
  // NOTE: this operation may shrink the underlying buffer. To avoid shrinking
  // the underlying buffer call `erase(begin(), end())`.
  using Base::clear;

  // node_hash_set::erase()
  //
  // Erases elements within the `node_hash_set`. Erasing does not trigger a
  // rehash. Overloads are listed below.
  //
  // void erase(const_iterator pos):
  //
  //   Erases the element at `position` of the `node_hash_set`, returning
  //   `void`.
  //
  //   NOTE: this return behavior is different than that of STL containers in
  //   general and `std::unordered_set` in particular.
  //
  // iterator erase(const_iterator first, const_iterator last):
  //
  //   Erases the elements in the open interval [`first`, `last`), returning an
  //   iterator pointing to `last`. The special case of calling
  //   `erase(begin(), end())` resets the reserved growth such that if
  //   `reserve(N)` has previously been called and there has been no intervening
  //   call to `clear()`, then after calling `erase(begin(), end())`, it is safe
  //   to assume that inserting N elements will not cause a rehash.
  //
  // size_type erase(const key_type& key):
  //
  //   Erases the element with the matching key, if it exists, returning the
  //   number of elements erased (0 or 1).
  using Base::erase;

  // node_hash_set::insert()
  //
  // Inserts an element of the specified value into the `node_hash_set`,
  // returning an iterator pointing to the newly inserted element, provided that
  // an element with the given key does not already exist. If rehashing occurs
  // due to the insertion, all iterators are invalidated. Overloads are listed
  // below.
  //
  // std::pair<iterator,bool> insert(const T& value):
  //
  //   Inserts a value into the `node_hash_set`. Returns a pair consisting of an
  //   iterator to the inserted element (or to the element that prevented the
  //   insertion) and a bool denoting whether the insertion took place.
  //
  // std::pair<iterator,bool> insert(T&& value):
  //
  //   Inserts a moveable value into the `node_hash_set`. Returns a pair
  //   consisting of an iterator to the inserted element (or to the element that
  //   prevented the insertion) and a bool denoting whether the insertion took
  //   place.
  //
  // iterator insert(const_iterator hint, const T& value):
  // iterator insert(const_iterator hint, T&& value):
  //
  //   Inserts a value, using the position of `hint` as a non-binding suggestion
  //   for where to begin the insertion search. Returns an iterator to the
  //   inserted element, or to the existing element that prevented the
  //   insertion.
  //
  // void insert(InputIterator first, InputIterator last):
  //
  //   Inserts a range of values [`first`, `last`).
  //
  //   NOTE: Although the STL does not specify which element may be inserted if
  //   multiple keys compare equivalently, for `node_hash_set` we guarantee the
  //   first match is inserted.
  //
  // void insert(std::initializer_list<T> ilist):
  //
  //   Inserts the elements within the initializer list `ilist`.
  //
  //   NOTE: Although the STL does not specify which element may be inserted if
  //   multiple keys compare equivalently within the initializer list, for
  //   `node_hash_set` we guarantee the first match is inserted.
  using Base::insert;

  // node_hash_set::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `node_hash_set`, provided that no element with the given key
  // already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  using Base::emplace;

  // node_hash_set::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `node_hash_set`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search, and only inserts
  // provided that no element with the given key already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  using Base::emplace_hint;

  // node_hash_set::extract()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle. Overloads are listed below.
  //
  // node_type extract(const_iterator position):
  //
  //   Extracts the element at the indicated position and returns a node handle
  //   owning that extracted data.
  //
  // node_type extract(const key_type& x):
  //
  //   Extracts the element with the key matching the passed key value and
  //   returns a node handle owning that extracted data. If the `node_hash_set`
  //   does not contain an element with a matching key, this function returns an
  // empty node handle.
  using Base::extract;

  // node_hash_set::merge()
  //
  // Extracts elements from a given `source` node hash set into this
  // `node_hash_set`. If the destination `node_hash_set` already contains an
  // element with an equivalent key, that element is not extracted.
  using Base::merge;

  // node_hash_set::swap(node_hash_set& other)
  //
  // Exchanges the contents of this `node_hash_set` with those of the `other`
  // node hash set, avoiding invocation of any move, copy, or swap operations on
  // individual elements.
  //
  // All iterators and references on the `node_hash_set` remain valid, excepting
  // for the past-the-end iterator, which is invalidated.
  //
  // `swap()` requires that the node hash set's hashing and key equivalence
  // functions be Swappable, and are exchanged using unqualified calls to
  // non-member `swap()`. If the set's allocator has
  // `std::allocator_traits<allocator_type>::propagate_on_container_swap::value`
  // set to `true`, the allocators are also exchanged using an unqualified call
  // to non-member `swap()`; otherwise, the allocators are not swapped.
  using Base::swap;

  // node_hash_set::rehash(count)
  //
  // Rehashes the `node_hash_set`, setting the number of slots to be at least
  // the passed value. If the new number of slots increases the load factor more
  // than the current maximum load factor
  // (`count` < `size()` / `max_load_factor()`), then the new number of slots
  // will be at least `size()` / `max_load_factor()`.
  //
  // To force a rehash, pass rehash(0).
  //
  // NOTE: unlike behavior in `std::unordered_set`, references are also
  // invalidated upon a `rehash()`.
  using Base::rehash;

  // node_hash_set::reserve(count)
  //
  // Sets the number of slots in the `node_hash_set` to the number needed to
  // accommodate at least `count` total elements without exceeding the current
  // maximum load factor, and may rehash the container if needed.
  using Base::reserve;

  // node_hash_set::contains()
  //
  // Determines whether an element comparing equal to the given `key` exists
  // within the `node_hash_set`, returning `true` if so or `false` otherwise.
  using Base::contains;

  // node_hash_set::count(const Key& key) const
  //
  // Returns the number of elements comparing equal to the given `key` within
  // the `node_hash_set`. note that this function will return either `1` or `0`
  // since duplicate elements are not allowed within a `node_hash_set`.
  using Base::count;

  // node_hash_set::equal_range()
  //
  // Returns a closed range [first, last], defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `node_hash_set`.
  using Base::equal_range;

  // node_hash_set::find()
  //
  // Finds an element with the passed `key` within the `node_hash_set`.
  using Base::find;

  // node_hash_set::bucket_count()
  //
  // Returns the number of "buckets" within the `node_hash_set`. Note that
  // because a node hash set contains all elements within its internal storage,
  // this value simply equals the current capacity of the `node_hash_set`.
  using Base::bucket_count;

  // node_hash_set::load_factor()
  //
  // Returns the current load factor of the `node_hash_set` (the average number
  // of slots occupied with a value within the hash set).
  using Base::load_factor;

  // node_hash_set::max_load_factor()
  //
  // Manages the maximum load factor of the `node_hash_set`. Overloads are
  // listed below.
  //
  // float node_hash_set::max_load_factor()
  //
  //   Returns the current maximum load factor of the `node_hash_set`.
  //
  // void node_hash_set::max_load_factor(float ml)
  //
  //   Sets the maximum load factor of the `node_hash_set` to the passed value.
  //
  //   NOTE: This overload is provided only for API compatibility with the STL;
  //   `node_hash_set` will ignore any set load factor and manage its rehashing
  //   internally as an implementation detail.
  using Base::max_load_factor;

  // node_hash_set::get_allocator()
  //
  // Returns the allocator function associated with this `node_hash_set`.
  using Base::get_allocator;

  // node_hash_set::hash_function()
  //
  // Returns the hashing function used to hash the keys within this
  // `node_hash_set`.
  using Base::hash_function;

  // node_hash_set::key_eq()
  //
  // Returns the function used for comparing keys equality.
  using Base::key_eq;
};

// erase_if(node_hash_set<>, Pred)
//
// Erases all elements that satisfy the predicate `pred` from the container `c`.
// Returns the number of erased elements.
template <typename T, typename H, typename E, typename A, typename Predicate>
typename node_hash_set<T, H, E, A>::size_type erase_if(
    node_hash_set<T, H, E, A>& c, Predicate pred) {
  return container_internal::EraseIf(pred, &c);
}

namespace container_internal {

template <class T>
struct NodeHashSetPolicy
    : absl::container_internal::node_slot_policy<T&, NodeHashSetPolicy<T>> {
  using key_type = T;
  using init_type = T;
  using constant_iterators = std::true_type;

  template <class Allocator, class... Args>
  static T* new_element(Allocator* alloc, Args&&... args) {
    using ValueAlloc =
        typename absl::allocator_traits<Allocator>::template rebind_alloc<T>;
    ValueAlloc value_alloc(*alloc);
    T* res = absl::allocator_traits<ValueAlloc>::allocate(value_alloc, 1);
    absl::allocator_traits<ValueAlloc>::construct(value_alloc, res,
                                                  std::forward<Args>(args)...);
    return res;
  }

  template <class Allocator>
  static void delete_element(Allocator* alloc, T* elem) {
    using ValueAlloc =
        typename absl::allocator_traits<Allocator>::template rebind_alloc<T>;
    ValueAlloc value_alloc(*alloc);
    absl::allocator_traits<ValueAlloc>::destroy(value_alloc, elem);
    absl::allocator_traits<ValueAlloc>::deallocate(value_alloc, elem, 1);
  }

  template <class F, class... Args>
  static decltype(absl::container_internal::DecomposeValue(
      std::declval<F>(), std::declval<Args>()...))
  apply(F&& f, Args&&... args) {
    return absl::container_internal::DecomposeValue(
        std::forward<F>(f), std::forward<Args>(args)...);
  }

  static size_t element_space_used(const T*) { return sizeof(T); }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return &TypeErasedDerefAndApplyToSlotFn<Hash, T>;
  }
};
}  // namespace container_internal

namespace container_algorithm_internal {

// Specialization of trait in absl/algorithm/container.h
template <class Key, class Hash, class KeyEqual, class Allocator>
struct IsUnorderedContainer<absl::node_hash_set<Key, Hash, KeyEqual, Allocator>>
    : std::true_type {};

}  // namespace container_algorithm_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_NODE_HASH_SET_H_
