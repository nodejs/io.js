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

#ifndef ABSL_CONTAINER_INTERNAL_UNORDERED_SET_CONSTRUCTOR_TEST_H_
#define ABSL_CONTAINER_INTERNAL_UNORDERED_SET_CONSTRUCTOR_TEST_H_

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/container/internal/hash_generator_testing.h"
#include "absl/container/internal/hash_policy_testing.h"
#include "absl/meta/type_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {

template <class UnordMap>
class ConstructorTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(ConstructorTest);

TYPED_TEST_P(ConstructorTest, NoArgs) {
  TypeParam m;
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
}

TYPED_TEST_P(ConstructorTest, BucketCount) {
  TypeParam m(123);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, BucketCountHash) {
  using H = typename TypeParam::hasher;
  H hasher;
  TypeParam m(123, hasher);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, BucketCountHashEqual) {
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  H hasher;
  E equal;
  TypeParam m(123, hasher, equal);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.key_eq(), equal);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, BucketCountHashEqualAlloc) {
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(123, hasher, equal, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.key_eq(), equal);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);

  const auto& cm = m;
  EXPECT_EQ(cm.hash_function(), hasher);
  EXPECT_EQ(cm.key_eq(), equal);
  EXPECT_EQ(cm.get_allocator(), alloc);
  EXPECT_TRUE(cm.empty());
  EXPECT_THAT(keys(cm), ::testing::UnorderedElementsAre());
  EXPECT_GE(cm.bucket_count(), 123);
}

template <typename T>
struct is_std_unordered_set : std::false_type {};

template <typename... T>
struct is_std_unordered_set<std::unordered_set<T...>> : std::true_type {};

#if defined(UNORDERED_SET_CXX14) || defined(UNORDERED_SET_CXX17)
using has_cxx14_std_apis = std::true_type;
#else
using has_cxx14_std_apis = std::false_type;
#endif

template <typename T>
using expect_cxx14_apis =
    absl::disjunction<absl::negation<is_std_unordered_set<T>>,
                      has_cxx14_std_apis>;

template <typename TypeParam>
void BucketCountAllocTest(std::false_type) {}

template <typename TypeParam>
void BucketCountAllocTest(std::true_type) {
  using A = typename TypeParam::allocator_type;
  A alloc(0);
  TypeParam m(123, alloc);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, BucketCountAlloc) {
  BucketCountAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

template <typename TypeParam>
void BucketCountHashAllocTest(std::false_type) {}

template <typename TypeParam>
void BucketCountHashAllocTest(std::true_type) {
  using H = typename TypeParam::hasher;
  using A = typename TypeParam::allocator_type;
  H hasher;
  A alloc(0);
  TypeParam m(123, hasher, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, BucketCountHashAlloc) {
  BucketCountHashAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

#if ABSL_UNORDERED_SUPPORTS_ALLOC_CTORS
using has_alloc_std_constructors = std::true_type;
#else
using has_alloc_std_constructors = std::false_type;
#endif

template <typename T>
using expect_alloc_constructors =
    absl::disjunction<absl::negation<is_std_unordered_set<T>>,
                      has_alloc_std_constructors>;

template <typename TypeParam>
void AllocTest(std::false_type) {}

template <typename TypeParam>
void AllocTest(std::true_type) {
  using A = typename TypeParam::allocator_type;
  A alloc(0);
  TypeParam m(alloc);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_TRUE(m.empty());
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAre());
}

TYPED_TEST_P(ConstructorTest, Alloc) {
  AllocTest<TypeParam>(expect_alloc_constructors<TypeParam>());
}

TYPED_TEST_P(ConstructorTest, InputIteratorBucketHashEqualAlloc) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  std::vector<T> values;
  for (size_t i = 0; i != 10; ++i)
    values.push_back(hash_internal::Generator<T>()());
  TypeParam m(values.begin(), values.end(), 123, hasher, equal, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.key_eq(), equal);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

template <typename TypeParam>
void InputIteratorBucketAllocTest(std::false_type) {}

template <typename TypeParam>
void InputIteratorBucketAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using A = typename TypeParam::allocator_type;
  A alloc(0);
  std::vector<T> values;
  for (size_t i = 0; i != 10; ++i)
    values.push_back(hash_internal::Generator<T>()());
  TypeParam m(values.begin(), values.end(), 123, alloc);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, InputIteratorBucketAlloc) {
  InputIteratorBucketAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

template <typename TypeParam>
void InputIteratorBucketHashAllocTest(std::false_type) {}

template <typename TypeParam>
void InputIteratorBucketHashAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using A = typename TypeParam::allocator_type;
  H hasher;
  A alloc(0);
  std::vector<T> values;
  for (size_t i = 0; i != 10; ++i)
    values.push_back(hash_internal::Generator<T>()());
  TypeParam m(values.begin(), values.end(), 123, hasher, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, InputIteratorBucketHashAlloc) {
  InputIteratorBucketHashAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

TYPED_TEST_P(ConstructorTest, CopyConstructor) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(123, hasher, equal, alloc);
  for (size_t i = 0; i != 10; ++i) m.insert(hash_internal::Generator<T>()());
  TypeParam n(m);
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_EQ(m.get_allocator(), n.get_allocator());
  EXPECT_EQ(m, n);
  EXPECT_NE(TypeParam(0, hasher, equal, alloc), n);
}

template <typename TypeParam>
void CopyConstructorAllocTest(std::false_type) {}

template <typename TypeParam>
void CopyConstructorAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(123, hasher, equal, alloc);
  for (size_t i = 0; i != 10; ++i) m.insert(hash_internal::Generator<T>()());
  TypeParam n(m, A(11));
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_NE(m.get_allocator(), n.get_allocator());
  EXPECT_EQ(m, n);
}

TYPED_TEST_P(ConstructorTest, CopyConstructorAlloc) {
  CopyConstructorAllocTest<TypeParam>(expect_alloc_constructors<TypeParam>());
}

// TODO(alkis): Test non-propagating allocators on copy constructors.

TYPED_TEST_P(ConstructorTest, MoveConstructor) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(123, hasher, equal, alloc);
  for (size_t i = 0; i != 10; ++i) m.insert(hash_internal::Generator<T>()());
  TypeParam t(m);
  TypeParam n(std::move(t));
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_EQ(m.get_allocator(), n.get_allocator());
  EXPECT_EQ(m, n);
}

template <typename TypeParam>
void MoveConstructorAllocTest(std::false_type) {}

template <typename TypeParam>
void MoveConstructorAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(123, hasher, equal, alloc);
  for (size_t i = 0; i != 10; ++i) m.insert(hash_internal::Generator<T>()());
  TypeParam t(m);
  TypeParam n(std::move(t), A(1));
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_NE(m.get_allocator(), n.get_allocator());
  EXPECT_EQ(m, n);
}

TYPED_TEST_P(ConstructorTest, MoveConstructorAlloc) {
  MoveConstructorAllocTest<TypeParam>(expect_alloc_constructors<TypeParam>());
}

// TODO(alkis): Test non-propagating allocators on move constructors.

TYPED_TEST_P(ConstructorTest, InitializerListBucketHashEqualAlloc) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  TypeParam m(values, 123, hasher, equal, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.key_eq(), equal);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

template <typename TypeParam>
void InitializerListBucketAllocTest(std::false_type) {}

template <typename TypeParam>
void InitializerListBucketAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using A = typename TypeParam::allocator_type;
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  A alloc(0);
  TypeParam m(values, 123, alloc);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, InitializerListBucketAlloc) {
  InitializerListBucketAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

template <typename TypeParam>
void InitializerListBucketHashAllocTest(std::false_type) {}

template <typename TypeParam>
void InitializerListBucketHashAllocTest(std::true_type) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using A = typename TypeParam::allocator_type;
  H hasher;
  A alloc(0);
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  TypeParam m(values, 123, hasher, alloc);
  EXPECT_EQ(m.hash_function(), hasher);
  EXPECT_EQ(m.get_allocator(), alloc);
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
  EXPECT_GE(m.bucket_count(), 123);
}

TYPED_TEST_P(ConstructorTest, InitializerListBucketHashAlloc) {
  InitializerListBucketHashAllocTest<TypeParam>(expect_cxx14_apis<TypeParam>());
}

TYPED_TEST_P(ConstructorTest, CopyAssignment) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  hash_internal::Generator<T> gen;
  TypeParam m({gen(), gen(), gen()}, 123, hasher, equal, alloc);
  TypeParam n;
  n = m;
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_EQ(m, n);
}

// TODO(alkis): Test [non-]propagating allocators on move/copy assignments
// (it depends on traits).

TYPED_TEST_P(ConstructorTest, MoveAssignment) {
  using T = hash_internal::GeneratedType<TypeParam>;
  using H = typename TypeParam::hasher;
  using E = typename TypeParam::key_equal;
  using A = typename TypeParam::allocator_type;
  H hasher;
  E equal;
  A alloc(0);
  hash_internal::Generator<T> gen;
  TypeParam m({gen(), gen(), gen()}, 123, hasher, equal, alloc);
  TypeParam t(m);
  TypeParam n;
  n = std::move(t);
  EXPECT_EQ(m.hash_function(), n.hash_function());
  EXPECT_EQ(m.key_eq(), n.key_eq());
  EXPECT_EQ(m, n);
}

TYPED_TEST_P(ConstructorTest, AssignmentFromInitializerList) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  TypeParam m;
  m = values;
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
}

TYPED_TEST_P(ConstructorTest, AssignmentOverwritesExisting) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  TypeParam m({gen(), gen(), gen()});
  TypeParam n({gen()});
  n = m;
  EXPECT_EQ(m, n);
}

TYPED_TEST_P(ConstructorTest, MoveAssignmentOverwritesExisting) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  TypeParam m({gen(), gen(), gen()});
  TypeParam t(m);
  TypeParam n({gen()});
  n = std::move(t);
  EXPECT_EQ(m, n);
}

TYPED_TEST_P(ConstructorTest, AssignmentFromInitializerListOverwritesExisting) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  TypeParam m;
  m = values;
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
}

TYPED_TEST_P(ConstructorTest, AssignmentOnSelf) {
  using T = hash_internal::GeneratedType<TypeParam>;
  hash_internal::Generator<T> gen;
  std::initializer_list<T> values = {gen(), gen(), gen(), gen(), gen()};
  TypeParam m(values);
  m = *&m;  // Avoid -Wself-assign.
  EXPECT_THAT(keys(m), ::testing::UnorderedElementsAreArray(values));
}

REGISTER_TYPED_TEST_SUITE_P(
    ConstructorTest, NoArgs, BucketCount, BucketCountHash, BucketCountHashEqual,
    BucketCountHashEqualAlloc, BucketCountAlloc, BucketCountHashAlloc, Alloc,
    InputIteratorBucketHashEqualAlloc, InputIteratorBucketAlloc,
    InputIteratorBucketHashAlloc, CopyConstructor, CopyConstructorAlloc,
    MoveConstructor, MoveConstructorAlloc, InitializerListBucketHashEqualAlloc,
    InitializerListBucketAlloc, InitializerListBucketHashAlloc, CopyAssignment,
    MoveAssignment, AssignmentFromInitializerList, AssignmentOverwritesExisting,
    MoveAssignmentOverwritesExisting,
    AssignmentFromInitializerListOverwritesExisting, AssignmentOnSelf);

}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_CONTAINER_INTERNAL_UNORDERED_SET_CONSTRUCTOR_TEST_H_
