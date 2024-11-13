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

#include "absl/container/node_hash_map.h"

#include <cstddef>
#include <new>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/internal/hash_policy_testing.h"
#include "absl/container/internal/tracked.h"
#include "absl/container/internal/unordered_map_constructor_test.h"
#include "absl/container/internal/unordered_map_lookup_test.h"
#include "absl/container/internal/unordered_map_members_test.h"
#include "absl/container/internal/unordered_map_modifiers_test.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

using MapTypes = ::testing::Types<
    absl::node_hash_map<int, int, StatefulTestingHash, StatefulTestingEqual,
                        Alloc<std::pair<const int, int>>>,
    absl::node_hash_map<std::string, std::string, StatefulTestingHash,
                        StatefulTestingEqual,
                        Alloc<std::pair<const std::string, std::string>>>>;

INSTANTIATE_TYPED_TEST_SUITE_P(NodeHashMap, ConstructorTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(NodeHashMap, LookupTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(NodeHashMap, MembersTest, MapTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(NodeHashMap, ModifiersTest, MapTypes);

using M = absl::node_hash_map<std::string, Tracked<int>>;

TEST(NodeHashMap, Emplace) {
  M m;
  Tracked<int> t(53);
  m.emplace("a", t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(1, t.num_copies());

  m.emplace(std::string("a"), t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(1, t.num_copies());

  std::string a("a");
  m.emplace(a, t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(1, t.num_copies());

  const std::string ca("a");
  m.emplace(a, t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(1, t.num_copies());

  m.emplace(std::make_pair("a", t));
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(2, t.num_copies());

  m.emplace(std::make_pair(std::string("a"), t));
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(3, t.num_copies());

  std::pair<std::string, Tracked<int>> p("a", t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(4, t.num_copies());
  m.emplace(p);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(4, t.num_copies());

  const std::pair<std::string, Tracked<int>> cp("a", t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(5, t.num_copies());
  m.emplace(cp);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(5, t.num_copies());

  std::pair<const std::string, Tracked<int>> pc("a", t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(6, t.num_copies());
  m.emplace(pc);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(6, t.num_copies());

  const std::pair<const std::string, Tracked<int>> cpc("a", t);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(7, t.num_copies());
  m.emplace(cpc);
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(7, t.num_copies());

  m.emplace(std::piecewise_construct, std::forward_as_tuple("a"),
            std::forward_as_tuple(t));
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(7, t.num_copies());

  m.emplace(std::piecewise_construct, std::forward_as_tuple(std::string("a")),
            std::forward_as_tuple(t));
  ASSERT_EQ(0, t.num_moves());
  ASSERT_EQ(7, t.num_copies());
}

TEST(NodeHashMap, AssignRecursive) {
  struct Tree {
    // Verify that unordered_map<K, IncompleteType> can be instantiated.
    absl::node_hash_map<int, Tree> children;
  };
  Tree root;
  const Tree& child = root.children.emplace().first->second;
  // Verify that `lhs = rhs` doesn't read rhs after clearing lhs.
  root = child;
}

TEST(FlatHashMap, MoveOnlyKey) {
  struct Key {
    Key() = default;
    Key(Key&&) = default;
    Key& operator=(Key&&) = default;
  };
  struct Eq {
    bool operator()(const Key&, const Key&) const { return true; }
  };
  struct Hash {
    size_t operator()(const Key&) const { return 0; }
  };
  absl::node_hash_map<Key, int, Hash, Eq> m;
  m[Key()];
}

struct NonMovableKey {
  explicit NonMovableKey(int i) : i(i) {}
  NonMovableKey(NonMovableKey&&) = delete;
  int i;
};
struct NonMovableKeyHash {
  using is_transparent = void;
  size_t operator()(const NonMovableKey& k) const { return k.i; }
  size_t operator()(int k) const { return k; }
};
struct NonMovableKeyEq {
  using is_transparent = void;
  bool operator()(const NonMovableKey& a, const NonMovableKey& b) const {
    return a.i == b.i;
  }
  bool operator()(const NonMovableKey& a, int b) const { return a.i == b; }
};

TEST(NodeHashMap, MergeExtractInsert) {
  absl::node_hash_map<NonMovableKey, int, NonMovableKeyHash, NonMovableKeyEq>
      set1, set2;
  set1.emplace(std::piecewise_construct, std::make_tuple(7),
               std::make_tuple(-7));
  set1.emplace(std::piecewise_construct, std::make_tuple(17),
               std::make_tuple(-17));

  set2.emplace(std::piecewise_construct, std::make_tuple(7),
               std::make_tuple(-70));
  set2.emplace(std::piecewise_construct, std::make_tuple(19),
               std::make_tuple(-190));

  auto Elem = [](int key, int value) {
    return Pair(Field(&NonMovableKey::i, key), value);
  };

  EXPECT_THAT(set1, UnorderedElementsAre(Elem(7, -7), Elem(17, -17)));
  EXPECT_THAT(set2, UnorderedElementsAre(Elem(7, -70), Elem(19, -190)));

  // NonMovableKey is neither copyable nor movable. We should still be able to
  // move nodes around.
  static_assert(!std::is_move_constructible<NonMovableKey>::value, "");
  set1.merge(set2);

  EXPECT_THAT(set1,
              UnorderedElementsAre(Elem(7, -7), Elem(17, -17), Elem(19, -190)));
  EXPECT_THAT(set2, UnorderedElementsAre(Elem(7, -70)));

  auto node = set1.extract(7);
  EXPECT_TRUE(node);
  EXPECT_EQ(node.key().i, 7);
  EXPECT_EQ(node.mapped(), -7);
  EXPECT_THAT(set1, UnorderedElementsAre(Elem(17, -17), Elem(19, -190)));

  auto insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_FALSE(insert_result.inserted);
  EXPECT_TRUE(insert_result.node);
  EXPECT_EQ(insert_result.node.key().i, 7);
  EXPECT_EQ(insert_result.node.mapped(), -7);
  EXPECT_THAT(*insert_result.position, Elem(7, -70));
  EXPECT_THAT(set2, UnorderedElementsAre(Elem(7, -70)));

  node = set1.extract(17);
  EXPECT_TRUE(node);
  EXPECT_EQ(node.key().i, 17);
  EXPECT_EQ(node.mapped(), -17);
  EXPECT_THAT(set1, UnorderedElementsAre(Elem(19, -190)));

  node.mapped() = 23;

  insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_TRUE(insert_result.inserted);
  EXPECT_FALSE(insert_result.node);
  EXPECT_THAT(*insert_result.position, Elem(17, 23));
  EXPECT_THAT(set2, UnorderedElementsAre(Elem(7, -70), Elem(17, 23)));
}

bool FirstIsEven(std::pair<const int, int> p) { return p.first % 2 == 0; }

TEST(NodeHashMap, EraseIf) {
  // Erase all elements.
  {
    node_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, [](std::pair<const int, int>) { return true; }), 5);
    EXPECT_THAT(s, IsEmpty());
  }
  // Erase no elements.
  {
    node_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, [](std::pair<const int, int>) { return false; }), 0);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(2, 2), Pair(3, 3),
                                        Pair(4, 4), Pair(5, 5)));
  }
  // Erase specific elements.
  {
    node_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s,
                       [](std::pair<const int, int> kvp) {
                         return kvp.first % 2 == 1;
                       }),
              3);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(2, 2), Pair(4, 4)));
  }
  // Predicate is function reference.
  {
    node_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, FirstIsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(3, 3), Pair(5, 5)));
  }
  // Predicate is function pointer.
  {
    node_hash_map<int, int> s = {{1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}};
    EXPECT_EQ(erase_if(s, &FirstIsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(Pair(1, 1), Pair(3, 3), Pair(5, 5)));
  }
}

TEST(NodeHashMap, CForEach) {
  node_hash_map<int, int> m;
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
      const node_hash_map<int, int>& cm = m;
      absl::container_internal::c_for_each_fast(
          cm, [&v](const std::pair<const int, int>& p) { v.push_back(p); });
      EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("const object iteration");
      std::vector<std::pair<int, int>> v;
      absl::container_internal::c_for_each_fast(
          node_hash_map<int, int>(m),
          [&v](std::pair<const int, int>& p) { v.push_back(p); });
      EXPECT_THAT(v, UnorderedElementsAreArray(expected));
    }
    m[i] = i;
    expected.emplace_back(i, i);
  }
}

TEST(NodeHashMap, CForEachMutate) {
  node_hash_map<int, int> s;
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
TEST(NodeHashMap, NodeHandleMutableKeyAccess) {
  node_hash_map<std::string, std::string> map;

  map["key1"] = "mapped";

  auto nh = map.extract(map.begin());
  nh.key().resize(3);
  map.insert(std::move(nh));

  EXPECT_THAT(map, testing::ElementsAre(Pair("key", "mapped")));
}
#endif

TEST(NodeHashMap, RecursiveTypeCompiles) {
  struct RecursiveType {
    node_hash_map<int, RecursiveType> m;
  };
  RecursiveType t;
  t.m[0] = RecursiveType{};
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
