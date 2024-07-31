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

#include "absl/strings/match.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/strings/string_view.h"

namespace {

TEST(MatchTest, StartsWith) {
  const std::string s1("123\0abc", 7);
  const absl::string_view a("foobar");
  const absl::string_view b(s1);
  const absl::string_view e;
  EXPECT_TRUE(absl::StartsWith(a, a));
  EXPECT_TRUE(absl::StartsWith(a, "foo"));
  EXPECT_TRUE(absl::StartsWith(a, e));
  EXPECT_TRUE(absl::StartsWith(b, s1));
  EXPECT_TRUE(absl::StartsWith(b, b));
  EXPECT_TRUE(absl::StartsWith(b, e));
  EXPECT_TRUE(absl::StartsWith(e, ""));
  EXPECT_FALSE(absl::StartsWith(a, b));
  EXPECT_FALSE(absl::StartsWith(b, a));
  EXPECT_FALSE(absl::StartsWith(e, a));
}

TEST(MatchTest, EndsWith) {
  const std::string s1("123\0abc", 7);
  const absl::string_view a("foobar");
  const absl::string_view b(s1);
  const absl::string_view e;
  EXPECT_TRUE(absl::EndsWith(a, a));
  EXPECT_TRUE(absl::EndsWith(a, "bar"));
  EXPECT_TRUE(absl::EndsWith(a, e));
  EXPECT_TRUE(absl::EndsWith(b, s1));
  EXPECT_TRUE(absl::EndsWith(b, b));
  EXPECT_TRUE(absl::EndsWith(b, e));
  EXPECT_TRUE(absl::EndsWith(e, ""));
  EXPECT_FALSE(absl::EndsWith(a, b));
  EXPECT_FALSE(absl::EndsWith(b, a));
  EXPECT_FALSE(absl::EndsWith(e, a));
}

TEST(MatchTest, Contains) {
  absl::string_view a("abcdefg");
  absl::string_view b("abcd");
  absl::string_view c("efg");
  absl::string_view d("gh");
  EXPECT_TRUE(absl::StrContains(a, a));
  EXPECT_TRUE(absl::StrContains(a, b));
  EXPECT_TRUE(absl::StrContains(a, c));
  EXPECT_FALSE(absl::StrContains(a, d));
  EXPECT_TRUE(absl::StrContains("", ""));
  EXPECT_TRUE(absl::StrContains("abc", ""));
  EXPECT_FALSE(absl::StrContains("", "a"));
}

TEST(MatchTest, ContainsChar) {
  absl::string_view a("abcdefg");
  absl::string_view b("abcd");
  EXPECT_TRUE(absl::StrContains(a, 'a'));
  EXPECT_TRUE(absl::StrContains(a, 'b'));
  EXPECT_TRUE(absl::StrContains(a, 'e'));
  EXPECT_FALSE(absl::StrContains(a, 'h'));

  EXPECT_TRUE(absl::StrContains(b, 'a'));
  EXPECT_TRUE(absl::StrContains(b, 'b'));
  EXPECT_FALSE(absl::StrContains(b, 'e'));
  EXPECT_FALSE(absl::StrContains(b, 'h'));

  EXPECT_FALSE(absl::StrContains("", 'a'));
  EXPECT_FALSE(absl::StrContains("", 'a'));
}

TEST(MatchTest, ContainsNull) {
  const std::string s = "foo";
  const char* cs = "foo";
  const absl::string_view sv("foo");
  const absl::string_view sv2("foo\0bar", 4);
  EXPECT_EQ(s, "foo");
  EXPECT_EQ(sv, "foo");
  EXPECT_NE(sv2, "foo");
  EXPECT_TRUE(absl::EndsWith(s, sv));
  EXPECT_TRUE(absl::StartsWith(cs, sv));
  EXPECT_TRUE(absl::StrContains(cs, sv));
  EXPECT_FALSE(absl::StrContains(cs, sv2));
}

TEST(MatchTest, EqualsIgnoreCase) {
  std::string text = "the";
  absl::string_view data(text);

  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "The"));
  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "THE"));
  EXPECT_TRUE(absl::EqualsIgnoreCase(data, "the"));
  EXPECT_FALSE(absl::EqualsIgnoreCase(data, "Quick"));
  EXPECT_FALSE(absl::EqualsIgnoreCase(data, "then"));
}

TEST(MatchTest, StartsWithIgnoreCase) {
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", "foo"));
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", "Fo"));
  EXPECT_TRUE(absl::StartsWithIgnoreCase("foo", ""));
  EXPECT_FALSE(absl::StartsWithIgnoreCase("foo", "fooo"));
  EXPECT_FALSE(absl::StartsWithIgnoreCase("", "fo"));
}

TEST(MatchTest, EndsWithIgnoreCase) {
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", "foo"));
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", "Oo"));
  EXPECT_TRUE(absl::EndsWithIgnoreCase("foo", ""));
  EXPECT_FALSE(absl::EndsWithIgnoreCase("foo", "fooo"));
  EXPECT_FALSE(absl::EndsWithIgnoreCase("", "fo"));
}

TEST(MatchTest, ContainsIgnoreCase) {
  EXPECT_TRUE(absl::StrContainsIgnoreCase("foo", "foo"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("FOO", "Foo"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("--FOO", "Foo"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("FOO--", "Foo"));
  EXPECT_FALSE(absl::StrContainsIgnoreCase("BAR", "Foo"));
  EXPECT_FALSE(absl::StrContainsIgnoreCase("BAR", "Foo"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("123456", "123456"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("123456", "234"));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("", ""));
  EXPECT_TRUE(absl::StrContainsIgnoreCase("abc", ""));
  EXPECT_FALSE(absl::StrContainsIgnoreCase("", "a"));
}

TEST(MatchTest, ContainsCharIgnoreCase) {
  absl::string_view a("AaBCdefg!");
  absl::string_view b("AaBCd!");
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'a'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'A'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'b'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'B'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'e'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, 'E'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(a, 'h'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(a, 'H'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(a, '!'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(a, '?'));

  EXPECT_TRUE(absl::StrContainsIgnoreCase(b, 'a'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(b, 'A'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(b, 'b'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(b, 'B'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(b, 'e'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(b, 'E'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(b, 'h'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(b, 'H'));
  EXPECT_TRUE(absl::StrContainsIgnoreCase(b, '!'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase(b, '?'));

  EXPECT_FALSE(absl::StrContainsIgnoreCase("", 'a'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase("", 'A'));
  EXPECT_FALSE(absl::StrContainsIgnoreCase("", '0'));
}

TEST(MatchTest, FindLongestCommonPrefix) {
  EXPECT_EQ(absl::FindLongestCommonPrefix("", ""), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("", "abc"), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", ""), "");
  EXPECT_EQ(absl::FindLongestCommonPrefix("ab", "abc"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "ab"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "abd"), "ab");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abc", "abcd"), "abc");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abcd", "abcd"), "abcd");
  EXPECT_EQ(absl::FindLongestCommonPrefix("abcd", "efgh"), "");

  // "abcde" v. "abc" but in the middle of other data
  EXPECT_EQ(absl::FindLongestCommonPrefix(
                absl::string_view("1234 abcdef").substr(5, 5),
                absl::string_view("5678 abcdef").substr(5, 3)),
            "abc");
}

// Since the little-endian implementation involves a bit of if-else and various
// return paths, the following tests aims to provide full test coverage of the
// implementation.
TEST(MatchTest, FindLongestCommonPrefixLoad16Mismatch) {
  const std::string x1 = "abcdefgh";
  const std::string x2 = "abcde_";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(MatchTest, FindLongestCommonPrefixLoad16MatchesNoLast) {
  const std::string x1 = "abcdef";
  const std::string x2 = "abcdef";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdef");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdef");
}

TEST(MatchTest, FindLongestCommonPrefixLoad16MatchesLastCharMismatches) {
  const std::string x1 = "abcdefg";
  const std::string x2 = "abcdef_h";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdef");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdef");
}

TEST(MatchTest, FindLongestCommonPrefixLoad16MatchesLastMatches) {
  const std::string x1 = "abcde";
  const std::string x2 = "abcdefgh";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(MatchTest, FindLongestCommonPrefixSize8Load64Mismatches) {
  const std::string x1 = "abcdefghijk";
  const std::string x2 = "abcde_g_";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcde");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcde");
}

TEST(MatchTest, FindLongestCommonPrefixSize8Load64Matches) {
  const std::string x1 = "abcdefgh";
  const std::string x2 = "abcdefgh";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "abcdefgh");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "abcdefgh");
}

TEST(MatchTest, FindLongestCommonPrefixSize15Load64Mismatches) {
  const std::string x1 = "012345670123456";
  const std::string x2 = "0123456701_34_6";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "0123456701");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "0123456701");
}

TEST(MatchTest, FindLongestCommonPrefixSize15Load64Matches) {
  const std::string x1 = "012345670123456";
  const std::string x2 = "0123456701234567";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "012345670123456");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "012345670123456");
}

TEST(MatchTest, FindLongestCommonPrefixSizeFirstByteOfLast8BytesMismatch) {
  const std::string x1 = "012345670123456701234567";
  const std::string x2 = "0123456701234567_1234567";
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), "0123456701234567");
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), "0123456701234567");
}

TEST(MatchTest, FindLongestCommonPrefixLargeLastCharMismatches) {
  const std::string x1(300, 'x');
  std::string x2 = x1;
  x2.back() = '#';
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), std::string(299, 'x'));
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), std::string(299, 'x'));
}

TEST(MatchTest, FindLongestCommonPrefixLargeFullMatch) {
  const std::string x1(300, 'x');
  const std::string x2 = x1;
  EXPECT_EQ(absl::FindLongestCommonPrefix(x1, x2), std::string(300, 'x'));
  EXPECT_EQ(absl::FindLongestCommonPrefix(x2, x1), std::string(300, 'x'));
}

TEST(MatchTest, FindLongestCommonSuffix) {
  EXPECT_EQ(absl::FindLongestCommonSuffix("", ""), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("", "abc"), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", ""), "");
  EXPECT_EQ(absl::FindLongestCommonSuffix("bc", "abc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", "bc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abc", "dbc"), "bc");
  EXPECT_EQ(absl::FindLongestCommonSuffix("bcd", "abcd"), "bcd");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abcd", "abcd"), "abcd");
  EXPECT_EQ(absl::FindLongestCommonSuffix("abcd", "efgh"), "");

  // "abcde" v. "cde" but in the middle of other data
  EXPECT_EQ(absl::FindLongestCommonSuffix(
                absl::string_view("1234 abcdef").substr(5, 5),
                absl::string_view("5678 abcdef").substr(7, 3)),
            "cde");
}

}  // namespace
