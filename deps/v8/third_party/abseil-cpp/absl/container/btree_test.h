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

#ifndef ABSL_CONTAINER_BTREE_TEST_H_
#define ABSL_CONTAINER_BTREE_TEST_H_

#include <algorithm>
#include <cassert>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/cord.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

// Like remove_const but propagates the removal through std::pair.
template <typename T>
struct remove_pair_const {
  using type = typename std::remove_const<T>::type;
};
template <typename T, typename U>
struct remove_pair_const<std::pair<T, U> > {
  using type = std::pair<typename remove_pair_const<T>::type,
                         typename remove_pair_const<U>::type>;
};

// Utility class to provide an accessor for a key given a value. The default
// behavior is to treat the value as a pair and return the first element.
template <typename K, typename V>
struct KeyOfValue {
  struct type {
    const K& operator()(const V& p) const { return p.first; }
  };
};

// Partial specialization of KeyOfValue class for when the key and value are
// the same type such as in set<> and btree_set<>.
template <typename K>
struct KeyOfValue<K, K> {
  struct type {
    const K& operator()(const K& k) const { return k; }
  };
};

inline char* GenerateDigits(char buf[16], unsigned val, unsigned maxval) {
  assert(val <= maxval);
  constexpr unsigned kBase = 64;  // avoid integer division.
  unsigned p = 15;
  buf[p--] = 0;
  while (maxval > 0) {
    buf[p--] = ' ' + (val % kBase);
    val /= kBase;
    maxval /= kBase;
  }
  return buf + p + 1;
}

template <typename K>
struct Generator {
  int maxval;
  explicit Generator(int m) : maxval(m) {}
  K operator()(int i) const {
    assert(i <= maxval);
    return K(i);
  }
};

template <>
struct Generator<absl::Time> {
  int maxval;
  explicit Generator(int m) : maxval(m) {}
  absl::Time operator()(int i) const { return absl::FromUnixMillis(i); }
};

template <>
struct Generator<std::string> {
  int maxval;
  explicit Generator(int m) : maxval(m) {}
  std::string operator()(int i) const {
    char buf[16];
    return GenerateDigits(buf, i, maxval);
  }
};

template <>
struct Generator<Cord> {
  int maxval;
  explicit Generator(int m) : maxval(m) {}
  Cord operator()(int i) const {
    char buf[16];
    return Cord(GenerateDigits(buf, i, maxval));
  }
};

template <typename T, typename U>
struct Generator<std::pair<T, U> > {
  Generator<typename remove_pair_const<T>::type> tgen;
  Generator<typename remove_pair_const<U>::type> ugen;

  explicit Generator(int m) : tgen(m), ugen(m) {}
  std::pair<T, U> operator()(int i) const {
    return std::make_pair(tgen(i), ugen(i));
  }
};

// Generate n values for our tests and benchmarks. Value range is [0, maxval].
inline std::vector<int> GenerateNumbersWithSeed(int n, int maxval, int seed) {
  // NOTE: Some tests rely on generated numbers not changing between test runs.
  // We use std::minstd_rand0 because it is well-defined, but don't use
  // std::uniform_int_distribution because platforms use different algorithms.
  std::minstd_rand0 rng(seed);

  std::vector<int> values;
  absl::flat_hash_set<int> unique_values;
  if (values.size() < n) {
    for (int i = values.size(); i < n; i++) {
      int value;
      do {
        value = static_cast<int>(rng()) % (maxval + 1);
      } while (!unique_values.insert(value).second);

      values.push_back(value);
    }
  }
  return values;
}

// Generates n values in the range [0, maxval].
template <typename V>
std::vector<V> GenerateValuesWithSeed(int n, int maxval, int seed) {
  const std::vector<int> nums = GenerateNumbersWithSeed(n, maxval, seed);
  Generator<V> gen(maxval);
  std::vector<V> vec;

  vec.reserve(n);
  for (int i = 0; i < n; i++) {
    vec.push_back(gen(nums[i]));
  }

  return vec;
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_BTREE_TEST_H_
