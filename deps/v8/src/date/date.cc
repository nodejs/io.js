// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/date/date.h"

#include <limits>

#include "src/base/overflowing-math.h"
#include "src/date/dateparser-inl.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"
#ifdef V8_INTL_SUPPORT
#include "src/objects/intl-objects.h"
#endif
#include "src/strings/string-stream.h"

namespace v8 {
namespace internal {

static const int kDaysIn4Years = 4 * 365 + 1;
static const int kDaysIn100Years = 25 * kDaysIn4Years - 1;
static const int kDaysIn400Years = 4 * kDaysIn100Years + 1;
static const int kDays1970to2000 = 30 * 365 + 7;
static const int kDaysOffset =
    1000 * kDaysIn400Years + 5 * kDaysIn400Years - kDays1970to2000;
static const int kYearsOffset = 400000;
static const char kDaysInMonths[] = {31, 28, 31, 30, 31, 30,
                                     31, 31, 30, 31, 30, 31};

DateCache::DateCache()
    : stamp_(kNullAddress),
      tz_cache_(
#ifdef V8_INTL_SUPPORT
          Intl::CreateTimeZoneCache()
#else
          base::OS::CreateTimezoneCache()
#endif
      ) {
  ResetDateCache(base::TimezoneCache::TimeZoneDetection::kSkip);
}

void DateCache::ResetDateCache(
    base::TimezoneCache::TimeZoneDetection time_zone_detection) {
  if (stamp_.value() >= Smi::kMaxValue) {
    stamp_ = Smi::zero();
  } else {
    stamp_ = Smi::FromInt(stamp_.value() + 1);
  }
  DCHECK(stamp_ != Smi::FromInt(kInvalidStamp));
  for (int i = 0; i < kDSTSize; ++i) {
    ClearSegment(&dst_[i]);
  }
  dst_usage_counter_ = 0;
  before_ = &dst_[0];
  after_ = &dst_[1];
  ymd_valid_ = false;
#ifdef V8_INTL_SUPPORT
  if (!v8_flags.icu_timezone_data) {
#endif
    local_offset_ms_ = kInvalidLocalOffsetInMs;
#ifdef V8_INTL_SUPPORT
  }
#endif
  tz_cache_->Clear(time_zone_detection);
  tz_name_ = nullptr;
  dst_tz_name_ = nullptr;
}

void DateCache::ClearSegment(DST* segment) {
  segment->start_sec = kMaxEpochTimeInSec;
  segment->end_sec = -kMaxEpochTimeInSec;
  segment->offset_ms = 0;
  segment->last_used = 0;
}

void DateCache::YearMonthDayFromDays(int days, int* year, int* month,
                                     int* day) {
  if (ymd_valid_) {
    // Check conservatively if the given 'days' has
    // the same year and month as the cached 'days'.
    int new_day = ymd_day_ + (days - ymd_days_);
    if (new_day >= 1 && new_day <= 28) {
      ymd_day_ = new_day;
      ymd_days_ = days;
      *year = ymd_year_;
      *month = ymd_month_;
      *day = new_day;
      return;
    }
  }
  int save_days = days;

  days += kDaysOffset;
  *year = 400 * (days / kDaysIn400Years) - kYearsOffset;
  days %= kDaysIn400Years;

  DCHECK_EQ(save_days, DaysFromYearMonth(*year, 0) + days);

  days--;
  int yd1 = days / kDaysIn100Years;
  days %= kDaysIn100Years;
  *year += 100 * yd1;

  days++;
  int yd2 = days / kDaysIn4Years;
  days %= kDaysIn4Years;
  *year += 4 * yd2;

  days--;
  int yd3 = days / 365;
  days %= 365;
  *year += yd3;

  bool is_leap = (!yd1 || yd2) && !yd3;

  DCHECK_GE(days, -1);
  DCHECK(is_leap || (days >= 0));
  DCHECK((days < 365) || (is_leap && (days < 366)));
  DCHECK(is_leap == ((*year % 4 == 0) && (*year % 100 || (*year % 400 == 0))));
  DCHECK(is_leap || ((DaysFromYearMonth(*year, 0) + days) == save_days));
  DCHECK(!is_leap || ((DaysFromYearMonth(*year, 0) + days + 1) == save_days));

  days += is_leap;

  // Check if the date is after February.
  if (days >= 31 + 28 + (is_leap ? 1 : 0)) {
    days -= 31 + 28 + (is_leap ? 1 : 0);
    // Find the date starting from March.
    for (int i = 2; i < 12; i++) {
      if (days < kDaysInMonths[i]) {
        *month = i;
        *day = days + 1;
        break;
      }
      days -= kDaysInMonths[i];
    }
  } else {
    // Check January and February.
    if (days < 31) {
      *month = 0;
      *day = days + 1;
    } else {
      *month = 1;
      *day = days - 31 + 1;
    }
  }
  DCHECK(DaysFromYearMonth(*year, *month) + *day - 1 == save_days);
  ymd_valid_ = true;
  ymd_year_ = *year;
  ymd_month_ = *month;
  ymd_day_ = *day;
  ymd_days_ = save_days;
}

int DateCache::DaysFromYearMonth(int year, int month) {
  static const int day_from_month[] = {0,   31,  59,  90,  120, 151,
                                       181, 212, 243, 273, 304, 334};
  static const int day_from_month_leap[] = {0,   31,  60,  91,  121, 152,
                                            182, 213, 244, 274, 305, 335};

  year += month / 12;
  month %= 12;
  if (month < 0) {
    year--;
    month += 12;
  }

  DCHECK_GE(month, 0);
  DCHECK_LT(month, 12);

  // year_delta is an arbitrary number such that:
  // a) year_delta = -1 (mod 400)
  // b) year + year_delta > 0 for years in the range defined by
  //    ECMA 262 - 15.9.1.1, i.e. upto 100,000,000 days on either side of
  //    Jan 1 1970. This is required so that we don't run into integer
  //    division of negative numbers.
  // c) there shouldn't be an overflow for 32-bit integers in the following
  //    operations.
  static const int year_delta = 399999;
  static const int base_day =
      365 * (1970 + year_delta) + (1970 + year_delta) / 4 -
      (1970 + year_delta) / 100 + (1970 + year_delta) / 400;

  int year1 = year + year_delta;
  int day_from_year =
      365 * year1 + year1 / 4 - year1 / 100 + year1 / 400 - base_day;

  if ((year % 4 != 0) || (year % 100 == 0 && year % 400 != 0)) {
    return day_from_year + day_from_month[month];
  }
  return day_from_year + day_from_month_leap[month];
}

void DateCache::BreakDownTime(int64_t time_ms, int* year, int* month, int* day,
                              int* weekday, int* hour, int* min, int* sec,
                              int* ms) {
  int const days = DaysFromTime(time_ms);
  int const time_in_day_ms = TimeInDay(time_ms, days);
  YearMonthDayFromDays(days, year, month, day);
  *weekday = Weekday(days);
  *hour = time_in_day_ms / (60 * 60 * 1000);
  *min = (time_in_day_ms / (60 * 1000)) % 60;
  *sec = (time_in_day_ms / 1000) % 60;
  *ms = time_in_day_ms % 1000;
}

// Implements LocalTimeZonedjustment(t, isUTC)
// ECMA 262 - ES#sec-local-time-zone-adjustment
int DateCache::GetLocalOffsetFromOS(int64_t time_ms, bool is_utc) {
  double offset;
#ifdef V8_INTL_SUPPORT
  if (v8_flags.icu_timezone_data) {
    offset = tz_cache_->LocalTimeOffset(static_cast<double>(time_ms), is_utc);
  } else {
#endif
    // When ICU timezone data is not used, we need to compute the timezone
    // offset for a given local time.
    //
    // The following shows that using DST for (t - LocalTZA - hour) produces
    // correct conversion where LocalTZA is the timezone offset in winter (no
    // DST) and the timezone offset is assumed to have no historical change.
    // Note that it does not work for the past and the future if LocalTZA (no
    // DST) is different from the current LocalTZA (no DST). For instance,
    // this will break for Europe/Moscow in 2012 ~ 2013 because LocalTZA was
    // 4h instead of the current 3h (as of 2018).
    //
    // Consider transition to DST at local time L1.
    // Let L0 = L1 - hour, L2 = L1 + hour,
    //     U1 = UTC time that corresponds to L1,
    //     U0 = U1 - hour.
    // Transitioning to DST moves local clock one hour forward L1 => L2, so
    // U0 = UTC time that corresponds to L0 = L0 - LocalTZA,
    // U1 = UTC time that corresponds to L1 = L1 - LocalTZA,
    // U1 = UTC time that corresponds to L2 = L2 - LocalTZA - hour.
    // Note that DST(U0 - hour) = 0, DST(U0) = 0, DST(U1) = 1.
    // U0 = L0 - LocalTZA - DST(L0 - LocalTZA - hour),
    // U1 = L1 - LocalTZA - DST(L1 - LocalTZA - hour),
    // U1 = L2 - LocalTZA - DST(L2 - LocalTZA - hour).
    //
    // Consider transition from DST at local time L1.
    // Let L0 = L1 - hour,
    //     U1 = UTC time that corresponds to L1,
    //     U0 = U1 - hour, U2 = U1 + hour.
    // Transitioning from DST moves local clock one hour back L1 => L0, so
    // U0 = UTC time that corresponds to L0 (before transition)
    //    = L0 - LocalTZA - hour.
    // U1 = UTC time that corresponds to L0 (after transition)
    //    = L0 - LocalTZA = L1 - LocalTZA - hour
    // U2 = UTC time that corresponds to L1 = L1 - LocalTZA.
    // Note that DST(U0) = 1, DST(U1) = 0, DST(U2) = 0.
    // U0 = L0 - LocalTZA - DST(L0 - LocalTZA - hour) = L0 - LocalTZA - DST(U0).
    // U2 = L1 - LocalTZA - DST(L1 - LocalTZA - hour) = L1 - LocalTZA - DST(U1).
    // It is impossible to get U1 from local time.
    if (local_offset_ms_ == kInvalidLocalOffsetInMs) {
      // This gets the constant LocalTZA (arguments are ignored).
      local_offset_ms_ =
          tz_cache_->LocalTimeOffset(static_cast<double>(time_ms), is_utc);
    }
    offset = local_offset_ms_;
    if (!is_utc) {
      const int kMsPerHour = 3600 * 1000;
      time_ms -= (offset + kMsPerHour);
    }
    offset += DaylightSavingsOffsetInMs(time_ms);
#ifdef V8_INTL_SUPPORT
  }
#endif
  DCHECK_LT(offset, kInvalidLocalOffsetInMs);
  return static_cast<int>(offset);
}

void DateCache::ExtendTheAfterSegment(int time_sec, int offset_ms) {
  if (after_->offset_ms == offset_ms &&
      after_->start_sec - kDefaultDSTDeltaInSec <= time_sec &&
      time_sec <= after_->end_sec) {
    // Extend the after_ segment.
    after_->start_sec = time_sec;
  } else {
    // The after_ segment is either invalid or starts too late.
    if (!InvalidSegment(after_)) {
      // If the after_ segment is valid, replace it with a new segment.
      after_ = LeastRecentlyUsedDST(before_);
    }
    after_->start_sec = time_sec;
    after_->end_sec = time_sec;
    after_->offset_ms = offset_ms;
    after_->last_used = ++dst_usage_counter_;
  }
}

int DateCache::DaylightSavingsOffsetInMs(int64_t time_ms) {
  int time_sec = (time_ms >= 0 && time_ms <= kMaxEpochTimeInMs)
                     ? static_cast<int>(time_ms / 1000)
                     : static_cast<int>(EquivalentTime(time_ms) / 1000);

  // Invalidate cache if the usage counter is close to overflow.
  // Note that dst_usage_counter is incremented less than ten times
  // in this function.
  if (dst_usage_counter_ >= kMaxInt - 10) {
    dst_usage_counter_ = 0;
    for (int i = 0; i < kDSTSize; ++i) {
      ClearSegment(&dst_[i]);
    }
  }

  // Optimistic fast check.
  if (before_->start_sec <= time_sec && time_sec <= before_->end_sec) {
    // Cache hit.
    before_->last_used = ++dst_usage_counter_;
    return before_->offset_ms;
  }

  ProbeDST(time_sec);

  DCHECK(InvalidSegment(before_) || before_->start_sec <= time_sec);
  DCHECK(InvalidSegment(after_) || time_sec < after_->start_sec);

  if (InvalidSegment(before_)) {
    // Cache miss.
    before_->start_sec = time_sec;
    before_->end_sec = time_sec;
    before_->offset_ms = GetDaylightSavingsOffsetFromOS(time_sec);
    before_->last_used = ++dst_usage_counter_;
    return before_->offset_ms;
  }

  if (time_sec <= before_->end_sec) {
    // Cache hit.
    before_->last_used = ++dst_usage_counter_;
    return before_->offset_ms;
  }

  if (time_sec - kDefaultDSTDeltaInSec > before_->end_sec) {
    // If the before_ segment ends too early, then just
    // query for the offset of the time_sec
    int offset_ms = GetDaylightSavingsOffsetFromOS(time_sec);
    ExtendTheAfterSegment(time_sec, offset_ms);
    // This swap helps the optimistic fast check in subsequent invocations.
    DST* temp = before_;
    before_ = after_;
    after_ = temp;
    return offset_ms;
  }

  // Now the time_sec is between
  // before_->end_sec and before_->end_sec + default DST delta.
  // Update the usage counter of before_ since it is going to be used.
  before_->last_used = ++dst_usage_counter_;

  // Check if after_ segment is invalid or starts too late.
  // Note that start_sec of invalid segments is kMaxEpochTimeInSec.
  int new_after_start_sec =
      before_->end_sec < kMaxEpochTimeInSec - kDefaultDSTDeltaInSec
          ? before_->end_sec + kDefaultDSTDeltaInSec
          : kMaxEpochTimeInSec;
  if (new_after_start_sec <= after_->start_sec) {
    int new_offset_ms = GetDaylightSavingsOffsetFromOS(new_after_start_sec);
    ExtendTheAfterSegment(new_after_start_sec, new_offset_ms);
  } else {
    DCHECK(!InvalidSegment(after_));
    // Update the usage counter of after_ since it is going to be used.
    after_->last_used = ++dst_usage_counter_;
  }

  // Now the time_sec is between before_->end_sec and after_->start_sec.
  // Only one daylight savings offset change can occur in this interval.

  if (before_->offset_ms == after_->offset_ms) {
    // Merge two segments if they have the same offset.
    before_->end_sec = after_->end_sec;
    ClearSegment(after_);
    return before_->offset_ms;
  }

  // Binary search for daylight savings offset change point,
  // but give up if we don't find it in five iterations.
  for (int i = 4; i >= 0; --i) {
    int delta = after_->start_sec - before_->end_sec;
    int middle_sec = (i == 0) ? time_sec : before_->end_sec + delta / 2;
    int offset_ms = GetDaylightSavingsOffsetFromOS(middle_sec);
    if (before_->offset_ms == offset_ms) {
      before_->end_sec = middle_sec;
      if (time_sec <= before_->end_sec) {
        return offset_ms;
      }
    } else {
      DCHECK(after_->offset_ms == offset_ms);
      after_->start_sec = middle_sec;
      if (time_sec >= after_->start_sec) {
        // This swap helps the optimistic fast check in subsequent invocations.
        DST* temp = before_;
        before_ = after_;
        after_ = temp;
        return offset_ms;
      }
    }
  }
  return 0;
}

void DateCache::ProbeDST(int time_sec) {
  DST* before = nullptr;
  DST* after = nullptr;
  DCHECK(before_ != after_);

  for (int i = 0; i < kDSTSize; ++i) {
    if (dst_[i].start_sec <= time_sec) {
      if (before == nullptr || before->start_sec < dst_[i].start_sec) {
        before = &dst_[i];
      }
    } else if (time_sec < dst_[i].end_sec) {
      if (after == nullptr || after->end_sec > dst_[i].end_sec) {
        after = &dst_[i];
      }
    }
  }

  // If before or after segments were not found,
  // then set them to any invalid segment.
  if (before == nullptr) {
    before = InvalidSegment(before_) ? before_ : LeastRecentlyUsedDST(after);
  }
  if (after == nullptr) {
    after = InvalidSegment(after_) && before != after_
                ? after_
                : LeastRecentlyUsedDST(before);
  }

  DCHECK_NOT_NULL(before);
  DCHECK_NOT_NULL(after);
  DCHECK(before != after);
  DCHECK(InvalidSegment(before) || before->start_sec <= time_sec);
  DCHECK(InvalidSegment(after) || time_sec < after->start_sec);
  DCHECK(InvalidSegment(before) || InvalidSegment(after) ||
         before->end_sec < after->start_sec);

  before_ = before;
  after_ = after;
}

DateCache::DST* DateCache::LeastRecentlyUsedDST(DST* skip) {
  DST* result = nullptr;
  for (int i = 0; i < kDSTSize; ++i) {
    if (&dst_[i] == skip) continue;
    if (result == nullptr || result->last_used > dst_[i].last_used) {
      result = &dst_[i];
    }
  }
  ClearSegment(result);
  return result;
}

namespace {

// ES6 section 20.3.1.1 Time Values and Time Range
const double kMinYear = -1000000.0;
const double kMaxYear = -kMinYear;
const double kMinMonth = -10000000.0;
const double kMaxMonth = -kMinMonth;

const double kMsPerDay = 86400000.0;

const double kMsPerSecond = 1000.0;
const double kMsPerMinute = 60000.0;
const double kMsPerHour = 3600000.0;

}  // namespace

double MakeDate(double day, double time) {
  if (std::isfinite(day) && std::isfinite(time)) {
    return time + day * kMsPerDay;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

double MakeDay(double year, double month, double date) {
  if ((kMinYear <= year && year <= kMaxYear) &&
      (kMinMonth <= month && month <= kMaxMonth) && std::isfinite(date)) {
    int y = FastD2I(year);
    int m = FastD2I(month);
    y += m / 12;
    m %= 12;
    if (m < 0) {
      m += 12;
      y -= 1;
    }
    DCHECK_LE(0, m);
    DCHECK_LT(m, 12);

    // kYearDelta is an arbitrary number such that:
    // a) kYearDelta = -1 (mod 400)
    // b) year + kYearDelta > 0 for years in the range defined by
    //    ECMA 262 - 15.9.1.1, i.e. upto 100,000,000 days on either side of
    //    Jan 1 1970. This is required so that we don't run into integer
    //    division of negative numbers.
    // c) there shouldn't be an overflow for 32-bit integers in the following
    //    operations.
    static const int kYearDelta = 399999;
    static const int kBaseDay =
        365 * (1970 + kYearDelta) + (1970 + kYearDelta) / 4 -
        (1970 + kYearDelta) / 100 + (1970 + kYearDelta) / 400;
    int day_from_year = 365 * (y + kYearDelta) + (y + kYearDelta) / 4 -
                        (y + kYearDelta) / 100 + (y + kYearDelta) / 400 -
                        kBaseDay;
    if ((y % 4 != 0) || (y % 100 == 0 && y % 400 != 0)) {
      static const int kDayFromMonth[] = {0,   31,  59,  90,  120, 151,
                                          181, 212, 243, 273, 304, 334};
      day_from_year += kDayFromMonth[m];
    } else {
      static const int kDayFromMonth[] = {0,   31,  60,  91,  121, 152,
                                          182, 213, 244, 274, 305, 335};
      day_from_year += kDayFromMonth[m];
    }
    return static_cast<double>(day_from_year - 1) + DoubleToInteger(date);
  }
  return std::numeric_limits<double>::quiet_NaN();
}

double MakeTime(double hour, double min, double sec, double ms) {
  if (std::isfinite(hour) && std::isfinite(min) && std::isfinite(sec) &&
      std::isfinite(ms)) {
    double const h = DoubleToInteger(hour);
    double const m = DoubleToInteger(min);
    double const s = DoubleToInteger(sec);
    double const milli = DoubleToInteger(ms);
    return h * kMsPerHour + m * kMsPerMinute + s * kMsPerSecond + milli;
  }
  return std::numeric_limits<double>::quiet_NaN();
}

namespace {

const char* kShortWeekDays[] = {"Sun", "Mon", "Tue", "Wed",
                                "Thu", "Fri", "Sat"};
const char* kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

template <class... Args>
DateBuffer FormatDate(const char* format, Args... args) {
  DateBuffer buffer;
  SmallStringOptimizedAllocator<DateBuffer::kInlineSize> allocator(&buffer);
  StringStream sstream(&allocator);
  sstream.Add(format, args...);
  buffer.resize_no_init(sstream.length());
  return buffer;
}

}  // namespace

DateBuffer ToDateString(double time_val, DateCache* date_cache,
                        ToDateStringMode mode) {
  if (std::isnan(time_val)) {
    return FormatDate("Invalid Date");
  }
  int64_t time_ms = static_cast<int64_t>(time_val);
  int64_t local_time_ms = (mode == ToDateStringMode::kUTCDateAndTime ||
                           mode == ToDateStringMode::kISODateAndTime)
                              ? time_ms
                              : date_cache->ToLocal(time_ms);
  int year, month, day, weekday, hour, min, sec, ms;
  date_cache->BreakDownTime(local_time_ms, &year, &month, &day, &weekday, &hour,
                            &min, &sec, &ms);
  int timezone_offset = -date_cache->TimezoneOffset(time_ms);
  int timezone_hour = std::abs(timezone_offset) / 60;
  int timezone_min = std::abs(timezone_offset) % 60;
  const char* local_timezone = date_cache->LocalTimezone(time_ms);
  switch (mode) {
    case ToDateStringMode::kLocalDate:
      return FormatDate((year < 0) ? "%s %s %02d %05d" : "%s %s %02d %04d",
                        kShortWeekDays[weekday], kShortMonths[month], day,
                        year);
    case ToDateStringMode::kLocalTime:
      return FormatDate("%02d:%02d:%02d GMT%c%02d%02d (%s)", hour, min, sec,
                        (timezone_offset < 0) ? '-' : '+', timezone_hour,
                        timezone_min, local_timezone);
    case ToDateStringMode::kLocalDateAndTime:
      return FormatDate(
          (year < 0) ? "%s %s %02d %05d %02d:%02d:%02d GMT%c%02d%02d (%s)"
                     : "%s %s %02d %04d %02d:%02d:%02d GMT%c%02d%02d (%s)",
          kShortWeekDays[weekday], kShortMonths[month], day, year, hour, min,
          sec, (timezone_offset < 0) ? '-' : '+', timezone_hour, timezone_min,
          local_timezone);
    case ToDateStringMode::kUTCDateAndTime:
      return FormatDate((year < 0) ? "%s, %02d %s %05d %02d:%02d:%02d GMT"
                                   : "%s, %02d %s %04d %02d:%02d:%02d GMT",
                        kShortWeekDays[weekday], day, kShortMonths[month], year,
                        hour, min, sec);
    case ToDateStringMode::kISODateAndTime:
      if (year >= 0 && year <= 9999) {
        return FormatDate("%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year,
                          month + 1, day, hour, min, sec, ms);
      } else if (year < 0) {
        return FormatDate("-%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", -year,
                          month + 1, day, hour, min, sec, ms);
      } else {
        return FormatDate("+%06d-%02d-%02dT%02d:%02d:%02d.%03dZ", year,
                          month + 1, day, hour, min, sec, ms);
      }
  }
  UNREACHABLE();
}

// ES6 section 20.3.1.16 Date Time String Format
double ParseDateTimeString(Isolate* isolate, Handle<String> str) {
  str = String::Flatten(isolate, str);
  double out[DateParser::OUTPUT_SIZE];
  DisallowGarbageCollection no_gc;
  String::FlatContent str_content = str->GetFlatContent(no_gc);
  bool result;
  if (str_content.IsOneByte()) {
    result = DateParser::Parse(isolate, str_content.ToOneByteVector(), out);
  } else {
    result = DateParser::Parse(isolate, str_content.ToUC16Vector(), out);
  }
  if (!result) return std::numeric_limits<double>::quiet_NaN();
  double const day = MakeDay(out[DateParser::YEAR], out[DateParser::MONTH],
                             out[DateParser::DAY]);
  double const time =
      MakeTime(out[DateParser::HOUR], out[DateParser::MINUTE],
               out[DateParser::SECOND], out[DateParser::MILLISECOND]);
  double date = MakeDate(day, time);
  if (std::isnan(out[DateParser::UTC_OFFSET])) {
    if (date >= -DateCache::kMaxTimeBeforeUTCInMs &&
        date <= DateCache::kMaxTimeBeforeUTCInMs) {
      date = isolate->date_cache()->ToUTC(static_cast<int64_t>(date));
    } else {
      return std::numeric_limits<double>::quiet_NaN();
    }
  } else {
    date -= out[DateParser::UTC_OFFSET] * 1000.0;
  }
  if (!DateCache::TryTimeClip(&date)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return date;
}

}  // namespace internal
}  // namespace v8
