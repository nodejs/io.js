// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_COMMON_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_COMMON_H_

#include <type_traits>

#define TYPENAME1     \
  template <typename> \
  typename

namespace v8::base::tmp {

template <typename T, typename U>
struct equals : std::bool_constant<false> {};
template <typename T>
struct equals<T, T> : std::bool_constant<true> {};

template <TYPENAME1 T, TYPENAME1 U>
struct equals1 : std::bool_constant<false> {};
template <TYPENAME1 T>
struct equals1<T, T> : std::bool_constant<true> {};

template <TYPENAME1 T, typename U>
struct instantiate {
  using type = T<U>;
};

template <typename I, TYPENAME1 T>
struct is_instantiation_of : std::bool_constant<false> {};
template <typename U, TYPENAME1 T>
struct is_instantiation_of<T<U>, T> : std::bool_constant<true> {};

}  // namespace v8::base::tmp

#undef TYPENAME1

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_COMMON_H_
