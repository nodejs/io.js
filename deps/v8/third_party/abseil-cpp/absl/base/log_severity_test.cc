// Copyright 2018 The Abseil Authors.
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

#include "absl/base/log_severity.h"

#include <cstdint>
#include <ios>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <tuple>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/flags/internal/flag.h"
#include "absl/flags/marshalling.h"
#include "absl/strings/str_cat.h"

namespace {
using ::testing::Eq;
using ::testing::IsFalse;
using ::testing::IsTrue;
using ::testing::TestWithParam;
using ::testing::Values;

template <typename T>
std::string StreamHelper(T value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

TEST(StreamTest, Works) {
  EXPECT_THAT(StreamHelper(static_cast<absl::LogSeverity>(-100)),
              Eq("absl::LogSeverity(-100)"));
  EXPECT_THAT(StreamHelper(absl::LogSeverity::kInfo), Eq("INFO"));
  EXPECT_THAT(StreamHelper(absl::LogSeverity::kWarning), Eq("WARNING"));
  EXPECT_THAT(StreamHelper(absl::LogSeverity::kError), Eq("ERROR"));
  EXPECT_THAT(StreamHelper(absl::LogSeverity::kFatal), Eq("FATAL"));
  EXPECT_THAT(StreamHelper(static_cast<absl::LogSeverity>(4)),
              Eq("absl::LogSeverity(4)"));
}

static_assert(absl::flags_internal::FlagUseValueAndInitBitStorage<
                  absl::LogSeverity>::value,
              "Flags of type absl::LogSeverity ought to be lock-free.");

using ParseFlagFromOutOfRangeIntegerTest = TestWithParam<int64_t>;
INSTANTIATE_TEST_SUITE_P(
    Instantiation, ParseFlagFromOutOfRangeIntegerTest,
    Values(static_cast<int64_t>(std::numeric_limits<int>::min()) - 1,
           static_cast<int64_t>(std::numeric_limits<int>::max()) + 1));
TEST_P(ParseFlagFromOutOfRangeIntegerTest, ReturnsError) {
  const std::string to_parse = absl::StrCat(GetParam());
  absl::LogSeverity value;
  std::string error;
  EXPECT_THAT(absl::ParseFlag(to_parse, &value, &error), IsFalse()) << value;
}

using ParseFlagFromAlmostOutOfRangeIntegerTest = TestWithParam<int>;
INSTANTIATE_TEST_SUITE_P(Instantiation,
                         ParseFlagFromAlmostOutOfRangeIntegerTest,
                         Values(std::numeric_limits<int>::min(),
                                std::numeric_limits<int>::max()));
TEST_P(ParseFlagFromAlmostOutOfRangeIntegerTest, YieldsExpectedValue) {
  const auto expected = static_cast<absl::LogSeverity>(GetParam());
  const std::string to_parse = absl::StrCat(GetParam());
  absl::LogSeverity value;
  std::string error;
  ASSERT_THAT(absl::ParseFlag(to_parse, &value, &error), IsTrue()) << error;
  EXPECT_THAT(value, Eq(expected));
}

using ParseFlagFromIntegerMatchingEnumeratorTest =
    TestWithParam<std::tuple<absl::string_view, absl::LogSeverity>>;
INSTANTIATE_TEST_SUITE_P(
    Instantiation, ParseFlagFromIntegerMatchingEnumeratorTest,
    Values(std::make_tuple("0", absl::LogSeverity::kInfo),
           std::make_tuple(" 0", absl::LogSeverity::kInfo),
           std::make_tuple("-0", absl::LogSeverity::kInfo),
           std::make_tuple("+0", absl::LogSeverity::kInfo),
           std::make_tuple("00", absl::LogSeverity::kInfo),
           std::make_tuple("0 ", absl::LogSeverity::kInfo),
           std::make_tuple("0x0", absl::LogSeverity::kInfo),
           std::make_tuple("1", absl::LogSeverity::kWarning),
           std::make_tuple("+1", absl::LogSeverity::kWarning),
           std::make_tuple("2", absl::LogSeverity::kError),
           std::make_tuple("3", absl::LogSeverity::kFatal)));
TEST_P(ParseFlagFromIntegerMatchingEnumeratorTest, YieldsExpectedValue) {
  const absl::string_view to_parse = std::get<0>(GetParam());
  const absl::LogSeverity expected = std::get<1>(GetParam());
  absl::LogSeverity value;
  std::string error;
  ASSERT_THAT(absl::ParseFlag(to_parse, &value, &error), IsTrue()) << error;
  EXPECT_THAT(value, Eq(expected));
}

using ParseFlagFromOtherIntegerTest =
    TestWithParam<std::tuple<absl::string_view, int>>;
INSTANTIATE_TEST_SUITE_P(Instantiation, ParseFlagFromOtherIntegerTest,
                         Values(std::make_tuple("-1", -1),
                                std::make_tuple("4", 4),
                                std::make_tuple("010", 10),
                                std::make_tuple("0x10", 16)));
TEST_P(ParseFlagFromOtherIntegerTest, YieldsExpectedValue) {
  const absl::string_view to_parse = std::get<0>(GetParam());
  const auto expected = static_cast<absl::LogSeverity>(std::get<1>(GetParam()));
  absl::LogSeverity value;
  std::string error;
  ASSERT_THAT(absl::ParseFlag(to_parse, &value, &error), IsTrue()) << error;
  EXPECT_THAT(value, Eq(expected));
}

using ParseFlagFromEnumeratorTest =
    TestWithParam<std::tuple<absl::string_view, absl::LogSeverity>>;
INSTANTIATE_TEST_SUITE_P(
    Instantiation, ParseFlagFromEnumeratorTest,
    Values(std::make_tuple("INFO", absl::LogSeverity::kInfo),
           std::make_tuple("info", absl::LogSeverity::kInfo),
           std::make_tuple("kInfo", absl::LogSeverity::kInfo),
           std::make_tuple("iNfO", absl::LogSeverity::kInfo),
           std::make_tuple("kInFo", absl::LogSeverity::kInfo),
           std::make_tuple("WARNING", absl::LogSeverity::kWarning),
           std::make_tuple("warning", absl::LogSeverity::kWarning),
           std::make_tuple("kWarning", absl::LogSeverity::kWarning),
           std::make_tuple("WaRnInG", absl::LogSeverity::kWarning),
           std::make_tuple("KwArNiNg", absl::LogSeverity::kWarning),
           std::make_tuple("ERROR", absl::LogSeverity::kError),
           std::make_tuple("error", absl::LogSeverity::kError),
           std::make_tuple("kError", absl::LogSeverity::kError),
           std::make_tuple("eRrOr", absl::LogSeverity::kError),
           std::make_tuple("kErRoR", absl::LogSeverity::kError),
           std::make_tuple("FATAL", absl::LogSeverity::kFatal),
           std::make_tuple("fatal", absl::LogSeverity::kFatal),
           std::make_tuple("kFatal", absl::LogSeverity::kFatal),
           std::make_tuple("FaTaL", absl::LogSeverity::kFatal),
           std::make_tuple("KfAtAl", absl::LogSeverity::kFatal),
           std::make_tuple("DFATAL", absl::kLogDebugFatal),
           std::make_tuple("dfatal", absl::kLogDebugFatal),
           std::make_tuple("kLogDebugFatal", absl::kLogDebugFatal),
           std::make_tuple("dFaTaL", absl::kLogDebugFatal),
           std::make_tuple("kLoGdEbUgFaTaL", absl::kLogDebugFatal)));
TEST_P(ParseFlagFromEnumeratorTest, YieldsExpectedValue) {
  const absl::string_view to_parse = std::get<0>(GetParam());
  const absl::LogSeverity expected = std::get<1>(GetParam());
  absl::LogSeverity value;
  std::string error;
  ASSERT_THAT(absl::ParseFlag(to_parse, &value, &error), IsTrue()) << error;
  EXPECT_THAT(value, Eq(expected));
}

using ParseFlagFromGarbageTest = TestWithParam<absl::string_view>;
INSTANTIATE_TEST_SUITE_P(Instantiation, ParseFlagFromGarbageTest,
                         Values("", "\0", " ", "garbage", "kkinfo", "I",
                                "kDFATAL", "LogDebugFatal", "lOgDeBuGfAtAl"));
TEST_P(ParseFlagFromGarbageTest, ReturnsError) {
  const absl::string_view to_parse = GetParam();
  absl::LogSeverity value;
  std::string error;
  EXPECT_THAT(absl::ParseFlag(to_parse, &value, &error), IsFalse()) << value;
}

using UnparseFlagToEnumeratorTest =
    TestWithParam<std::tuple<absl::LogSeverity, absl::string_view>>;
INSTANTIATE_TEST_SUITE_P(
    Instantiation, UnparseFlagToEnumeratorTest,
    Values(std::make_tuple(absl::LogSeverity::kInfo, "INFO"),
           std::make_tuple(absl::LogSeverity::kWarning, "WARNING"),
           std::make_tuple(absl::LogSeverity::kError, "ERROR"),
           std::make_tuple(absl::LogSeverity::kFatal, "FATAL")));
TEST_P(UnparseFlagToEnumeratorTest, ReturnsExpectedValueAndRoundTrips) {
  const absl::LogSeverity to_unparse = std::get<0>(GetParam());
  const absl::string_view expected = std::get<1>(GetParam());
  const std::string stringified_value = absl::UnparseFlag(to_unparse);
  EXPECT_THAT(stringified_value, Eq(expected));
  absl::LogSeverity reparsed_value;
  std::string error;
  EXPECT_THAT(absl::ParseFlag(stringified_value, &reparsed_value, &error),
              IsTrue());
  EXPECT_THAT(reparsed_value, Eq(to_unparse));
}

using UnparseFlagToOtherIntegerTest = TestWithParam<int>;
INSTANTIATE_TEST_SUITE_P(Instantiation, UnparseFlagToOtherIntegerTest,
                         Values(std::numeric_limits<int>::min(), -1, 4,
                                std::numeric_limits<int>::max()));
TEST_P(UnparseFlagToOtherIntegerTest, ReturnsExpectedValueAndRoundTrips) {
  const absl::LogSeverity to_unparse =
      static_cast<absl::LogSeverity>(GetParam());
  const std::string expected = absl::StrCat(GetParam());
  const std::string stringified_value = absl::UnparseFlag(to_unparse);
  EXPECT_THAT(stringified_value, Eq(expected));
  absl::LogSeverity reparsed_value;
  std::string error;
  EXPECT_THAT(absl::ParseFlag(stringified_value, &reparsed_value, &error),
              IsTrue());
  EXPECT_THAT(reparsed_value, Eq(to_unparse));
}

TEST(LogThresholdTest, LogSeverityAtLeastTest) {
  EXPECT_LT(absl::LogSeverity::kError, absl::LogSeverityAtLeast::kFatal);
  EXPECT_GT(absl::LogSeverityAtLeast::kError, absl::LogSeverity::kInfo);

  EXPECT_LE(absl::LogSeverityAtLeast::kInfo, absl::LogSeverity::kError);
  EXPECT_GE(absl::LogSeverity::kError, absl::LogSeverityAtLeast::kInfo);
}

TEST(LogThresholdTest, LogSeverityAtMostTest) {
  EXPECT_GT(absl::LogSeverity::kError, absl::LogSeverityAtMost::kWarning);
  EXPECT_LT(absl::LogSeverityAtMost::kError, absl::LogSeverity::kFatal);

  EXPECT_GE(absl::LogSeverityAtMost::kFatal, absl::LogSeverity::kError);
  EXPECT_LE(absl::LogSeverity::kWarning, absl::LogSeverityAtMost::kError);
}

TEST(LogThresholdTest, Extremes) {
  EXPECT_LT(absl::LogSeverity::kFatal, absl::LogSeverityAtLeast::kInfinity);
  EXPECT_GT(absl::LogSeverity::kInfo,
            absl::LogSeverityAtMost::kNegativeInfinity);
}

TEST(LogThresholdTest, Output) {
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtLeast::kInfo), Eq(">=INFO"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtLeast::kWarning),
              Eq(">=WARNING"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtLeast::kError), Eq(">=ERROR"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtLeast::kFatal), Eq(">=FATAL"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtLeast::kInfinity),
              Eq("INFINITY"));

  EXPECT_THAT(StreamHelper(absl::LogSeverityAtMost::kInfo), Eq("<=INFO"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtMost::kWarning), Eq("<=WARNING"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtMost::kError), Eq("<=ERROR"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtMost::kFatal), Eq("<=FATAL"));
  EXPECT_THAT(StreamHelper(absl::LogSeverityAtMost::kNegativeInfinity),
              Eq("NEGATIVE_INFINITY"));
}

}  // namespace
