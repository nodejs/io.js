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

// This file implements the TimeZoneIf interface using the "zoneinfo"
// data provided by the IANA Time Zone Database (i.e., the only real game
// in town).
//
// TimeZoneInfo represents the history of UTC-offset changes within a time
// zone. Most changes are due to daylight-saving rules, but occasionally
// shifts are made to the time-zone's base offset. The database only attempts
// to be definitive for times since 1970, so be wary of local-time conversions
// before that. Also, rule and zone-boundary changes are made at the whim
// of governments, so the conversion of future times needs to be taken with
// a grain of salt.
//
// For more information see tzfile(5), http://www.iana.org/time-zones, or
// https://en.wikipedia.org/wiki/Zoneinfo.
//
// Note that we assume the proleptic Gregorian calendar and 60-second
// minutes throughout.

#include "time_zone_info.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time.h"
#include "time_zone_fixed.h"
#include "time_zone_posix.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

namespace {

inline bool IsLeap(year_t year) {
  return (year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0);
}

// The number of days in non-leap and leap years respectively.
const std::int_least32_t kDaysPerYear[2] = {365, 366};

// The day offsets of the beginning of each (1-based) month in non-leap and
// leap years respectively (e.g., 335 days before December in a leap year).
const std::int_least16_t kMonthOffsets[2][1 + 12 + 1] = {
    {-1, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365},
    {-1, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366},
};

// We reject leap-second encoded zoneinfo and so assume 60-second minutes.
const std::int_least32_t kSecsPerDay = 24 * 60 * 60;

// 400-year chunks always have 146097 days (20871 weeks).
const std::int_least64_t kSecsPer400Years = 146097LL * kSecsPerDay;

// Like kDaysPerYear[] but scaled up by a factor of kSecsPerDay.
const std::int_least32_t kSecsPerYear[2] = {
    365 * kSecsPerDay,
    366 * kSecsPerDay,
};

// Convert a cctz::weekday to a POSIX TZ weekday number (0==Sun, ..., 6=Sat).
inline int ToPosixWeekday(weekday wd) {
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

// Single-byte, unsigned numeric values are encoded directly.
inline std::uint_fast8_t Decode8(const char* cp) {
  return static_cast<std::uint_fast8_t>(*cp) & 0xff;
}

// Multi-byte, numeric values are encoded using a MSB first,
// twos-complement representation. These helpers decode, from
// the given address, 4-byte and 8-byte values respectively.
// Note: If int_fastXX_t == intXX_t and this machine is not
// twos complement, then there will be at least one input value
// we cannot represent.
std::int_fast32_t Decode32(const char* cp) {
  std::uint_fast32_t v = 0;
  for (int i = 0; i != (32 / 8); ++i) v = (v << 8) | Decode8(cp++);
  const std::int_fast32_t s32max = 0x7fffffff;
  const auto s32maxU = static_cast<std::uint_fast32_t>(s32max);
  if (v <= s32maxU) return static_cast<std::int_fast32_t>(v);
  return static_cast<std::int_fast32_t>(v - s32maxU - 1) - s32max - 1;
}

std::int_fast64_t Decode64(const char* cp) {
  std::uint_fast64_t v = 0;
  for (int i = 0; i != (64 / 8); ++i) v = (v << 8) | Decode8(cp++);
  const std::int_fast64_t s64max = 0x7fffffffffffffff;
  const auto s64maxU = static_cast<std::uint_fast64_t>(s64max);
  if (v <= s64maxU) return static_cast<std::int_fast64_t>(v);
  return static_cast<std::int_fast64_t>(v - s64maxU - 1) - s64max - 1;
}

struct Header {            // counts of:
  std::size_t timecnt;     // transition times
  std::size_t typecnt;     // transition types
  std::size_t charcnt;     // zone abbreviation characters
  std::size_t leapcnt;     // leap seconds (we expect none)
  std::size_t ttisstdcnt;  // UTC/local indicators (unused)
  std::size_t ttisutcnt;   // standard/wall indicators (unused)

  bool Build(const tzhead& tzh);
  std::size_t DataLength(std::size_t time_len) const;
};

// Builds the in-memory header using the raw bytes from the file.
bool Header::Build(const tzhead& tzh) {
  std::int_fast32_t v;
  if ((v = Decode32(tzh.tzh_timecnt)) < 0) return false;
  timecnt = static_cast<std::size_t>(v);
  if ((v = Decode32(tzh.tzh_typecnt)) < 0) return false;
  typecnt = static_cast<std::size_t>(v);
  if ((v = Decode32(tzh.tzh_charcnt)) < 0) return false;
  charcnt = static_cast<std::size_t>(v);
  if ((v = Decode32(tzh.tzh_leapcnt)) < 0) return false;
  leapcnt = static_cast<std::size_t>(v);
  if ((v = Decode32(tzh.tzh_ttisstdcnt)) < 0) return false;
  ttisstdcnt = static_cast<std::size_t>(v);
  if ((v = Decode32(tzh.tzh_ttisutcnt)) < 0) return false;
  ttisutcnt = static_cast<std::size_t>(v);
  return true;
}

// How many bytes of data are associated with this header. The result
// depends upon whether this is a section with 4-byte or 8-byte times.
std::size_t Header::DataLength(std::size_t time_len) const {
  std::size_t len = 0;
  len += (time_len + 1) * timecnt;  // unix_time + type_index
  len += (4 + 1 + 1) * typecnt;     // utc_offset + is_dst + abbr_index
  len += 1 * charcnt;               // abbreviations
  len += (time_len + 4) * leapcnt;  // leap-time + TAI-UTC
  len += 1 * ttisstdcnt;            // UTC/local indicators
  len += 1 * ttisutcnt;             // standard/wall indicators
  return len;
}

// Does the rule for future transitions call for year-round daylight time?
// See tz/zic.c:stringzone() for the details on how such rules are encoded.
bool AllYearDST(const PosixTimeZone& posix) {
  if (posix.dst_start.date.fmt != PosixTransition::N) return false;
  if (posix.dst_start.date.n.day != 0) return false;
  if (posix.dst_start.time.offset != 0) return false;

  if (posix.dst_end.date.fmt != PosixTransition::J) return false;
  if (posix.dst_end.date.j.day != kDaysPerYear[0]) return false;
  const auto offset = posix.std_offset - posix.dst_offset;
  if (posix.dst_end.time.offset + offset != kSecsPerDay) return false;

  return true;
}

// Generate a year-relative offset for a PosixTransition.
std::int_fast64_t TransOffset(bool leap_year, int jan1_weekday,
                              const PosixTransition& pt) {
  std::int_fast64_t days = 0;
  switch (pt.date.fmt) {
    case PosixTransition::J: {
      days = pt.date.j.day;
      if (!leap_year || days < kMonthOffsets[1][3]) days -= 1;
      break;
    }
    case PosixTransition::N: {
      days = pt.date.n.day;
      break;
    }
    case PosixTransition::M: {
      const bool last_week = (pt.date.m.week == 5);
      days = kMonthOffsets[leap_year][pt.date.m.month + last_week];
      const std::int_fast64_t weekday = (jan1_weekday + days) % 7;
      if (last_week) {
        days -= (weekday + 7 - 1 - pt.date.m.weekday) % 7 + 1;
      } else {
        days += (pt.date.m.weekday + 7 - weekday) % 7;
        days += (pt.date.m.week - 1) * 7;
      }
      break;
    }
  }
  return (days * kSecsPerDay) + pt.time.offset;
}

inline time_zone::civil_lookup MakeUnique(const time_point<seconds>& tp) {
  time_zone::civil_lookup cl;
  cl.kind = time_zone::civil_lookup::UNIQUE;
  cl.pre = cl.trans = cl.post = tp;
  return cl;
}

inline time_zone::civil_lookup MakeUnique(std::int_fast64_t unix_time) {
  return MakeUnique(FromUnixSeconds(unix_time));
}

inline time_zone::civil_lookup MakeSkipped(const Transition& tr,
                                           const civil_second& cs) {
  time_zone::civil_lookup cl;
  cl.kind = time_zone::civil_lookup::SKIPPED;
  cl.pre = FromUnixSeconds(tr.unix_time - 1 + (cs - tr.prev_civil_sec));
  cl.trans = FromUnixSeconds(tr.unix_time);
  cl.post = FromUnixSeconds(tr.unix_time - (tr.civil_sec - cs));
  return cl;
}

inline time_zone::civil_lookup MakeRepeated(const Transition& tr,
                                            const civil_second& cs) {
  time_zone::civil_lookup cl;
  cl.kind = time_zone::civil_lookup::REPEATED;
  cl.pre = FromUnixSeconds(tr.unix_time - 1 - (tr.prev_civil_sec - cs));
  cl.trans = FromUnixSeconds(tr.unix_time);
  cl.post = FromUnixSeconds(tr.unix_time + (cs - tr.civil_sec));
  return cl;
}

inline civil_second YearShift(const civil_second& cs, year_t shift) {
  return civil_second(cs.year() + shift, cs.month(), cs.day(), cs.hour(),
                      cs.minute(), cs.second());
}

}  // namespace

// Find/make a transition type with these attributes.
bool TimeZoneInfo::GetTransitionType(std::int_fast32_t utc_offset, bool is_dst,
                                     const std::string& abbr,
                                     std::uint_least8_t* index) {
  std::size_t type_index = 0;
  std::size_t abbr_index = abbreviations_.size();
  for (; type_index != transition_types_.size(); ++type_index) {
    const TransitionType& tt(transition_types_[type_index]);
    const char* tt_abbr = &abbreviations_[tt.abbr_index];
    if (tt_abbr == abbr) abbr_index = tt.abbr_index;
    if (tt.utc_offset == utc_offset && tt.is_dst == is_dst) {
      if (abbr_index == tt.abbr_index) break;  // reuse
    }
  }
  if (type_index > 255 || abbr_index > 255) {
    // No index space (8 bits) available for a new type or abbreviation.
    return false;
  }
  if (type_index == transition_types_.size()) {
    TransitionType& tt(*transition_types_.emplace(transition_types_.end()));
    tt.utc_offset = static_cast<std::int_least32_t>(utc_offset);
    tt.is_dst = is_dst;
    if (abbr_index == abbreviations_.size()) {
      abbreviations_.append(abbr);
      abbreviations_.append(1, '\0');
    }
    tt.abbr_index = static_cast<std::uint_least8_t>(abbr_index);
  }
  *index = static_cast<std::uint_least8_t>(type_index);
  return true;
}

// zic(8) can generate no-op transitions when a zone changes rules at an
// instant when there is actually no discontinuity.  So we check whether
// two transitions have equivalent types (same offset/is_dst/abbr).
bool TimeZoneInfo::EquivTransitions(std::uint_fast8_t tt1_index,
                                    std::uint_fast8_t tt2_index) const {
  if (tt1_index == tt2_index) return true;
  const TransitionType& tt1(transition_types_[tt1_index]);
  const TransitionType& tt2(transition_types_[tt2_index]);
  if (tt1.utc_offset != tt2.utc_offset) return false;
  if (tt1.is_dst != tt2.is_dst) return false;
  if (tt1.abbr_index != tt2.abbr_index) return false;
  return true;
}

// Use the POSIX-TZ-environment-variable-style string to handle times
// in years after the last transition stored in the zoneinfo data.
bool TimeZoneInfo::ExtendTransitions() {
  extended_ = false;
  if (future_spec_.empty()) return true;  // last transition prevails

  PosixTimeZone posix;
  if (!ParsePosixSpec(future_spec_, &posix)) return false;

  // Find transition type for the future std specification.
  std::uint_least8_t std_ti;
  if (!GetTransitionType(posix.std_offset, false, posix.std_abbr, &std_ti))
    return false;

  if (posix.dst_abbr.empty()) {  // std only
    // The future specification should match the last transition, and
    // that means that handling the future will fall out naturally.
    return EquivTransitions(transitions_.back().type_index, std_ti);
  }

  // Find transition type for the future dst specification.
  std::uint_least8_t dst_ti;
  if (!GetTransitionType(posix.dst_offset, true, posix.dst_abbr, &dst_ti))
    return false;

  if (AllYearDST(posix)) {  // dst only
    // The future specification should match the last transition, and
    // that means that handling the future will fall out naturally.
    return EquivTransitions(transitions_.back().type_index, dst_ti);
  }

  // Extend the transitions for an additional 401 years using the future
  // specification. Years beyond those can be handled by mapping back to
  // a cycle-equivalent year within that range. Note that we need 401
  // (well, at least the first transition in the 401st year) so that the
  // end of the 400th year is mapped back to an extended year. And first
  // we may also need two additional transitions for the current year.
  transitions_.reserve(transitions_.size() + 2 + 401 * 2);
  extended_ = true;

  const Transition& last(transitions_.back());
  const std::int_fast64_t last_time = last.unix_time;
  const TransitionType& last_tt(transition_types_[last.type_index]);
  last_year_ = LocalTime(last_time, last_tt).cs.year();
  bool leap_year = IsLeap(last_year_);
  const civil_second jan1(last_year_);
  std::int_fast64_t jan1_time = jan1 - civil_second();
  int jan1_weekday = ToPosixWeekday(get_weekday(jan1));

  Transition dst = {0, dst_ti, civil_second(), civil_second()};
  Transition std = {0, std_ti, civil_second(), civil_second()};
  for (const year_t limit = last_year_ + 401;; ++last_year_) {
    auto dst_trans_off = TransOffset(leap_year, jan1_weekday, posix.dst_start);
    auto std_trans_off = TransOffset(leap_year, jan1_weekday, posix.dst_end);
    dst.unix_time = jan1_time + dst_trans_off - posix.std_offset;
    std.unix_time = jan1_time + std_trans_off - posix.dst_offset;
    const auto* ta = dst.unix_time < std.unix_time ? &dst : &std;
    const auto* tb = dst.unix_time < std.unix_time ? &std : &dst;
    if (last_time < tb->unix_time) {
      if (last_time < ta->unix_time) transitions_.push_back(*ta);
      transitions_.push_back(*tb);
    }
    if (last_year_ == limit) break;
    jan1_time += kSecsPerYear[leap_year];
    jan1_weekday = (jan1_weekday + kDaysPerYear[leap_year]) % 7;
    leap_year = !leap_year && IsLeap(last_year_ + 1);
  }

  return true;
}

namespace {

using FilePtr = std::unique_ptr<FILE, int (*)(FILE*)>;

// fopen(3) adaptor.
inline FilePtr FOpen(const char* path, const char* mode) {
#if defined(_MSC_VER)
  FILE* fp;
  if (fopen_s(&fp, path, mode) != 0) fp = nullptr;
  return FilePtr(fp, fclose);
#else
  // TODO: Enable the close-on-exec flag.
  return FilePtr(fopen(path, mode), fclose);
#endif
}

// A stdio(3)-backed implementation of ZoneInfoSource.
class FileZoneInfoSource : public ZoneInfoSource {
 public:
  static std::unique_ptr<ZoneInfoSource> Open(const std::string& name);

  std::size_t Read(void* ptr, std::size_t size) override {
    size = std::min(size, len_);
    std::size_t nread = fread(ptr, 1, size, fp_.get());
    len_ -= nread;
    return nread;
  }
  int Skip(std::size_t offset) override {
    offset = std::min(offset, len_);
    int rc = fseek(fp_.get(), static_cast<long>(offset), SEEK_CUR);
    if (rc == 0) len_ -= offset;
    return rc;
  }
  std::string Version() const override {
    // TODO: It would nice if the zoneinfo data included the tzdb version.
    return std::string();
  }

 protected:
  explicit FileZoneInfoSource(
      FilePtr fp, std::size_t len = std::numeric_limits<std::size_t>::max())
      : fp_(std::move(fp)), len_(len) {}

 private:
  FilePtr fp_;
  std::size_t len_;
};

std::unique_ptr<ZoneInfoSource> FileZoneInfoSource::Open(
    const std::string& name) {
  // Use of the "file:" prefix is intended for testing purposes only.
  const std::size_t pos = (name.compare(0, 5, "file:") == 0) ? 5 : 0;

  // Map the time-zone name to a path name.
  std::string path;
  if (pos == name.size() || name[pos] != '/') {
    const char* tzdir = "/usr/share/zoneinfo";
    char* tzdir_env = nullptr;
#if defined(_MSC_VER)
    _dupenv_s(&tzdir_env, nullptr, "TZDIR");
#else
    tzdir_env = std::getenv("TZDIR");
#endif
    if (tzdir_env && *tzdir_env) tzdir = tzdir_env;
    path += tzdir;
    path += '/';
#if defined(_MSC_VER)
    free(tzdir_env);
#endif
  }
  path.append(name, pos, std::string::npos);

  // Open the zoneinfo file.
  auto fp = FOpen(path.c_str(), "rb");
  if (fp == nullptr) return nullptr;
  return std::unique_ptr<ZoneInfoSource>(new FileZoneInfoSource(std::move(fp)));
}

class AndroidZoneInfoSource : public FileZoneInfoSource {
 public:
  static std::unique_ptr<ZoneInfoSource> Open(const std::string& name);
  std::string Version() const override { return version_; }

 private:
  explicit AndroidZoneInfoSource(FilePtr fp, std::size_t len,
                                 std::string version)
      : FileZoneInfoSource(std::move(fp), len), version_(std::move(version)) {}
  std::string version_;
};

std::unique_ptr<ZoneInfoSource> AndroidZoneInfoSource::Open(
    const std::string& name) {
  // Use of the "file:" prefix is intended for testing purposes only.
  const std::size_t pos = (name.compare(0, 5, "file:") == 0) ? 5 : 0;

  // See Android's libc/tzcode/bionic.cpp for additional information.
  for (const char* tzdata : {"/apex/com.android.tzdata/etc/tz/tzdata",
                             "/data/misc/zoneinfo/current/tzdata",
                             "/system/usr/share/zoneinfo/tzdata"}) {
    auto fp = FOpen(tzdata, "rb");
    if (fp == nullptr) continue;

    char hbuf[24];  // covers header.zonetab_offset too
    if (fread(hbuf, 1, sizeof(hbuf), fp.get()) != sizeof(hbuf)) continue;
    if (strncmp(hbuf, "tzdata", 6) != 0) continue;
    const char* vers = (hbuf[11] == '\0') ? hbuf + 6 : "";
    const std::int_fast32_t index_offset = Decode32(hbuf + 12);
    const std::int_fast32_t data_offset = Decode32(hbuf + 16);
    if (index_offset < 0 || data_offset < index_offset) continue;
    if (fseek(fp.get(), static_cast<long>(index_offset), SEEK_SET) != 0)
      continue;

    char ebuf[52];  // covers entry.unused too
    const std::size_t index_size =
        static_cast<std::size_t>(data_offset - index_offset);
    const std::size_t zonecnt = index_size / sizeof(ebuf);
    if (zonecnt * sizeof(ebuf) != index_size) continue;
    for (std::size_t i = 0; i != zonecnt; ++i) {
      if (fread(ebuf, 1, sizeof(ebuf), fp.get()) != sizeof(ebuf)) break;
      const std::int_fast32_t start = data_offset + Decode32(ebuf + 40);
      const std::int_fast32_t length = Decode32(ebuf + 44);
      if (start < 0 || length < 0) break;
      ebuf[40] = '\0';  // ensure zone name is NUL terminated
      if (strcmp(name.c_str() + pos, ebuf) == 0) {
        if (fseek(fp.get(), static_cast<long>(start), SEEK_SET) != 0) break;
        return std::unique_ptr<ZoneInfoSource>(new AndroidZoneInfoSource(
            std::move(fp), static_cast<std::size_t>(length), vers));
      }
    }
  }

  return nullptr;
}

// A zoneinfo source for use inside Fuchsia components. This attempts to
// read zoneinfo files from one of several known paths in a component's
// incoming namespace. [Config data][1] is preferred, but package-specific
// resources are also supported.
//
// Fuchsia's implementation supports `FileZoneInfoSource::Version()`.
//
// [1]:
// https://fuchsia.dev/fuchsia-src/development/components/data#using_config_data_in_your_component
class FuchsiaZoneInfoSource : public FileZoneInfoSource {
 public:
  static std::unique_ptr<ZoneInfoSource> Open(const std::string& name);
  std::string Version() const override { return version_; }

 private:
  explicit FuchsiaZoneInfoSource(FilePtr fp, std::string version)
      : FileZoneInfoSource(std::move(fp)), version_(std::move(version)) {}
  std::string version_;
};

std::unique_ptr<ZoneInfoSource> FuchsiaZoneInfoSource::Open(
    const std::string& name) {
  // Use of the "file:" prefix is intended for testing purposes only.
  const std::size_t pos = (name.compare(0, 5, "file:") == 0) ? 5 : 0;

  // Prefixes where a Fuchsia component might find zoneinfo files,
  // in descending order of preference.
  const auto kTzdataPrefixes = {
      // The tzdata from `config-data`.
      "/config/data/tzdata/",
      // The tzdata bundled in the component's package.
      "/pkg/data/tzdata/",
      // General data storage.
      "/data/tzdata/",
      // The recommended path for routed-in tzdata files.
      // See for details:
      // https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#typical_directory_structure
      "/config/tzdata/",
  };
  const auto kEmptyPrefix = {""};
  const bool name_absolute = (pos != name.size() && name[pos] == '/');
  const auto prefixes = name_absolute ? kEmptyPrefix : kTzdataPrefixes;

  // Fuchsia builds place zoneinfo files at "<prefix><format><name>".
  for (const std::string prefix : prefixes) {
    std::string path = prefix;
    if (!prefix.empty()) path += "zoneinfo/tzif2/";  // format
    path.append(name, pos, std::string::npos);

    auto fp = FOpen(path.c_str(), "rb");
    if (fp == nullptr) continue;

    std::string version;
    if (!prefix.empty()) {
      // Fuchsia builds place the version in "<prefix>revision.txt".
      std::ifstream version_stream(prefix + "revision.txt");
      if (version_stream.is_open()) {
        // revision.txt should contain no newlines, but to be
        // defensive we read just the first line.
        std::getline(version_stream, version);
      }
    }

    return std::unique_ptr<ZoneInfoSource>(
        new FuchsiaZoneInfoSource(std::move(fp), std::move(version)));
  }

  return nullptr;
}

}  // namespace

// What (no leap-seconds) UTC+seconds zoneinfo would look like.
bool TimeZoneInfo::ResetToBuiltinUTC(const seconds& offset) {
  transition_types_.resize(1);
  TransitionType& tt(transition_types_.back());
  tt.utc_offset = static_cast<std::int_least32_t>(offset.count());
  tt.is_dst = false;
  tt.abbr_index = 0;

  // We temporarily add some redundant, contemporary (2015 through 2025)
  // transitions for performance reasons.  See TimeZoneInfo::LocalTime().
  // TODO: Fix the performance issue and remove the extra transitions.
  transitions_.clear();
  transitions_.reserve(12);
  for (const std::int_fast64_t unix_time : {
           -(1LL << 59),  // a "first half" transition
           1420070400LL,  // 2015-01-01T00:00:00+00:00
           1451606400LL,  // 2016-01-01T00:00:00+00:00
           1483228800LL,  // 2017-01-01T00:00:00+00:00
           1514764800LL,  // 2018-01-01T00:00:00+00:00
           1546300800LL,  // 2019-01-01T00:00:00+00:00
           1577836800LL,  // 2020-01-01T00:00:00+00:00
           1609459200LL,  // 2021-01-01T00:00:00+00:00
           1640995200LL,  // 2022-01-01T00:00:00+00:00
           1672531200LL,  // 2023-01-01T00:00:00+00:00
           1704067200LL,  // 2024-01-01T00:00:00+00:00
           1735689600LL,  // 2025-01-01T00:00:00+00:00
       }) {
    Transition& tr(*transitions_.emplace(transitions_.end()));
    tr.unix_time = unix_time;
    tr.type_index = 0;
    tr.civil_sec = LocalTime(tr.unix_time, tt).cs;
    tr.prev_civil_sec = tr.civil_sec - 1;
  }

  default_transition_type_ = 0;
  abbreviations_ = FixedOffsetToAbbr(offset);
  abbreviations_.append(1, '\0');
  future_spec_.clear();  // never needed for a fixed-offset zone
  extended_ = false;

  tt.civil_max = LocalTime(seconds::max().count(), tt).cs;
  tt.civil_min = LocalTime(seconds::min().count(), tt).cs;

  transitions_.shrink_to_fit();
  return true;
}

bool TimeZoneInfo::Load(ZoneInfoSource* zip) {
  // Read and validate the header.
  tzhead tzh;
  if (zip->Read(&tzh, sizeof(tzh)) != sizeof(tzh)) return false;
  if (strncmp(tzh.tzh_magic, TZ_MAGIC, sizeof(tzh.tzh_magic)) != 0)
    return false;
  Header hdr;
  if (!hdr.Build(tzh)) return false;
  std::size_t time_len = 4;
  if (tzh.tzh_version[0] != '\0') {
    // Skip the 4-byte data.
    if (zip->Skip(hdr.DataLength(time_len)) != 0) return false;
    // Read and validate the header for the 8-byte data.
    if (zip->Read(&tzh, sizeof(tzh)) != sizeof(tzh)) return false;
    if (strncmp(tzh.tzh_magic, TZ_MAGIC, sizeof(tzh.tzh_magic)) != 0)
      return false;
    if (tzh.tzh_version[0] == '\0') return false;
    if (!hdr.Build(tzh)) return false;
    time_len = 8;
  }
  if (hdr.typecnt == 0) return false;
  if (hdr.leapcnt != 0) {
    // This code assumes 60-second minutes so we do not want
    // the leap-second encoded zoneinfo. We could reverse the
    // compensation, but the "right" encoding is rarely used
    // so currently we simply reject such data.
    return false;
  }
  if (hdr.ttisstdcnt != 0 && hdr.ttisstdcnt != hdr.typecnt) return false;
  if (hdr.ttisutcnt != 0 && hdr.ttisutcnt != hdr.typecnt) return false;

  // Read the data into a local buffer.
  std::size_t len = hdr.DataLength(time_len);
  std::vector<char> tbuf(len);
  if (zip->Read(tbuf.data(), len) != len) return false;
  const char* bp = tbuf.data();

  // Decode and validate the transitions.
  transitions_.reserve(hdr.timecnt + 2);
  transitions_.resize(hdr.timecnt);
  for (std::size_t i = 0; i != hdr.timecnt; ++i) {
    transitions_[i].unix_time = (time_len == 4) ? Decode32(bp) : Decode64(bp);
    bp += time_len;
    if (i != 0) {
      // Check that the transitions are ordered by time (as zic guarantees).
      if (!Transition::ByUnixTime()(transitions_[i - 1], transitions_[i]))
        return false;  // out of order
    }
  }
  bool seen_type_0 = false;
  for (std::size_t i = 0; i != hdr.timecnt; ++i) {
    transitions_[i].type_index = Decode8(bp++);
    if (transitions_[i].type_index >= hdr.typecnt) return false;
    if (transitions_[i].type_index == 0) seen_type_0 = true;
  }

  // Decode and validate the transition types.
  transition_types_.reserve(hdr.typecnt + 2);
  transition_types_.resize(hdr.typecnt);
  for (std::size_t i = 0; i != hdr.typecnt; ++i) {
    transition_types_[i].utc_offset =
        static_cast<std::int_least32_t>(Decode32(bp));
    if (transition_types_[i].utc_offset >= kSecsPerDay ||
        transition_types_[i].utc_offset <= -kSecsPerDay)
      return false;
    bp += 4;
    transition_types_[i].is_dst = (Decode8(bp++) != 0);
    transition_types_[i].abbr_index = Decode8(bp++);
    if (transition_types_[i].abbr_index >= hdr.charcnt) return false;
  }

  // Determine the before-first-transition type.
  default_transition_type_ = 0;
  if (seen_type_0 && hdr.timecnt != 0) {
    std::uint_fast8_t index = 0;
    if (transition_types_[0].is_dst) {
      index = transitions_[0].type_index;
      while (index != 0 && transition_types_[index].is_dst) --index;
    }
    while (index != hdr.typecnt && transition_types_[index].is_dst) ++index;
    if (index != hdr.typecnt) default_transition_type_ = index;
  }

  // Copy all the abbreviations.
  abbreviations_.reserve(hdr.charcnt + 10);
  abbreviations_.assign(bp, hdr.charcnt);
  bp += hdr.charcnt;

  // Skip the unused portions. We've already dispensed with leap-second
  // encoded zoneinfo. The ttisstd/ttisgmt indicators only apply when
  // interpreting a POSIX spec that does not include start/end rules, and
  // that isn't the case here (see "zic -p").
  bp += (time_len + 4) * hdr.leapcnt;  // leap-time + TAI-UTC
  bp += 1 * hdr.ttisstdcnt;            // UTC/local indicators
  bp += 1 * hdr.ttisutcnt;             // standard/wall indicators
  assert(bp == tbuf.data() + tbuf.size());

  future_spec_.clear();
  if (tzh.tzh_version[0] != '\0') {
    // Snarf up the NL-enclosed future POSIX spec. Note
    // that version '3' files utilize an extended format.
    auto get_char = [](ZoneInfoSource* azip) -> int {
      unsigned char ch;  // all non-EOF results are positive
      return (azip->Read(&ch, 1) == 1) ? ch : EOF;
    };
    if (get_char(zip) != '\n') return false;
    for (int c = get_char(zip); c != '\n'; c = get_char(zip)) {
      if (c == EOF) return false;
      future_spec_.push_back(static_cast<char>(c));
    }
  }

  // We don't check for EOF so that we're forwards compatible.

  // If we did not find version information during the standard loading
  // process (as of tzh_version '3' that is unsupported), then ask the
  // ZoneInfoSource for any out-of-bound version string it may be privy to.
  if (version_.empty()) {
    version_ = zip->Version();
  }

  // Ensure that there is always a transition in the first half of the
  // time line (the second half is handled below) so that the signed
  // difference between a civil_second and the civil_second of its
  // previous transition is always representable, without overflow.
  if (transitions_.empty() || transitions_.front().unix_time >= 0) {
    Transition& tr(*transitions_.emplace(transitions_.begin()));
    tr.unix_time = -(1LL << 59);  // -18267312070-10-26T17:01:52+00:00
    tr.type_index = default_transition_type_;
  }

  // Extend the transitions using the future specification.
  if (!ExtendTransitions()) return false;

  // Ensure that there is always a transition in the second half of the
  // time line (the first half is handled above) so that the signed
  // difference between a civil_second and the civil_second of its
  // previous transition is always representable, without overflow.
  const Transition& last(transitions_.back());
  if (last.unix_time < 0) {
    const std::uint_fast8_t type_index = last.type_index;
    Transition& tr(*transitions_.emplace(transitions_.end()));
    tr.unix_time = 2147483647;  // 2038-01-19T03:14:07+00:00
    tr.type_index = type_index;
  }

  // Compute the local civil time for each transition and the preceding
  // second. These will be used for reverse conversions in MakeTime().
  const TransitionType* ttp = &transition_types_[default_transition_type_];
  for (std::size_t i = 0; i != transitions_.size(); ++i) {
    Transition& tr(transitions_[i]);
    tr.prev_civil_sec = LocalTime(tr.unix_time, *ttp).cs - 1;
    ttp = &transition_types_[tr.type_index];
    tr.civil_sec = LocalTime(tr.unix_time, *ttp).cs;
    if (i != 0) {
      // Check that the transitions are ordered by civil time. Essentially
      // this means that an offset change cannot cross another such change.
      // No one does this in practice, and we depend on it in MakeTime().
      if (!Transition::ByCivilTime()(transitions_[i - 1], tr))
        return false;  // out of order
    }
  }

  // Compute the maximum/minimum civil times that can be converted to a
  // time_point<seconds> for each of the zone's transition types.
  for (auto& tt : transition_types_) {
    tt.civil_max = LocalTime(seconds::max().count(), tt).cs;
    tt.civil_min = LocalTime(seconds::min().count(), tt).cs;
  }

  transitions_.shrink_to_fit();
  return true;
}

bool TimeZoneInfo::Load(const std::string& name) {
  // We can ensure that the loading of UTC or any other fixed-offset
  // zone never fails because the simple, fixed-offset state can be
  // internally generated. Note that this depends on our choice to not
  // accept leap-second encoded ("right") zoneinfo.
  auto offset = seconds::zero();
  if (FixedOffsetFromName(name, &offset)) {
    return ResetToBuiltinUTC(offset);
  }

  // Find and use a ZoneInfoSource to load the named zone.
  auto zip = cctz_extension::zone_info_source_factory(
      name, [](const std::string& n) -> std::unique_ptr<ZoneInfoSource> {
        if (auto z = FileZoneInfoSource::Open(n)) return z;
        if (auto z = AndroidZoneInfoSource::Open(n)) return z;
        if (auto z = FuchsiaZoneInfoSource::Open(n)) return z;
        return nullptr;
      });
  return zip != nullptr && Load(zip.get());
}

std::unique_ptr<TimeZoneInfo> TimeZoneInfo::UTC() {
  auto tz = std::unique_ptr<TimeZoneInfo>(new TimeZoneInfo);
  tz->ResetToBuiltinUTC(seconds::zero());
  return tz;
}

std::unique_ptr<TimeZoneInfo> TimeZoneInfo::Make(const std::string& name) {
  auto tz = std::unique_ptr<TimeZoneInfo>(new TimeZoneInfo);
  if (!tz->Load(name)) tz.reset();  // fallback to UTC
  return tz;
}

// BreakTime() translation for a particular transition type.
time_zone::absolute_lookup TimeZoneInfo::LocalTime(
    std::int_fast64_t unix_time, const TransitionType& tt) const {
  // A civil time in "+offset" looks like (time+offset) in UTC.
  // Note: We perform two additions in the civil_second domain to
  // sidestep the chance of overflow in (unix_time + tt.utc_offset).
  return {(civil_second() + unix_time) + tt.utc_offset, tt.utc_offset,
          tt.is_dst, &abbreviations_[tt.abbr_index]};
}

// BreakTime() translation for a particular transition.
time_zone::absolute_lookup TimeZoneInfo::LocalTime(std::int_fast64_t unix_time,
                                                   const Transition& tr) const {
  const TransitionType& tt = transition_types_[tr.type_index];
  // Note: (unix_time - tr.unix_time) will never overflow as we
  // have ensured that there is always a "nearby" transition.
  return {tr.civil_sec + (unix_time - tr.unix_time),  // TODO: Optimize.
          tt.utc_offset, tt.is_dst, &abbreviations_[tt.abbr_index]};
}

// MakeTime() translation with a conversion-preserving +N * 400-year shift.
time_zone::civil_lookup TimeZoneInfo::TimeLocal(const civil_second& cs,
                                                year_t c4_shift) const {
  assert(last_year_ - 400 < cs.year() && cs.year() <= last_year_);
  time_zone::civil_lookup cl = MakeTime(cs);
  if (c4_shift > seconds::max().count() / kSecsPer400Years) {
    cl.pre = cl.trans = cl.post = time_point<seconds>::max();
  } else {
    const auto offset = seconds(c4_shift * kSecsPer400Years);
    const auto limit = time_point<seconds>::max() - offset;
    for (auto* tp : {&cl.pre, &cl.trans, &cl.post}) {
      if (*tp > limit) {
        *tp = time_point<seconds>::max();
      } else {
        *tp += offset;
      }
    }
  }
  return cl;
}

time_zone::absolute_lookup TimeZoneInfo::BreakTime(
    const time_point<seconds>& tp) const {
  std::int_fast64_t unix_time = ToUnixSeconds(tp);
  const std::size_t timecnt = transitions_.size();
  assert(timecnt != 0);  // We always add a transition.

  if (unix_time < transitions_[0].unix_time) {
    return LocalTime(unix_time, transition_types_[default_transition_type_]);
  }
  if (unix_time >= transitions_[timecnt - 1].unix_time) {
    // After the last transition. If we extended the transitions using
    // future_spec_, shift back to a supported year using the 400-year
    // cycle of calendaric equivalence and then compensate accordingly.
    if (extended_) {
      const std::int_fast64_t diff =
          unix_time - transitions_[timecnt - 1].unix_time;
      const year_t shift = diff / kSecsPer400Years + 1;
      const auto d = seconds(shift * kSecsPer400Years);
      time_zone::absolute_lookup al = BreakTime(tp - d);
      al.cs = YearShift(al.cs, shift * 400);
      return al;
    }
    return LocalTime(unix_time, transitions_[timecnt - 1]);
  }

  const std::size_t hint = local_time_hint_.load(std::memory_order_relaxed);
  if (0 < hint && hint < timecnt) {
    if (transitions_[hint - 1].unix_time <= unix_time) {
      if (unix_time < transitions_[hint].unix_time) {
        return LocalTime(unix_time, transitions_[hint - 1]);
      }
    }
  }

  const Transition target = {unix_time, 0, civil_second(), civil_second()};
  const Transition* begin = &transitions_[0];
  const Transition* tr = std::upper_bound(begin, begin + timecnt, target,
                                          Transition::ByUnixTime());
  local_time_hint_.store(static_cast<std::size_t>(tr - begin),
                         std::memory_order_relaxed);
  return LocalTime(unix_time, *--tr);
}

time_zone::civil_lookup TimeZoneInfo::MakeTime(const civil_second& cs) const {
  const std::size_t timecnt = transitions_.size();
  assert(timecnt != 0);  // We always add a transition.

  // Find the first transition after our target civil time.
  const Transition* tr = nullptr;
  const Transition* begin = &transitions_[0];
  const Transition* end = begin + timecnt;
  if (cs < begin->civil_sec) {
    tr = begin;
  } else if (cs >= transitions_[timecnt - 1].civil_sec) {
    tr = end;
  } else {
    const std::size_t hint = time_local_hint_.load(std::memory_order_relaxed);
    if (0 < hint && hint < timecnt) {
      if (transitions_[hint - 1].civil_sec <= cs) {
        if (cs < transitions_[hint].civil_sec) {
          tr = begin + hint;
        }
      }
    }
    if (tr == nullptr) {
      const Transition target = {0, 0, cs, civil_second()};
      tr = std::upper_bound(begin, end, target, Transition::ByCivilTime());
      time_local_hint_.store(static_cast<std::size_t>(tr - begin),
                             std::memory_order_relaxed);
    }
  }

  if (tr == begin) {
    if (tr->prev_civil_sec >= cs) {
      // Before first transition, so use the default offset.
      const TransitionType& tt(transition_types_[default_transition_type_]);
      if (cs < tt.civil_min) return MakeUnique(time_point<seconds>::min());
      return MakeUnique(cs - (civil_second() + tt.utc_offset));
    }
    // tr->prev_civil_sec < cs < tr->civil_sec
    return MakeSkipped(*tr, cs);
  }

  if (tr == end) {
    if (cs > (--tr)->prev_civil_sec) {
      // After the last transition. If we extended the transitions using
      // future_spec_, shift back to a supported year using the 400-year
      // cycle of calendaric equivalence and then compensate accordingly.
      if (extended_ && cs.year() > last_year_) {
        const year_t shift = (cs.year() - last_year_ - 1) / 400 + 1;
        return TimeLocal(YearShift(cs, shift * -400), shift);
      }
      const TransitionType& tt(transition_types_[tr->type_index]);
      if (cs > tt.civil_max) return MakeUnique(time_point<seconds>::max());
      return MakeUnique(tr->unix_time + (cs - tr->civil_sec));
    }
    // tr->civil_sec <= cs <= tr->prev_civil_sec
    return MakeRepeated(*tr, cs);
  }

  if (tr->prev_civil_sec < cs) {
    // tr->prev_civil_sec < cs < tr->civil_sec
    return MakeSkipped(*tr, cs);
  }

  if (cs <= (--tr)->prev_civil_sec) {
    // tr->civil_sec <= cs <= tr->prev_civil_sec
    return MakeRepeated(*tr, cs);
  }

  // In between transitions.
  return MakeUnique(tr->unix_time + (cs - tr->civil_sec));
}

std::string TimeZoneInfo::Version() const { return version_; }

std::string TimeZoneInfo::Description() const {
  std::ostringstream oss;
  oss << "#trans=" << transitions_.size();
  oss << " #types=" << transition_types_.size();
  oss << " spec='" << future_spec_ << "'";
  return oss.str();
}

bool TimeZoneInfo::NextTransition(const time_point<seconds>& tp,
                                  time_zone::civil_transition* trans) const {
  if (transitions_.empty()) return false;
  const Transition* begin = &transitions_[0];
  const Transition* end = begin + transitions_.size();
  if (begin->unix_time <= -(1LL << 59)) {
    // Do not report the BIG_BANG found in some zoneinfo data as it is
    // really a sentinel, not a transition.  See pre-2018f tz/zic.c.
    ++begin;
  }
  std::int_fast64_t unix_time = ToUnixSeconds(tp);
  const Transition target = {unix_time, 0, civil_second(), civil_second()};
  const Transition* tr =
      std::upper_bound(begin, end, target, Transition::ByUnixTime());
  for (; tr != end; ++tr) {  // skip no-op transitions
    std::uint_fast8_t prev_type_index =
        (tr == begin) ? default_transition_type_ : tr[-1].type_index;
    if (!EquivTransitions(prev_type_index, tr[0].type_index)) break;
  }
  // When tr == end we return false, ignoring future_spec_.
  if (tr == end) return false;
  trans->from = tr->prev_civil_sec + 1;
  trans->to = tr->civil_sec;
  return true;
}

bool TimeZoneInfo::PrevTransition(const time_point<seconds>& tp,
                                  time_zone::civil_transition* trans) const {
  if (transitions_.empty()) return false;
  const Transition* begin = &transitions_[0];
  const Transition* end = begin + transitions_.size();
  if (begin->unix_time <= -(1LL << 59)) {
    // Do not report the BIG_BANG found in some zoneinfo data as it is
    // really a sentinel, not a transition.  See pre-2018f tz/zic.c.
    ++begin;
  }
  std::int_fast64_t unix_time = ToUnixSeconds(tp);
  if (FromUnixSeconds(unix_time) != tp) {
    if (unix_time == std::numeric_limits<std::int_fast64_t>::max()) {
      if (end == begin) return false;  // Ignore future_spec_.
      trans->from = (--end)->prev_civil_sec + 1;
      trans->to = end->civil_sec;
      return true;
    }
    unix_time += 1;  // ceils
  }
  const Transition target = {unix_time, 0, civil_second(), civil_second()};
  const Transition* tr =
      std::lower_bound(begin, end, target, Transition::ByUnixTime());
  for (; tr != begin; --tr) {  // skip no-op transitions
    std::uint_fast8_t prev_type_index =
        (tr - 1 == begin) ? default_transition_type_ : tr[-2].type_index;
    if (!EquivTransitions(prev_type_index, tr[-1].type_index)) break;
  }
  // When tr == end we return the "last" transition, ignoring future_spec_.
  if (tr == begin) return false;
  trans->from = (--tr)->prev_civil_sec + 1;
  trans->to = tr->civil_sec;
  return true;
}

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl
