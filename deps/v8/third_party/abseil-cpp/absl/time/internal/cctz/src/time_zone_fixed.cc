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

#include "absl/time/internal/cctz/src/time_zone_fixed.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>
#include <string>

#include "absl/base/config.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

namespace {

// The prefix used for the internal names of fixed-offset zones.
const char kFixedZonePrefix[] = "Fixed/UTC";

const char kDigits[] = "0123456789";

char* Format02d(char* p, int v) {
  *p++ = kDigits[(v / 10) % 10];
  *p++ = kDigits[v % 10];
  return p;
}

int Parse02d(const char* p) {
  if (const char* ap = std::strchr(kDigits, *p)) {
    int v = static_cast<int>(ap - kDigits);
    if (const char* bp = std::strchr(kDigits, *++p)) {
      return (v * 10) + static_cast<int>(bp - kDigits);
    }
  }
  return -1;
}

}  // namespace

bool FixedOffsetFromName(const std::string& name, seconds* offset) {
  if (name == "UTC" || name == "UTC0") {
    *offset = seconds::zero();
    return true;
  }

  const std::size_t prefix_len = sizeof(kFixedZonePrefix) - 1;
  const char* const ep = kFixedZonePrefix + prefix_len;
  if (name.size() != prefix_len + 9)  // <prefix>+99:99:99
    return false;
  if (!std::equal(kFixedZonePrefix, ep, name.begin())) return false;
  const char* np = name.data() + prefix_len;
  if (np[0] != '+' && np[0] != '-') return false;
  if (np[3] != ':' || np[6] != ':')  // see note below about large offsets
    return false;

  int hours = Parse02d(np + 1);
  if (hours == -1) return false;
  int mins = Parse02d(np + 4);
  if (mins == -1) return false;
  int secs = Parse02d(np + 7);
  if (secs == -1) return false;

  secs += ((hours * 60) + mins) * 60;
  if (secs > 24 * 60 * 60) return false;  // outside supported offset range
  *offset = seconds(secs * (np[0] == '-' ? -1 : 1));  // "-" means west
  return true;
}

std::string FixedOffsetToName(const seconds& offset) {
  if (offset == seconds::zero()) return "UTC";
  if (offset < std::chrono::hours(-24) || offset > std::chrono::hours(24)) {
    // We don't support fixed-offset zones more than 24 hours
    // away from UTC to avoid complications in rendering such
    // offsets and to (somewhat) limit the total number of zones.
    return "UTC";
  }
  int offset_seconds = static_cast<int>(offset.count());
  const char sign = (offset_seconds < 0 ? '-' : '+');
  int offset_minutes = offset_seconds / 60;
  offset_seconds %= 60;
  if (sign == '-') {
    if (offset_seconds > 0) {
      offset_seconds -= 60;
      offset_minutes += 1;
    }
    offset_seconds = -offset_seconds;
    offset_minutes = -offset_minutes;
  }
  int offset_hours = offset_minutes / 60;
  offset_minutes %= 60;
  const std::size_t prefix_len = sizeof(kFixedZonePrefix) - 1;
  char buf[prefix_len + sizeof("-24:00:00")];
  char* ep = std::copy_n(kFixedZonePrefix, prefix_len, buf);
  *ep++ = sign;
  ep = Format02d(ep, offset_hours);
  *ep++ = ':';
  ep = Format02d(ep, offset_minutes);
  *ep++ = ':';
  ep = Format02d(ep, offset_seconds);
  *ep++ = '\0';
  assert(ep == buf + sizeof(buf));
  return buf;
}

std::string FixedOffsetToAbbr(const seconds& offset) {
  std::string abbr = FixedOffsetToName(offset);
  const std::size_t prefix_len = sizeof(kFixedZonePrefix) - 1;
  if (abbr.size() == prefix_len + 9) {         // <prefix>+99:99:99
    abbr.erase(0, prefix_len);                 // +99:99:99
    abbr.erase(6, 1);                          // +99:9999
    abbr.erase(3, 1);                          // +999999
    if (abbr[5] == '0' && abbr[6] == '0') {    // +999900
      abbr.erase(5, 2);                        // +9999
      if (abbr[3] == '0' && abbr[4] == '0') {  // +9900
        abbr.erase(3, 2);                      // +99
      }
    }
  }
  return abbr;
}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
