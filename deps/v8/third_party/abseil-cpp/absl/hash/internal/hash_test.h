// Copyright 2023 The Abseil Authors.
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

// Common code shared between absl/hash/hash_test.cc and
// absl/hash/hash_instantiated_test.cc.

#ifndef ABSL_HASH_INTERNAL_HASH_TEST_H_
#define ABSL_HASH_INTERNAL_HASH_TEST_H_

#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/hash/hash.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace hash_test_internal {

// Utility wrapper of T for the purposes of testing the `AbslHash` type erasure
// mechanism.  `TypeErasedValue<T>` can be constructed with a `T`, and can
// be compared and hashed.  However, all hashing goes through the hashing
// type-erasure framework.
template <typename T>
class TypeErasedValue {
 public:
  TypeErasedValue() = default;
  TypeErasedValue(const TypeErasedValue&) = default;
  TypeErasedValue(TypeErasedValue&&) = default;
  explicit TypeErasedValue(const T& n) : n_(n) {}

  template <typename H>
  friend H AbslHashValue(H hash_state, const TypeErasedValue& v) {
    v.HashValue(absl::HashState::Create(&hash_state));
    return hash_state;
  }

  void HashValue(absl::HashState state) const {
    absl::HashState::combine(std::move(state), n_);
  }

  bool operator==(const TypeErasedValue& rhs) const { return n_ == rhs.n_; }
  bool operator!=(const TypeErasedValue& rhs) const { return !(*this == rhs); }

 private:
  T n_;
};

// A TypeErasedValue refinement, for containers.  It exposes the wrapped
// `value_type` and is constructible from an initializer list.
template <typename T>
class TypeErasedContainer : public TypeErasedValue<T> {
 public:
  using value_type = typename T::value_type;
  TypeErasedContainer() = default;
  TypeErasedContainer(const TypeErasedContainer&) = default;
  TypeErasedContainer(TypeErasedContainer&&) = default;
  explicit TypeErasedContainer(const T& n) : TypeErasedValue<T>(n) {}
  TypeErasedContainer(std::initializer_list<value_type> init_list)
      : TypeErasedContainer(T(init_list.begin(), init_list.end())) {}
  // one-argument constructor of value type T, to appease older toolchains that
  // get confused by one-element initializer lists in some contexts
  explicit TypeErasedContainer(const value_type& v)
      : TypeErasedContainer(T(&v, &v + 1)) {}
};

// Helper trait to verify if T is hashable. We use absl::Hash's poison status to
// detect it.
template <typename T>
using is_hashable = std::is_default_constructible<absl::Hash<T>>;

}  // namespace hash_test_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_HASH_INTERNAL_HASH_TEST_H_
