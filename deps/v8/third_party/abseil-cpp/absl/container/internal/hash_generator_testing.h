// Copyright 2018 The Abseil Authors.
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
// Generates random values for testing. Specialized only for the few types we
// care about.

#ifndef ABSL_CONTAINER_INTERNAL_HASH_GENERATOR_TESTING_H_
#define ABSL_CONTAINER_INTERNAL_HASH_GENERATOR_TESTING_H_

#include <stdint.h>

#include <algorithm>
#include <cassert>
#include <iosfwd>
#include <random>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/container/internal/hash_policy_testing.h"
#include "absl/memory/memory.h"
#include "absl/meta/type_traits.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace hash_internal {
namespace generator_internal {

template <class Container, class = void>
struct IsMap : std::false_type {};

template <class Map>
struct IsMap<Map, absl::void_t<typename Map::mapped_type>> : std::true_type {};

}  // namespace generator_internal

std::mt19937_64* GetSharedRng();

enum Enum {
  kEnumEmpty,
  kEnumDeleted,
};

enum class EnumClass : uint64_t {
  kEmpty,
  kDeleted,
};

inline std::ostream& operator<<(std::ostream& o, const EnumClass& ec) {
  return o << static_cast<uint64_t>(ec);
}

template <class T, class E = void>
struct Generator;

template <class T>
struct Generator<T, typename std::enable_if<std::is_integral<T>::value>::type> {
  T operator()() const {
    std::uniform_int_distribution<T> dist;
    return dist(*GetSharedRng());
  }
};

template <>
struct Generator<Enum> {
  Enum operator()() const {
    std::uniform_int_distribution<typename std::underlying_type<Enum>::type>
        dist;
    while (true) {
      auto variate = dist(*GetSharedRng());
      if (variate != kEnumEmpty && variate != kEnumDeleted)
        return static_cast<Enum>(variate);
    }
  }
};

template <>
struct Generator<EnumClass> {
  EnumClass operator()() const {
    std::uniform_int_distribution<
        typename std::underlying_type<EnumClass>::type>
        dist;
    while (true) {
      EnumClass variate = static_cast<EnumClass>(dist(*GetSharedRng()));
      if (variate != EnumClass::kEmpty && variate != EnumClass::kDeleted)
        return static_cast<EnumClass>(variate);
    }
  }
};

template <>
struct Generator<std::string> {
  std::string operator()() const;
};

template <>
struct Generator<absl::string_view> {
  absl::string_view operator()() const;
};

template <>
struct Generator<NonStandardLayout> {
  NonStandardLayout operator()() const {
    return NonStandardLayout(Generator<std::string>()());
  }
};

template <class K, class V>
struct Generator<std::pair<K, V>> {
  std::pair<K, V> operator()() const {
    return std::pair<K, V>(Generator<typename std::decay<K>::type>()(),
                           Generator<typename std::decay<V>::type>()());
  }
};

template <class... Ts>
struct Generator<std::tuple<Ts...>> {
  std::tuple<Ts...> operator()() const {
    return std::tuple<Ts...>(Generator<typename std::decay<Ts>::type>()()...);
  }
};

template <class T>
struct Generator<std::unique_ptr<T>> {
  std::unique_ptr<T> operator()() const {
    return absl::make_unique<T>(Generator<T>()());
  }
};

template <class U>
struct Generator<U, absl::void_t<decltype(std::declval<U&>().key()),
                                decltype(std::declval<U&>().value())>>
    : Generator<std::pair<
          typename std::decay<decltype(std::declval<U&>().key())>::type,
          typename std::decay<decltype(std::declval<U&>().value())>::type>> {};

template <class Container>
using GeneratedType = decltype(
    std::declval<const Generator<
        typename std::conditional<generator_internal::IsMap<Container>::value,
                                  typename Container::value_type,
                                  typename Container::key_type>::type>&>()());

// Naive wrapper that performs a linear search of previous values.
// Beware this is O(SQR), which is reasonable for smaller kMaxValues.
template <class T, size_t kMaxValues = 64, class E = void>
struct UniqueGenerator {
  Generator<T, E> gen;
  std::vector<T> values;

  T operator()() {
    assert(values.size() < kMaxValues);
    for (;;) {
      T value = gen();
      if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
        return value;
      }
    }
  }
};

}  // namespace hash_internal
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_HASH_GENERATOR_TESTING_H_
