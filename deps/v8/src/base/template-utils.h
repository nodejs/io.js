// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_UTILS_H_
#define V8_BASE_TEMPLATE_UTILS_H_

#include <stddef.h>

#include <array>
#include <functional>
#include <iosfwd>
#include <tuple>
#include <type_traits>
#include <utility>

namespace v8 {
namespace base {

namespace detail {

template <typename Function, std::size_t... Indexes>
constexpr inline auto make_array_helper(Function f,
                                        std::index_sequence<Indexes...>)
    -> std::array<decltype(f(0)), sizeof...(Indexes)> {
  return {{f(Indexes)...}};
}

}  // namespace detail

// base::make_array: Create an array of fixed length, initialized by a function.
// The content of the array is created by calling the function with 0 .. Size-1.
// Example usage to create the array {0, 2, 4}:
//   std::array<int, 3> arr = base::make_array<3>(
//       [](std::size_t i) { return static_cast<int>(2 * i); });
// The resulting array will be constexpr if the passed function is constexpr.
template <std::size_t Size, class Function>
constexpr auto make_array(Function f) {
  return detail::make_array_helper(f, std::make_index_sequence<Size>{});
}

// base::overloaded: Create a callable which wraps a collection of other
// callables, and treats them as an overload set. A typical use case would
// be passing a collection of lambda functions to templated code which could
// call them with different argument types, e.g.
//
//   CallWithIntOrDouble(base::overloaded{
//     [&] (int val) { process_int(val); }
//     [&] (double val) { process_double(val); }
//   });
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// Helper to determine how to pass values: Pass scalars and arrays by value,
// others by const reference (even if it was a non-const ref before; this is
// disallowed by the style guide anyway).
// The default is to also remove array extends (int[5] -> int*), but this can be
// disabled by setting {remove_array_extend} to false.
template <typename T, bool remove_array_extend = true>
struct pass_value_or_ref {
  using noref_t = typename std::remove_reference<T>::type;
  using decay_t = typename std::conditional<
      std::is_array<noref_t>::value && !remove_array_extend, noref_t,
      typename std::decay<noref_t>::type>::type;
  using type = typename std::conditional<std::is_scalar<decay_t>::value ||
                                             std::is_array<decay_t>::value,
                                         decay_t, const decay_t&>::type;
};

template <typename T, typename TStream = std::ostream>
concept has_output_operator = requires(T t, TStream stream) { stream << t; };

// Turn std::tuple<A...> into std::tuple<A..., T>.
template <class Tuple, class T>
using append_tuple_type = decltype(std::tuple_cat(
    std::declval<Tuple>(), std::declval<std::tuple<T>>()));

// Turn std::tuple<A...> into std::tuple<T, A...>.
template <class T, class Tuple>
using prepend_tuple_type = decltype(std::tuple_cat(
    std::declval<std::tuple<T>>(), std::declval<Tuple>()));

namespace detail {

template <size_t N, typename Tuple>
constexpr bool NIsNotGreaterThanTupleSize =
    N <= std::tuple_size_v<std::decay_t<Tuple>>;

template <size_t N, typename T, size_t... Ints>
constexpr auto tuple_slice_impl(const T& tpl, std::index_sequence<Ints...>) {
  return std::tuple{std::get<N + Ints>(tpl)...};
}

template <typename Tuple, typename Function, size_t... Index>
constexpr auto tuple_for_each_impl(const Tuple& tpl, Function&& function,
                                   std::index_sequence<Index...>) {
  (function(std::get<Index>(tpl)), ...);
}

template <typename Tuple, typename Function, size_t... Index>
constexpr auto tuple_for_each_with_index_impl(const Tuple& tpl,
                                              Function&& function,
                                              std::index_sequence<Index...>) {
  (function(std::get<Index>(tpl), std::integral_constant<size_t, Index>()),
   ...);
}

template <typename Tuple, typename Function, size_t... Index>
constexpr auto tuple_map_impl(Tuple&& tpl, const Function& function,
                              std::index_sequence<Index...>) {
  return std::make_tuple(
      function(std::get<Index>(std::forward<Tuple>(tpl)))...);
}

template <typename TupleV, typename TupleU, typename Function, size_t... Index>
constexpr auto tuple_map2_impl(TupleV&& tplv, TupleU&& tplu,
                               const Function& function,
                               std::index_sequence<Index...>) {
  return std::make_tuple(
      function(std::get<Index>(tplv), std::get<Index>(tplu))...);
}

template <size_t I, typename T, typename Tuple, typename Function>
constexpr auto tuple_fold_impl(T&& initial, Tuple&& tpl, Function&& function) {
  if constexpr (I == 0) {
    return function(std::forward<T>(initial), std::get<0>(tpl));
  } else {
    return function(tuple_fold_impl<I - 1>(std::forward<T>(initial),
                                           std::forward<Tuple>(tpl), function),
                    std::get<I>(tpl));
  }
}

}  // namespace detail

// Get the first N elements from a tuple.
template <size_t N, typename Tuple>
constexpr auto tuple_head(Tuple&& tpl) {
  constexpr size_t total_size = std::tuple_size_v<std::decay_t<Tuple>>;
  static_assert(N <= total_size);
  return detail::tuple_slice_impl<0>(std::forward<Tuple>(tpl),
                                     std::make_index_sequence<N>());
}

// Drop the first N elements from a tuple.
template <size_t N, typename Tuple>
// If the user accidentally passes in an N that is larger than the tuple
// size, the unsigned subtraction will create a giant index sequence and
// crash the compiler. To avoid this and fail early, disable this function
// for invalid N.
  requires(detail::NIsNotGreaterThanTupleSize<N, Tuple>)
constexpr auto tuple_drop(Tuple&& tpl) {
  constexpr size_t total_size = std::tuple_size_v<std::decay_t<Tuple>>;
  static_assert(N <= total_size);
  return detail::tuple_slice_impl<N>(
      std::forward<Tuple>(tpl), std::make_index_sequence<total_size - N>());
}

// Calls `function(v)` for each `v` in the tuple.
template <typename Tuple, typename Function>
constexpr void tuple_for_each(Tuple&& tpl, Function&& function) {
  detail::tuple_for_each_impl(
      std::forward<Tuple>(tpl), function,
      std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>());
}

// Calls `function(v, i)` for each `v` in the tuple, with index `i`. The index
// `i` is passed as an std::integral_constant<size_t>, rather than a raw size_t,
// to allow it to be used
template <typename Tuple, typename Function>
constexpr void tuple_for_each_with_index(Tuple&& tpl, Function&& function) {
  detail::tuple_for_each_with_index_impl(
      std::forward<Tuple>(tpl), function,
      std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>());
}

// Calls `function(v)` for each `v` in the tuple and returns a new tuple with
// all the results.
template <typename Tuple, typename Function>
constexpr auto tuple_map(Tuple&& tpl, Function&& function) {
  return detail::tuple_map_impl(
      std::forward<Tuple>(tpl), function,
      std::make_index_sequence<std::tuple_size_v<std::decay_t<Tuple>>>());
}

// Calls `function(v, u)` for pairs `v<I>, u<I>` in the
// tuples and returns a new tuple with all the results.
template <typename TupleV, typename TupleU, typename Function>
constexpr auto tuple_map2(TupleV&& tplv, TupleU&& tplu, Function&& function) {
  constexpr size_t S = std::tuple_size_v<std::decay_t<TupleV>>;
  static_assert(S == std::tuple_size_v<std::decay_t<TupleU>>);
  return detail::tuple_map2_impl(std::forward<TupleV>(tplv),
                                 std::forward<TupleU>(tplu), function,
                                 std::make_index_sequence<S>());
}

// Left fold (reduce) the tuple starting with an initial value by applying
// function(...function(initial, tpl<0>)..., tpl<size-1>)
template <typename T, typename Tuple, typename Function>
constexpr auto tuple_fold(T&& initial, Tuple&& tpl, Function&& function) {
  return detail::tuple_fold_impl<std::tuple_size_v<std::decay_t<Tuple>> - 1>(
      std::forward<T>(initial), std::forward<Tuple>(tpl), function);
}

#ifdef __clang__

template <size_t N, typename... Ts>
using nth_type_t = __type_pack_element<N, Ts...>;

#else

namespace detail {
template <size_t N, typename... Ts>
struct nth_type;

template <typename T, typename... Ts>
struct nth_type<0, T, Ts...> {
  using type = T;
};

template <size_t N, typename T, typename... Ts>
struct nth_type<N, T, Ts...> : public nth_type<N - 1, Ts...> {};
}  // namespace detail

template <size_t N, typename... T>
using nth_type_t = typename detail::nth_type<N, T...>::type;

#endif

// Find SearchT in Ts. SearchT must be present at most once in Ts, and returns
// sizeof...(Ts) if not found.
template <typename SearchT, typename... Ts>
struct index_of_type;

template <typename SearchT, typename... Ts>
constexpr size_t index_of_type_v = index_of_type<SearchT, Ts...>::value;
template <typename SearchT, typename... Ts>
constexpr bool has_type_v =
    index_of_type<SearchT, Ts...>::value < sizeof...(Ts);

// Not found / empty list.
template <typename SearchT>
struct index_of_type<SearchT> : public std::integral_constant<size_t, 0> {};

// SearchT found at head of list.
template <typename SearchT, typename... Ts>
struct index_of_type<SearchT, SearchT, Ts...>
    : public std::integral_constant<size_t, 0> {
  // SearchT is not allowed to be anywhere else in the list.
  static_assert(!has_type_v<SearchT, Ts...>);
};

// Recursion, SearchT not found at head of list.
template <typename SearchT, typename T, typename... Ts>
struct index_of_type<SearchT, T, Ts...>
    : public std::integral_constant<size_t,
                                    1 + index_of_type<SearchT, Ts...>::value> {
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_TEMPLATE_UTILS_H_
