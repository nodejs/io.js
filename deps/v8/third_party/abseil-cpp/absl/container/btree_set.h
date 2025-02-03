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
// File: btree_set.h
// -----------------------------------------------------------------------------
//
// This header file defines B-tree sets: sorted associative containers of
// values.
//
//     * `absl::btree_set<>`
//     * `absl::btree_multiset<>`
//
// These B-tree types are similar to the corresponding types in the STL
// (`std::set` and `std::multiset`) and generally conform to the STL interfaces
// of those types. However, because they are implemented using B-trees, they
// are more efficient in most situations.
//
// Unlike `std::set` and `std::multiset`, which are commonly implemented using
// red-black tree nodes, B-tree sets use more generic B-tree nodes able to hold
// multiple values per node. Holding multiple values per node often makes
// B-tree sets perform better than their `std::set` counterparts, because
// multiple entries can be checked within the same cache hit.
//
// However, these types should not be considered drop-in replacements for
// `std::set` and `std::multiset` as there are some API differences, which are
// noted in this header file. The most consequential differences with respect to
// migrating to b-tree from the STL types are listed in the next paragraph.
// Other API differences are minor.
//
// Importantly, insertions and deletions may invalidate outstanding iterators,
// pointers, and references to elements. Such invalidations are typically only
// an issue if insertion and deletion operations are interleaved with the use of
// more than one iterator, pointer, or reference simultaneously. For this
// reason, `insert()`, `erase()`, and `extract_and_get_next()` return a valid
// iterator at the current position.
//
// Another API difference is that btree iterators can be subtracted, and this
// is faster than using std::distance.
//
// B-tree sets are not exception-safe.

#ifndef ABSL_CONTAINER_BTREE_SET_H_
#define ABSL_CONTAINER_BTREE_SET_H_

#include "absl/base/attributes.h"
#include "absl/container/internal/btree.h"  // IWYU pragma: export
#include "absl/container/internal/btree_container.h"  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace container_internal {

template <typename Key>
struct set_slot_policy;

template <typename Key, typename Compare, typename Alloc, int TargetNodeSize,
          bool IsMulti>
struct set_params;

}  // namespace container_internal

// absl::btree_set<>
//
// An `absl::btree_set<K>` is an ordered associative container of unique key
// values designed to be a more efficient replacement for `std::set` (in most
// cases).
//
// Keys are sorted using an (optional) comparison function, which defaults to
// `std::less<K>`.
//
// An `absl::btree_set<K>` uses a default allocator of `std::allocator<K>` to
// allocate (and deallocate) nodes, and construct and destruct values within
// those nodes. You may instead specify a custom allocator `A` (which in turn
// requires specifying a custom comparator `C`) as in
// `absl::btree_set<K, C, A>`.
//
template <typename Key, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<Key>>
class ABSL_ATTRIBUTE_OWNER btree_set
    : public container_internal::btree_set_container<
          container_internal::btree<container_internal::set_params<
              Key, Compare, Alloc, /*TargetNodeSize=*/256,
              /*IsMulti=*/false>>> {
  using Base = typename btree_set::btree_set_container;

 public:
  // Constructors and Assignment Operators
  //
  // A `btree_set` supports the same overload set as `std::set`
  // for construction and assignment:
  //
  // * Default constructor
  //
  //   absl::btree_set<std::string> set1;
  //
  // * Initializer List constructor
  //
  //   absl::btree_set<std::string> set2 =
  //       {{"huey"}, {"dewey"}, {"louie"},};
  //
  // * Copy constructor
  //
  //   absl::btree_set<std::string> set3(set2);
  //
  // * Copy assignment operator
  //
  //  absl::btree_set<std::string> set4;
  //  set4 = set3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::btree_set<std::string> set5(std::move(set4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::btree_set<std::string> set6;
  //   set6 = std::move(set5);
  //
  // * Range constructor
  //
  //   std::vector<std::string> v = {"a", "b"};
  //   absl::btree_set<std::string> set7(v.begin(), v.end());
  btree_set() {}
  using Base::Base;

  // btree_set::begin()
  //
  // Returns an iterator to the beginning of the `btree_set`.
  using Base::begin;

  // btree_set::cbegin()
  //
  // Returns a const iterator to the beginning of the `btree_set`.
  using Base::cbegin;

  // btree_set::end()
  //
  // Returns an iterator to the end of the `btree_set`.
  using Base::end;

  // btree_set::cend()
  //
  // Returns a const iterator to the end of the `btree_set`.
  using Base::cend;

  // btree_set::empty()
  //
  // Returns whether or not the `btree_set` is empty.
  using Base::empty;

  // btree_set::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `btree_set` under current memory constraints. This value can be thought
  // of as the largest value of `std::distance(begin(), end())` for a
  // `btree_set<Key>`.
  using Base::max_size;

  // btree_set::size()
  //
  // Returns the number of elements currently within the `btree_set`.
  using Base::size;

  // btree_set::clear()
  //
  // Removes all elements from the `btree_set`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  using Base::clear;

  // btree_set::erase()
  //
  // Erases elements within the `btree_set`. Overloads are listed below.
  //
  // iterator erase(iterator position):
  // iterator erase(const_iterator position):
  //
  //   Erases the element at `position` of the `btree_set`, returning
  //   the iterator pointing to the element after the one that was erased
  //   (or end() if none exists).
  //
  // iterator erase(const_iterator first, const_iterator last):
  //
  //   Erases the elements in the open interval [`first`, `last`), returning
  //   the iterator pointing to the element after the interval that was erased
  //   (or end() if none exists).
  //
  // template <typename K> size_type erase(const K& key):
  //
  //   Erases the element with the matching key, if it exists, returning the
  //   number of elements erased (0 or 1).
  using Base::erase;

  // btree_set::insert()
  //
  // Inserts an element of the specified value into the `btree_set`,
  // returning an iterator pointing to the newly inserted element, provided that
  // an element with the given key does not already exist. If an insertion
  // occurs, any references, pointers, or iterators are invalidated.
  // Overloads are listed below.
  //
  // std::pair<iterator,bool> insert(const value_type& value):
  //
  //   Inserts a value into the `btree_set`. Returns a pair consisting of an
  //   iterator to the inserted element (or to the element that prevented the
  //   insertion) and a bool denoting whether the insertion took place.
  //
  // std::pair<iterator,bool> insert(value_type&& value):
  //
  //   Inserts a moveable value into the `btree_set`. Returns a pair
  //   consisting of an iterator to the inserted element (or to the element that
  //   prevented the insertion) and a bool denoting whether the insertion took
  //   place.
  //
  // iterator insert(const_iterator hint, const value_type& value):
  // iterator insert(const_iterator hint, value_type&& value):
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
  // void insert(std::initializer_list<init_type> ilist):
  //
  //   Inserts the elements within the initializer list `ilist`.
  using Base::insert;

  // btree_set::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_set`, provided that no element with the given key
  // already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated.
  using Base::emplace;

  // btree_set::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_set`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search, and only inserts
  // provided that no element with the given key already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated.
  using Base::emplace_hint;

  // btree_set::extract()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle. Any references, pointers, or iterators
  // are invalidated. Overloads are listed below.
  //
  // node_type extract(const_iterator position):
  //
  //   Extracts the element at the indicated position and returns a node handle
  //   owning that extracted data.
  //
  // template <typename K> node_type extract(const K& k):
  //
  //   Extracts the element with the key matching the passed key value and
  //   returns a node handle owning that extracted data. If the `btree_set`
  //   does not contain an element with a matching key, this function returns an
  //   empty node handle.
  //
  // NOTE: In this context, `node_type` refers to the C++17 concept of a
  // move-only type that owns and provides access to the elements in associative
  // containers (https://en.cppreference.com/w/cpp/container/node_handle).
  // It does NOT refer to the data layout of the underlying btree.
  using Base::extract;

  // btree_set::extract_and_get_next()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle along with an iterator to the next
  // element.
  //
  // extract_and_get_next_return_type extract_and_get_next(
  //     const_iterator position):
  //
  //   Extracts the element at the indicated position, returns a struct
  //   containing a member named `node`: a node handle owning that extracted
  //   data and a member named `next`: an iterator pointing to the next element
  //   in the btree.
  using Base::extract_and_get_next;

  // btree_set::merge()
  //
  // Extracts elements from a given `source` btree_set into this
  // `btree_set`. If the destination `btree_set` already contains an
  // element with an equivalent key, that element is not extracted.
  using Base::merge;

  // btree_set::swap(btree_set& other)
  //
  // Exchanges the contents of this `btree_set` with those of the `other`
  // btree_set, avoiding invocation of any move, copy, or swap operations on
  // individual elements.
  //
  // All iterators and references on the `btree_set` remain valid, excepting
  // for the past-the-end iterator, which is invalidated.
  using Base::swap;

  // btree_set::contains()
  //
  // template <typename K> bool contains(const K& key) const:
  //
  // Determines whether an element comparing equal to the given `key` exists
  // within the `btree_set`, returning `true` if so or `false` otherwise.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::contains;

  // btree_set::count()
  //
  // template <typename K> size_type count(const K& key) const:
  //
  // Returns the number of elements comparing equal to the given `key` within
  // the `btree_set`. Note that this function will return either `1` or `0`
  // since duplicate elements are not allowed within a `btree_set`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::count;

  // btree_set::equal_range()
  //
  // Returns a closed range [first, last], defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `btree_set`.
  using Base::equal_range;

  // btree_set::find()
  //
  // template <typename K> iterator find(const K& key):
  // template <typename K> const_iterator find(const K& key) const:
  //
  // Finds an element with the passed `key` within the `btree_set`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::find;

  // btree_set::lower_bound()
  //
  // template <typename K> iterator lower_bound(const K& key):
  // template <typename K> const_iterator lower_bound(const K& key) const:
  //
  // Finds the first element that is not less than `key` within the `btree_set`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::lower_bound;

  // btree_set::upper_bound()
  //
  // template <typename K> iterator upper_bound(const K& key):
  // template <typename K> const_iterator upper_bound(const K& key) const:
  //
  // Finds the first element that is greater than `key` within the `btree_set`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::upper_bound;

  // btree_set::get_allocator()
  //
  // Returns the allocator function associated with this `btree_set`.
  using Base::get_allocator;

  // btree_set::key_comp();
  //
  // Returns the key comparator associated with this `btree_set`.
  using Base::key_comp;

  // btree_set::value_comp();
  //
  // Returns the value comparator associated with this `btree_set`. The keys to
  // sort the elements are the values themselves, therefore `value_comp` and its
  // sibling member function `key_comp` are equivalent.
  using Base::value_comp;
};

// absl::swap(absl::btree_set<>, absl::btree_set<>)
//
// Swaps the contents of two `absl::btree_set` containers.
template <typename K, typename C, typename A>
void swap(btree_set<K, C, A> &x, btree_set<K, C, A> &y) {
  return x.swap(y);
}

// absl::erase_if(absl::btree_set<>, Pred)
//
// Erases all elements that satisfy the predicate pred from the container.
// Returns the number of erased elements.
template <typename K, typename C, typename A, typename Pred>
typename btree_set<K, C, A>::size_type erase_if(btree_set<K, C, A> &set,
                                                Pred pred) {
  return container_internal::btree_access::erase_if(set, std::move(pred));
}

// absl::btree_multiset<>
//
// An `absl::btree_multiset<K>` is an ordered associative container of
// keys and associated values designed to be a more efficient replacement
// for `std::multiset` (in most cases). Unlike `absl::btree_set`, a B-tree
// multiset allows equivalent elements.
//
// Keys are sorted using an (optional) comparison function, which defaults to
// `std::less<K>`.
//
// An `absl::btree_multiset<K>` uses a default allocator of `std::allocator<K>`
// to allocate (and deallocate) nodes, and construct and destruct values within
// those nodes. You may instead specify a custom allocator `A` (which in turn
// requires specifying a custom comparator `C`) as in
// `absl::btree_multiset<K, C, A>`.
//
template <typename Key, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<Key>>
class ABSL_ATTRIBUTE_OWNER btree_multiset
    : public container_internal::btree_multiset_container<
          container_internal::btree<container_internal::set_params<
              Key, Compare, Alloc, /*TargetNodeSize=*/256,
              /*IsMulti=*/true>>> {
  using Base = typename btree_multiset::btree_multiset_container;

 public:
  // Constructors and Assignment Operators
  //
  // A `btree_multiset` supports the same overload set as `std::set`
  // for construction and assignment:
  //
  // * Default constructor
  //
  //   absl::btree_multiset<std::string> set1;
  //
  // * Initializer List constructor
  //
  //   absl::btree_multiset<std::string> set2 =
  //       {{"huey"}, {"dewey"}, {"louie"},};
  //
  // * Copy constructor
  //
  //   absl::btree_multiset<std::string> set3(set2);
  //
  // * Copy assignment operator
  //
  //  absl::btree_multiset<std::string> set4;
  //  set4 = set3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::btree_multiset<std::string> set5(std::move(set4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::btree_multiset<std::string> set6;
  //   set6 = std::move(set5);
  //
  // * Range constructor
  //
  //   std::vector<std::string> v = {"a", "b"};
  //   absl::btree_multiset<std::string> set7(v.begin(), v.end());
  btree_multiset() {}
  using Base::Base;

  // btree_multiset::begin()
  //
  // Returns an iterator to the beginning of the `btree_multiset`.
  using Base::begin;

  // btree_multiset::cbegin()
  //
  // Returns a const iterator to the beginning of the `btree_multiset`.
  using Base::cbegin;

  // btree_multiset::end()
  //
  // Returns an iterator to the end of the `btree_multiset`.
  using Base::end;

  // btree_multiset::cend()
  //
  // Returns a const iterator to the end of the `btree_multiset`.
  using Base::cend;

  // btree_multiset::empty()
  //
  // Returns whether or not the `btree_multiset` is empty.
  using Base::empty;

  // btree_multiset::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `btree_multiset` under current memory constraints. This value can be
  // thought of as the largest value of `std::distance(begin(), end())` for a
  // `btree_multiset<Key>`.
  using Base::max_size;

  // btree_multiset::size()
  //
  // Returns the number of elements currently within the `btree_multiset`.
  using Base::size;

  // btree_multiset::clear()
  //
  // Removes all elements from the `btree_multiset`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  using Base::clear;

  // btree_multiset::erase()
  //
  // Erases elements within the `btree_multiset`. Overloads are listed below.
  //
  // iterator erase(iterator position):
  // iterator erase(const_iterator position):
  //
  //   Erases the element at `position` of the `btree_multiset`, returning
  //   the iterator pointing to the element after the one that was erased
  //   (or end() if none exists).
  //
  // iterator erase(const_iterator first, const_iterator last):
  //
  //   Erases the elements in the open interval [`first`, `last`), returning
  //   the iterator pointing to the element after the interval that was erased
  //   (or end() if none exists).
  //
  // template <typename K> size_type erase(const K& key):
  //
  //   Erases the elements matching the key, if any exist, returning the
  //   number of elements erased.
  using Base::erase;

  // btree_multiset::insert()
  //
  // Inserts an element of the specified value into the `btree_multiset`,
  // returning an iterator pointing to the newly inserted element.
  // Any references, pointers, or iterators are invalidated.  Overloads are
  // listed below.
  //
  // iterator insert(const value_type& value):
  //
  //   Inserts a value into the `btree_multiset`, returning an iterator to the
  //   inserted element.
  //
  // iterator insert(value_type&& value):
  //
  //   Inserts a moveable value into the `btree_multiset`, returning an iterator
  //   to the inserted element.
  //
  // iterator insert(const_iterator hint, const value_type& value):
  // iterator insert(const_iterator hint, value_type&& value):
  //
  //   Inserts a value, using the position of `hint` as a non-binding suggestion
  //   for where to begin the insertion search. Returns an iterator to the
  //   inserted element.
  //
  // void insert(InputIterator first, InputIterator last):
  //
  //   Inserts a range of values [`first`, `last`).
  //
  // void insert(std::initializer_list<init_type> ilist):
  //
  //   Inserts the elements within the initializer list `ilist`.
  using Base::insert;

  // btree_multiset::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_multiset`. Any references, pointers, or iterators are
  // invalidated.
  using Base::emplace;

  // btree_multiset::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_multiset`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search.
  //
  // Any references, pointers, or iterators are invalidated.
  using Base::emplace_hint;

  // btree_multiset::extract()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle. Overloads are listed below.
  //
  // node_type extract(const_iterator position):
  //
  //   Extracts the element at the indicated position and returns a node handle
  //   owning that extracted data.
  //
  // template <typename K> node_type extract(const K& k):
  //
  //   Extracts the element with the key matching the passed key value and
  //   returns a node handle owning that extracted data. If the `btree_multiset`
  //   does not contain an element with a matching key, this function returns an
  //   empty node handle.
  //
  // NOTE: In this context, `node_type` refers to the C++17 concept of a
  // move-only type that owns and provides access to the elements in associative
  // containers (https://en.cppreference.com/w/cpp/container/node_handle).
  // It does NOT refer to the data layout of the underlying btree.
  using Base::extract;

  // btree_multiset::extract_and_get_next()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle along with an iterator to the next
  // element.
  //
  // extract_and_get_next_return_type extract_and_get_next(
  //     const_iterator position):
  //
  //   Extracts the element at the indicated position, returns a struct
  //   containing a member named `node`: a node handle owning that extracted
  //   data and a member named `next`: an iterator pointing to the next element
  //   in the btree.
  using Base::extract_and_get_next;

  // btree_multiset::merge()
  //
  // Extracts all elements from a given `source` btree_multiset into this
  // `btree_multiset`.
  using Base::merge;

  // btree_multiset::swap(btree_multiset& other)
  //
  // Exchanges the contents of this `btree_multiset` with those of the `other`
  // btree_multiset, avoiding invocation of any move, copy, or swap operations
  // on individual elements.
  //
  // All iterators and references on the `btree_multiset` remain valid,
  // excepting for the past-the-end iterator, which is invalidated.
  using Base::swap;

  // btree_multiset::contains()
  //
  // template <typename K> bool contains(const K& key) const:
  //
  // Determines whether an element comparing equal to the given `key` exists
  // within the `btree_multiset`, returning `true` if so or `false` otherwise.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::contains;

  // btree_multiset::count()
  //
  // template <typename K> size_type count(const K& key) const:
  //
  // Returns the number of elements comparing equal to the given `key` within
  // the `btree_multiset`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::count;

  // btree_multiset::equal_range()
  //
  // Returns a closed range [first, last], defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `btree_multiset`.
  using Base::equal_range;

  // btree_multiset::find()
  //
  // template <typename K> iterator find(const K& key):
  // template <typename K> const_iterator find(const K& key) const:
  //
  // Finds an element with the passed `key` within the `btree_multiset`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::find;

  // btree_multiset::lower_bound()
  //
  // template <typename K> iterator lower_bound(const K& key):
  // template <typename K> const_iterator lower_bound(const K& key) const:
  //
  // Finds the first element that is not less than `key` within the
  // `btree_multiset`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::lower_bound;

  // btree_multiset::upper_bound()
  //
  // template <typename K> iterator upper_bound(const K& key):
  // template <typename K> const_iterator upper_bound(const K& key) const:
  //
  // Finds the first element that is greater than `key` within the
  // `btree_multiset`.
  //
  // Supports heterogeneous lookup, provided that the set has a compatible
  // heterogeneous comparator.
  using Base::upper_bound;

  // btree_multiset::get_allocator()
  //
  // Returns the allocator function associated with this `btree_multiset`.
  using Base::get_allocator;

  // btree_multiset::key_comp();
  //
  // Returns the key comparator associated with this `btree_multiset`.
  using Base::key_comp;

  // btree_multiset::value_comp();
  //
  // Returns the value comparator associated with this `btree_multiset`. The
  // keys to sort the elements are the values themselves, therefore `value_comp`
  // and its sibling member function `key_comp` are equivalent.
  using Base::value_comp;
};

// absl::swap(absl::btree_multiset<>, absl::btree_multiset<>)
//
// Swaps the contents of two `absl::btree_multiset` containers.
template <typename K, typename C, typename A>
void swap(btree_multiset<K, C, A> &x, btree_multiset<K, C, A> &y) {
  return x.swap(y);
}

// absl::erase_if(absl::btree_multiset<>, Pred)
//
// Erases all elements that satisfy the predicate pred from the container.
// Returns the number of erased elements.
template <typename K, typename C, typename A, typename Pred>
typename btree_multiset<K, C, A>::size_type erase_if(
   btree_multiset<K, C, A> & set, Pred pred) {
  return container_internal::btree_access::erase_if(set, std::move(pred));
}

namespace container_internal {

// This type implements the necessary functions from the
// absl::container_internal::slot_type interface for btree_(multi)set.
template <typename Key>
struct set_slot_policy {
  using slot_type = Key;
  using value_type = Key;
  using mutable_value_type = Key;

  static value_type &element(slot_type *slot) { return *slot; }
  static const value_type &element(const slot_type *slot) { return *slot; }

  template <typename Alloc, class... Args>
  static void construct(Alloc *alloc, slot_type *slot, Args &&...args) {
    absl::allocator_traits<Alloc>::construct(*alloc, slot,
                                             std::forward<Args>(args)...);
  }

  template <typename Alloc>
  static void construct(Alloc *alloc, slot_type *slot, slot_type *other) {
    absl::allocator_traits<Alloc>::construct(*alloc, slot, std::move(*other));
  }

  template <typename Alloc>
  static void construct(Alloc *alloc, slot_type *slot, const slot_type *other) {
    absl::allocator_traits<Alloc>::construct(*alloc, slot, *other);
  }

  template <typename Alloc>
  static void destroy(Alloc *alloc, slot_type *slot) {
    absl::allocator_traits<Alloc>::destroy(*alloc, slot);
  }
};

// A parameters structure for holding the type parameters for a btree_set.
// Compare and Alloc should be nothrow copy-constructible.
template <typename Key, typename Compare, typename Alloc, int TargetNodeSize,
          bool IsMulti>
struct set_params : common_params<Key, Compare, Alloc, TargetNodeSize, IsMulti,
                                  /*IsMap=*/false, set_slot_policy<Key>> {
  using value_type = Key;
  using slot_type = typename set_params::common_params::slot_type;

  template <typename V>
  static const V &key(const V &value) {
    return value;
  }
  static const Key &key(const slot_type *slot) { return *slot; }
  static const Key &key(slot_type *slot) { return *slot; }
};

}  // namespace container_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_BTREE_SET_H_
