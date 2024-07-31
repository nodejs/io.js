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

#include "absl/strings/internal/cord_rep_btree_navigator.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_test_util.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::Eq;
using ::testing::Ne;

using ::absl::cordrep_testing::CordRepBtreeFromFlats;
using ::absl::cordrep_testing::CordToString;
using ::absl::cordrep_testing::CreateFlatsFromString;
using ::absl::cordrep_testing::CreateRandomString;
using ::absl::cordrep_testing::MakeFlat;
using ::absl::cordrep_testing::MakeSubstring;

using ReadResult = CordRepBtreeNavigator::ReadResult;
using Position = CordRepBtreeNavigator::Position;

// CordRepBtreeNavigatorTest is a test fixture which automatically creates a
// tree to test navigation logic on. The parameter `count' defines the number of
// data edges in the test tree.
class CordRepBtreeNavigatorTest : public testing::TestWithParam<size_t> {
 public:
  using Flats = std::vector<CordRep*>;
  static constexpr size_t kCharsPerFlat = 3;

  CordRepBtreeNavigatorTest() {
    data_ = CreateRandomString(count() * kCharsPerFlat);
    flats_ = CreateFlatsFromString(data_, kCharsPerFlat);

    // Turn flat 0 or 1 into a substring to cover partial reads on substrings.
    if (count() > 1) {
      CordRep::Unref(flats_[1]);
      flats_[1] = MakeSubstring(kCharsPerFlat, kCharsPerFlat, MakeFlat(data_));
    } else {
      CordRep::Unref(flats_[0]);
      flats_[0] = MakeSubstring(0, kCharsPerFlat, MakeFlat(data_));
    }

    tree_ = CordRepBtreeFromFlats(flats_);
  }

  ~CordRepBtreeNavigatorTest() override { CordRep::Unref(tree_); }

  size_t count() const { return GetParam(); }
  CordRepBtree* tree() { return tree_; }
  const std::string& data() const { return data_; }
  const std::vector<CordRep*>& flats() const { return flats_; }

  static std::string ToString(testing::TestParamInfo<size_t> param) {
    return absl::StrCat(param.param, "_Flats");
  }

 private:
  std::string data_;
  Flats flats_;
  CordRepBtree* tree_;
};

INSTANTIATE_TEST_SUITE_P(
    WithParam, CordRepBtreeNavigatorTest,
    testing::Values(1, CordRepBtree::kMaxCapacity - 1,
                    CordRepBtree::kMaxCapacity,
                    CordRepBtree::kMaxCapacity* CordRepBtree::kMaxCapacity - 1,
                    CordRepBtree::kMaxCapacity* CordRepBtree::kMaxCapacity,
                    CordRepBtree::kMaxCapacity* CordRepBtree::kMaxCapacity + 1,
                    CordRepBtree::kMaxCapacity* CordRepBtree::kMaxCapacity * 2 +
                        17),
    CordRepBtreeNavigatorTest::ToString);

TEST(CordRepBtreeNavigatorTest, Uninitialized) {
  CordRepBtreeNavigator nav;
  EXPECT_FALSE(nav);
  EXPECT_THAT(nav.btree(), Eq(nullptr));
#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)
  EXPECT_DEATH(nav.Current(), ".*");
#endif
}

TEST_P(CordRepBtreeNavigatorTest, InitFirst) {
  CordRepBtreeNavigator nav;
  CordRep* edge = nav.InitFirst(tree());
  EXPECT_TRUE(nav);
  EXPECT_THAT(nav.btree(), Eq(tree()));
  EXPECT_THAT(nav.Current(), Eq(flats().front()));
  EXPECT_THAT(edge, Eq(flats().front()));
}

TEST_P(CordRepBtreeNavigatorTest, InitLast) {
  CordRepBtreeNavigator nav;
  CordRep* edge = nav.InitLast(tree());
  EXPECT_TRUE(nav);
  EXPECT_THAT(nav.btree(), Eq(tree()));
  EXPECT_THAT(nav.Current(), Eq(flats().back()));
  EXPECT_THAT(edge, Eq(flats().back()));
}

TEST_P(CordRepBtreeNavigatorTest, NextPrev) {
  CordRepBtreeNavigator nav;
  nav.InitFirst(tree());
  const Flats& flats = this->flats();

  EXPECT_THAT(nav.Previous(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.front()));
  for (size_t i = 1; i < flats.size(); ++i) {
    ASSERT_THAT(nav.Next(), Eq(flats[i]));
    EXPECT_THAT(nav.Current(), Eq(flats[i]));
  }
  EXPECT_THAT(nav.Next(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.back()));
  for (size_t i = flats.size() - 1; i > 0; --i) {
    ASSERT_THAT(nav.Previous(), Eq(flats[i - 1]));
    EXPECT_THAT(nav.Current(), Eq(flats[i - 1]));
  }
  EXPECT_THAT(nav.Previous(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.front()));
}

TEST_P(CordRepBtreeNavigatorTest, PrevNext) {
  CordRepBtreeNavigator nav;
  nav.InitLast(tree());
  const Flats& flats = this->flats();

  EXPECT_THAT(nav.Next(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.back()));
  for (size_t i = flats.size() - 1; i > 0; --i) {
    ASSERT_THAT(nav.Previous(), Eq(flats[i - 1]));
    EXPECT_THAT(nav.Current(), Eq(flats[i - 1]));
  }
  EXPECT_THAT(nav.Previous(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.front()));
  for (size_t i = 1; i < flats.size(); ++i) {
    ASSERT_THAT(nav.Next(), Eq(flats[i]));
    EXPECT_THAT(nav.Current(), Eq(flats[i]));
  }
  EXPECT_THAT(nav.Next(), Eq(nullptr));
  EXPECT_THAT(nav.Current(), Eq(flats.back()));
}

TEST(CordRepBtreeNavigatorTest, Reset) {
  CordRepBtree* tree = CordRepBtree::Create(MakeFlat("abc"));
  CordRepBtreeNavigator nav;
  nav.InitFirst(tree);
  nav.Reset();
  EXPECT_FALSE(nav);
  EXPECT_THAT(nav.btree(), Eq(nullptr));
#if defined(GTEST_HAS_DEATH_TEST) && !defined(NDEBUG)
  EXPECT_DEATH(nav.Current(), ".*");
#endif
  CordRep::Unref(tree);
}

TEST_P(CordRepBtreeNavigatorTest, Skip) {
  size_t count = this->count();
  const Flats& flats = this->flats();
  CordRepBtreeNavigator nav;
  nav.InitFirst(tree());

  for (size_t char_offset = 0; char_offset < kCharsPerFlat; ++char_offset) {
    Position pos = nav.Skip(char_offset);
    EXPECT_THAT(pos.edge, Eq(nav.Current()));
    EXPECT_THAT(pos.edge, Eq(flats[0]));
    EXPECT_THAT(pos.offset, Eq(char_offset));
  }

  for (size_t index1 = 0; index1 < count; ++index1) {
    for (size_t index2 = index1; index2 < count; ++index2) {
      for (size_t char_offset = 0; char_offset < kCharsPerFlat; ++char_offset) {
        CordRepBtreeNavigator nav;
        nav.InitFirst(tree());

        size_t length1 = index1 * kCharsPerFlat;
        Position pos1 = nav.Skip(length1 + char_offset);
        ASSERT_THAT(pos1.edge, Eq(flats[index1]));
        ASSERT_THAT(pos1.edge, Eq(nav.Current()));
        ASSERT_THAT(pos1.offset, Eq(char_offset));

        size_t length2 = index2 * kCharsPerFlat;
        Position pos2 = nav.Skip(length2 - length1 + char_offset);
        ASSERT_THAT(pos2.edge, Eq(flats[index2]));
        ASSERT_THAT(pos2.edge, Eq(nav.Current()));
        ASSERT_THAT(pos2.offset, Eq(char_offset));
      }
    }
  }
}

TEST_P(CordRepBtreeNavigatorTest, Seek) {
  size_t count = this->count();
  const Flats& flats = this->flats();
  CordRepBtreeNavigator nav;
  nav.InitFirst(tree());

  for (size_t char_offset = 0; char_offset < kCharsPerFlat; ++char_offset) {
    Position pos = nav.Seek(char_offset);
    EXPECT_THAT(pos.edge, Eq(nav.Current()));
    EXPECT_THAT(pos.edge, Eq(flats[0]));
    EXPECT_THAT(pos.offset, Eq(char_offset));
  }

  for (size_t index = 0; index < count; ++index) {
    for (size_t char_offset = 0; char_offset < kCharsPerFlat; ++char_offset) {
      size_t offset = index * kCharsPerFlat + char_offset;
      Position pos1 = nav.Seek(offset);
      ASSERT_THAT(pos1.edge, Eq(flats[index]));
      ASSERT_THAT(pos1.edge, Eq(nav.Current()));
      ASSERT_THAT(pos1.offset, Eq(char_offset));
    }
  }
}

TEST(CordRepBtreeNavigatorTest, InitOffset) {
  // Whitebox: InitOffset() is implemented in terms of Seek() which is
  // exhaustively tested. Only test it initializes / forwards properly..
  CordRepBtree* tree = CordRepBtree::Create(MakeFlat("abc"));
  tree = CordRepBtree::Append(tree, MakeFlat("def"));
  CordRepBtreeNavigator nav;
  Position pos = nav.InitOffset(tree, 5);
  EXPECT_TRUE(nav);
  EXPECT_THAT(nav.btree(), Eq(tree));
  EXPECT_THAT(pos.edge, Eq(tree->Edges()[1]));
  EXPECT_THAT(pos.edge, Eq(nav.Current()));
  EXPECT_THAT(pos.offset, Eq(2u));
  CordRep::Unref(tree);
}

TEST(CordRepBtreeNavigatorTest, InitOffsetAndSeekBeyondLength) {
  CordRepBtree* tree1 = CordRepBtree::Create(MakeFlat("abc"));
  CordRepBtree* tree2 = CordRepBtree::Create(MakeFlat("def"));

  CordRepBtreeNavigator nav;
  nav.InitFirst(tree1);
  EXPECT_THAT(nav.Seek(3).edge, Eq(nullptr));
  EXPECT_THAT(nav.Seek(100).edge, Eq(nullptr));
  EXPECT_THAT(nav.btree(), Eq(tree1));
  EXPECT_THAT(nav.Current(), Eq(tree1->Edges().front()));

  EXPECT_THAT(nav.InitOffset(tree2, 3).edge, Eq(nullptr));
  EXPECT_THAT(nav.InitOffset(tree2, 100).edge, Eq(nullptr));
  EXPECT_THAT(nav.btree(), Eq(tree1));
  EXPECT_THAT(nav.Current(), Eq(tree1->Edges().front()));

  CordRep::Unref(tree1);
  CordRep::Unref(tree2);
}

TEST_P(CordRepBtreeNavigatorTest, Read) {
  const Flats& flats = this->flats();
  const std::string& data = this->data();

  for (size_t offset = 0; offset < data.size(); ++offset) {
    for (size_t length = 1; length <= data.size() - offset; ++length) {
      CordRepBtreeNavigator nav;
      nav.InitFirst(tree());

      // Skip towards edge holding offset
      size_t edge_offset = nav.Skip(offset).offset;

      // Read node
      ReadResult result = nav.Read(edge_offset, length);
      ASSERT_THAT(result.tree, Ne(nullptr));
      EXPECT_THAT(result.tree->length, Eq(length));
      if (result.tree->tag == BTREE) {
        ASSERT_TRUE(CordRepBtree::IsValid(result.tree->btree()));
      }

      // Verify contents
      std::string value = CordToString(result.tree);
      EXPECT_THAT(value, Eq(data.substr(offset, length)));

      // Verify 'partial last edge' reads.
      size_t partial = (offset + length) % kCharsPerFlat;
      ASSERT_THAT(result.n, Eq(partial));

      // Verify ending position if not EOF
      if (offset + length < data.size()) {
        size_t index = (offset + length) / kCharsPerFlat;
        EXPECT_THAT(nav.Current(), Eq(flats[index]));
      }

      CordRep::Unref(result.tree);
    }
  }
}

TEST_P(CordRepBtreeNavigatorTest, ReadBeyondLengthOfTree) {
  CordRepBtreeNavigator nav;
  nav.InitFirst(tree());
  ReadResult result = nav.Read(2, tree()->length);
  ASSERT_THAT(result.tree, Eq(nullptr));
}

TEST(CordRepBtreeNavigatorTest, NavigateMaximumTreeDepth) {
  CordRepFlat* flat1 = MakeFlat("Hello world");
  CordRepFlat* flat2 = MakeFlat("World Hello");

  CordRepBtree* node = CordRepBtree::Create(flat1);
  node = CordRepBtree::Append(node, flat2);
  while (node->height() < CordRepBtree::kMaxHeight) {
    node = CordRepBtree::New(node);
  }

  CordRepBtreeNavigator nav;
  CordRep* edge = nav.InitFirst(node);
  EXPECT_THAT(edge, Eq(flat1));
  EXPECT_THAT(nav.Next(), Eq(flat2));
  EXPECT_THAT(nav.Next(), Eq(nullptr));
  EXPECT_THAT(nav.Previous(), Eq(flat1));
  EXPECT_THAT(nav.Previous(), Eq(nullptr));

  CordRep::Unref(node);
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
