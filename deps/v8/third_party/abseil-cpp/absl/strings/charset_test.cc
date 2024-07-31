// Copyright 2020 The Abseil Authors.
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

#include "absl/strings/charset.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"

namespace {

constexpr absl::CharSet everything_map = ~absl::CharSet();
constexpr absl::CharSet nothing_map = absl::CharSet();

TEST(Charmap, AllTests) {
  const absl::CharSet also_nothing_map("");
  EXPECT_TRUE(everything_map.contains('\0'));
  EXPECT_FALSE(nothing_map.contains('\0'));
  EXPECT_FALSE(also_nothing_map.contains('\0'));
  for (unsigned char ch = 1; ch != 0; ++ch) {
    SCOPED_TRACE(ch);
    EXPECT_TRUE(everything_map.contains(ch));
    EXPECT_FALSE(nothing_map.contains(ch));
    EXPECT_FALSE(also_nothing_map.contains(ch));
  }

  const absl::CharSet symbols(absl::string_view("&@#@^!@?", 5));
  EXPECT_TRUE(symbols.contains('&'));
  EXPECT_TRUE(symbols.contains('@'));
  EXPECT_TRUE(symbols.contains('#'));
  EXPECT_TRUE(symbols.contains('^'));
  EXPECT_FALSE(symbols.contains('!'));
  EXPECT_FALSE(symbols.contains('?'));
  int cnt = 0;
  for (unsigned char ch = 1; ch != 0; ++ch) cnt += symbols.contains(ch);
  EXPECT_EQ(cnt, 4);

  const absl::CharSet lets(absl::string_view("^abcde", 3));
  const absl::CharSet lets2(absl::string_view("fghij\0klmnop", 10));
  const absl::CharSet lets3("fghij\0klmnop");
  EXPECT_TRUE(lets2.contains('k'));
  EXPECT_FALSE(lets3.contains('k'));

  EXPECT_FALSE((symbols & lets).empty());
  EXPECT_TRUE((lets2 & lets).empty());
  EXPECT_FALSE((lets & symbols).empty());
  EXPECT_TRUE((lets & lets2).empty());

  EXPECT_TRUE(nothing_map.empty());
  EXPECT_FALSE(lets.empty());
}

std::string Members(const absl::CharSet& m) {
  std::string r;
  for (size_t i = 0; i < 256; ++i)
    if (m.contains(i)) r.push_back(i);
  return r;
}

std::string ClosedRangeString(unsigned char lo, unsigned char hi) {
  // Don't depend on lo<hi. Just increment until lo==hi.
  std::string s;
  while (true) {
    s.push_back(lo);
    if (lo == hi) break;
    ++lo;
  }
  return s;
}

TEST(Charmap, Constexpr) {
  constexpr absl::CharSet kEmpty = absl::CharSet();
  EXPECT_EQ(Members(kEmpty), "");
  constexpr absl::CharSet kA = absl::CharSet::Char('A');
  EXPECT_EQ(Members(kA), "A");
  constexpr absl::CharSet kAZ = absl::CharSet::Range('A', 'Z');
  EXPECT_EQ(Members(kAZ), "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  constexpr absl::CharSet kIdentifier =
      absl::CharSet::Range('0', '9') | absl::CharSet::Range('A', 'Z') |
      absl::CharSet::Range('a', 'z') | absl::CharSet::Char('_');
  EXPECT_EQ(Members(kIdentifier),
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "_"
            "abcdefghijklmnopqrstuvwxyz");
  constexpr absl::CharSet kAll = ~absl::CharSet();
  for (size_t i = 0; i < 256; ++i) {
    SCOPED_TRACE(i);
    EXPECT_TRUE(kAll.contains(i));
  }
  constexpr absl::CharSet kHello = absl::CharSet("Hello, world!");
  EXPECT_EQ(Members(kHello), " !,Hdelorw");

  // test negation and intersection
  constexpr absl::CharSet kABC =
      absl::CharSet::Range('A', 'Z') & ~absl::CharSet::Range('D', 'Z');
  EXPECT_EQ(Members(kABC), "ABC");

  // contains
  constexpr bool kContainsA = absl::CharSet("abc").contains('a');
  EXPECT_TRUE(kContainsA);
  constexpr bool kContainsD = absl::CharSet("abc").contains('d');
  EXPECT_FALSE(kContainsD);

  // empty
  constexpr bool kEmptyIsEmpty = absl::CharSet().empty();
  EXPECT_TRUE(kEmptyIsEmpty);
  constexpr bool kNotEmptyIsEmpty = absl::CharSet("abc").empty();
  EXPECT_FALSE(kNotEmptyIsEmpty);
}

TEST(Charmap, Range) {
  // Exhaustive testing takes too long, so test some of the boundaries that
  // are perhaps going to cause trouble.
  std::vector<size_t> poi = {0,   1,   2,   3,   4,   7,   8,   9,  15,
                             16,  17,  30,  31,  32,  33,  63,  64, 65,
                             127, 128, 129, 223, 224, 225, 254, 255};
  for (auto lo = poi.begin(); lo != poi.end(); ++lo) {
    SCOPED_TRACE(*lo);
    for (auto hi = lo; hi != poi.end(); ++hi) {
      SCOPED_TRACE(*hi);
      EXPECT_EQ(Members(absl::CharSet::Range(*lo, *hi)),
                ClosedRangeString(*lo, *hi));
    }
  }
}

TEST(Charmap, NullByteWithStringView) {
  char characters[5] = {'a', 'b', '\0', 'd', 'x'};
  absl::string_view view(characters, 5);
  absl::CharSet tester(view);
  EXPECT_TRUE(tester.contains('a'));
  EXPECT_TRUE(tester.contains('b'));
  EXPECT_TRUE(tester.contains('\0'));
  EXPECT_TRUE(tester.contains('d'));
  EXPECT_TRUE(tester.contains('x'));
  EXPECT_FALSE(tester.contains('c'));
}

TEST(CharmapCtype, Match) {
  for (int c = 0; c < 256; ++c) {
    SCOPED_TRACE(c);
    SCOPED_TRACE(static_cast<char>(c));
    EXPECT_EQ(absl::ascii_isupper(c),
              absl::CharSet::AsciiUppercase().contains(c));
    EXPECT_EQ(absl::ascii_islower(c),
              absl::CharSet::AsciiLowercase().contains(c));
    EXPECT_EQ(absl::ascii_isdigit(c), absl::CharSet::AsciiDigits().contains(c));
    EXPECT_EQ(absl::ascii_isalpha(c),
              absl::CharSet::AsciiAlphabet().contains(c));
    EXPECT_EQ(absl::ascii_isalnum(c),
              absl::CharSet::AsciiAlphanumerics().contains(c));
    EXPECT_EQ(absl::ascii_isxdigit(c),
              absl::CharSet::AsciiHexDigits().contains(c));
    EXPECT_EQ(absl::ascii_isprint(c),
              absl::CharSet::AsciiPrintable().contains(c));
    EXPECT_EQ(absl::ascii_isspace(c),
              absl::CharSet::AsciiWhitespace().contains(c));
    EXPECT_EQ(absl::ascii_ispunct(c),
              absl::CharSet::AsciiPunctuation().contains(c));
  }
}

}  // namespace
