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
// Adapts a policy for nodes.
//
// The node policy should model:
//
// struct Policy {
//   // Returns a new node allocated and constructed using the allocator, using
//   // the specified arguments.
//   template <class Alloc, class... Args>
//   value_type* new_element(Alloc* alloc, Args&&... args) const;
//
//   // Destroys and deallocates node using the allocator.
//   template <class Alloc>
//   void delete_element(Alloc* alloc, value_type* node) const;
// };
//
// It may also optionally define `value()` and `apply()`. For documentation on
// these, see hash_policy_traits.h.

#ifndef ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_
#define ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class Reference, class Policy>
struct node_slot_policy {
  static_assert(std::is_lvalue_reference<Reference>::value, "");

  using slot_type = typename std::remove_cv<
      typename std::remove_reference<Reference>::type>::type*;

  template <class Alloc, class... Args>
  static void construct(Alloc* alloc, slot_type* slot, Args&&... args) {
    *slot = Policy::new_element(alloc, std::forward<Args>(args)...);
  }

  template <class Alloc>
  static void destroy(Alloc* alloc, slot_type* slot) {
    Policy::delete_element(alloc, *slot);
  }

  // Returns true_type to indicate that transfer can use memcpy.
  template <class Alloc>
  static std::true_type transfer(Alloc*, slot_type* new_slot,
                                 slot_type* old_slot) {
    *new_slot = *old_slot;
    return {};
  }

  static size_t space_used(const slot_type* slot) {
    if (slot == nullptr) return Policy::element_space_used(nullptr);
    return Policy::element_space_used(*slot);
  }

  static Reference element(slot_type* slot) { return **slot; }

  template <class T, class P = Policy>
  static auto value(T* elem) -> decltype(P::value(elem)) {
    return P::value(elem);
  }

  template <class... Ts, class P = Policy>
  static auto apply(Ts&&... ts) -> decltype(P::apply(std::forward<Ts>(ts)...)) {
    return P::apply(std::forward<Ts>(ts)...);
  }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_NODE_SLOT_POLICY_H_
