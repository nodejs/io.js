// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_ALGORITHM_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_ALGORITHM_H_

#include <type_traits>

#include "src/base/template-meta-programming/list.h"

namespace v8::base::tmp {

namespace detail {

template <template <typename> typename F, typename List>
struct map_impl;
template <template <typename> typename F, typename... Ts>
struct map_impl<F, list<Ts...>> {
  using type = list<typename F<Ts>::type...>;
};

template <typename List, typename T, int NotFoundIndex, int I>
struct find_first_impl;
template <typename Head, typename... Tail, typename T, int NotFoundIndex, int I>
struct find_first_impl<list<Head, Tail...>, T, NotFoundIndex, I>
    : find_first_impl<list<Tail...>, T, NotFoundIndex, I + 1> {};
template <typename... Tail, typename T, int NotFoundIndex, int I>
struct find_first_impl<list<T, Tail...>, T, NotFoundIndex, I>
    : std::integral_constant<int, I> {};
template <typename T, int NotFoundIndex, int I>
struct find_first_impl<list<>, T, NotFoundIndex, I>
    : std::integral_constant<int, NotFoundIndex> {};

template <typename List, template <typename, typename> typename Cmp>
struct all_equal_impl;
template <typename Head, typename... Tail,
          template <typename, typename> typename Cmp>
struct all_equal_impl<list<Head, Tail...>, Cmp>
    : std::bool_constant<(Cmp<Head, Tail>::value && ...)> {};

}  // namespace detail

// map<F, List>::type is a new list after applying {F} (in the form of
// F<E>::type) to all elements (E) in {List}.
template <template <typename> typename F, typename List>
struct map : detail::map_impl<F, List> {};
template <template <typename> typename F, typename List>
using map_t = typename map<F, List>::type;

// find_first<List, T, NotFoundIndex = -1>::value is the index of the first
// occurrence of {T} in {List} or otherwise {NotFoundIndex}.
template <typename List, typename T, int NotFoundIndex = -1>
struct find_first : detail::find_first_impl<List, T, NotFoundIndex, 0> {};
template <typename List, typename T, int NotFoundIndex = -1>
constexpr int find_first_v = find_first<List, T, NotFoundIndex>::value;

// contains<List, T>::value is true iff {List} contains {T}.
template <typename List, typename T>
struct contains : std::bool_constant<find_first<List, T, -1>::value != -1> {};
template <typename List, typename T>
constexpr int contains_v = contains<List, T>::value;

// all_equal<List, Cmp = std::is_same>::value is true iff all values in {List}
// are equal with respect to {Cmp}.
template <typename List,
          template <typename, typename> typename Cmp = std::is_same>
struct all_equal : detail::all_equal_impl<List, Cmp> {};
template <typename List,
          template <typename, typename> typename Cmp = std::is_same>
constexpr bool all_equal_v = all_equal<List, Cmp>::value;

}  // namespace v8::base::tmp

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_ALGORITHM_H_
