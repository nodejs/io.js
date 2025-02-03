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

#include "absl/container/flat_hash_map.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/hash_policy_testing.h"
#include "absl/container/internal/test_allocator.h"
#include "absl/container/internal/unordered_map_constructor_test.h"
#include "absl/container/internal/unordered_map_lookup_test.h"
#include "absl/container/internal/unordered_map_members_test.h"
#include "absl/container/internal/unordered_map_modifiers_test.h"
#include "absl/log/check.h"
#include "absl/meta/type_traits.h"
#include "absl/types/any.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {
using ::absl::container_internal::hash_internal::Enum;
using ::absl::container_internal::hash_internal::EnumClass;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

// Check that absl::flat_hash_map works in a global constructor.
struct BeforeMain {
  BeforeMain() {
    absl::flat_hash_map<int, int> x;
    x.insert({1, 1});
    CHECK(x.find(0) == x.end()) << "x should not contain 0";
    auto it = x.find(1);
    CHECK(it != x.end()) << "x should contain 1";
    CHECK(it->second) << "1 should map to 1";
  }
};
const BeforeMain before_main;

template <class K, class V>
using Map = flat_hash_map<K, V, StatefulTestingHash, StatefulTestingEqual,
                          Alloc<std::pair<const K, V>>>;

static_assert(!std::is_standard_layout<NonStandardLayout>(), "");

using MapTypes =
    ::testing::Types<Map<int, int>, Map<std::string, int>,
                     Map<Enum, std::string>, Map<EnumClass, int>,
                     Map<int, NonStandardLayout>, Map<NonStandardLayout, int>>;

INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashMap, ConstructorTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashMap, LookupTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashMap, MembersTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashMap, ModifiersTest, MapTypes);

using UniquePtrMapTypes = ::testing::Types<Map<int, std::unique_ptr<int>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashMap, UniquePtrModifiersTest,
                               UniquePtrMapTypes);

TEST(FlatHashMap, StandardLayout) {
  struct Int {
    explicit Int(size_t value) : value(value) {}
    Int() : value(0) { ADD_FAILURE(); }
    Int(const Int& other) : value(other.value) { ADD_FAILURE(); }
    Int(Int&&) = default;
    bool operator==(const Int& other) const { return value == other.value; }
    size_t value;
  };
  static_assert(std::is_standard_layout<Int>(), "");

  struct Hash {
    size_t operator()(const Int& obj) const { return obj.value; }
  };

  // Verify that neither the key nor the value get default-constructed or
  // copy-constructed.
  {
    flat_hash_map<Int, Int, Hash> m;
    m.try_emplace(Int(1), Int(2));
    m.try_emplace(Int(3), Int(4));
    m.erase(Int(1));
    m.rehash(2 * m.bucket_count());
  }
  {
    flat_hash_map<Int, Int, Hash> m;
    m.try_emplace(Int(1), Int(2));
    m.try_emplace(Int(3), Int(4));
    m.erase(Int(1));
    m.clear();
  }
}

TEST(FlatHashMap, Relocatability) {
  static_assert(absl::is_trivially_relocatable<int>::value, "");
  static_assert(
      absl::is_trivially_relocatable<std::pair<const int, int>>::value, "");
  static_assert(
      std::is_same<decltype(absl::container_internal::FlatHashMapPolicy<
                            int, int>::transfer<std::allocator<char>>(nullptr,
                                                                      nullptr,
                                                                      nullptr)),
                   std::true_type>::value,
      "");

    struct NonRelocatable {
      NonRelocatable() = default;
      NonRelocatable(NonRelocatable&&) {}
      NonRelocatable& operator=(NonRelocatable&&) { return *this; }
      void* self = nullptr;
    };

  EXPECT_FALSE(absl::is_trivially_relocatable<NonRelocatable>::value);
  EXPECT_TRUE(
      (std::is_same<decltype(absl::container_internal::FlatHashMapPolicy<
                            int, NonRelocatable>::
                                transfer<std::allocator<char>>(nullptr, nullptr,
                                                               nullptr)),
                   std::false_type>::value));
}

// gcc becomes unhappy if this is inside the method, so pull it out here.
struct balast {};

TEST(FlatHashMap, IteratesMsan) {
  // Because SwissTable randomizes on pointer addresses, we keep old tables
  // around to ensure we don't reuse old memory.
  std::vector<absl::flat_hash_map<int, balast>> garbage;
  for (int i = 0; i < 100; ++i) {
    absl::flat_hash_map<int, balast> t;
    for (int j = 0; j < 100; ++j) {
      t[j];
      for (const auto& p : t) EXPECT_THAT(p, Pair(_, _));
    }
    garbage.push_back(std::move(t));
  }
}

// Demonstration of the "Lazy Key" pattern.  This uses heterogeneous insert to
// avoid creating expensive key elements when the item is already present in the
// map.
struct LazyInt {
  explicit LazyInt(size_t value, int* tracker)
      : value(value), tracker(tracker) {}

  explicit operator size_t() const {
    ++*tracker;
    return value;
  }

  size_t value;
  int* tracker;
};

struct Hash {
  using is_transparent = void;
  int* tracker;
  size_t operator()(size_t obj) const {
    ++*tracker;
    return obj;
  }
  size_t operator()(const LazyInt& obj) const {
    ++*tracker;
    return obj.value;
  }
};

struct Eq {
  using is_transparent = void;
  bool operator()(size_t lhs, size_t rhs) const { return lhs == rhs; }
  bool operator()(size_t lhs, const LazyInt& rhs) const {
    return lhs == rhs.value;
  }
};

TEST(FlatHashMap, LazyKeyPattern) {
  // hashes are only guaranteed in opt mode, we use assertions to track internal
  // state that can cause extra calls to hash.
  int conversions = 0;
  int hashes = 0;
  flat_hash_map<size_t, size_t, Hash, Eq> m(0, Hash{&hashes});
  m.reserve(3);

  m[LazyInt(1, &conversions)] = 1;
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, 1)));
  EXPECT_EQ(conversions, 1);
#ifdef NDEBUG
  EXPECT_EQ(hashes, 1);
#endif

  m[LazyInt(1, &conversions)] = 2;
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, 2)));
  EXPECT_EQ(conversions, 1);
#ifdef NDEBUG
  EXPECT_EQ(hashes, 2);
#endif

  m.try_emplace(LazyInt(2, &conversions), 3);
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, 2), Pair(2, 3)));
  EXPECT_EQ(conversions, 2);
#ifdef NDEBUG
  EXPECT_EQ(hashes, 3);
#endif

  m.try_emplace(LazyInt(2, &conversions), 4);
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, 2), Pair(2, 3)));
  EXPECT_EQ(conversions, 2);
#ifdef NDEBUG
  EXPECT_EQ(hashes, 4);
#endif
}

TEST(FlatHashMap, BitfieldArgument) {
  union {
    int n : 1;
  };
  n = 0;
  flat_hash_map<int, int> m;
  m.erase(n);
  m.count(n);
  m.prefetch(n);
  m.find(n);
  m.contains(n);
  m.equal_range(n);
  m.insert_or_assign(n, n);
  m.insert_or_assign(m.end(), n, n);
  m.try_emplace(n);
  m.try_emplace(m.end(), n);
  m.at(n);
  m[n];
}

TEST(FlatHashMap, MergeExtractInsert) {
  // We can't test mutable keys, or non-copyable keys with flat_hash_map.
  // Test that the nodes have the proper API.
  absl::flat_hash_map<int, int> m = {{1, 7}, {2, 9}};
  auto node = m.extract(1);
  EXPECT_TRUE(node);
  EXPECT_EQ(node.key(), 1);
  EXPECT_EQ(node.mapped(), 7);
  EXPECT_THAT(m, UnorderedElementsAre(Pair(2, 9)));

  node.mapped() = 17;
  m.insert(std::move(node));
  EXPECT_THAT(m, UnorderedElementsAre(Pair(1, 17), Pair(2, 9)));
}

bool FirstIsEven(std::pair<const int, int> p) { return p.first % 2 == 0; }

TEST(FlatHashMap, EraseIf) {
  // Erase all elements.
  {
    flat_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, [](std::pair<const int, int>) { return true; }), 5);
    EXPECT_THAT(s, IsEmpty());
  }
  // Erase no elements.
  {
    flat_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, [](std::pair<const int, int>) { return false; }), 0);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3),
                                        Pair(4, 4), Pair(5, 5)));
  }
  // Erase specific elements.
  {
    flat_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s,
                       [](std::pair<const int, int> kvp) {
                         return kvp.first % 2 == 1;
                       }),
              3);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(2, 2), Pair(4, 4)));
  }
  // Predicate is function reference.
  {
    flat_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, FirstIsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(3, 3), Pair(5, 5)));
  }
  // Predicate is function pointer.
  {
    flat_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, &FirstIsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(3, 3), Pair(5, 5)));
  }
}

TEST(FlatHashMap, CForEach) {
  flat_hash_map<int, int> m;
  std::vector<std::pair<int, int>> expected;
  for (int i = 0; i < 100; ++i) {
    {
      SCOPED_TRACE("mutable object iteration");
      std::vector<std::pair<int, int>> v;
      absl::container_internal::c_for_each_fast(
          m, [&v](std::pair<const int, int>& p) { v.push_back(p); });
      EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("const object iteration");
      std::vector<std::pair<int, int>> v;
      const flat_hash_map<int, int>& cm = m;
      absl::container_internal::c_for_each_fast(
          cm, [&v](const std::pair<const int, int>& p) { v.push_back(p); });
      EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("const object iteration");
      std::vector<std::pair<int, int>> v;
      absl::container_internal::c_for_each_fast(
          flat_hash_map<int, int>(m),
          [&v](std::pair<const int, int>& p) { v.push_back(p); });
      EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    }
    m[i] = i;
    expected.emplace_back(i, i);
  }
}

TEST(FlatHashMap, CForEachMutate) {
  flat_hash_map<int, int> s;
  std::vector<std::pair<int, int>> expected;
  for (int i = 0; i < 100; ++i) {
    std::vector<std::pair<int, int>> v;
    absl::container_internal::c_for_each_fast(
        s, [&v](std::pair<const int, int>& p) {
          v.push_back(p);
          p.second++;
        });
    EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    for (auto& p : expected) {
      p.second++;
    }
    EXPECT_THAT(s, UnorderedElementsAreArray(expected));
    s[i] = i;
    expected.emplace_back(i, i);
  }
}

// This test requires std::launder for mutable key access in node handles.
#if defined(__cpp_lib_launder) && __cpp_lib_launder >= 201606
TEST(FlatHashMap, NodeHandleMutableKeyAccess) {
  flat_hash_map<std::string, std::string> map;

  map["key1"] = "mapped";

  auto nh = map.extract(map.begin());
  nh.key().resize(3);
  map.insert(std::move(nh));

  EXPECT_THAT(map, testing::ElementsAre(Pair("key", "mapped")));
}
#endif

TEST(FlatHashMap, Reserve) {
  // Verify that if we reserve(size() + n) then we can perform n insertions
  // without a rehash, i.e., without invalidating any references.
  for (size_t trial = 0; trial < 20; ++trial) {
    for (size_t initial = 3; initial < 100; ++initial) {
      // Fill in `initial` entries, then erase 2 of them, then reserve space for
      // two inserts and check for reference stability while doing the inserts.
      flat_hash_map<size_t, size_t> map;
      for (size_t i = 0; i < initial; ++i) {
        map[i] = i;
      }
      map.erase(0);
      map.erase(1);
      map.reserve(map.size() + 2);
      size_t& a2 = map[2];
      // In the event of a failure, asan will complain in one of these two
      // assignments.
      map[initial] = a2;
      map[initial + 1] = a2;
      // Fail even when not under asan:
      size_t& a2new = map[2];
      EXPECT_EQ(&a2, &a2new);
    }
  }
}

TEST(FlatHashMap, RecursiveTypeCompiles) {
  struct RecursiveType {
    flat_hash_map<int, RecursiveType> m;
  };
  RecursiveType t;
  t.m[0] = RecursiveType{};
}

TEST(FlatHashMap, FlatHashMapPolicyDestroyReturnsTrue) {
  EXPECT_TRUE(
      (decltype(FlatHashMapPolicy<int, char>::destroy<std::allocator<char>>(
          nullptr, nullptr))()));
  EXPECT_FALSE(
      (decltype(FlatHashMapPolicy<int, char>::destroy<CountingAllocator<char>>(
          nullptr, nullptr))()));
  EXPECT_FALSE((decltype(FlatHashMapPolicy<int, std::unique_ptr<int>>::destroy<
                         std::allocator<char>>(nullptr, nullptr))()));
}

struct InconsistentHashEqType {
  InconsistentHashEqType(int v1, int v2) : v1(v1), v2(v2) {}
  template <typename H>
  friend H AbslHashValue(H h, InconsistentHashEqType t) {
    return H::combine(std::move(h), t.v1);
  }
  bool operator==(InconsistentHashEqType t) const { return v2 == t.v2; }
  int v1, v2;
};

TEST(Iterator, InconsistentHashEqFunctorsValidation) {
  if (!IsAssertEnabled()) GTEST_SKIP() << "Assertions not enabled.";

  absl::flat_hash_map<InconsistentHashEqType, int> m;
  for (int i = 0; i < 10; ++i) m[{i, i}] = 1;
  // We need to insert multiple times to guarantee that we get the assertion
  // because it's possible for the hash to collide with the inserted element
  // that has v2==0. In those cases, the new element won't be inserted.
  auto insert_conflicting_elems = [&] {
    for (int i = 100; i < 20000; ++i) {
      EXPECT_EQ((m[{i, 0}]), 1);
    }
  };

  const char* crash_message = "hash/eq functors are inconsistent.";
#if defined(__arm__) || defined(__aarch64__)
  // On ARM, the crash message is garbled so don't expect a specific message.
  crash_message = "";
#endif
  EXPECT_DEATH_IF_SUPPORTED(insert_conflicting_elems(), crash_message);
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
