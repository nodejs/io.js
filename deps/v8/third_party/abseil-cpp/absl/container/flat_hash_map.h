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
// File: flat_hash_map.h
// -----------------------------------------------------------------------------
//
// An `absl::flat_hash_map<K, V>` is an unordered associative container of
// unique keys and associated values designed to be a more efficient replacement
// for `std::unordered_map`. Like `unordered_map`, search, insertion, and
// deletion of map elements can be done as an `O(1)` operation. However,
// `flat_hash_map` (and other unordered associative containers known as the
// collection of Abseil "Swiss tables") contain other optimizations that result
// in both memory and computation advantages.
//
// In most cases, your default choice for a hash map should be a map of type
// `flat_hash_map`.

#ifndef ABSL_CONTAINER_FLAT_HASH_MAP_H_
#define ABSL_CONTAINER_FLAT_HASH_MAP_H_

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/macros.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_function_defaults.h"  // IWYU pragma: export
#include "absl/container/internal/raw_hash_map.h"  // IWYU pragma: export
#include "absl/memory/memory.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
template <class K, class V>
struct FlatHashMapPolicy;
}  // namespace container_internal

// -----------------------------------------------------------------------------
// absl::flat_hash_map
// -----------------------------------------------------------------------------
//
// An `absl::flat_hash_map<K, V>` is an unordered associative container which
// has been optimized for both speed and memory footprint in most common use
// cases. Its interface is similar to that of `std::unordered_map<K, V>` with
// the following notable differences:
//
// * Requires keys that are CopyConstructible
// * Requires values that are MoveConstructible
// * Supports heterogeneous lookup, through `find()`, `operator[]()` and
//   `insert()`, provided that the map is provided a compatible heterogeneous
//   hashing function and equality operator. See below for details.
// * Invalidates any references and pointers to elements within the table after
//   `rehash()` and when the table is moved.
// * Contains a `capacity()` member function indicating the number of element
//   slots (open, deleted, and empty) within the hash map.
// * Returns `void` from the `erase(iterator)` overload.
//
// By default, `flat_hash_map` uses the `absl::Hash` hashing framework.
// All fundamental and Abseil types that support the `absl::Hash` framework have
// a compatible equality operator for comparing insertions into `flat_hash_map`.
// If your type is not yet supported by the `absl::Hash` framework, see
// absl/hash/hash.h for information on extending Abseil hashing to user-defined
// types.
//
// Using `absl::flat_hash_map` at interface boundaries in dynamically loaded
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
// NOTE: A `flat_hash_map` stores its value types directly inside its
// implementation array to avoid memory indirection. Because a `flat_hash_map`
// is designed to move data when rehashed, map values will not retain pointer
// stability. If you require pointer stability, or if your values are large,
// consider using `absl::flat_hash_map<Key, std::unique_ptr<Value>>` instead.
// If your types are not moveable or you require pointer stability for keys,
// consider `absl::node_hash_map`.
//
// Example:
//
//   // Create a flat hash map of three strings (that map to strings)
//   absl::flat_hash_map<std::string, std::string> ducks =
//     {{"a", "huey"}, {"b", "dewey"}, {"c", "louie"}};
//
//  // Insert a new element into the flat hash map
//  ducks.insert({"d", "donald"});
//
//  // Force a rehash of the flat hash map
//  ducks.rehash(0);
//
//  // Find the element with the key "b"
//  std::string search_key = "b";
//  auto result = ducks.find(search_key);
//  if (result != ducks.end()) {
//    std::cout << "Result: " << result->second << std::endl;
//  }
template <class K, class V,
          class Hash = absl::container_internal::hash_default_hash<K>,
          class Eq = absl::container_internal::hash_default_eq<K>,
          class Allocator = std::allocator<std::pair<const K, V>>>
class flat_hash_map : public absl::container_internal::raw_hash_map<
                          absl::container_internal::FlatHashMapPolicy<K, V>,
                          Hash, Eq, Allocator> {
  using Base = typename flat_hash_map::raw_hash_map;

 public:
  // Constructors and Assignment Operators
  //
  // A flat_hash_map supports the same overload set as `std::unordered_map`
  // for construction and assignment:
  //
  // *  Default constructor
  //
  //    // No allocation for the table's elements is made.
  //    absl::flat_hash_map<int, std::string> map1;
  //
  // * Initializer List constructor
  //
  //   absl::flat_hash_map<int, std::string> map2 =
  //       {{1, "huey"}, {2, "dewey"}, {3, "louie"},};
  //
  // * Copy constructor
  //
  //   absl::flat_hash_map<int, std::string> map3(map2);
  //
  // * Copy assignment operator
  //
  //  // Hash functor and Comparator are copied as well
  //  absl::flat_hash_map<int, std::string> map4;
  //  map4 = map3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::flat_hash_map<int, std::string> map5(std::move(map4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::flat_hash_map<int, std::string> map6;
  //   map6 = std::move(map5);
  //
  // * Range constructor
  //
  //   std::vector<std::pair<int, std::string>> v = {{1, "a"}, {2, "b"}};
  //   absl::flat_hash_map<int, std::string> map7(v.begin(), v.end());
  flat_hash_map() {}
  using Base::Base;

  // flat_hash_map::begin()
  //
  // Returns an iterator to the beginning of the `flat_hash_map`.
  using Base::begin;

  // flat_hash_map::cbegin()
  //
  // Returns a const iterator to the beginning of the `flat_hash_map`.
  using Base::cbegin;

  // flat_hash_map::cend()
  //
  // Returns a const iterator to the end of the `flat_hash_map`.
  using Base::cend;

  // flat_hash_map::end()
  //
  // Returns an iterator to the end of the `flat_hash_map`.
  using Base::end;

  // flat_hash_map::capacity()
  //
  // Returns the number of element slots (assigned, deleted, and empty)
  // available within the `flat_hash_map`.
  //
  // NOTE: this member function is particular to `absl::flat_hash_map` and is
  // not provided in the `std::unordered_map` API.
  using Base::capacity;

  // flat_hash_map::empty()
  //
  // Returns whether or not the `flat_hash_map` is empty.
  using Base::empty;

  // flat_hash_map::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `flat_hash_map` under current memory constraints. This value can be thought
  // of the largest value of `std::distance(begin(), end())` for a
  // `flat_hash_map<K, V>`.
  using Base::max_size;

  // flat_hash_map::size()
  //
  // Returns the number of elements currently within the `flat_hash_map`.
  using Base::size;

  // flat_hash_map::clear()
  //
  // Removes all elements from the `flat_hash_map`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  //
  // NOTE: this operation may shrink the underlying buffer. To avoid shrinking
  // the underlying buffer call `erase(begin(), end())`.
  using Base::clear;

  // flat_hash_map::erase()
  //
  // Erases elements within the `flat_hash_map`. Erasing does not trigger a
  // rehash. Overloads are listed below.
  //
  // void erase(const_iterator pos):
  //
  //   Erases the element at `position` of the `flat_hash_map`, returning
  //   `void`.
  //
  //   NOTE: returning `void` in this case is different than that of STL
  //   containers in general and `std::unordered_map` in particular (which
  //   return an iterator to the element following the erased element). If that
  //   iterator is needed, simply post increment the iterator:
  //
  //     map.erase(it++);
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

  // flat_hash_map::insert()
  //
  // Inserts an element of the specified value into the `flat_hash_map`,
  // returning an iterator pointing to the newly inserted element, provided that
  // an element with the given key does not already exist. If rehashing occurs
  // due to the insertion, all iterators are invalidated. Overloads are listed
  // below.
  //
  // std::pair<iterator,bool> insert(const init_type& value):
  //
  //   Inserts a value into the `flat_hash_map`. Returns a pair consisting of an
  //   iterator to the inserted element (or to the element that prevented the
  //   insertion) and a bool denoting whether the insertion took place.
  //
  // std::pair<iterator,bool> insert(T&& value):
  // std::pair<iterator,bool> insert(init_type&& value):
  //
  //   Inserts a moveable value into the `flat_hash_map`. Returns a pair
  //   consisting of an iterator to the inserted element (or to the element that
  //   prevented the insertion) and a bool denoting whether the insertion took
  //   place.
  //
  // iterator insert(const_iterator hint, const init_type& value):
  // iterator insert(const_iterator hint, T&& value):
  // iterator insert(const_iterator hint, init_type&& value);
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
  //   multiple keys compare equivalently, for `flat_hash_map` we guarantee the
  //   first match is inserted.
  //
  // void insert(std::initializer_list<init_type> ilist):
  //
  //   Inserts the elements within the initializer list `ilist`.
  //
  //   NOTE: Although the STL does not specify which element may be inserted if
  //   multiple keys compare equivalently within the initializer list, for
  //   `flat_hash_map` we guarantee the first match is inserted.
  using Base::insert;

  // flat_hash_map::insert_or_assign()
  //
  // Inserts an element of the specified value into the `flat_hash_map` provided
  // that a value with the given key does not already exist, or replaces it with
  // the element value if a key for that value already exists, returning an
  // iterator pointing to the newly inserted element.  If rehashing occurs due
  // to the insertion, all existing iterators are invalidated. Overloads are
  // listed below.
  //
  // pair<iterator, bool> insert_or_assign(const init_type& k, T&& obj):
  // pair<iterator, bool> insert_or_assign(init_type&& k, T&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `flat_hash_map`.
  //
  // iterator insert_or_assign(const_iterator hint,
  //                           const init_type& k, T&& obj):
  // iterator insert_or_assign(const_iterator hint, init_type&& k, T&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `flat_hash_map` using the position of `hint` as a non-binding suggestion
  //   for where to begin the insertion search.
  using Base::insert_or_assign;

  // flat_hash_map::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `flat_hash_map`, provided that no element with the given key
  // already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately. Prefer `try_emplace()` unless your key is not
  // copyable or moveable.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  using Base::emplace;

  // flat_hash_map::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `flat_hash_map`, using the position of `hint` as a non-binding
  // suggestion for where to begin the insertion search, and only inserts
  // provided that no element with the given key already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately. Prefer `try_emplace()` unless your key is not
  // copyable or moveable.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  using Base::emplace_hint;

  // flat_hash_map::try_emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `flat_hash_map`, provided that no element with the given key
  // already exists. Unlike `emplace()`, if an element with the given key
  // already exists, we guarantee that no element is constructed.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  // Overloads are listed below.
  //
  //   pair<iterator, bool> try_emplace(const key_type& k, Args&&... args):
  //   pair<iterator, bool> try_emplace(key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `flat_hash_map`.
  //
  //   iterator try_emplace(const_iterator hint,
  //                        const key_type& k, Args&&... args):
  //   iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `flat_hash_map` using the position of `hint` as a non-binding suggestion
  // for where to begin the insertion search.
  //
  // All `try_emplace()` overloads make the same guarantees regarding rvalue
  // arguments as `std::unordered_map::try_emplace()`, namely that these
  // functions will not move from rvalue arguments if insertions do not happen.
  using Base::try_emplace;

  // flat_hash_map::extract()
  //
  // Extracts the indicated element, erasing it in the process, and returns it
  // as a C++17-compatible node handle. Overloads are listed below.
  //
  // node_type extract(const_iterator position):
  //
  //   Extracts the key,value pair of the element at the indicated position and
  //   returns a node handle owning that extracted data.
  //
  // node_type extract(const key_type& x):
  //
  //   Extracts the key,value pair of the element with a key matching the passed
  //   key value and returns a node handle owning that extracted data. If the
  //   `flat_hash_map` does not contain an element with a matching key, this
  //   function returns an empty node handle.
  //
  // NOTE: when compiled in an earlier version of C++ than C++17,
  // `node_type::key()` returns a const reference to the key instead of a
  // mutable reference. We cannot safely return a mutable reference without
  // std::launder (which is not available before C++17).
  using Base::extract;

  // flat_hash_map::merge()
  //
  // Extracts elements from a given `source` flat hash map into this
  // `flat_hash_map`. If the destination `flat_hash_map` already contains an
  // element with an equivalent key, that element is not extracted.
  using Base::merge;

  // flat_hash_map::swap(flat_hash_map& other)
  //
  // Exchanges the contents of this `flat_hash_map` with those of the `other`
  // flat hash map, avoiding invocation of any move, copy, or swap operations on
  // individual elements.
  //
  // All iterators and references on the `flat_hash_map` remain valid, excepting
  // for the past-the-end iterator, which is invalidated.
  //
  // `swap()` requires that the flat hash map's hashing and key equivalence
  // functions be Swappable, and are exchanged using unqualified calls to
  // non-member `swap()`. If the map's allocator has
  // `std::allocator_traits<allocator_type>::propagate_on_container_swap::value`
  // set to `true`, the allocators are also exchanged using an unqualified call
  // to non-member `swap()`; otherwise, the allocators are not swapped.
  using Base::swap;

  // flat_hash_map::rehash(count)
  //
  // Rehashes the `flat_hash_map`, setting the number of slots to be at least
  // the passed value. If the new number of slots increases the load factor more
  // than the current maximum load factor
  // (`count` < `size()` / `max_load_factor()`), then the new number of slots
  // will be at least `size()` / `max_load_factor()`.
  //
  // To force a rehash, pass rehash(0).
  //
  // NOTE: unlike behavior in `std::unordered_map`, references are also
  // invalidated upon a `rehash()`.
  using Base::rehash;

  // flat_hash_map::reserve(count)
  //
  // Sets the number of slots in the `flat_hash_map` to the number needed to
  // accommodate at least `count` total elements without exceeding the current
  // maximum load factor, and may rehash the container if needed.
  using Base::reserve;

  // flat_hash_map::at()
  //
  // Returns a reference to the mapped value of the element with key equivalent
  // to the passed key.
  using Base::at;

  // flat_hash_map::contains()
  //
  // Determines whether an element with a key comparing equal to the given `key`
  // exists within the `flat_hash_map`, returning `true` if so or `false`
  // otherwise.
  using Base::contains;

  // flat_hash_map::count(const Key& key) const
  //
  // Returns the number of elements with a key comparing equal to the given
  // `key` within the `flat_hash_map`. note that this function will return
  // either `1` or `0` since duplicate keys are not allowed within a
  // `flat_hash_map`.
  using Base::count;

  // flat_hash_map::equal_range()
  //
  // Returns a closed range [first, last], defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `flat_hash_map`.
  using Base::equal_range;

  // flat_hash_map::find()
  //
  // Finds an element with the passed `key` within the `flat_hash_map`.
  using Base::find;

  // flat_hash_map::operator[]()
  //
  // Returns a reference to the value mapped to the passed key within the
  // `flat_hash_map`, performing an `insert()` if the key does not already
  // exist.
  //
  // If an insertion occurs and results in a rehashing of the container, all
  // iterators are invalidated. Otherwise iterators are not affected and
  // references are not invalidated. Overloads are listed below.
  //
  // T& operator[](const Key& key):
  //
  //   Inserts an init_type object constructed in-place if the element with the
  //   given key does not exist.
  //
  // T& operator[](Key&& key):
  //
  //   Inserts an init_type object constructed in-place provided that an element
  //   with the given key does not exist.
  using Base::operator[];

  // flat_hash_map::bucket_count()
  //
  // Returns the number of "buckets" within the `flat_hash_map`. Note that
  // because a flat hash map contains all elements within its internal storage,
  // this value simply equals the current capacity of the `flat_hash_map`.
  using Base::bucket_count;

  // flat_hash_map::load_factor()
  //
  // Returns the current load factor of the `flat_hash_map` (the average number
  // of slots occupied with a value within the hash map).
  using Base::load_factor;

  // flat_hash_map::max_load_factor()
  //
  // Manages the maximum load factor of the `flat_hash_map`. Overloads are
  // listed below.
  //
  // float flat_hash_map::max_load_factor()
  //
  //   Returns the current maximum load factor of the `flat_hash_map`.
  //
  // void flat_hash_map::max_load_factor(float ml)
  //
  //   Sets the maximum load factor of the `flat_hash_map` to the passed value.
  //
  //   NOTE: This overload is provided only for API compatibility with the STL;
  //   `flat_hash_map` will ignore any set load factor and manage its rehashing
  //   internally as an implementation detail.
  using Base::max_load_factor;

  // flat_hash_map::get_allocator()
  //
  // Returns the allocator function associated with this `flat_hash_map`.
  using Base::get_allocator;

  // flat_hash_map::hash_function()
  //
  // Returns the hashing function used to hash the keys within this
  // `flat_hash_map`.
  using Base::hash_function;

  // flat_hash_map::key_eq()
  //
  // Returns the function used for comparing keys equality.
  using Base::key_eq;
};

// erase_if(flat_hash_map<>, Pred)
//
// Erases all elements that satisfy the predicate `pred` from the container `c`.
// Returns the number of erased elements.
template <typename K, typename V, typename H, typename E, typename A,
          typename Predicate>
typename flat_hash_map<K, V, H, E, A>::size_type erase_if(
    flat_hash_map<K, V, H, E, A>& c, Predicate pred) {
  return container_internal::EraseIf(pred, &c);
}

namespace container_internal {

template <class K, class V>
struct FlatHashMapPolicy {
  using slot_policy = container_internal::map_slot_policy<K, V>;
  using slot_type = typename slot_policy::slot_type;
  using key_type = K;
  using mapped_type = V;
  using init_type = std::pair</*non const*/ key_type, mapped_type>;

  template <class Allocator, class... Args>
  static void construct(Allocator* alloc, slot_type* slot, Args&&... args) {
    slot_policy::construct(alloc, slot, std::forward<Args>(args)...);
  }

  // Returns std::true_type in case destroy is trivial.
  template <class Allocator>
  static auto destroy(Allocator* alloc, slot_type* slot) {
    return slot_policy::destroy(alloc, slot);
  }

  template <class Allocator>
  static auto transfer(Allocator* alloc, slot_type* new_slot,
                       slot_type* old_slot) {
    return slot_policy::transfer(alloc, new_slot, old_slot);
  }

  template <class F, class... Args>
  static decltype(absl::container_internal::DecomposePair(
      std::declval<F>(), std::declval<Args>()...))
  apply(F&& f, Args&&... args) {
    return absl::container_internal::DecomposePair(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
  }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return memory_internal::IsLayoutCompatible<K, V>::value
               ? &TypeErasedApplyToSlotFn<Hash, K>
               : nullptr;
  }

  static size_t space_used(const slot_type*) { return 0; }

  static std::pair<const K, V>& element(slot_type* slot) { return slot->value; }

  static V& value(std::pair<const K, V>* kv) { return kv->second; }
  static const V& value(const std::pair<const K, V>* kv) { return kv->second; }
};

}  // namespace container_internal

namespace container_algorithm_internal {

// Specialization of trait in absl/algorithm/container.h
template <class Key, class T, class Hash, class KeyEqual, class Allocator>
struct IsUnorderedContainer<
    absl::flat_hash_map<Key, T, Hash, KeyEqual, Allocator>> : std::true_type {};

}  // namespace container_algorithm_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_FLAT_HASH_MAP_H_
