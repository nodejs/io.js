// Copyright 2023 The Abseil Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "absl/strings/has_ostream_operator.h"

#include <ostream>
#include <string>

#include "gtest/gtest.h"
#include "absl/types/optional.h"

namespace {

struct TypeWithoutOstreamOp {};

struct TypeWithOstreamOp {
  friend std::ostream& operator<<(std::ostream& os, const TypeWithOstreamOp&) {
    return os;
  }
};

TEST(HasOstreamOperatorTest, Works) {
  EXPECT_TRUE(absl::HasOstreamOperator<int>::value);
  EXPECT_TRUE(absl::HasOstreamOperator<std::string>::value);
  EXPECT_FALSE(absl::HasOstreamOperator<absl::optional<int>>::value);
  EXPECT_FALSE(absl::HasOstreamOperator<TypeWithoutOstreamOp>::value);
  EXPECT_TRUE(absl::HasOstreamOperator<TypeWithOstreamOp>::value);
}

}  // namespace
