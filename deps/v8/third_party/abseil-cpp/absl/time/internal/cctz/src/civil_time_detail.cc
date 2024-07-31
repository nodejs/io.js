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

#include "absl/time/internal/cctz/include/cctz/civil_time_detail.h"

#include <iomanip>
#include <ostream>
#include <sstream>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {
namespace detail {

// Output stream operators output a format matching YYYY-MM-DDThh:mm:ss,
// while omitting fields inferior to the type's alignment. For example,
// civil_day is formatted only as YYYY-MM-DD.
std::ostream& operator<<(std::ostream& os, const civil_year& y) {
  std::stringstream ss;
  ss << y.year();  // No padding.
  return os << ss.str();
}
std::ostream& operator<<(std::ostream& os, const civil_month& m) {
  std::stringstream ss;
  ss << civil_year(m) << '-';
  ss << std::setfill('0') << std::setw(2) << m.month();
  return os << ss.str();
}
std::ostream& operator<<(std::ostream& os, const civil_day& d) {
  std::stringstream ss;
  ss << civil_month(d) << '-';
  ss << std::setfill('0') << std::setw(2) << d.day();
  return os << ss.str();
}
std::ostream& operator<<(std::ostream& os, const civil_hour& h) {
  std::stringstream ss;
  ss << civil_day(h) << 'T';
  ss << std::setfill('0') << std::setw(2) << h.hour();
  return os << ss.str();
}
std::ostream& operator<<(std::ostream& os, const civil_minute& m) {
  std::stringstream ss;
  ss << civil_hour(m) << ':';
  ss << std::setfill('0') << std::setw(2) << m.minute();
  return os << ss.str();
}
std::ostream& operator<<(std::ostream& os, const civil_second& s) {
  std::stringstream ss;
  ss << civil_minute(s) << ':';
  ss << std::setfill('0') << std::setw(2) << s.second();
  return os << ss.str();
}

////////////////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream& os, weekday wd) {
  switch (wd) {
    case weekday::monday:
      return os << "Monday";
    case weekday::tuesday:
      return os << "Tuesday";
    case weekday::wednesday:
      return os << "Wednesday";
    case weekday::thursday:
      return os << "Thursday";
    case weekday::friday:
      return os << "Friday";
    case weekday::saturday:
      return os << "Saturday";
    case weekday::sunday:
      return os << "Sunday";
  }
  return os;  // Should never get here, but -Wreturn-type may warn without this.
}

}  // namespace detail
}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
