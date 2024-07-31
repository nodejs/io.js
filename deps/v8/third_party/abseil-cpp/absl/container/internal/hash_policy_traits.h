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

#ifndef ABSL_CONTAINER_INTERNAL_HASH_POLICY_TRAITS_H_
#define ABSL_CONTAINER_INTERNAL_HASH_POLICY_TRAITS_H_

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/container/internal/common_policy_traits.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Defines how slots are initialized/destroyed/moved.
template <class Policy, class = void>
struct hash_policy_traits : common_policy_traits<Policy> {
  // The type of the keys stored in the hashtable.
  using key_type = typename Policy::key_type;

 private:
  struct ReturnKey {
    // When C++17 is available, we can use std::launder to provide mutable
    // access to the key for use in node handle.
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
    template <class Key,
              absl::enable_if_t<std::is_lvalue_reference<Key>::value, int> = 0>
    static key_type& Impl(Key&& k, int) {
      return *std::launder(
          const_cast<key_type*>(std::addressof(std::forward<Key>(k))));
    }
#endif

    template <class Key>
    static Key Impl(Key&& k, char) {
      return std::forward<Key>(k);
    }

    // When Key=T&, we forward the lvalue reference.
    // When Key=T, we return by value to avoid a dangling reference.
    // eg, for string_hash_map.
    template <class Key, class... Args>
    auto operator()(Key&& k, const Args&...) const
        -> decltype(Impl(std::forward<Key>(k), 0)) {
      return Impl(std::forward<Key>(k), 0);
    }
  };

  template <class P = Policy, class = void>
  struct ConstantIteratorsImpl : std::false_type {};

  template <class P>
  struct ConstantIteratorsImpl<P, absl::void_t<typename P::constant_iterators>>
      : P::constant_iterators {};

 public:
  // The actual object stored in the hash table.
  using slot_type = typename Policy::slot_type;

  // The argument type for insertions into the hashtable. This is different
  // from value_type for increased performance. See initializer_list constructor
  // and insert() member functions for more details.
  using init_type = typename Policy::init_type;

  using reference = decltype(Policy::element(std::declval<slot_type*>()));
  using pointer = typename std::remove_reference<reference>::type*;
  using value_type = typename std::remove_reference<reference>::type;

  // Policies can set this variable to tell raw_hash_set that all iterators
  // should be constant, even `iterator`. This is useful for set-like
  // containers.
  // Defaults to false if not provided by the policy.
  using constant_iterators = ConstantIteratorsImpl<>;

  // Returns the amount of memory owned by `slot`, exclusive of `sizeof(*slot)`.
  //
  // If `slot` is nullptr, returns the constant amount of memory owned by any
  // full slot or -1 if slots own variable amounts of memory.
  //
  // PRECONDITION: `slot` is INITIALIZED or nullptr
  template <class P = Policy>
  static size_t space_used(const slot_type* slot) {
    return P::space_used(slot);
  }

  // Provides generalized access to the key for elements, both for elements in
  // the table and for elements that have not yet been inserted (or even
  // constructed).  We would like an API that allows us to say: `key(args...)`
  // but we cannot do that for all cases, so we use this more general API that
  // can be used for many things, including the following:
  //
  //   - Given an element in a table, get its key.
  //   - Given an element initializer, get its key.
  //   - Given `emplace()` arguments, get the element key.
  //
  // Implementations of this must adhere to a very strict technical
  // specification around aliasing and consuming arguments:
  //
  // Let `value_type` be the result type of `element()` without ref- and
  // cv-qualifiers. The first argument is a functor, the rest are constructor
  // arguments for `value_type`. Returns `std::forward<F>(f)(k, xs...)`, where
  // `k` is the element key, and `xs...` are the new constructor arguments for
  // `value_type`. It's allowed for `k` to alias `xs...`, and for both to alias
  // `ts...`. The key won't be touched once `xs...` are used to construct an
  // element; `ts...` won't be touched at all, which allows `apply()` to consume
  // any rvalues among them.
  //
  // If `value_type` is constructible from `Ts&&...`, `Policy::apply()` must not
  // trigger a hard compile error unless it originates from `f`. In other words,
  // `Policy::apply()` must be SFINAE-friendly. If `value_type` is not
  // constructible from `Ts&&...`, either SFINAE or a hard compile error is OK.
  //
  // If `Ts...` is `[cv] value_type[&]` or `[cv] init_type[&]`,
  // `Policy::apply()` must work. A compile error is not allowed, SFINAE or not.
  template <class F, class... Ts, class P = Policy>
  static auto apply(F&& f, Ts&&... ts)
      -> decltype(P::apply(std::forward<F>(f), std::forward<Ts>(ts)...)) {
    return P::apply(std::forward<F>(f), std::forward<Ts>(ts)...);
  }

  // Returns the "key" portion of the slot.
  // Used for node handle manipulation.
  template <class P = Policy>
  static auto mutable_key(slot_type* slot)
      -> decltype(P::apply(ReturnKey(), hash_policy_traits::element(slot))) {
    return P::apply(ReturnKey(), hash_policy_traits::element(slot));
  }

  // Returns the "value" (as opposed to the "key") portion of the element. Used
  // by maps to implement `operator[]`, `at()` and `insert_or_assign()`.
  template <class T, class P = Policy>
  static auto value(T* elem) -> decltype(P::value(elem)) {
    return P::value(elem);
  }

  using HashSlotFn = size_t (*)(const void* hash_fn, void* slot);

  template <class Hash>
  static constexpr HashSlotFn get_hash_slot_fn() {
// get_hash_slot_fn may return nullptr to signal that non type erased function
// should be used. GCC warns against comparing function address with nullptr.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
// silent error: the address of * will never be NULL [-Werror=address]
#pragma GCC diagnostic ignored "-Waddress"
#endif
    return Policy::template get_hash_slot_fn<Hash>() == nullptr
               ? &hash_slot_fn_non_type_erased<Hash>
               : Policy::template get_hash_slot_fn<Hash>();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
  }

  // Whether small object optimization is enabled. False by default.
  static constexpr bool soo_enabled() { return soo_enabled_impl(Rank1{}); }

 private:
  template <class Hash>
  struct HashElement {
    template <class K, class... Args>
    size_t operator()(const K& key, Args&&...) const {
      return h(key);
    }
    const Hash& h;
  };

  template <class Hash>
  static size_t hash_slot_fn_non_type_erased(const void* hash_fn, void* slot) {
    return Policy::apply(HashElement<Hash>{*static_cast<const Hash*>(hash_fn)},
                         Policy::element(static_cast<slot_type*>(slot)));
  }

  // Use go/ranked-overloads for dispatching. Rank1 is preferred.
  struct Rank0 {};
  struct Rank1 : Rank0 {};

  // Use auto -> decltype as an enabler.
  template <class P = Policy>
  static constexpr auto soo_enabled_impl(Rank1) -> decltype(P::soo_enabled()) {
    return P::soo_enabled();
  }

  static constexpr bool soo_enabled_impl(Rank0) { return false; }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_HASH_POLICY_TRAITS_H_
