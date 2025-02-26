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

#if !defined(_CRT_SECURE_NO_WARNINGS) && defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS 1
#endif

#include "absl/time/internal/cctz/src/time_zone_libc.h"

#include <chrono>
#include <ctime>
#include <limits>
#include <utility>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

#if defined(_AIX)
extern "C" {
extern long altzone;
}
#endif

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

namespace {

#if defined(_WIN32) || defined(_WIN64)
// Uses the globals: '_timezone', '_dstbias' and '_tzname'.
auto tm_gmtoff(const std::tm& tm) -> decltype(_timezone + _dstbias) {
  const bool is_dst = tm.tm_isdst > 0;
  return _timezone + (is_dst ? _dstbias : 0);
}
auto tm_zone(const std::tm& tm) -> decltype(_tzname[0]) {
  const bool is_dst = tm.tm_isdst > 0;
  return _tzname[is_dst];
}
#elif defined(__sun) || defined(_AIX)
// Uses the globals: 'timezone', 'altzone' and 'tzname'.
auto tm_gmtoff(const std::tm& tm) -> decltype(timezone) {
  const bool is_dst = tm.tm_isdst > 0;
  return is_dst ? altzone : timezone;
}
auto tm_zone(const std::tm& tm) -> decltype(tzname[0]) {
  const bool is_dst = tm.tm_isdst > 0;
  return tzname[is_dst];
}
#elif defined(__native_client__) || defined(__myriad2__) || \
    defined(__EMSCRIPTEN__)
// Uses the globals: '_timezone' and 'tzname'.
auto tm_gmtoff(const std::tm& tm) -> decltype(_timezone + 0) {
  const bool is_dst = tm.tm_isdst > 0;
  return _timezone + (is_dst ? 60 * 60 : 0);
}
auto tm_zone(const std::tm& tm) -> decltype(tzname[0]) {
  const bool is_dst = tm.tm_isdst > 0;
  return tzname[is_dst];
}
#elif defined(__VXWORKS__)
// Uses the globals: 'timezone' and 'tzname'.
auto tm_gmtoff(const std::tm& tm) -> decltype(timezone + 0) {
  const bool is_dst = tm.tm_isdst > 0;
  return timezone + (is_dst ? 60 * 60 : 0);
}
auto tm_zone(const std::tm& tm) -> decltype(tzname[0]) {
  const bool is_dst = tm.tm_isdst > 0;
  return tzname[is_dst];
}
#else
// Adapt to different spellings of the struct std::tm extension fields.
#if defined(tm_gmtoff)
auto tm_gmtoff(const std::tm& tm) -> decltype(tm.tm_gmtoff) {
  return tm.tm_gmtoff;
}
#elif defined(__tm_gmtoff)
auto tm_gmtoff(const std::tm& tm) -> decltype(tm.__tm_gmtoff) {
  return tm.__tm_gmtoff;
}
#else
template <typename T>
auto tm_gmtoff(const T& tm) -> decltype(tm.tm_gmtoff) {
  return tm.tm_gmtoff;
}
template <typename T>
auto tm_gmtoff(const T& tm) -> decltype(tm.__tm_gmtoff) {
  return tm.__tm_gmtoff;
}
#endif  // tm_gmtoff
#if defined(tm_zone)
auto tm_zone(const std::tm& tm) -> decltype(tm.tm_zone) { return tm.tm_zone; }
#elif defined(__tm_zone)
auto tm_zone(const std::tm& tm) -> decltype(tm.__tm_zone) {
  return tm.__tm_zone;
}
#else
template <typename T>
auto tm_zone(const T& tm) -> decltype(tm.tm_zone) {
  return tm.tm_zone;
}
template <typename T>
auto tm_zone(const T& tm) -> decltype(tm.__tm_zone) {
  return tm.__tm_zone;
}
#endif  // tm_zone
#endif
using tm_gmtoff_t = decltype(tm_gmtoff(std::tm{}));

inline std::tm* gm_time(const std::time_t* timep, std::tm* result) {
#if defined(_WIN32) || defined(_WIN64)
  return gmtime_s(result, timep) ? nullptr : result;
#else
  return gmtime_r(timep, result);
#endif
}

inline std::tm* local_time(const std::time_t* timep, std::tm* result) {
#if defined(_WIN32) || defined(_WIN64)
  return localtime_s(result, timep) ? nullptr : result;
#else
  return localtime_r(timep, result);
#endif
}

// Converts a civil second and "dst" flag into a time_t and a struct tm.
// Returns false if time_t cannot represent the requested civil second.
// Caller must have already checked that cs.year() will fit into a tm_year.
bool make_time(const civil_second& cs, int is_dst, std::time_t* t,
               std::tm* tm) {
  tm->tm_year = static_cast<int>(cs.year() - year_t{1900});
  tm->tm_mon = cs.month() - 1;
  tm->tm_mday = cs.day();
  tm->tm_hour = cs.hour();
  tm->tm_min = cs.minute();
  tm->tm_sec = cs.second();
  tm->tm_isdst = is_dst;
  *t = std::mktime(tm);
  if (*t == std::time_t{-1}) {
    std::tm tm2;
    const std::tm* tmp = local_time(t, &tm2);
    if (tmp == nullptr || tmp->tm_year != tm->tm_year ||
        tmp->tm_mon != tm->tm_mon || tmp->tm_mday != tm->tm_mday ||
        tmp->tm_hour != tm->tm_hour || tmp->tm_min != tm->tm_min ||
        tmp->tm_sec != tm->tm_sec) {
      // A true error (not just one second before the epoch).
      return false;
    }
  }
  return true;
}

// Find the least time_t in [lo:hi] where local time matches offset, given:
// (1) lo doesn't match, (2) hi does, and (3) there is only one transition.
std::time_t find_trans(std::time_t lo, std::time_t hi, tm_gmtoff_t offset) {
  std::tm tm;
  while (lo + 1 != hi) {
    const std::time_t mid = lo + (hi - lo) / 2;
    std::tm* tmp = local_time(&mid, &tm);
    if (tmp != nullptr) {
      if (tm_gmtoff(*tmp) == offset) {
        hi = mid;
      } else {
        lo = mid;
      }
    } else {
      // If std::tm cannot hold some result we resort to a linear search,
      // ignoring all failed conversions.  Slow, but never really happens.
      while (++lo != hi) {
        tmp = local_time(&lo, &tm);
        if (tmp != nullptr) {
          if (tm_gmtoff(*tmp) == offset) break;
        }
      }
      return lo;
    }
  }
  return hi;
}

}  // namespace

std::unique_ptr<TimeZoneLibC> TimeZoneLibC::Make(const std::string& name) {
  return std::unique_ptr<TimeZoneLibC>(new TimeZoneLibC(name));
}

time_zone::absolute_lookup TimeZoneLibC::BreakTime(
    const time_point<seconds>& tp) const {
  time_zone::absolute_lookup al;
  al.offset = 0;
  al.is_dst = false;
  al.abbr = "-00";

  const std::int_fast64_t s = ToUnixSeconds(tp);

  // If std::time_t cannot hold the input we saturate the output.
  if (s < std::numeric_limits<std::time_t>::min()) {
    al.cs = civil_second::min();
    return al;
  }
  if (s > std::numeric_limits<std::time_t>::max()) {
    al.cs = civil_second::max();
    return al;
  }

  const std::time_t t = static_cast<std::time_t>(s);
  std::tm tm;
  std::tm* tmp = local_ ? local_time(&t, &tm) : gm_time(&t, &tm);

  // If std::tm cannot hold the result we saturate the output.
  if (tmp == nullptr) {
    al.cs = (s < 0) ? civil_second::min() : civil_second::max();
    return al;
  }

  const year_t year = tmp->tm_year + year_t{1900};
  al.cs = civil_second(year, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour,
                       tmp->tm_min, tmp->tm_sec);
  al.offset = static_cast<int>(tm_gmtoff(*tmp));
  al.abbr = local_ ? tm_zone(*tmp) : "UTC";  // as expected by cctz
  al.is_dst = tmp->tm_isdst > 0;
  return al;
}

time_zone::civil_lookup TimeZoneLibC::MakeTime(const civil_second& cs) const {
  if (!local_) {
    // If time_point<seconds> cannot hold the result we saturate.
    static const civil_second min_tp_cs =
        civil_second() + ToUnixSeconds(time_point<seconds>::min());
    static const civil_second max_tp_cs =
        civil_second() + ToUnixSeconds(time_point<seconds>::max());
    const time_point<seconds> tp = (cs < min_tp_cs) ? time_point<seconds>::min()
                                   : (cs > max_tp_cs)
                                       ? time_point<seconds>::max()
                                       : FromUnixSeconds(cs - civil_second());
    return {time_zone::civil_lookup::UNIQUE, tp, tp, tp};
  }

  // If tm_year cannot hold the requested year we saturate the result.
  if (cs.year() < 0) {
    if (cs.year() < std::numeric_limits<int>::min() + year_t{1900}) {
      const time_point<seconds> tp = time_point<seconds>::min();
      return {time_zone::civil_lookup::UNIQUE, tp, tp, tp};
    }
  } else {
    if (cs.year() - year_t{1900} > std::numeric_limits<int>::max()) {
      const time_point<seconds> tp = time_point<seconds>::max();
      return {time_zone::civil_lookup::UNIQUE, tp, tp, tp};
    }
  }

  // We probe with "is_dst" values of 0 and 1 to try to distinguish unique
  // civil seconds from skipped or repeated ones.  This is not always possible
  // however, as the "dst" flag does not change over some offset transitions.
  // We are also subject to the vagaries of mktime() implementations. For
  // example, some implementations treat "tm_isdst" as a demand (useless),
  // and some as a disambiguator (useful).
  std::time_t t0, t1;
  std::tm tm0, tm1;
  if (make_time(cs, 0, &t0, &tm0) && make_time(cs, 1, &t1, &tm1)) {
    if (tm0.tm_isdst == tm1.tm_isdst) {
      // The civil time was singular (pre == trans == post).
      const time_point<seconds> tp = FromUnixSeconds(tm0.tm_isdst ? t1 : t0);
      return {time_zone::civil_lookup::UNIQUE, tp, tp, tp};
    }

    tm_gmtoff_t offset = tm_gmtoff(tm0);
    if (t0 < t1) {  // negative DST
      std::swap(t0, t1);
      offset = tm_gmtoff(tm1);
    }

    const std::time_t tt = find_trans(t1, t0, offset);
    const time_point<seconds> trans = FromUnixSeconds(tt);

    if (tm0.tm_isdst) {
      // The civil time did not exist (pre >= trans > post).
      const time_point<seconds> pre = FromUnixSeconds(t0);
      const time_point<seconds> post = FromUnixSeconds(t1);
      return {time_zone::civil_lookup::SKIPPED, pre, trans, post};
    }

    // The civil time was ambiguous (pre < trans <= post).
    const time_point<seconds> pre = FromUnixSeconds(t1);
    const time_point<seconds> post = FromUnixSeconds(t0);
    return {time_zone::civil_lookup::REPEATED, pre, trans, post};
  }

  // make_time() failed somehow so we saturate the result.
  const time_point<seconds> tp = (cs < civil_second())
                                     ? time_point<seconds>::min()
                                     : time_point<seconds>::max();
  return {time_zone::civil_lookup::UNIQUE, tp, tp, tp};
}

bool TimeZoneLibC::NextTransition(const time_point<seconds>&,
                                  time_zone::civil_transition*) const {
  return false;
}

bool TimeZoneLibC::PrevTransition(const time_point<seconds>&,
                                  time_zone::civil_transition*) const {
  return false;
}

std::string TimeZoneLibC::Version() const {
  return std::string();  // unknown
}

std::string TimeZoneLibC::Description() const {
  return local_ ? "localtime" : "UTC";
}

TimeZoneLibC::TimeZoneLibC(const std::string& name)
    : local_(name == "localtime") {}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
