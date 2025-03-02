// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_FAST_HASH_H_
#define V8_COMPILER_TURBOSHAFT_FAST_HASH_H_

#include <tuple>

#include "src/base/hashing.h"
#include "src/base/vector.h"

namespace v8::internal::compiler::turboshaft {

// fast_hash_combine() / fast_hash_value() produce a bad but very fast to
// compute hash, intended for hash-tables and only usable for data that is
// sufficiently random already and has high variance in their low bits.

V8_INLINE size_t fast_hash_combine() { return 0u; }
V8_INLINE size_t fast_hash_combine(size_t acc) { return acc; }
V8_INLINE size_t fast_hash_combine(size_t acc, size_t value) {
  return 17 * acc + value;
}
template <typename T, typename... Ts>
V8_INLINE size_t fast_hash_combine(T const& v, Ts const&... vs);

template <class T>
struct fast_hash {
  size_t operator()(const T& v) const {
    if constexpr (std::is_enum<T>::value) {
      return static_cast<size_t>(v);
    } else {
      return base::hash<T>()(v);
    }
  }
};

template <typename T1, typename T2>
struct fast_hash<std::pair<T1, T2>> {
  size_t operator()(const std::pair<T1, T2>& v) const {
    return fast_hash_combine(v.first, v.second);
  }
};

template <class... Ts>
struct fast_hash<std::tuple<Ts...>> {
  size_t operator()(const std::tuple<Ts...>& v) const {
    return impl(v, std::make_index_sequence<sizeof...(Ts)>());
  }

  template <size_t... I>
  V8_INLINE size_t impl(std::tuple<Ts...> const& v,
                        std::index_sequence<I...>) const {
    return fast_hash_combine(std::get<I>(v)...);
  }
};

template <typename T, typename... Ts>
V8_INLINE size_t fast_hash_combine(T const& v, Ts const&... vs) {
  return fast_hash_combine(fast_hash_combine(vs...), fast_hash<T>()(v));
}

template <typename Iterator>
V8_INLINE size_t fast_hash_range(Iterator first, Iterator last) {
  size_t acc = 0;
  for (; first != last; ++first) {
    acc = fast_hash_combine(acc, *first);
  }
  return acc;
}

template <typename T>
struct fast_hash<base::Vector<T>> {
  V8_INLINE size_t operator()(base::Vector<T> v) const {
    return fast_hash_range(v.begin(), v.end());
  }
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_FAST_HASH_H_
