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

// Unit test for memutil.cc

#include "absl/strings/internal/memutil.h"

#include <cstdlib>

#include "gtest/gtest.h"

namespace {

TEST(MemUtil, memcasecmp) {
  // check memutil functions
  const char a[] = "hello there";

  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO there",
                                               sizeof("hello there") - 1),
            0);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO therf",
                                               sizeof("hello there") - 1),
            -1);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "heLLO therf",
                                               sizeof("hello there") - 2),
            0);
  EXPECT_EQ(absl::strings_internal::memcasecmp(a, "whatever", 0), 0);
}

}  // namespace
