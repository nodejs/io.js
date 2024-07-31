// Copyright 2017 The Abseil Authors.
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

// This file contains functions that remove a defined part from the string,
// i.e., strip the string.

#include "absl/strings/strip.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace {

TEST(Strip, ConsumePrefixOneChar) {
  absl::string_view input("abc");
  EXPECT_TRUE(absl::ConsumePrefix(&input, "a"));
  EXPECT_EQ(input, "bc");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "x"));
  EXPECT_EQ(input, "bc");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "b"));
  EXPECT_EQ(input, "c");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "c"));
  EXPECT_EQ(input, "");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "a"));
  EXPECT_EQ(input, "");
}

TEST(Strip, ConsumePrefix) {
  absl::string_view input("abcdef");
  EXPECT_FALSE(absl::ConsumePrefix(&input, "abcdefg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "abce"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumePrefix(&input, ""));
  EXPECT_EQ(input, "abcdef");

  EXPECT_FALSE(absl::ConsumePrefix(&input, "abcdeg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumePrefix(&input, "abcdef"));
  EXPECT_EQ(input, "");

  input = "abcdef";
  EXPECT_TRUE(absl::ConsumePrefix(&input, "abcde"));
  EXPECT_EQ(input, "f");
}

TEST(Strip, ConsumeSuffix) {
  absl::string_view input("abcdef");
  EXPECT_FALSE(absl::ConsumeSuffix(&input, "abcdefg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, ""));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "def"));
  EXPECT_EQ(input, "abc");

  input = "abcdef";
  EXPECT_FALSE(absl::ConsumeSuffix(&input, "abcdeg"));
  EXPECT_EQ(input, "abcdef");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "f"));
  EXPECT_EQ(input, "abcde");

  EXPECT_TRUE(absl::ConsumeSuffix(&input, "abcde"));
  EXPECT_EQ(input, "");
}

TEST(Strip, StripPrefix) {
  const absl::string_view null_str;

  EXPECT_EQ(absl::StripPrefix("foobar", "foo"), "bar");
  EXPECT_EQ(absl::StripPrefix("foobar", ""), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", null_str), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", "foobar"), "");
  EXPECT_EQ(absl::StripPrefix("foobar", "bar"), "foobar");
  EXPECT_EQ(absl::StripPrefix("foobar", "foobarr"), "foobar");
  EXPECT_EQ(absl::StripPrefix("", ""), "");
}

TEST(Strip, StripSuffix) {
  const absl::string_view null_str;

  EXPECT_EQ(absl::StripSuffix("foobar", "bar"), "foo");
  EXPECT_EQ(absl::StripSuffix("foobar", ""), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", null_str), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", "foobar"), "");
  EXPECT_EQ(absl::StripSuffix("foobar", "foo"), "foobar");
  EXPECT_EQ(absl::StripSuffix("foobar", "ffoobar"), "foobar");
  EXPECT_EQ(absl::StripSuffix("", ""), "");
}

TEST(Strip, RemoveExtraAsciiWhitespace) {
  const char* inputs[] = {
      "No extra space",
      "  Leading whitespace",
      "Trailing whitespace  ",
      "  Leading and trailing  ",
      " Whitespace \t  in\v   middle  ",
      "'Eeeeep!  \n Newlines!\n",
      "nospaces",
  };
  const char* outputs[] = {
      "No extra space",
      "Leading whitespace",
      "Trailing whitespace",
      "Leading and trailing",
      "Whitespace in middle",
      "'Eeeeep! Newlines!",
      "nospaces",
  };
  int NUM_TESTS = 7;

  for (int i = 0; i < NUM_TESTS; i++) {
    std::string s(inputs[i]);
    absl::RemoveExtraAsciiWhitespace(&s);
    EXPECT_STREQ(outputs[i], s.c_str());
  }

  // Test that absl::RemoveExtraAsciiWhitespace returns immediately for empty
  // strings (It was adding the \0 character to the C++ std::string, which broke
  // tests involving empty())
  std::string zero_string = "";
  assert(zero_string.empty());
  absl::RemoveExtraAsciiWhitespace(&zero_string);
  EXPECT_EQ(zero_string.size(), 0);
  EXPECT_TRUE(zero_string.empty());
}

TEST(Strip, StripTrailingAsciiWhitespace) {
  std::string test = "foo  ";
  absl::StripTrailingAsciiWhitespace(&test);
  EXPECT_EQ(test, "foo");

  test = "   ";
  absl::StripTrailingAsciiWhitespace(&test);
  EXPECT_EQ(test, "");

  test = "";
  absl::StripTrailingAsciiWhitespace(&test);
  EXPECT_EQ(test, "");

  test = " abc\t";
  absl::StripTrailingAsciiWhitespace(&test);
  EXPECT_EQ(test, " abc");
}

TEST(String, StripLeadingAsciiWhitespace) {
  absl::string_view orig = "\t  \n\f\r\n\vfoo";
  EXPECT_EQ("foo", absl::StripLeadingAsciiWhitespace(orig));
  orig = "\t  \n\f\r\v\n\t  \n\f\r\v\n";
  EXPECT_EQ(absl::string_view(), absl::StripLeadingAsciiWhitespace(orig));
}

TEST(Strip, StripAsciiWhitespace) {
  std::string test2 = "\t  \f\r\n\vfoo \t\f\r\v\n";
  absl::StripAsciiWhitespace(&test2);
  EXPECT_EQ(test2, "foo");
  std::string test3 = "bar";
  absl::StripAsciiWhitespace(&test3);
  EXPECT_EQ(test3, "bar");
  std::string test4 = "\t  \f\r\n\vfoo";
  absl::StripAsciiWhitespace(&test4);
  EXPECT_EQ(test4, "foo");
  std::string test5 = "foo \t\f\r\v\n";
  absl::StripAsciiWhitespace(&test5);
  EXPECT_EQ(test5, "foo");
  absl::string_view test6("\t  \f\r\n\vfoo \t\f\r\v\n");
  test6 = absl::StripAsciiWhitespace(test6);
  EXPECT_EQ(test6, "foo");
  test6 = absl::StripAsciiWhitespace(test6);
  EXPECT_EQ(test6, "foo");  // already stripped
}

}  // namespace
