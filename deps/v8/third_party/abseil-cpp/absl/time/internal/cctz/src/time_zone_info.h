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

#ifndef ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_INFO_H_
#define ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_INFO_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"
#include "absl/time/internal/cctz/include/cctz/zone_info_source.h"
#include "time_zone_if.h"
#include "tzfile.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// A transition to a new UTC offset.
struct Transition {
  std::int_least64_t unix_time;   // the instant of this transition
  std::uint_least8_t type_index;  // index of the transition type
  civil_second civil_sec;         // local civil time of transition
  civil_second prev_civil_sec;    // local civil time one second earlier

  struct ByUnixTime {
    inline bool operator()(const Transition& lhs, const Transition& rhs) const {
      return lhs.unix_time < rhs.unix_time;
    }
  };
  struct ByCivilTime {
    inline bool operator()(const Transition& lhs, const Transition& rhs) const {
      return lhs.civil_sec < rhs.civil_sec;
    }
  };
};

// The characteristics of a particular transition.
struct TransitionType {
  std::int_least32_t utc_offset;  // the new prevailing UTC offset
  civil_second civil_max;         // max convertible civil time for offset
  civil_second civil_min;         // min convertible civil time for offset
  bool is_dst;                    // did we move into daylight-saving time
  std::uint_least8_t abbr_index;  // index of the new abbreviation
};

// A time zone backed by the IANA Time Zone Database (zoneinfo).
class TimeZoneInfo : public TimeZoneIf {
 public:
  // Factories.
  static std::unique_ptr<TimeZoneInfo> UTC();  // never fails
  static std::unique_ptr<TimeZoneInfo> Make(const std::string& name);

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
  TimeZoneInfo() = default;
  TimeZoneInfo(const TimeZoneInfo&) = delete;
  TimeZoneInfo& operator=(const TimeZoneInfo&) = delete;

  bool GetTransitionType(std::int_fast32_t utc_offset, bool is_dst,
                         const std::string& abbr, std::uint_least8_t* index);
  bool EquivTransitions(std::uint_fast8_t tt1_index,
                        std::uint_fast8_t tt2_index) const;
  bool ExtendTransitions();

  bool ResetToBuiltinUTC(const seconds& offset);
  bool Load(const std::string& name);
  bool Load(ZoneInfoSource* zip);

  // Helpers for BreakTime() and MakeTime().
  time_zone::absolute_lookup LocalTime(std::int_fast64_t unix_time,
                                       const TransitionType& tt) const;
  time_zone::absolute_lookup LocalTime(std::int_fast64_t unix_time,
                                       const Transition& tr) const;
  time_zone::civil_lookup TimeLocal(const civil_second& cs,
                                    year_t c4_shift) const;

  std::vector<Transition> transitions_;  // ordered by unix_time and civil_sec
  std::vector<TransitionType> transition_types_;  // distinct transition types
  std::uint_fast8_t default_transition_type_;     // for before first transition
  std::string abbreviations_;  // all the NUL-terminated abbreviations

  std::string version_;      // the tzdata version if available
  std::string future_spec_;  // for after the last zic transition
  bool extended_;            // future_spec_ was used to generate transitions
  year_t last_year_;         // the final year of the generated transitions

  // We remember the transitions found during the last BreakTime() and
  // MakeTime() calls. If the next request is for the same transition we
  // will avoid re-searching.
  mutable std::atomic<std::size_t> local_time_hint_ = {};  // BreakTime() hint
  mutable std::atomic<std::size_t> time_local_hint_ = {};  // MakeTime() hint
};

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_TIME_ZONE_INFO_H_
