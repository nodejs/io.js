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

#include "absl/container/internal/node_slot_policy.h"

#include <memory>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/container/internal/hash_policy_traits.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace container_internal {
namespace {

using ::testing::Pointee;

struct Policy : node_slot_policy<int&, Policy> {
  using key_type = int;
  using init_type = int;

  template <class Alloc>
  static int* new_element(Alloc* alloc, int value) {
    return new int(value);
  }

  template <class Alloc>
  static void delete_element(Alloc* alloc, int* elem) {
    delete elem;
  }
};

using NodePolicy = hash_policy_traits<Policy>;

struct NodeTest : ::testing::Test {
  std::allocator<int> alloc;
  int n = 53;
  int* a = &n;
};

TEST_F(NodeTest, ConstructDestroy) {
  NodePolicy::construct(&alloc, &a, 42);
  EXPECT_THAT(a, Pointee(42));
  NodePolicy::destroy(&alloc, &a);
}

TEST_F(NodeTest, transfer) {
  int s = 42;
  int* b = &s;
  NodePolicy::transfer(&alloc, &a, &b);
  EXPECT_EQ(&s, a);
  EXPECT_TRUE(NodePolicy::transfer_uses_memcpy());
}

}  // namespace
}  // namespace container_internal
ABSL_NAMESPACE_END
}  // namespace absl
