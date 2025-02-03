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

#ifndef ABSL_CONTAINER_INTERNAL_CONTAINER_MEMORY_H_
#define ABSL_CONTAINER_INTERNAL_CONTAINER_MEMORY_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/base/config.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/utility/utility.h"

#ifdef ABSL_HAVE_ADDRESS_SANITIZER
#include <sanitizer/asan_interface.h>
#endif

#ifdef ABSL_HAVE_MEMORY_SANITIZER
#include <sanitizer/msan_interface.h>
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <size_t Alignment>
struct alignas(Alignment) AlignedType {};

// Allocates at least n bytes aligned to the specified alignment.
// Alignment must be a power of 2. It must be positive.
//
// Note that many allocators don't honor alignment requirements above certain
// threshold (usually either alignof(std::max_align_t) or alignof(void*)).
// Allocate() doesn't apply alignment corrections. If the underlying allocator
// returns insufficiently alignment pointer, that's what you are going to get.
template <size_t Alignment, class Alloc>
void* Allocate(Alloc* alloc, size_t n) {
  static_assert(Alignment > 0, "");
  assert(n && "n must be positive");
  using M = AlignedType<Alignment>;
  using A = typename absl::allocator_traits<Alloc>::template rebind_alloc<M>;
  using AT = typename absl::allocator_traits<Alloc>::template rebind_traits<M>;
  // On macOS, "mem_alloc" is a #define with one argument defined in
  // rpc/types.h, so we can't name the variable "mem_alloc" and initialize it
  // with the "foo(bar)" syntax.
  A my_mem_alloc(*alloc);
  void* p = AT::allocate(my_mem_alloc, (n + sizeof(M) - 1) / sizeof(M));
  assert(reinterpret_cast<uintptr_t>(p) % Alignment == 0 &&
         "allocator does not respect alignment");
  return p;
}

// Returns true if the destruction of the value with given Allocator will be
// trivial.
template <class Allocator, class ValueType>
constexpr auto IsDestructionTrivial() {
  constexpr bool result =
      std::is_trivially_destructible<ValueType>::value &&
      std::is_same<typename absl::allocator_traits<
                       Allocator>::template rebind_alloc<char>,
                   std::allocator<char>>::value;
  return std::integral_constant<bool, result>();
}

// The pointer must have been previously obtained by calling
// Allocate<Alignment>(alloc, n).
template <size_t Alignment, class Alloc>
void Deallocate(Alloc* alloc, void* p, size_t n) {
  static_assert(Alignment > 0, "");
  assert(n && "n must be positive");
  using M = AlignedType<Alignment>;
  using A = typename absl::allocator_traits<Alloc>::template rebind_alloc<M>;
  using AT = typename absl::allocator_traits<Alloc>::template rebind_traits<M>;
  // On macOS, "mem_alloc" is a #define with one argument defined in
  // rpc/types.h, so we can't name the variable "mem_alloc" and initialize it
  // with the "foo(bar)" syntax.
  A my_mem_alloc(*alloc);
  AT::deallocate(my_mem_alloc, static_cast<M*>(p),
                 (n + sizeof(M) - 1) / sizeof(M));
}

namespace memory_internal {

// Constructs T into uninitialized storage pointed by `ptr` using the args
// specified in the tuple.
template <class Alloc, class T, class Tuple, size_t... I>
void ConstructFromTupleImpl(Alloc* alloc, T* ptr, Tuple&& t,
                            absl::index_sequence<I...>) {
  absl::allocator_traits<Alloc>::construct(
      *alloc, ptr, std::get<I>(std::forward<Tuple>(t))...);
}

template <class T, class F>
struct WithConstructedImplF {
  template <class... Args>
  decltype(std::declval<F>()(std::declval<T>())) operator()(
      Args&&... args) const {
    return std::forward<F>(f)(T(std::forward<Args>(args)...));
  }
  F&& f;
};

template <class T, class Tuple, size_t... Is, class F>
decltype(std::declval<F>()(std::declval<T>())) WithConstructedImpl(
    Tuple&& t, absl::index_sequence<Is...>, F&& f) {
  return WithConstructedImplF<T, F>{std::forward<F>(f)}(
      std::get<Is>(std::forward<Tuple>(t))...);
}

template <class T, size_t... Is>
auto TupleRefImpl(T&& t, absl::index_sequence<Is...>)
    -> decltype(std::forward_as_tuple(std::get<Is>(std::forward<T>(t))...)) {
  return std::forward_as_tuple(std::get<Is>(std::forward<T>(t))...);
}

// Returns a tuple of references to the elements of the input tuple. T must be a
// tuple.
template <class T>
auto TupleRef(T&& t) -> decltype(TupleRefImpl(
    std::forward<T>(t),
    absl::make_index_sequence<
        std::tuple_size<typename std::decay<T>::type>::value>())) {
  return TupleRefImpl(
      std::forward<T>(t),
      absl::make_index_sequence<
          std::tuple_size<typename std::decay<T>::type>::value>());
}

template <class F, class K, class V>
decltype(std::declval<F>()(std::declval<const K&>(), std::piecewise_construct,
                           std::declval<std::tuple<K>>(), std::declval<V>()))
DecomposePairImpl(F&& f, std::pair<std::tuple<K>, V> p) {
  const auto& key = std::get<0>(p.first);
  return std::forward<F>(f)(key, std::piecewise_construct, std::move(p.first),
                            std::move(p.second));
}

}  // namespace memory_internal

// Constructs T into uninitialized storage pointed by `ptr` using the args
// specified in the tuple.
template <class Alloc, class T, class Tuple>
void ConstructFromTuple(Alloc* alloc, T* ptr, Tuple&& t) {
  memory_internal::ConstructFromTupleImpl(
      alloc, ptr, std::forward<Tuple>(t),
      absl::make_index_sequence<
          std::tuple_size<typename std::decay<Tuple>::type>::value>());
}

// Constructs T using the args specified in the tuple and calls F with the
// constructed value.
template <class T, class Tuple, class F>
decltype(std::declval<F>()(std::declval<T>())) WithConstructed(Tuple&& t,
                                                               F&& f) {
  return memory_internal::WithConstructedImpl<T>(
      std::forward<Tuple>(t),
      absl::make_index_sequence<
          std::tuple_size<typename std::decay<Tuple>::type>::value>(),
      std::forward<F>(f));
}

// Given arguments of an std::pair's constructor, PairArgs() returns a pair of
// tuples with references to the passed arguments. The tuples contain
// constructor arguments for the first and the second elements of the pair.
//
// The following two snippets are equivalent.
//
// 1. std::pair<F, S> p(args...);
//
// 2. auto a = PairArgs(args...);
//    std::pair<F, S> p(std::piecewise_construct,
//                      std::move(a.first), std::move(a.second));
inline std::pair<std::tuple<>, std::tuple<>> PairArgs() { return {}; }
template <class F, class S>
std::pair<std::tuple<F&&>, std::tuple<S&&>> PairArgs(F&& f, S&& s) {
  return {std::piecewise_construct, std::forward_as_tuple(std::forward<F>(f)),
          std::forward_as_tuple(std::forward<S>(s))};
}
template <class F, class S>
std::pair<std::tuple<const F&>, std::tuple<const S&>> PairArgs(
    const std::pair<F, S>& p) {
  return PairArgs(p.first, p.second);
}
template <class F, class S>
std::pair<std::tuple<F&&>, std::tuple<S&&>> PairArgs(std::pair<F, S>&& p) {
  return PairArgs(std::forward<F>(p.first), std::forward<S>(p.second));
}
template <class F, class S>
auto PairArgs(std::piecewise_construct_t, F&& f, S&& s)
    -> decltype(std::make_pair(memory_internal::TupleRef(std::forward<F>(f)),
                               memory_internal::TupleRef(std::forward<S>(s)))) {
  return std::make_pair(memory_internal::TupleRef(std::forward<F>(f)),
                        memory_internal::TupleRef(std::forward<S>(s)));
}

// A helper function for implementing apply() in map policies.
template <class F, class... Args>
auto DecomposePair(F&& f, Args&&... args)
    -> decltype(memory_internal::DecomposePairImpl(
        std::forward<F>(f), PairArgs(std::forward<Args>(args)...))) {
  return memory_internal::DecomposePairImpl(
      std::forward<F>(f), PairArgs(std::forward<Args>(args)...));
}

// A helper function for implementing apply() in set policies.
template <class F, class Arg>
decltype(std::declval<F>()(std::declval<const Arg&>(), std::declval<Arg>()))
DecomposeValue(F&& f, Arg&& arg) {
  const auto& key = arg;
  return std::forward<F>(f)(key, std::forward<Arg>(arg));
}

// Helper functions for asan and msan.
inline void SanitizerPoisonMemoryRegion(const void* m, size_t s) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  ASAN_POISON_MEMORY_REGION(m, s);
#endif
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  __msan_poison(m, s);
#endif
  (void)m;
  (void)s;
}

inline void SanitizerUnpoisonMemoryRegion(const void* m, size_t s) {
#ifdef ABSL_HAVE_ADDRESS_SANITIZER
  ASAN_UNPOISON_MEMORY_REGION(m, s);
#endif
#ifdef ABSL_HAVE_MEMORY_SANITIZER
  __msan_unpoison(m, s);
#endif
  (void)m;
  (void)s;
}

template <typename T>
inline void SanitizerPoisonObject(const T* object) {
  SanitizerPoisonMemoryRegion(object, sizeof(T));
}

template <typename T>
inline void SanitizerUnpoisonObject(const T* object) {
  SanitizerUnpoisonMemoryRegion(object, sizeof(T));
}

namespace memory_internal {

// If Pair is a standard-layout type, OffsetOf<Pair>::kFirst and
// OffsetOf<Pair>::kSecond are equivalent to offsetof(Pair, first) and
// offsetof(Pair, second) respectively. Otherwise they are -1.
//
// The purpose of OffsetOf is to avoid calling offsetof() on non-standard-layout
// type, which is non-portable.
template <class Pair, class = std::true_type>
struct OffsetOf {
  static constexpr size_t kFirst = static_cast<size_t>(-1);
  static constexpr size_t kSecond = static_cast<size_t>(-1);
};

template <class Pair>
struct OffsetOf<Pair, typename std::is_standard_layout<Pair>::type> {
  static constexpr size_t kFirst = offsetof(Pair, first);
  static constexpr size_t kSecond = offsetof(Pair, second);
};

template <class K, class V>
struct IsLayoutCompatible {
 private:
  struct Pair {
    K first;
    V second;
  };

  // Is P layout-compatible with Pair?
  template <class P>
  static constexpr bool LayoutCompatible() {
    return std::is_standard_layout<P>() && sizeof(P) == sizeof(Pair) &&
           alignof(P) == alignof(Pair) &&
           memory_internal::OffsetOf<P>::kFirst ==
               memory_internal::OffsetOf<Pair>::kFirst &&
           memory_internal::OffsetOf<P>::kSecond ==
               memory_internal::OffsetOf<Pair>::kSecond;
  }

 public:
  // Whether pair<const K, V> and pair<K, V> are layout-compatible. If they are,
  // then it is safe to store them in a union and read from either.
  static constexpr bool value = std::is_standard_layout<K>() &&
                                std::is_standard_layout<Pair>() &&
                                memory_internal::OffsetOf<Pair>::kFirst == 0 &&
                                LayoutCompatible<std::pair<K, V>>() &&
                                LayoutCompatible<std::pair<const K, V>>();
};

}  // namespace memory_internal

// The internal storage type for key-value containers like flat_hash_map.
//
// It is convenient for the value_type of a flat_hash_map<K, V> to be
// pair<const K, V>; the "const K" prevents accidental modification of the key
// when dealing with the reference returned from find() and similar methods.
// However, this creates other problems; we want to be able to emplace(K, V)
// efficiently with move operations, and similarly be able to move a
// pair<K, V> in insert().
//
// The solution is this union, which aliases the const and non-const versions
// of the pair. This also allows flat_hash_map<const K, V> to work, even though
// that has the same efficiency issues with move in emplace() and insert() -
// but people do it anyway.
//
// If kMutableKeys is false, only the value member can be accessed.
//
// If kMutableKeys is true, key can be accessed through all slots while value
// and mutable_value must be accessed only via INITIALIZED slots. Slots are
// created and destroyed via mutable_value so that the key can be moved later.
//
// Accessing one of the union fields while the other is active is safe as
// long as they are layout-compatible, which is guaranteed by the definition of
// kMutableKeys. For C++11, the relevant section of the standard is
// https://timsong-cpp.github.io/cppwp/n3337/class.mem#19 (9.2.19)
template <class K, class V>
union map_slot_type {
  map_slot_type() {}
  ~map_slot_type() = delete;
  using value_type = std::pair<const K, V>;
  using mutable_value_type =
      std::pair<absl::remove_const_t<K>, absl::remove_const_t<V>>;

  value_type value;
  mutable_value_type mutable_value;
  absl::remove_const_t<K> key;
};

template <class K, class V>
struct map_slot_policy {
  using slot_type = map_slot_type<K, V>;
  using value_type = std::pair<const K, V>;
  using mutable_value_type =
      std::pair<absl::remove_const_t<K>, absl::remove_const_t<V>>;

 private:
  static void emplace(slot_type* slot) {
    // The construction of union doesn't do anything at runtime but it allows us
    // to access its members without violating aliasing rules.
    new (slot) slot_type;
  }
  // If pair<const K, V> and pair<K, V> are layout-compatible, we can accept one
  // or the other via slot_type. We are also free to access the key via
  // slot_type::key in this case.
  using kMutableKeys = memory_internal::IsLayoutCompatible<K, V>;

 public:
  static value_type& element(slot_type* slot) { return slot->value; }
  static const value_type& element(const slot_type* slot) {
    return slot->value;
  }

  // When C++17 is available, we can use std::launder to provide mutable
  // access to the key for use in node handle.
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
  static K& mutable_key(slot_type* slot) {
    // Still check for kMutableKeys so that we can avoid calling std::launder
    // unless necessary because it can interfere with optimizations.
    return kMutableKeys::value ? slot->key
                               : *std::launder(const_cast<K*>(
                                     std::addressof(slot->value.first)));
  }
#else  // !(defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606)
  static const K& mutable_key(slot_type* slot) { return key(slot); }
#endif

  static const K& key(const slot_type* slot) {
    return kMutableKeys::value ? slot->key : slot->value.first;
  }

  template <class Allocator, class... Args>
  static void construct(Allocator* alloc, slot_type* slot, Args&&... args) {
    emplace(slot);
    if (kMutableKeys::value) {
      absl::allocator_traits<Allocator>::construct(*alloc, &slot->mutable_value,
                                                   std::forward<Args>(args)...);
    } else {
      absl::allocator_traits<Allocator>::construct(*alloc, &slot->value,
                                                   std::forward<Args>(args)...);
    }
  }

  // Construct this slot by moving from another slot.
  template <class Allocator>
  static void construct(Allocator* alloc, slot_type* slot, slot_type* other) {
    emplace(slot);
    if (kMutableKeys::value) {
      absl::allocator_traits<Allocator>::construct(
          *alloc, &slot->mutable_value, std::move(other->mutable_value));
    } else {
      absl::allocator_traits<Allocator>::construct(*alloc, &slot->value,
                                                   std::move(other->value));
    }
  }

  // Construct this slot by copying from another slot.
  template <class Allocator>
  static void construct(Allocator* alloc, slot_type* slot,
                        const slot_type* other) {
    emplace(slot);
    absl::allocator_traits<Allocator>::construct(*alloc, &slot->value,
                                                 other->value);
  }

  template <class Allocator>
  static auto destroy(Allocator* alloc, slot_type* slot) {
    if (kMutableKeys::value) {
      absl::allocator_traits<Allocator>::destroy(*alloc, &slot->mutable_value);
    } else {
      absl::allocator_traits<Allocator>::destroy(*alloc, &slot->value);
    }
    return IsDestructionTrivial<Allocator, value_type>();
  }

  template <class Allocator>
  static auto transfer(Allocator* alloc, slot_type* new_slot,
                       slot_type* old_slot) {
    auto is_relocatable =
        typename absl::is_trivially_relocatable<value_type>::type();

    emplace(new_slot);
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
    if (is_relocatable) {
      // TODO(b/247130232,b/251814870): remove casts after fixing warnings.
      std::memcpy(static_cast<void*>(std::launder(&new_slot->value)),
                  static_cast<const void*>(&old_slot->value),
                  sizeof(value_type));
      return is_relocatable;
    }
#endif

    if (kMutableKeys::value) {
      absl::allocator_traits<Allocator>::construct(
          *alloc, &new_slot->mutable_value, std::move(old_slot->mutable_value));
    } else {
      absl::allocator_traits<Allocator>::construct(*alloc, &new_slot->value,
                                                   std::move(old_slot->value));
    }
    destroy(alloc, old_slot);
    return is_relocatable;
  }
};

// Type erased function for computing hash of the slot.
using HashSlotFn = size_t (*)(const void* hash_fn, void* slot);

// Type erased function to apply `Fn` to data inside of the `slot`.
// The data is expected to have type `T`.
template <class Fn, class T>
size_t TypeErasedApplyToSlotFn(const void* fn, void* slot) {
  const auto* f = static_cast<const Fn*>(fn);
  return (*f)(*static_cast<const T*>(slot));
}

// Type erased function to apply `Fn` to data inside of the `*slot_ptr`.
// The data is expected to have type `T`.
template <class Fn, class T>
size_t TypeErasedDerefAndApplyToSlotFn(const void* fn, void* slot_ptr) {
  const auto* f = static_cast<const Fn*>(fn);
  const T* slot = *static_cast<const T**>(slot_ptr);
  return (*f)(*slot);
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_CONTAINER_MEMORY_H_
