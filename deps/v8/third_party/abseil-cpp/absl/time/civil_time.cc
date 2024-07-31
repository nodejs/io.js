// Copyright 2018 The Abseil Authors.
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

#include "absl/time/civil_time.h"

#include <cstdlib>
#include <ostream>
#include <string>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"

namespace absl {
ABSL_NAMESPACE_BEGIN

namespace {

// Since a civil time has a larger year range than absl::Time (64-bit years vs
// 64-bit seconds, respectively) we normalize years to roughly +/- 400 years
// around the year 2400, which will produce an equivalent year in a range that
// absl::Time can handle.
inline civil_year_t NormalizeYear(civil_year_t year) {
  return 2400 + year % 400;
}

// Formats the given CivilSecond according to the given format.
std::string FormatYearAnd(string_view fmt, CivilSecond cs) {
  const CivilSecond ncs(NormalizeYear(cs.year()), cs.month(), cs.day(),
                        cs.hour(), cs.minute(), cs.second());
  const TimeZone utc = UTCTimeZone();
  return StrCat(cs.year(), FormatTime(fmt, FromCivil(ncs, utc), utc));
}

template <typename CivilT>
bool ParseYearAnd(string_view fmt, string_view s, CivilT* c) {
  // Civil times support a larger year range than absl::Time, so we need to
  // parse the year separately, normalize it, then use absl::ParseTime on the
  // normalized string.
  const std::string ss = std::string(s);  // TODO(absl-team): Avoid conversion.
  const char* const np = ss.c_str();
  char* endp;
  errno = 0;
  const civil_year_t y =
      std::strtoll(np, &endp, 10);  // NOLINT(runtime/deprecated_fn)
  if (endp == np || errno == ERANGE) return false;
  const std::string norm = StrCat(NormalizeYear(y), endp);

  const TimeZone utc = UTCTimeZone();
  Time t;
  if (ParseTime(StrCat("%Y", fmt), norm, utc, &t, nullptr)) {
    const auto cs = ToCivilSecond(t, utc);
    *c = CivilT(y, cs.month(), cs.day(), cs.hour(), cs.minute(), cs.second());
    return true;
  }

  return false;
}

// Tries to parse the type as a CivilT1, but then assigns the result to the
// argument of type CivilT2.
template <typename CivilT1, typename CivilT2>
bool ParseAs(string_view s, CivilT2* c) {
  CivilT1 t1;
  if (ParseCivilTime(s, &t1)) {
    *c = CivilT2(t1);
    return true;
  }
  return false;
}

template <typename CivilT>
bool ParseLenient(string_view s, CivilT* c) {
  // A fastpath for when the given string data parses exactly into the given
  // type T (e.g., s="YYYY-MM-DD" and CivilT=CivilDay).
  if (ParseCivilTime(s, c)) return true;
  // Try parsing as each of the 6 types, trying the most common types first
  // (based on csearch results).
  if (ParseAs<CivilDay>(s, c)) return true;
  if (ParseAs<CivilSecond>(s, c)) return true;
  if (ParseAs<CivilHour>(s, c)) return true;
  if (ParseAs<CivilMonth>(s, c)) return true;
  if (ParseAs<CivilMinute>(s, c)) return true;
  if (ParseAs<CivilYear>(s, c)) return true;
  return false;
}
}  // namespace

std::string FormatCivilTime(CivilSecond c) {
  return FormatYearAnd("-%m-%d%ET%H:%M:%S", c);
}
std::string FormatCivilTime(CivilMinute c) {
  return FormatYearAnd("-%m-%d%ET%H:%M", c);
}
std::string FormatCivilTime(CivilHour c) {
  return FormatYearAnd("-%m-%d%ET%H", c);
}
std::string FormatCivilTime(CivilDay c) { return FormatYearAnd("-%m-%d", c); }
std::string FormatCivilTime(CivilMonth c) { return FormatYearAnd("-%m", c); }
std::string FormatCivilTime(CivilYear c) { return FormatYearAnd("", c); }

bool ParseCivilTime(string_view s, CivilSecond* c) {
  return ParseYearAnd("-%m-%d%ET%H:%M:%S", s, c);
}
bool ParseCivilTime(string_view s, CivilMinute* c) {
  return ParseYearAnd("-%m-%d%ET%H:%M", s, c);
}
bool ParseCivilTime(string_view s, CivilHour* c) {
  return ParseYearAnd("-%m-%d%ET%H", s, c);
}
bool ParseCivilTime(string_view s, CivilDay* c) {
  return ParseYearAnd("-%m-%d", s, c);
}
bool ParseCivilTime(string_view s, CivilMonth* c) {
  return ParseYearAnd("-%m", s, c);
}
bool ParseCivilTime(string_view s, CivilYear* c) {
  return ParseYearAnd("", s, c);
}

bool ParseLenientCivilTime(string_view s, CivilSecond* c) {
  return ParseLenient(s, c);
}
bool ParseLenientCivilTime(string_view s, CivilMinute* c) {
  return ParseLenient(s, c);
}
bool ParseLenientCivilTime(string_view s, CivilHour* c) {
  return ParseLenient(s, c);
}
bool ParseLenientCivilTime(string_view s, CivilDay* c) {
  return ParseLenient(s, c);
}
bool ParseLenientCivilTime(string_view s, CivilMonth* c) {
  return ParseLenient(s, c);
}
bool ParseLenientCivilTime(string_view s, CivilYear* c) {
  return ParseLenient(s, c);
}

namespace time_internal {

std::ostream& operator<<(std::ostream& os, CivilYear y) {
  return os << FormatCivilTime(y);
}
std::ostream& operator<<(std::ostream& os, CivilMonth m) {
  return os << FormatCivilTime(m);
}
std::ostream& operator<<(std::ostream& os, CivilDay d) {
  return os << FormatCivilTime(d);
}
std::ostream& operator<<(std::ostream& os, CivilHour h) {
  return os << FormatCivilTime(h);
}
std::ostream& operator<<(std::ostream& os, CivilMinute m) {
  return os << FormatCivilTime(m);
}
std::ostream& operator<<(std::ostream& os, CivilSecond s) {
  return os << FormatCivilTime(s);
}

bool AbslParseFlag(string_view s, CivilSecond* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
bool AbslParseFlag(string_view s, CivilMinute* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
bool AbslParseFlag(string_view s, CivilHour* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
bool AbslParseFlag(string_view s, CivilDay* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
bool AbslParseFlag(string_view s, CivilMonth* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
bool AbslParseFlag(string_view s, CivilYear* c, std::string*) {
  return ParseLenientCivilTime(s, c);
}
std::string AbslUnparseFlag(CivilSecond c) { return FormatCivilTime(c); }
std::string AbslUnparseFlag(CivilMinute c) { return FormatCivilTime(c); }
std::string AbslUnparseFlag(CivilHour c) { return FormatCivilTime(c); }
std::string AbslUnparseFlag(CivilDay c) { return FormatCivilTime(c); }
std::string AbslUnparseFlag(CivilMonth c) { return FormatCivilTime(c); }
std::string AbslUnparseFlag(CivilYear c) { return FormatCivilTime(c); }

}  // namespace time_internal

ABSL_NAMESPACE_END
}  // namespace absl
