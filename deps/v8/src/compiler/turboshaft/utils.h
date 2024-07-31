// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_UTILS_H_
#define V8_COMPILER_TURBOSHAFT_UTILS_H_

#include <iostream>
#include <limits>
#include <tuple>

#include "src/base/logging.h"
#include "src/base/macros.h"

namespace v8::internal::compiler::turboshaft {

template <class... Ts>
struct any_of : std::tuple<const Ts&...> {
  explicit any_of(const Ts&... args) : std::tuple<const Ts&...>(args...) {}

  template <class T, size_t... indices>
  bool Contains(const T& value, std::index_sequence<indices...>) {
    return ((value == std::get<indices>(*this)) || ...);
  }

  template <size_t... indices>
  std::ostream& PrintTo(std::ostream& os, std::index_sequence<indices...>) {
    bool first = true;
    os << "any_of(";
    (((first ? (first = false, os) : os << ", "),
      os << base::PrintCheckOperand(std::get<indices>(*this))),
     ...);
    return os << ")";
  }
};
template <class... Args>
any_of(const Args&...) -> any_of<Args...>;

template <class T, class... Ts>
bool operator==(const T& value, any_of<Ts...> options) {
  return options.Contains(value, std::index_sequence_for<Ts...>{});
}

template <class... Ts>
std::ostream& operator<<(std::ostream& os, any_of<Ts...> any) {
  return any.PrintTo(os, std::index_sequence_for<Ts...>{});
}

template <class... Ts>
struct all_of : std::tuple<const Ts&...> {
  explicit all_of(const Ts&... args) : std::tuple<const Ts&...>(args...) {}

  template <class T, size_t... indices>
  bool AllEqualTo(const T& value, std::index_sequence<indices...>) {
    return ((value == std::get<indices>(*this)) && ...);
  }

  template <size_t... indices>
  std::ostream& PrintTo(std::ostream& os, std::index_sequence<indices...>) {
    bool first = true;
    os << "all_of(";
    (((first ? (first = false, os) : os << ", "),
      os << base::PrintCheckOperand(std::get<indices>(*this))),
     ...);
    return os << ")";
  }
};
template <class... Args>
all_of(const Args&...) -> all_of<Args...>;

template <class T, class... Ts>
bool operator==(all_of<Ts...> values, const T& target) {
  return values.AllEqualTo(target, std::index_sequence_for<Ts...>{});
}

template <class... Ts>
std::ostream& operator<<(std::ostream& os, all_of<Ts...> all) {
  return all.PrintTo(os, std::index_sequence_for<Ts...>{});
}

#ifdef DEBUG
V8_EXPORT_PRIVATE bool ShouldSkipOptimizationStep();
#else
V8_EXPORT_PRIVATE inline bool ShouldSkipOptimizationStep() { return false; }
#endif

// Set `*ptr` to `new_value` while the scope is active, reset to the previous
// value upon destruction.
template <class T>
class ScopedModification {
 public:
  ScopedModification(T* ptr, T new_value)
      : ptr_(ptr), old_value_(std::move(*ptr)) {
    *ptr = std::move(new_value);
  }

  ~ScopedModification() { *ptr_ = std::move(old_value_); }

  const T& old_value() const { return old_value_; }

 private:
  T* ptr_;
  T old_value_;
};

// The `multi`-switch mechanism helps to switch on multiple values at the same
// time. Example:
//
//   switch (multi(change.from, change.to)) {
//     case multi(Word32(), Float32()): ...
//     case multi(Word32(), Float64()): ...
//     case multi(Word64(), Float32()): ...
//     case multi(Word64(), Float64()): ...
//     ...
//   }
//
// This works for an arbitrary number of dimensions and arbitrary types as long
// as they can be encoded into an integral value and their combination fits into
// a uint64_t. For types to be used, they need to provide a specialization of
// MultiSwitch<T> with this signature:
//
//   template<>
//   struct MultiSwitch<T> {
//     static constexpr uint64_t max_value = ...
//     static constexpr uint64_t encode(T value) { ... }
//   };
//
// For `max_value` choose a value that is larger than all encoded values. Choose
// this as small as possible to make jump tables more dense. If a type's value
// count is somewhat close to a multiple of two, consider using this, as this
// might lead to slightly faster encoding. The encoding follows this formula:
//
//   multi(v1, v2, v3) =
//     let t1 = MultiSwitch<T3>::encode(v3) in
//     let t2 = (t1 * MultiSwitch<T2>::max_value)
//              + MultiSwitch<T2>::encode(v2) in
//     (t2 * MultiSwitch<T1>::max_value) + MultiSwitch<T1>::encode(v1)
//
// For integral types (like enums), use
//
//   DEFINE_MULTI_SWITCH_INTEGRAL(MyType, MaxValue)
//
template <typename T, typename Enable = void>
struct MultiSwitch;

template <typename T, uint64_t MaxValue>
struct MultiSwitchIntegral {
  static constexpr uint64_t max_value = MaxValue;
  static constexpr uint64_t encode(T value) {
    const uint64_t v = static_cast<uint64_t>(value);
    DCHECK_LT(v, max_value);
    return v;
  }
};

#define DEFINE_MULTI_SWITCH_INTEGRAL(name, max_value) \
  template <>                                         \
  struct MultiSwitch<name> : MultiSwitchIntegral<name, max_value> {};

namespace detail {
template <typename T>
constexpr uint64_t multi_encode(const T& value) {
  return MultiSwitch<T>::encode(value);
}

template <typename Head, typename Next, typename... Rest>
constexpr uint64_t multi_encode(const Head& head, const Next& next,
                                const Rest&... rest) {
  uint64_t v = multi_encode(next, rest...);
  DCHECK_LT(
      v, std::numeric_limits<uint64_t>::max() / MultiSwitch<Head>::max_value);
  return (v * MultiSwitch<Head>::max_value) + MultiSwitch<Head>::encode(head);
}
}  // namespace detail

template <typename... Ts>
inline constexpr uint64_t multi(const Ts&... values) {
  return detail::multi_encode(values...);
}

DEFINE_MULTI_SWITCH_INTEGRAL(bool, 2)

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_UTILS_H_
