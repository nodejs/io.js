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

#if !defined(HAS_STRPTIME)
#if !defined(_MSC_VER) && !defined(__MINGW32__) && !defined(__VXWORKS__)
#define HAS_STRPTIME 1  // Assume everyone else has strptime().
#endif
#endif

#if defined(HAS_STRPTIME) && HAS_STRPTIME
#if !defined(_XOPEN_SOURCE) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
#define _XOPEN_SOURCE 500  // Exposes definitions for SUSv2 (UNIX 98).
#endif
#endif

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/time_zone.h"

// Include time.h directly since, by C++ standards, ctime doesn't have to
// declare strptime.
#include <time.h>

#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <vector>
#if !HAS_STRPTIME
#include <iomanip>
#include <sstream>
#endif

#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "time_zone_if.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {
namespace detail {

namespace {

#if !HAS_STRPTIME
// Build a strptime() using C++11's std::get_time().
char* strptime(const char* s, const char* fmt, std::tm* tm) {
  std::istringstream input(s);
  input >> std::get_time(tm, fmt);
  if (input.fail()) return nullptr;
  return const_cast<char*>(s) +
         (input.eof() ? strlen(s) : static_cast<std::size_t>(input.tellg()));
}
#endif

// Convert a cctz::weekday to a tm_wday value (0-6, Sunday = 0).
int ToTmWday(weekday wd) {
  switch (wd) {
    case weekday::sunday:
      return 0;
    case weekday::monday:
      return 1;
    case weekday::tuesday:
      return 2;
    case weekday::wednesday:
      return 3;
    case weekday::thursday:
      return 4;
    case weekday::friday:
      return 5;
    case weekday::saturday:
      return 6;
  }
  return 0; /*NOTREACHED*/
}

// Convert a tm_wday value (0-6, Sunday = 0) to a cctz::weekday.
weekday FromTmWday(int tm_wday) {
  switch (tm_wday) {
    case 0:
      return weekday::sunday;
    case 1:
      return weekday::monday;
    case 2:
      return weekday::tuesday;
    case 3:
      return weekday::wednesday;
    case 4:
      return weekday::thursday;
    case 5:
      return weekday::friday;
    case 6:
      return weekday::saturday;
  }
  return weekday::sunday; /*NOTREACHED*/
}

std::tm ToTM(const time_zone::absolute_lookup& al) {
  std::tm tm{};
  tm.tm_sec = al.cs.second();
  tm.tm_min = al.cs.minute();
  tm.tm_hour = al.cs.hour();
  tm.tm_mday = al.cs.day();
  tm.tm_mon = al.cs.month() - 1;

  // Saturate tm.tm_year is cases of over/underflow.
  if (al.cs.year() < std::numeric_limits<int>::min() + 1900) {
    tm.tm_year = std::numeric_limits<int>::min();
  } else if (al.cs.year() - 1900 > std::numeric_limits<int>::max()) {
    tm.tm_year = std::numeric_limits<int>::max();
  } else {
    tm.tm_year = static_cast<int>(al.cs.year() - 1900);
  }

  tm.tm_wday = ToTmWday(get_weekday(al.cs));
  tm.tm_yday = get_yearday(al.cs) - 1;
  tm.tm_isdst = al.is_dst ? 1 : 0;
  return tm;
}

// Returns the week of the year [0:53] given a civil day and the day on
// which weeks are defined to start.
int ToWeek(const civil_day& cd, weekday week_start) {
  const civil_day d(cd.year() % 400, cd.month(), cd.day());
  return static_cast<int>((d - prev_weekday(civil_year(d), week_start)) / 7);
}

const char kDigits[] = "0123456789";

// Formats a 64-bit integer in the given field width.  Note that it is up
// to the caller of Format64() [and Format02d()/FormatOffset()] to ensure
// that there is sufficient space before ep to hold the conversion.
char* Format64(char* ep, int width, std::int_fast64_t v) {
  bool neg = false;
  if (v < 0) {
    --width;
    neg = true;
    if (v == std::numeric_limits<std::int_fast64_t>::min()) {
      // Avoid negating minimum value.
      std::int_fast64_t last_digit = -(v % 10);
      v /= 10;
      if (last_digit < 0) {
        ++v;
        last_digit += 10;
      }
      --width;
      *--ep = kDigits[last_digit];
    }
    v = -v;
  }
  do {
    --width;
    *--ep = kDigits[v % 10];
  } while (v /= 10);
  while (--width >= 0) *--ep = '0';  // zero pad
  if (neg) *--ep = '-';
  return ep;
}

// Formats [0 .. 99] as %02d.
char* Format02d(char* ep, int v) {
  *--ep = kDigits[v % 10];
  *--ep = kDigits[(v / 10) % 10];
  return ep;
}

// Formats a UTC offset, like +00:00.
char* FormatOffset(char* ep, int offset, const char* mode) {
  // TODO: Follow the RFC3339 "Unknown Local Offset Convention" and
  // generate a "negative zero" when we're formatting a zero offset
  // as the result of a failed load_time_zone().
  char sign = '+';
  if (offset < 0) {
    offset = -offset;  // bounded by 24h so no overflow
    sign = '-';
  }
  const int seconds = offset % 60;
  const int minutes = (offset /= 60) % 60;
  const int hours = offset /= 60;
  const char sep = mode[0];
  const bool ext = (sep != '\0' && mode[1] == '*');
  const bool ccc = (ext && mode[2] == ':');
  if (ext && (!ccc || seconds != 0)) {
    ep = Format02d(ep, seconds);
    *--ep = sep;
  } else {
    // If we're not rendering seconds, sub-minute negative offsets
    // should get a positive sign (e.g., offset=-10s => "+00:00").
    if (hours == 0 && minutes == 0) sign = '+';
  }
  if (!ccc || minutes != 0 || seconds != 0) {
    ep = Format02d(ep, minutes);
    if (sep != '\0') *--ep = sep;
  }
  ep = Format02d(ep, hours);
  *--ep = sign;
  return ep;
}

// Formats a std::tm using strftime(3).
void FormatTM(std::string* out, const std::string& fmt, const std::tm& tm) {
  // strftime(3) returns the number of characters placed in the output
  // array (which may be 0 characters).  It also returns 0 to indicate
  // an error, like the array wasn't large enough.  To accommodate this,
  // the following code grows the buffer size from 2x the format string
  // length up to 32x.
  for (std::size_t i = 2; i != 32; i *= 2) {
    std::size_t buf_size = fmt.size() * i;
    std::vector<char> buf(buf_size);
    if (std::size_t len = strftime(&buf[0], buf_size, fmt.c_str(), &tm)) {
      out->append(&buf[0], len);
      return;
    }
  }
}

// Used for %E#S/%E#f specifiers and for data values in parse().
template <typename T>
const char* ParseInt(const char* dp, int width, T min, T max, T* vp) {
  if (dp != nullptr) {
    const T kmin = std::numeric_limits<T>::min();
    bool erange = false;
    bool neg = false;
    T value = 0;
    if (*dp == '-') {
      neg = true;
      if (width <= 0 || --width != 0) {
        ++dp;
      } else {
        dp = nullptr;  // width was 1
      }
    }
    if (const char* const bp = dp) {
      while (const char* cp = strchr(kDigits, *dp)) {
        int d = static_cast<int>(cp - kDigits);
        if (d >= 10) break;
        if (value < kmin / 10) {
          erange = true;
          break;
        }
        value *= 10;
        if (value < kmin + d) {
          erange = true;
          break;
        }
        value -= d;
        dp += 1;
        if (width > 0 && --width == 0) break;
      }
      if (dp != bp && !erange && (neg || value != kmin)) {
        if (!neg || value != 0) {
          if (!neg) value = -value;  // make positive
          if (min <= value && value <= max) {
            *vp = value;
          } else {
            dp = nullptr;
          }
        } else {
          dp = nullptr;
        }
      } else {
        dp = nullptr;
      }
    }
  }
  return dp;
}

// The number of base-10 digits that can be represented by a signed 64-bit
// integer.  That is, 10^kDigits10_64 <= 2^63 - 1 < 10^(kDigits10_64 + 1).
const int kDigits10_64 = 18;

// 10^n for everything that can be represented by a signed 64-bit integer.
const std::int_fast64_t kExp10[kDigits10_64 + 1] = {
    1,
    10,
    100,
    1000,
    10000,
    100000,
    1000000,
    10000000,
    100000000,
    1000000000,
    10000000000,
    100000000000,
    1000000000000,
    10000000000000,
    100000000000000,
    1000000000000000,
    10000000000000000,
    100000000000000000,
    1000000000000000000,
};

}  // namespace

// Uses strftime(3) to format the given Time.  The following extended format
// specifiers are also supported:
//
//   - %Ez  - RFC3339-compatible numeric UTC offset (+hh:mm or -hh:mm)
//   - %E*z - Full-resolution numeric UTC offset (+hh:mm:ss or -hh:mm:ss)
//   - %E#S - Seconds with # digits of fractional precision
//   - %E*S - Seconds with full fractional precision (a literal '*')
//   - %E4Y - Four-character years (-999 ... -001, 0000, 0001 ... 9999)
//   - %ET  - The RFC3339 "date-time" separator "T"
//
// The standard specifiers from RFC3339_* (%Y, %m, %d, %H, %M, and %S) are
// handled internally for performance reasons.  strftime(3) is slow due to
// a POSIX requirement to respect changes to ${TZ}.
//
// The TZ/GNU %s extension is handled internally because strftime() has
// to use mktime() to generate it, and that assumes the local time zone.
//
// We also handle the %z and %Z specifiers to accommodate platforms that do
// not support the tm_gmtoff and tm_zone extensions to std::tm.
//
// Requires that zero() <= fs < seconds(1).
std::string format(const std::string& format, const time_point<seconds>& tp,
                   const detail::femtoseconds& fs, const time_zone& tz) {
  std::string result;
  result.reserve(format.size());  // A reasonable guess for the result size.
  const time_zone::absolute_lookup al = tz.lookup(tp);
  const std::tm tm = ToTM(al);

  // Scratch buffer for internal conversions.
  char buf[3 + kDigits10_64];  // enough for longest conversion
  char* const ep = buf + sizeof(buf);
  char* bp;  // works back from ep

  // Maintain three, disjoint subsequences that span format.
  //   [format.begin() ... pending) : already formatted into result
  //   [pending ... cur) : formatting pending, but no special cases
  //   [cur ... format.end()) : unexamined
  // Initially, everything is in the unexamined part.
  const char* pending = format.c_str();  // NUL terminated
  const char* cur = pending;
  const char* end = pending + format.length();

  while (cur != end) {  // while something is unexamined
    // Moves cur to the next percent sign.
    const char* start = cur;
    while (cur != end && *cur != '%') ++cur;

    // If the new pending text is all ordinary, copy it out.
    if (cur != start && pending == start) {
      result.append(pending, static_cast<std::size_t>(cur - pending));
      pending = start = cur;
    }

    // Span the sequential percent signs.
    const char* percent = cur;
    while (cur != end && *cur == '%') ++cur;

    // If the new pending text is all percents, copy out one
    // percent for every matched pair, then skip those pairs.
    if (cur != start && pending == start) {
      std::size_t escaped = static_cast<std::size_t>(cur - pending) / 2;
      result.append(pending, escaped);
      pending += escaped * 2;
      // Also copy out a single trailing percent.
      if (pending != cur && cur == end) {
        result.push_back(*pending++);
      }
    }

    // Loop unless we have an unescaped percent.
    if (cur == end || (cur - percent) % 2 == 0) continue;

    // Simple specifiers that we handle ourselves.
    if (strchr("YmdeUuWwHMSzZs%", *cur)) {
      if (cur - 1 != pending) {
        FormatTM(&result, std::string(pending, cur - 1), tm);
      }
      switch (*cur) {
        case 'Y':
          // This avoids the tm.tm_year overflow problem for %Y, however
          // tm.tm_year will still be used by other specifiers like %D.
          bp = Format64(ep, 0, al.cs.year());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'm':
          bp = Format02d(ep, al.cs.month());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'd':
        case 'e':
          bp = Format02d(ep, al.cs.day());
          if (*cur == 'e' && *bp == '0') *bp = ' ';  // for Windows
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'U':
          bp = Format02d(ep, ToWeek(civil_day(al.cs), weekday::sunday));
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'u':
          bp = Format64(ep, 0, tm.tm_wday ? tm.tm_wday : 7);
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'W':
          bp = Format02d(ep, ToWeek(civil_day(al.cs), weekday::monday));
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'w':
          bp = Format64(ep, 0, tm.tm_wday);
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'H':
          bp = Format02d(ep, al.cs.hour());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'M':
          bp = Format02d(ep, al.cs.minute());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'S':
          bp = Format02d(ep, al.cs.second());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'z':
          bp = FormatOffset(ep, al.offset, "");
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case 'Z':
          result.append(al.abbr);
          break;
        case 's':
          bp = Format64(ep, 0, ToUnixSeconds(tp));
          result.append(bp, static_cast<std::size_t>(ep - bp));
          break;
        case '%':
          result.push_back('%');
          break;
      }
      pending = ++cur;
      continue;
    }

    // More complex specifiers that we handle ourselves.
    if (*cur == ':' && cur + 1 != end) {
      if (*(cur + 1) == 'z') {
        // Formats %:z.
        if (cur - 1 != pending) {
          FormatTM(&result, std::string(pending, cur - 1), tm);
        }
        bp = FormatOffset(ep, al.offset, ":");
        result.append(bp, static_cast<std::size_t>(ep - bp));
        pending = cur += 2;
        continue;
      }
      if (*(cur + 1) == ':' && cur + 2 != end) {
        if (*(cur + 2) == 'z') {
          // Formats %::z.
          if (cur - 1 != pending) {
            FormatTM(&result, std::string(pending, cur - 1), tm);
          }
          bp = FormatOffset(ep, al.offset, ":*");
          result.append(bp, static_cast<std::size_t>(ep - bp));
          pending = cur += 3;
          continue;
        }
        if (*(cur + 2) == ':' && cur + 3 != end) {
          if (*(cur + 3) == 'z') {
            // Formats %:::z.
            if (cur - 1 != pending) {
              FormatTM(&result, std::string(pending, cur - 1), tm);
            }
            bp = FormatOffset(ep, al.offset, ":*:");
            result.append(bp, static_cast<std::size_t>(ep - bp));
            pending = cur += 4;
            continue;
          }
        }
      }
    }

    // Loop if there is no E modifier.
    if (*cur != 'E' || ++cur == end) continue;

    // Format our extensions.
    if (*cur == 'T') {
      // Formats %ET.
      if (cur - 2 != pending) {
        FormatTM(&result, std::string(pending, cur - 2), tm);
      }
      result.append("T");
      pending = ++cur;
    } else if (*cur == 'z') {
      // Formats %Ez.
      if (cur - 2 != pending) {
        FormatTM(&result, std::string(pending, cur - 2), tm);
      }
      bp = FormatOffset(ep, al.offset, ":");
      result.append(bp, static_cast<std::size_t>(ep - bp));
      pending = ++cur;
    } else if (*cur == '*' && cur + 1 != end && *(cur + 1) == 'z') {
      // Formats %E*z.
      if (cur - 2 != pending) {
        FormatTM(&result, std::string(pending, cur - 2), tm);
      }
      bp = FormatOffset(ep, al.offset, ":*");
      result.append(bp, static_cast<std::size_t>(ep - bp));
      pending = cur += 2;
    } else if (*cur == '*' && cur + 1 != end &&
               (*(cur + 1) == 'S' || *(cur + 1) == 'f')) {
      // Formats %E*S or %E*F.
      if (cur - 2 != pending) {
        FormatTM(&result, std::string(pending, cur - 2), tm);
      }
      char* cp = ep;
      bp = Format64(cp, 15, fs.count());
      while (cp != bp && cp[-1] == '0') --cp;
      switch (*(cur + 1)) {
        case 'S':
          if (cp != bp) *--bp = '.';
          bp = Format02d(bp, al.cs.second());
          break;
        case 'f':
          if (cp == bp) *--bp = '0';
          break;
      }
      result.append(bp, static_cast<std::size_t>(cp - bp));
      pending = cur += 2;
    } else if (*cur == '4' && cur + 1 != end && *(cur + 1) == 'Y') {
      // Formats %E4Y.
      if (cur - 2 != pending) {
        FormatTM(&result, std::string(pending, cur - 2), tm);
      }
      bp = Format64(ep, 4, al.cs.year());
      result.append(bp, static_cast<std::size_t>(ep - bp));
      pending = cur += 2;
    } else if (std::isdigit(*cur)) {
      // Possibly found %E#S or %E#f.
      int n = 0;
      if (const char* np = ParseInt(cur, 0, 0, 1024, &n)) {
        if (*np == 'S' || *np == 'f') {
          // Formats %E#S or %E#f.
          if (cur - 2 != pending) {
            FormatTM(&result, std::string(pending, cur - 2), tm);
          }
          bp = ep;
          if (n > 0) {
            if (n > kDigits10_64) n = kDigits10_64;
            bp = Format64(bp, n,
                          (n > 15) ? fs.count() * kExp10[n - 15]
                                   : fs.count() / kExp10[15 - n]);
            if (*np == 'S') *--bp = '.';
          }
          if (*np == 'S') bp = Format02d(bp, al.cs.second());
          result.append(bp, static_cast<std::size_t>(ep - bp));
          pending = cur = ++np;
        }
      }
    }
  }

  // Formats any remaining data.
  if (end != pending) {
    FormatTM(&result, std::string(pending, end), tm);
  }

  return result;
}

namespace {

const char* ParseOffset(const char* dp, const char* mode, int* offset) {
  if (dp != nullptr) {
    const char first = *dp++;
    if (first == '+' || first == '-') {
      char sep = mode[0];
      int hours = 0;
      int minutes = 0;
      int seconds = 0;
      const char* ap = ParseInt(dp, 2, 0, 23, &hours);
      if (ap != nullptr && ap - dp == 2) {
        dp = ap;
        if (sep != '\0' && *ap == sep) ++ap;
        const char* bp = ParseInt(ap, 2, 0, 59, &minutes);
        if (bp != nullptr && bp - ap == 2) {
          dp = bp;
          if (sep != '\0' && *bp == sep) ++bp;
          const char* cp = ParseInt(bp, 2, 0, 59, &seconds);
          if (cp != nullptr && cp - bp == 2) dp = cp;
        }
        *offset = ((hours * 60 + minutes) * 60) + seconds;
        if (first == '-') *offset = -*offset;
      } else {
        dp = nullptr;
      }
    } else if (first == 'Z' || first == 'z') {  // Zulu
      *offset = 0;
    } else {
      dp = nullptr;
    }
  }
  return dp;
}

const char* ParseZone(const char* dp, std::string* zone) {
  zone->clear();
  if (dp != nullptr) {
    while (*dp != '\0' && !std::isspace(*dp)) zone->push_back(*dp++);
    if (zone->empty()) dp = nullptr;
  }
  return dp;
}

const char* ParseSubSeconds(const char* dp, detail::femtoseconds* subseconds) {
  if (dp != nullptr) {
    std::int_fast64_t v = 0;
    std::int_fast64_t exp = 0;
    const char* const bp = dp;
    while (const char* cp = strchr(kDigits, *dp)) {
      int d = static_cast<int>(cp - kDigits);
      if (d >= 10) break;
      if (exp < 15) {
        exp += 1;
        v *= 10;
        v += d;
      }
      ++dp;
    }
    if (dp != bp) {
      v *= kExp10[15 - exp];
      *subseconds = detail::femtoseconds(v);
    } else {
      dp = nullptr;
    }
  }
  return dp;
}

// Parses a string into a std::tm using strptime(3).
const char* ParseTM(const char* dp, const char* fmt, std::tm* tm) {
  if (dp != nullptr) {
    dp = strptime(dp, fmt, tm);
  }
  return dp;
}

// Sets year, tm_mon and tm_mday given the year, week_num, and tm_wday,
// and the day on which weeks are defined to start.  Returns false if year
// would need to move outside its bounds.
bool FromWeek(int week_num, weekday week_start, year_t* year, std::tm* tm) {
  const civil_year y(*year % 400);
  civil_day cd = prev_weekday(y, week_start);  // week 0
  cd = next_weekday(cd - 1, FromTmWday(tm->tm_wday)) + (week_num * 7);
  if (const year_t shift = cd.year() - y.year()) {
    if (shift > 0) {
      if (*year > std::numeric_limits<year_t>::max() - shift) return false;
    } else {
      if (*year < std::numeric_limits<year_t>::min() - shift) return false;
    }
    *year += shift;
  }
  tm->tm_mon = cd.month() - 1;
  tm->tm_mday = cd.day();
  return true;
}

}  // namespace

// Uses strptime(3) to parse the given input.  Supports the same extended
// format specifiers as format(), although %E#S and %E*S are treated
// identically (and similarly for %E#f and %E*f).  %Ez and %E*z also accept
// the same inputs. %ET accepts either 'T' or 't'.
//
// The standard specifiers from RFC3339_* (%Y, %m, %d, %H, %M, and %S) are
// handled internally so that we can normally avoid strptime() altogether
// (which is particularly helpful when the native implementation is broken).
//
// The TZ/GNU %s extension is handled internally because strptime() has to
// use localtime_r() to generate it, and that assumes the local time zone.
//
// We also handle the %z specifier to accommodate platforms that do not
// support the tm_gmtoff extension to std::tm.  %Z is parsed but ignored.
bool parse(const std::string& format, const std::string& input,
           const time_zone& tz, time_point<seconds>* sec,
           detail::femtoseconds* fs, std::string* err) {
  // The unparsed input.
  const char* data = input.c_str();  // NUL terminated

  // Skips leading whitespace.
  while (std::isspace(*data)) ++data;

  const year_t kyearmax = std::numeric_limits<year_t>::max();
  const year_t kyearmin = std::numeric_limits<year_t>::min();

  // Sets default values for unspecified fields.
  bool saw_year = false;
  year_t year = 1970;
  std::tm tm{};
  tm.tm_year = 1970 - 1900;
  tm.tm_mon = 1 - 1;  // Jan
  tm.tm_mday = 1;
  tm.tm_hour = 0;
  tm.tm_min = 0;
  tm.tm_sec = 0;
  tm.tm_wday = 4;  // Thu
  tm.tm_yday = 0;
  tm.tm_isdst = 0;
  auto subseconds = detail::femtoseconds::zero();
  bool saw_offset = false;
  int offset = 0;  // No offset from passed tz.
  std::string zone = "UTC";

  const char* fmt = format.c_str();  // NUL terminated
  bool twelve_hour = false;
  bool afternoon = false;
  int week_num = -1;
  weekday week_start = weekday::sunday;

  bool saw_percent_s = false;
  std::int_fast64_t percent_s = 0;

  // Steps through format, one specifier at a time.
  while (data != nullptr && *fmt != '\0') {
    if (std::isspace(*fmt)) {
      while (std::isspace(*data)) ++data;
      while (std::isspace(*++fmt)) continue;
      continue;
    }

    if (*fmt != '%') {
      if (*data == *fmt) {
        ++data;
        ++fmt;
      } else {
        data = nullptr;
      }
      continue;
    }

    const char* percent = fmt;
    if (*++fmt == '\0') {
      data = nullptr;
      continue;
    }
    switch (*fmt++) {
      case 'Y':
        // Symmetrically with FormatTime(), directly handing %Y avoids the
        // tm.tm_year overflow problem.  However, tm.tm_year will still be
        // used by other specifiers like %D.
        data = ParseInt(data, 0, kyearmin, kyearmax, &year);
        if (data != nullptr) saw_year = true;
        continue;
      case 'm':
        data = ParseInt(data, 2, 1, 12, &tm.tm_mon);
        if (data != nullptr) tm.tm_mon -= 1;
        week_num = -1;
        continue;
      case 'd':
      case 'e':
        data = ParseInt(data, 2, 1, 31, &tm.tm_mday);
        week_num = -1;
        continue;
      case 'U':
        data = ParseInt(data, 0, 0, 53, &week_num);
        week_start = weekday::sunday;
        continue;
      case 'W':
        data = ParseInt(data, 0, 0, 53, &week_num);
        week_start = weekday::monday;
        continue;
      case 'u':
        data = ParseInt(data, 0, 1, 7, &tm.tm_wday);
        if (data != nullptr) tm.tm_wday %= 7;
        continue;
      case 'w':
        data = ParseInt(data, 0, 0, 6, &tm.tm_wday);
        continue;
      case 'H':
        data = ParseInt(data, 2, 0, 23, &tm.tm_hour);
        twelve_hour = false;
        continue;
      case 'M':
        data = ParseInt(data, 2, 0, 59, &tm.tm_min);
        continue;
      case 'S':
        data = ParseInt(data, 2, 0, 60, &tm.tm_sec);
        continue;
      case 'I':
      case 'l':
      case 'r':  // probably uses %I
        twelve_hour = true;
        break;
      case 'R':  // uses %H
      case 'T':  // uses %H
      case 'c':  // probably uses %H
      case 'X':  // probably uses %H
        twelve_hour = false;
        break;
      case 'z':
        data = ParseOffset(data, "", &offset);
        if (data != nullptr) saw_offset = true;
        continue;
      case 'Z':  // ignored; zone abbreviations are ambiguous
        data = ParseZone(data, &zone);
        continue;
      case 's':
        data =
            ParseInt(data, 0, std::numeric_limits<std::int_fast64_t>::min(),
                     std::numeric_limits<std::int_fast64_t>::max(), &percent_s);
        if (data != nullptr) saw_percent_s = true;
        continue;
      case ':':
        if (fmt[0] == 'z' ||
            (fmt[0] == ':' &&
             (fmt[1] == 'z' || (fmt[1] == ':' && fmt[2] == 'z')))) {
          data = ParseOffset(data, ":", &offset);
          if (data != nullptr) saw_offset = true;
          fmt += (fmt[0] == 'z') ? 1 : (fmt[1] == 'z') ? 2 : 3;
          continue;
        }
        break;
      case '%':
        data = (*data == '%' ? data + 1 : nullptr);
        continue;
      case 'E':
        if (fmt[0] == 'T') {
          if (*data == 'T' || *data == 't') {
            ++data;
            ++fmt;
          } else {
            data = nullptr;
          }
          continue;
        }
        if (fmt[0] == 'z' || (fmt[0] == '*' && fmt[1] == 'z')) {
          data = ParseOffset(data, ":", &offset);
          if (data != nullptr) saw_offset = true;
          fmt += (fmt[0] == 'z') ? 1 : 2;
          continue;
        }
        if (fmt[0] == '*' && fmt[1] == 'S') {
          data = ParseInt(data, 2, 0, 60, &tm.tm_sec);
          if (data != nullptr && *data == '.') {
            data = ParseSubSeconds(data + 1, &subseconds);
          }
          fmt += 2;
          continue;
        }
        if (fmt[0] == '*' && fmt[1] == 'f') {
          if (data != nullptr && std::isdigit(*data)) {
            data = ParseSubSeconds(data, &subseconds);
          }
          fmt += 2;
          continue;
        }
        if (fmt[0] == '4' && fmt[1] == 'Y') {
          const char* bp = data;
          data = ParseInt(data, 4, year_t{-999}, year_t{9999}, &year);
          if (data != nullptr) {
            if (data - bp == 4) {
              saw_year = true;
            } else {
              data = nullptr;  // stopped too soon
            }
          }
          fmt += 2;
          continue;
        }
        if (std::isdigit(*fmt)) {
          int n = 0;  // value ignored
          if (const char* np = ParseInt(fmt, 0, 0, 1024, &n)) {
            if (*np == 'S') {
              data = ParseInt(data, 2, 0, 60, &tm.tm_sec);
              if (data != nullptr && *data == '.') {
                data = ParseSubSeconds(data + 1, &subseconds);
              }
              fmt = ++np;
              continue;
            }
            if (*np == 'f') {
              if (data != nullptr && std::isdigit(*data)) {
                data = ParseSubSeconds(data, &subseconds);
              }
              fmt = ++np;
              continue;
            }
          }
        }
        if (*fmt == 'c') twelve_hour = false;  // probably uses %H
        if (*fmt == 'X') twelve_hour = false;  // probably uses %H
        if (*fmt != '\0') ++fmt;
        break;
      case 'O':
        if (*fmt == 'H') twelve_hour = false;
        if (*fmt == 'I') twelve_hour = true;
        if (*fmt != '\0') ++fmt;
        break;
    }

    // Parses the current specifier.
    const char* orig_data = data;
    std::string spec(percent, static_cast<std::size_t>(fmt - percent));
    data = ParseTM(data, spec.c_str(), &tm);

    // If we successfully parsed %p we need to remember whether the result
    // was AM or PM so that we can adjust tm_hour before time_zone::lookup().
    // So reparse the input with a known AM hour, and check if it is shifted
    // to a PM hour.
    if (spec == "%p" && data != nullptr) {
      std::string test_input = "1";
      test_input.append(orig_data, static_cast<std::size_t>(data - orig_data));
      const char* test_data = test_input.c_str();
      std::tm tmp{};
      ParseTM(test_data, "%I%p", &tmp);
      afternoon = (tmp.tm_hour == 13);
    }
  }

  // Adjust a 12-hour tm_hour value if it should be in the afternoon.
  if (twelve_hour && afternoon && tm.tm_hour < 12) {
    tm.tm_hour += 12;
  }

  if (data == nullptr) {
    if (err != nullptr) *err = "Failed to parse input";
    return false;
  }

  // Skip any remaining whitespace.
  while (std::isspace(*data)) ++data;

  // parse() must consume the entire input string.
  if (*data != '\0') {
    if (err != nullptr) *err = "Illegal trailing data in input string";
    return false;
  }

  // If we saw %s then we ignore anything else and return that time.
  if (saw_percent_s) {
    *sec = FromUnixSeconds(percent_s);
    *fs = detail::femtoseconds::zero();
    return true;
  }

  // If we saw %z, %Ez, or %E*z then we want to interpret the parsed fields
  // in UTC and then shift by that offset.  Otherwise we want to interpret
  // the fields directly in the passed time_zone.
  time_zone ptz = saw_offset ? utc_time_zone() : tz;

  // Allows a leap second of 60 to normalize forward to the following ":00".
  if (tm.tm_sec == 60) {
    tm.tm_sec -= 1;
    offset -= 1;
    subseconds = detail::femtoseconds::zero();
  }

  if (!saw_year) {
    year = year_t{tm.tm_year};
    if (year > kyearmax - 1900) {
      // Platform-dependent, maybe unreachable.
      if (err != nullptr) *err = "Out-of-range year";
      return false;
    }
    year += 1900;
  }

  // Compute year, tm.tm_mon and tm.tm_mday if we parsed a week number.
  if (week_num != -1) {
    if (!FromWeek(week_num, week_start, &year, &tm)) {
      if (err != nullptr) *err = "Out-of-range field";
      return false;
    }
  }

  const int month = tm.tm_mon + 1;
  civil_second cs(year, month, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

  // parse() should not allow normalization. Due to the restricted field
  // ranges above (see ParseInt()), the only possibility is for days to roll
  // into months. That is, parsing "Sep 31" should not produce "Oct 1".
  if (cs.month() != month || cs.day() != tm.tm_mday) {
    if (err != nullptr) *err = "Out-of-range field";
    return false;
  }

  // Accounts for the offset adjustment before converting to absolute time.
  if ((offset < 0 && cs > civil_second::max() + offset) ||
      (offset > 0 && cs < civil_second::min() + offset)) {
    if (err != nullptr) *err = "Out-of-range field";
    return false;
  }
  cs -= offset;

  const auto tp = ptz.lookup(cs).pre;
  // Checks for overflow/underflow and returns an error as necessary.
  if (tp == time_point<seconds>::max()) {
    const auto al = ptz.lookup(time_point<seconds>::max());
    if (cs > al.cs) {
      if (err != nullptr) *err = "Out-of-range field";
      return false;
    }
  }
  if (tp == time_point<seconds>::min()) {
    const auto al = ptz.lookup(time_point<seconds>::min());
    if (cs < al.cs) {
      if (err != nullptr) *err = "Out-of-range field";
      return false;
    }
  }

  *sec = tp;
  *fs = subseconds;
  return true;
}

}  // namespace detail
}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
