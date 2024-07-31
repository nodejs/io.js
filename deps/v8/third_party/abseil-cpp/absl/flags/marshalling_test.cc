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

#include "absl/flags/marshalling.h"

#include <stdint.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

TEST(MarshallingTest, TestBoolParsing) {
  std::string err;
  bool value;

  // True values.
  EXPECT_TRUE(absl::ParseFlag("True", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("true", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("TRUE", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("Yes", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("yes", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("YES", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("t", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("T", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("y", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("Y", &value, &err));
  EXPECT_TRUE(value);

  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_TRUE(value);

  // False values.
  EXPECT_TRUE(absl::ParseFlag("False", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("false", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("FALSE", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("No", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("no", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("NO", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("f", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("F", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("n", &value, &err));
  EXPECT_FALSE(value);
  EXPECT_TRUE(absl::ParseFlag("N", &value, &err));
  EXPECT_FALSE(value);

  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_FALSE(value);

  // Whitespace handling.
  EXPECT_TRUE(absl::ParseFlag("  true", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("true  ", &value, &err));
  EXPECT_TRUE(value);
  EXPECT_TRUE(absl::ParseFlag("  true   ", &value, &err));
  EXPECT_TRUE(value);

  // Invalid input.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("11", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("tt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt16Parsing) {
  std::string err;
  int16_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-18765", &value, &err));
  EXPECT_EQ(value, -18765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-001", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("-0x7FFD", &value, &err));
  EXPECT_EQ(value, -32765);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("40000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint16Parsing) {
  std::string err;
  uint16_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("70000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt32Parsing) {
  std::string err;
  int32_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-001", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);

  EXPECT_TRUE(absl::ParseFlag("-0x7FFFFFFD", &value, &err));
  EXPECT_EQ(value, -2147483645);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("70000000000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint32Parsing) {
  std::string err;
  uint32_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0X234", &value, &err));
  EXPECT_EQ(value, 564);
  EXPECT_TRUE(absl::ParseFlag("0xFFFFFFFD", &value, &err));
  EXPECT_EQ(value, 4294967293);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x22    ", &value, &err));
  EXPECT_EQ(value, 34);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("140000000000", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt64Parsing) {
  std::string err;
  int64_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0XFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("-0x7FFFFFFFFFFFFFFE", &value, &err));
  EXPECT_EQ(value, -9223372036854775806);
  EXPECT_TRUE(absl::ParseFlag("-0x02", &value, &err));
  EXPECT_EQ(value, -2);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);
  EXPECT_TRUE(absl::ParseFlag(" 0x7F    ", &value, &err));
  EXPECT_EQ(value, 127);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUInt64Parsing) {
  std::string err;
  uint64_t value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+13", &value, &err));
  EXPECT_EQ(value, 13);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000300", &value, &err));
  EXPECT_EQ(value, 300);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0XFFFF", &value, &err));
  EXPECT_EQ(value, 65535);
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10  ", &value, &err));
  EXPECT_EQ(value, 10);
  EXPECT_TRUE(absl::ParseFlag("  11", &value, &err));
  EXPECT_EQ(value, 11);
  EXPECT_TRUE(absl::ParseFlag("  012  ", &value, &err));
  EXPECT_EQ(value, 12);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0xFFFFFFFFFFFFFFFFFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt128Parsing) {
  std::string err;
  absl::int128 value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("-1", &value, &err));
  EXPECT_EQ(value, -1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("-98765", &value, &err));
  EXPECT_EQ(value, -98765);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0xFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("0xFFF0FFFFFFFFFFFFFFF", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(0x000000000000fff, 0xFFFFFFFFFFFFFFF));

  EXPECT_TRUE(absl::ParseFlag("-0x10000000000000000", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(-1, 0));
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("16  ", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  16", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  0100  ", &value, &err));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(absl::ParseFlag(" 0x7B    ", &value, &err));
  EXPECT_EQ(value, 123);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint128Parsing) {
  std::string err;
  absl::uint128 value;

  // Decimal values.
  EXPECT_TRUE(absl::ParseFlag("0", &value, &err));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("123", &value, &err));
  EXPECT_EQ(value, 123);
  EXPECT_TRUE(absl::ParseFlag("+3", &value, &err));
  EXPECT_EQ(value, 3);

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("001", &value, &err));
  EXPECT_EQ(value, 1);
  EXPECT_TRUE(absl::ParseFlag("0000100", &value, &err));
  EXPECT_EQ(value, 100);

  // Hex values.
  EXPECT_TRUE(absl::ParseFlag("0x10", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("0xFFFAAABBBCCCDDD", &value, &err));
  EXPECT_EQ(value, 1152827684197027293);
  EXPECT_TRUE(absl::ParseFlag("0xFFF0FFFFFFFFFFFFFFF", &value, &err));
  EXPECT_EQ(value, absl::MakeInt128(0x000000000000fff, 0xFFFFFFFFFFFFFFF));
  EXPECT_TRUE(absl::ParseFlag("+0x31", &value, &err));
  EXPECT_EQ(value, 49);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("16  ", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  16", &value, &err));
  EXPECT_EQ(value, 16);
  EXPECT_TRUE(absl::ParseFlag("  0100  ", &value, &err));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(absl::ParseFlag(" 0x7B    ", &value, &err));
  EXPECT_EQ(value, 123);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2U", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("FFF", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("-0x10000000000000000", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestFloatParsing) {
  std::string err;
  float value;

  // Ordinary values.
  EXPECT_TRUE(absl::ParseFlag("1.3", &value, &err));
  EXPECT_FLOAT_EQ(value, 1.3f);
  EXPECT_TRUE(absl::ParseFlag("-0.1", &value, &err));
  EXPECT_DOUBLE_EQ(value, -0.1f);
  EXPECT_TRUE(absl::ParseFlag("+0.01", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.01f);

  // Scientific values.
  EXPECT_TRUE(absl::ParseFlag("1.2e3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.2e3f);
  EXPECT_TRUE(absl::ParseFlag("9.8765402e-37", &value, &err));
  EXPECT_DOUBLE_EQ(value, 9.8765402e-37f);
  EXPECT_TRUE(absl::ParseFlag("0.11e+3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.11e+3f);
  EXPECT_TRUE(absl::ParseFlag("1.e-2300", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.f);
  EXPECT_TRUE(absl::ParseFlag("1.e+2300", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01.6", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.6f);
  EXPECT_TRUE(absl::ParseFlag("000.0001", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.0001f);

  // Trailing zero values.
  EXPECT_TRUE(absl::ParseFlag("-5.1000", &value, &err));
  EXPECT_DOUBLE_EQ(value, -5.1f);

  // Exceptional values.
  EXPECT_TRUE(absl::ParseFlag("NaN", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("Inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Hex values
  EXPECT_TRUE(absl::ParseFlag("0x10.23p12", &value, &err));
  EXPECT_DOUBLE_EQ(value, 66096.f);
  EXPECT_TRUE(absl::ParseFlag("-0xF1.A3p-2", &value, &err));
  EXPECT_NEAR(value, -60.4092f, 5e-5f);
  EXPECT_TRUE(absl::ParseFlag("+0x0.0AAp-12", &value, &err));
  EXPECT_NEAR(value, 1.01328e-05f, 5e-11f);
  EXPECT_TRUE(absl::ParseFlag("0x.01p1", &value, &err));
  EXPECT_NEAR(value, 0.0078125f, 5e-8f);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10.1  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 10.1f);
  EXPECT_TRUE(absl::ParseFlag("  2.34", &value, &err));
  EXPECT_DOUBLE_EQ(value, 2.34f);
  EXPECT_TRUE(absl::ParseFlag("  5.7  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 5.7f);
  EXPECT_TRUE(absl::ParseFlag("  -0xE0.F3p01  ", &value, &err));
  EXPECT_NEAR(value, -449.8984375f, 5e-8f);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2.3xxx", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0x0.1pAA", &value, &err));
  // TODO(rogeeff): below assertion should fail
  EXPECT_TRUE(absl::ParseFlag("0x0.1", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestDoubleParsing) {
  std::string err;
  double value;

  // Ordinary values.
  EXPECT_TRUE(absl::ParseFlag("1.3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.3);
  EXPECT_TRUE(absl::ParseFlag("-0.1", &value, &err));
  EXPECT_DOUBLE_EQ(value, -0.1);
  EXPECT_TRUE(absl::ParseFlag("+0.01", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.01);

  // Scientific values.
  EXPECT_TRUE(absl::ParseFlag("1.2e3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.2e3);
  EXPECT_TRUE(absl::ParseFlag("9.00000002e-123", &value, &err));
  EXPECT_DOUBLE_EQ(value, 9.00000002e-123);
  EXPECT_TRUE(absl::ParseFlag("0.11e+3", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.11e+3);
  EXPECT_TRUE(absl::ParseFlag("1.e-2300", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0);
  EXPECT_TRUE(absl::ParseFlag("1.e+2300", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Leading zero values.
  EXPECT_TRUE(absl::ParseFlag("01.6", &value, &err));
  EXPECT_DOUBLE_EQ(value, 1.6);
  EXPECT_TRUE(absl::ParseFlag("000.0001", &value, &err));
  EXPECT_DOUBLE_EQ(value, 0.0001);

  // Trailing zero values.
  EXPECT_TRUE(absl::ParseFlag("-5.1000", &value, &err));
  EXPECT_DOUBLE_EQ(value, -5.1);

  // Exceptional values.
  EXPECT_TRUE(absl::ParseFlag("NaN", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("nan", &value, &err));
  EXPECT_TRUE(std::isnan(value));
  EXPECT_TRUE(absl::ParseFlag("Inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));
  EXPECT_TRUE(absl::ParseFlag("inf", &value, &err));
  EXPECT_TRUE(std::isinf(value));

  // Hex values
  EXPECT_TRUE(absl::ParseFlag("0x10.23p12", &value, &err));
  EXPECT_DOUBLE_EQ(value, 66096);
  EXPECT_TRUE(absl::ParseFlag("-0xF1.A3p-2", &value, &err));
  EXPECT_NEAR(value, -60.4092, 5e-5);
  EXPECT_TRUE(absl::ParseFlag("+0x0.0AAp-12", &value, &err));
  EXPECT_NEAR(value, 1.01328e-05, 5e-11);
  EXPECT_TRUE(absl::ParseFlag("0x.01p1", &value, &err));
  EXPECT_NEAR(value, 0.0078125, 5e-8);

  // Whitespace handling
  EXPECT_TRUE(absl::ParseFlag("10.1  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 10.1);
  EXPECT_TRUE(absl::ParseFlag("  2.34", &value, &err));
  EXPECT_DOUBLE_EQ(value, 2.34);
  EXPECT_TRUE(absl::ParseFlag("  5.7  ", &value, &err));
  EXPECT_DOUBLE_EQ(value, 5.7);
  EXPECT_TRUE(absl::ParseFlag("  -0xE0.F3p01  ", &value, &err));
  EXPECT_NEAR(value, -449.8984375, 5e-8);

  // Invalid values.
  EXPECT_FALSE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(absl::ParseFlag(" ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("  ", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("--1", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\n", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("\t", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("2.3xxx", &value, &err));
  EXPECT_FALSE(absl::ParseFlag("0x0.1pAA", &value, &err));
  // TODO(rogeeff): below assertion should fail
  EXPECT_TRUE(absl::ParseFlag("0x0.1", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestStringParsing) {
  std::string err;
  std::string value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, "");
  EXPECT_TRUE(absl::ParseFlag(" ", &value, &err));
  EXPECT_EQ(value, " ");
  EXPECT_TRUE(absl::ParseFlag("   ", &value, &err));
  EXPECT_EQ(value, "   ");
  EXPECT_TRUE(absl::ParseFlag("\n", &value, &err));
  EXPECT_EQ(value, "\n");
  EXPECT_TRUE(absl::ParseFlag("\t", &value, &err));
  EXPECT_EQ(value, "\t");
  EXPECT_TRUE(absl::ParseFlag("asdfg", &value, &err));
  EXPECT_EQ(value, "asdfg");
  EXPECT_TRUE(absl::ParseFlag("asdf ghjk", &value, &err));
  EXPECT_EQ(value, "asdf ghjk");
  EXPECT_TRUE(absl::ParseFlag("a\nb\nc", &value, &err));
  EXPECT_EQ(value, "a\nb\nc");
  EXPECT_TRUE(absl::ParseFlag("asd\0fgh", &value, &err));
  EXPECT_EQ(value, "asd");
  EXPECT_TRUE(absl::ParseFlag("\\\\", &value, &err));
  EXPECT_EQ(value, "\\\\");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestVectorOfStringParsing) {
  std::string err;
  std::vector<std::string> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>{});
  EXPECT_TRUE(absl::ParseFlag("1", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"1"}));
  EXPECT_TRUE(absl::ParseFlag("a,b", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "b"}));
  EXPECT_TRUE(absl::ParseFlag("a,b,c,", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "b", "c", ""}));
  EXPECT_TRUE(absl::ParseFlag("a,,", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", "", ""}));
  EXPECT_TRUE(absl::ParseFlag(",", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"", ""}));
  EXPECT_TRUE(absl::ParseFlag("a, b,c ", &value, &err));
  EXPECT_EQ(value, std::vector<std::string>({"a", " b", "c "}));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalBoolParsing) {
  std::string err;
  absl::optional<bool> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("true", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_TRUE(*value);

  EXPECT_TRUE(absl::ParseFlag("false", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_FALSE(*value);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalIntParsing) {
  std::string err;
  absl::optional<int> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("10", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 10);

  EXPECT_TRUE(absl::ParseFlag("0x1F", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 31);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalDoubleParsing) {
  std::string err;
  absl::optional<double> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag("1.11", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 1.11);

  EXPECT_TRUE(absl::ParseFlag("-0.12", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, -0.12);

  EXPECT_FALSE(absl::ParseFlag("nullopt", &value, &err));
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalStringParsing) {
  std::string err;
  absl::optional<std::string> value;

  EXPECT_TRUE(absl::ParseFlag("", &value, &err));
  EXPECT_FALSE(value.has_value());

  EXPECT_TRUE(absl::ParseFlag(" ", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, " ");

  EXPECT_TRUE(absl::ParseFlag("aqswde", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "aqswde");

  EXPECT_TRUE(absl::ParseFlag("nullopt", &value, &err));
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "nullopt");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestBoolUnparsing) {
  EXPECT_EQ(absl::UnparseFlag(true), "true");
  EXPECT_EQ(absl::UnparseFlag(false), "false");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt16Unparsing) {
  int16_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 9876;
  EXPECT_EQ(absl::UnparseFlag(value), "9876");
  value = -987;
  EXPECT_EQ(absl::UnparseFlag(value), "-987");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint16Unparsing) {
  uint16_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 19876;
  EXPECT_EQ(absl::UnparseFlag(value), "19876");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt32Unparsing) {
  int32_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 12345;
  EXPECT_EQ(absl::UnparseFlag(value), "12345");
  value = -987;
  EXPECT_EQ(absl::UnparseFlag(value), "-987");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint32Unparsing) {
  uint32_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 1234500;
  EXPECT_EQ(absl::UnparseFlag(value), "1234500");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt64Unparsing) {
  int64_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = -987654321L;
  EXPECT_EQ(absl::UnparseFlag(value), "-987654321");
  value = 0x7FFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "9223372036854775807");
  value = 0xFFFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint64Unparsing) {
  uint64_t value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = 0xFFFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "18446744073709551615");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestInt128Unparsing) {
  absl::int128 value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -1;
  EXPECT_EQ(absl::UnparseFlag(value), "-1");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = -987654321L;
  EXPECT_EQ(absl::UnparseFlag(value), "-987654321");
  value = 0x7FFFFFFFFFFFFFFF;
  EXPECT_EQ(absl::UnparseFlag(value), "9223372036854775807");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestUint128Unparsing) {
  absl::uint128 value;

  value = 1;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = 123456789L;
  EXPECT_EQ(absl::UnparseFlag(value), "123456789");
  value = absl::MakeUint128(0, 0xFFFFFFFFFFFFFFFF);
  EXPECT_EQ(absl::UnparseFlag(value), "18446744073709551615");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestFloatUnparsing) {
  float value;

  value = 1.1f;
  EXPECT_EQ(absl::UnparseFlag(value), "1.1");
  value = 0.01f;
  EXPECT_EQ(absl::UnparseFlag(value), "0.01");
  value = 1.23e-2f;
  EXPECT_EQ(absl::UnparseFlag(value), "0.0123");
  value = -0.71f;
  EXPECT_EQ(absl::UnparseFlag(value), "-0.71");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestDoubleUnparsing) {
  double value;

  value = 1.1;
  EXPECT_EQ(absl::UnparseFlag(value), "1.1");
  value = 0.01;
  EXPECT_EQ(absl::UnparseFlag(value), "0.01");
  value = 1.23e-2;
  EXPECT_EQ(absl::UnparseFlag(value), "0.0123");
  value = -0.71;
  EXPECT_EQ(absl::UnparseFlag(value), "-0.71");
  value = -0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = std::nan("");
  EXPECT_EQ(absl::UnparseFlag(value), "nan");
  value = std::numeric_limits<double>::infinity();
  EXPECT_EQ(absl::UnparseFlag(value), "inf");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestStringUnparsing) {
  EXPECT_EQ(absl::UnparseFlag(""), "");
  EXPECT_EQ(absl::UnparseFlag(" "), " ");
  EXPECT_EQ(absl::UnparseFlag("qwerty"), "qwerty");
  EXPECT_EQ(absl::UnparseFlag("ASDFGH"), "ASDFGH");
  EXPECT_EQ(absl::UnparseFlag("\n\t  "), "\n\t  ");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalBoolUnparsing) {
  absl::optional<bool> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = true;
  EXPECT_EQ(absl::UnparseFlag(value), "true");
  value = false;
  EXPECT_EQ(absl::UnparseFlag(value), "false");
  value = absl::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalIntUnparsing) {
  absl::optional<int> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = 0;
  EXPECT_EQ(absl::UnparseFlag(value), "0");
  value = -12;
  EXPECT_EQ(absl::UnparseFlag(value), "-12");
  value = absl::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalDoubleUnparsing) {
  absl::optional<double> value;

  EXPECT_EQ(absl::UnparseFlag(value), "");
  value = 1.;
  EXPECT_EQ(absl::UnparseFlag(value), "1");
  value = -1.23;
  EXPECT_EQ(absl::UnparseFlag(value), "-1.23");
  value = absl::nullopt;
  EXPECT_EQ(absl::UnparseFlag(value), "");
}

// --------------------------------------------------------------------

TEST(MarshallingTest, TestOptionalStringUnparsing) {
  absl::optional<std::string> strvalue;
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  strvalue = "asdfg";
  EXPECT_EQ(absl::UnparseFlag(strvalue), "asdfg");

  strvalue = " ";
  EXPECT_EQ(absl::UnparseFlag(strvalue), " ");

  strvalue = "";  // It is UB to set an optional string flag to ""
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");
}

// --------------------------------------------------------------------

#if defined(ABSL_HAVE_STD_OPTIONAL) && !defined(ABSL_USES_STD_OPTIONAL)

TEST(MarshallingTest, TestStdOptionalUnparsing) {
  std::optional<std::string> strvalue;
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  strvalue = "asdfg";
  EXPECT_EQ(absl::UnparseFlag(strvalue), "asdfg");

  strvalue = " ";
  EXPECT_EQ(absl::UnparseFlag(strvalue), " ");

  strvalue = "";  // It is UB to set an optional string flag to ""
  EXPECT_EQ(absl::UnparseFlag(strvalue), "");

  std::optional<int> intvalue;
  EXPECT_EQ(absl::UnparseFlag(intvalue), "");

  intvalue = 10;
  EXPECT_EQ(absl::UnparseFlag(intvalue), "10");
}

// --------------------------------------------------------------------

#endif

template <typename T>
void TestRoundtrip(T v) {
  T new_v;
  std::string err;
  EXPECT_TRUE(absl::ParseFlag(absl::UnparseFlag(v), &new_v, &err));
  EXPECT_EQ(new_v, v);
}

TEST(MarshallingTest, TestFloatRoundTrip) {
  TestRoundtrip(0.1f);
  TestRoundtrip(0.12f);
  TestRoundtrip(0.123f);
  TestRoundtrip(0.1234f);
  TestRoundtrip(0.12345f);
  TestRoundtrip(0.123456f);
  TestRoundtrip(0.1234567f);
  TestRoundtrip(0.12345678f);

  TestRoundtrip(0.1e20f);
  TestRoundtrip(0.12e20f);
  TestRoundtrip(0.123e20f);
  TestRoundtrip(0.1234e20f);
  TestRoundtrip(0.12345e20f);
  TestRoundtrip(0.123456e20f);
  TestRoundtrip(0.1234567e20f);
  TestRoundtrip(0.12345678e20f);

  TestRoundtrip(0.1e-20f);
  TestRoundtrip(0.12e-20f);
  TestRoundtrip(0.123e-20f);
  TestRoundtrip(0.1234e-20f);
  TestRoundtrip(0.12345e-20f);
  TestRoundtrip(0.123456e-20f);
  TestRoundtrip(0.1234567e-20f);
  TestRoundtrip(0.12345678e-20f);
}

TEST(MarshallingTest, TestDoubleRoundTrip) {
  TestRoundtrip(0.1);
  TestRoundtrip(0.12);
  TestRoundtrip(0.123);
  TestRoundtrip(0.1234);
  TestRoundtrip(0.12345);
  TestRoundtrip(0.123456);
  TestRoundtrip(0.1234567);
  TestRoundtrip(0.12345678);
  TestRoundtrip(0.123456789);
  TestRoundtrip(0.1234567891);
  TestRoundtrip(0.12345678912);
  TestRoundtrip(0.123456789123);
  TestRoundtrip(0.1234567891234);
  TestRoundtrip(0.12345678912345);
  TestRoundtrip(0.123456789123456);
  TestRoundtrip(0.1234567891234567);
  TestRoundtrip(0.12345678912345678);

  TestRoundtrip(0.1e50);
  TestRoundtrip(0.12e50);
  TestRoundtrip(0.123e50);
  TestRoundtrip(0.1234e50);
  TestRoundtrip(0.12345e50);
  TestRoundtrip(0.123456e50);
  TestRoundtrip(0.1234567e50);
  TestRoundtrip(0.12345678e50);
  TestRoundtrip(0.123456789e50);
  TestRoundtrip(0.1234567891e50);
  TestRoundtrip(0.12345678912e50);
  TestRoundtrip(0.123456789123e50);
  TestRoundtrip(0.1234567891234e50);
  TestRoundtrip(0.12345678912345e50);
  TestRoundtrip(0.123456789123456e50);
  TestRoundtrip(0.1234567891234567e50);
  TestRoundtrip(0.12345678912345678e50);

  TestRoundtrip(0.1e-50);
  TestRoundtrip(0.12e-50);
  TestRoundtrip(0.123e-50);
  TestRoundtrip(0.1234e-50);
  TestRoundtrip(0.12345e-50);
  TestRoundtrip(0.123456e-50);
  TestRoundtrip(0.1234567e-50);
  TestRoundtrip(0.12345678e-50);
  TestRoundtrip(0.123456789e-50);
  TestRoundtrip(0.1234567891e-50);
  TestRoundtrip(0.12345678912e-50);
  TestRoundtrip(0.123456789123e-50);
  TestRoundtrip(0.1234567891234e-50);
  TestRoundtrip(0.12345678912345e-50);
  TestRoundtrip(0.123456789123456e-50);
  TestRoundtrip(0.1234567891234567e-50);
  TestRoundtrip(0.12345678912345678e-50);
}

}  // namespace
