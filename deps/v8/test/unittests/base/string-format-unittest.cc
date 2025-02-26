// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/string-format.h"

#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest-support.h"

namespace v8::base {

// Some hard-coded assumptions.
constexpr int kMaxPrintedIntLen = 11;
constexpr int kMaxPrintedUint32Len = 10;
constexpr int kMaxPrintedUint64Len = 20;
constexpr int kMaxPrintedSizetLen = sizeof(size_t) == sizeof(uint32_t)
                                        ? kMaxPrintedUint32Len
                                        : kMaxPrintedUint64Len;

TEST(FormattedStringTest, Empty) {
  auto empty = FormattedString{};
  EXPECT_EQ("", decltype(empty)::kFormat);
  EXPECT_EQ(1, decltype(empty)::kMaxLen);
  EXPECT_EQ('\0', empty.PrintToArray()[0]);
}

TEST(FormattedStringTest, SingleString) {
  auto message = FormattedString{} << "foo";
  EXPECT_EQ("%s", decltype(message)::kFormat);

  constexpr std::array<char, 4> kExpectedOutput{'f', 'o', 'o', '\0'};
  EXPECT_EQ(kExpectedOutput, message.PrintToArray());
}

TEST(FormattedStringTest, Int) {
  auto message = FormattedString{} << 42;
  EXPECT_EQ("%d", decltype(message)::kFormat);
  // +1 for null-termination.
  EXPECT_EQ(kMaxPrintedIntLen + 1, decltype(message)::kMaxLen);

  EXPECT_THAT(message.PrintToArray().data(), ::testing::StrEq("42"));
}

TEST(FormattedStringTest, MaxInt) {
  auto message = FormattedString{} << std::numeric_limits<int>::max();
  auto result_arr = message.PrintToArray();
  // We *nearly* used the full reserved array size (the minimum integer is still
  // one character longer)..
  EXPECT_EQ(size_t{decltype(message)::kMaxLen}, result_arr.size());
  EXPECT_THAT(result_arr.data(), ::testing::StrEq("2147483647"));
}

TEST(FormattedStringTest, MinInt) {
  auto message = FormattedString{} << std::numeric_limits<int>::min();
  auto result_arr = message.PrintToArray();
  // We used the full reserved array size.
  EXPECT_EQ(size_t{decltype(message)::kMaxLen}, result_arr.size());
  EXPECT_THAT(result_arr.data(), ::testing::StrEq("-2147483648"));
}

TEST(FormattedStringTest, SizeT) {
  auto message = FormattedString{} << size_t{42};
  EXPECT_EQ(sizeof(size_t) == sizeof(uint32_t) ? "%" PRIu32 : "%" PRIu64,
            decltype(message)::kFormat);
  // +1 for null-termination.
  EXPECT_EQ(kMaxPrintedSizetLen + 1, decltype(message)::kMaxLen);

  EXPECT_THAT(message.PrintToArray().data(), ::testing::StrEq("42"));
}

TEST(FormattedStringTest, MaxSizeT) {
  auto message = FormattedString{} << std::numeric_limits<size_t>::max();
  auto result_arr = message.PrintToArray();
  // We used the full reserved array size.
  EXPECT_EQ(size_t{decltype(message)::kMaxLen}, result_arr.size());
  constexpr const char* kMaxSizeTStr =
      sizeof(size_t) == 4 ? "4294967295" : "18446744073709551615";
  EXPECT_THAT(result_arr.data(), ::testing::StrEq(kMaxSizeTStr));
}

TEST(FormattedStringTest, Combination) {
  auto message = FormattedString{} << "Expected " << 11 << " got " << size_t{42}
                                   << "!";
  EXPECT_EQ(sizeof(size_t) == sizeof(uint32_t) ? "%s%d%s%" PRIu32 "%s"
                                               : "%s%d%s%" PRIu64 "%s",
            decltype(message)::kFormat);
  size_t expected_array_len =
      strlen("Expected  got !") + kMaxPrintedIntLen + kMaxPrintedSizetLen + 1;
  EXPECT_EQ(expected_array_len, size_t{decltype(message)::kMaxLen});

  EXPECT_THAT(message.PrintToArray().data(),
              ::testing::StrEq("Expected 11 got 42!"));
}

TEST(FormattedStringTest, Uint32AndUint64) {
  auto message = FormattedString{} << uint32_t{1} << " != " << uint64_t{2};
  EXPECT_EQ("%" PRIu32 "%s%" PRIu64, decltype(message)::kFormat);
  size_t expected_array_len =
      kMaxPrintedUint32Len + 4 + kMaxPrintedUint64Len + 1;
  EXPECT_EQ(expected_array_len, size_t{decltype(message)::kMaxLen});

  EXPECT_THAT(message.PrintToArray().data(), ::testing::StrEq("1 != 2"));
}

}  // namespace v8::base
