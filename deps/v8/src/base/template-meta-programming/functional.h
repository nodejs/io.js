// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_FUNCTIONAL_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_FUNCTIONAL_H_

#include "src/base/template-meta-programming/list.h"

namespace v8::base::tmp {

template <typename T>
struct lazy_false : std::false_type {};
template <typename T>
struct lazy_true : std::true_type {};

// call_parameters returns a list of parameter types of the given (member)
// function pointer.
template <typename>
struct call_parameters;
template <typename R, typename... Args>
struct call_parameters<R (*)(Args...)> {
  using type = list<Args...>;
};
template <typename R, typename O, typename... Args>
struct call_parameters<R (O::*)(Args...)> {
  using type = list<Args...>;
};
template <typename T>
using call_parameters_t = typename call_parameters<T>::type;

}  // namespace v8::base::tmp

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_FUNCTIONAL_H_
