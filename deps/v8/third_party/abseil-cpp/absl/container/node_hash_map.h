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
// File: node_hash_map.h
// -----------------------------------------------------------------------------
//
// An `absl::node_hash_map<K, V>` is an unordered associative container of
// unique keys and associated values designed to be a more efficient replacement
// for `std::unordered_map`. Like `unordered_map`, search, insertion, and
// deletion of map elements can be done as an `O(1)` operation. However,
// `node_hash_map` (and other unordered associative containers known as the
// collection of Abseil "Swiss tables") contain other optimizations that result
// in both memory and computation advantages.
//
// In most cases, your default choice for a hash map should be a map of type
// `flat_hash_map`. However, if you need pointer stability and cannot store
// a `flat_hash_map` with `unique_ptr` elements, a `node_hash_map` may be a
// valid alternative. As well, if you are migrating your code from using
// `std::unordered_map`, a `node_hash_map` provides a more straightforward
// migration, because it guarantees pointer stability. Consider migrating to
// `node_hash_map` and perhaps converting to a more efficient `flat_hash_map`
// upon further review.
//
// `node_hash_map` is not exception-safe.

#ifndef ABSL_CONTAINER_NODE_HASH_MAP_H_
#define ABSL_CONTAINER_NODE_HASH_MAP_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/base/attributes.h"
#include "absl/container/hash_container_defaults.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/node_slot_policy.h"
#include "absl/container/internal/raw_hash_map.h"  // IWYU pragma: export
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
template <class Key, class Value>
class NodeHashMapPolicy;
}  // namespace container_internal

// -----------------------------------------------------------------------------
// absl::node_hash_map
// -----------------------------------------------------------------------------
//
// An `absl::node_hash_map<K, V>` is an unordered associative container which
// has been optimized for both speed and memory footprint in most common use
// cases. Its interface is similar to that of `std::unordered_map<K, V>` with
// the following notable differences:
//
// * Supports heterogeneous lookup, through `find()`, `operator[]()` and
//   `insert()`, provided that the map is provided a compatible heterogeneous
//   hashing function and equality operator. See below for details.
// * Contains a `capacity()` member function indicating the number of element
//   slots (open, deleted, and empty) within the hash map.
// * Returns `void` from the `erase(iterator)` overload.
//
// By default, `node_hash_map` uses the `absl::Hash` hashing framework.
// All fundamental and Abseil types that support the `absl::Hash` framework have
// a compatible equality operator for comparing insertions into `node_hash_map`.
// If your type is not yet supported by the `absl::Hash` framework, see
// absl/hash/hash.h for information on extending Abseil hashing to user-defined
// types.
//
// Using `absl::node_hash_map` at interface boundaries in dynamically loaded
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
//   // Create a node hash map of three strings (that map to strings)
//   absl::node_hash_map<std::string, std::string> ducks =
//     {{"a", "huey"}, {"b", "dewey"}, {"c", "louie"}};
//
//  // Insert a new element into the node hash map
//  ducks.insert({"d", "donald"}};
//
//  // Force a rehash of the node hash map
//  ducks.rehash(0);
//
//  // Find the element with the key "b"
//  std::string search_key = "b";
//  auto result = ducks.find(search_key);
//  if (result != ducks.end()) {
//    std::cout << "Result: " << result->second << std::endl;
//  }
template <class Key, class Value, class Hash = DefaultHashContainerHash<Key>,
          class Eq = DefaultHashContainerEq<Key>,
          class Alloc = std::allocator<std::pair<const Key, Value>>>
class ABSL_INTERNAL_ATTRIBUTE_OWNER node_hash_map
    : public absl::container_internal::raw_hash_map<
          absl::container_internal::NodeHashMapPolicy<Key, Value>, Hash, Eq,
          Alloc> {
  using Base = typename node_hash_map::raw_hash_map;

 public:
  // Constructors and Assignment Operators
  //
  // A node_hash_map supports the same overload set as `std::unordered_map`
  // for construction and assignment:
  //
  // *  Default constructor
  //
  //    // No allocation for the table's elements is made.
  //    absl::node_hash_map<int, std::string> map1;
  //
  // * Initializer List constructor
  //
  //   absl::node_hash_map<int, std::string> map2 =
  //       {{1, "huey"}, {2, "dewey"}, {3, "louie"},};
  //
  // * Copy constructor
  //
  //   absl::node_hash_map<int, std::string> map3(map2);
  //
  // * Copy assignment operator
  //
  //  // Hash functor and Comparator are copied as well
  //  absl::node_hash_map<int, std::string> map4;
  //  map4 = map3;
  //
  // * Move constructor
  //
  //   // Move is guaranteed efficient
  //   absl::node_hash_map<int, std::string> map5(std::move(map4));
  //
  // * Move assignment operator
  //
  //   // May be efficient if allocators are compatible
  //   absl::node_hash_map<int, std::string> map6;
  //   map6 = std::move(map5);
  //
  // * Range constructor
  //
  //   std::vector<std::pair<int, std::string>> v = {{1, "a"}, {2, "b"}};
  //   absl::node_hash_map<int, std::string> map7(v.begin(), v.end());
  node_hash_map() {}
  using Base::Base;

  // node_hash_map::begin()
  //
  // Returns an iterator to the beginning of the `node_hash_map`.
  using Base::begin;

  // node_hash_map::cbegin()
  //
  // Returns a const iterator to the beginning of the `node_hash_map`.
  using Base::cbegin;

  // node_hash_map::cend()
  //
  // Returns a const iterator to the end of the `node_hash_map`.
  using Base::cend;

  // node_hash_map::end()
  //
  // Returns an iterator to the end of the `node_hash_map`.
  using Base::end;

  // node_hash_map::capacity()
  //
  // Returns the number of element slots (assigned, deleted, and empty)
  // available within the `node_hash_map`.
  //
  // NOTE: this member function is particular to `absl::node_hash_map` and is
  // not provided in the `std::unordered_map` API.
  using Base::capacity;

  // node_hash_map::empty()
  //
  // Returns whether or not the `node_hash_map` is empty.
  using Base::empty;

  // node_hash_map::max_size()
  //
  // Returns the largest theoretical possible number of elements within a
  // `node_hash_map` under current memory constraints. This value can be thought
  // of as the largest value of `std::distance(begin(), end())` for a
  // `node_hash_map<K, V>`.
  using Base::max_size;

  // node_hash_map::size()
  //
  // Returns the number of elements currently within the `node_hash_map`.
  using Base::size;

  // node_hash_map::clear()
  //
  // Removes all elements from the `node_hash_map`. Invalidates any references,
  // pointers, or iterators referring to contained elements.
  //
  // NOTE: this operation may shrink the underlying buffer. To avoid shrinking
  // the underlying buffer call `erase(begin(), end())`.
  using Base::clear;

  // node_hash_map::erase()
  //
  // Erases elements within the `node_hash_map`. Erasing does not trigger a
  // rehash. Overloads are listed below.
  //
  // void erase(const_iterator pos):
  //
  //   Erases the element at `position` of the `node_hash_map`, returning
  //   `void`.
  //
  //   NOTE: this return behavior is different than that of STL containers in
  //   general and `std::unordered_map` in particular.
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

  // node_hash_map::insert()
  //
  // Inserts an element of the specified value into the `node_hash_map`,
  // returning an iterator pointing to the newly inserted element, provided that
  // an element with the given key does not already exist. If rehashing occurs
  // due to the insertion, all iterators are invalidated. Overloads are listed
  // below.
  //
  // std::pair<iterator,bool> insert(const init_type& value):
  //
  //   Inserts a value into the `node_hash_map`. Returns a pair consisting of an
  //   iterator to the inserted element (or to the element that prevented the
  //   insertion) and a `bool` denoting whether the insertion took place.
  //
  // std::pair<iterator,bool> insert(T&& value):
  // std::pair<iterator,bool> insert(init_type&& value):
  //
  //   Inserts a moveable value into the `node_hash_map`. Returns a `std::pair`
  //   consisting of an iterator to the inserted element (or to the element that
  //   prevented the insertion) and a `bool` denoting whether the insertion took
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
  //   multiple keys compare equivalently, for `node_hash_map` we guarantee the
  //   first match is inserted.
  //
  // void insert(std::initializer_list<init_type> ilist):
  //
  //   Inserts the elements within the initializer list `ilist`.
  //
  //   NOTE: Although the STL does not specify which element may be inserted if
  //   multiple keys compare equivalently within the initializer list, for
  //   `node_hash_map` we guarantee the first match is inserted.
  using Base::insert;

  // node_hash_map::insert_or_assign()
  //
  // Inserts an element of the specified value into the `node_hash_map` provided
  // that a value with the given key does not already exist, or replaces it with
  // the element value if a key for that value already exists, returning an
  // iterator pointing to the newly inserted element. If rehashing occurs due to
  // the insertion, all iterators are invalidated. Overloads are listed
  // below.
  //
  // std::pair<iterator, bool> insert_or_assign(const init_type& k, T&& obj):
  // std::pair<iterator, bool> insert_or_assign(init_type&& k, T&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `node_hash_map`.
  //
  // iterator insert_or_assign(const_iterator hint,
  //                           const init_type& k, T&& obj):
  // iterator insert_or_assign(const_iterator hint, init_type&& k, T&& obj):
  //
  //   Inserts/Assigns (or moves) the element of the specified key into the
  //   `node_hash_map` using the position of `hint` as a non-binding suggestion
  //   for where to begin the insertion search.
  using Base::insert_or_assign;

  // node_hash_map::emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `node_hash_map`, provided that no element with the given key
  // already exists.
  //
  // The element may be constructed even if there already is an element with the
  // key in the container, in which case the newly constructed element will be
  // destroyed immediately. Prefer `try_emplace()` unless your key is not
  // copyable or moveable.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  using Base::emplace;

  // node_hash_map::emplace_hint()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `node_hash_map`, using the position of `hint` as a non-binding
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

  // node_hash_map::try_emplace()
  //
  // Inserts an element of the specified value by constructing it in-place
  // within the `node_hash_map`, provided that no element with the given key
  // already exists. Unlike `emplace()`, if an element with the given key
  // already exists, we guarantee that no element is constructed.
  //
  // If rehashing occurs due to the insertion, all iterators are invalidated.
  // Overloads are listed below.
  //
  //   std::pair<iterator, bool> try_emplace(const key_type& k, Args&&... args):
  //   std::pair<iterator, bool> try_emplace(key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `node_hash_map`.
  //
  //   iterator try_emplace(const_iterator hint,
  //                        const key_type& k, Args&&... args):
  //   iterator try_emplace(const_iterator hint, key_type&& k, Args&&... args):
  //
  // Inserts (via copy or move) the element of the specified key into the
  // `node_hash_map` using the position of `hint` as a non-binding suggestion
  // for where to begin the insertion search.
  //
  // All `try_emplace()` overloads make the same guarantees regarding rvalue
  // arguments as `std::unordered_map::try_emplace()`, namely that these
  // functions will not move from rvalue arguments if insertions do not happen.
  using Base::try_emplace;

  // node_hash_map::extract()
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
  //   `node_hash_map` does not contain an element with a matching key, this
  //   function returns an empty node handle.
  //
  // NOTE: when compiled in an earlier version of C++ than C++17,
  // `node_type::key()` returns a const reference to the key instead of a
  // mutable reference. We cannot safely return a mutable reference without
  // std::launder (which is not available before C++17).
  using Base::extract;

  // node_hash_map::merge()
  //
  // Extracts elements from a given `source` node hash map into this
  // `node_hash_map`. If the destination `node_hash_map` already contains an
  // element with an equivalent key, that element is not extracted.
  using Base::merge;

  // node_hash_map::swap(node_hash_map& other)
  //
  // Exchanges the contents of this `node_hash_map` with those of the `other`
  // node hash map.
  //
  // All iterators and references on the `node_hash_map` remain valid, excepting
  // for the past-the-end iterator, which is invalidated.
  //
  // `swap()` requires that the node hash map's hashing and key equivalence
  // functions be Swappable, and are exchanged using unqualified calls to
  // non-member `swap()`. If the map's allocator has
  // `std::allocator_traits<allocator_type>::propagate_on_container_swap::value`
  // set to `true`, the allocators are also exchanged using an unqualified call
  // to non-member `swap()`; otherwise, the allocators are not swapped.
  using Base::swap;

  // node_hash_map::rehash(count)
  //
  // Rehashes the `node_hash_map`, setting the number of slots to be at least
  // the passed value. If the new number of slots increases the load factor more
  // than the current maximum load factor
  // (`count` < `size()` / `max_load_factor()`), then the new number of slots
  // will be at least `size()` / `max_load_factor()`.
  //
  // To force a rehash, pass rehash(0).
  using Base::rehash;

  // node_hash_map::reserve(count)
  //
  // Sets the number of slots in the `node_hash_map` to the number needed to
  // accommodate at least `count` total elements without exceeding the current
  // maximum load factor, and may rehash the container if needed.
  using Base::reserve;

  // node_hash_map::at()
  //
  // Returns a reference to the mapped value of the element with key equivalent
  // to the passed key.
  using Base::at;

  // node_hash_map::contains()
  //
  // Determines whether an element with a key comparing equal to the given `key`
  // exists within the `node_hash_map`, returning `true` if so or `false`
  // otherwise.
  using Base::contains;

  // node_hash_map::count(const Key& key) const
  //
  // Returns the number of elements with a key comparing equal to the given
  // `key` within the `node_hash_map`. note that this function will return
  // either `1` or `0` since duplicate keys are not allowed within a
  // `node_hash_map`.
  using Base::count;

  // node_hash_map::equal_range()
  //
  // Returns a closed range [first, last], defined by a `std::pair` of two
  // iterators, containing all elements with the passed key in the
  // `node_hash_map`.
  using Base::equal_range;

  // node_hash_map::find()
  //
  // Finds an element with the passed `key` within the `node_hash_map`.
  using Base::find;

  // node_hash_map::operator[]()
  //
  // Returns a reference to the value mapped to the passed key within the
  // `node_hash_map`, performing an `insert()` if the key does not already
  // exist. If an insertion occurs and results in a rehashing of the container,
  // all iterators are invalidated. Otherwise iterators are not affected and
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

  // node_hash_map::bucket_count()
  //
  // Returns the number of "buckets" within the `node_hash_map`.
  using Base::bucket_count;

  // node_hash_map::load_factor()
  //
  // Returns the current load factor of the `node_hash_map` (the average number
  // of slots occupied with a value within the hash map).
  using Base::load_factor;

  // node_hash_map::max_load_factor()
  //
  // Manages the maximum load factor of the `node_hash_map`. Overloads are
  // listed below.
  //
  // float node_hash_map::max_load_factor()
  //
  //   Returns the current maximum load factor of the `node_hash_map`.
  //
  // void node_hash_map::max_load_factor(float ml)
  //
  //   Sets the maximum load factor of the `node_hash_map` to the passed value.
  //
  //   NOTE: This overload is provided only for API compatibility with the STL;
  //   `node_hash_map` will ignore any set load factor and manage its rehashing
  //   internally as an implementation detail.
  using Base::max_load_factor;

  // node_hash_map::get_allocator()
  //
  // Returns the allocator function associated with this `node_hash_map`.
  using Base::get_allocator;

  // node_hash_map::hash_function()
  //
  // Returns the hashing function used to hash the keys within this
  // `node_hash_map`.
  using Base::hash_function;

  // node_hash_map::key_eq()
  //
  // Returns the function used for comparing keys equality.
  using Base::key_eq;
};

// erase_if(node_hash_map<>, Pred)
//
// Erases all elements that satisfy the predicate `pred` from the container `c`.
// Returns the number of erased elements.
template <typename K, typename V, typename H, typename E, typename A,
          typename Predicate>
typename node_hash_map<K, V, H, E, A>::size_type erase_if(
    node_hash_map<K, V, H, E, A>& c, Predicate pred) {
  return container_internal::EraseIf(pred, &c);
}

// swap(node_hash_map<>, node_hash_map<>)
//
// Swaps the contents of two `node_hash_map` containers.
//
// NOTE: we need to define this function template in order for
// `flat_hash_set::swap` to be called instead of `std::swap`. Even though we
// have `swap(raw_hash_set&, raw_hash_set&)` defined, that function requires a
// derived-to-base conversion, whereas `std::swap` is a function template so
// `std::swap` will be preferred by compiler.
template <typename K, typename V, typename H, typename E, typename A>
void swap(node_hash_map<K, V, H, E, A>& x,
          node_hash_map<K, V, H, E, A>& y) noexcept(noexcept(x.swap(y))) {
  return x.swap(y);
}

namespace container_internal {

// c_for_each_fast(node_hash_map<>, Function)
//
// Container-based version of the <algorithm> `std::for_each()` function to
// apply a function to a container's elements.
// There is no guarantees on the order of the function calls.
// Erasure and/or insertion of elements in the function is not allowed.
template <typename K, typename V, typename H, typename E, typename A,
          typename Function>
decay_t<Function> c_for_each_fast(const node_hash_map<K, V, H, E, A>& c,
                                  Function&& f) {
  container_internal::ForEach(f, &c);
  return f;
}
template <typename K, typename V, typename H, typename E, typename A,
          typename Function>
decay_t<Function> c_for_each_fast(node_hash_map<K, V, H, E, A>& c,
                                  Function&& f) {
  container_internal::ForEach(f, &c);
  return f;
}
template <typename K, typename V, typename H, typename E, typename A,
          typename Function>
decay_t<Function> c_for_each_fast(node_hash_map<K, V, H, E, A>&& c,
                                  Function&& f) {
  container_internal::ForEach(f, &c);
  return f;
}

}  // namespace container_internal

namespace container_internal {

template <class Key, class Value>
class NodeHashMapPolicy
    : public absl::container_internal::node_slot_policy<
          std::pair<const Key, Value>&, NodeHashMapPolicy<Key, Value>> {
  using value_type = std::pair<const Key, Value>;

 public:
  using key_type = Key;
  using mapped_type = Value;
  using init_type = std::pair</*non const*/ key_type, mapped_type>;

  template <class Allocator, class... Args>
  static value_type* new_element(Allocator* alloc, Args&&... args) {
    using PairAlloc = typename absl::allocator_traits<
        Allocator>::template rebind_alloc<value_type>;
    PairAlloc pair_alloc(*alloc);
    value_type* res =
        absl::allocator_traits<PairAlloc>::allocate(pair_alloc, 1);
    absl::allocator_traits<PairAlloc>::construct(pair_alloc, res,
                                                 std::forward<Args>(args)...);
    return res;
  }

  template <class Allocator>
  static void delete_element(Allocator* alloc, value_type* pair) {
    using PairAlloc = typename absl::allocator_traits<
        Allocator>::template rebind_alloc<value_type>;
    PairAlloc pair_alloc(*alloc);
    absl::allocator_traits<PairAlloc>::destroy(pair_alloc, pair);
    absl::allocator_traits<PairAlloc>::deallocate(pair_alloc, pair, 1);
  }

  template <class F, class... Args>
  static decltype(absl::container_internal::DecomposePair(
      std::declval<F>(), std::declval<Args>()...))
  apply(F&& f, Args&&... args) {
    return absl::container_internal::DecomposePair(std::forward<F>(f),
                                                   std::forward<Args>(args)...);
  }

  static size_t element_space_used(const value_type*) {
    return sizeof(value_type);
  }

  static Value& value(value_type* elem) { return elem->second; }
  static const Value& value(const value_type* elem) { return elem->second; }

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
    return memory_internal::IsLayoutCompatible<Key, Value>::value
               ? &TypeErasedDerefAndApplyToSlotFn<Hash, Key>
               : nullptr;
  }
};
}  // namespace container_internal

namespace container_algorithm_internal {

// Specialization of trait in absl/algorithm/container.h
template <class Key, class T, class Hash, class KeyEqual, class Allocator>
struct IsUnorderedContainer<
    absl::node_hash_map<Key, T, Hash, KeyEqual, Allocator>> : std::true_type {};

}  // namespace container_algorithm_internal

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_NODE_HASH_MAP_H_
