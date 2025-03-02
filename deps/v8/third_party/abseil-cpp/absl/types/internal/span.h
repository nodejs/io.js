//
// Copyright 2019 The Abseil Authors.
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
#ifndef ABSL_TYPES_INTERNAL_SPAN_H_
#define ABSL_TYPES_INTERNAL_SPAN_H_

#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>

#include "absl/algorithm/algorithm.h"
#include "absl/base/config.h"
#include "absl/base/internal/throw_delegate.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

template <typename T>
class Span;

namespace span_internal {
// Wrappers for access to container data pointers.
template <typename C>
constexpr auto GetDataImpl(C& c, char) noexcept  // NOLINT(runtime/references)
    -> decltype(c.data()) {
  return c.data();
}

// Before C++17, std::string::data returns a const char* in all cases.
inline char* GetDataImpl(std::string& s,  // NOLINT(runtime/references)
                         int) noexcept {
  return &s[0];
}

template <typename C>
constexpr auto GetData(C& c) noexcept  // NOLINT(runtime/references)
    -> decltype(GetDataImpl(c, 0)) {
  return GetDataImpl(c, 0);
}

// Detection idioms for size() and data().
template <typename C>
using HasSize =
    std::is_integral<absl::decay_t<decltype(std::declval<C&>().size())>>;

// We want to enable conversion from vector<T*> to Span<const T* const> but
// disable conversion from vector<Derived> to Span<Base>. Here we use
// the fact that U** is convertible to Q* const* if and only if Q is the same
// type or a more cv-qualified version of U.  We also decay the result type of
// data() to avoid problems with classes which have a member function data()
// which returns a reference.
template <typename T, typename C>
using HasData =
    std::is_convertible<absl::decay_t<decltype(GetData(std::declval<C&>()))>*,
                        T* const*>;

// Extracts value type from a Container
template <typename C>
struct ElementType {
  using type = typename absl::remove_reference_t<C>::value_type;
};

template <typename T, size_t N>
struct ElementType<T (&)[N]> {
  using type = T;
};

template <typename C>
using ElementT = typename ElementType<C>::type;

template <typename T>
using EnableIfMutable =
    typename std::enable_if<!std::is_const<T>::value, int>::type;

template <template <typename> class SpanT, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool EqualImpl(SpanT<T> a, SpanT<T> b) {
  static_assert(std::is_const<T>::value, "");
  return std::equal(a.begin(), a.end(), b.begin(), b.end());
}

template <template <typename> class SpanT, typename T>
ABSL_INTERNAL_CONSTEXPR_SINCE_CXX20 bool LessThanImpl(SpanT<T> a, SpanT<T> b) {
  // We can't use value_type since that is remove_cv_t<T>, so we go the long way
  // around.
  static_assert(std::is_const<T>::value, "");
  return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end());
}

template <typename From, typename To>
using EnableIfConvertibleTo =
    typename std::enable_if<std::is_convertible<From, To>::value>::type;

// IsView is true for types where the return type of .data() is the same for
// mutable and const instances. This isn't foolproof, but it's only used to
// enable a compiler warning.
template <typename T, typename = void, typename = void>
struct IsView {
  static constexpr bool value = false;
};

template <typename T>
struct IsView<
    T, absl::void_t<decltype(span_internal::GetData(std::declval<const T&>()))>,
    absl::void_t<decltype(span_internal::GetData(std::declval<T&>()))>> {
 private:
  using Container = std::remove_const_t<T>;
  using ConstData =
      decltype(span_internal::GetData(std::declval<const Container&>()));
  using MutData = decltype(span_internal::GetData(std::declval<Container&>()));
 public:
  static constexpr bool value = std::is_same<ConstData, MutData>::value;
};

// These enablers result in 'int' so they can be used as typenames or defaults
// in template parameters lists.
template <typename T>
using EnableIfIsView = std::enable_if_t<IsView<T>::value, int>;

template <typename T>
using EnableIfNotIsView = std::enable_if_t<!IsView<T>::value, int>;

}  // namespace span_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TYPES_INTERNAL_SPAN_H_
