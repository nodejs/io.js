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
// Utilities to help tests verify that hash tables properly handle stateful
// allocators and hash functions.

#ifndef ABSL_CONTAINER_INTERNAL_HASH_POLICY_TESTING_H_
#define ABSL_CONTAINER_INTERNAL_HASH_POLICY_TESTING_H_

#include <cstdlib>
#include <limits>
#include <memory>
#include <ostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/hash/hash.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace hash_testing_internal {

template <class Derived>
struct WithId {
  WithId() : id_(next_id<Derived>()) {}
  WithId(const WithId& that) : id_(that.id_) {}
  WithId(WithId&& that) : id_(that.id_) { that.id_ = 0; }
  WithId& operator=(const WithId& that) {
    id_ = that.id_;
    return *this;
  }
  WithId& operator=(WithId&& that) {
    id_ = that.id_;
    that.id_ = 0;
    return *this;
  }

  size_t id() const { return id_; }

  friend bool operator==(const WithId& a, const WithId& b) {
    return a.id_ == b.id_;
  }
  friend bool operator!=(const WithId& a, const WithId& b) { return !(a == b); }

 protected:
  explicit WithId(size_t id) : id_(id) {}

 private:
  size_t id_;

  template <class T>
  static size_t next_id() {
    // 0 is reserved for moved from state.
    static size_t gId = 1;
    return gId++;
  }
};

}  // namespace hash_testing_internal

struct NonStandardLayout {
  NonStandardLayout() {}
  explicit NonStandardLayout(std::string s) : value(std::move(s)) {}
  virtual ~NonStandardLayout() {}

  friend bool operator==(const NonStandardLayout& a,
                         const NonStandardLayout& b) {
    return a.value == b.value;
  }
  friend bool operator!=(const NonStandardLayout& a,
                         const NonStandardLayout& b) {
    return a.value != b.value;
  }

  template <typename H>
  friend H AbslHashValue(H h, const NonStandardLayout& v) {
    return H::combine(std::move(h), v.value);
  }

  std::string value;
};

struct StatefulTestingHash
    : absl::container_internal::hash_testing_internal::WithId<
          StatefulTestingHash> {
  template <class T>
  size_t operator()(const T& t) const {
    return absl::Hash<T>{}(t);
  }
};

struct StatefulTestingEqual
    : absl::container_internal::hash_testing_internal::WithId<
          StatefulTestingEqual> {
  template <class T, class U>
  bool operator()(const T& t, const U& u) const {
    return t == u;
  }
};

// It is expected that Alloc() == Alloc() for all allocators so we cannot use
// WithId base. We need to explicitly assign ids.
template <class T = int>
struct Alloc : std::allocator<T> {
  using propagate_on_container_swap = std::true_type;

  // Using old paradigm for this to ensure compatibility.
  explicit Alloc(size_t id = 0) : id_(id) {}

  Alloc(const Alloc&) = default;
  Alloc& operator=(const Alloc&) = default;

  template <class U>
  Alloc(const Alloc<U>& that) : std::allocator<T>(that), id_(that.id()) {}

  template <class U>
  struct rebind {
    using other = Alloc<U>;
  };

  size_t id() const { return id_; }

  friend bool operator==(const Alloc& a, const Alloc& b) {
    return a.id_ == b.id_;
  }
  friend bool operator!=(const Alloc& a, const Alloc& b) { return !(a == b); }

 private:
  size_t id_ = (std::numeric_limits<size_t>::max)();
};

template <class Map>
auto items(const Map& m) -> std::vector<
    std::pair<typename Map::key_type, typename Map::mapped_type>> {
  using std::get;
  std::vector<std::pair<typename Map::key_type, typename Map::mapped_type>> res;
  res.reserve(m.size());
  for (const auto& v : m) res.emplace_back(get<0>(v), get<1>(v));
  return res;
}

template <class Set>
auto keys(const Set& s)
    -> std::vector<typename std::decay<typename Set::key_type>::type> {
  std::vector<typename std::decay<typename Set::key_type>::type> res;
  res.reserve(s.size());
  for (const auto& v : s) res.emplace_back(v);
  return res;
}

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

// ABSL_UNORDERED_SUPPORTS_ALLOC_CTORS is false for glibcxx versions
// where the unordered containers are missing certain constructors that
// take allocator arguments. This test is defined ad-hoc for the platforms
// we care about (notably Crosstool 17) because libstdcxx's useless
// versioning scheme precludes a more principled solution.
// From GCC-4.9 Changelog: (src: https://gcc.gnu.org/gcc-4.9/changes.html)
// "the unordered associative containers in <unordered_map> and <unordered_set>
// meet the allocator-aware container requirements;"
#if defined(__GLIBCXX__) && __GLIBCXX__ <= 20140425
#define ABSL_UNORDERED_SUPPORTS_ALLOC_CTORS 0
#else
#define ABSL_UNORDERED_SUPPORTS_ALLOC_CTORS 1
#endif

#endif  // ABSL_CONTAINER_INTERNAL_HASH_POLICY_TESTING_H_
