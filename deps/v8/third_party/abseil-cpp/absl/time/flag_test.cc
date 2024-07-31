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

#include "absl/flags/flag.h"

#include <string>

#include "gtest/gtest.h"
#include "absl/flags/reflection.h"
#include "absl/time/civil_time.h"
#include "absl/time/time.h"

ABSL_FLAG(absl::CivilSecond, test_flag_civil_second,
          absl::CivilSecond(2015, 1, 2, 3, 4, 5), "");
ABSL_FLAG(absl::CivilMinute, test_flag_civil_minute,
          absl::CivilMinute(2015, 1, 2, 3, 4), "");
ABSL_FLAG(absl::CivilHour, test_flag_civil_hour, absl::CivilHour(2015, 1, 2, 3),
          "");
ABSL_FLAG(absl::CivilDay, test_flag_civil_day, absl::CivilDay(2015, 1, 2), "");
ABSL_FLAG(absl::CivilMonth, test_flag_civil_month, absl::CivilMonth(2015, 1),
          "");
ABSL_FLAG(absl::CivilYear, test_flag_civil_year, absl::CivilYear(2015), "");

ABSL_FLAG(absl::Duration, test_duration_flag, absl::Seconds(5),
          "For testing support for Duration flags");
ABSL_FLAG(absl::Time, test_time_flag, absl::InfinitePast(),
          "For testing support for Time flags");

namespace {

bool SetFlagValue(absl::string_view flag_name, absl::string_view value) {
  auto* flag = absl::FindCommandLineFlag(flag_name);
  if (!flag) return false;
  std::string err;
  return flag->ParseFrom(value, &err);
}

bool GetFlagValue(absl::string_view flag_name, std::string& value) {
  auto* flag = absl::FindCommandLineFlag(flag_name);
  if (!flag) return false;
  value = flag->CurrentValue();
  return true;
}

TEST(CivilTime, FlagSupport) {
  // Tests the default setting of the flags.
  const absl::CivilSecond kDefaultSec(2015, 1, 2, 3, 4, 5);
  EXPECT_EQ(absl::CivilSecond(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_second));
  EXPECT_EQ(absl::CivilMinute(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_minute));
  EXPECT_EQ(absl::CivilHour(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_hour));
  EXPECT_EQ(absl::CivilDay(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_day));
  EXPECT_EQ(absl::CivilMonth(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_month));
  EXPECT_EQ(absl::CivilYear(kDefaultSec),
            absl::GetFlag(FLAGS_test_flag_civil_year));

  // Sets flags to a new value.
  const absl::CivilSecond kNewSec(2016, 6, 7, 8, 9, 10);
  absl::SetFlag(&FLAGS_test_flag_civil_second, absl::CivilSecond(kNewSec));
  absl::SetFlag(&FLAGS_test_flag_civil_minute, absl::CivilMinute(kNewSec));
  absl::SetFlag(&FLAGS_test_flag_civil_hour, absl::CivilHour(kNewSec));
  absl::SetFlag(&FLAGS_test_flag_civil_day, absl::CivilDay(kNewSec));
  absl::SetFlag(&FLAGS_test_flag_civil_month, absl::CivilMonth(kNewSec));
  absl::SetFlag(&FLAGS_test_flag_civil_year, absl::CivilYear(kNewSec));

  EXPECT_EQ(absl::CivilSecond(kNewSec),
            absl::GetFlag(FLAGS_test_flag_civil_second));
  EXPECT_EQ(absl::CivilMinute(kNewSec),
            absl::GetFlag(FLAGS_test_flag_civil_minute));
  EXPECT_EQ(absl::CivilHour(kNewSec),
            absl::GetFlag(FLAGS_test_flag_civil_hour));
  EXPECT_EQ(absl::CivilDay(kNewSec), absl::GetFlag(FLAGS_test_flag_civil_day));
  EXPECT_EQ(absl::CivilMonth(kNewSec),
            absl::GetFlag(FLAGS_test_flag_civil_month));
  EXPECT_EQ(absl::CivilYear(kNewSec),
            absl::GetFlag(FLAGS_test_flag_civil_year));
}

TEST(Duration, FlagSupport) {
  EXPECT_EQ(absl::Seconds(5), absl::GetFlag(FLAGS_test_duration_flag));

  absl::SetFlag(&FLAGS_test_duration_flag, absl::Seconds(10));
  EXPECT_EQ(absl::Seconds(10), absl::GetFlag(FLAGS_test_duration_flag));

  EXPECT_TRUE(SetFlagValue("test_duration_flag", "20s"));
  EXPECT_EQ(absl::Seconds(20), absl::GetFlag(FLAGS_test_duration_flag));

  std::string current_flag_value;
  EXPECT_TRUE(GetFlagValue("test_duration_flag", current_flag_value));
  EXPECT_EQ("20s", current_flag_value);
}

TEST(Time, FlagSupport) {
  EXPECT_EQ(absl::InfinitePast(), absl::GetFlag(FLAGS_test_time_flag));

  const absl::Time t = absl::FromCivil(absl::CivilSecond(2016, 1, 2, 3, 4, 5),
                                       absl::UTCTimeZone());
  absl::SetFlag(&FLAGS_test_time_flag, t);
  EXPECT_EQ(t, absl::GetFlag(FLAGS_test_time_flag));

  // Successful parse
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:06Z"));
  EXPECT_EQ(t + absl::Seconds(1), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:07.0Z"));
  EXPECT_EQ(t + absl::Seconds(2), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:08.000Z"));
  EXPECT_EQ(t + absl::Seconds(3), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:09+00:00"));
  EXPECT_EQ(t + absl::Seconds(4), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:05.123+00:00"));
  EXPECT_EQ(t + absl::Milliseconds(123), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:05.123+08:00"));
  EXPECT_EQ(t + absl::Milliseconds(123) - absl::Hours(8),
            absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "infinite-future"));
  EXPECT_EQ(absl::InfiniteFuture(), absl::GetFlag(FLAGS_test_time_flag));
  EXPECT_TRUE(SetFlagValue("test_time_flag", "infinite-past"));
  EXPECT_EQ(absl::InfinitePast(), absl::GetFlag(FLAGS_test_time_flag));

  EXPECT_FALSE(SetFlagValue("test_time_flag", "2016-01-02T03:04:06"));
  EXPECT_FALSE(SetFlagValue("test_time_flag", "2016-01-02"));
  EXPECT_FALSE(SetFlagValue("test_time_flag", "2016-01-02Z"));
  EXPECT_FALSE(SetFlagValue("test_time_flag", "2016-01-02+00:00"));
  EXPECT_FALSE(SetFlagValue("test_time_flag", "2016-99-99T03:04:06Z"));

  EXPECT_TRUE(SetFlagValue("test_time_flag", "2016-01-02T03:04:05Z"));
  std::string current_flag_value;
  EXPECT_TRUE(GetFlagValue("test_time_flag", current_flag_value));
  EXPECT_EQ("2016-01-02T03:04:05+00:00", current_flag_value);
}

}  // namespace
