// Copyright 2017 The Abseil Authors.
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

#include "absl/algorithm/algorithm.h"

#include <algorithm>
#include <list>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace {

class LinearSearchTest : public testing::Test {
 protected:
  LinearSearchTest() : container_{1, 2, 3} {}

  static bool Is3(int n) { return n == 3; }
  static bool Is4(int n) { return n == 4; }

  std::vector<int> container_;
};

TEST_F(LinearSearchTest, linear_search) {
  EXPECT_TRUE(absl::linear_search(container_.begin(), container_.end(), 3));
  EXPECT_FALSE(absl::linear_search(container_.begin(), container_.end(), 4));
}

TEST_F(LinearSearchTest, linear_searchConst) {
  const std::vector<int> *const const_container = &container_;
  EXPECT_TRUE(
      absl::linear_search(const_container->begin(), const_container->end(), 3));
  EXPECT_FALSE(
      absl::linear_search(const_container->begin(), const_container->end(), 4));
}

}  // namespace
