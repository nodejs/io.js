// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_

#include <type_traits>

namespace v8::base::tmp {

template <typename...>
struct list {};

namespace detail {

template <typename List>
struct length_impl;
template <typename... Ts>
struct length_impl<list<Ts...>> : std::integral_constant<int, sizeof...(Ts)> {};

template <typename List, int Index>
struct element_impl;
template <typename Head, typename... Tail>
struct element_impl<list<Head, Tail...>, 0> {
  using type = Head;
};
template <typename Head, typename... Tail, int Index>
struct element_impl<list<Head, Tail...>, Index>
    : element_impl<list<Tail...>, Index - 1> {};

}  // namespace detail

// length<List>::value is the length of the {List}.
template <typename List>
struct length : detail::length_impl<List> {};
template <typename List>
constexpr int length_v = length<List>::value;

// element<List, Index>::type is the {List}'s element at {Index}.
template <typename List, int Index>
struct element : detail::element_impl<List, Index> {};
template <typename List, int Index>
using element_t = typename element<List, Index>::type;

}  // namespace v8::base::tmp

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_
