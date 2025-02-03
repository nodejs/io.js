// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DATE_DATE_H_
#define V8_DATE_DATE_H_

#include <cmath>

#include "src/base/small-vector.h"
#include "src/base/timezone-cache.h"
#include "src/common/globals.h"
#include "src/objects/smi.h"

namespace v8 {
namespace internal {

class V8_EXPORT_PRIVATE DateCache {
 public:
  static const int kMsPerMin = 60 * 1000;
  static const int kSecPerDay = 24 * 60 * 60;
  static const int64_t kMsPerDay = kSecPerDay * 1000;
  static const int64_t kMsPerMonth = kMsPerDay * 30;

  // The largest time that can be passed to OS date-time library functions.
  static const int kMaxEpochTimeInSec = kMaxInt;
  static const int64_t kMaxEpochTimeInMs = static_cast<int64_t>(kMaxInt) * 1000;

  // The largest time that can be stored in JSDate.
  static const int64_t kMaxTimeInMs =
      static_cast<int64_t>(864000000) * 10000000;

  // Conservative upper bound on time that can be stored in JSDate
  // before UTC conversion.
  static const int64_t kMaxTimeBeforeUTCInMs = kMaxTimeInMs + kMsPerMonth;

  // Sentinel that denotes an invalid local offset.
  static const int kInvalidLocalOffsetInMs = kMaxInt;
  // Sentinel that denotes an invalid cache stamp.
  // It is an invariant of DateCache that cache stamp is non-negative.
  static const int kInvalidStamp = -1;

  DateCache();

  virtual ~DateCache() {
    delete tz_cache_;
    tz_cache_ = nullptr;
  }

  // Clears cached timezone information and increments the cache stamp.
  void ResetDateCache(
      base::TimezoneCache::TimeZoneDetection time_zone_detection);

  // Computes floor(time_ms / kMsPerDay).
  static int DaysFromTime(int64_t time_ms) {
    if (time_ms < 0) time_ms -= (kMsPerDay - 1);
    return static_cast<int>(time_ms / kMsPerDay);
  }

  // Computes modulo(time_ms, kMsPerDay) given that
  // days = floor(time_ms / kMsPerDay).
  static int TimeInDay(int64_t time_ms, int days) {
    return static_cast<int>(time_ms - days * kMsPerDay);
  }

  // Performs the success path of the ECMA 262 TimeClip operation (when the
  // value is within the range, truncates it to an integer). Returns false if
  // the value is outside the range, and should be clipped to NaN.
  // ECMA 262 - ES#sec-timeclip TimeClip (time)
  static bool TryTimeClip(double* time) {
    if (-kMaxTimeInMs <= *time && *time <= kMaxTimeInMs) {
      // Inline the finite part of DoubleToInteger here, since the range check
      // already covers the non-finite checks.
      *time = ((*time > 0) ? std::floor(*time) : std::ceil(*time)) + 0.0;
      return true;
    }
    return false;
  }

  // Given the number of days since the epoch, computes the weekday.
  // ECMA 262 - 15.9.1.6.
  int Weekday(int days) {
    int result = (days + 4) % 7;
    return result >= 0 ? result : result + 7;
  }

  bool IsLeap(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  }

  // ECMA 262 - ES#sec-local-time-zone-adjustment
  int LocalOffsetInMs(int64_t time, bool is_utc) {
    return GetLocalOffsetFromOS(time, is_utc);
  }

  const char* LocalTimezone(int64_t time_ms) {
    if (time_ms < 0 || time_ms > kMaxEpochTimeInMs) {
      time_ms = EquivalentTime(time_ms);
    }
    bool is_dst = DaylightSavingsOffsetInMs(time_ms) != 0;
    const char** name = is_dst ? &dst_tz_name_ : &tz_name_;
    if (*name == nullptr) {
      *name = tz_cache_->LocalTimezone(static_cast<double>(time_ms));
    }
    return *name;
  }

  // ECMA 262 - 15.9.5.26
  int TimezoneOffset(int64_t time_ms) {
    int64_t local_ms = ToLocal(time_ms);
    return static_cast<int>((time_ms - local_ms) / kMsPerMin);
  }

  // ECMA 262 - ES#sec-localtime-t
  // LocalTime(t) = t + LocalTZA(t, true)
  int64_t ToLocal(int64_t time_ms) {
    return time_ms + LocalOffsetInMs(time_ms, true);
  }

  // ECMA 262 - ES#sec-utc-t
  // UTC(t) = t - LocalTZA(t, false)
  int64_t ToUTC(int64_t time_ms) {
    return time_ms - LocalOffsetInMs(time_ms, false);
  }

  // Computes a time equivalent to the given time according
  // to ECMA 262 - 15.9.1.9.
  // The issue here is that some library calls don't work right for dates
  // that cannot be represented using a non-negative signed 32 bit integer
  // (measured in whole seconds based on the 1970 epoch).
  // We solve this by mapping the time to a year with same leap-year-ness
  // and same starting day for the year. The ECMAscript specification says
  // we must do this, but for compatibility with other browsers, we use
  // the actual year if it is in the range 1970..2037
  int64_t EquivalentTime(int64_t time_ms) {
    int days = DaysFromTime(time_ms);
    int time_within_day_ms = static_cast<int>(time_ms - days * kMsPerDay);
    int year, month, day;
    YearMonthDayFromDays(days, &year, &month, &day);
    int new_days = DaysFromYearMonth(EquivalentYear(year), month) + day - 1;
    return static_cast<int64_t>(new_days) * kMsPerDay + time_within_day_ms;
  }

  // Returns an equivalent year in the range [2008-2035] matching
  // - leap year,
  // - week day of first day.
  // ECMA 262 - 15.9.1.9.
  int EquivalentYear(int year) {
    int week_day = Weekday(DaysFromYearMonth(year, 0));
    int recent_year = (IsLeap(year) ? 1956 : 1967) + (week_day * 12) % 28;
    // Find the year in the range 2008..2037 that is equivalent mod 28.
    // Add 3*28 to give a positive argument to the modulus operator.
    return 2008 + (recent_year + 3 * 28 - 2008) % 28;
  }

  // Given the number of days since the epoch, computes
  // the corresponding year, month, and day.
  void YearMonthDayFromDays(int days, int* year, int* month, int* day);

  // Computes the number of days since the epoch for
  // the first day of the given month in the given year.
  int DaysFromYearMonth(int year, int month);

  // Breaks down the time value.
  void BreakDownTime(int64_t time_ms, int* year, int* month, int* day,
                     int* weekday, int* hour, int* min, int* sec, int* ms);

  // Cache stamp is used for invalidating caches in JSDate.
  // We increment the stamp each time when the timezone information changes.
  // JSDate objects perform stamp check and invalidate their caches if
  // their saved stamp is not equal to the current stamp.
  Tagged<Smi> stamp() { return stamp_; }
  void* stamp_address() { return &stamp_; }

  // These functions are virtual so that we can override them when testing.
  virtual int GetDaylightSavingsOffsetFromOS(int64_t time_sec) {
    double time_ms = static_cast<double>(time_sec * 1000);
    return static_cast<int>(tz_cache_->DaylightSavingsOffset(time_ms));
  }

  virtual int GetLocalOffsetFromOS(int64_t time_ms, bool is_utc);

 private:
  // The implementation relies on the fact that no time zones have
  // more than one daylight savings offset change per 19 days.
  // In Egypt in 2010 they decided to suspend DST during Ramadan. This
  // led to a short interval where DST is in effect from September 10 to
  // September 30.
  static const int kDefaultDSTDeltaInSec = 19 * kSecPerDay;

  // Size of the Daylight Savings Time cache.
  static const int kDSTSize = 32;

  // Daylight Savings Time segment stores a segment of time where
  // daylight savings offset does not change.
  struct DST {
    int start_sec;
    int end_sec;
    int offset_ms;
    int last_used;
  };

  // Computes the daylight savings offset for the given time.
  // ECMA 262 - 15.9.1.8
  int DaylightSavingsOffsetInMs(int64_t time_ms);

  // Sets the before_ and the after_ segments from the DST cache such that
  // the before_ segment starts earlier than the given time and
  // the after_ segment start later than the given time.
  // Both segments might be invalid.
  // The last_used counters of the before_ and after_ are updated.
  void ProbeDST(int time_sec);

  // Finds the least recently used segment from the DST cache that is not
  // equal to the given 'skip' segment.
  DST* LeastRecentlyUsedDST(DST* skip);

  // Extends the after_ segment with the given point or resets it
  // if it starts later than the given time + kDefaultDSTDeltaInSec.
  inline void ExtendTheAfterSegment(int time_sec, int offset_ms);

  // Makes the given segment invalid.
  inline void ClearSegment(DST* segment);

  bool InvalidSegment(DST* segment) {
    return segment->start_sec > segment->end_sec;
  }

  Tagged<Smi> stamp_;

  // Daylight Saving Time cache.
  DST dst_[kDSTSize];
  int dst_usage_counter_;
  DST* before_;
  DST* after_;

  int local_offset_ms_;

  // Year/Month/Day cache.
  bool ymd_valid_;
  int ymd_days_;
  int ymd_year_;
  int ymd_month_;
  int ymd_day_;

  // Timezone name cache
  const char* tz_name_;
  const char* dst_tz_name_;

  base::TimezoneCache* tz_cache_;
};

// Routines shared between Date and Temporal

// ES6 section 20.3.1.14 MakeDate (day, time)
double MakeDate(double day, double time);

// ES6 section 20.3.1.13 MakeDay (year, month, date)
double MakeDay(double year, double month, double date);

// ES6 section 20.3.1.12 MakeTime (hour, min, sec, ms)
double MakeTime(double hour, double min, double sec, double ms);

using DateBuffer = base::SmallVector<char, 128>;

enum class ToDateStringMode {
  kLocalDate,
  kLocalTime,
  kLocalDateAndTime,
  kUTCDateAndTime,
  kISODateAndTime
};

// ES6 section 20.3.4.41.1 ToDateString(tv)
DateBuffer ToDateString(double time_val, DateCache* date_cache,
                        ToDateStringMode mode);

double ParseDateTimeString(Isolate* isolate, Handle<String> str);

}  // namespace internal
}  // namespace v8

#endif  // V8_DATE_DATE_H_
