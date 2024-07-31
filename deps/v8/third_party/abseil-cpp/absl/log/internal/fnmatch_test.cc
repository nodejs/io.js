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

#include "absl/log/internal/fnmatch.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {
using ::testing::IsFalse;
using ::testing::IsTrue;

TEST(FNMatchTest, Works) {
  using absl::log_internal::FNMatch;
  EXPECT_THAT(FNMatch("foo", "foo"), IsTrue());
  EXPECT_THAT(FNMatch("foo", "bar"), IsFalse());
  EXPECT_THAT(FNMatch("foo", "fo"), IsFalse());
  EXPECT_THAT(FNMatch("foo", "foo2"), IsFalse());
  EXPECT_THAT(FNMatch("bar/foo.ext", "bar/foo.ext"), IsTrue());
  EXPECT_THAT(FNMatch("*ba*r/fo*o.ext*", "bar/foo.ext"), IsTrue());
  EXPECT_THAT(FNMatch("bar/foo.ext", "bar/baz.ext"), IsFalse());
  EXPECT_THAT(FNMatch("bar/foo.ext", "bar/foo"), IsFalse());
  EXPECT_THAT(FNMatch("bar/foo.ext", "bar/foo.ext.zip"), IsFalse());
  EXPECT_THAT(FNMatch("ba?/*.ext", "bar/foo.ext"), IsTrue());
  EXPECT_THAT(FNMatch("ba?/*.ext", "baZ/FOO.ext"), IsTrue());
  EXPECT_THAT(FNMatch("ba?/*.ext", "barr/foo.ext"), IsFalse());
  EXPECT_THAT(FNMatch("ba?/*.ext", "bar/foo.ext2"), IsFalse());
  EXPECT_THAT(FNMatch("ba?/*", "bar/foo.ext2"), IsTrue());
  EXPECT_THAT(FNMatch("ba?/*", "bar/"), IsTrue());
  EXPECT_THAT(FNMatch("ba?/?", "bar/"), IsFalse());
  EXPECT_THAT(FNMatch("ba?/*", "bar"), IsFalse());
  EXPECT_THAT(FNMatch("?x", "zx"), IsTrue());
  EXPECT_THAT(FNMatch("*b", "aab"), IsTrue());
  EXPECT_THAT(FNMatch("a*b", "aXb"), IsTrue());
  EXPECT_THAT(FNMatch("", ""), IsTrue());
  EXPECT_THAT(FNMatch("", "a"), IsFalse());
  EXPECT_THAT(FNMatch("ab*", "ab"), IsTrue());
  EXPECT_THAT(FNMatch("ab**", "ab"), IsTrue());
  EXPECT_THAT(FNMatch("ab*?", "ab"), IsFalse());
  EXPECT_THAT(FNMatch("*", "bbb"), IsTrue());
  EXPECT_THAT(FNMatch("*", ""), IsTrue());
  EXPECT_THAT(FNMatch("?", ""), IsFalse());
  EXPECT_THAT(FNMatch("***", "**p"), IsTrue());
  EXPECT_THAT(FNMatch("**", "*"), IsTrue());
  EXPECT_THAT(FNMatch("*?", "*"), IsTrue());
}

}  // namespace
