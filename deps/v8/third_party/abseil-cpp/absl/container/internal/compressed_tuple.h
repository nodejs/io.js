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
// Helper class to perform the Empty Base Optimization.
// Ts can contain classes and non-classes, empty or not. For the ones that
// are empty classes, we perform the optimization. If all types in Ts are empty
// classes, then CompressedTuple<Ts...> is itself an empty class.
//
// To access the members, use member get<N>() function.
//
// Eg:
//   absl::container_internal::CompressedTuple<int, T1, T2, T3> value(7, t1, t2,
//                                                                    t3);
//   assert(value.get<0>() == 7);
//   T1& t1 = value.get<1>();
//   const T2& t2 = value.get<2>();
//   ...
//
// https://en.cppreference.com/w/cpp/language/ebo

#ifndef ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_
#define ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_

#include <initializer_list>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/utility/utility.h"

#if defined(_MSC_VER) && !defined(__NVCC__)
// We need to mark these classes with this declspec to ensure that
// CompressedTuple happens.
#define ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC __declspec(empty_bases)
#else
#define ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <typename... Ts>
class CompressedTuple;

namespace internal_compressed_tuple {

template <typename D, size_t I>
struct Elem;
template <typename... B, size_t I>
struct Elem<CompressedTuple<B...>, I>
    : std::tuple_element<I, std::tuple<B...>> {};
template <typename D, size_t I>
using ElemT = typename Elem<D, I>::type;

// We can't use EBCO on other CompressedTuples because that would mean that we
// derive from multiple Storage<> instantiations with the same I parameter,
// and potentially from multiple identical Storage<> instantiations.  So anytime
// we use type inheritance rather than encapsulation, we mark
// CompressedTupleImpl, to make this easy to detect.
struct uses_inheritance {};

template <typename T>
constexpr bool ShouldUseBase() {
  return std::is_class<T>::value && std::is_empty<T>::value &&
         !std::is_final<T>::value &&
         !std::is_base_of<uses_inheritance, T>::value;
}

// The storage class provides two specializations:
//  - For empty classes, it stores T as a base class.
//  - For everything else, it stores T as a member.
template <typename T, size_t I, bool UseBase = ShouldUseBase<T>()>
struct Storage {
  T value;
  constexpr Storage() = default;
  template <typename V>
  explicit constexpr Storage(absl::in_place_t, V&& v)
      : value(std::forward<V>(v)) {}
  constexpr const T& get() const& { return value; }
  T& get() & { return value; }
  constexpr const T&& get() const&& { return std::move(*this).value; }
  T&& get() && { return std::move(*this).value; }
};

template <typename T, size_t I>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC Storage<T, I, true> : T {
  constexpr Storage() = default;

  template <typename V>
  explicit constexpr Storage(absl::in_place_t, V&& v) : T(std::forward<V>(v)) {}

  constexpr const T& get() const& { return *this; }
  T& get() & { return *this; }
  constexpr const T&& get() const&& { return std::move(*this); }
  T&& get() && { return std::move(*this); }
};

template <typename D, typename I, bool ShouldAnyUseBase>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl;

template <typename... Ts, size_t... I, bool ShouldAnyUseBase>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl<
    CompressedTuple<Ts...>, absl::index_sequence<I...>, ShouldAnyUseBase>
    // We use the dummy identity function through std::integral_constant to
    // convince MSVC of accepting and expanding I in that context. Without it
    // you would get:
    //   error C3548: 'I': parameter pack cannot be used in this context
    : uses_inheritance,
      Storage<Ts, std::integral_constant<size_t, I>::value>... {
  constexpr CompressedTupleImpl() = default;
  template <typename... Vs>
  explicit constexpr CompressedTupleImpl(absl::in_place_t, Vs&&... args)
      : Storage<Ts, I>(absl::in_place, std::forward<Vs>(args))... {}
  friend CompressedTuple<Ts...>;
};

template <typename... Ts, size_t... I>
struct ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTupleImpl<
    CompressedTuple<Ts...>, absl::index_sequence<I...>, false>
    // We use the dummy identity function as above...
    : Storage<Ts, std::integral_constant<size_t, I>::value, false>... {
  constexpr CompressedTupleImpl() = default;
  template <typename... Vs>
  explicit constexpr CompressedTupleImpl(absl::in_place_t, Vs&&... args)
      : Storage<Ts, I, false>(absl::in_place, std::forward<Vs>(args))... {}
  friend CompressedTuple<Ts...>;
};

std::false_type Or(std::initializer_list<std::false_type>);
std::true_type Or(std::initializer_list<bool>);

// MSVC requires this to be done separately rather than within the declaration
// of CompressedTuple below.
template <typename... Ts>
constexpr bool ShouldAnyUseBase() {
  return decltype(
      Or({std::integral_constant<bool, ShouldUseBase<Ts>()>()...})){};
}

template <typename T, typename V>
using TupleElementMoveConstructible =
    typename std::conditional<std::is_reference<T>::value,
                              std::is_convertible<V, T>,
                              std::is_constructible<T, V&&>>::type;

template <bool SizeMatches, class T, class... Vs>
struct TupleMoveConstructible : std::false_type {};

template <class... Ts, class... Vs>
struct TupleMoveConstructible<true, CompressedTuple<Ts...>, Vs...>
    : std::integral_constant<
          bool, absl::conjunction<
                    TupleElementMoveConstructible<Ts, Vs&&>...>::value> {};

template <typename T>
struct compressed_tuple_size;

template <typename... Es>
struct compressed_tuple_size<CompressedTuple<Es...>>
    : public std::integral_constant<std::size_t, sizeof...(Es)> {};

template <class T, class... Vs>
struct TupleItemsMoveConstructible
    : std::integral_constant<
          bool, TupleMoveConstructible<compressed_tuple_size<T>::value ==
                                           sizeof...(Vs),
                                       T, Vs...>::value> {};

}  // namespace internal_compressed_tuple

// Helper class to perform the Empty Base Class Optimization.
// Ts can contain classes and non-classes, empty or not. For the ones that
// are empty classes, we perform the CompressedTuple. If all types in Ts are
// empty classes, then CompressedTuple<Ts...> is itself an empty class.  (This
// does not apply when one or more of those empty classes is itself an empty
// CompressedTuple.)
//
// To access the members, use member .get<N>() function.
//
// Eg:
//   absl::container_internal::CompressedTuple<int, T1, T2, T3> value(7, t1, t2,
//                                                                    t3);
//   assert(value.get<0>() == 7);
//   T1& t1 = value.get<1>();
//   const T2& t2 = value.get<2>();
//   ...
//
// https://en.cppreference.com/w/cpp/language/ebo
template <typename... Ts>
class ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTuple
    : private internal_compressed_tuple::CompressedTupleImpl<
          CompressedTuple<Ts...>, absl::index_sequence_for<Ts...>,
          internal_compressed_tuple::ShouldAnyUseBase<Ts...>()> {
 private:
  template <int I>
  using ElemT = internal_compressed_tuple::ElemT<CompressedTuple, I>;

  template <int I>
  using StorageT = internal_compressed_tuple::Storage<ElemT<I>, I>;

 public:
  // There seems to be a bug in MSVC dealing in which using '=default' here will
  // cause the compiler to ignore the body of other constructors. The work-
  // around is to explicitly implement the default constructor.
#if defined(_MSC_VER)
  constexpr CompressedTuple() : CompressedTuple::CompressedTupleImpl() {}
#else
  constexpr CompressedTuple() = default;
#endif
  explicit constexpr CompressedTuple(const Ts&... base)
      : CompressedTuple::CompressedTupleImpl(absl::in_place, base...) {}

  template <typename First, typename... Vs,
            absl::enable_if_t<
                absl::conjunction<
                    // Ensure we are not hiding default copy/move constructors.
                    absl::negation<std::is_same<void(CompressedTuple),
                                                void(absl::decay_t<First>)>>,
                    internal_compressed_tuple::TupleItemsMoveConstructible<
                        CompressedTuple<Ts...>, First, Vs...>>::value,
                bool> = true>
  explicit constexpr CompressedTuple(First&& first, Vs&&... base)
      : CompressedTuple::CompressedTupleImpl(absl::in_place,
                                             std::forward<First>(first),
                                             std::forward<Vs>(base)...) {}

  template <int I>
  ElemT<I>& get() & {
    return StorageT<I>::get();
  }

  template <int I>
  constexpr const ElemT<I>& get() const& {
    return StorageT<I>::get();
  }

  template <int I>
  ElemT<I>&& get() && {
    return std::move(*this).StorageT<I>::get();
  }

  template <int I>
  constexpr const ElemT<I>&& get() const&& {
    return std::move(*this).StorageT<I>::get();
  }
};

// Explicit specialization for a zero-element tuple
// (needed to avoid ambiguous overloads for the default constructor).
template <>
class ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC CompressedTuple<> {};

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#undef ABSL_INTERNAL_COMPRESSED_TUPLE_DECLSPEC

#endif  // ABSL_CONTAINER_INTERNAL_COMPRESSED_TUPLE_H_
