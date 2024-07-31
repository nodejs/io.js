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

#include <cstddef>

#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/substitute.h"

namespace {

TEST(CharFormatting, Char) {
  const char v = 'A';

  // Desired behavior: does not compile:
  // EXPECT_EQ(absl::StrCat(v, "B"), "AB");
  // EXPECT_EQ(absl::StrFormat("%vB", v), "AB");

  // Legacy behavior: format as char:
  EXPECT_EQ(absl::Substitute("$0B", v), "AB");
}

enum CharEnum : char {};
TEST(CharFormatting, CharEnum) {
  auto v = static_cast<CharEnum>('A');

  // Desired behavior: format as decimal
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");

  // Legacy behavior: format as character:

  // Some older versions of gcc behave differently in this one case
#if !defined(__GNUC__) || defined(__clang__)
  EXPECT_EQ(absl::Substitute("$0B", v), "AB");
#endif
}

enum class CharEnumClass: char {};
TEST(CharFormatting, CharEnumClass) {
  auto v = static_cast<CharEnumClass>('A');

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");

  // Legacy behavior: format as character:
  EXPECT_EQ(absl::Substitute("$0B", v), "AB");
}

TEST(CharFormatting, UnsignedChar) {
  const unsigned char v = 'A';

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  const unsigned char w = 255;
  EXPECT_EQ(absl::StrCat(w, "B"), "255B");
  EXPECT_EQ(absl::Substitute("$0B", w), "255B");
  // EXPECT_EQ(absl::StrFormat("%vB", v), "255B");
}

TEST(CharFormatting, SignedChar) {
  const signed char v = 'A';

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  const signed char w = -128;
  EXPECT_EQ(absl::StrCat(w, "B"), "-128B");
  EXPECT_EQ(absl::Substitute("$0B", w), "-128B");
}

enum UnsignedCharEnum : unsigned char {};
TEST(CharFormatting, UnsignedCharEnum) {
  auto v = static_cast<UnsignedCharEnum>('A');

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  auto w = static_cast<UnsignedCharEnum>(255);
  EXPECT_EQ(absl::StrCat(w, "B"), "255B");
  EXPECT_EQ(absl::Substitute("$0B", w), "255B");
  EXPECT_EQ(absl::StrFormat("%vB", w), "255B");
}

enum SignedCharEnum : signed char {};
TEST(CharFormatting, SignedCharEnum) {
  auto v = static_cast<SignedCharEnum>('A');

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  auto w = static_cast<SignedCharEnum>(-128);
  EXPECT_EQ(absl::StrCat(w, "B"), "-128B");
  EXPECT_EQ(absl::Substitute("$0B", w), "-128B");
  EXPECT_EQ(absl::StrFormat("%vB", w), "-128B");
}

enum class UnsignedCharEnumClass : unsigned char {};
TEST(CharFormatting, UnsignedCharEnumClass) {
  auto v = static_cast<UnsignedCharEnumClass>('A');

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  auto w = static_cast<UnsignedCharEnumClass>(255);
  EXPECT_EQ(absl::StrCat(w, "B"), "255B");
  EXPECT_EQ(absl::Substitute("$0B", w), "255B");
  EXPECT_EQ(absl::StrFormat("%vB", w), "255B");
}

enum SignedCharEnumClass : signed char {};
TEST(CharFormatting, SignedCharEnumClass) {
  auto v = static_cast<SignedCharEnumClass>('A');

  // Desired behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");

  // Signedness check
  auto w = static_cast<SignedCharEnumClass>(-128);
  EXPECT_EQ(absl::StrCat(w, "B"), "-128B");
  EXPECT_EQ(absl::Substitute("$0B", w), "-128B");
  EXPECT_EQ(absl::StrFormat("%vB", w), "-128B");
}

#ifdef __cpp_lib_byte
TEST(CharFormatting, StdByte) {
  auto v = static_cast<std::byte>('A');
  // Desired behavior: format as 0xff
  // (No APIs do this today.)

  // Legacy behavior: format as decimal:
  EXPECT_EQ(absl::StrCat(v, "B"), "65B");
  EXPECT_EQ(absl::Substitute("$0B", v), "65B");
  EXPECT_EQ(absl::StrFormat("%vB", v), "65B");
}
#endif  // _cpp_lib_byte

}  // namespace
