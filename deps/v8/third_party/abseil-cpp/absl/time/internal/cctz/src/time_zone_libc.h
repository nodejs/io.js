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

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_LIBC_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_LIBC_H_

#include <memory>
#include <string>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/src/time_zone_if.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// A time zone backed by gmtime_r(3), localtime_r(3), and mktime(3),
// and which therefore only supports UTC and the local time zone.
class TimeZoneLibC : public TimeZoneIf {
 public:
  // Factory.
  static std::unique_ptr<TimeZoneLibC> Make(const std::string& name);

  // TimeZoneIf implementations.
  time_zone::absolute_lookup BreakTime(
      const time_point<seconds>& tp) const override;
  time_zone::civil_lookup MakeTime(const civil_second& cs) const override;
  bool NextTransition(const time_point<seconds>& tp,
                      time_zone::civil_transition* trans) const override;
  bool PrevTransition(const time_point<seconds>& tp,
                      time_zone::civil_transition* trans) const override;
  std::string Version() const override;
  std::string Description() const override;

 private:
  explicit TimeZoneLibC(const std::string& name);
  TimeZoneLibC(const TimeZoneLibC&) = delete;
  TimeZoneLibC& operator=(const TimeZoneLibC&) = delete;

  const bool local_;  // localtime or UTC
};

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_LIBC_H_
