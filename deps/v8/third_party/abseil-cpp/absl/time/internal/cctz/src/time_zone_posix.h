// Copyright 2016 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//   https://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

// Parsing of a POSIX zone spec as described in the TZ part of section 8.3 in
// http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html.
//
// The current POSIX spec for America/Los_Angeles is "PST8PDT,M3.2.0,M11.1.0",
// which would be broken down as ...
//
//   PosixTimeZone {
//     std_abbr = "PST"
//     std_offset = -28800
//     dst_abbr = "PDT"
//     dst_offset = -25200
//     dst_start = PosixTransition {
//       date {
//         m {
//           month = 3
//           week = 2
//           weekday = 0
//         }
//       }
//       time {
//         offset = 7200
//       }
//     }
//     dst_end = PosixTransition {
//       date {
//         m {
//           month = 11
//           week = 1
//           weekday = 0
//         }
//       }
//       time {
//         offset = 7200
//       }
//     }
//   }

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_POSIX_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_POSIX_H_

#include <cstdint>
#include <string>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// The date/time of the transition. The date is specified as either:
// (J) the Nth day of the year (1 <= N <= 365), excluding leap days, or
// (N) the Nth day of the year (0 <= N <= 365), including leap days, or
// (M) the Nth weekday of a month (e.g., the 2nd Sunday in March).
// The time, specified as a day offset, identifies the particular moment
// of the transition, and may be negative or >= 24h, and in which case
// it would take us to another day, and perhaps week, or even month.
struct PosixTransition {
  enum DateFormat { J, N, M };

  struct Date {
    struct NonLeapDay {
      std::int_fast16_t day;  // day of non-leap year [1:365]
    };
    struct Day {
      std::int_fast16_t day;  // day of year [0:365]
    };
    struct MonthWeekWeekday {
      std::int_fast8_t month;    // month of year [1:12]
      std::int_fast8_t week;     // week of month [1:5] (5==last)
      std::int_fast8_t weekday;  // 0==Sun, ..., 6=Sat
    };

    DateFormat fmt;

    union {
      NonLeapDay j;
      Day n;
      MonthWeekWeekday m;
    };
  };

  struct Time {
    std::int_fast32_t offset;  // seconds before/after 00:00:00
  };

  Date date;
  Time time;
};

// The entirety of a POSIX-string specified time-zone rule. The standard
// abbreviation and offset are always given. If the time zone includes
// daylight saving, then the daylight abbreviation is non-empty and the
// remaining fields are also valid. Note that the start/end transitions
// are not ordered---in the southern hemisphere the transition to end
// daylight time occurs first in any particular year.
struct PosixTimeZone {
  std::string std_abbr;
  std::int_fast32_t std_offset;

  std::string dst_abbr;
  std::int_fast32_t dst_offset;
  PosixTransition dst_start;
  PosixTransition dst_end;
};

// Breaks down a POSIX time-zone specification into its constituent pieces,
// filling in any missing values (DST offset, or start/end transition times)
// with the standard-defined defaults. Returns false if the specification
// could not be parsed (although some fields of *res may have been altered).
bool ParsePosixSpec(const std::string& spec, PosixTimeZone* res);

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_POSIX_H_
