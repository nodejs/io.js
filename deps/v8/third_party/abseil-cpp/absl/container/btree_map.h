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
// File: btree_map.h
// -----------------------------------------------------------------------------
//
// This header file defines B-tree maps: sorted associative containers mapping
// keys to values.
//
//     * `absl::btree_map<>`
//     * `absl::btree_multimap<>`
//
// These B-tree types are similar to the corresponding types in the STL
// (`std::map` and `std::multimap`) and generally conform to the STL interfaces
// of those types. However, because they are implemented using B-trees, they
// are more efficient in most situations.
//
// Unlike `std::map` and `std::multimap`, which are commonly implemented using
// red-black tree nodes, B-tree maps use more generic B-tree nodes able to hold
// multiple values per node. Holding multiple values per node often makes
// B-tree maps perform better than their `std::map` counterparts, because
// multiple entries can be checked within the same cache hit.
//
// However, these types should not be considered drop-in replacements for
// `std::map` and `std::multimap` as there are some API differences, which are
// noted in this header file. The most consequential differences with respect to
// migrating to b-tree from the STL types are listed in the next paragraph.
// Other API differences are minor.
//
// Importantly, insertions and deletions may invalidate outstanding iterators,
// pointers, and references to elements. Such invalidations are typically only
// an issue if insertion and deletion operations are interleaved with the use of
// more than one iterator, pointer, or reference simultaneously.  For this
// reason, `insert()`, `erase()`, and `extract_and_get_next()` return a valid
// iterator at the current position. Another important difference is that
// key-types must be copy-constructible.
//
// Another API difference is that btree iterators can be subtracted, and this
// is faster than using std::distance.
//
// B-tree maps are not exception-safe.

#ifndef ABSL_CONTAINER_BTREE_MAP_H_
#define ABSL_CONTAINER_BTREE_MAP_H_

#include "absl/base/attributes.h"
#include "absl/container/internal/btree.h"  // IWYU pragma: export
#include "absl/container/internal/btree_container.h"  // IWYU pragma: export

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace container_internal {

template <typename Key, typename Data, typename Compare, typename Alloc,
          int TargetNodeSize, bool IsMulti>
struct map_params;

}  // namespace container_internal

// absl::btree_map<>
//
// An `absl::btree_map<K, V>` is an ordered associative container of
// unique keys and associated values designed to be a more efficient replacement
// for `std::map` (in most cases).
//
// Keys are sorted using an (optional) comparison function, which defaults to
// `std::less<K>`.
//
// An `absl::btree_map<K, V>` uses a default allocator of
// `std::allocator<std::pair<const K, V>>` to allocate (and deallocate)
// nodes, and construct and destruct values within those nodes. You may
// instead specify a custom allocator `A` (which in turn requires specifying a
// custom comparator `C`) as in `absl::btree_map<K, V, C, A>`.
//
template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<std::pair<const Key, Value>>>
class ABSL_ATTRIBUTE_OWNER btree_map
    : public container_internal::btree_map_container<
          container_internal::btree<container_internal::map_params<
              Key, Value, Compare, Alloc, /*TargetNodeSize=*/256,
              /*IsMulti=*/false>>> {
  using Base = typename btree_map::btree_map_container;

 public:
  // Constructors and Assignment Operators
  //
  // A `btree_map` supports the same overload set as `std::map`
  // for construction and assignment:
  //
  // * Default constructor
  //
  //   absl::btree_map<int, std::string> map1;
  //
  // * Initializer List constructor
  //
  //   absl::btree_map<int, std::string> map2 =
  //       {{1, "huey"}, {2, "dewey"}, {3, "louie"},};
  //
  // * Copy constructor
  //
  //   absl::btree_map<int, std::string> map3(map2);
  //
  // * Copy assignment operator
  //
  //  absl::btree_map<int, std::string> map4;
  //  map4 = map3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::btree_map<int, std::string> map5(std::move(map4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::btree_map<int, std::string> map6;
  //   map6 = std::move(map5);
  //
  // * Range constructor
  //
  //   std::vector<std::pair<int, std::string>> v = {{1, "a"}, {2, "b"}};
  //   absl::btree_map<int, std::string> map7(v.begin(), v.end());
  btree_map() {}
  using Base::Base;

  // btree_map::begin()
  //
  // Returns an iterator to the beginning of the `btree_map`.
  using Base::begin;

  // btree_map::cbegin()
  //
  // Returns a const iterator to the beginning of the `btree_map`.
  using Base::cbegin;

  // btree_map::end()
  //
  // Returns an iterator to the end of the `btree_map`.
  using Base::end;

  // btree_map::cend()
  //
  // Returns a const iterator to the end of the `btree_map`.
  using Base::cend;

  // btree_map::empty()
  //
  // Returns whether or not the `btree_map` is empty.
  using Base::empty;

  // btree_map::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `btree_map` under current memory constraints. This value can be thought
  // of as the largest value of `std::distance(begin(), end())` for a
  // `btree_map<Key, T>`.
  using Base::max_size;

  // btree_map::size()
  //
  // Returns the number of elements currently within the `btree_map`.
  using Base::size;

  // btree_map::clear()
  //
  // Removes all elements from the `btree_map`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  using Base::clear;

  // btree_map::erase()
  //
  // Erases elements within the `btree_map`. If an erase occurs, any references,
  // pointers, or iterators are invalidated.
  // Overloads are listed below.
  //
  // iterator erase(iterator position):
  // iterator erase(const_iterator position):
  //
  //   Erases the element at `position` of the `btree_map`, returning
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

  // btree_map::insert()
  //
  // Inserts an element of the specified value into the `btree_map`,
  // returning an iterator pointing to the newly inserted element, provided that
  // an element with the given key does not already exist. If an insertion
  // occurs, any references, pointers, or iterators are invalidated.
  // Overloads are listed below.
  //
  // std::pair<iterator,bool> insert(const value_type& value):
  //
  //   Inserts a value into the `btree_map`. Returns a pair consisting of an
  //   iterator to the inserted element (or to the element that prevented the
  //   insertion) and a bool denoting whether the insertion took place.
  //
  // std::pair<iterator,bool> insert(value_type&& value):
  //
  //   Inserts a moveable value into the `btree_map`. Returns a pair
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

  // btree_map::insert_or_assign()
  //
  // Inserts an element of the specified value into the `btree_map` provided
  // that a value with the given key does not already exist, or replaces the
  // corresponding mapped type with the forwarded `obj` argument if a key for
  // that value already exists, returning an iterator pointing to the newly
  // inserted element. Overloads are listed below.
  //
  // pair<iterator, bool> insert_or_assign(const key_type& k, M&& obj):
  // pair<iterator, bool> insert_or_assign(key_type&& k, M&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `btree_map`. If the returned bool is true, insertion took place, and if
  //   it's false, assignment took place.
  //
  // iterator insert_or_assign(const_iterator hint,
  //                           const key_type& k, M&& obj):
  // iterator insert_or_assign(const_iterator hint, key_type&& k, M&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `btree_map` using the position of `hint` as a non-binding suggestion
  //   for where to begin the insertion search.
  using Base::insert_or_assign;

  // btree_map::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_map`, provided that no element with the given key
  // already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately. Prefer `try_emplace()` unless your key is not
  // copyable or moveable.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated.
  using Base::emplace;

  // btree_map::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_map`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search, and only inserts
  // provided that no element with the given key already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately. Prefer `try_emplace()` unless your key is not
  // copyable or moveable.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated.
  using Base::emplace_hint;

  // btree_map::try_emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_map`, provided that no element with the given key
  // already exists. Unlike `emplace()`, if an element with the given key
  // already exists, we guarantee that no element is constructed.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated.
  //
  // Overloads are listed below.
  //
  //   std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args):
  //   std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `btree_map`.
  //
  //   iterator try_emplace(const_iterator hint,
  //                        const key_type& k, Args&&... args):
  //   iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `btree_map` using the position of `hint` as a non-binding suggestion
  // for where to begin the insertion search.
  using Base::try_emplace;

  // btree_map::extract()
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
  //   returns a node handle owning that extracted data. If the `btree_map`
  //   does not contain an element with a matching key, this function returns an
  //   empty node handle.
  //
  // NOTE: when compiled in an earlier version of C++ than C++17,
  // `node_type::key()` returns a const reference to the key instead of a
  // mutable reference. We cannot safely return a mutable reference without
  // std::launder (which is not available before C++17).
  //
  // NOTE: In this context, `node_type` refers to the C++17 concept of a
  // move-only type that owns and provides access to the elements in associative
  // containers (https://en.cppreference.com/w/cpp/container/node_handle).
  // It does NOT refer to the data layout of the underlying btree.
  using Base::extract;

  // btree_map::extract_and_get_next()
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

  // btree_map::merge()
  //
  // Extracts elements from a given `source` btree_map into this
  // `btree_map`. If the destination `btree_map` already contains an
  // element with an equivalent key, that element is not extracted.
  using Base::merge;

  // btree_map::swap(btree_map& other)
  //
  // Exchanges the contents of this `btree_map` with those of the `other`
  // btree_map, avoiding invocation of any move, copy, or swap operations on
  // individual elements.
  //
  // All iterators and references on the `btree_map` remain valid, excepting
  // for the past-the-end iterator, which is invalidated.
  using Base::swap;

  // btree_map::at()
  //
  // Returns a reference to the mapped value of the element with key equivalent
  // to the passed key.
  using Base::at;

  // btree_map::contains()
  //
  // template <typename K> bool contains(const K& key) const:
  //
  // Determines whether an element comparing equal to the given `key` exists
  // within the `btree_map`, returning `true` if so or `false` otherwise.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::contains;

  // btree_map::count()
  //
  // template <typename K> size_type count(const K& key) const:
  //
  // Returns the number of elements comparing equal to the given `key` within
  // the `btree_map`. Note that this function will return either `1` or `0`
  // since duplicate elements are not allowed within a `btree_map`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::count;

  // btree_map::equal_range()
  //
  // Returns a half-open range [first, last), defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the `btree_map`.
  using Base::equal_range;

  // btree_map::find()
  //
  // template <typename K> iterator find(const K& key):
  // template <typename K> const_iterator find(const K& key) const:
  //
  // Finds an element with the passed `key` within the `btree_map`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::find;

  // btree_map::lower_bound()
  //
  // template <typename K> iterator lower_bound(const K& key):
  // template <typename K> const_iterator lower_bound(const K& key) const:
  //
  // Finds the first element with a key that is not less than `key` within the
  // `btree_map`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::lower_bound;

  // btree_map::upper_bound()
  //
  // template <typename K> iterator upper_bound(const K& key):
  // template <typename K> const_iterator upper_bound(const K& key) const:
  //
  // Finds the first element with a key that is greater than `key` within the
  // `btree_map`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::upper_bound;

  // btree_map::operator[]()
  //
  // Returns a reference to the value mapped to the passed key within the
  // `btree_map`, performing an `insert()` if the key does not already
  // exist.
  //
  // If an insertion occurs, any references, pointers, or iterators are
  // invalidated. Otherwise iterators are not affected and references are not
  // invalidated. Overloads are listed below.
  //
  // T& operator[](key_type&& key):
  // T& operator[](const key_type& key):
  //
  //   Inserts a value_type object constructed in-place if the element with the
  //   given key does not exist.
  using Base::operator[];

  // btree_map::get_allocator()
  //
  // Returns the allocator function associated with this `btree_map`.
  using Base::get_allocator;

  // btree_map::key_comp();
  //
  // Returns the key comparator associated with this `btree_map`.
  using Base::key_comp;

  // btree_map::value_comp();
  //
  // Returns the value comparator associated with this `btree_map`.
  using Base::value_comp;
};

// absl::swap(absl::btree_map<>, absl::btree_map<>)
//
// Swaps the contents of two `absl::btree_map` containers.
template <typename K, typename V, typename C, typename A>
void swap(btree_map<K, V, C, A> &x, btree_map<K, V, C, A> &y) {
  return x.swap(y);
}

// absl::erase_if(absl::btree_map<>, Pred)
//
// Erases all elements that satisfy the predicate pred from the container.
// Returns the number of erased elements.
template <typename K, typename V, typename C, typename A, typename Pred>
typename btree_map<K, V, C, A>::size_type erase_if(
    btree_map<K, V, C, A> &map, Pred pred) {
  return container_internal::btree_access::erase_if(map, std::move(pred));
}

// absl::btree_multimap
//
// An `absl::btree_multimap<K, V>` is an ordered associative container of
// keys and associated values designed to be a more efficient replacement for
// `std::multimap` (in most cases). Unlike `absl::btree_map`, a B-tree multimap
// allows multiple elements with equivalent keys.
//
// Keys are sorted using an (optional) comparison function, which defaults to
// `std::less<K>`.
//
// An `absl::btree_multimap<K, V>` uses a default allocator of
// `std::allocator<std::pair<const K, V>>` to allocate (and deallocate)
// nodes, and construct and destruct values within those nodes. You may
// instead specify a custom allocator `A` (which in turn requires specifying a
// custom comparator `C`) as in `absl::btree_multimap<K, V, C, A>`.
//
template <typename Key, typename Value, typename Compare = std::less<Key>,
          typename Alloc = std::allocator<std::pair<const Key, Value>>>
class ABSL_ATTRIBUTE_OWNER btree_multimap
    : public container_internal::btree_multimap_container<
          container_internal::btree<container_internal::map_params<
              Key, Value, Compare, Alloc, /*TargetNodeSize=*/256,
              /*IsMulti=*/true>>> {
  using Base = typename btree_multimap::btree_multimap_container;

 public:
  // Constructors and Assignment Operators
  //
  // A `btree_multimap` supports the same overload set as `std::multimap`
  // for construction and assignment:
  //
  // * Default constructor
  //
  //   absl::btree_multimap<int, std::string> map1;
  //
  // * Initializer List constructor
  //
  //   absl::btree_multimap<int, std::string> map2 =
  //       {{1, "huey"}, {2, "dewey"}, {3, "louie"},};
  //
  // * Copy constructor
  //
  //   absl::btree_multimap<int, std::string> map3(map2);
  //
  // * Copy assignment operator
  //
  //  absl::btree_multimap<int, std::string> map4;
  //  map4 = map3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::btree_multimap<int, std::string> map5(std::move(map4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::btree_multimap<int, std::string> map6;
  //   map6 = std::move(map5);
  //
  // * Range constructor
  //
  //   std::vector<std::pair<int, std::string>> v = {{1, "a"}, {2, "b"}};
  //   absl::btree_multimap<int, std::string> map7(v.begin(), v.end());
  btree_multimap() {}
  using Base::Base;

  // btree_multimap::begin()
  //
  // Returns an iterator to the beginning of the `btree_multimap`.
  using Base::begin;

  // btree_multimap::cbegin()
  //
  // Returns a const iterator to the beginning of the `btree_multimap`.
  using Base::cbegin;

  // btree_multimap::end()
  //
  // Returns an iterator to the end of the `btree_multimap`.
  using Base::end;

  // btree_multimap::cend()
  //
  // Returns a const iterator to the end of the `btree_multimap`.
  using Base::cend;

  // btree_multimap::empty()
  //
  // Returns whether or not the `btree_multimap` is empty.
  using Base::empty;

  // btree_multimap::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `btree_multimap` under current memory constraints. This value can be
  // thought of as the largest value of `std::distance(begin(), end())` for a
  // `btree_multimap<Key, T>`.
  using Base::max_size;

  // btree_multimap::size()
  //
  // Returns the number of elements currently within the `btree_multimap`.
  using Base::size;

  // btree_multimap::clear()
  //
  // Removes all elements from the `btree_multimap`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  using Base::clear;

  // btree_multimap::erase()
  //
  // Erases elements within the `btree_multimap`. If an erase occurs, any
  // references, pointers, or iterators are invalidated.
  // Overloads are listed below.
  //
  // iterator erase(iterator position):
  // iterator erase(const_iterator position):
  //
  //   Erases the element at `position` of the `btree_multimap`, returning
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

  // btree_multimap::insert()
  //
  // Inserts an element of the specified value into the `btree_multimap`,
  // returning an iterator pointing to the newly inserted element.
  // Any references, pointers, or iterators are invalidated.  Overloads are
  // listed below.
  //
  // iterator insert(const value_type& value):
  //
  //   Inserts a value into the `btree_multimap`, returning an iterator to the
  //   inserted element.
  //
  // iterator insert(value_type&& value):
  //
  //   Inserts a moveable value into the `btree_multimap`, returning an iterator
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

  // btree_multimap::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_multimap`. Any references, pointers, or iterators are
  // invalidated.
  using Base::emplace;

  // btree_multimap::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `btree_multimap`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search.
  //
  // Any references, pointers, or iterators are invalidated.
  using Base::emplace_hint;

  // btree_multimap::extract()
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
  //   returns a node handle owning that extracted data. If the `btree_multimap`
  //   does not contain an element with a matching key, this function returns an
  //   empty node handle.
  //
  // NOTE: when compiled in an earlier version of C++ than C++17,
  // `node_type::key()` returns a const reference to the key instead of a
  // mutable reference. We cannot safely return a mutable reference without
  // std::launder (which is not available before C++17).
  //
  // NOTE: In this context, `node_type` refers to the C++17 concept of a
  // move-only type that owns and provides access to the elements in associative
  // containers (https://en.cppreference.com/w/cpp/container/node_handle).
  // It does NOT refer to the data layout of the underlying btree.
  using Base::extract;

  // btree_multimap::extract_and_get_next()
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

  // btree_multimap::merge()
  //
  // Extracts all elements from a given `source` btree_multimap into this
  // `btree_multimap`.
  using Base::merge;

  // btree_multimap::swap(btree_multimap& other)
  //
  // Exchanges the contents of this `btree_multimap` with those of the `other`
  // btree_multimap, avoiding invocation of any move, copy, or swap operations
  // on individual elements.
  //
  // All iterators and references on the `btree_multimap` remain valid,
  // excepting for the past-the-end iterator, which is invalidated.
  using Base::swap;

  // btree_multimap::contains()
  //
  // template <typename K> bool contains(const K& key) const:
  //
  // Determines whether an element comparing equal to the given `key` exists
  // within the `btree_multimap`, returning `true` if so or `false` otherwise.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::contains;

  // btree_multimap::count()
  //
  // template <typename K> size_type count(const K& key) const:
  //
  // Returns the number of elements comparing equal to the given `key` within
  // the `btree_multimap`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::count;

  // btree_multimap::equal_range()
  //
  // Returns a half-open range [first, last), defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `btree_multimap`.
  using Base::equal_range;

  // btree_multimap::find()
  //
  // template <typename K> iterator find(const K& key):
  // template <typename K> const_iterator find(const K& key) const:
  //
  // Finds an element with the passed `key` within the `btree_multimap`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::find;

  // btree_multimap::lower_bound()
  //
  // template <typename K> iterator lower_bound(const K& key):
  // template <typename K> const_iterator lower_bound(const K& key) const:
  //
  // Finds the first element with a key that is not less than `key` within the
  // `btree_multimap`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::lower_bound;

  // btree_multimap::upper_bound()
  //
  // template <typename K> iterator upper_bound(const K& key):
  // template <typename K> const_iterator upper_bound(const K& key) const:
  //
  // Finds the first element with a key that is greater than `key` within the
  // `btree_multimap`.
  //
  // Supports heterogeneous lookup, provided that the map has a compatible
  // heterogeneous comparator.
  using Base::upper_bound;

  // btree_multimap::get_allocator()
  //
  // Returns the allocator function associated with this `btree_multimap`.
  using Base::get_allocator;

  // btree_multimap::key_comp();
  //
  // Returns the key comparator associated with this `btree_multimap`.
  using Base::key_comp;

  // btree_multimap::value_comp();
  //
  // Returns the value comparator associated with this `btree_multimap`.
  using Base::value_comp;
};

// absl::swap(absl::btree_multimap<>, absl::btree_multimap<>)
//
// Swaps the contents of two `absl::btree_multimap` containers.
template <typename K, typename V, typename C, typename A>
void swap(btree_multimap<K, V, C, A> &x, btree_multimap<K, V, C, A> &y) {
  return x.swap(y);
}

// absl::erase_if(absl::btree_multimap<>, Pred)
//
// Erases all elements that satisfy the predicate pred from the container.
// Returns the number of erased elements.
template <typename K, typename V, typename C, typename A, typename Pred>
typename btree_multimap<K, V, C, A>::size_type erase_if(
    btree_multimap<K, V, C, A> &map, Pred pred) {
  return container_internal::btree_access::erase_if(map, std::move(pred));
}

namespace container_internal {

// A parameters structure for holding the type parameters for a btree_map.
// Compare and Alloc should be nothrow copy-constructible.
template <typename Key, typename Data, typename Compare, typename Alloc,
          int TargetNodeSize, bool IsMulti>
struct map_params : common_params<Key, Compare, Alloc, TargetNodeSize, IsMulti,
                                  /*IsMap=*/true, map_slot_policy<Key, Data>> {
  using super_type = typename map_params::common_params;
  using mapped_type = Data;
  // This type allows us to move keys when it is safe to do so. It is safe
  // for maps in which value_type and mutable_value_type are layout compatible.
  using slot_policy = typename super_type::slot_policy;
  using slot_type = typename super_type::slot_type;
  using value_type = typename super_type::value_type;
  using init_type = typename super_type::init_type;

  template <typename V>
  static auto key(const V &value ABSL_ATTRIBUTE_LIFETIME_BOUND)
      -> decltype((value.first)) {
    return value.first;
  }
  static const Key &key(const slot_type *s) { return slot_policy::key(s); }
  static const Key &key(slot_type *s) { return slot_policy::key(s); }
  // For use in node handle.
  static auto mutable_key(slot_type *s)
      -> decltype(slot_policy::mutable_key(s)) {
    return slot_policy::mutable_key(s);
  }
  static mapped_type &value(value_type *value) { return value->second; }
};

}  // namespace container_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_BTREE_MAP_H_
