// Copyright 2022 The Abseil Authors.
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

#ifndef ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_
#define ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class Policy, class = void>
struct policy_trait_element_is_owner : std::false_type {};

template <class Policy>
struct policy_trait_element_is_owner<
    Policy,
    std::enable_if_t<!std::is_void<typename Policy::element_is_owner>::value>>
    : Policy::element_is_owner {};

// Defines how slots are initialized/destroyed/moved.
template <class Policy, class = void>
struct common_policy_traits {
  // The actual object stored in the container.
  using slot_type = typename Policy::slot_type;
  using reference = decltype(Policy::element(std::declval<slot_type*>()));
  using value_type = typename std::remove_reference<reference>::type;

  // PRECONDITION: `slot` is UNINITIALIZED
  // POSTCONDITION: `slot` is INITIALIZED
  template <class Alloc, class... Args>
  static void construct(Alloc* alloc, slot_type* slot, Args&&... args) {
    Policy::construct(alloc, slot, std::forward<Args>(args)...);
  }

  // PRECONDITION: `slot` is INITIALIZED
  // POSTCONDITION: `slot` is UNINITIALIZED
  // Returns std::true_type in case destroy is trivial.
  template <class Alloc>
  static auto destroy(Alloc* alloc, slot_type* slot) {
    return Policy::destroy(alloc, slot);
  }

  // Transfers the `old_slot` to `new_slot`. Any memory allocated by the
  // allocator inside `old_slot` to `new_slot` can be transferred.
  //
  // OPTIONAL: defaults to:
  //
  //     clone(new_slot, std::move(*old_slot));
  //     destroy(old_slot);
  //
  // PRECONDITION: `new_slot` is UNINITIALIZED and `old_slot` is INITIALIZED
  // POSTCONDITION: `new_slot` is INITIALIZED and `old_slot` is
  //                UNINITIALIZED
  template <class Alloc>
  static void transfer(Alloc* alloc, slot_type* new_slot, slot_type* old_slot) {
    transfer_impl(alloc, new_slot, old_slot, Rank2{});
  }

  // PRECONDITION: `slot` is INITIALIZED
  // POSTCONDITION: `slot` is INITIALIZED
  // Note: we use remove_const_t so that the two overloads have different args
  // in the case of sets with explicitly const value_types.
  template <class P = Policy>
  static auto element(absl::remove_const_t<slot_type>* slot)
      -> decltype(P::element(slot)) {
    return P::element(slot);
  }
  template <class P = Policy>
  static auto element(const slot_type* slot) -> decltype(P::element(slot)) {
    return P::element(slot);
  }

  static constexpr bool transfer_uses_memcpy() {
    return std::is_same<decltype(transfer_impl<std::allocator<char>>(
                            nullptr, nullptr, nullptr, Rank2{})),
                        std::true_type>::value;
  }

  // Returns true if destroy is trivial and can be omitted.
  template <class Alloc>
  static constexpr bool destroy_is_trivial() {
    return std::is_same<decltype(destroy<Alloc>(nullptr, nullptr)),
                        std::true_type>::value;
  }

 private:
  // Use go/ranked-overloads for dispatching.
  struct Rank0 {};
  struct Rank1 : Rank0 {};
  struct Rank2 : Rank1 {};

  // Use auto -> decltype as an enabler.
  // P::transfer returns std::true_type if transfer uses memcpy (e.g. in
  // node_slot_policy).
  template <class Alloc, class P = Policy>
  static auto transfer_impl(Alloc* alloc, slot_type* new_slot,
                            slot_type* old_slot,
                            Rank2) -> decltype(P::transfer(alloc, new_slot,
                                                           old_slot)) {
    return P::transfer(alloc, new_slot, old_slot);
  }
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
  // This overload returns true_type for the trait below.
  // The conditional_t is to make the enabler type dependent.
  template <class Alloc,
            typename = std::enable_if_t<absl::is_trivially_relocatable<
                std::conditional_t<false, Alloc, value_type>>::value>>
  static std::true_type transfer_impl(Alloc*, slot_type* new_slot,
                                      slot_type* old_slot, Rank1) {
    // TODO(b/247130232): remove casts after fixing warnings.
    // TODO(b/251814870): remove casts after fixing warnings.
    std::memcpy(
        static_cast<void*>(std::launder(
            const_cast<std::remove_const_t<value_type>*>(&element(new_slot)))),
        static_cast<const void*>(&element(old_slot)), sizeof(value_type));
    return {};
  }
#endif

  template <class Alloc>
  static void transfer_impl(Alloc* alloc, slot_type* new_slot,
                            slot_type* old_slot, Rank0) {
    construct(alloc, new_slot, std::move(element(old_slot)));
    destroy(alloc, old_slot);
  }
};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_COMMON_POLICY_TRAITS_H_
