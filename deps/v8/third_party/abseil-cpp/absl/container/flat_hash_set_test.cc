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

#include "absl/container/flat_hash_set.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/hash_container_defaults.h"
#include "absl/container/internal/container_memory.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/test_allocator.h"
#include "absl/container/internal/unordered_set_constructor_test.h"
#include "absl/container/internal/unordered_set_lookup_test.h"
#include "absl/container/internal/unordered_set_members_test.h"
#include "absl/container/internal/unordered_set_modifiers_test.h"
#include "absl/hash/hash.h"
#include "absl/log/check.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::absl::container_internal::hash_internal::Enum;
using ::absl::container_internal::hash_internal::EnumClass;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

// Check that absl::flat_hash_set works in a global constructor.
struct BeforeMain {
  BeforeMain() {
    absl::flat_hash_set<int> x;
    x.insert(1);
    CHECK(!x.contains(0)) << "x should not contain 0";
    CHECK(x.contains(1)) << "x should contain 1";
  }
};
const BeforeMain before_main;

template <class T>
using Set =
    absl::flat_hash_set<T, StatefulTestingHash, StatefulTestingEqual, Alloc<T>>;

using SetTypes =
    ::testing::Types<Set<int>, Set<std::string>, Set<Enum>, Set<EnumClass>>;

INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashSet, ConstructorTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashSet, LookupTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashSet, MembersTest, SetTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(FlatHashSet, ModifiersTest, SetTypes);

TEST(FlatHashSet, EmplaceString) {
  std::vector<std::string> v = {"a", "b"};
  absl::flat_hash_set<absl::string_view> hs(v.begin(), v.end());
  EXPECT_THAT(hs, UnorderedElementsAreArray(v));
}

TEST(FlatHashSet, BitfieldArgument) {
  union {
    int n : 1;
  };
  n = 0;
  absl::flat_hash_set<int> s = {n};
  s.insert(n);
  s.insert(s.end(), n);
  s.insert({n});
  s.erase(n);
  s.count(n);
  s.prefetch(n);
  s.find(n);
  s.contains(n);
  s.equal_range(n);
}

TEST(FlatHashSet, MergeExtractInsert) {
  struct Hash {
    size_t operator()(const std::unique_ptr<int>& p) const { return *p; }
  };
  struct Eq {
    bool operator()(const std::unique_ptr<int>& a,
                    const std::unique_ptr<int>& b) const {
      return *a == *b;
    }
  };
  absl::flat_hash_set<std::unique_ptr<int>, Hash, Eq> set1, set2;
  set1.insert(absl::make_unique<int>(7));
  set1.insert(absl::make_unique<int>(17));

  set2.insert(absl::make_unique<int>(7));
  set2.insert(absl::make_unique<int>(19));

  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(7), Pointee(17)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7), Pointee(19)));

  set1.merge(set2);

  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(7), Pointee(17), Pointee(19)));
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7)));

  auto node = set1.extract(absl::make_unique<int>(7));
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Pointee(7));
  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(17), Pointee(19)));

  auto insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_FALSE(insert_result.inserted);
  EXPECT_TRUE(insert_result.node);
  EXPECT_THAT(insert_result.node.value(), Pointee(7));
  EXPECT_EQ(**insert_result.position, 7);
  EXPECT_NE(insert_result.position->get(), insert_result.node.value().get());
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7)));

  node = set1.extract(absl::make_unique<int>(17));
  EXPECT_TRUE(node);
  EXPECT_THAT(node.value(), Pointee(17));
  EXPECT_THAT(set1, UnorderedElementsAre(Pointee(19)));

  node.value() = absl::make_unique<int>(23);

  insert_result = set2.insert(std::move(node));
  EXPECT_FALSE(node);
  EXPECT_TRUE(insert_result.inserted);
  EXPECT_FALSE(insert_result.node);
  EXPECT_EQ(**insert_result.position, 23);
  EXPECT_THAT(set2, UnorderedElementsAre(Pointee(7), Pointee(23)));
}

bool IsEven(int k) { return k % 2 == 0; }

TEST(FlatHashSet, EraseIf) {
  // Erase all elements.
  {
    flat_hash_set<int> s = {1, 2, 3, 4, 5};
    EXPECT_EQ(erase_if(s, [](int) { return true; }), 5);
    EXPECT_THAT(s, IsEmpty());
  }
  // Erase no elements.
  {
    flat_hash_set<int> s = {1, 2, 3, 4, 5};
    EXPECT_EQ(erase_if(s, [](int) { return false; }), 0);
    EXPECT_THAT(s, UnorderedElementsAre(1, 2, 3, 4, 5));
  }
  // Erase specific elements.
  {
    flat_hash_set<int> s = {1, 2, 3, 4, 5};
    EXPECT_EQ(erase_if(s, [](int k) { return k % 2 == 1; }), 3);
    EXPECT_THAT(s, UnorderedElementsAre(2, 4));
  }
  // Predicate is function reference.
  {
    flat_hash_set<int> s = {1, 2, 3, 4, 5};
    EXPECT_EQ(erase_if(s, IsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(1, 3, 5));
  }
  // Predicate is function pointer.
  {
    flat_hash_set<int> s = {1, 2, 3, 4, 5};
    EXPECT_EQ(erase_if(s, &IsEven), 2);
    EXPECT_THAT(s, UnorderedElementsAre(1, 3, 5));
  }
}

TEST(FlatHashSet, CForEach) {
  using ValueType = std::pair<int, int>;
  flat_hash_set<ValueType> s;
  std::vector<ValueType> expected;
  for (int i = 0; i < 100; ++i) {
    {
      SCOPED_TRACE("mutable object iteration");
      std::vector<ValueType> v;
      absl::container_internal::c_for_each_fast(
          s, [&v](const ValueType& p) { v.push_back(p); });
      ASSERT_THAT(v, UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("const object iteration");
      std::vector<ValueType> v;
      const flat_hash_set<ValueType>& cs = s;
      absl::container_internal::c_for_each_fast(
          cs, [&v](const ValueType& p) { v.push_back(p); });
      ASSERT_THAT(v, UnorderedElementsAreArray(expected));
    }
    {
      SCOPED_TRACE("temporary object iteration");
      std::vector<ValueType> v;
      absl::container_internal::c_for_each_fast(
          flat_hash_set<ValueType>(s),
          [&v](const ValueType& p) { v.push_back(p); });
      ASSERT_THAT(v, UnorderedElementsAreArray(expected));
    }
    s.emplace(i, i);
    expected.emplace_back(i, i);
  }
}

class PoisonSoo {
  int64_t data_;

 public:
  explicit PoisonSoo(int64_t d) : data_(d) { SanitizerPoisonObject(&data_); }
  PoisonSoo(const PoisonSoo& that) : PoisonSoo(*that) {}
  ~PoisonSoo() { SanitizerUnpoisonObject(&data_); }

  int64_t operator*() const {
    SanitizerUnpoisonObject(&data_);
    const int64_t ret = data_;
    SanitizerPoisonObject(&data_);
    return ret;
  }
  template <typename H>
  friend H AbslHashValue(H h, const PoisonSoo& pi) {
    return H::combine(std::move(h), *pi);
  }
  bool operator==(const PoisonSoo& rhs) const { return **this == *rhs; }
};

TEST(FlatHashSet, PoisonSooBasic) {
  PoisonSoo a(0), b(1);
  flat_hash_set<PoisonSoo> set;
  set.insert(a);
  EXPECT_THAT(set, UnorderedElementsAre(a));
  set.insert(b);
  EXPECT_THAT(set, UnorderedElementsAre(a, b));
  set.erase(a);
  EXPECT_THAT(set, UnorderedElementsAre(b));
  set.rehash(0);  // Shrink to SOO.
  EXPECT_THAT(set, UnorderedElementsAre(b));
}

TEST(FlatHashSet, PoisonSooMoveConstructSooToSoo) {
  PoisonSoo a(0);
  flat_hash_set<PoisonSoo> set;
  set.insert(a);
  flat_hash_set<PoisonSoo> set2(std::move(set));
  EXPECT_THAT(set2, UnorderedElementsAre(a));
}

TEST(FlatHashSet, PoisonSooAllocMoveConstructSooToSoo) {
  PoisonSoo a(0);
  flat_hash_set<PoisonSoo> set;
  set.insert(a);
  flat_hash_set<PoisonSoo> set2(std::move(set), std::allocator<PoisonSoo>());
  EXPECT_THAT(set2, UnorderedElementsAre(a));
}

TEST(FlatHashSet, PoisonSooMoveAssignFullSooToEmptySoo) {
  PoisonSoo a(0);
  flat_hash_set<PoisonSoo> set, set2;
  set.insert(a);
  set2 = std::move(set);
  EXPECT_THAT(set2, UnorderedElementsAre(a));
}

TEST(FlatHashSet, PoisonSooMoveAssignFullSooToFullSoo) {
  PoisonSoo a(0), b(1);
  flat_hash_set<PoisonSoo> set, set2;
  set.insert(a);
  set2.insert(b);
  set2 = std::move(set);
  EXPECT_THAT(set2, UnorderedElementsAre(a));
}

TEST(FlatHashSet, FlatHashSetPolicyDestroyReturnsTrue) {
  EXPECT_TRUE((decltype(FlatHashSetPolicy<int>::destroy<std::allocator<int>>(
      nullptr, nullptr))()));
  EXPECT_FALSE(
      (decltype(FlatHashSetPolicy<int>::destroy<CountingAllocator<int>>(
          nullptr, nullptr))()));
  EXPECT_FALSE((decltype(FlatHashSetPolicy<std::unique_ptr<int>>::destroy<
                         std::allocator<int>>(nullptr, nullptr))()));
}

struct HashEqInvalidOnMove {
  HashEqInvalidOnMove() = default;
  HashEqInvalidOnMove(const HashEqInvalidOnMove& rhs) = default;
  HashEqInvalidOnMove(HashEqInvalidOnMove&& rhs) { rhs.moved = true; }
  HashEqInvalidOnMove& operator=(const HashEqInvalidOnMove& rhs) = default;
  HashEqInvalidOnMove& operator=(HashEqInvalidOnMove&& rhs) {
    rhs.moved = true;
    return *this;
  }

  size_t operator()(int x) const {
    CHECK(!moved);
    return absl::HashOf(x);
  }

  bool operator()(int x, int y) const {
    CHECK(!moved);
    return x == y;
  }

  bool moved = false;
};

TEST(FlatHashSet, MovedFromCleared_HashMustBeValid) {
  flat_hash_set<int, HashEqInvalidOnMove> s1, s2;
  // Moving the hashtable must not move the hasher because we need to support
  // this behavior.
  s2 = std::move(s1);
  s1.clear();
  s1.insert(2);
  EXPECT_THAT(s1, UnorderedElementsAre(2));
}

TEST(FlatHashSet, MovedFromCleared_EqMustBeValid) {
  flat_hash_set<int, DefaultHashContainerHash<int>, HashEqInvalidOnMove> s1, s2;
  // Moving the hashtable must not move the equality functor because we need to
  // support this behavior.
  s2 = std::move(s1);
  s1.clear();
  s1.insert(2);
  EXPECT_THAT(s1, UnorderedElementsAre(2));
}

TEST(FlatHashSet, Equality) {
  {
    flat_hash_set<int> s1 = {1, 2, 3};
    flat_hash_set<int> s2 = {1, 2, 3};
    EXPECT_EQ(s1, s2);
  }
  {
    flat_hash_set<std::string> s1 = {"a", "b", "c"};
    flat_hash_set<std::string> s2 = {"a", "b", "c"};
    EXPECT_EQ(s1, s2);
  }
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
