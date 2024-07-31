//
// Copyright 2022 The Abseil Authors.
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

#include "absl/log/die_if_null.h"

#include <stdint.h>

#include <memory>
#include <utility>

#include "gtest/gtest.h"
#include "absl/base/attributes.h"
#include "absl/log/internal/test_helpers.h"

namespace {

auto* test_env ABSL_ATTRIBUTE_UNUSED = ::testing::AddGlobalTestEnvironment(
    new absl::log_internal::LogTestEnvironment);

// TODO(b/69907837): Revisit these tests with the goal of making them less
// convoluted.
TEST(AbslDieIfNull, Simple) {
  int64_t t;
  void* ptr = static_cast<void*>(&t);
  void* ref = ABSL_DIE_IF_NULL(ptr);
  ASSERT_EQ(ptr, ref);

  char* t_as_char;
  t_as_char = ABSL_DIE_IF_NULL(reinterpret_cast<char*>(&t));
  (void)t_as_char;

  unsigned char* t_as_uchar;
  t_as_uchar = ABSL_DIE_IF_NULL(reinterpret_cast<unsigned char*>(&t));
  (void)t_as_uchar;

  int* t_as_int;
  t_as_int = ABSL_DIE_IF_NULL(reinterpret_cast<int*>(&t));
  (void)t_as_int;

  int64_t* t_as_int64_t;
  t_as_int64_t = ABSL_DIE_IF_NULL(reinterpret_cast<int64_t*>(&t));
  (void)t_as_int64_t;

  std::unique_ptr<int64_t> sptr(new int64_t);
  EXPECT_EQ(sptr.get(), ABSL_DIE_IF_NULL(sptr).get());
  ABSL_DIE_IF_NULL(sptr).reset();

  int64_t* int_ptr = new int64_t();
  EXPECT_EQ(int_ptr, ABSL_DIE_IF_NULL(std::unique_ptr<int64_t>(int_ptr)).get());
}

#if GTEST_HAS_DEATH_TEST
TEST(DeathCheckAbslDieIfNull, Simple) {
  void* ptr;
  ASSERT_DEATH({ ptr = ABSL_DIE_IF_NULL(nullptr); }, "");
  (void)ptr;

  std::unique_ptr<int64_t> sptr;
  ASSERT_DEATH(ptr = ABSL_DIE_IF_NULL(sptr).get(), "");
}
#endif

// Ensures that ABSL_DIE_IF_NULL works with C++11's std::unique_ptr and
// std::shared_ptr.
TEST(AbslDieIfNull, DoesNotCompareSmartPointerToNULL) {
  std::unique_ptr<int> up(new int);
  EXPECT_EQ(&up, &ABSL_DIE_IF_NULL(up));
  ABSL_DIE_IF_NULL(up).reset();

  std::shared_ptr<int> sp(new int);
  EXPECT_EQ(&sp, &ABSL_DIE_IF_NULL(sp));
  ABSL_DIE_IF_NULL(sp).reset();
}

// Verifies that ABSL_DIE_IF_NULL returns an rvalue reference if its argument is
// an rvalue reference.
TEST(AbslDieIfNull, PreservesRValues) {
  int64_t* ptr = new int64_t();
  auto uptr = ABSL_DIE_IF_NULL(std::unique_ptr<int64_t>(ptr));
  EXPECT_EQ(ptr, uptr.get());
}

// Verifies that ABSL_DIE_IF_NULL returns an lvalue if its argument is an
// lvalue.
TEST(AbslDieIfNull, PreservesLValues) {
  int64_t array[2] = {0};
  int64_t* a = array + 0;
  int64_t* b = array + 1;
  using std::swap;
  swap(ABSL_DIE_IF_NULL(a), ABSL_DIE_IF_NULL(b));
  EXPECT_EQ(array + 1, a);
  EXPECT_EQ(array + 0, b);
}

}  // namespace
