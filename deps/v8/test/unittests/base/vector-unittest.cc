// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/vector.h"

#include <algorithm>

#include "testing/gmock-support.h"

namespace v8 {
namespace base {

TEST(VectorTest, Factories) {
  auto vec = base::CStrVector("foo");
  EXPECT_EQ(3u, vec.size());
  EXPECT_EQ(0, memcmp(vec.begin(), "foo", 3));

  vec = base::ArrayVector("foo");
  EXPECT_EQ(4u, vec.size());
  EXPECT_EQ(0, memcmp(vec.begin(), "foo\0", 4));

  vec = base::CStrVector("foo\0\0");
  EXPECT_EQ(3u, vec.size());
  EXPECT_EQ(0, memcmp(vec.begin(), "foo", 3));

  vec = base::CStrVector("");
  EXPECT_EQ(0u, vec.size());

  vec = base::CStrVector("\0");
  EXPECT_EQ(0u, vec.size());
}

// Test operator== and operator!= on different Vector types.
TEST(VectorTest, Equals) {
  auto foo1 = base::CStrVector("foo");
  auto foo2 = base::ArrayVector("ffoo") + 1;
  EXPECT_EQ(4u, foo2.size());  // Includes trailing '\0'.
  foo2.Truncate(foo2.size() - 1);
  // This is a requirement for the test.
  EXPECT_NE(foo1.begin(), foo2.begin());
  EXPECT_EQ(foo1, foo2);

  // Compare base::Vector<char> against base::Vector<const char>.
  char arr1[] = {'a', 'b', 'c'};
  char arr2[] = {'a', 'b', 'c'};
  char arr3[] = {'a', 'b', 'd'};
  base::Vector<char> vec1_char = base::ArrayVector(arr1);
  base::Vector<const char> vec1_const_char = vec1_char;
  base::Vector<char> vec2_char = base::ArrayVector(arr2);
  base::Vector<char> vec3_char = base::ArrayVector(arr3);
  EXPECT_NE(vec1_char.begin(), vec2_char.begin());
  // Note: We directly call operator== and operator!= here (without EXPECT_EQ or
  // EXPECT_NE) to have full control over the arguments.
  EXPECT_TRUE(vec1_char == vec1_const_char);
  EXPECT_TRUE(vec1_char == vec2_char);
  EXPECT_TRUE(vec1_const_char == vec2_char);
  EXPECT_TRUE(vec1_const_char != vec3_char);
  EXPECT_TRUE(vec3_char != vec2_char);
  EXPECT_TRUE(vec3_char != vec1_const_char);
}

TEST(OwnedVectorTest, Equals) {
  auto int_vec = base::OwnedVector<int>::New(4);
  EXPECT_EQ(4u, int_vec.size());
  auto find_non_zero = [](int i) { return i != 0; };
  EXPECT_EQ(int_vec.end(),
            std::find_if(int_vec.begin(), int_vec.end(), find_non_zero));

  constexpr int kInit[] = {4, 11, 3};
  auto init_vec1 = base::OwnedCopyOf(kInit);
  // Note: {const int} should also work: We initialize the owned vector, but
  // afterwards it's non-modifyable.
  auto init_vec2 = base::OwnedCopyOf(base::ArrayVector(kInit));
  EXPECT_EQ(init_vec1.as_vector(), base::ArrayVector(kInit));
  EXPECT_EQ(init_vec1.as_vector(), init_vec2.as_vector());
}

TEST(OwnedVectorTest, MoveConstructionAndAssignment) {
  constexpr int kValues[] = {4, 11, 3};
  auto int_vec = base::OwnedCopyOf(kValues);
  EXPECT_EQ(3u, int_vec.size());

  auto move_constructed_vec = std::move(int_vec);
  EXPECT_EQ(move_constructed_vec.as_vector(), base::ArrayVector(kValues));

  auto move_assigned_to_empty = base::OwnedVector<int>{};
  move_assigned_to_empty = std::move(move_constructed_vec);
  EXPECT_EQ(move_assigned_to_empty.as_vector(), base::ArrayVector(kValues));

  auto move_assigned_to_non_empty = base::OwnedVector<int>::New(2);
  move_assigned_to_non_empty = std::move(move_assigned_to_empty);
  EXPECT_EQ(move_assigned_to_non_empty.as_vector(), base::ArrayVector(kValues));

  // All but the last vector must be empty (length 0, nullptr data).
  EXPECT_TRUE(int_vec.empty());
  EXPECT_TRUE(int_vec.begin() == nullptr);
  EXPECT_TRUE(move_constructed_vec.empty());
  EXPECT_TRUE(move_constructed_vec.begin() == nullptr);
  EXPECT_TRUE(move_assigned_to_empty.empty());
  EXPECT_TRUE(move_assigned_to_empty.begin() == nullptr);
}

// Test that the constexpr factory methods work.
TEST(VectorTest, ConstexprFactories) {
  static constexpr int kInit1[] = {4, 11, 3};
  static constexpr auto kVec1 = base::ArrayVector(kInit1);
  static_assert(kVec1.size() == 3);
  EXPECT_THAT(kVec1, testing::ElementsAreArray(kInit1));

  static constexpr auto kVec2 = base::VectorOf(kInit1, 2);
  static_assert(kVec2.size() == 2);
  EXPECT_THAT(kVec2, testing::ElementsAre(4, 11));

  static constexpr const char kInit3[] = "foobar";
  static constexpr auto kVec3 = base::StaticCharVector(kInit3);
  static_assert(kVec3.size() == 6);
  EXPECT_THAT(kVec3, testing::ElementsAreArray(kInit3, kInit3 + 6));
}

}  // namespace base
}  // namespace v8
