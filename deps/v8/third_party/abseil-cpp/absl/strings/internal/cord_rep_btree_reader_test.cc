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

#include "absl/strings/internal/cord_rep_btree_reader.h"

#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/strings/cord.h"
#include "absl/strings/internal/cord_internal.h"
#include "absl/strings/internal/cord_rep_btree.h"
#include "absl/strings/internal/cord_rep_test_util.h"
#include "absl/strings/string_view.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace cord_internal {
namespace {

using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::Ne;
using ::testing::Not;

using ::absl::cordrep_testing::CordRepBtreeFromFlats;
using ::absl::cordrep_testing::MakeFlat;
using ::absl::cordrep_testing::CordToString;
using ::absl::cordrep_testing::CreateFlatsFromString;
using ::absl::cordrep_testing::CreateRandomString;

using ReadResult = CordRepBtreeReader::ReadResult;

TEST(CordRepBtreeReaderTest, Next) {
  constexpr size_t kChars = 3;
  const size_t cap = CordRepBtree::kMaxCapacity;
  size_t counts[] = {1, 2, cap, cap * cap, cap * cap + 1, cap * cap * 2 + 17};

  for (size_t count : counts) {
    std::string data = CreateRandomString(count * kChars);
    std::vector<CordRep*> flats = CreateFlatsFromString(data, kChars);
    CordRepBtree* node = CordRepBtreeFromFlats(flats);

    CordRepBtreeReader reader;
    size_t remaining = data.length();
    absl::string_view chunk = reader.Init(node);
    EXPECT_THAT(chunk, Eq(data.substr(0, chunk.length())));

    remaining -= chunk.length();
    EXPECT_THAT(reader.remaining(), Eq(remaining));

    while (remaining > 0) {
      const size_t offset = data.length() - remaining;
      chunk = reader.Next();
      EXPECT_THAT(chunk, Eq(data.substr(offset, chunk.length())));

      remaining -= chunk.length();
      EXPECT_THAT(reader.remaining(), Eq(remaining));
    }

    EXPECT_THAT(reader.remaining(), Eq(0u));

    // Verify trying to read beyond EOF returns empty string_view
    EXPECT_THAT(reader.Next(), testing::IsEmpty());

    CordRep::Unref(node);
  }
}

TEST(CordRepBtreeReaderTest, Skip) {
  constexpr size_t kChars = 3;
  const size_t cap = CordRepBtree::kMaxCapacity;
  size_t counts[] = {1, 2, cap, cap * cap, cap * cap + 1, cap * cap * 2 + 17};

  for (size_t count : counts) {
    std::string data = CreateRandomString(count * kChars);
    std::vector<CordRep*> flats = CreateFlatsFromString(data, kChars);
    CordRepBtree* node = CordRepBtreeFromFlats(flats);

    for (size_t skip1 = 0; skip1 < data.length() - kChars; ++skip1) {
      for (size_t skip2 = 0; skip2 < data.length() - kChars; ++skip2) {
        CordRepBtreeReader reader;
        size_t remaining = data.length();
        absl::string_view chunk = reader.Init(node);
        remaining -= chunk.length();

        chunk = reader.Skip(skip1);
        size_t offset = data.length() - remaining;
        ASSERT_THAT(chunk, Eq(data.substr(offset + skip1, chunk.length())));
        remaining -= chunk.length() + skip1;
        ASSERT_THAT(reader.remaining(), Eq(remaining));

        if (remaining == 0) continue;

        size_t skip = std::min(remaining - 1, skip2);
        chunk = reader.Skip(skip);
        offset = data.length() - remaining;
        ASSERT_THAT(chunk, Eq(data.substr(offset + skip, chunk.length())));
      }
    }

    CordRep::Unref(node);
  }
}

TEST(CordRepBtreeReaderTest, SkipBeyondLength) {
  CordRepBtree* tree = CordRepBtree::Create(MakeFlat("abc"));
  tree = CordRepBtree::Append(tree, MakeFlat("def"));
  CordRepBtreeReader reader;
  reader.Init(tree);
  EXPECT_THAT(reader.Skip(100), IsEmpty());
  EXPECT_THAT(reader.remaining(), Eq(0u));
  CordRep::Unref(tree);
}

TEST(CordRepBtreeReaderTest, Seek) {
  constexpr size_t kChars = 3;
  const size_t cap = CordRepBtree::kMaxCapacity;
  size_t counts[] = {1, 2, cap, cap * cap, cap * cap + 1, cap * cap * 2 + 17};

  for (size_t count : counts) {
    std::string data = CreateRandomString(count * kChars);
    std::vector<CordRep*> flats = CreateFlatsFromString(data, kChars);
    CordRepBtree* node = CordRepBtreeFromFlats(flats);

    for (size_t seek = 0; seek < data.length() - 1; ++seek) {
      CordRepBtreeReader reader;
      reader.Init(node);
      absl::string_view chunk = reader.Seek(seek);
      ASSERT_THAT(chunk, Not(IsEmpty()));
      ASSERT_THAT(chunk, Eq(data.substr(seek, chunk.length())));
      ASSERT_THAT(reader.remaining(),
                  Eq(data.length() - seek - chunk.length()));
    }

    CordRep::Unref(node);
  }
}

TEST(CordRepBtreeReaderTest, SeekBeyondLength) {
  CordRepBtree* tree = CordRepBtree::Create(MakeFlat("abc"));
  tree = CordRepBtree::Append(tree, MakeFlat("def"));
  CordRepBtreeReader reader;
  reader.Init(tree);
  EXPECT_THAT(reader.Seek(6), IsEmpty());
  EXPECT_THAT(reader.remaining(), Eq(0u));
  EXPECT_THAT(reader.Seek(100), IsEmpty());
  EXPECT_THAT(reader.remaining(), Eq(0u));
  CordRep::Unref(tree);
}

TEST(CordRepBtreeReaderTest, Read) {
  std::string data = "abcdefghijklmno";
  std::vector<CordRep*> flats = CreateFlatsFromString(data, 5);
  CordRepBtree* node = CordRepBtreeFromFlats(flats);

  CordRep* tree;
  CordRepBtreeReader reader;
  absl::string_view chunk;

  // Read zero bytes
  chunk = reader.Init(node);
  chunk = reader.Read(0, chunk.length(), tree);
  EXPECT_THAT(tree, Eq(nullptr));
  EXPECT_THAT(chunk, Eq("abcde"));
  EXPECT_THAT(reader.remaining(), Eq(10u));
  EXPECT_THAT(reader.Next(), Eq("fghij"));

  // Read in full
  chunk = reader.Init(node);
  chunk = reader.Read(15, chunk.length(), tree);
  EXPECT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("abcdefghijklmno"));
  EXPECT_THAT(chunk, Eq(""));
  EXPECT_THAT(reader.remaining(), Eq(0u));
  CordRep::Unref(tree);

  // Read < chunk bytes
  chunk = reader.Init(node);
  chunk = reader.Read(3, chunk.length(), tree);
  ASSERT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("abc"));
  EXPECT_THAT(chunk, Eq("de"));
  EXPECT_THAT(reader.remaining(), Eq(10u));
  EXPECT_THAT(reader.Next(), Eq("fghij"));
  CordRep::Unref(tree);

  // Read < chunk bytes at offset
  chunk = reader.Init(node);
  chunk = reader.Read(2, chunk.length() - 2, tree);
  ASSERT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("cd"));
  EXPECT_THAT(chunk, Eq("e"));
  EXPECT_THAT(reader.remaining(), Eq(10u));
  EXPECT_THAT(reader.Next(), Eq("fghij"));
  CordRep::Unref(tree);

  // Read from consumed chunk
  chunk = reader.Init(node);
  chunk = reader.Read(3, 0, tree);
  ASSERT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("fgh"));
  EXPECT_THAT(chunk, Eq("ij"));
  EXPECT_THAT(reader.remaining(), Eq(5u));
  EXPECT_THAT(reader.Next(), Eq("klmno"));
  CordRep::Unref(tree);

  // Read across chunks
  chunk = reader.Init(node);
  chunk = reader.Read(12, chunk.length() - 2, tree);
  ASSERT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("cdefghijklmn"));
  EXPECT_THAT(chunk, Eq("o"));
  EXPECT_THAT(reader.remaining(), Eq(0u));
  CordRep::Unref(tree);

  // Read across chunks landing on exact edge boundary
  chunk = reader.Init(node);
  chunk = reader.Read(10 - 2, chunk.length() - 2, tree);
  ASSERT_THAT(tree, Ne(nullptr));
  EXPECT_THAT(CordToString(tree), Eq("cdefghij"));
  EXPECT_THAT(chunk, Eq("klmno"));
  EXPECT_THAT(reader.remaining(), Eq(0u));
  CordRep::Unref(tree);

  CordRep::Unref(node);
}

TEST(CordRepBtreeReaderTest, ReadExhaustive) {
  constexpr size_t kChars = 3;
  const size_t cap = CordRepBtree::kMaxCapacity;
  size_t counts[] = {1, 2, cap, cap * cap + 1, cap * cap * cap * 2 + 17};

  for (size_t count : counts) {
    std::string data = CreateRandomString(count * kChars);
    std::vector<CordRep*> flats = CreateFlatsFromString(data, kChars);
    CordRepBtree* node = CordRepBtreeFromFlats(flats);

    for (size_t read_size : {kChars - 1, kChars, kChars + 7, cap * cap}) {
      CordRepBtreeReader reader;
      absl::string_view chunk = reader.Init(node);

      // `consumed` tracks the end of last consumed chunk which is the start of
      // the next chunk: we always read with `chunk_size = chunk.length()`.
      size_t consumed = 0;
      size_t remaining = data.length();
      while (remaining > 0) {
        CordRep* tree;
        size_t n = (std::min)(remaining, read_size);
        chunk = reader.Read(n, chunk.length(), tree);
        EXPECT_THAT(tree, Ne(nullptr));
        if (tree) {
          EXPECT_THAT(CordToString(tree), Eq(data.substr(consumed, n)));
          CordRep::Unref(tree);
        }

        consumed += n;
        remaining -= n;
        EXPECT_THAT(reader.remaining(), Eq(remaining - chunk.length()));

        if (remaining > 0) {
          ASSERT_FALSE(chunk.empty());
          ASSERT_THAT(chunk, Eq(data.substr(consumed, chunk.length())));
        } else {
          ASSERT_TRUE(chunk.empty()) << chunk;
        }
      }
    }

    CordRep::Unref(node);
  }
}

}  // namespace
}  // namespace cord_internal
ABSL_NAMESPACE_END
}  // namespace absl
