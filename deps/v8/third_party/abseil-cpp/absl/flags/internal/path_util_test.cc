//
//  Copyright 2019 The Abseil Authors.
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

#include "absl/flags/internal/path_util.h"

#include "gtest/gtest.h"

namespace {

namespace flags = absl::flags_internal;

TEST(FlagsPathUtilTest, TestBasename) {
  EXPECT_EQ(flags::Basename(""), "");
  EXPECT_EQ(flags::Basename("a.cc"), "a.cc");
  EXPECT_EQ(flags::Basename("dir/a.cc"), "a.cc");
  EXPECT_EQ(flags::Basename("dir1/dir2/a.cc"), "a.cc");
  EXPECT_EQ(flags::Basename("../dir1/dir2/a.cc"), "a.cc");
  EXPECT_EQ(flags::Basename("/dir1/dir2/a.cc"), "a.cc");
  EXPECT_EQ(flags::Basename("/dir1/dir2/../dir3/a.cc"), "a.cc");
}

// --------------------------------------------------------------------

TEST(FlagsPathUtilTest, TestPackage) {
  EXPECT_EQ(flags::Package(""), "");
  EXPECT_EQ(flags::Package("a.cc"), "");
  EXPECT_EQ(flags::Package("dir/a.cc"), "dir/");
  EXPECT_EQ(flags::Package("dir1/dir2/a.cc"), "dir1/dir2/");
  EXPECT_EQ(flags::Package("../dir1/dir2/a.cc"), "../dir1/dir2/");
  EXPECT_EQ(flags::Package("/dir1/dir2/a.cc"), "/dir1/dir2/");
  EXPECT_EQ(flags::Package("/dir1/dir2/../dir3/a.cc"), "/dir1/dir2/../dir3/");
}

}  // namespace
