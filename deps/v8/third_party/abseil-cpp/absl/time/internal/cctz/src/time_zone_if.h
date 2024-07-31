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

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IF_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IF_H_

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// A simple interface used to hide time-zone complexities from time_zone::Impl.
// Subclasses implement the functions for civil-time conversions in the zone.
class TimeZoneIf {
 public:
  // Factory functions for TimeZoneIf implementations.
  static std::unique_ptr<TimeZoneIf> UTC();  // never fails
  static std::unique_ptr<TimeZoneIf> Make(const std::string& name);

  virtual ~TimeZoneIf();

  virtual time_zone::absolute_lookup BreakTime(
      const time_point<seconds>& tp) const = 0;
  virtual time_zone::civil_lookup MakeTime(const civil_second& cs) const = 0;

  virtual bool NextTransition(const time_point<seconds>& tp,
                              time_zone::civil_transition* trans) const = 0;
  virtual bool PrevTransition(const time_point<seconds>& tp,
                              time_zone::civil_transition* trans) const = 0;

  virtual std::string Version() const = 0;
  virtual std::string Description() const = 0;

 protected:
  TimeZoneIf() = default;
  TimeZoneIf(const TimeZoneIf&) = delete;
  TimeZoneIf& operator=(const TimeZoneIf&) = delete;
};

// Convert between time_point<seconds> and a count of seconds since the
// Unix epoch.  We assume that the std::chrono::system_clock and the
// Unix clock are second aligned, and that the results are representable.
// (That is, that they share an epoch, which is required since C++20.)
inline std::int_fast64_t ToUnixSeconds(const time_point<seconds>& tp) {
  return (tp - std::chrono::time_point_cast<seconds>(
                   std::chrono::system_clock::from_time_t(0)))
      .count();
}
inline time_point<seconds> FromUnixSeconds(std::int_fast64_t t) {
  return std::chrono::time_point_cast<seconds>(
             std::chrono::system_clock::from_time_t(0)) +
         seconds(t);
}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_IF_H_
