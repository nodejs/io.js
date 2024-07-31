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

#include "absl/time/clock.h"

#include "absl/base/config.h"
#if defined(ABSL_HAVE_ALARM)
#include <signal.h>
#include <unistd.h>
#ifdef _AIX
// sig_t is not defined in AIX.
typedef void (*sig_t)(int);
#endif
#elif defined(__linux__) || defined(__APPLE__)
#error all known Linux and Apple targets have alarm
#endif

#include "gtest/gtest.h"
#include "absl/time/time.h"

namespace {

TEST(Time, Now) {
  const absl::Time before = absl::FromUnixNanos(absl::GetCurrentTimeNanos());
  const absl::Time now = absl::Now();
  const absl::Time after = absl::FromUnixNanos(absl::GetCurrentTimeNanos());
  EXPECT_GE(now, before);
  EXPECT_GE(after, now);
}

enum class AlarmPolicy { kWithoutAlarm, kWithAlarm };

#if defined(ABSL_HAVE_ALARM)
bool alarm_handler_invoked = false;

void AlarmHandler(int signo) {
  ASSERT_EQ(signo, SIGALRM);
  alarm_handler_invoked = true;
}
#endif

// Does SleepFor(d) take between lower_bound and upper_bound at least
// once between now and (now + timeout)?  If requested (and supported),
// add an alarm for the middle of the sleep period and expect it to fire.
bool SleepForBounded(absl::Duration d, absl::Duration lower_bound,
                     absl::Duration upper_bound, absl::Duration timeout,
                     AlarmPolicy alarm_policy, int* attempts) {
  const absl::Time deadline = absl::Now() + timeout;
  while (absl::Now() < deadline) {
#if defined(ABSL_HAVE_ALARM)
    sig_t old_alarm = SIG_DFL;
    if (alarm_policy == AlarmPolicy::kWithAlarm) {
      alarm_handler_invoked = false;
      old_alarm = signal(SIGALRM, AlarmHandler);
      alarm(absl::ToInt64Seconds(d / 2));
    }
#else
    EXPECT_EQ(alarm_policy, AlarmPolicy::kWithoutAlarm);
#endif
    ++*attempts;
    absl::Time start = absl::Now();
    absl::SleepFor(d);
    absl::Duration actual = absl::Now() - start;
#if defined(ABSL_HAVE_ALARM)
    if (alarm_policy == AlarmPolicy::kWithAlarm) {
      signal(SIGALRM, old_alarm);
      if (!alarm_handler_invoked) continue;
    }
#endif
    if (lower_bound <= actual && actual <= upper_bound) {
      return true;  // yes, the SleepFor() was correctly bounded
    }
  }
  return false;
}

testing::AssertionResult AssertSleepForBounded(absl::Duration d,
                                               absl::Duration early,
                                               absl::Duration late,
                                               absl::Duration timeout,
                                               AlarmPolicy alarm_policy) {
  const absl::Duration lower_bound = d - early;
  const absl::Duration upper_bound = d + late;
  int attempts = 0;
  if (SleepForBounded(d, lower_bound, upper_bound, timeout, alarm_policy,
                      &attempts)) {
    return testing::AssertionSuccess();
  }
  return testing::AssertionFailure()
         << "SleepFor(" << d << ") did not return within [" << lower_bound
         << ":" << upper_bound << "] in " << attempts << " attempt"
         << (attempts == 1 ? "" : "s") << " over " << timeout
         << (alarm_policy == AlarmPolicy::kWithAlarm ? " with" : " without")
         << " an alarm";
}

// Tests that SleepFor() returns neither too early nor too late.
TEST(SleepFor, Bounded) {
  const absl::Duration d = absl::Milliseconds(2500);
  const absl::Duration early = absl::Milliseconds(100);
  const absl::Duration late = absl::Milliseconds(300);
  const absl::Duration timeout = 48 * d;
  EXPECT_TRUE(AssertSleepForBounded(d, early, late, timeout,
                                    AlarmPolicy::kWithoutAlarm));
#if defined(ABSL_HAVE_ALARM)
  EXPECT_TRUE(AssertSleepForBounded(d, early, late, timeout,
                                    AlarmPolicy::kWithAlarm));
#endif
}

}  // namespace
