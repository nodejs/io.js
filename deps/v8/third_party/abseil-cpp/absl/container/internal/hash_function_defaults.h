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
// Define the default Hash and Eq functions for SwissTable containers.
//
// std::hash<T> and std::equal_to<T> are not appropriate hash and equal
// functions for SwissTable containers. There are two reasons for this.
//
// SwissTable containers are power of 2 sized containers:
//
// This means they use the lower bits of the hash value to find the slot for
// each entry. The typical hash function for integral types is the identity.
// This is a very weak hash function for SwissTable and any power of 2 sized
// hashtable implementation which will lead to excessive collisions. For
// SwissTable we use murmur3 style mixing to reduce collisions to a minimum.
//
// SwissTable containers support heterogeneous lookup:
//
// In order to make heterogeneous lookup work, hash and equal functions must be
// polymorphic. At the same time they have to satisfy the same requirements the
// C++ standard imposes on hash functions and equality operators. That is:
//
//   if hash_default_eq<T>(a, b) returns true for any a and b of type T, then
//   hash_default_hash<T>(a) must equal hash_default_hash<T>(b)
//
// For SwissTable containers this requirement is relaxed to allow a and b of
// any and possibly different types. Note that like the standard the hash and
// equal functions are still bound to T. This is important because some type U
// can be hashed by/tested for equality differently depending on T. A notable
// example is `const char*`. `const char*` is treated as a c-style string when
// the hash function is hash<std::string> but as a pointer when the hash
// function is hash<void*>.
//
#ifndef ABSL_CONTAINER_INTERNAL_HASH_FUNCTION_DEFAULTS_H_
#define ABSL_CONTAINER_INTERNAL_HASH_FUNCTION_DEFAULTS_H_

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>

#include "absl/base/config.h"
#include "absl/container/internal/common.h"
#include "absl/hash/hash.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/cord.h"
#include "absl/strings/string_view.h"

#ifdef ABSL_HAVE_STD_STRING_VIEW
#include <string_view>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// The hash of an object of type T is computed by using absl::Hash.
template <class T, class E = void>
struct HashEq {
  using Hash = absl::Hash<T>;
  using Eq = std::equal_to<T>;
};

struct StringHash {
  using is_transparent = void;

  size_t operator()(absl::string_view v) const {
    return absl::Hash<absl::string_view>{}(v);
  }
  size_t operator()(const absl::Cord& v) const {
    return absl::Hash<absl::Cord>{}(v);
  }
};

struct StringEq {
  using is_transparent = void;
  bool operator()(absl::string_view lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }
  bool operator()(const absl::Cord& lhs, const absl::Cord& rhs) const {
    return lhs == rhs;
  }
  bool operator()(const absl::Cord& lhs, absl::string_view rhs) const {
    return lhs == rhs;
  }
  bool operator()(absl::string_view lhs, const absl::Cord& rhs) const {
    return lhs == rhs;
  }
};

// Supports heterogeneous lookup for string-like elements.
struct StringHashEq {
  using Hash = StringHash;
  using Eq = StringEq;
};

template <>
struct HashEq<std::string> : StringHashEq {};
template <>
struct HashEq<absl::string_view> : StringHashEq {};
template <>
struct HashEq<absl::Cord> : StringHashEq {};

#ifdef ABSL_HAVE_STD_STRING_VIEW

template <typename TChar>
struct BasicStringHash {
  using is_transparent = void;

  size_t operator()(std::basic_string_view<TChar> v) const {
    return absl::Hash<std::basic_string_view<TChar>>{}(v);
  }
};

template <typename TChar>
struct BasicStringEq {
  using is_transparent = void;
  bool operator()(std::basic_string_view<TChar> lhs,
                  std::basic_string_view<TChar> rhs) const {
    return lhs == rhs;
  }
};

// Supports heterogeneous lookup for w/u16/u32 string + string_view + char*.
template <typename TChar>
struct BasicStringHashEq {
  using Hash = BasicStringHash<TChar>;
  using Eq = BasicStringEq<TChar>;
};

template <>
struct HashEq<std::wstring> : BasicStringHashEq<wchar_t> {};
template <>
struct HashEq<std::wstring_view> : BasicStringHashEq<wchar_t> {};
template <>
struct HashEq<std::u16string> : BasicStringHashEq<char16_t> {};
template <>
struct HashEq<std::u16string_view> : BasicStringHashEq<char16_t> {};
template <>
struct HashEq<std::u32string> : BasicStringHashEq<char32_t> {};
template <>
struct HashEq<std::u32string_view> : BasicStringHashEq<char32_t> {};

#endif  // ABSL_HAVE_STD_STRING_VIEW

// Supports heterogeneous lookup for pointers and smart pointers.
template <class T>
struct HashEq<T*> {
  struct Hash {
    using is_transparent = void;
    template <class U>
    size_t operator()(const U& ptr) const {
      return absl::Hash<const T*>{}(HashEq::ToPtr(ptr));
    }
  };
  struct Eq {
    using is_transparent = void;
    template <class A, class B>
    bool operator()(const A& a, const B& b) const {
      return HashEq::ToPtr(a) == HashEq::ToPtr(b);
    }
  };

 private:
  static const T* ToPtr(const T* ptr) { return ptr; }
  template <class U, class D>
  static const T* ToPtr(const std::unique_ptr<U, D>& ptr) {
    return ptr.get();
  }
  template <class U>
  static const T* ToPtr(const std::shared_ptr<U>& ptr) {
    return ptr.get();
  }
};

template <class T, class D>
struct HashEq<std::unique_ptr<T, D>> : HashEq<T*> {};
template <class T>
struct HashEq<std::shared_ptr<T>> : HashEq<T*> {};

template <typename T, typename E = void>
struct HasAbslContainerHash : std::false_type {};

template <typename T>
struct HasAbslContainerHash<T, absl::void_t<typename T::absl_container_hash>>
    : std::true_type {};

template <typename T, typename E = void>
struct HasAbslContainerEq : std::false_type {};

template <typename T>
struct HasAbslContainerEq<T, absl::void_t<typename T::absl_container_eq>>
    : std::true_type {};

template <typename T, typename E = void>
struct AbslContainerEq {
  using type = std::equal_to<>;
};

template <typename T>
struct AbslContainerEq<
    T, typename std::enable_if_t<HasAbslContainerEq<T>::value>> {
  using type = typename T::absl_container_eq;
};

template <typename T, typename E = void>
struct AbslContainerHash {
  using type = void;
};

template <typename T>
struct AbslContainerHash<
    T, typename std::enable_if_t<HasAbslContainerHash<T>::value>> {
  using type = typename T::absl_container_hash;
};

// HashEq specialization for user types that provide `absl_container_hash` and
// (optionally) `absl_container_eq`. This specialization allows user types to
// provide heterogeneous lookup without requiring to explicitly specify Hash/Eq
// type arguments in unordered Abseil containers.
//
// Both `absl_container_hash` and `absl_container_eq` should be transparent
// (have inner is_transparent type). While there is no technical reason to
// restrict to transparent-only types, there is also no feasible use case when
// it shouldn't be transparent - it is easier to relax the requirement later if
// such a case arises rather than restricting it.
//
// If type provides only `absl_container_hash` then `eq` part will be
// `std::equal_to<void>`.
//
// User types are not allowed to provide only a `Eq` part as there is no
// feasible use case for this behavior - if Hash should be a default one then Eq
// should be an equivalent to the `std::equal_to<T>`.
template <typename T>
struct HashEq<T, typename std::enable_if_t<HasAbslContainerHash<T>::value>> {
  using Hash = typename AbslContainerHash<T>::type;
  using Eq = typename AbslContainerEq<T>::type;
  static_assert(IsTransparent<Hash>::value,
                "absl_container_hash must be transparent. To achieve it add a "
                "`using is_transparent = void;` clause to this type.");
  static_assert(IsTransparent<Eq>::value,
                "absl_container_eq must be transparent. To achieve it add a "
                "`using is_transparent = void;` clause to this type.");
};

// This header's visibility is restricted.  If you need to access the default
// hasher please use the container's ::hasher alias instead.
//
// Example: typename Hash = typename absl::flat_hash_map<K, V>::hasher
template <class T>
using hash_default_hash = typename container_internal::HashEq<T>::Hash;

// This header's visibility is restricted.  If you need to access the default
// key equal please use the container's ::key_equal alias instead.
//
// Example: typename Eq = typename absl::flat_hash_map<K, V, Hash>::key_equal
template <class T>
using hash_default_eq = typename container_internal::HashEq<T>::Eq;

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_HASH_FUNCTION_DEFAULTS_H_
