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

#include "absl/strings/has_absl_stringify.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/types/optional.h"

namespace {

struct TypeWithoutAbslStringify {};

struct TypeWithAbslStringify {
  template <typename Sink>
  friend void AbslStringify(Sink&, const TypeWithAbslStringify&) {}
};

TEST(HasAbslStringifyTest, Works) {
  EXPECT_FALSE(absl::HasAbslStringify<int>::value);
  EXPECT_FALSE(absl::HasAbslStringify<std::string>::value);
  EXPECT_FALSE(absl::HasAbslStringify<TypeWithoutAbslStringify>::value);
  EXPECT_TRUE(absl::HasAbslStringify<TypeWithAbslStringify>::value);
  EXPECT_FALSE(
      absl::HasAbslStringify<absl::optional<TypeWithAbslStringify>>::value);
}

}  // namespace
