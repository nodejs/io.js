// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_
#define V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_

#include <algorithm>

#include "src/base/compiler-specific.h"
#include "src/base/logging.h"

namespace v8::base::tmp {

#ifdef HAS_CPP_CLASS_TYPES_AS_TEMPLATE_ARGS

#ifdef __cpp_lib_to_array
using std::to_array;
#else
namespace detail {
template <typename T, size_t N, size_t... I>
constexpr std::array<std::remove_cv_t<T>, N> to_array_impl(
    T (&a)[N], std::index_sequence<I...>) {
  return {{a[I]...}};
}
}  // namespace detail
template <typename T, size_t N>
constexpr std::array<std::remove_cv_t<T>, N> to_array(T (&a)[N]) {
  return detail::to_array_impl(a, std::make_index_sequence<N>{});
}
#endif

// This class provides a way to pass compile time string literals to templates
// using extended non-type template parameters.
template <size_t N>
class StringLiteral {
 public:
  constexpr StringLiteral(const char (&s)[N])  // NOLINT(runtime/explicit)
      : data_(to_array(s)) {
    // We assume '\0' terminated strings.
    DCHECK_EQ(data_[N - 1], '\0');
  }

  size_t size() const {
    DCHECK_EQ(data_[N - 1], '\0');
    // `size` does not include the terminating '\0'.
    return N - 1;
  }

  const char* c_str() const { return data_.data(); }

  // `data_` cannot be private to satisify requirements of a structural type.
  const std::array<char, N> data_;
};

// Deduction guide for `StringLiteral`.
template <size_t N>
StringLiteral(const char (&)[N]) -> StringLiteral<N>;

#endif

}  // namespace v8::base::tmp

#endif  // V8_BASE_TEMPLATE_META_PROGRAMMING_STRING_LITERAL_H_
