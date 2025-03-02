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
// File: hash.h
// -----------------------------------------------------------------------------
//
// This header file defines the Abseil `hash` library and the Abseil hashing
// framework. This framework consists of the following:
//
//   * The `absl::Hash` functor, which is used to invoke the hasher within the
//     Abseil hashing framework. `absl::Hash<T>` supports most basic types and
//     a number of Abseil types out of the box.
//   * `AbslHashValue`, an extension point that allows you to extend types to
//     support Abseil hashing without requiring you to define a hashing
//     algorithm.
//   * `HashState`, a type-erased class which implements the manipulation of the
//     hash state (H) itself; contains member functions `combine()`,
//     `combine_contiguous()`, and `combine_unordered()`; and which you can use
//     to contribute to an existing hash state when hashing your types.
//
// Unlike `std::hash` or other hashing frameworks, the Abseil hashing framework
// provides most of its utility by abstracting away the hash algorithm (and its
// implementation) entirely. Instead, a type invokes the Abseil hashing
// framework by simply combining its state with the state of known, hashable
// types. Hashing of that combined state is separately done by `absl::Hash`.
//
// One should assume that a hash algorithm is chosen randomly at the start of
// each process.  E.g., `absl::Hash<int>{}(9)` in one process and
// `absl::Hash<int>{}(9)` in another process are likely to differ.
//
// `absl::Hash` may also produce different values from different dynamically
// loaded libraries. For this reason, `absl::Hash` values must never cross
// boundaries in dynamically loaded libraries (including when used in types like
// hash containers.)
//
// `absl::Hash` is intended to strongly mix input bits with a target of passing
// an [Avalanche Test](https://en.wikipedia.org/wiki/Avalanche_effect).
//
// Example:
//
//   // Suppose we have a class `Circle` for which we want to add hashing:
//   class Circle {
//    public:
//     ...
//    private:
//     std::pair<int, int> center_;
//     int radius_;
//   };
//
//   // To add hashing support to `Circle`, we simply need to add a free
//   // (non-member) function `AbslHashValue()`, and return the combined hash
//   // state of the existing hash state and the class state. You can add such a
//   // free function using a friend declaration within the body of the class:
//   class Circle {
//    public:
//     ...
//     template <typename H>
//     friend H AbslHashValue(H h, const Circle& c) {
//       return H::combine(std::move(h), c.center_, c.radius_);
//     }
//     ...
//   };
//
// For more information, see Adding Type Support to `absl::Hash` below.
//
#ifndef ABSL_HASH_HASH_H_
#define ABSL_HASH_HASH_H_

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/functional/function_ref.h"
#include "absl/hash/internal/hash.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

// -----------------------------------------------------------------------------
// `absl::Hash`
// -----------------------------------------------------------------------------
//
// `absl::Hash<T>` is a convenient general-purpose hash functor for any type `T`
// satisfying any of the following conditions (in order):
//
//  * T is an arithmetic or pointer type
//  * T defines an overload for `AbslHashValue(H, const T&)` for an arbitrary
//    hash state `H`.
//  - T defines a specialization of `std::hash<T>`
//
// `absl::Hash` intrinsically supports the following types:
//
//   * All integral types (including bool)
//   * All enum types
//   * All floating-point types (although hashing them is discouraged)
//   * All pointer types, including nullptr_t
//   * std::pair<T1, T2>, if T1 and T2 are hashable
//   * std::tuple<Ts...>, if all the Ts... are hashable
//   * std::unique_ptr and std::shared_ptr
//   * All string-like types including:
//     * absl::Cord
//     * std::string (as well as any instance of std::basic_string that
//       uses one of {char, wchar_t, char16_t, char32_t} and its associated
//       std::char_traits)
//     * std::string_view (as well as any instance of std::basic_string_view
//       that uses one of {char, wchar_t, char16_t, char32_t} and its associated
//       std::char_traits)
//  * All the standard sequence containers (provided the elements are hashable)
//  * All the standard associative containers (provided the elements are
//    hashable)
//  * absl types such as the following:
//    * absl::string_view
//    * absl::uint128
//    * absl::Time, absl::Duration, and absl::TimeZone
//  * absl containers (provided the elements are hashable) such as the
//    following:
//    * absl::flat_hash_set, absl::node_hash_set, absl::btree_set
//    * absl::flat_hash_map, absl::node_hash_map, absl::btree_map
//    * absl::btree_multiset, absl::btree_multimap
//    * absl::InlinedVector
//    * absl::FixedArray
//
// When absl::Hash is used to hash an unordered container with a custom hash
// functor, the elements are hashed using default absl::Hash semantics, not
// the custom hash functor.  This is consistent with the behavior of
// operator==() on unordered containers, which compares elements pairwise with
// operator==() rather than the custom equality functor.  It is usually a
// mistake to use either operator==() or absl::Hash on unordered collections
// that use functors incompatible with operator==() equality.
//
// Note: the list above is not meant to be exhaustive. Additional type support
// may be added, in which case the above list will be updated.
//
// -----------------------------------------------------------------------------
// absl::Hash Invocation Evaluation
// -----------------------------------------------------------------------------
//
// When invoked, `absl::Hash<T>` searches for supplied hash functions in the
// following order:
//
//   * Natively supported types out of the box (see above)
//   * Types for which an `AbslHashValue()` overload is provided (such as
//     user-defined types). See "Adding Type Support to `absl::Hash`" below.
//   * Types which define a `std::hash<T>` specialization
//
// The fallback to legacy hash functions exists mainly for backwards
// compatibility. If you have a choice, prefer defining an `AbslHashValue`
// overload instead of specializing any legacy hash functors.
//
// -----------------------------------------------------------------------------
// The Hash State Concept, and using `HashState` for Type Erasure
// -----------------------------------------------------------------------------
//
// The `absl::Hash` framework relies on the Concept of a "hash state." Such a
// hash state is used in several places:
//
// * Within existing implementations of `absl::Hash<T>` to store the hashed
//   state of an object. Note that it is up to the implementation how it stores
//   such state. A hash table, for example, may mix the state to produce an
//   integer value; a testing framework may simply hold a vector of that state.
// * Within implementations of `AbslHashValue()` used to extend user-defined
//   types. (See "Adding Type Support to absl::Hash" below.)
// * Inside a `HashState`, providing type erasure for the concept of a hash
//   state, which you can use to extend the `absl::Hash` framework for types
//   that are otherwise difficult to extend using `AbslHashValue()`. (See the
//   `HashState` class below.)
//
// The "hash state" concept contains three member functions for mixing hash
// state:
//
// * `H::combine(state, values...)`
//
//   Combines an arbitrary number of values into a hash state, returning the
//   updated state. Note that the existing hash state is move-only and must be
//   passed by value.
//
//   Each of the value types T must be hashable by H.
//
//   NOTE:
//
//     state = H::combine(std::move(state), value1, value2, value3);
//
//   must be guaranteed to produce the same hash expansion as
//
//     state = H::combine(std::move(state), value1);
//     state = H::combine(std::move(state), value2);
//     state = H::combine(std::move(state), value3);
//
// * `H::combine_contiguous(state, data, size)`
//
//    Combines a contiguous array of `size` elements into a hash state,
//    returning the updated state. Note that the existing hash state is
//    move-only and must be passed by value.
//
//    NOTE:
//
//      state = H::combine_contiguous(std::move(state), data, size);
//
//    need NOT be guaranteed to produce the same hash expansion as a loop
//    (it may perform internal optimizations). If you need this guarantee, use a
//    loop instead.
//
// * `H::combine_unordered(state, begin, end)`
//
//    Combines a set of elements denoted by an iterator pair into a hash
//    state, returning the updated state.  Note that the existing hash
//    state is move-only and must be passed by value.
//
//    Unlike the other two methods, the hashing is order-independent.
//    This can be used to hash unordered collections.
//
// -----------------------------------------------------------------------------
// Adding Type Support to `absl::Hash`
// -----------------------------------------------------------------------------
//
// To add support for your user-defined type, add a proper `AbslHashValue()`
// overload as a free (non-member) function. The overload will take an
// existing hash state and should combine that state with state from the type.
//
// Example:
//
//   template <typename H>
//   H AbslHashValue(H state, const MyType& v) {
//     return H::combine(std::move(state), v.field1, ..., v.fieldN);
//   }
//
// where `(field1, ..., fieldN)` are the members you would use on your
// `operator==` to define equality.
//
// Notice that `AbslHashValue` is not a class member, but an ordinary function.
// An `AbslHashValue` overload for a type should only be declared in the same
// file and namespace as said type. The proper `AbslHashValue` implementation
// for a given type will be discovered via ADL.
//
// Note: unlike `std::hash', `absl::Hash` should never be specialized. It must
// only be extended by adding `AbslHashValue()` overloads.
//
template <typename T>
using Hash = absl::hash_internal::Hash<T>;

// HashOf
//
// absl::HashOf() is a helper that generates a hash from the values of its
// arguments.  It dispatches to absl::Hash directly, as follows:
//  * HashOf(t) == absl::Hash<T>{}(t)
//  * HashOf(a, b, c) == HashOf(std::make_tuple(a, b, c))
//
// HashOf(a1, a2, ...) == HashOf(b1, b2, ...) is guaranteed when
//  * The argument lists have pairwise identical C++ types
//  * a1 == b1 && a2 == b2 && ...
//
// The requirement that the arguments match in both type and value is critical.
// It means that `a == b` does not necessarily imply `HashOf(a) == HashOf(b)` if
// `a` and `b` have different types. For example, `HashOf(2) != HashOf(2.0)`.
template <int&... ExplicitArgumentBarrier, typename... Types>
size_t HashOf(const Types&... values) {
  auto tuple = std::tie(values...);
  return absl::Hash<decltype(tuple)>{}(tuple);
}

// HashState
//
// A type erased version of the hash state concept, for use in user-defined
// `AbslHashValue` implementations that can't use templates (such as PImpl
// classes, virtual functions, etc.). The type erasure adds overhead so it
// should be avoided unless necessary.
//
// Note: This wrapper will only erase calls to
//     combine_contiguous(H, const unsigned char*, size_t)
//     RunCombineUnordered(H, CombinerF)
//
// All other calls will be handled internally and will not invoke overloads
// provided by the wrapped class.
//
// Users of this class should still define a template `AbslHashValue` function,
// but can use `absl::HashState::Create(&state)` to erase the type of the hash
// state and dispatch to their private hashing logic.
//
// This state can be used like any other hash state. In particular, you can call
// `HashState::combine()` and `HashState::combine_contiguous()` on it.
//
// Example:
//
//   class Interface {
//    public:
//     template <typename H>
//     friend H AbslHashValue(H state, const Interface& value) {
//       state = H::combine(std::move(state), std::type_index(typeid(*this)));
//       value.HashValue(absl::HashState::Create(&state));
//       return state;
//     }
//    private:
//     virtual void HashValue(absl::HashState state) const = 0;
//   };
//
//   class Impl : Interface {
//    private:
//     void HashValue(absl::HashState state) const override {
//       absl::HashState::combine(std::move(state), v1_, v2_);
//     }
//     int v1_;
//     std::string v2_;
//   };
class HashState : public hash_internal::HashStateBase<HashState> {
 public:
  // HashState::Create()
  //
  // Create a new `HashState` instance that wraps `state`. All calls to
  // `combine()` and `combine_contiguous()` on the new instance will be
  // redirected to the original `state` object. The `state` object must outlive
  // the `HashState` instance. `T` must be a subclass of `HashStateBase<T>` -
  // users should not define their own HashState types.
  template <
      typename T,
      absl::enable_if_t<
          std::is_base_of<hash_internal::HashStateBase<T>, T>::value, int> = 0>
  static HashState Create(T* state) {
    HashState s;
    s.Init(state);
    return s;
  }

  HashState(const HashState&) = delete;
  HashState& operator=(const HashState&) = delete;
  HashState(HashState&&) = default;
  HashState& operator=(HashState&&) = default;

  // HashState::combine()
  //
  // Combines an arbitrary number of values into a hash state, returning the
  // updated state.
  using HashState::HashStateBase::combine;

  // HashState::combine_contiguous()
  //
  // Combines a contiguous array of `size` elements into a hash state, returning
  // the updated state.
  static HashState combine_contiguous(HashState hash_state,
                                      const unsigned char* first, size_t size) {
    hash_state.combine_contiguous_(hash_state.state_, first, size);
    return hash_state;
  }
  using HashState::HashStateBase::combine_contiguous;

 private:
  HashState() = default;

  friend class HashState::HashStateBase;
  friend struct hash_internal::CombineRaw;

  template <typename T>
  static void CombineContiguousImpl(void* p, const unsigned char* first,
                                    size_t size) {
    T& state = *static_cast<T*>(p);
    state = T::combine_contiguous(std::move(state), first, size);
  }

  static HashState combine_raw(HashState hash_state, uint64_t value) {
    hash_state.combine_raw_(hash_state.state_, value);
    return hash_state;
  }

  template <typename T>
  static void CombineRawImpl(void* p, uint64_t value) {
    T& state = *static_cast<T*>(p);
    state = hash_internal::CombineRaw()(std::move(state), value);
  }

  template <typename T>
  void Init(T* state) {
    state_ = state;
    combine_contiguous_ = &CombineContiguousImpl<T>;
    combine_raw_ = &CombineRawImpl<T>;
    run_combine_unordered_ = &RunCombineUnorderedImpl<T>;
  }

  template <typename HS>
  struct CombineUnorderedInvoker {
    template <typename T, typename ConsumerT>
    void operator()(T inner_state, ConsumerT inner_cb) {
      f(HashState::Create(&inner_state),
        [&](HashState& inner_erased) { inner_cb(inner_erased.Real<T>()); });
    }

    absl::FunctionRef<void(HS, absl::FunctionRef<void(HS&)>)> f;
  };

  template <typename T>
  static HashState RunCombineUnorderedImpl(
      HashState state,
      absl::FunctionRef<void(HashState, absl::FunctionRef<void(HashState&)>)>
          f) {
    // Note that this implementation assumes that inner_state and outer_state
    // are the same type.  This isn't true in the SpyHash case, but SpyHash
    // types are move-convertible to each other, so this still works.
    T& real_state = state.Real<T>();
    real_state = T::RunCombineUnordered(
        std::move(real_state), CombineUnorderedInvoker<HashState>{f});
    return state;
  }

  template <typename CombinerT>
  static HashState RunCombineUnordered(HashState state, CombinerT combiner) {
    auto* run = state.run_combine_unordered_;
    return run(std::move(state), std::ref(combiner));
  }

  // Do not erase an already erased state.
  void Init(HashState* state) {
    state_ = state->state_;
    combine_contiguous_ = state->combine_contiguous_;
    combine_raw_ = state->combine_raw_;
    run_combine_unordered_ = state->run_combine_unordered_;
  }

  template <typename T>
  T& Real() {
    return *static_cast<T*>(state_);
  }

  void* state_;
  void (*combine_contiguous_)(void*, const unsigned char*, size_t);
  void (*combine_raw_)(void*, uint64_t);
  HashState (*run_combine_unordered_)(
      HashState state,
      absl::FunctionRef<void(HashState, absl::FunctionRef<void(HashState&)>)>);
};

ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HASH_HASH_H_
