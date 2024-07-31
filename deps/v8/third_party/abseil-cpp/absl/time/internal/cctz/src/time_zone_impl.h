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

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IMPL_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IMPL_H_

#include <memory>
#include <string>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"
#include "time_zone_if.h"
#include "time_zone_info.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// time_zone::Impl is the internal object referenced by a cctz::time_zone.
class time_zone::Impl {
 public:
  // The UTC time zone. Also used for other time zones that fail to load.
  static time_zone UTC();

  // Load a named time zone. Returns false if the name is invalid, or if
  // some other kind of error occurs. Note that loading "UTC" never fails.
  static bool LoadTimeZone(const std::string& name, time_zone* tz);

  // Clears the map of cached time zones.  Primarily for use in benchmarks
  // that gauge the performance of loading/parsing the time-zone data.
  static void ClearTimeZoneMapTestOnly();

  // The primary key is the time-zone ID (e.g., "America/New_York").
  const std::string& Name() const {
    // TODO: It would nice if the zoneinfo data included the zone name.
    return name_;
  }

  // Breaks a time_point down to civil-time components in this time zone.
  time_zone::absolute_lookup BreakTime(const time_point<seconds>& tp) const {
    return zone_->BreakTime(tp);
  }

  // Converts the civil-time components in this time zone into a time_point.
  // That is, the opposite of BreakTime(). The requested civil time may be
  // ambiguous or illegal due to a change of UTC offset.
  time_zone::civil_lookup MakeTime(const civil_second& cs) const {
    return zone_->MakeTime(cs);
  }

  // Finds the time of the next/previous offset change in this time zone.
  bool NextTransition(const time_point<seconds>& tp,
                      time_zone::civil_transition* trans) const {
    return zone_->NextTransition(tp, trans);
  }
  bool PrevTransition(const time_point<seconds>& tp,
                      time_zone::civil_transition* trans) const {
    return zone_->PrevTransition(tp, trans);
  }

  // Returns an implementation-defined version string for this time zone.
  std::string Version() const { return zone_->Version(); }

  // Returns an implementation-defined description of this time zone.
  std::string Description() const { return zone_->Description(); }

 private:
  Impl();
  explicit Impl(const std::string& name);
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  static const Impl* UTCImpl();

  const std::string name_;
  std::unique_ptr<TimeZoneIf> zone_;
};

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IMPL_H_
