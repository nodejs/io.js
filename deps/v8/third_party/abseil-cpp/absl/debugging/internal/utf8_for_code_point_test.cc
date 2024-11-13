// Copyright 2024 The Abseil Authors
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

#include "absl/debugging/internal/utf8_for_code_point.h"

#include <cstdint>

#include "gtest/gtest.h"
#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace debugging_internal {
namespace {

TEST(Utf8ForCodePointTest, RecognizesTheSmallestCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0});
  ASSERT_EQ(utf8.length, 1);
  EXPECT_EQ(utf8.bytes[0], '\0');
}

TEST(Utf8ForCodePointTest, RecognizesAsciiSmallA) {
  Utf8ForCodePoint utf8(uint64_t{'a'});
  ASSERT_EQ(utf8.length, 1);
  EXPECT_EQ(utf8.bytes[0], 'a');
}

TEST(Utf8ForCodePointTest, RecognizesTheLargestOneByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x7f});
  ASSERT_EQ(utf8.length, 1);
  EXPECT_EQ(utf8.bytes[0], '\x7f');
}

TEST(Utf8ForCodePointTest, RecognizesTheSmallestTwoByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x80});
  ASSERT_EQ(utf8.length, 2);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xc2));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0x80));
}

TEST(Utf8ForCodePointTest, RecognizesSmallNWithTilde) {
  Utf8ForCodePoint utf8(uint64_t{0xf1});
  ASSERT_EQ(utf8.length, 2);
  const char* want = "ñ";
  EXPECT_EQ(utf8.bytes[0], want[0]);
  EXPECT_EQ(utf8.bytes[1], want[1]);
}

TEST(Utf8ForCodePointTest, RecognizesCapitalPi) {
  Utf8ForCodePoint utf8(uint64_t{0x3a0});
  ASSERT_EQ(utf8.length, 2);
  const char* want = "Π";
  EXPECT_EQ(utf8.bytes[0], want[0]);
  EXPECT_EQ(utf8.bytes[1], want[1]);
}

TEST(Utf8ForCodePointTest, RecognizesTheLargestTwoByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x7ff});
  ASSERT_EQ(utf8.length, 2);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xdf));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0xbf));
}

TEST(Utf8ForCodePointTest, RecognizesTheSmallestThreeByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x800});
  ASSERT_EQ(utf8.length, 3);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xe0));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0xa0));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0x80));
}

TEST(Utf8ForCodePointTest, RecognizesTheChineseCharacterZhong1AsInZhong1Wen2) {
  Utf8ForCodePoint utf8(uint64_t{0x4e2d});
  ASSERT_EQ(utf8.length, 3);
  const char* want = "中";
  EXPECT_EQ(utf8.bytes[0], want[0]);
  EXPECT_EQ(utf8.bytes[1], want[1]);
  EXPECT_EQ(utf8.bytes[2], want[2]);
}

TEST(Utf8ForCodePointTest, RecognizesOneBeforeTheSmallestSurrogate) {
  Utf8ForCodePoint utf8(uint64_t{0xd7ff});
  ASSERT_EQ(utf8.length, 3);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xed));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0x9f));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0xbf));
}

TEST(Utf8ForCodePointTest, RejectsTheSmallestSurrogate) {
  Utf8ForCodePoint utf8(uint64_t{0xd800});
  EXPECT_EQ(utf8.length, 0);
}

TEST(Utf8ForCodePointTest, RejectsTheLargestSurrogate) {
  Utf8ForCodePoint utf8(uint64_t{0xdfff});
  EXPECT_EQ(utf8.length, 0);
}

TEST(Utf8ForCodePointTest, RecognizesOnePastTheLargestSurrogate) {
  Utf8ForCodePoint utf8(uint64_t{0xe000});
  ASSERT_EQ(utf8.length, 3);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xee));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0x80));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0x80));
}

TEST(Utf8ForCodePointTest, RecognizesTheLargestThreeByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0xffff});
  ASSERT_EQ(utf8.length, 3);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xef));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0xbf));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0xbf));
}

TEST(Utf8ForCodePointTest, RecognizesTheSmallestFourByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x10000});
  ASSERT_EQ(utf8.length, 4);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xf0));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0x90));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0x80));
  EXPECT_EQ(utf8.bytes[3], static_cast<char>(0x80));
}

TEST(Utf8ForCodePointTest, RecognizesTheJackOfHearts) {
  Utf8ForCodePoint utf8(uint64_t{0x1f0bb});
  ASSERT_EQ(utf8.length, 4);
  const char* want = "🂻";
  EXPECT_EQ(utf8.bytes[0], want[0]);
  EXPECT_EQ(utf8.bytes[1], want[1]);
  EXPECT_EQ(utf8.bytes[2], want[2]);
  EXPECT_EQ(utf8.bytes[3], want[3]);
}

TEST(Utf8ForCodePointTest, RecognizesTheLargestFourByteCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x10ffff});
  ASSERT_EQ(utf8.length, 4);
  EXPECT_EQ(utf8.bytes[0], static_cast<char>(0xf4));
  EXPECT_EQ(utf8.bytes[1], static_cast<char>(0x8f));
  EXPECT_EQ(utf8.bytes[2], static_cast<char>(0xbf));
  EXPECT_EQ(utf8.bytes[3], static_cast<char>(0xbf));
}

TEST(Utf8ForCodePointTest, RejectsTheSmallestOverlargeCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0x110000});
  EXPECT_EQ(utf8.length, 0);
}

TEST(Utf8ForCodePointTest, RejectsAThroughlyOverlargeCodePoint) {
  Utf8ForCodePoint utf8(uint64_t{0xffffffff00000000});
  EXPECT_EQ(utf8.length, 0);
}

TEST(Utf8ForCodePointTest, OkReturnsTrueForAValidCodePoint) {
  EXPECT_TRUE(Utf8ForCodePoint(uint64_t{0}).ok());
}

TEST(Utf8ForCodePointTest, OkReturnsFalseForAnInvalidCodePoint) {
  EXPECT_FALSE(Utf8ForCodePoint(uint64_t{0xffffffff00000000}).ok());
}

}  // namespace
}  // namespace debugging_internal
ABSL_NAMESPACE_END
}  // namespace absl
