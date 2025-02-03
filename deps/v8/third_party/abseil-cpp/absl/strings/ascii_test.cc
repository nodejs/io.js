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

#include "absl/strings/ascii.h"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstring>
#include <string>

#include "gtest/gtest.h"
#include "absl/base/macros.h"
#include "absl/strings/string_view.h"

namespace {

TEST(AsciiIsFoo, All) {
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
      EXPECT_TRUE(absl::ascii_isalpha(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isalpha(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if ((c >= '0' && c <= '9'))
      EXPECT_TRUE(absl::ascii_isdigit(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isdigit(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (absl::ascii_isalpha(c) || absl::ascii_isdigit(c))
      EXPECT_TRUE(absl::ascii_isalnum(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isalnum(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i != '\0' && strchr(" \r\n\t\v\f", i))
      EXPECT_TRUE(absl::ascii_isspace(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isspace(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i >= 32 && i < 127)
      EXPECT_TRUE(absl::ascii_isprint(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isprint(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (absl::ascii_isprint(c) && !absl::ascii_isspace(c) &&
        !absl::ascii_isalnum(c)) {
      EXPECT_TRUE(absl::ascii_ispunct(c)) << ": failed on " << c;
    } else {
      EXPECT_TRUE(!absl::ascii_ispunct(c)) << ": failed on " << c;
    }
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i == ' ' || i == '\t')
      EXPECT_TRUE(absl::ascii_isblank(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isblank(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i < 32 || i == 127)
      EXPECT_TRUE(absl::ascii_iscntrl(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_iscntrl(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (absl::ascii_isdigit(c) || (i >= 'A' && i <= 'F') ||
        (i >= 'a' && i <= 'f')) {
      EXPECT_TRUE(absl::ascii_isxdigit(c)) << ": failed on " << c;
    } else {
      EXPECT_TRUE(!absl::ascii_isxdigit(c)) << ": failed on " << c;
    }
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i > 32 && i < 127)
      EXPECT_TRUE(absl::ascii_isgraph(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isgraph(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i >= 'A' && i <= 'Z')
      EXPECT_TRUE(absl::ascii_isupper(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_isupper(c)) << ": failed on " << c;
  }
  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (i >= 'a' && i <= 'z')
      EXPECT_TRUE(absl::ascii_islower(c)) << ": failed on " << c;
    else
      EXPECT_TRUE(!absl::ascii_islower(c)) << ": failed on " << c;
  }
  for (unsigned char c = 0; c < 128; c++) {
    EXPECT_TRUE(absl::ascii_isascii(c)) << ": failed on " << c;
  }
  for (int i = 128; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    EXPECT_TRUE(!absl::ascii_isascii(c)) << ": failed on " << c;
  }
}

// Checks that absl::ascii_isfoo returns the same value as isfoo in the C
// locale.
TEST(AsciiIsFoo, SameAsIsFoo) {
#ifndef __ANDROID__
  // temporarily change locale to C. It should already be C, but just for safety
  const char* old_locale = setlocale(LC_CTYPE, "C");
  ASSERT_TRUE(old_locale != nullptr);
#endif

  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    EXPECT_EQ(isalpha(c) != 0, absl::ascii_isalpha(c)) << c;
    EXPECT_EQ(isdigit(c) != 0, absl::ascii_isdigit(c)) << c;
    EXPECT_EQ(isalnum(c) != 0, absl::ascii_isalnum(c)) << c;
    EXPECT_EQ(isspace(c) != 0, absl::ascii_isspace(c)) << c;
    EXPECT_EQ(ispunct(c) != 0, absl::ascii_ispunct(c)) << c;
    EXPECT_EQ(isblank(c) != 0, absl::ascii_isblank(c)) << c;
    EXPECT_EQ(iscntrl(c) != 0, absl::ascii_iscntrl(c)) << c;
    EXPECT_EQ(isxdigit(c) != 0, absl::ascii_isxdigit(c)) << c;
    EXPECT_EQ(isprint(c) != 0, absl::ascii_isprint(c)) << c;
    EXPECT_EQ(isgraph(c) != 0, absl::ascii_isgraph(c)) << c;
    EXPECT_EQ(isupper(c) != 0, absl::ascii_isupper(c)) << c;
    EXPECT_EQ(islower(c) != 0, absl::ascii_islower(c)) << c;
    EXPECT_EQ(isascii(c) != 0, absl::ascii_isascii(c)) << c;
  }

#ifndef __ANDROID__
  // restore the old locale.
  ASSERT_TRUE(setlocale(LC_CTYPE, old_locale));
#endif
}

TEST(AsciiToFoo, All) {
#ifndef __ANDROID__
  // temporarily change locale to C. It should already be C, but just for safety
  const char* old_locale = setlocale(LC_CTYPE, "C");
  ASSERT_TRUE(old_locale != nullptr);
#endif

  for (int i = 0; i < 256; i++) {
    const auto c = static_cast<unsigned char>(i);
    if (absl::ascii_islower(c))
      EXPECT_EQ(absl::ascii_toupper(c), 'A' + (i - 'a')) << c;
    else
      EXPECT_EQ(absl::ascii_toupper(c), static_cast<char>(i)) << c;

    if (absl::ascii_isupper(c))
      EXPECT_EQ(absl::ascii_tolower(c), 'a' + (i - 'A')) << c;
    else
      EXPECT_EQ(absl::ascii_tolower(c), static_cast<char>(i)) << c;

    // These CHECKs only hold in a C locale.
    EXPECT_EQ(static_cast<char>(tolower(i)), absl::ascii_tolower(c)) << c;
    EXPECT_EQ(static_cast<char>(toupper(i)), absl::ascii_toupper(c)) << c;
  }
#ifndef __ANDROID__
  // restore the old locale.
  ASSERT_TRUE(setlocale(LC_CTYPE, old_locale));
#endif
}

TEST(AsciiStrTo, Lower) {
  const char buf[] = "ABCDEF";
  const std::string str("GHIJKL");
  const std::string str2("MNOPQR");
  const absl::string_view sp(str2);
  const std::string long_str("ABCDEFGHIJKLMNOPQRSTUVWXYZ1!a");
  std::string mutable_str("_`?@[{AMNOPQRSTUVWXYZ");
  auto fun = []() -> std::string { return "PQRSTU"; };

  EXPECT_EQ("abcdef", absl::AsciiStrToLower(buf));
  EXPECT_EQ("ghijkl", absl::AsciiStrToLower(str));
  EXPECT_EQ("mnopqr", absl::AsciiStrToLower(sp));
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz1!a", absl::AsciiStrToLower(long_str));
  EXPECT_EQ("pqrstu", absl::AsciiStrToLower(fun()));

  absl::AsciiStrToLower(&mutable_str);
  EXPECT_EQ("_`?@[{amnopqrstuvwxyz", mutable_str);

  char mutable_buf[] = "Mutable";
  std::transform(mutable_buf, mutable_buf + strlen(mutable_buf),
                 mutable_buf, absl::ascii_tolower);
  EXPECT_STREQ("mutable", mutable_buf);
}

TEST(AsciiStrTo, Upper) {
  const char buf[] = "abcdef";
  const std::string str("ghijkl");
  const std::string str2("_`?@[{amnopqrstuvwxyz");
  const absl::string_view sp(str2);
  const std::string long_str("abcdefghijklmnopqrstuvwxyz1!A");
  auto fun = []() -> std::string { return "pqrstu"; };

  EXPECT_EQ("ABCDEF", absl::AsciiStrToUpper(buf));
  EXPECT_EQ("GHIJKL", absl::AsciiStrToUpper(str));
  EXPECT_EQ("_`?@[{AMNOPQRSTUVWXYZ", absl::AsciiStrToUpper(sp));
  EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ1!A", absl::AsciiStrToUpper(long_str));
  EXPECT_EQ("PQRSTU", absl::AsciiStrToUpper(fun()));

  char mutable_buf[] = "Mutable";
  std::transform(mutable_buf, mutable_buf + strlen(mutable_buf),
                 mutable_buf, absl::ascii_toupper);
  EXPECT_STREQ("MUTABLE", mutable_buf);
}

TEST(StripLeadingAsciiWhitespace, FromStringView) {
  EXPECT_EQ(absl::string_view{},
            absl::StripLeadingAsciiWhitespace(absl::string_view{}));
  EXPECT_EQ("foo", absl::StripLeadingAsciiWhitespace({"foo"}));
  EXPECT_EQ("foo", absl::StripLeadingAsciiWhitespace({"\t  \n\f\r\n\vfoo"}));
  EXPECT_EQ("foo foo\n ",
            absl::StripLeadingAsciiWhitespace({"\t  \n\f\r\n\vfoo foo\n "}));
  EXPECT_EQ(absl::string_view{}, absl::StripLeadingAsciiWhitespace(
                                     {"\t  \n\f\r\v\n\t  \n\f\r\v\n"}));
}

TEST(StripLeadingAsciiWhitespace, InPlace) {
  std::string str;

  absl::StripLeadingAsciiWhitespace(&str);
  EXPECT_EQ("", str);

  str = "foo";
  absl::StripLeadingAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = "\t  \n\f\r\n\vfoo";
  absl::StripLeadingAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = "\t  \n\f\r\n\vfoo foo\n ";
  absl::StripLeadingAsciiWhitespace(&str);
  EXPECT_EQ("foo foo\n ", str);

  str = "\t  \n\f\r\v\n\t  \n\f\r\v\n";
  absl::StripLeadingAsciiWhitespace(&str);
  EXPECT_EQ(absl::string_view{}, str);
}

TEST(StripTrailingAsciiWhitespace, FromStringView) {
  EXPECT_EQ(absl::string_view{},
            absl::StripTrailingAsciiWhitespace(absl::string_view{}));
  EXPECT_EQ("foo", absl::StripTrailingAsciiWhitespace({"foo"}));
  EXPECT_EQ("foo", absl::StripTrailingAsciiWhitespace({"foo\t  \n\f\r\n\v"}));
  EXPECT_EQ(" \nfoo foo",
            absl::StripTrailingAsciiWhitespace({" \nfoo foo\t  \n\f\r\n\v"}));
  EXPECT_EQ(absl::string_view{}, absl::StripTrailingAsciiWhitespace(
                                     {"\t  \n\f\r\v\n\t  \n\f\r\v\n"}));
}

TEST(StripTrailingAsciiWhitespace, InPlace) {
  std::string str;

  absl::StripTrailingAsciiWhitespace(&str);
  EXPECT_EQ("", str);

  str = "foo";
  absl::StripTrailingAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = "foo\t  \n\f\r\n\v";
  absl::StripTrailingAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = " \nfoo foo\t  \n\f\r\n\v";
  absl::StripTrailingAsciiWhitespace(&str);
  EXPECT_EQ(" \nfoo foo", str);

  str = "\t  \n\f\r\v\n\t  \n\f\r\v\n";
  absl::StripTrailingAsciiWhitespace(&str);
  EXPECT_EQ(absl::string_view{}, str);
}

TEST(StripAsciiWhitespace, FromStringView) {
  EXPECT_EQ(absl::string_view{},
            absl::StripAsciiWhitespace(absl::string_view{}));
  EXPECT_EQ("foo", absl::StripAsciiWhitespace({"foo"}));
  EXPECT_EQ("foo",
            absl::StripAsciiWhitespace({"\t  \n\f\r\n\vfoo\t  \n\f\r\n\v"}));
  EXPECT_EQ("foo foo", absl::StripAsciiWhitespace(
                           {"\t  \n\f\r\n\vfoo foo\t  \n\f\r\n\v"}));
  EXPECT_EQ(absl::string_view{},
            absl::StripAsciiWhitespace({"\t  \n\f\r\v\n\t  \n\f\r\v\n"}));
}

TEST(StripAsciiWhitespace, InPlace) {
  std::string str;

  absl::StripAsciiWhitespace(&str);
  EXPECT_EQ("", str);

  str = "foo";
  absl::StripAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = "\t  \n\f\r\n\vfoo\t  \n\f\r\n\v";
  absl::StripAsciiWhitespace(&str);
  EXPECT_EQ("foo", str);

  str = "\t  \n\f\r\n\vfoo foo\t  \n\f\r\n\v";
  absl::StripAsciiWhitespace(&str);
  EXPECT_EQ("foo foo", str);

  str = "\t  \n\f\r\v\n\t  \n\f\r\v\n";
  absl::StripAsciiWhitespace(&str);
  EXPECT_EQ(absl::string_view{}, str);
}

TEST(RemoveExtraAsciiWhitespace, InPlace) {
  const char* inputs[] = {"No extra space",
                          "  Leading whitespace",
                          "Trailing whitespace  ",
                          "  Leading and trailing  ",
                          " Whitespace \t  in\v   middle  ",
                          "'Eeeeep!  \n Newlines!\n",
                          "nospaces",
                          "",
                          "\n\t a\t\n\nb \t\n"};

  const char* outputs[] = {
      "No extra space",
      "Leading whitespace",
      "Trailing whitespace",
      "Leading and trailing",
      "Whitespace in middle",
      "'Eeeeep! Newlines!",
      "nospaces",
      "",
      "a\nb",
  };
  const int NUM_TESTS = ABSL_ARRAYSIZE(inputs);

  for (int i = 0; i < NUM_TESTS; i++) {
    std::string s(inputs[i]);
    absl::RemoveExtraAsciiWhitespace(&s);
    EXPECT_EQ(outputs[i], s);
  }
}

}  // namespace
