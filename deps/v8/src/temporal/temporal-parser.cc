// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/temporal/temporal-parser.h"

#include <optional>

#include "src/base/bounds.h"
#include "src/objects/string-inl.h"
#include "src/strings/char-predicates-inl.h"

namespace v8::internal {

namespace {

// Temporal #prod-TZLeadingChar
inline constexpr bool IsTZLeadingChar(base::uc32 c) {
  return base::IsInRange(AsciiAlphaToLower(c), 'a', 'z') || c == '.' ||
         c == '_';
}

// Temporal #prod-TZChar
inline constexpr bool IsTZChar(base::uc32 c) {
  return IsTZLeadingChar(c) || c == '-';
}

// Temporal #prod-DecimalSeparator
inline constexpr bool IsDecimalSeparator(base::uc32 c) {
  return c == '.' || c == ',';
}

// Temporal #prod-DateTimeSeparator
inline constexpr bool IsDateTimeSeparator(base::uc32 c) {
  return c == ' ' || AsciiAlphaToLower(c) == 't';
}

// Temporal #prod-ASCIISign
inline constexpr bool IsAsciiSign(base::uc32 c) { return c == '-' || c == '+'; }

// Temporal #prod-Sign
inline constexpr bool IsSign(base::uc32 c) {
  return c == 0x2212 || IsAsciiSign(c);
}

// Temporal #prod-TimeZoneUTCOffsetSign
inline constexpr bool IsTimeZoneUTCOffsetSign(base::uc32 c) {
  return IsSign(c);
}

inline constexpr base::uc32 CanonicalSign(base::uc32 c) {
  return c == 0x2212 ? '-' : c;
}

inline constexpr int32_t ToInt(base::uc32 c) { return c - '0'; }

// A helper template to make the scanning of production w/ two digits simpler.
template <typename Char>
bool HasTwoDigits(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() >= (s + 2) && IsDecimalDigit(str[s]) &&
      IsDecimalDigit(str[s + 1])) {
    *out = ToInt(str[s]) * 10 + ToInt(str[s + 1]);
    return true;
  }
  return false;
}

// A helper template to make the scanning of production w/ a single two digits
// value simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectValue(base::Vector<Char> str, int32_t s,
                                 int32_t expected, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) && (*out == expected) ? 2 : 0;
}

// A helper template to make the scanning of production w/ two digits value in a
// range simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectRange(base::Vector<Char> str, int32_t s, int32_t min,
                                 int32_t max, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) && base::IsInRange(*out, min, max) ? 2
                                                                            : 0;
}

// A helper template to make the scanning of production w/ two digits value as 0
// or in a range simpler.
template <typename Char>
int32_t ScanTwoDigitsExpectZeroOrRange(base::Vector<Char> str, int32_t s,
                                       int32_t min, int32_t max, int32_t* out) {
  return HasTwoDigits<Char>(str, s, out) &&
                 (*out == 0 || base::IsInRange(*out, min, max))
             ? 2
             : 0;
}

/**
 * The TemporalParser use two types of internal routine:
 * - Scan routines: Follow the function signature below:
 *   template <typename Char> int32_t Scan$ProductionName(
 *   base::Vector<Char> str, int32_t s, R* out)
 *
 *   These routine scan the next item from position s in str and store the
 *   parsed result into out if the expected string is successfully scanned.
 *   It return the length of matched text from s or 0 to indicate no
 *   expected item matched.
 *
 * - Satisfy routines: Follow the function sigature below:
 *   template <typename Char>
 *   bool Satisfy$ProductionName(base::Vector<Char> str, R* r);
 *   It scan from the beginning of the str by calling Scan routines to put
 *   parsed result into r and return true if the entire str satisfy the
 *   production. It internally use Scan routines.
 *
 * TODO(ftang) investigate refactoring to class before shipping
 * Reference to RegExpParserImpl by encapsulating the cursor position and
 * only manipulating the current character and position with Next(),
 * Advance(), current(), etc
 */

// For Hour Production
// Hour:
//   [0 1] Digit
//   2 [0 1 2 3]
template <typename Char>
int32_t ScanHour(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 23, out);
}

// UnpaddedHour :
//   DecimalDigit
//   1 DecimalDigit
//   20
//   21
//   22
//   23
template <typename Char>
int32_t ScanUnpaddedHour(base::Vector<Char> str, int32_t s) {
  int32_t dummy;
  int32_t len = ScanTwoDigitsExpectRange<Char>(str, s, 10, 23, &dummy);
  if (len > 0) return len;
  if (str.length() >= (s + 1) && IsDecimalDigit(str[s])) return 1;
  return 0;
}

// MinuteSecond:
//   [0 1 2 3 4 5] Digit
template <typename Char>
int32_t ScanMinuteSecond(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 59, out);
}

// For the forward production in the grammar such as
// ProductionB:
//   ProductionT
#define SCAN_FORWARD(B, T, R)                                \
  template <typename Char>                                   \
  int32_t Scan##B(base::Vector<Char> str, int32_t s, R* r) { \
    return Scan##T(str, s, r);                               \
  }

// Same as above but store the result into a particular field in R

// For the forward production in the grammar such as
// ProductionB:
//   ProductionT1
//   ProductionT2
#define SCAN_EITHER_FORWARD(B, T1, T2, R)                    \
  template <typename Char>                                   \
  int32_t Scan##B(base::Vector<Char> str, int32_t s, R* r) { \
    int32_t len;                                             \
    if ((len = Scan##T1(str, s, r)) > 0) return len;         \
    return Scan##T2(str, s, r);                              \
  }

// TimeHour: Hour
SCAN_FORWARD(TimeHour, Hour, int32_t)

// TimeMinute: MinuteSecond
SCAN_FORWARD(TimeMinute, MinuteSecond, int32_t)

// TimeSecond:
//   MinuteSecond
//   60
template <typename Char>
int32_t ScanTimeSecond(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 0, 60, out);
}

constexpr int kPowerOfTen[] = {1,      10,      100,      1000,     10000,
                               100000, 1000000, 10000000, 100000000};

// FractionalPart : Digit{1,9}
template <typename Char>
int32_t ScanFractionalPart(base::Vector<Char> str, int32_t s, int32_t* out) {
  int32_t cur = s;
  if ((str.length() < (cur + 1)) || !IsDecimalDigit(str[cur])) return 0;
  *out = ToInt(str[cur++]);
  while ((cur < str.length()) && ((cur - s) < 9) && IsDecimalDigit(str[cur])) {
    *out = 10 * (*out) + ToInt(str[cur++]);
  }
  *out *= kPowerOfTen[9 - (cur - s)];
  return cur - s;
}

// TimeFraction: FractionalPart
SCAN_FORWARD(TimeFractionalPart, FractionalPart, int32_t)

// Fraction: DecimalSeparator FractionalPart
// DecimalSeparator: one of , .
template <typename Char>
int32_t ScanFraction(base::Vector<Char> str, int32_t s, int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanFractionalPart(str, s + 1, out)) == 0) return 0;
  return len + 1;
}

// TimeFraction: DecimalSeparator TimeFractionalPart
// DecimalSeparator: one of , .
template <typename Char>
int32_t ScanTimeFraction(base::Vector<Char> str, int32_t s, int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanTimeFractionalPart(str, s + 1, out)) == 0) return 0;
  return len + 1;
}

template <typename Char>
int32_t ScanTimeFraction(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Result* r) {
  return ScanTimeFraction(str, s, &(r->time_nanosecond));
}

// TimeSpec:
//  TimeHour
//  TimeHour : TimeMinute
//  TimeHour : TimeMinute : TimeSecond [TimeFraction]
//  TimeHour TimeMinute
//  TimeHour TimeMinute TimeSecond [TimeFraction]
template <typename Char>
int32_t ScanTimeSpec(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  int32_t time_hour, time_minute, time_second;
  int32_t len;
  int32_t cur = s;
  if ((len = ScanTimeHour(str, cur, &time_hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    // TimeHour
    r->time_hour = time_hour;
    return cur - s;
  }
  if (str[cur] == ':') {
    cur++;
    if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || (str[cur] != ':')) {
      // TimeHour : TimeMinute
      r->time_hour = time_hour;
      r->time_minute = time_minute;
      return cur - s;
    }
    cur++;
    if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) return 0;
  } else {
    if ((len = ScanTimeMinute(str, cur, &time_minute)) == 0) {
      // TimeHour
      r->time_hour = time_hour;
      return cur - s;
    }
    cur += len;
    if ((len = ScanTimeSecond(str, cur, &time_second)) == 0) {
      // TimeHour TimeMinute
      r->time_hour = time_hour;
      r->time_minute = time_minute;
      return cur - s;
    }
  }
  cur += len;
  len = ScanTimeFraction(str, cur, r);
  r->time_hour = time_hour;
  r->time_minute = time_minute;
  r->time_second = time_second;
  cur += len;
  return cur - s;
}

// TimeSpecSeparator: DateTimeSeparator TimeSpec
// DateTimeSeparator: SPACE, 't', or 'T'
template <typename Char>
int32_t ScanTimeSpecSeparator(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  if (!(((s + 1) < str.length()) && IsDateTimeSeparator(str[s]))) return 0;
  int32_t len = ScanTimeSpec(str, s + 1, r);
  return (len == 0) ? 0 : len + 1;
}

// DateExtendedYear: Sign Digit Digit Digit Digit Digit Digit
template <typename Char>
int32_t ScanDateExtendedYear(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() < (s + 7)) return 0;
  if (IsSign(str[s]) && IsDecimalDigit(str[s + 1]) &&
      IsDecimalDigit(str[s + 2]) && IsDecimalDigit(str[s + 3]) &&
      IsDecimalDigit(str[s + 4]) && IsDecimalDigit(str[s + 5]) &&
      IsDecimalDigit(str[s + 6])) {
    int32_t sign = (CanonicalSign(str[s]) == '-') ? -1 : 1;
    *out = sign * (ToInt(str[s + 1]) * 100000 + ToInt(str[s + 2]) * 10000 +
                   ToInt(str[s + 3]) * 1000 + ToInt(str[s + 4]) * 100 +
                   ToInt(str[s + 5]) * 10 + ToInt(str[s + 6]));
    // In the end of #sec-temporal-iso8601grammar
    // It is a Syntax Error if DateExtendedYear is "-000000" or "−000000"
    // (U+2212 MINUS SIGN followed by 000000).
    if (sign == -1 && *out == 0) return 0;
    return 7;
  }
  return 0;
}

// DateFourDigitYear: Digit Digit Digit Digit
template <typename Char>
int32_t ScanDateFourDigitYear(base::Vector<Char> str, int32_t s, int32_t* out) {
  if (str.length() < (s + 4)) return 0;
  if (IsDecimalDigit(str[s]) && IsDecimalDigit(str[s + 1]) &&
      IsDecimalDigit(str[s + 2]) && IsDecimalDigit(str[s + 3])) {
    *out = ToInt(str[s]) * 1000 + ToInt(str[s + 1]) * 100 +
           ToInt(str[s + 2]) * 10 + ToInt(str[s + 3]);
    return 4;
  }
  return 0;
}

// DateYear:
//   DateFourDigitYear
//   DateExtendedYear
// The lookahead is at most 1 char.
SCAN_EITHER_FORWARD(DateYear, DateFourDigitYear, DateExtendedYear, int32_t)

// DateMonth:
//   0 NonzeroDigit
//   10
//   11
//   12
template <typename Char>
int32_t ScanDateMonth(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 1, 12, out);
}

// DateDay:
//   0 NonzeroDigit
//   1 Digit
//   2 Digit
//   30
//   31
template <typename Char>
int32_t ScanDateDay(base::Vector<Char> str, int32_t s, int32_t* out) {
  return ScanTwoDigitsExpectRange<Char>(str, s, 1, 31, out);
}

// Date:
//   DateYear - DateMonth - DateDay
//   DateYear DateMonth DateDay
template <typename Char>
int32_t ScanDate(base::Vector<Char> str, int32_t s, ParsedISO8601Result* r) {
  int32_t date_year, date_month, date_day;
  int32_t cur = s;
  int32_t len;
  if ((len = ScanDateYear(str, cur, &date_year)) == 0) return 0;
  if (((cur += len) + 1) > str.length()) return 0;
  if (str[cur] == '-') {
    cur++;
    if ((len = ScanDateMonth(str, cur, &date_month)) == 0) return 0;
    cur += len;
    if (((cur + 1) > str.length()) || (str[cur++] != '-')) return 0;
  } else {
    if ((len = ScanDateMonth(str, cur, &date_month)) == 0) return 0;
    cur += len;
  }
  if ((len = ScanDateDay(str, cur, &date_day)) == 0) return 0;
  r->date_year = date_year;
  r->date_month = date_month;
  r->date_day = date_day;
  return cur + len - s;
}

// DateMonthWithThirtyOneDays : one of
//    01 03 05 07 08 10 12
template <typename Char>
int32_t ScanDateMonthWithThirtyOneDays(base::Vector<Char> str, int32_t s) {
  int32_t value;
  if (!HasTwoDigits(str, s, &value)) return false;
  return value == 1 || value == 3 || value == 5 || value == 7 || value == 8 ||
         value == 10 || value == 12;
}

// TimeZoneUTCOffsetHour: Hour
SCAN_FORWARD(TimeZoneUTCOffsetHour, Hour, int32_t)

// TimeZoneUTCOffsetMinute
SCAN_FORWARD(TimeZoneUTCOffsetMinute, MinuteSecond, int32_t)

// TimeZoneUTCOffsetSecond
SCAN_FORWARD(TimeZoneUTCOffsetSecond, MinuteSecond, int32_t)

// TimeZoneUTCOffsetFractionalPart: FractionalPart
// See PR1796
SCAN_FORWARD(TimeZoneUTCOffsetFractionalPart, FractionalPart, int32_t)

// TimeZoneUTCOffsetFraction: DecimalSeparator TimeZoneUTCOffsetFractionalPart
// See PR1796
template <typename Char>
int32_t ScanTimeZoneUTCOffsetFraction(base::Vector<Char> str, int32_t s,
                                      int32_t* out) {
  if ((str.length() < (s + 2)) || (!IsDecimalSeparator(str[s]))) return 0;
  int32_t len;
  if ((len = ScanTimeZoneUTCOffsetFractionalPart(str, s + 1, out)) > 0) {
    return len + 1;
  }
  return 0;
}

// TimeZoneNumericUTCOffset:
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
//   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute :
//   TimeZoneUTCOffsetSecond [TimeZoneUTCOffsetFraction] TimeZoneUTCOffsetSign
//   TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute TimeZoneUTCOffsetSecond
//   [TimeZoneUTCOffsetFraction]

template <typename Char>
int32_t ScanTimeZoneNumericUTCOffset(base::Vector<Char> str, int32_t s,
                                     ParsedISO8601Result* r) {
  int32_t len, hour, minute, second, nanosecond;
  int32_t cur = s;
  if ((str.length() < (cur + 1)) || (!IsTimeZoneUTCOffsetSign(str[cur]))) {
    return 0;
  }
  int32_t sign = (CanonicalSign(str[cur++]) == '-') ? -1 : 1;
  if ((len = ScanTimeZoneUTCOffsetHour(str, cur, &hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
    r->tzuo_sign = sign;
    r->tzuo_hour = hour;
    r->offset_string_start = s;
    r->offset_string_length = cur - s;
    return cur - s;
  }
  if (str[cur] == ':') {
    cur++;
    if ((len = ScanTimeZoneUTCOffsetMinute(str, cur, &minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || str[cur] != ':') {
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour : TimeZoneUTCOffsetMinute
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->tzuo_minute = minute;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
    cur++;
    if ((len = ScanTimeZoneUTCOffsetSecond(str, cur, &second)) == 0) return 0;
  } else {
    if ((len = ScanTimeZoneUTCOffsetMinute(str, cur, &minute)) == 0) {
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
    cur += len;
    if ((len = ScanTimeZoneUTCOffsetSecond(str, cur, &second)) == 0) {
      //   TimeZoneUTCOffsetSign TimeZoneUTCOffsetHour TimeZoneUTCOffsetMinute
      r->tzuo_sign = sign;
      r->tzuo_hour = hour;
      r->tzuo_minute = minute;
      r->offset_string_start = s;
      r->offset_string_length = cur - s;
      return cur - s;
    }
  }
  cur += len;
  len = ScanTimeZoneUTCOffsetFraction(str, cur, &nanosecond);
  r->tzuo_sign = sign;
  r->tzuo_hour = hour;
  r->tzuo_minute = minute;
  r->tzuo_second = second;
  if (len > 0) r->tzuo_nanosecond = nanosecond;
  r->offset_string_start = s;
  r->offset_string_length = cur + len - s;
  cur += len;
  return cur - s;
}

// TimeZoneUTCOffset:
//   TimeZoneNumericUTCOffset
//   UTCDesignator
template <typename Char>
int32_t ScanTimeZoneUTCOffset(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  if (str.length() < (s + 1)) return 0;
  if (AsciiAlphaToLower(str[s]) == 'z') {
    // UTCDesignator
    r->utc_designator = true;
    return 1;
  }
  // TimeZoneNumericUTCOffset
  return ScanTimeZoneNumericUTCOffset(str, s, r);
}

// TimeZoneIANANameComponent :
//   TZLeadingChar TZChar{0,13} but not one of . or ..
template <typename Char>
int32_t ScanTimeZoneIANANameComponent(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  if (str.length() < (cur + 1) || !IsTZLeadingChar(str[cur++])) return 0;
  while (((cur) < str.length()) && ((cur - s) < 14) && IsTZChar(str[cur])) {
    cur++;
  }
  if ((cur - s) == 1 && str[s] == '.') return 0;
  if ((cur - s) == 2 && str[s] == '.' && str[s + 1] == '.') return 0;
  return cur - s;
}
// TimeZoneIANALegacyName :
//   Etc/GMT0
//   GMT0
//   GMT-0
//   GMT+0
//   EST5EDT
//   CST6CDT
//   MST7MDT
//   PST8PDT

template <typename Char>
int32_t ScanTimeZoneIANALegacyName(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  {
    constexpr int32_t len = 4;
    if (str.length() < cur + len) return 0;
    if (CompareCharsEqual(str.begin() + cur, "GMT0", len)) return len;
  }

  {
    constexpr int32_t len = 5;
    if (str.length() < cur + len) return 0;
    if (CompareCharsEqual(str.begin() + cur, "GMT+0", len) ||
        CompareCharsEqual(str.begin() + cur, "GMT-0", len)) {
      return len;
    }
  }

  {
    constexpr int32_t len = 7;
    if (str.length() < cur + len) return 0;
    if (CompareCharsEqual(str.begin() + cur, "EST5EDT", len) ||
        CompareCharsEqual(str.begin() + cur, "CST6CDT", len) ||
        CompareCharsEqual(str.begin() + cur, "MST7MDT", len) ||
        CompareCharsEqual(str.begin() + cur, "PST8PDT", len)) {
      return len;
    }
  }

  {
    constexpr int32_t len = 8;
    if (str.length() < cur + len) return 0;
    if (CompareCharsEqual(str.begin() + cur, "Etc/GMT0", len)) return len;
  }

  return 0;
}

// Etc/GMT ASCIISign UnpaddedHour
template <typename Char>
int32_t ScanEtcGMTASCIISignUnpaddedHour(base::Vector<Char> str, int32_t s) {
  if ((s + 9) > str.length()) return 0;
  int32_t cur = s;
  int32_t len = arraysize("Etc/GMT") - 1;
  if (!CompareCharsEqual(str.begin() + cur, "Etc/GMT", len)) return 0;
  cur += len;
  Char sign = str[cur++];
  if (!IsAsciiSign(sign)) return 0;
  len = ScanUnpaddedHour(str, cur);
  if (len == 0) return 0;
  cur += len;
  return cur - s;
}

// TimeZoneIANANameTail :
//   TimeZoneIANANameComponent
//   TimeZoneIANANameComponent / TimeZoneIANANameTail
// TimeZoneIANAName :
//   Etc/GMT ASCIISign UnpaddedHour
//   TimeZoneIANANameTail
//   TimeZoneIANALegacyName
// The spec text use tail recusion with TimeZoneIANANameComponent and
// TimeZoneIANANameTail. In our implementation, we use an iteration loop
// instead.
template <typename Char>
int32_t ScanTimeZoneIANAName(base::Vector<Char> str, int32_t s) {
  int32_t len;
  if ((len = ScanEtcGMTASCIISignUnpaddedHour(str, s)) > 0 ||
      (len = ScanTimeZoneIANALegacyName(str, s)) > 0) {
    return len;
  }
  int32_t cur = s;
  if ((len = ScanTimeZoneIANANameComponent(str, cur)) == 0) return 0;
  cur += len;
  while ((str.length() > (cur + 1)) && (str[cur] == '/')) {
    cur++;
    if ((len = ScanTimeZoneIANANameComponent(str, cur)) == 0) {
      return 0;
    }
    // TimeZoneIANANameComponent / TimeZoneIANAName
    cur += len;
  }
  return cur - s;
}

// TimeZoneUTCOffsetName
//   Sign Hour
//   Sign Hour : MinuteSecond
//   Sign Hour MinuteSecond
//   Sign Hour : MinuteSecond : MinuteSecond [Fraction]
//   Sign Hour MinuteSecond MinuteSecond [Fraction]
//
template <typename Char>
int32_t ScanTimeZoneUTCOffsetName(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  int32_t len;
  if ((str.length() < (s + 3)) || !IsSign(str[cur++])) return 0;
  int32_t hour, minute, second, fraction;
  if ((len = ScanHour(str, cur, &hour)) == 0) return 0;
  cur += len;
  if ((cur + 1) > str.length()) {
    // Sign Hour
    return cur - s;
  }
  if (str[cur] == ':') {
    // Sign Hour :
    cur++;
    if ((len = ScanMinuteSecond(str, cur, &minute)) == 0) return 0;
    cur += len;
    if ((cur + 1) > str.length() || (str[cur] != ':')) {
      // Sign Hour : MinuteSecond
      return cur - s;
    }
    cur++;
    // Sign Hour : MinuteSecond :
    if ((len = ScanMinuteSecond(str, cur, &second)) == 0) return 0;
    cur += len;
    len = ScanFraction(str, cur, &fraction);
    return cur + len - s;
  } else {
    if ((len = ScanMinuteSecond(str, cur, &minute)) == 0) {
      // Sign Hour
      return cur - s;
    }
    cur += len;
    if ((len = ScanMinuteSecond(str, cur, &second)) == 0) {
      // Sign Hour MinuteSecond
      return cur - s;
    }
    cur += len;
    len = ScanFraction(str, cur, &fraction);
    //  Sign Hour MinuteSecond MinuteSecond [Fraction]
    cur += len;
    return cur - s;
  }
}

// TimeZoneBracketedName
//   TimeZoneIANAName
//   "Etc/GMT" ASCIISign Hour
//   TimeZoneUTCOffsetName
// Since "Etc/GMT" also fit TimeZoneIANAName so we need to try
// "Etc/GMT" ASCIISign Hour first.
template <typename Char>
int32_t ScanEtcGMTAsciiSignHour(base::Vector<Char> str, int32_t s) {
  if ((s + 10) > str.length()) return 0;
  int32_t cur = s;
  if ((str[cur++] != 'E') || (str[cur++] != 't') || (str[cur++] != 'c') ||
      (str[cur++] != '/') || (str[cur++] != 'G') || (str[cur++] != 'M') ||
      (str[cur++] != 'T')) {
    return 0;
  }
  Char sign = str[cur++];
  if (!IsAsciiSign(sign)) return 0;
  int32_t hour;
  int32_t len = ScanHour(str, cur, &hour);
  if (len == 0) return 0;
  //   "Etc/GMT" ASCIISign Hour
  return 10;
}

template <typename Char>
int32_t ScanTimeZoneIdentifier(base::Vector<Char> str, int32_t s,
                               ParsedISO8601Result* r);
// TimeZoneBracketedAnnotation :
// [ TimeZoneIdentifier ]
template <typename Char>
int32_t ScanTimeZoneBracketedAnnotation(base::Vector<Char> str, int32_t s,
                                        ParsedISO8601Result* r) {
  if ((str.length() < (s + 3)) || (str[s] != '[')) return 0;
  int32_t cur = s + 1;
  int32_t len = ScanTimeZoneIdentifier(str, cur, r);
  cur += len;
  if (len == 0 || str.length() < (cur + 1) || (str[cur] != ']')) {
    // Only ScanTimeZoneBracketedAnnotation know the post condition of
    // TimeZoneIdentifier is not matched so we need to reset here.
    r->tzi_name_start = 0;
    r->tzi_name_length = 0;
    return 0;
  }
  cur++;
  return cur - s;
}

// TimeZoneOffsetRequired:
//   TimeZoneUTCOffset [TimeZoneBracketedAnnotation]
template <typename Char>
int32_t ScanTimeZoneOffsetRequired(base::Vector<Char> str, int32_t s,
                                   ParsedISO8601Result* r) {
  int32_t cur = s;
  cur += ScanTimeZoneUTCOffset(str, cur, r);
  if (cur == s) return 0;
  cur += ScanTimeZoneBracketedAnnotation(str, cur, r);
  return cur - s;
}

//   TimeZoneNameRequired:
//   [TimeZoneUTCOffset] TimeZoneBracketedAnnotation
template <typename Char>
int32_t ScanTimeZoneNameRequired(base::Vector<Char> str, int32_t s,
                                 ParsedISO8601Result* r) {
  int32_t cur = s;
  cur += ScanTimeZoneUTCOffset(str, cur, r);
  int32_t len = ScanTimeZoneBracketedAnnotation(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  return cur - s;
}

// TimeZone:
//   TimeZoneUTCOffset [TimeZoneBracketedAnnotation]
//   TimeZoneBracketedAnnotation
template <typename Char>
int32_t ScanTimeZone(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len;
  // TimeZoneUTCOffset [TimeZoneBracketedAnnotation]
  if ((len = ScanTimeZoneUTCOffset(str, cur, r)) > 0) {
    cur += len;
    // [TimeZoneBracketedAnnotation]
    len = ScanTimeZoneBracketedAnnotation(str, cur, r);
    cur += len;
    return cur - s;
  }
  // TimeZoneBracketedAnnotation
  return ScanTimeZoneBracketedAnnotation(str, cur, r);
}

// ValidMonthDay :
//   DateMonth [-] 0 NonZeroDigit
//   DateMonth [-] 1 DecimalDigit
//   DateMonth [-] 2 DecimalDigit
//   DateMonth [-] 30 but not one of 0230 or 02-30
//   DateMonthWithThirtyOneDays [-] 31
template <typename Char>
int32_t ScanValidMonthDay(base::Vector<Char> str, int32_t s) {
  int32_t len;
  int32_t cur = s;
  int32_t date_month;
  if ((len = ScanDateMonth(str, cur, &date_month)) > 0) {
    cur += len;
    if (str.length() >= (cur + 1)) {
      if (str[cur] == '-') cur++;
      int32_t day_of_month;
      if ((len = ScanTwoDigitsExpectRange(str, cur, 1, 30, &day_of_month)) >
          0) {
        cur += len;
        // 0 NonZeroDigit
        // 1 DecimalDigit
        // 2 DecimalDigit
        // 30 but not one of 0230 or 02-30
        if (date_month != 2 || day_of_month != 30) {
          return cur - s;
        }
      }
    }
  }
  // Reset cur
  cur = s;
  //   DateMonthWithThirtyOneDays [-] 31
  if ((len = ScanDateMonthWithThirtyOneDays(str, cur)) > 0) {
    cur += len;
    if (str.length() >= (cur + 1)) {
      if (str[cur] == '-') cur++;
      int32_t dummy;
      if ((len = ScanTwoDigitsExpectValue(str, cur, 31, &dummy)) > 0) {
        cur += len;
        return cur - s;
      }
    }
  }
  return 0;
}

template <typename Char>
int32_t ScanDateSpecYearMonth(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r);

// TimeSpecWithOptionalTimeZoneNotAmbiguous :
//   TimeSpec [TimeZone] but not one of ValidMonthDay or DateSpecYearMonth
template <typename Char>
int32_t ScanTimeSpecWithOptionalTimeZoneNotAmbiguous(base::Vector<Char> str,
                                                     int32_t s,
                                                     ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len;
  if ((len = ScanTimeSpec(str, cur, r)) == 0) return 0;
  cur += len;
  // [TimeZone]
  len = ScanTimeZone(str, cur, r);
  cur += len;
  len = cur - s;
  // If it match ValidMonthDay, consider invalid.
  if (ScanValidMonthDay(str, s) == len) return 0;
  // If it match DateSpecYearMonth, consider invalid.
  ParsedISO8601Result tmp;
  if (ScanDateSpecYearMonth(str, s, &tmp) == len) return 0;
  return len;
}

// CalendarNameComponent:
//   CalChar {3,8}
template <typename Char>
int32_t ScanCalendarNameComponent(base::Vector<Char> str, int32_t s) {
  int32_t cur = s;
  while ((cur < str.length()) && IsAlphaNumeric(str[cur])) cur++;
  if ((cur - s) < 3 || (cur - s) > 8) return 0;
  return cur - s;
}

// CalendarNameTail :
//   CalendarNameComponent
//   CalendarNameComponent - CalendarNameTail
// CalendarName :
//   CalendarNameTail
// The spec text use tail recusion with CalendarNameComponent and
// CalendarNameTail. In our implementation, we use an iteration loop instead.
template <typename Char>
int32_t ScanCalendarName(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len;
  if ((len = ScanCalendarNameComponent(str, cur)) == 0) return 0;
  cur += len;
  while ((str.length() > (cur + 1)) && (str[cur++] == '-')) {
    if ((len = ScanCalendarNameComponent(str, cur)) == 0) return 0;
    // CalendarNameComponent - CalendarName
    cur += len;
  }
  r->calendar_name_start = s;
  r->calendar_name_length = cur - s;
  return cur - s;
}

// Calendar: '[u-ca=' CalendarName ']'
template <typename Char>
int32_t ScanCalendar(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  if (str.length() < (s + 7)) return 0;
  int32_t cur = s;
  // "[u-ca="
  if ((str[cur++] != '[') || (str[cur++] != 'u') || (str[cur++] != '-') ||
      (str[cur++] != 'c') || (str[cur++] != 'a') || (str[cur++] != '=')) {
    return 0;
  }
  int32_t len = ScanCalendarName(str, cur, r);
  if (len == 0) return 0;
  if ((str.length() < (cur + len + 1)) || (str[cur + len] != ']')) {
    // Only ScanCalendar know the post condition of CalendarName is not met and
    // need to reset here.
    r->calendar_name_start = 0;
    r->calendar_name_length = 0;
    return 0;
  }
  return 6 + len + 1;
}

// CalendarTime_L1:
//  TimeDesignator TimeSpec [TimeZone] [Calendar]
template <typename Char>
int32_t ScanCalendarTime_L1(base::Vector<Char> str, int32_t s,
                            ParsedISO8601Result* r) {
  int32_t cur = s;
  if (str.length() < (s + 1)) return 0;
  // TimeDesignator
  if (AsciiAlphaToLower(str[cur++]) != 't') return 0;
  int32_t len = ScanTimeSpec(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// CalendarTime_L2 :
//  TimeSpecWithOptionalTimeZoneNotAmbiguous [Calendar]
template <typename Char>
int32_t ScanCalendarTime_L2(base::Vector<Char> str, int32_t s,
                            ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanTimeSpecWithOptionalTimeZoneNotAmbiguous(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// DateTime: Date [TimeSpecSeparator][TimeZone]
template <typename Char>
int32_t ScanDateTime(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanTimeSpecSeparator(str, cur, r);
  cur += ScanTimeZone(str, cur, r);
  return cur - s;
}

// DateSpecYearMonth: DateYear ['-'] DateMonth
template <typename Char>
int32_t ScanDateSpecYearMonth(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Result* r) {
  int32_t date_year, date_month;
  int32_t cur = s;
  int32_t len = ScanDateYear(str, cur, &date_year);
  if (len == 0) return 0;
  cur += len;
  if (str.length() < (cur + 1)) return 0;
  if (str[cur] == '-') cur++;
  len = ScanDateMonth(str, cur, &date_month);
  if (len == 0) return 0;
  r->date_year = date_year;
  r->date_month = date_month;
  cur += len;
  return cur - s;
}

// DateSpecMonthDay:
//   [TwoDash] DateMonth [-] DateDay
template <typename Char>
int32_t ScanDateSpecMonthDay(base::Vector<Char> str, int32_t s,
                             ParsedISO8601Result* r) {
  if (str.length() < (s + 4)) return 0;
  int32_t cur = s;
  if (str[cur] == '-') {
    // The first two dash are optional together
    if (str[++cur] != '-') return 0;
    // TwoDash
    cur++;
  }
  int32_t date_month, date_day;
  int32_t len = ScanDateMonth(str, cur, &date_month);
  if (len == 0) return 0;
  cur += len;
  if (str.length() < (cur + 1)) return 0;
  // '-'
  if (str[cur] == '-') cur++;
  len = ScanDateDay(str, cur, &date_day);
  if (len == 0) return 0;
  r->date_month = date_month;
  r->date_day = date_day;
  cur += len;
  return cur - s;
}

// TimeZoneIdentifier :
//   TimeZoneIANAName
//   TimeZoneUTCOffsetName
template <typename Char>
int32_t ScanTimeZoneIdentifier(base::Vector<Char> str, int32_t s,
                               ParsedISO8601Result* r) {
  int32_t len;
  int32_t cur = s;
  if ((len = ScanTimeZoneIANAName(str, cur)) > 0 ||
      (len = ScanTimeZoneUTCOffsetName(str, cur)) > 0) {
    cur += len;
    r->tzi_name_start = s;
    r->tzi_name_length = len;
    return cur - s;
  }
  return 0;
}

// CalendarDateTime: DateTime [Calendar]
template <typename Char>
int32_t ScanCalendarDateTime(base::Vector<Char> str, int32_t s,
                             ParsedISO8601Result* r) {
  int32_t len = ScanDateTime(str, s, r);
  if (len == 0) return 0;
  return len + ScanCalendar(str, len, r);
}

// CalendarDateTimeTimeRequired: Date TimeSpecSeparator [TimeZone] [Calendar]
template <typename Char>
int32_t ScanCalendarDateTimeTimeRequired(base::Vector<Char> str, int32_t s,
                                         ParsedISO8601Result* r) {
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  len = ScanTimeSpecSeparator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [TimeZone]
  cur += ScanTimeZone(str, cur, r);
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// TemporalZonedDateTimeString:
//   Date [TimeSpecSeparator] TimeZoneNameRequired [Calendar]
template <typename Char>
int32_t ScanTemporalZonedDateTimeString(base::Vector<Char> str, int32_t s,
                                        ParsedISO8601Result* r) {
  // Date
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // TimeSpecSeparator
  cur += ScanTimeSpecSeparator(str, cur, r);

  // TimeZoneNameRequired
  len = ScanTimeZoneNameRequired(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // Calendar
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

SCAN_FORWARD(TemporalDateTimeString, CalendarDateTime, ParsedISO8601Result)

// TemporalMonthDayString
//   DateSpecMonthDay
//   CalendarDateTime
// The lookahead is at most 5 chars.
SCAN_EITHER_FORWARD(TemporalMonthDayString, DateSpecMonthDay, CalendarDateTime,
                    ParsedISO8601Result)

// TemporalInstantString
//   Date [TimeSpecSeparator] TimeZoneOffsetRequired [Calendar]
template <typename Char>
int32_t ScanTemporalInstantString(base::Vector<Char> str, int32_t s,
                                  ParsedISO8601Result* r) {
  // Date
  int32_t cur = s;
  int32_t len = ScanDate(str, cur, r);
  if (len == 0) return 0;
  cur += len;

  // [TimeSpecSeparator]
  cur += ScanTimeSpecSeparator(str, cur, r);

  // TimeZoneOffsetRequired
  len = ScanTimeZoneOffsetRequired(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  // [Calendar]
  cur += ScanCalendar(str, cur, r);
  return cur - s;
}

// ==============================================================================
#define SATISIFY(T, R)                            \
  template <typename Char>                        \
  bool Satisfy##T(base::Vector<Char> str, R* r) { \
    R ret;                                        \
    int32_t len = Scan##T(str, 0, &ret);          \
    if ((len > 0) && (len == str.length())) {     \
      *r = ret;                                   \
      return true;                                \
    }                                             \
    return false;                                 \
  }

#define IF_SATISFY_RETURN(T)             \
  {                                      \
    if (Satisfy##T(str, r)) return true; \
  }

#define SATISIFY_EITHER(T1, T2, T3, R)             \
  template <typename Char>                         \
  bool Satisfy##T1(base::Vector<Char> str, R* r) { \
    IF_SATISFY_RETURN(T2)                          \
    IF_SATISFY_RETURN(T3)                          \
    return false;                                  \
  }

SATISIFY(TemporalDateTimeString, ParsedISO8601Result)
SATISIFY(DateTime, ParsedISO8601Result)
SATISIFY(DateSpecYearMonth, ParsedISO8601Result)
SATISIFY(DateSpecMonthDay, ParsedISO8601Result)
SATISIFY(CalendarDateTime, ParsedISO8601Result)
SATISIFY(CalendarTime_L1, ParsedISO8601Result)
SATISIFY(CalendarTime_L2, ParsedISO8601Result)

template <typename Char>
bool SatisfyCalendarTime(base::Vector<Char> str, ParsedISO8601Result* r) {
  IF_SATISFY_RETURN(CalendarTime_L1)
  IF_SATISFY_RETURN(CalendarTime_L2)
  return false;
}
SATISIFY(CalendarDateTimeTimeRequired, ParsedISO8601Result)
SATISIFY_EITHER(TemporalTimeString, CalendarTime, CalendarDateTimeTimeRequired,
                ParsedISO8601Result)
SATISIFY_EITHER(TemporalYearMonthString, DateSpecYearMonth, CalendarDateTime,
                ParsedISO8601Result)
SATISIFY_EITHER(TemporalMonthDayString, DateSpecMonthDay, CalendarDateTime,
                ParsedISO8601Result)
SATISIFY(TimeZoneNumericUTCOffset, ParsedISO8601Result)
SATISIFY(TimeZoneIdentifier, ParsedISO8601Result)
SATISIFY(TemporalInstantString, ParsedISO8601Result)
SATISIFY(TemporalZonedDateTimeString, ParsedISO8601Result)

SATISIFY(CalendarName, ParsedISO8601Result)

// Duration

// Digits : Digit [Digits]

template <typename Char>
int32_t ScanDigits(base::Vector<Char> str, int32_t s, double* out) {
  if (str.length() < (s + 1) || !IsDecimalDigit(str[s])) return 0;
  *out = ToInt(str[s]);
  int32_t len = 1;
  while (s + len + 1 <= str.length() && IsDecimalDigit(str[s + len])) {
    *out = 10 * (*out) + ToInt(str[s + len]);
    len++;
  }
  return len;
}

SCAN_FORWARD(DurationYears, Digits, double)
SCAN_FORWARD(DurationMonths, Digits, double)
SCAN_FORWARD(DurationWeeks, Digits, double)
SCAN_FORWARD(DurationDays, Digits, double)

// DurationWholeHours : Digits
SCAN_FORWARD(DurationWholeHours, Digits, double)

// DurationWholeMinutes : Digits
SCAN_FORWARD(DurationWholeMinutes, Digits, double)

// DurationWholeSeconds : Digits
SCAN_FORWARD(DurationWholeSeconds, Digits, double)

// DurationHoursFraction : TimeFraction
SCAN_FORWARD(DurationHoursFraction, TimeFraction, int32_t)

// DurationMinutesFraction : TimeFraction
SCAN_FORWARD(DurationMinutesFraction, TimeFraction, int32_t)

// DurationSecondsFraction : TimeFraction
SCAN_FORWARD(DurationSecondsFraction, TimeFraction, int32_t)

#define DURATION_WHOLE_FRACTION_DESIGNATOR(Name, name, d)                 \
  template <typename Char>                                                \
  int32_t ScanDurationWhole##Name##FractionDesignator(                    \
      base::Vector<Char> str, int32_t s, ParsedISO8601Duration* r) {      \
    int32_t cur = s;                                                      \
    double whole = ParsedISO8601Duration::kEmpty;                         \
    cur += ScanDurationWhole##Name(str, cur, &whole);                     \
    if (cur == s) return 0;                                               \
    int32_t fraction = ParsedISO8601Duration::kEmpty;                     \
    int32_t len = ScanDuration##Name##Fraction(str, cur, &fraction);      \
    cur += len;                                                           \
    if (str.length() < (cur + 1) || AsciiAlphaToLower(str[cur++]) != (d)) \
      return 0;                                                           \
    r->whole_##name = whole;                                              \
    r->name##_fraction = fraction;                                        \
    return cur - s;                                                       \
  }

DURATION_WHOLE_FRACTION_DESIGNATOR(Seconds, seconds, 's')
DURATION_WHOLE_FRACTION_DESIGNATOR(Minutes, minutes, 'm')
DURATION_WHOLE_FRACTION_DESIGNATOR(Hours, hours, 'h')

// DurationSecondsPart :
//   DurationWholeSeconds DurationSecondsFractionopt SecondsDesignator
SCAN_FORWARD(DurationSecondsPart, DurationWholeSecondsFractionDesignator,
             ParsedISO8601Duration)

// DurationMinutesPart :
//   DurationWholeMinutes DurationMinutesFractionopt MinutesDesignator
//   [DurationSecondsPart]
template <typename Char>
int32_t ScanDurationMinutesPart(base::Vector<Char> str, int32_t s,
                                ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationWholeMinutesFractionDesignator(str, s, r);
  if (len == 0) return 0;
  cur += len;
  cur += ScanDurationSecondsPart(str, cur, r);
  return cur - s;
}

// DurationHoursPart :
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   DurationMinutesPart
//
//   DurationWholeHours DurationHoursFractionopt HoursDesignator
//   [DurationSecondsPart]
template <typename Char>
int32_t ScanDurationHoursPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationWholeHoursFractionDesignator(str, s, r);
  if (len == 0) return 0;
  cur += len;
  len = ScanDurationMinutesPart(str, cur, r);
  if (len > 0) {
    cur += len;
  } else {
    cur += ScanDurationSecondsPart(str, cur, r);
  }
  return cur - s;
}

// DurationTime :
//   TimeDesignator DurationHoursPart
//   TimeDesignator DurationMinutesPart
//   TimeDesignator DurationSecondsPart
template <typename Char>
int32_t ScanDurationTime(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Duration* r) {
  int32_t cur = s;
  if (str.length() < (s + 1)) return 0;
  if (AsciiAlphaToLower(str[cur++]) != 't') return 0;
  if ((cur += ScanDurationHoursPart(str, cur, r)) - s > 1) return cur - s;
  if ((cur += ScanDurationMinutesPart(str, cur, r)) - s > 1) return cur - s;
  if ((cur += ScanDurationSecondsPart(str, cur, r)) - s > 1) return cur - s;
  return 0;
}

#define DURATION_AND_DESIGNATOR(Name, name, d)                              \
  template <typename Char>                                                  \
  int32_t ScanDuration##Name##Designator(base::Vector<Char> str, int32_t s, \
                                         ParsedISO8601Duration* r) {        \
    int32_t cur = s;                                                        \
    double name;                                                            \
    if ((cur += ScanDuration##Name(str, cur, &name)) == s) return 0;        \
    if (str.length() < (cur + 1) || AsciiAlphaToLower(str[cur++]) != (d)) { \
      return 0;                                                             \
    }                                                                       \
    r->name = name;                                                         \
    return cur - s;                                                         \
  }

DURATION_AND_DESIGNATOR(Days, days, 'd')
DURATION_AND_DESIGNATOR(Weeks, weeks, 'w')
DURATION_AND_DESIGNATOR(Months, months, 'm')
DURATION_AND_DESIGNATOR(Years, years, 'y')

// DurationDaysPart : DurationDays DaysDesignator
SCAN_FORWARD(DurationDaysPart, DurationDaysDesignator, ParsedISO8601Duration)

// DurationWeeksPart : DurationWeeks WeeksDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationWeeksPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  if ((cur += ScanDurationWeeksDesignator(str, cur, r)) == s) return 0;
  cur += ScanDurationDaysPart(str, cur, r);
  return cur - s;
}

// DurationMonthsPart :
//   DurationMonths MonthsDesignator DurationWeeksPart
//   DurationMonths MonthsDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationMonthsPart(base::Vector<Char> str, int32_t s,
                               ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationMonthsDesignator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  if ((len = ScanDurationWeeksPart(str, cur, r)) > 0) {
    cur += len;
  } else {
    cur += ScanDurationDaysPart(str, cur, r);
  }
  return cur - s;
}

// DurationYearsPart :
//   DurationYears YearsDesignator DurationMonthsPart
//   DurationYears YearsDesignator DurationWeeksPart
//   DurationYears YearsDesignator [DurationDaysPart]
template <typename Char>
int32_t ScanDurationYearsPart(base::Vector<Char> str, int32_t s,
                              ParsedISO8601Duration* r) {
  int32_t cur = s;
  int32_t len = ScanDurationYearsDesignator(str, cur, r);
  if (len == 0) return 0;
  cur += len;
  if ((len = ScanDurationMonthsPart(str, cur, r)) > 0) {
    cur += len;
  } else if ((len = ScanDurationWeeksPart(str, cur, r)) > 0) {
    cur += len;
  } else {
    len = ScanDurationDaysPart(str, cur, r);
    cur += len;
  }
  return cur - s;
}

// DurationDate :
//   DurationYearsPart [DurationTime]
//   DurationMonthsPart [DurationTime]
//   DurationWeeksPart [DurationTime]
//   DurationDaysPart [DurationTime]
template <typename Char>
int32_t ScanDurationDate(base::Vector<Char> str, int32_t s,
                         ParsedISO8601Duration* r) {
  int32_t cur = s;
  do {
    if ((cur += ScanDurationYearsPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationMonthsPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationWeeksPart(str, cur, r)) > s) break;
    if ((cur += ScanDurationDaysPart(str, cur, r)) > s) break;
    return 0;
  } while (false);
  cur += ScanDurationTime(str, cur, r);
  return cur - s;
}

// Duration :
//   Signopt DurationDesignator DurationDate
//   Signopt DurationDesignator DurationTime
template <typename Char>
int32_t ScanDuration(base::Vector<Char> str, int32_t s,
                     ParsedISO8601Duration* r) {
  if (str.length() < (s + 2)) return 0;
  int32_t cur = s;
  int32_t sign =
      (IsSign(str[cur]) && CanonicalSign(str[cur++]) == '-') ? -1 : 1;
  if (AsciiAlphaToLower(str[cur++]) != 'p') return 0;
  int32_t len = ScanDurationDate(str, cur, r);
  if (len == 0) len = ScanDurationTime(str, cur, r);
  if (len == 0) return 0;
  r->sign = sign;
  cur += len;
  return cur - s;
}
SCAN_FORWARD(TemporalDurationString, Duration, ParsedISO8601Duration)

SATISIFY(TemporalDurationString, ParsedISO8601Duration)

}  // namespace

#define IMPL_PARSE_METHOD(R, NAME)                                         \
  std::optional<R> TemporalParser::Parse##NAME(                            \
      Isolate* isolate, DirectHandle<String> iso_string) {                 \
    bool valid;                                                            \
    R parsed;                                                              \
    iso_string = String::Flatten(isolate, iso_string);                     \
    {                                                                      \
      DisallowGarbageCollection no_gc;                                     \
      String::FlatContent str_content = iso_string->GetFlatContent(no_gc); \
      if (str_content.IsOneByte()) {                                       \
        valid = Satisfy##NAME(str_content.ToOneByteVector(), &parsed);     \
      } else {                                                             \
        valid = Satisfy##NAME(str_content.ToUC16Vector(), &parsed);        \
      }                                                                    \
    }                                                                      \
    if (valid) return parsed;                                              \
    return std::nullopt;                                                   \
  }

IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalDateTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalYearMonthString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalMonthDayString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalInstantString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TemporalZonedDateTimeString)
IMPL_PARSE_METHOD(ParsedISO8601Result, TimeZoneIdentifier)
IMPL_PARSE_METHOD(ParsedISO8601Result, CalendarName)
IMPL_PARSE_METHOD(ParsedISO8601Result, TimeZoneNumericUTCOffset)
IMPL_PARSE_METHOD(ParsedISO8601Duration, TemporalDurationString)

}  // namespace v8::internal
