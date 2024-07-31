// Copyright 2022 The Abseil Authors
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

#include "absl/strings/internal/cord_data_edge.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_test_util.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::absl::cordrep_testing::MakeExternal;
using ::absl::cordrep_testing::MakeFlat;
using ::absl::cordrep_testing::MakeSubstring;

TEST(CordDataEdgeTest, IsDataEdgeOnFlat) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  EXPECT_TRUE(IsDataEdge(rep));
  CordRep::Unref(rep);
}

TEST(CordDataEdgeTest, IsDataEdgeOnExternal) {
  CordRep* rep = MakeExternal("Lorem ipsum dolor sit amet, consectetur ...");
  EXPECT_TRUE(IsDataEdge(rep));
  CordRep::Unref(rep);
}

TEST(CordDataEdgeTest, IsDataEdgeOnSubstringOfFlat) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  CordRep* substr = MakeSubstring(1, 20, rep);
  EXPECT_TRUE(IsDataEdge(substr));
  CordRep::Unref(substr);
}

TEST(CordDataEdgeTest, IsDataEdgeOnSubstringOfExternal) {
  CordRep* rep = MakeExternal("Lorem ipsum dolor sit amet, consectetur ...");
  CordRep* substr = MakeSubstring(1, 20, rep);
  EXPECT_TRUE(IsDataEdge(substr));
  CordRep::Unref(substr);
}

TEST(CordDataEdgeTest, IsDataEdgeOnBtree) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  CordRepBtree* tree = CordRepBtree::New(rep);
  EXPECT_FALSE(IsDataEdge(tree));
  CordRep::Unref(tree);
}

TEST(CordDataEdgeTest, IsDataEdgeOnBadSubstr) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  CordRep* substr = MakeSubstring(1, 18, MakeSubstring(1, 20, rep));
  EXPECT_FALSE(IsDataEdge(substr));
  CordRep::Unref(substr);
}

TEST(CordDataEdgeTest, EdgeDataOnFlat) {
  absl::string_view value = "Lorem ipsum dolor sit amet, consectetur ...";
  CordRep* rep = MakeFlat(value);
  EXPECT_EQ(EdgeData(rep), value);
  CordRep::Unref(rep);
}

TEST(CordDataEdgeTest, EdgeDataOnExternal) {
  absl::string_view value = "Lorem ipsum dolor sit amet, consectetur ...";
  CordRep* rep = MakeExternal(value);
  EXPECT_EQ(EdgeData(rep), value);
  CordRep::Unref(rep);
}

TEST(CordDataEdgeTest, EdgeDataOnSubstringOfFlat) {
  absl::string_view value = "Lorem ipsum dolor sit amet, consectetur ...";
  CordRep* rep = MakeFlat(value);
  CordRep* substr = MakeSubstring(1, 20, rep);
  EXPECT_EQ(EdgeData(substr), value.substr(1, 20));
  CordRep::Unref(substr);
}

TEST(CordDataEdgeTest, EdgeDataOnSubstringOfExternal) {
  absl::string_view value = "Lorem ipsum dolor sit amet, consectetur ...";
  CordRep* rep = MakeExternal(value);
  CordRep* substr = MakeSubstring(1, 20, rep);
  EXPECT_EQ(EdgeData(substr), value.substr(1, 20));
  CordRep::Unref(substr);
}

#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)

TEST(CordDataEdgeTest, IsDataEdgeOnNullPtr) {
  EXPECT_DEATH(IsDataEdge(nullptr), ".*");
}

TEST(CordDataEdgeTest, EdgeDataOnNullPtr) {
  EXPECT_DEATH(EdgeData(nullptr), ".*");
}

TEST(CordDataEdgeTest, EdgeDataOnBtree) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  CordRepBtree* tree = CordRepBtree::New(rep);
  EXPECT_DEATH(EdgeData(tree), ".*");
  CordRep::Unref(tree);
}

TEST(CordDataEdgeTest, EdgeDataOnBadSubstr) {
  CordRep* rep = MakeFlat("Lorem ipsum dolor sit amet, consectetur ...");
  CordRep* substr = MakeSubstring(1, 18, MakeSubstring(1, 20, rep));
  EXPECT_DEATH(EdgeData(substr), ".*");
  CordRep::Unref(substr);
}

#endif  // GTEST_HAS_DEATH_TEST && !NDEBUG

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
