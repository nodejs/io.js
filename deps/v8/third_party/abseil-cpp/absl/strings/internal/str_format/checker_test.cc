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

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_format.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace str_format_internal {
namespace {

std::string ConvToString(FormatConversionCharSet conv) {
  std::string out;
#define CONV_SET_CASE(c)                                    \
  if (Contains(conv, FormatConversionCharSetInternal::c)) { \
    out += #c;                                              \
  }
  ABSL_INTERNAL_CONVERSION_CHARS_EXPAND_(CONV_SET_CASE, )
#undef CONV_SET_CASE
  if (Contains(conv, FormatConversionCharSetInternal::kStar)) {
    out += "*";
  }
  return out;
}

TEST(StrFormatChecker, ArgumentToConv) {
  FormatConversionCharSet conv = ArgumentToConv<std::string>();
  EXPECT_EQ(ConvToString(conv), "sv");

  conv = ArgumentToConv<const char*>();
  EXPECT_EQ(ConvToString(conv), "sp");

  conv = ArgumentToConv<double>();
  EXPECT_EQ(ConvToString(conv), "fFeEgGaAv");

  conv = ArgumentToConv<int>();
  EXPECT_EQ(ConvToString(conv), "cdiouxXfFeEgGaAv*");

  conv = ArgumentToConv<std::string*>();
  EXPECT_EQ(ConvToString(conv), "p");
}

#ifdef ABSL_INTERNAL_ENABLE_FORMAT_CHECKER

struct Case {
  bool result;
  const char* format;
};

template <typename... Args>
constexpr Case ValidFormat(const char* format) {
  return {ValidFormatImpl<ArgumentToConv<Args>()...>(format), format};
}

TEST(StrFormatChecker, ValidFormat) {
  // We want to make sure these expressions are constexpr and they have the
  // expected value.
  // If they are not constexpr the attribute will just ignore them and not give
  // a compile time error.
  enum e {};
  enum class e2 {};
  constexpr Case trues[] = {
      ValidFormat<>("abc"),  //

      ValidFormat<e>("%d"),                             //
      ValidFormat<e2>("%d"),                            //
      ValidFormat<int>("%% %d"),                        //
      ValidFormat<int>("%ld"),                          //
      ValidFormat<int>("%lld"),                         //
      ValidFormat<std::string>("%s"),                   //
      ValidFormat<std::string>("%10s"),                 //
      ValidFormat<int>("%.10x"),                        //
      ValidFormat<int, int>("%*.3x"),                   //
      ValidFormat<int>("%1.d"),                         //
      ValidFormat<int>("%.d"),                          //
      ValidFormat<int, double>("%d %g"),                //
      ValidFormat<int, std::string>("%*s"),             //
      ValidFormat<int, double>("%.*f"),                 //
      ValidFormat<void (*)(), volatile int*>("%p %p"),  //
      ValidFormat<string_view, const char*, double, void*>(
          "string_view=%s const char*=%s double=%f void*=%p)"),
      ValidFormat<int>("%v"),  //

      ValidFormat<int>("%% %1$d"),               //
      ValidFormat<int>("%1$ld"),                 //
      ValidFormat<int>("%1$lld"),                //
      ValidFormat<std::string>("%1$s"),          //
      ValidFormat<std::string>("%1$10s"),        //
      ValidFormat<int>("%1$.10x"),               //
      ValidFormat<int>("%1$*1$.*1$d"),           //
      ValidFormat<int, int>("%1$*2$.3x"),        //
      ValidFormat<int>("%1$1.d"),                //
      ValidFormat<int>("%1$.d"),                 //
      ValidFormat<double, int>("%2$d %1$g"),     //
      ValidFormat<int, std::string>("%2$*1$s"),  //
      ValidFormat<int, double>("%2$.*1$f"),      //
      ValidFormat<void*, string_view, const char*, double>(
          "string_view=%2$s const char*=%3$s double=%4$f void*=%1$p "
          "repeat=%3$s)"),
      ValidFormat<std::string>("%1$v"),
  };

  for (Case c : trues) {
    EXPECT_TRUE(c.result) << c.format;
  }

  constexpr Case falses[] = {
      ValidFormat<int>(""),  //

      ValidFormat<e>("%s"),                  //
      ValidFormat<e2>("%s"),                 //
      ValidFormat<>("%s"),                   //
      ValidFormat<>("%r"),                   //
      ValidFormat<int>("%s"),                //
      ValidFormat<int>("%.1.d"),             //
      ValidFormat<int>("%*1d"),              //
      ValidFormat<int>("%1-d"),              //
      ValidFormat<std::string, int>("%*s"),  //
      ValidFormat<int>("%*d"),               //
      ValidFormat<std::string>("%p"),        //
      ValidFormat<int (*)(int)>("%d"),       //
      ValidFormat<int>("%1v"),               //
      ValidFormat<int>("%.1v"),              //

      ValidFormat<>("%3$d"),                     //
      ValidFormat<>("%1$r"),                     //
      ValidFormat<int>("%1$s"),                  //
      ValidFormat<int>("%1$.1.d"),               //
      ValidFormat<int>("%1$*2$1d"),              //
      ValidFormat<int>("%1$1-d"),                //
      ValidFormat<std::string, int>("%2$*1$s"),  //
      ValidFormat<std::string>("%1$p"),          //
      ValidFormat<int>("%1$*2$v"),               //

      ValidFormat<int, int>("%d %2$d"),  //
  };

  for (Case c : falses) {
    EXPECT_FALSE(c.result) << "format<" << c.format << ">";
  }
}

TEST(StrFormatChecker, LongFormat) {
#define CHARS_X_40 "1234567890123456789012345678901234567890"
#define CHARS_X_400                                                            \
  CHARS_X_40 CHARS_X_40 CHARS_X_40 CHARS_X_40 CHARS_X_40 CHARS_X_40 CHARS_X_40 \
      CHARS_X_40 CHARS_X_40 CHARS_X_40
#define CHARS_X_4000                                                      \
  CHARS_X_400 CHARS_X_400 CHARS_X_400 CHARS_X_400 CHARS_X_400 CHARS_X_400 \
      CHARS_X_400 CHARS_X_400 CHARS_X_400 CHARS_X_400
  constexpr char long_format[] =
      CHARS_X_4000 "%d" CHARS_X_4000 "%s" CHARS_X_4000;
  constexpr bool is_valid = ValidFormat<int, std::string>(long_format).result;
  EXPECT_TRUE(is_valid);
}

#endif  // ABSL_INTERNAL_ENABLE_FORMAT_CHECKER

}  // namespace
}  // namespace str_format_internal
ABSL_NAMESPACE_END
}  // namespace absl
