// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_

#include <cstddef>
#include <limits>
#include <type_traits>

#include "src/base/template-meta-programming/common.h"

#define TYPENAME1     \
  template <typename> \
  typename

namespace v8::base::tmp {

// list is a classical type list.
template <typename...>
struct list {};

// list1 is a version of list that holds 1-ary template types.
template <TYPENAME1...>
struct list1 {};

namespace detail {

template <typename List>
struct length_impl;
template <typename... Ts>
struct length_impl<list<Ts...>>
    : std::integral_constant<size_t, sizeof...(Ts)> {};
template <TYPENAME1... Ts>
struct length_impl<list1<Ts...>>
    : std::integral_constant<size_t, sizeof...(Ts)> {};

template <typename List, size_t Index>
struct element_impl;
template <typename Head, typename... Tail>
struct element_impl<list<Head, Tail...>, 0> {
  using type = Head;
};
template <typename Head, typename... Tail, size_t Index>
struct element_impl<list<Head, Tail...>, Index>
    : element_impl<list<Tail...>, Index - 1> {};

template <template <typename> typename F, typename List>
struct map_impl;
template <template <typename> typename F, typename... Ts>
struct map_impl<F, list<Ts...>> {
  using type = list<typename F<Ts>::type...>;
};

template <size_t I, size_t Otherwise, typename T, typename List>
struct index_of_impl;
template <size_t I, size_t Otherwise, typename T, typename Head,
          typename... Tail>
struct index_of_impl<I, Otherwise, T, list<Head, Tail...>>
    : public index_of_impl<I + 1, Otherwise, T, list<Tail...>> {};
template <size_t I, size_t Otherwise, typename T, typename... Tail>
struct index_of_impl<I, Otherwise, T, list<T, Tail...>>
    : public std::integral_constant<size_t, I> {};
template <size_t I, size_t Otherwise, typename T>
struct index_of_impl<I, Otherwise, T, list<>>
    : public std::integral_constant<size_t, Otherwise> {};

template <size_t I, size_t Otherwise, TYPENAME1 T, typename List1>
struct index_of1_impl;
template <size_t I, size_t Otherwise, TYPENAME1 T, TYPENAME1 Head,
          TYPENAME1... Tail>
struct index_of1_impl<I, Otherwise, T, list1<Head, Tail...>>
    : public index_of1_impl<I + 1, Otherwise, T, list1<Tail...>> {};
template <size_t I, size_t Otherwise, TYPENAME1 T, TYPENAME1... Tail>
struct index_of1_impl<I, Otherwise, T, list1<T, Tail...>>
    : public std::integral_constant<size_t, I> {};
template <size_t I, size_t Otherwise, TYPENAME1 T>
struct index_of1_impl<I, Otherwise, T, list1<>>
    : public std::integral_constant<size_t, Otherwise> {};

template <typename List, typename Element>
struct contains_impl;
template <typename... Ts, typename Element>
struct contains_impl<list<Ts...>, Element>
    : std::bool_constant<(equals<Ts, Element>::value || ...)> {};

template <typename List, TYPENAME1 Element>
struct contains_impl1;
template <TYPENAME1... Ts, TYPENAME1 Element>
struct contains_impl1<list1<Ts...>, Element>
    : std::bool_constant<(equals1<Ts, Element>::value || ...)> {};

template <typename List, template <typename, typename> typename Cmp>
struct all_equal_impl;
template <typename Head, typename... Tail,
          template <typename, typename> typename Cmp>
struct all_equal_impl<list<Head, Tail...>, Cmp>
    : std::bool_constant<(Cmp<Head, Tail>::value && ...)> {};

template <size_t I, typename T, typename Before, typename After>
struct insert_at_impl;
template <size_t I, typename T, typename... Before, typename Head,
          typename... Tail>
struct insert_at_impl<I, T, list<Before...>, list<Head, Tail...>>
    : insert_at_impl<I - 1, T, list<Before..., Head>, list<Tail...>> {};
template <size_t I, typename T, typename... Before>
struct insert_at_impl<I, T, list<Before...>, list<>> {
  using type = list<Before..., T>;
};
template <typename T, typename... Before, typename Head, typename... Tail>
struct insert_at_impl<0, T, list<Before...>, list<Head, Tail...>> {
  using type = list<Before..., T, Head, Tail...>;
};

template <size_t I, TYPENAME1 T, typename Before, typename After>
struct insert_at1_impl;
template <size_t I, TYPENAME1 T, TYPENAME1... Before, TYPENAME1 Head,
          TYPENAME1... Tail>
struct insert_at1_impl<I, T, list1<Before...>, list1<Head, Tail...>>
    : insert_at1_impl<I - 1, T, list1<Before..., Head>, list1<Tail...>> {};
template <size_t I, TYPENAME1 T, TYPENAME1... Before>
struct insert_at1_impl<I, T, list1<Before...>, list<>> {
  using type = list1<Before..., T>;
};
template <TYPENAME1 T, TYPENAME1... Before, TYPENAME1 Head, TYPENAME1... Tail>
struct insert_at1_impl<0, T, list1<Before...>, list1<Head, Tail...>> {
  using type = list1<Before..., T, Head, Tail...>;
};

template <template <typename, typename> typename F, typename T, typename List>
struct fold_right_impl;
template <template <typename, typename> typename F, typename T, typename Head,
          typename... Tail>
struct fold_right_impl<F, T, list<Head, Tail...>> {
  using type =
      F<Head, typename fold_right_impl<F, T, list<Tail...>>::type>::type;
};
template <template <typename, typename> typename F, typename T>
struct fold_right_impl<F, T, list<>> {
  using type = T;
};

template <template <TYPENAME1, typename> typename F, typename T, typename List1>
struct fold_right1_impl;
template <template <TYPENAME1, typename> typename F, typename T, TYPENAME1 Head,
          TYPENAME1... Tail>
struct fold_right1_impl<F, T, list1<Head, Tail...>> {
  using type =
      F<Head, typename fold_right1_impl<F, T, list1<Tail...>>::type>::type;
};
template <template <TYPENAME1, typename> typename F, typename T>
struct fold_right1_impl<F, T, list1<>> {
  using type = T;
};

}  // namespace detail

// length<List>::value is the length of the {List}.
template <typename List>
struct length : detail::length_impl<List> {};
template <typename List>
constexpr size_t length_v = length<List>::value;
template <typename List1>
struct length1 : detail::length_impl<List1> {};
template <typename List1>
constexpr size_t length1_v = length1<List1>::value;

// element<List, Index>::type is the {List}'s element at {Index}.
template <typename List, size_t Index>
struct element : detail::element_impl<List, Index> {};
template <typename List, size_t Index>
using element_t = typename element<List, Index>::type;

// map<F, List>::type is a new list after applying {F} (in the form of
// F<E>::type) to all elements (E) in {List}.
template <template <typename> typename F, typename List>
struct map : detail::map_impl<F, List> {};
template <template <typename> typename F, typename List>
using map_t = typename map<F, List>::type;

// index_of<List, T, Otherwise>::value is the first index of {T} in {List} or
// {Otherwise} if the list doesn't contain {T}.
template <typename List, typename T,
          size_t Otherwise = std::numeric_limits<size_t>::max()>
struct index_of : detail::index_of_impl<0, Otherwise, T, List> {};
template <typename List, typename T,
          size_t Otherwise = std::numeric_limits<size_t>::max()>
constexpr size_t index_of_v = index_of<List, T, Otherwise>::value;
template <typename List1, TYPENAME1 T,
          size_t Otherwise = std::numeric_limits<size_t>::max()>
struct index_of1 : detail::index_of1_impl<0, Otherwise, T, List1> {};
template <typename List1, TYPENAME1 T,
          size_t Otherwise = std::numeric_limits<size_t>::max()>
constexpr size_t index_of1_v = index_of1<List1, T, Otherwise>::value;

// contains<List, T>::value is true iff {List} contains {T}.
template <typename List, typename T>
struct contains : detail::contains_impl<List, T> {};
template <typename List, typename T>
constexpr bool contains_v = contains<List, T>::value;
template <typename List1, TYPENAME1 T>
struct contains1 : detail::contains_impl1<List1, T> {};
template <typename List1, TYPENAME1 T>
constexpr bool contains1_v = contains1<List1, T>::value;

// all_equal<List, Cmp = equals>::value is true iff all values in {List}
// are equal with respect to {Cmp}.
template <typename List, template <typename, typename> typename Cmp = equals>
struct all_equal : detail::all_equal_impl<List, Cmp> {};
template <typename List, template <typename, typename> typename Cmp = equals>
constexpr bool all_equal_v = all_equal<List, Cmp>::value;

// insert_at<List, I, T>::value is identical to {List}, except that {T} is
// inserted at position {I}. If {I} is larger than the length of the list, {T}
// is simply appended.
template <typename List, size_t I, typename T>
struct insert_at : public detail::insert_at_impl<I, T, list<>, List> {};
template <typename List, size_t I, typename T>
using insert_at_t = insert_at<List, I, T>::type;
template <typename List1, size_t I, TYPENAME1 T>
struct insert_at1 : public detail::insert_at1_impl<I, T, list1<>, List1> {};
template <typename List1, size_t I, TYPENAME1 T>
using insert_at1_t = insert_at1<List1, I, T>::type;

// fold_right recursively applies binary function {F} to elements of the {List}
// and the previous result, starting from the right. The initial value is {T}.
// Example, for E0, E1, ... En elements of List:
//   fold_right<F, List, T>::type
// resembles
//   F<E0, F<E1, ... F<En, T>::type ...>::type>::type.
template <template <typename, typename> typename F, typename List, typename T>
struct fold_right : public detail::fold_right_impl<F, T, List> {};
template <template <typename, typename> typename F, typename List, typename T>
using fold_right_t = fold_right<F, List, T>::type;
template <template <TYPENAME1, typename> typename F, typename List1, typename T>
struct fold_right1 : public detail::fold_right1_impl<F, T, List1> {};
template <template <TYPENAME1, typename> typename F, typename List1, typename T>
using fold_right1_t = fold_right1<F, List1, T>::type;

}  // namespace v8::base::tmp

#undef TYPENAME1

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_LIST_H_
