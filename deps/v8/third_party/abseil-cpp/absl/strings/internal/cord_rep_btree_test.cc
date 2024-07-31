// Copyright 2021 The Abseil Authors
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

#include "absl/strings/internal/cord_rep_btree.h"

#include <cmath>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/cleanup/cleanup.h"
#include "absl/strings/internal/cord_data_edge.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_test_util.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {

class CordRepBtreeTestPeer {
 public:
  static void SetEdge(CordRepBtree* node, size_t idx, CordRep* edge) {
    node->edges_[idx] = edge;
  }
  static void AddEdge(CordRepBtree* node, CordRep* edge) {
    node->edges_[node->fetch_add_end(1)] = edge;
  }
};

namespace {

using ::absl::cordrep_testing::AutoUnref;
using ::absl::cordrep_testing::CordCollectRepsIf;
using ::absl::cordrep_testing::CordToString;
using ::absl::cordrep_testing::CordVisitReps;
using ::absl::cordrep_testing::CreateFlatsFromString;
using ::absl::cordrep_testing::CreateRandomString;
using ::absl::cordrep_testing::MakeExternal;
using ::absl::cordrep_testing::MakeFlat;
using ::absl::cordrep_testing::MakeSubstring;
using ::testing::_;
using ::testing::AllOf;
using ::testing::AnyOf;
using ::testing::Conditional;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Le;
using ::testing::Ne;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::TypedEq;

MATCHER_P(EqFlatHolding, data, "Equals flat holding data") {
  if (arg->tag < FLAT) {
    *result_listener << "Expected FLAT, got tag " << static_cast<int>(arg->tag);
    return false;
  }
  std::string actual = CordToString(arg);
  if (actual != data) {
    *result_listener << "Expected flat holding \"" << data
                     << "\", got flat holding \"" << actual << "\"";
    return false;
  }
  return true;
}

MATCHER_P(IsNode, height, absl::StrCat("Is a valid node of height ", height)) {
  if (arg == nullptr) {
    *result_listener << "Expected NODE, got nullptr";
    return false;
  }
  if (arg->tag != BTREE) {
    *result_listener << "Expected NODE, got " << static_cast<int>(arg->tag);
    return false;
  }
  if (!CordRepBtree::IsValid(arg->btree())) {
    CordRepBtree::Dump(arg->btree(), "Expected valid NODE, got:", false,
                       *result_listener->stream());
    return false;
  }
  if (arg->btree()->height() != height) {
    *result_listener << "Expected NODE of height " << height << ", got "
                     << arg->btree()->height();
    return false;
  }
  return true;
}

MATCHER_P2(IsSubstring, start, length,
           absl::StrCat("Is a substring(start = ", start, ", length = ", length,
                        ")")) {
  if (arg == nullptr) {
    *result_listener << "Expected substring, got nullptr";
    return false;
  }
  if (arg->tag != SUBSTRING) {
    *result_listener << "Expected SUBSTRING, got "
                     << static_cast<int>(arg->tag);
    return false;
  }
  const CordRepSubstring* const substr = arg->substring();
  if (substr->start != start || substr->length != length) {
    *result_listener << "Expected substring(" << start << ", " << length
                     << "), got substring(" << substr->start << ", "
                     << substr->length << ")";
    return false;
  }
  return true;
}

MATCHER_P2(EqExtractResult, tree, rep, "Equals ExtractResult") {
  if (arg.tree != tree || arg.extracted != rep) {
    *result_listener << "Expected {" << static_cast<const void*>(tree) << ", "
                     << static_cast<const void*>(rep) << "}, got {" << arg.tree
                     << ", " << arg.extracted << "}";
    return false;
  }
  return true;
}

// DataConsumer is a simple helper class used by tests to 'consume' string
// fragments from the provided input in forward or backward direction.
class DataConsumer {
 public:
  // Starts consumption of `data`. Caller must make sure `data` outlives this
  // instance. Consumes data starting at the front if `forward` is true, else
  // consumes data from the back.
  DataConsumer(absl::string_view data, bool forward)
      : data_(data), forward_(forward) {}

  // Return the next `n` bytes from referenced data.
  absl::string_view Next(size_t n) {
    assert(n <= data_.size() - consumed_);
    consumed_ += n;
    return data_.substr(forward_ ? consumed_ - n : data_.size() - consumed_, n);
  }

  // Returns all data consumed so far.
  absl::string_view Consumed() const {
    return forward_ ? data_.substr(0, consumed_)
                    : data_.substr(data_.size() - consumed_);
  }

 private:
  absl::string_view data_;
  size_t consumed_ = 0;
  bool forward_;
};

// BtreeAdd returns either CordRepBtree::Append or CordRepBtree::Prepend.
CordRepBtree* BtreeAdd(CordRepBtree* node, bool append,
                       absl::string_view data) {
  return append ? CordRepBtree::Append(node, data)
                : CordRepBtree::Prepend(node, data);
}

// Recursively collects all leaf edges from `tree` and appends them to `edges`.
void GetLeafEdges(const CordRepBtree* tree, std::vector<CordRep*>& edges) {
  if (tree->height() == 0) {
    for (CordRep* edge : tree->Edges()) {
      edges.push_back(edge);
    }
  } else {
    for (CordRep* edge : tree->Edges()) {
      GetLeafEdges(edge->btree(), edges);
    }
  }
}

// Recursively collects and returns all leaf edges from `tree`.
std::vector<CordRep*> GetLeafEdges(const CordRepBtree* tree) {
  std::vector<CordRep*> edges;
  GetLeafEdges(tree, edges);
  return edges;
}

// Creates a flat containing the hexadecimal value of `i` zero padded
// to at least 4 digits prefixed with "0x", e.g.: "0x04AC".
CordRepFlat* MakeHexFlat(size_t i) {
  return MakeFlat(absl::StrCat("0x", absl::Hex(i, absl::kZeroPad4)));
}

CordRepBtree* MakeLeaf(size_t size = CordRepBtree::kMaxCapacity) {
  assert(size <= CordRepBtree::kMaxCapacity);
  CordRepBtree* leaf = CordRepBtree::Create(MakeHexFlat(0));
  for (size_t i = 1; i < size; ++i) {
    leaf = CordRepBtree::Append(leaf, MakeHexFlat(i));
  }
  return leaf;
}

CordRepBtree* MakeTree(size_t size, bool append = true) {
  CordRepBtree* tree = CordRepBtree::Create(MakeHexFlat(0));
  for (size_t i = 1; i < size; ++i) {
    tree = append ? CordRepBtree::Append(tree, MakeHexFlat(i))
                  : CordRepBtree::Prepend(tree, MakeHexFlat(i));
  }
  return tree;
}

CordRepBtree* CreateTree(absl::Span<CordRep* const> reps) {
  auto it = reps.begin();
  CordRepBtree* tree = CordRepBtree::Create(*it);
  while (++it != reps.end()) tree = CordRepBtree::Append(tree, *it);
  return tree;
}

CordRepBtree* CreateTree(absl::string_view data, size_t chunk_size) {
  return CreateTree(CreateFlatsFromString(data, chunk_size));
}

CordRepBtree* CreateTreeReverse(absl::string_view data, size_t chunk_size) {
  std::vector<CordRep*> flats = CreateFlatsFromString(data, chunk_size);
  auto rit = flats.rbegin();
  CordRepBtree* tree = CordRepBtree::Create(*rit);
  while (++rit != flats.rend()) tree = CordRepBtree::Prepend(tree, *rit);
  return tree;
}

class CordRepBtreeTest : public testing::TestWithParam<bool> {
 public:
  bool shared() const { return GetParam(); }

  static std::string ToString(testing::TestParamInfo<bool> param) {
    return param.param ? "Shared" : "Private";
  }
};

INSTANTIATE_TEST_SUITE_P(WithParam, CordRepBtreeTest, testing::Bool(),
                         CordRepBtreeTest::ToString);

class CordRepBtreeHeightTest : public testing::TestWithParam<int> {
 public:
  int height() const { return GetParam(); }

  static std::string ToString(testing::TestParamInfo<int> param) {
    return absl::StrCat(param.param);
  }
};

INSTANTIATE_TEST_SUITE_P(WithHeights, CordRepBtreeHeightTest,
                         testing::Range(0, CordRepBtree::kMaxHeight),
                         CordRepBtreeHeightTest::ToString);

using TwoBools = testing::tuple<bool, bool>;

class CordRepBtreeDualTest : public testing::TestWithParam<TwoBools> {
 public:
  bool first_shared() const { return std::get<0>(GetParam()); }
  bool second_shared() const { return std::get<1>(GetParam()); }

  static std::string ToString(testing::TestParamInfo<TwoBools> param) {
    if (std::get<0>(param.param)) {
      return std::get<1>(param.param) ? "BothShared" : "FirstShared";
    }
    return std::get<1>(param.param) ? "SecondShared" : "Private";
  }
};

INSTANTIATE_TEST_SUITE_P(WithParam, CordRepBtreeDualTest,
                         testing::Combine(testing::Bool(), testing::Bool()),
                         CordRepBtreeDualTest::ToString);

TEST(CordRepBtreeTest, SizeIsMultipleOf64) {
  // Only enforce for fully 64-bit platforms.
  if (sizeof(size_t) == 8 && sizeof(void*) == 8) {
    EXPECT_THAT(sizeof(CordRepBtree) % 64, Eq(0u))
        << "Should be multiple of 64";
  }
}

TEST(CordRepBtreeTest, NewDestroyEmptyTree) {
  auto* tree = CordRepBtree::New();
  EXPECT_THAT(tree->size(), Eq(0u));
  EXPECT_THAT(tree->height(), Eq(0));
  EXPECT_THAT(tree->Edges(), ElementsAre());
  CordRepBtree::Destroy(tree);
}

TEST(CordRepBtreeTest, NewDestroyEmptyTreeAtHeight) {
  auto* tree = CordRepBtree::New(3);
  EXPECT_THAT(tree->size(), Eq(0u));
  EXPECT_THAT(tree->height(), Eq(3));
  EXPECT_THAT(tree->Edges(), ElementsAre());
  CordRepBtree::Destroy(tree);
}

TEST(CordRepBtreeTest, Btree) {
  CordRep* rep = CordRepBtree::New();
  EXPECT_THAT(rep->btree(), Eq(rep));
  EXPECT_THAT(static_cast<const CordRep*>(rep)->btree(), Eq(rep));
  CordRep::Unref(rep);
#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)
  rep = MakeFlat("Hello world");
  EXPECT_DEATH(rep->btree(), ".*");
  EXPECT_DEATH(static_cast<const CordRep*>(rep)->btree(), ".*");
  CordRep::Unref(rep);
#endif
}

TEST(CordRepBtreeTest, EdgeData) {
  CordRepFlat* flat = MakeFlat("Hello world");
  CordRepExternal* external = MakeExternal("Hello external");
  CordRep* substr1 = MakeSubstring(1, 6, CordRep::Ref(flat));
  CordRep* substr2 = MakeSubstring(1, 6, CordRep::Ref(external));
  CordRep* bad_substr = MakeSubstring(1, 2, CordRep::Ref(substr1));

  EXPECT_TRUE(IsDataEdge(flat));
  EXPECT_THAT(EdgeData(flat).data(), TypedEq<const void*>(flat->Data()));
  EXPECT_THAT(EdgeData(flat), Eq("Hello world"));

  EXPECT_TRUE(IsDataEdge(external));
  EXPECT_THAT(EdgeData(external).data(), TypedEq<const void*>(external->base));
  EXPECT_THAT(EdgeData(external), Eq("Hello external"));

  EXPECT_TRUE(IsDataEdge(substr1));
  EXPECT_THAT(EdgeData(substr1).data(), TypedEq<const void*>(flat->Data() + 1));
  EXPECT_THAT(EdgeData(substr1), Eq("ello w"));

  EXPECT_TRUE(IsDataEdge(substr2));
  EXPECT_THAT(EdgeData(substr2).data(),
              TypedEq<const void*>(external->base + 1));
  EXPECT_THAT(EdgeData(substr2), Eq("ello e"));

  EXPECT_FALSE(IsDataEdge(bad_substr));
#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)
  EXPECT_DEATH(EdgeData(bad_substr), ".*");
#endif

  CordRep::Unref(bad_substr);
  CordRep::Unref(substr2);
  CordRep::Unref(substr1);
  CordRep::Unref(external);
  CordRep::Unref(flat);
}

TEST(CordRepBtreeTest, CreateUnrefLeaf) {
  auto* flat = MakeFlat("a");
  auto* leaf = CordRepBtree::Create(flat);
  EXPECT_THAT(leaf->size(), Eq(1u));
  EXPECT_THAT(leaf->height(), Eq(0));
  EXPECT_THAT(leaf->Edges(), ElementsAre(flat));
  CordRepBtree::Unref(leaf);
}

TEST(CordRepBtreeTest, NewUnrefNode) {
  auto* leaf = CordRepBtree::Create(MakeFlat("a"));
  CordRepBtree* tree = CordRepBtree::New(leaf);
  EXPECT_THAT(tree->size(), Eq(1u));
  EXPECT_THAT(tree->height(), Eq(1));
  EXPECT_THAT(tree->Edges(), ElementsAre(leaf));
  CordRepBtree::Unref(tree);
}

TEST_P(CordRepBtreeTest, AppendToLeafToCapacity) {
  AutoUnref refs;
  std::vector<CordRep*> flats;
  flats.push_back(MakeHexFlat(0));
  auto* leaf = CordRepBtree::Create(flats.back());

  for (size_t i = 1; i < CordRepBtree::kMaxCapacity; ++i) {
    refs.RefIf(shared(), leaf);
    flats.push_back(MakeHexFlat(i));
    auto* result = CordRepBtree::Append(leaf, flats.back());
    EXPECT_THAT(result->height(), Eq(0));
    EXPECT_THAT(result, Conditional(shared(), Ne(leaf), Eq(leaf)));
    EXPECT_THAT(result->Edges(), ElementsAreArray(flats));
    leaf = result;
  }
  CordRep::Unref(leaf);
}

TEST_P(CordRepBtreeTest, PrependToLeafToCapacity) {
  AutoUnref refs;
  std::deque<CordRep*> flats;
  flats.push_front(MakeHexFlat(0));
  auto* leaf = CordRepBtree::Create(flats.front());

  for (size_t i = 1; i < CordRepBtree::kMaxCapacity; ++i) {
    refs.RefIf(shared(), leaf);
    flats.push_front(MakeHexFlat(i));
    auto* result = CordRepBtree::Prepend(leaf, flats.front());
    EXPECT_THAT(result->height(), Eq(0));
    EXPECT_THAT(result, Conditional(shared(), Ne(leaf), Eq(leaf)));
    EXPECT_THAT(result->Edges(), ElementsAreArray(flats));
    leaf = result;
  }
  CordRep::Unref(leaf);
}

// This test specifically aims at code aligning data at either the front or the
// back of the contained `edges[]` array, alternating Append and Prepend will
// move `begin()` and `end()` values as needed for each added value.
TEST_P(CordRepBtreeTest, AppendPrependToLeafToCapacity) {
  AutoUnref refs;
  std::deque<CordRep*> flats;
  flats.push_front(MakeHexFlat(0));
  auto* leaf = CordRepBtree::Create(flats.front());

  for (size_t i = 1; i < CordRepBtree::kMaxCapacity; ++i) {
    refs.RefIf(shared(), leaf);
    CordRepBtree* result;
    if (i % 2 != 0) {
      flats.push_front(MakeHexFlat(i));
      result = CordRepBtree::Prepend(leaf, flats.front());
    } else {
      flats.push_back(MakeHexFlat(i));
      result = CordRepBtree::Append(leaf, flats.back());
    }
    EXPECT_THAT(result->height(), Eq(0));
    EXPECT_THAT(result, Conditional(shared(), Ne(leaf), Eq(leaf)));
    EXPECT_THAT(result->Edges(), ElementsAreArray(flats));
    leaf = result;
  }
  CordRep::Unref(leaf);
}

TEST_P(CordRepBtreeTest, AppendToLeafBeyondCapacity) {
  AutoUnref refs;
  auto* leaf = MakeLeaf();
  refs.RefIf(shared(), leaf);
  CordRep* flat = MakeFlat("abc");
  auto* result = CordRepBtree::Append(leaf, flat);
  ASSERT_THAT(result, IsNode(1));
  EXPECT_THAT(result, Ne(leaf));
  absl::Span<CordRep* const> edges = result->Edges();
  ASSERT_THAT(edges, ElementsAre(leaf, IsNode(0)));
  EXPECT_THAT(edges[1]->btree()->Edges(), ElementsAre(flat));
  CordRep::Unref(result);
}

TEST_P(CordRepBtreeTest, PrependToLeafBeyondCapacity) {
  AutoUnref refs;
  auto* leaf = MakeLeaf();
  refs.RefIf(shared(), leaf);
  CordRep* flat = MakeFlat("abc");
  auto* result = CordRepBtree::Prepend(leaf, flat);
  ASSERT_THAT(result, IsNode(1));
  EXPECT_THAT(result, Ne(leaf));
  absl::Span<CordRep* const> edges = result->Edges();
  ASSERT_THAT(edges, ElementsAre(IsNode(0), leaf));
  EXPECT_THAT(edges[0]->btree()->Edges(), ElementsAre(flat));
  CordRep::Unref(result);
}

TEST_P(CordRepBtreeTest, AppendToTreeOneDeep) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  AutoUnref refs;
  std::vector<CordRep*> flats;
  flats.push_back(MakeHexFlat(0));
  CordRepBtree* tree = CordRepBtree::Create(flats.back());
  for (size_t i = 1; i <= max_cap; ++i) {
    flats.push_back(MakeHexFlat(i));
    tree = CordRepBtree::Append(tree, flats.back());
  }
  ASSERT_THAT(tree, IsNode(1));

  for (size_t i = max_cap + 1; i < max_cap * max_cap; ++i) {
    // Ref top level tree based on param.
    // Ref leaf node once every 4 iterations, which should not have an
    // observable effect other than that the leaf itself is copied.
    refs.RefIf(shared(), tree);
    refs.RefIf(i % 4 == 0, tree->Edges().back());

    flats.push_back(MakeHexFlat(i));
    CordRepBtree* result = CordRepBtree::Append(tree, flats.back());
    ASSERT_THAT(result, IsNode(1));
    ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
    std::vector<CordRep*> edges = GetLeafEdges(result);
    ASSERT_THAT(edges, ElementsAreArray(flats));
    tree = result;
  }
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeTest, AppendToTreeTwoDeep) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  AutoUnref refs;
  std::vector<CordRep*> flats;
  flats.push_back(MakeHexFlat(0));
  CordRepBtree* tree = CordRepBtree::Create(flats.back());
  for (size_t i = 1; i <= max_cap * max_cap; ++i) {
    flats.push_back(MakeHexFlat(i));
    tree = CordRepBtree::Append(tree, flats.back());
  }
  ASSERT_THAT(tree, IsNode(2));
  for (size_t i = max_cap * max_cap + 1; i < max_cap * max_cap * max_cap; ++i) {
    // Ref top level tree based on param.
    // Ref child node once every 16 iterations, and leaf node every 4
    // iterations which  which should not have an observable effect other than
    //  the node and/or the leaf below it being copied.
    refs.RefIf(shared(), tree);
    refs.RefIf(i % 16 == 0, tree->Edges().back());
    refs.RefIf(i % 4 == 0, tree->Edges().back()->btree()->Edges().back());

    flats.push_back(MakeHexFlat(i));
    CordRepBtree* result = CordRepBtree::Append(tree, flats.back());
    ASSERT_THAT(result, IsNode(2));
    ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
    std::vector<CordRep*> edges = GetLeafEdges(result);
    ASSERT_THAT(edges, ElementsAreArray(flats));
    tree = result;
  }
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeTest, PrependToTreeOneDeep) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  AutoUnref refs;
  std::deque<CordRep*> flats;
  flats.push_back(MakeHexFlat(0));
  CordRepBtree* tree = CordRepBtree::Create(flats.back());
  for (size_t i = 1; i <= max_cap; ++i) {
    flats.push_front(MakeHexFlat(i));
    tree = CordRepBtree::Prepend(tree, flats.front());
  }
  ASSERT_THAT(tree, IsNode(1));

  for (size_t i = max_cap + 1; i < max_cap * max_cap; ++i) {
    // Ref top level tree based on param.
    // Ref leaf node once every 4 iterations which should not have an observable
    // effect other than than the leaf itself is copied.
    refs.RefIf(shared(), tree);
    refs.RefIf(i % 4 == 0, tree->Edges().back());

    flats.push_front(MakeHexFlat(i));
    CordRepBtree* result = CordRepBtree::Prepend(tree, flats.front());
    ASSERT_THAT(result, IsNode(1));
    ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
    std::vector<CordRep*> edges = GetLeafEdges(result);
    ASSERT_THAT(edges, ElementsAreArray(flats));
    tree = result;
  }
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeTest, PrependToTreeTwoDeep) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  AutoUnref refs;
  std::deque<CordRep*> flats;
  flats.push_back(MakeHexFlat(0));
  CordRepBtree* tree = CordRepBtree::Create(flats.back());
  for (size_t i = 1; i <= max_cap * max_cap; ++i) {
    flats.push_front(MakeHexFlat(i));
    tree = CordRepBtree::Prepend(tree, flats.front());
  }
  ASSERT_THAT(tree, IsNode(2));
  for (size_t i = max_cap * max_cap + 1; i < max_cap * max_cap * max_cap; ++i) {
    // Ref top level tree based on param.
    // Ref child node once every 16 iterations, and leaf node every 4
    // iterations which  which should not have an observable effect other than
    //  the node and/or the leaf below it being copied.
    refs.RefIf(shared(), tree);
    refs.RefIf(i % 16 == 0, tree->Edges().back());
    refs.RefIf(i % 4 == 0, tree->Edges().back()->btree()->Edges().back());

    flats.push_front(MakeHexFlat(i));
    CordRepBtree* result = CordRepBtree::Prepend(tree, flats.front());
    ASSERT_THAT(result, IsNode(2));
    ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
    std::vector<CordRep*> edges = GetLeafEdges(result);
    ASSERT_THAT(edges, ElementsAreArray(flats));
    tree = result;
  }
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeDualTest, MergeLeafsNotExceedingCapacity) {
  for (bool use_append : {false, true}) {
    SCOPED_TRACE(use_append ? "Using Append" : "Using Prepend");

    AutoUnref refs;
    std::vector<CordRep*> flats;

    // Build `left` side leaf appending all contained flats to `flats`
    CordRepBtree* left = MakeLeaf(3);
    GetLeafEdges(left, flats);
    refs.RefIf(first_shared(), left);

    // Build `right` side leaf appending all contained flats to `flats`
    CordRepBtree* right = MakeLeaf(2);
    GetLeafEdges(right, flats);
    refs.RefIf(second_shared(), right);

    CordRepBtree* tree = use_append ? CordRepBtree::Append(left, right)
                                    : CordRepBtree::Prepend(right, left);
    EXPECT_THAT(tree, IsNode(0));

    // `tree` contains all flats originally belonging to `left` and `right`.
    EXPECT_THAT(tree->Edges(), ElementsAreArray(flats));
    CordRepBtree::Unref(tree);
  }
}

TEST_P(CordRepBtreeDualTest, MergeLeafsExceedingCapacity) {
  for (bool use_append : {false, true}) {
    SCOPED_TRACE(use_append ? "Using Append" : "Using Prepend");

    AutoUnref refs;

    // Build `left` side tree appending all contained flats to `flats`
    CordRepBtree* left = MakeLeaf(CordRepBtree::kMaxCapacity - 2);
    refs.RefIf(first_shared(), left);

    // Build `right` side tree appending all contained flats to `flats`
    CordRepBtree* right = MakeLeaf(CordRepBtree::kMaxCapacity - 1);
    refs.RefIf(second_shared(), right);

    CordRepBtree* tree = use_append ? CordRepBtree::Append(left, right)
                                    : CordRepBtree::Prepend(right, left);
    EXPECT_THAT(tree, IsNode(1));
    EXPECT_THAT(tree->Edges(), ElementsAre(left, right));
    CordRepBtree::Unref(tree);
  }
}

TEST_P(CordRepBtreeDualTest, MergeEqualHeightTrees) {
  for (bool use_append : {false, true}) {
    SCOPED_TRACE(use_append ? "Using Append" : "Using Prepend");

    AutoUnref refs;
    std::vector<CordRep*> flats;

    // Build `left` side tree appending all contained flats to `flats`
    CordRepBtree* left = MakeTree(CordRepBtree::kMaxCapacity * 3);
    GetLeafEdges(left, flats);
    refs.RefIf(first_shared(), left);

    // Build `right` side tree appending all contained flats to `flats`
    CordRepBtree* right = MakeTree(CordRepBtree::kMaxCapacity * 2);
    GetLeafEdges(right, flats);
    refs.RefIf(second_shared(), right);

    CordRepBtree* tree = use_append ? CordRepBtree::Append(left, right)
                                    : CordRepBtree::Prepend(right, left);
    EXPECT_THAT(tree, IsNode(1));
    EXPECT_THAT(tree->Edges(), SizeIs(5u));

    // `tree` contains all flats originally belonging to `left` and `right`.
    EXPECT_THAT(GetLeafEdges(tree), ElementsAreArray(flats));
    CordRepBtree::Unref(tree);
  }
}

TEST_P(CordRepBtreeDualTest, MergeLeafWithTreeNotExceedingLeafCapacity) {
  for (bool use_append : {false, true}) {
    SCOPED_TRACE(use_append ? "Using Append" : "Using Prepend");

    AutoUnref refs;
    std::vector<CordRep*> flats;

    // Build `left` side tree appending all added flats to `flats`
    CordRepBtree* left = MakeTree(CordRepBtree::kMaxCapacity * 2 + 2);
    GetLeafEdges(left, flats);
    refs.RefIf(first_shared(), left);

    // Build `right` side tree appending all added flats to `flats`
    CordRepBtree* right = MakeTree(3);
    GetLeafEdges(right, flats);
    refs.RefIf(second_shared(), right);

    CordRepBtree* tree = use_append ? CordRepBtree::Append(left, right)
                                    : CordRepBtree::Prepend(right, left);
    EXPECT_THAT(tree, IsNode(1));
    EXPECT_THAT(tree->Edges(), SizeIs(3u));

    // `tree` contains all flats originally belonging to `left` and `right`.
    EXPECT_THAT(GetLeafEdges(tree), ElementsAreArray(flats));
    CordRepBtree::Unref(tree);
  }
}

TEST_P(CordRepBtreeDualTest, MergeLeafWithTreeExceedingLeafCapacity) {
  for (bool use_append : {false, true}) {
    SCOPED_TRACE(use_append ? "Using Append" : "Using Prepend");

    AutoUnref refs;
    std::vector<CordRep*> flats;

    // Build `left` side tree appending all added flats to `flats`
    CordRepBtree* left = MakeTree(CordRepBtree::kMaxCapacity * 3 - 2);
    GetLeafEdges(left, flats);
    refs.RefIf(first_shared(), left);

    // Build `right` side tree appending all added flats to `flats`
    CordRepBtree* right = MakeTree(3);
    GetLeafEdges(right, flats);
    refs.RefIf(second_shared(), right);

    CordRepBtree* tree = use_append ? CordRepBtree::Append(left, right)
                                    : CordRepBtree::Prepend(right, left);
    EXPECT_THAT(tree, IsNode(1));
    EXPECT_THAT(tree->Edges(), SizeIs(4u));

    // `tree` contains all flats originally belonging to `left` and `right`.
    EXPECT_THAT(GetLeafEdges(tree), ElementsAreArray(flats));
    CordRepBtree::Unref(tree);
  }
}

void RefEdgesAt(size_t depth, AutoUnref& refs, CordRepBtree* tree) {
  absl::Span<CordRep* const> edges = tree->Edges();
  if (depth == 0) {
    refs.Ref(edges.front());
    refs.Ref(edges.back());
  } else {
    assert(tree->height() > 0);
    RefEdgesAt(depth - 1, refs, edges.front()->btree());
    RefEdgesAt(depth - 1, refs, edges.back()->btree());
  }
}

TEST(CordRepBtreeTest, MergeFuzzTest) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  std::minstd_rand rnd;
  std::uniform_int_distribution<int> coin_flip(0, 1);
  std::uniform_int_distribution<int> dice_throw(1, 6);

  auto random_leaf_count = [&]() {
    std::uniform_int_distribution<int> dist_height(0, 3);
    std::uniform_int_distribution<int> dist_leaf(0, max_cap - 1);
    const int height = dist_height(rnd);
    return (height ? pow(max_cap, height) : 0) + dist_leaf(rnd);
  };

  for (int i = 0; i < 10000; ++i) {
    AutoUnref refs;
    std::vector<CordRep*> flats;

    CordRepBtree* left = MakeTree(random_leaf_count(), coin_flip(rnd));
    GetLeafEdges(left, flats);
    if (dice_throw(rnd) == 1) {
      std::uniform_int_distribution<size_t> dist(
          0, static_cast<size_t>(left->height()));
      RefEdgesAt(dist(rnd), refs, left);
    }

    CordRepBtree* right = MakeTree(random_leaf_count(), coin_flip(rnd));
    GetLeafEdges(right, flats);
    if (dice_throw(rnd) == 1) {
      std::uniform_int_distribution<size_t> dist(
          0, static_cast<size_t>(right->height()));
      RefEdgesAt(dist(rnd), refs, right);
    }

    CordRepBtree* tree = CordRepBtree::Append(left, right);
    EXPECT_THAT(GetLeafEdges(tree), ElementsAreArray(flats));
    CordRepBtree::Unref(tree);
  }
}

TEST_P(CordRepBtreeTest, RemoveSuffix) {
  // Create tree of 1, 2 and 3 levels high
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  for (size_t cap : {max_cap - 1, max_cap * 2, max_cap * max_cap * 2}) {
    const std::string data = CreateRandomString(cap * 512);

    {
      // Verify RemoveSuffix(<all>)
      AutoUnref refs;
      CordRepBtree* node = refs.RefIf(shared(), CreateTree(data, 512));
      EXPECT_THAT(CordRepBtree::RemoveSuffix(node, data.length()), Eq(nullptr));

      // Verify RemoveSuffix(<none>)
      node = refs.RefIf(shared(), CreateTree(data, 512));
      EXPECT_THAT(CordRepBtree::RemoveSuffix(node, 0), Eq(node));
      CordRep::Unref(node);
    }

    for (size_t n = 1; n < data.length(); ++n) {
      AutoUnref refs;
      auto flats = CreateFlatsFromString(data, 512);
      CordRepBtree* node = refs.RefIf(shared(), CreateTree(flats));
      CordRep* rep = refs.Add(CordRepBtree::RemoveSuffix(node, n));
      EXPECT_THAT(CordToString(rep), Eq(data.substr(0, data.length() - n)));

      // Collect all flats
      auto is_flat = [](CordRep* rep) { return rep->tag >= FLAT; };
      std::vector<CordRep*> edges = CordCollectRepsIf(is_flat, rep);
      ASSERT_THAT(edges.size(), Le(flats.size()));

      // Isolate last edge
      CordRep* last_edge = edges.back();
      edges.pop_back();
      const size_t last_length = rep->length - edges.size() * 512;

      // All flats except the last edge must be kept or copied 'as is'
      size_t index = 0;
      for (CordRep* edge : edges) {
        ASSERT_THAT(edge, Eq(flats[index++]));
        ASSERT_THAT(edge->length, Eq(512u));
      }

      // CordRepBtree may optimize small substrings to avoid waste, so only
      // check for flat sharing / updates where the code should always do this.
      if (last_length >= 500) {
        EXPECT_THAT(last_edge, Eq(flats[index++]));
        if (shared()) {
          EXPECT_THAT(last_edge->length, Eq(512u));
        } else {
          EXPECT_TRUE(last_edge->refcount.IsOne());
          EXPECT_THAT(last_edge->length, Eq(last_length));
        }
      }
    }
  }
}

TEST(CordRepBtreeTest, SubTree) {
  // Create tree of at least 2 levels high
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  const size_t n = max_cap * max_cap * 2;
  const std::string data = CreateRandomString(n * 3);
  std::vector<CordRep*> flats;
  for (absl::string_view s = data; !s.empty(); s.remove_prefix(3)) {
    flats.push_back(MakeFlat(s.substr(0, 3)));
  }
  CordRepBtree* node = CordRepBtree::Create(CordRep::Ref(flats[0]));
  for (size_t i = 1; i < flats.size(); ++i) {
    node = CordRepBtree::Append(node, CordRep::Ref(flats[i]));
  }

  for (size_t offset = 0; offset < data.length(); ++offset) {
    for (size_t length = 1; length <= data.length() - offset; ++length) {
      CordRep* rep = node->SubTree(offset, length);
      EXPECT_THAT(CordToString(rep), Eq(data.substr(offset, length)));
      CordRep::Unref(rep);
    }
  }
  CordRepBtree::Unref(node);
  for (CordRep* rep : flats) {
    CordRep::Unref(rep);
  }
}

TEST(CordRepBtreeTest, SubTreeOnExistingSubstring) {
  // This test verifies that a SubTree call on a pre-existing (large) substring
  // adjusts the existing substring if not shared, and else rewrites the
  // existing substring.
  AutoUnref refs;
  std::string data = CreateRandomString(1000);
  CordRepBtree* leaf = CordRepBtree::Create(MakeFlat("abc"));
  CordRep* flat = MakeFlat(data);
  leaf = CordRepBtree::Append(leaf, flat);

  // Setup tree containing substring.
  CordRep* result = leaf->SubTree(0, 3 + 990);
  ASSERT_THAT(result->tag, Eq(BTREE));
  CordRep::Unref(leaf);
  leaf = result->btree();
  ASSERT_THAT(leaf->Edges(), ElementsAre(_, IsSubstring(0u, 990u)));
  EXPECT_THAT(leaf->Edges()[1]->substring()->child, Eq(flat));

  // Verify substring of substring.
  result = leaf->SubTree(3 + 5, 970);
  ASSERT_THAT(result, IsSubstring(5u, 970u));
  EXPECT_THAT(result->substring()->child, Eq(flat));
  CordRep::Unref(result);

  CordRep::Unref(leaf);
}

TEST_P(CordRepBtreeTest, AddDataToLeaf) {
  const size_t n = CordRepBtree::kMaxCapacity;
  const std::string data = CreateRandomString(n * 3);

  for (bool append : {true, false}) {
    AutoUnref refs;
    DataConsumer consumer(data, append);
    SCOPED_TRACE(append ? "Append" : "Prepend");

    CordRepBtree* leaf = CordRepBtree::Create(MakeFlat(consumer.Next(3)));
    for (size_t i = 1; i < n; ++i) {
      refs.RefIf(shared(), leaf);
      CordRepBtree* result = BtreeAdd(leaf, append, consumer.Next(3));
      EXPECT_THAT(result, Conditional(shared(), Ne(leaf), Eq(leaf)));
      EXPECT_THAT(CordToString(result), Eq(consumer.Consumed()));
      leaf = result;
    }
    CordRep::Unref(leaf);
  }
}

TEST_P(CordRepBtreeTest, AppendDataToTree) {
  AutoUnref refs;
  size_t n = CordRepBtree::kMaxCapacity + CordRepBtree::kMaxCapacity / 2;
  std::string data = CreateRandomString(n * 3);
  CordRepBtree* tree = refs.RefIf(shared(), CreateTree(data, 3));
  CordRepBtree* leaf0 = tree->Edges()[0]->btree();
  CordRepBtree* leaf1 = tree->Edges()[1]->btree();
  CordRepBtree* result = CordRepBtree::Append(tree, "123456789");
  EXPECT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
  EXPECT_THAT(result->Edges(),
              ElementsAre(leaf0, Conditional(shared(), Ne(leaf1), Eq(leaf1))));
  EXPECT_THAT(CordToString(result), Eq(data + "123456789"));
  CordRep::Unref(result);
}

TEST_P(CordRepBtreeTest, PrependDataToTree) {
  AutoUnref refs;
  size_t n = CordRepBtree::kMaxCapacity + CordRepBtree::kMaxCapacity / 2;
  std::string data = CreateRandomString(n * 3);
  CordRepBtree* tree = refs.RefIf(shared(), CreateTreeReverse(data, 3));
  CordRepBtree* leaf0 = tree->Edges()[0]->btree();
  CordRepBtree* leaf1 = tree->Edges()[1]->btree();
  CordRepBtree* result = CordRepBtree::Prepend(tree, "123456789");
  EXPECT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
  EXPECT_THAT(result->Edges(),
              ElementsAre(Conditional(shared(), Ne(leaf0), Eq(leaf0)), leaf1));
  EXPECT_THAT(CordToString(result), Eq("123456789" + data));
  CordRep::Unref(result);
}

TEST_P(CordRepBtreeTest, AddDataToTreeThreeLevelsDeep) {
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  const size_t n = max_cap * max_cap * max_cap;
  const std::string data = CreateRandomString(n * 3);

  for (bool append : {true, false}) {
    AutoUnref refs;
    DataConsumer consumer(data, append);
    SCOPED_TRACE(append ? "Append" : "Prepend");

    // Fill leaf
    CordRepBtree* tree = CordRepBtree::Create(MakeFlat(consumer.Next(3)));
    for (size_t i = 1; i < max_cap; ++i) {
      tree = BtreeAdd(tree, append, consumer.Next(3));
    }
    ASSERT_THAT(CordToString(tree), Eq(consumer.Consumed()));

    // Fill to maximum at one deep
    refs.RefIf(shared(), tree);
    CordRepBtree* result = BtreeAdd(tree, append, consumer.Next(3));
    ASSERT_THAT(result, IsNode(1));
    ASSERT_THAT(result, Ne(tree));
    ASSERT_THAT(CordToString(result), Eq(consumer.Consumed()));
    tree = result;
    for (size_t i = max_cap + 1; i < max_cap * max_cap; ++i) {
      refs.RefIf(shared(), tree);
      result = BtreeAdd(tree, append, consumer.Next(3));
      ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
      ASSERT_THAT(CordToString(result), Eq(consumer.Consumed()));
      tree = result;
    }

    // Fill to maximum at two deep
    refs.RefIf(shared(), tree);
    result = BtreeAdd(tree, append, consumer.Next(3));
    ASSERT_THAT(result, IsNode(2));
    ASSERT_THAT(result, Ne(tree));
    ASSERT_THAT(CordToString(result), Eq(consumer.Consumed()));
    tree = result;
    for (size_t i = max_cap * max_cap + 1; i < max_cap * max_cap * max_cap;
         ++i) {
      refs.RefIf(shared(), tree);
      result = BtreeAdd(tree, append, consumer.Next(3));
      ASSERT_THAT(result, Conditional(shared(), Ne(tree), Eq(tree)));
      ASSERT_THAT(CordToString(result), Eq(consumer.Consumed()));
      tree = result;
    }

    CordRep::Unref(tree);
  }
}

TEST_P(CordRepBtreeTest, AddLargeDataToLeaf) {
  const size_t max_cap = CordRepBtree::kMaxCapacity;
  const size_t n = max_cap * max_cap * max_cap * 3 + 2;
  const std::string data = CreateRandomString(n * kMaxFlatLength);

  for (bool append : {true, false}) {
    AutoUnref refs;
    SCOPED_TRACE(append ? "Append" : "Prepend");

    CordRepBtree* leaf = CordRepBtree::Create(MakeFlat("abc"));
    refs.RefIf(shared(), leaf);
    CordRepBtree* result = BtreeAdd(leaf, append, data);
    EXPECT_THAT(CordToString(result), Eq(append ? "abc" + data : data + "abc"));
    CordRep::Unref(result);
  }
}

TEST_P(CordRepBtreeTest, CreateFromTreeReturnsTree) {
  AutoUnref refs;
  CordRepBtree* leaf = CordRepBtree::Create(MakeFlat("Hello world"));
  refs.RefIf(shared(), leaf);
  CordRepBtree* result = CordRepBtree::Create(leaf);
  EXPECT_THAT(result, Eq(leaf));
  CordRep::Unref(result);
}

TEST(CordRepBtreeTest, GetCharacter) {
  size_t n = CordRepBtree::kMaxCapacity * CordRepBtree::kMaxCapacity + 2;
  std::string data = CreateRandomString(n * 3);
  CordRepBtree* tree = CreateTree(data, 3);
  // Add a substring node for good measure.
  tree = tree->Append(tree, MakeSubstring(4, 5, MakeFlat("abcdefghijklm")));
  data += "efghi";
  for (size_t i = 0; i < data.length(); ++i) {
    ASSERT_THAT(tree->GetCharacter(i), Eq(data[i]));
  }
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeTest, IsFlatSingleFlat) {
  CordRepBtree* leaf = CordRepBtree::Create(MakeFlat("Hello world"));

  absl::string_view fragment;
  EXPECT_TRUE(leaf->IsFlat(nullptr));
  EXPECT_TRUE(leaf->IsFlat(&fragment));
  EXPECT_THAT(fragment, Eq("Hello world"));
  fragment = "";
  EXPECT_TRUE(leaf->IsFlat(0, 11, nullptr));
  EXPECT_TRUE(leaf->IsFlat(0, 11, &fragment));
  EXPECT_THAT(fragment, Eq("Hello world"));

  // Arbitrary ranges must check true as well.
  EXPECT_TRUE(leaf->IsFlat(1, 4, &fragment));
  EXPECT_THAT(fragment, Eq("ello"));
  EXPECT_TRUE(leaf->IsFlat(6, 5, &fragment));
  EXPECT_THAT(fragment, Eq("world"));

  CordRep::Unref(leaf);
}

TEST(CordRepBtreeTest, IsFlatMultiFlat) {
  size_t n = CordRepBtree::kMaxCapacity * CordRepBtree::kMaxCapacity + 2;
  std::string data = CreateRandomString(n * 3);
  CordRepBtree* tree = CreateTree(data, 3);
  // Add substring nodes for good measure.
  tree = tree->Append(tree, MakeSubstring(4, 3, MakeFlat("abcdefghijklm")));
  tree = tree->Append(tree, MakeSubstring(8, 3, MakeFlat("abcdefghijklm")));
  data += "efgijk";

  EXPECT_FALSE(tree->IsFlat(nullptr));
  absl::string_view fragment = "Can't touch this";
  EXPECT_FALSE(tree->IsFlat(&fragment));
  EXPECT_THAT(fragment, Eq("Can't touch this"));

  for (size_t offset = 0; offset < data.size(); offset += 3) {
    EXPECT_TRUE(tree->IsFlat(offset, 3, nullptr));
    EXPECT_TRUE(tree->IsFlat(offset, 3, &fragment));
    EXPECT_THAT(fragment, Eq(data.substr(offset, 3)));

    fragment = "Can't touch this";
    if (offset > 0) {
      EXPECT_FALSE(tree->IsFlat(offset - 1, 4, nullptr));
      EXPECT_FALSE(tree->IsFlat(offset - 1, 4, &fragment));
      EXPECT_THAT(fragment, Eq("Can't touch this"));
    }
    if (offset < data.size() - 4) {
      EXPECT_FALSE(tree->IsFlat(offset, 4, nullptr));
      EXPECT_FALSE(tree->IsFlat(offset, 4, &fragment));
      EXPECT_THAT(fragment, Eq("Can't touch this"));
    }
  }

  CordRep::Unref(tree);
}

#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)

TEST_P(CordRepBtreeHeightTest, GetAppendBufferNotPrivate) {
  CordRepBtree* tree = CordRepBtree::Create(MakeExternal("Foo"));
  CordRepBtree::Ref(tree);
  EXPECT_DEATH(tree->GetAppendBuffer(1), ".*");
  CordRepBtree::Unref(tree);
  CordRepBtree::Unref(tree);
}

#endif  // defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)

TEST_P(CordRepBtreeHeightTest, GetAppendBufferNotFlat) {
  CordRepBtree* tree = CordRepBtree::Create(MakeExternal("Foo"));
  for (int i = 1; i <= height(); ++i) {
    tree = CordRepBtree::New(tree);
  }
  EXPECT_THAT(tree->GetAppendBuffer(1), SizeIs(0u));
  CordRepBtree::Unref(tree);
}

TEST_P(CordRepBtreeHeightTest, GetAppendBufferFlatNotPrivate) {
  CordRepFlat* flat = MakeFlat("abc");
  CordRepBtree* tree = CordRepBtree::Create(CordRep::Ref(flat));
  for (int i = 1; i <= height(); ++i) {
    tree = CordRepBtree::New(tree);
  }
  EXPECT_THAT(tree->GetAppendBuffer(1), SizeIs(0u));
  CordRepBtree::Unref(tree);
  CordRep::Unref(flat);
}

TEST_P(CordRepBtreeHeightTest, GetAppendBufferTreeNotPrivate) {
  if (height() == 0) return;
  AutoUnref refs;
  CordRepFlat* flat = MakeFlat("abc");
  CordRepBtree* tree = CordRepBtree::Create(CordRep::Ref(flat));
  for (int i = 1; i <= height(); ++i) {
    if (i == (height() + 1) / 2) refs.Ref(tree);
    tree = CordRepBtree::New(tree);
  }
  EXPECT_THAT(tree->GetAppendBuffer(1), SizeIs(0u));
  CordRepBtree::Unref(tree);
  CordRep::Unref(flat);
}

TEST_P(CordRepBtreeHeightTest, GetAppendBufferFlatNoCapacity) {
  CordRepFlat* flat = MakeFlat("abc");
  flat->length = flat->Capacity();
  CordRepBtree* tree = CordRepBtree::Create(flat);
  for (int i = 1; i <= height(); ++i) {
    tree = CordRepBtree::New(tree);
  }
  EXPECT_THAT(tree->GetAppendBuffer(1), SizeIs(0u));
  CordRepBtree::Unref(tree);
}

TEST_P(CordRepBtreeHeightTest, GetAppendBufferFlatWithCapacity) {
  CordRepFlat* flat = MakeFlat("abc");
  CordRepBtree* tree = CordRepBtree::Create(flat);
  for (int i = 1; i <= height(); ++i) {
    tree = CordRepBtree::New(tree);
  }
  absl::Span<char> span = tree->GetAppendBuffer(2);
  EXPECT_THAT(span, SizeIs(2u));
  EXPECT_THAT(span.data(), TypedEq<void*>(flat->Data() + 3));
  EXPECT_THAT(tree->length, Eq(5u));

  size_t avail = flat->Capacity() - 5;
  span = tree->GetAppendBuffer(avail + 100);
  EXPECT_THAT(span, SizeIs(avail));
  EXPECT_THAT(span.data(), TypedEq<void*>(flat->Data() + 5));
  EXPECT_THAT(tree->length, Eq(5 + avail));

  CordRepBtree::Unref(tree);
}

TEST(CordRepBtreeTest, Dump) {
  // Handles nullptr
  std::stringstream ss;
  CordRepBtree::Dump(nullptr, ss);
  CordRepBtree::Dump(nullptr, "Once upon a label", ss);
  CordRepBtree::Dump(nullptr, "Once upon a label", false, ss);
  CordRepBtree::Dump(nullptr, "Once upon a label", true, ss);

  // Cover legal edges
  CordRepFlat* flat = MakeFlat("Hello world");
  CordRepExternal* external = MakeExternal("Hello external");
  CordRep* substr_flat = MakeSubstring(1, 6, CordRep::Ref(flat));
  CordRep* substr_external = MakeSubstring(2, 7, CordRep::Ref(external));

  // Build tree
  CordRepBtree* tree = CordRepBtree::Create(flat);
  tree = CordRepBtree::Append(tree, external);
  tree = CordRepBtree::Append(tree, substr_flat);
  tree = CordRepBtree::Append(tree, substr_external);

  // Repeat until we have a tree
  while (tree->height() == 0) {
    tree = CordRepBtree::Append(tree, CordRep::Ref(flat));
    tree = CordRepBtree::Append(tree, CordRep::Ref(external));
    tree = CordRepBtree::Append(tree, CordRep::Ref(substr_flat));
    tree = CordRepBtree::Append(tree, CordRep::Ref(substr_external));
  }

  for (int api = 0; api <= 3; ++api) {
    absl::string_view api_scope;
    std::stringstream ss;
    switch (api) {
      case 0:
        api_scope = "Bare";
        CordRepBtree::Dump(tree, ss);
        break;
      case 1:
        api_scope = "Label only";
        CordRepBtree::Dump(tree, "Once upon a label", ss);
        break;
      case 2:
        api_scope = "Label no content";
        CordRepBtree::Dump(tree, "Once upon a label", false, ss);
        break;
      default:
        api_scope = "Label and content";
        CordRepBtree::Dump(tree, "Once upon a label", true, ss);
        break;
    }
    SCOPED_TRACE(api_scope);
    std::string str = ss.str();

    // Contains Node(depth) / Leaf and private / shared indicators
    EXPECT_THAT(str, AllOf(HasSubstr("Node(1)"), HasSubstr("Leaf"),
                           HasSubstr("Private"), HasSubstr("Shared")));

    // Contains length and start offset of all data edges
    EXPECT_THAT(str, AllOf(HasSubstr("len = 11"), HasSubstr("len = 14"),
                           HasSubstr("len = 6"), HasSubstr("len = 7"),
                           HasSubstr("start = 1"), HasSubstr("start = 2")));

    // Contains address of all data edges
    EXPECT_THAT(
        str, AllOf(HasSubstr(absl::StrCat("0x", absl::Hex(flat))),
                   HasSubstr(absl::StrCat("0x", absl::Hex(external))),
                   HasSubstr(absl::StrCat("0x", absl::Hex(substr_flat))),
                   HasSubstr(absl::StrCat("0x", absl::Hex(substr_external)))));

    if (api != 0) {
      // Contains label
      EXPECT_THAT(str, HasSubstr("Once upon a label"));
    }

    if (api != 3) {
      // Does not contain contents
      EXPECT_THAT(str, Not(AnyOf((HasSubstr("data = \"Hello world\""),
                                  HasSubstr("data = \"Hello external\""),
                                  HasSubstr("data = \"ello w\""),
                                  HasSubstr("data = \"llo ext\"")))));
    } else {
      // Contains contents
      EXPECT_THAT(str, AllOf((HasSubstr("data = \"Hello world\""),
                              HasSubstr("data = \"Hello external\""),
                              HasSubstr("data = \"ello w\""),
                              HasSubstr("data = \"llo ext\""))));
    }
  }

  CordRep::Unref(tree);
}

TEST(CordRepBtreeTest, IsValid) {
  EXPECT_FALSE(CordRepBtree::IsValid(nullptr));

  CordRepBtree* empty = CordRepBtree::New(0);
  EXPECT_TRUE(CordRepBtree::IsValid(empty));
  CordRep::Unref(empty);

  for (bool as_tree : {false, true}) {
    CordRepBtree* leaf = CordRepBtree::Create(MakeFlat("abc"));
    CordRepBtree* tree = as_tree ? CordRepBtree::New(leaf) : nullptr;
    CordRepBtree* check = as_tree ? tree : leaf;

    ASSERT_TRUE(CordRepBtree::IsValid(check));
    leaf->length--;
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->length++;

    ASSERT_TRUE(CordRepBtree::IsValid(check));
    leaf->tag--;
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->tag++;

    // Height
    ASSERT_TRUE(CordRepBtree::IsValid(check));
    leaf->storage[0] = static_cast<uint8_t>(CordRepBtree::kMaxHeight + 1);
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->storage[0] = 1;
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->storage[0] = 0;

    // Begin
    ASSERT_TRUE(CordRepBtree::IsValid(check));
    const uint8_t begin = leaf->storage[1];
    leaf->storage[1] = static_cast<uint8_t>(CordRepBtree::kMaxCapacity);
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->storage[1] = 2;
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->storage[1] = begin;

    // End
    ASSERT_TRUE(CordRepBtree::IsValid(check));
    const uint8_t end = leaf->storage[2];
    leaf->storage[2] = static_cast<uint8_t>(CordRepBtree::kMaxCapacity + 1);
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    leaf->storage[2] = end;

    // DataEdge tag and value
    ASSERT_TRUE(CordRepBtree::IsValid(check));
    CordRep* const edge = leaf->Edges()[0];
    const uint8_t tag = edge->tag;
    CordRepBtreeTestPeer::SetEdge(leaf, begin, nullptr);
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    CordRepBtreeTestPeer::SetEdge(leaf, begin, edge);
    edge->tag = BTREE;
    EXPECT_FALSE(CordRepBtree::IsValid(check));
    edge->tag = tag;

    if (as_tree) {
      ASSERT_TRUE(CordRepBtree::IsValid(check));
      leaf->length--;
      EXPECT_FALSE(CordRepBtree::IsValid(check));
      leaf->length++;

      // Height
      ASSERT_TRUE(CordRepBtree::IsValid(check));
      tree->storage[0] = static_cast<uint8_t>(2);
      EXPECT_FALSE(CordRepBtree::IsValid(check));
      tree->storage[0] = 1;

      // Btree edge
      ASSERT_TRUE(CordRepBtree::IsValid(check));
      CordRep* const edge = tree->Edges()[0];
      const uint8_t tag = edge->tag;
      edge->tag = FLAT;
      EXPECT_FALSE(CordRepBtree::IsValid(check));
      edge->tag = tag;
    }

    ASSERT_TRUE(CordRepBtree::IsValid(check));
    CordRep::Unref(check);
  }
}

TEST(CordRepBtreeTest, AssertValid) {
  CordRepBtree* tree = CordRepBtree::Create(MakeFlat("abc"));
  const CordRepBtree* ctree = tree;
  EXPECT_THAT(CordRepBtree::AssertValid(tree), Eq(tree));
  EXPECT_THAT(CordRepBtree::AssertValid(ctree), Eq(ctree));

#if defined(GTEST_HAS_DEATH_TEST)
  CordRepBtree* nulltree = nullptr;
  const CordRepBtree* cnulltree = nullptr;
  EXPECT_DEBUG_DEATH(
      EXPECT_THAT(CordRepBtree::AssertValid(nulltree), Eq(nulltree)), ".*");
  EXPECT_DEBUG_DEATH(
      EXPECT_THAT(CordRepBtree::AssertValid(cnulltree), Eq(cnulltree)), ".*");

  tree->length--;
  EXPECT_DEBUG_DEATH(EXPECT_THAT(CordRepBtree::AssertValid(tree), Eq(tree)),
                     ".*");
  EXPECT_DEBUG_DEATH(EXPECT_THAT(CordRepBtree::AssertValid(ctree), Eq(ctree)),
                     ".*");
  tree->length++;
#endif
  CordRep::Unref(tree);
}

TEST(CordRepBtreeTest, CheckAssertValidShallowVsDeep) {
  // Restore exhaustive validation on any exit.
  const bool exhaustive_validation = IsCordBtreeExhaustiveValidationEnabled();
  auto cleanup = absl::MakeCleanup([exhaustive_validation] {
    SetCordBtreeExhaustiveValidation(exhaustive_validation);
  });

  // Create a tree of at least 2 levels, and mess with the original flat, which
  // should go undetected in shallow mode as the flat is too far away, but
  // should be detected in forced non-shallow mode.
  CordRep* flat = MakeFlat("abc");
  CordRepBtree* tree = CordRepBtree::Create(flat);
  constexpr size_t max_cap = CordRepBtree::kMaxCapacity;
  const size_t n = max_cap * max_cap * 2;
  for (size_t i = 0; i < n; ++i) {
    tree = CordRepBtree::Append(tree, MakeFlat("Hello world"));
  }
  flat->length = 100;

  SetCordBtreeExhaustiveValidation(false);
  EXPECT_FALSE(CordRepBtree::IsValid(tree));
  EXPECT_TRUE(CordRepBtree::IsValid(tree, true));
  EXPECT_FALSE(CordRepBtree::IsValid(tree, false));
  CordRepBtree::AssertValid(tree);
  CordRepBtree::AssertValid(tree, true);
#if defined(GTEST_HAS_DEATH_TEST)
  EXPECT_DEBUG_DEATH(CordRepBtree::AssertValid(tree, false), ".*");
#endif

  SetCordBtreeExhaustiveValidation(true);
  EXPECT_FALSE(CordRepBtree::IsValid(tree));
  EXPECT_FALSE(CordRepBtree::IsValid(tree, true));
  EXPECT_FALSE(CordRepBtree::IsValid(tree, false));
#if defined(GTEST_HAS_DEATH_TEST)
  EXPECT_DEBUG_DEATH(CordRepBtree::AssertValid(tree), ".*");
  EXPECT_DEBUG_DEATH(CordRepBtree::AssertValid(tree, true), ".*");
#endif

  flat->length = 3;
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeTest, Rebuild) {
  for (size_t size : {3u, 8u, 100u, 10000u, 1000000u}) {
    SCOPED_TRACE(absl::StrCat("Rebuild @", size));

    std::vector<CordRepFlat*> flats;
    for (size_t i = 0; i < size; ++i) {
      flats.push_back(CordRepFlat::New(2));
      flats.back()->Data()[0] = 'x';
      flats.back()->length = 1;
    }

    // Build the tree into 'right', and each so many 'split_limit' edges,
    // combine 'left' + 'right' into a new 'left', and start a new 'right'.
    // This guarantees we get a reasonable amount of chaos in the tree.
    size_t split_count = 0;
    size_t split_limit = 3;
    auto it = flats.begin();
    CordRepBtree* left = nullptr;
    CordRepBtree* right = CordRepBtree::New(*it);
    while (++it != flats.end()) {
      if (++split_count >= split_limit) {
        split_limit += split_limit / 16;
        left = left ? CordRepBtree::Append(left, right) : right;
        right = CordRepBtree::New(*it);
      } else {
        right = CordRepBtree::Append(right, *it);
      }
    }

    // Finalize tree
    left = left ? CordRepBtree::Append(left, right) : right;

    // Rebuild
    AutoUnref ref;
    left = ref.Add(CordRepBtree::Rebuild(ref.RefIf(shared(), left)));
    ASSERT_TRUE(CordRepBtree::IsValid(left));

    // Verify we have the exact same edges in the exact same order.
    bool ok = true;
    it = flats.begin();
    CordVisitReps(left, [&](CordRep* edge) {
      if (edge->tag < FLAT) return;
      ok = ok && (it != flats.end() && *it++ == edge);
    });
    EXPECT_TRUE(ok && it == flats.end()) << "Rebuild edges mismatch";
  }
}

// Convenience helper for CordRepBtree::ExtractAppendBuffer
CordRepBtree::ExtractResult ExtractLast(CordRepBtree* input, size_t cap = 1) {
  return CordRepBtree::ExtractAppendBuffer(input, cap);
}

TEST(CordRepBtreeTest, ExtractAppendBufferLeafSingleFlat) {
  CordRep* flat = MakeFlat("Abc");
  CordRepBtree* leaf = CordRepBtree::Create(flat);
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(nullptr, flat));
  CordRep::Unref(flat);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNodeSingleFlat) {
  CordRep* flat = MakeFlat("Abc");
  CordRepBtree* leaf = CordRepBtree::Create(flat);
  CordRepBtree* node = CordRepBtree::New(leaf);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(nullptr, flat));
  CordRep::Unref(flat);
}

TEST(CordRepBtreeTest, ExtractAppendBufferLeafTwoFlats) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  CordRepBtree* leaf = CreateTree(flats);
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(flats[0], flats[1]));
  CordRep::Unref(flats[0]);
  CordRep::Unref(flats[1]);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNodeTwoFlats) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  CordRepBtree* leaf = CreateTree(flats);
  CordRepBtree* node = CordRepBtree::New(leaf);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(flats[0], flats[1]));
  CordRep::Unref(flats[0]);
  CordRep::Unref(flats[1]);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNodeTwoFlatsInTwoLeafs) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  CordRepBtree* leaf1 = CordRepBtree::Create(flats[0]);
  CordRepBtree* leaf2 = CordRepBtree::Create(flats[1]);
  CordRepBtree* node = CordRepBtree::New(leaf1, leaf2);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(flats[0], flats[1]));
  CordRep::Unref(flats[0]);
  CordRep::Unref(flats[1]);
}

TEST(CordRepBtreeTest, ExtractAppendBufferLeafThreeFlats) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdefghi", 3);
  CordRepBtree* leaf = CreateTree(flats);
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(leaf, flats[2]));
  CordRep::Unref(flats[2]);
  CordRep::Unref(leaf);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNodeThreeFlatsRightNoFolding) {
  CordRep* flat = MakeFlat("Abc");
  std::vector<CordRep*> flats = CreateFlatsFromString("defghi", 3);
  CordRepBtree* leaf1 = CordRepBtree::Create(flat);
  CordRepBtree* leaf2 = CreateTree(flats);
  CordRepBtree* node = CordRepBtree::New(leaf1, leaf2);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(node, flats[1]));
  EXPECT_THAT(node->Edges(), ElementsAre(leaf1, leaf2));
  EXPECT_THAT(leaf1->Edges(), ElementsAre(flat));
  EXPECT_THAT(leaf2->Edges(), ElementsAre(flats[0]));
  CordRep::Unref(node);
  CordRep::Unref(flats[1]);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNodeThreeFlatsRightLeafFolding) {
  CordRep* flat = MakeFlat("Abc");
  std::vector<CordRep*> flats = CreateFlatsFromString("defghi", 3);
  CordRepBtree* leaf1 = CreateTree(flats);
  CordRepBtree* leaf2 = CordRepBtree::Create(flat);
  CordRepBtree* node = CordRepBtree::New(leaf1, leaf2);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(leaf1, flat));
  EXPECT_THAT(leaf1->Edges(), ElementsAreArray(flats));
  CordRep::Unref(leaf1);
  CordRep::Unref(flat);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNoCapacity) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  CordRepBtree* leaf = CreateTree(flats);
  size_t avail = flats[1]->flat()->Capacity() - flats[1]->length;
  EXPECT_THAT(ExtractLast(leaf, avail + 1), EqExtractResult(leaf, nullptr));
  EXPECT_THAT(ExtractLast(leaf, avail), EqExtractResult(flats[0], flats[1]));
  CordRep::Unref(flats[0]);
  CordRep::Unref(flats[1]);
}

TEST(CordRepBtreeTest, ExtractAppendBufferNotFlat) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  auto substr = MakeSubstring(1, 2, flats[1]);
  CordRepBtree* leaf = CreateTree({flats[0], substr});
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(leaf, nullptr));
  CordRep::Unref(leaf);
}

TEST(CordRepBtreeTest, ExtractAppendBufferShared) {
  std::vector<CordRep*> flats = CreateFlatsFromString("abcdef", 3);
  CordRepBtree* leaf = CreateTree(flats);

  CordRep::Ref(flats[1]);
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(leaf, nullptr));
  CordRep::Unref(flats[1]);

  CordRep::Ref(leaf);
  EXPECT_THAT(ExtractLast(leaf), EqExtractResult(leaf, nullptr));
  CordRep::Unref(leaf);

  CordRepBtree* node = CordRepBtree::New(leaf);
  CordRep::Ref(node);
  EXPECT_THAT(ExtractLast(node), EqExtractResult(node, nullptr));
  CordRep::Unref(node);

  CordRep::Unref(node);
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
