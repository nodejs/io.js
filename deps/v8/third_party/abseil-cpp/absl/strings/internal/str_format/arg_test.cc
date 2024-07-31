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

#include "absl/strings/internal/str_format/arg.h"

#include <limits>
#include <string>
#include "gtest/gtest.h"
#include "absl/base/config.h"
#include "absl/strings/str_format.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {
namespace {

class FormatArgImplTest : public ::testing::Test {
 public:
  enum Color { kRed, kGreen, kBlue };

  static const char *hi() { return "hi"; }

  struct X {};

  X x_;
};

inline FormatConvertResult<FormatConversionCharSet{}> AbslFormatConvert(
    const FormatArgImplTest::X &, const FormatConversionSpec &, FormatSink *) {
  return {false};
}

TEST_F(FormatArgImplTest, ToInt) {
  int out = 0;
  EXPECT_TRUE(FormatArgImplFriend::ToInt(FormatArgImpl(1), &out));
  EXPECT_EQ(1, out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(FormatArgImpl(-1), &out));
  EXPECT_EQ(-1, out);
  EXPECT_TRUE(
      FormatArgImplFriend::ToInt(FormatArgImpl(static_cast<char>(64)), &out));
  EXPECT_EQ(64, out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(
      FormatArgImpl(static_cast<unsigned long long>(123456)), &out));  // NOLINT
  EXPECT_EQ(123456, out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(
      FormatArgImpl(static_cast<unsigned long long>(  // NOLINT
                        std::numeric_limits<int>::max()) +
                    1),
      &out));
  EXPECT_EQ(std::numeric_limits<int>::max(), out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(
      FormatArgImpl(static_cast<long long>(  // NOLINT
                        std::numeric_limits<int>::min()) -
                    10),
      &out));
  EXPECT_EQ(std::numeric_limits<int>::min(), out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(FormatArgImpl(false), &out));
  EXPECT_EQ(0, out);
  EXPECT_TRUE(FormatArgImplFriend::ToInt(FormatArgImpl(true), &out));
  EXPECT_EQ(1, out);
  EXPECT_FALSE(FormatArgImplFriend::ToInt(FormatArgImpl(2.2), &out));
  EXPECT_FALSE(FormatArgImplFriend::ToInt(FormatArgImpl(3.2f), &out));
  EXPECT_FALSE(FormatArgImplFriend::ToInt(
      FormatArgImpl(static_cast<int *>(nullptr)), &out));
  EXPECT_FALSE(FormatArgImplFriend::ToInt(FormatArgImpl(hi()), &out));
  EXPECT_FALSE(FormatArgImplFriend::ToInt(FormatArgImpl("hi"), &out));
  EXPECT_FALSE(FormatArgImplFriend::ToInt(FormatArgImpl(x_), &out));
  EXPECT_TRUE(FormatArgImplFriend::ToInt(FormatArgImpl(kBlue), &out));
  EXPECT_EQ(2, out);
}

extern const char kMyArray[];

TEST_F(FormatArgImplTest, CharArraysDecayToCharPtr) {
  const char* a = "";
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl("")));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl("A")));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl("ABC")));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(kMyArray)));
}

extern const wchar_t kMyWCharTArray[];

TEST_F(FormatArgImplTest, WCharTArraysDecayToWCharTPtr) {
  const wchar_t* a = L"";
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(L"")));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(L"A")));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
            FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(L"ABC")));
  EXPECT_EQ(
      FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(a)),
      FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(kMyWCharTArray)));
}

TEST_F(FormatArgImplTest, OtherPtrDecayToVoidPtr) {
  auto expected = FormatArgImplFriend::GetVTablePtrForTest(
      FormatArgImpl(static_cast<void *>(nullptr)));
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(
                FormatArgImpl(static_cast<int *>(nullptr))),
            expected);
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(
                FormatArgImpl(static_cast<volatile int *>(nullptr))),
            expected);

  auto p = static_cast<void (*)()>([] {});
  EXPECT_EQ(FormatArgImplFriend::GetVTablePtrForTest(FormatArgImpl(p)),
            expected);
}

TEST_F(FormatArgImplTest, WorksWithCharArraysOfUnknownSize) {
  std::string s;
  FormatSinkImpl sink(&s);
  FormatConversionSpecImpl conv;
  FormatConversionSpecImplFriend::SetConversionChar(
      FormatConversionCharInternal::s, &conv);
  FormatConversionSpecImplFriend::SetFlags(Flags(), &conv);
  FormatConversionSpecImplFriend::SetWidth(-1, &conv);
  FormatConversionSpecImplFriend::SetPrecision(-1, &conv);
  EXPECT_TRUE(
      FormatArgImplFriend::Convert(FormatArgImpl(kMyArray), conv, &sink));
  sink.Flush();
  EXPECT_EQ("ABCDE", s);
}
const char kMyArray[] = "ABCDE";

TEST_F(FormatArgImplTest, WorksWithWCharTArraysOfUnknownSize) {
  std::string s;
  FormatSinkImpl sink(&s);
  FormatConversionSpecImpl conv;
  FormatConversionSpecImplFriend::SetConversionChar(
      FormatConversionCharInternal::s, &conv);
  FormatConversionSpecImplFriend::SetFlags(Flags(), &conv);
  FormatConversionSpecImplFriend::SetWidth(-1, &conv);
  FormatConversionSpecImplFriend::SetPrecision(-1, &conv);
  EXPECT_TRUE(
      FormatArgImplFriend::Convert(FormatArgImpl(kMyWCharTArray), conv, &sink));
  sink.Flush();
  EXPECT_EQ("ABCDE", s);
}
const wchar_t kMyWCharTArray[] = L"ABCDE";

}  // namespace
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
