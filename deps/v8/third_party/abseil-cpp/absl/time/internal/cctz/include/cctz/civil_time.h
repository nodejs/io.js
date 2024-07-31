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

#ifndef ABSL_TIME_INTERNAL_CCTZ_CIVIL_TIME_H_
#define ABSL_TIME_INTERNAL_CCTZ_CIVIL_TIME_H_

#include "absl/base/config.h"
#include "absl/time/internal/cctz/include/cctz/civil_time_detail.h"

namespace absl {
ABSL_NAMESPACE_BEGIN
namespace time_internal {
namespace cctz {

// The term "civil time" refers to the legally recognized human-scale time
// that is represented by the six fields YYYY-MM-DD hh:mm:ss. Modern-day civil
// time follows the Gregorian Calendar and is a time-zone-independent concept.
// A "date" is perhaps the most common example of a civil time (represented in
// this library as cctz::civil_day). This library provides six classes and a
// handful of functions that help with rounding, iterating, and arithmetic on
// civil times while avoiding complications like daylight-saving time (DST).
//
// The following six classes form the core of this civil-time library:
//
//   * civil_second
//   * civil_minute
//   * civil_hour
//   * civil_day
//   * civil_month
//   * civil_year
//
// Each class is a simple value type with the same interface for construction
// and the same six accessors for each of the civil fields (year, month, day,
// hour, minute, and second, aka YMDHMS). These classes differ only in their
// alignment, which is indicated by the type name and specifies the field on
// which arithmetic operates.
//
// Each class can be constructed by passing up to six optional integer
// arguments representing the YMDHMS fields (in that order) to the
// constructor. Omitted fields are assigned their minimum valid value. Hours,
// minutes, and seconds will be set to 0, month and day will be set to 1, and
// since there is no minimum valid year, it will be set to 1970. So, a
// default-constructed civil-time object will have YMDHMS fields representing
// "1970-01-01 00:00:00". Fields that are out-of-range are normalized (e.g.,
// October 32 -> November 1) so that all civil-time objects represent valid
// values.
//
// Each civil-time class is aligned to the civil-time field indicated in the
// class's name after normalization. Alignment is performed by setting all the
// inferior fields to their minimum valid value (as described above). The
// following are examples of how each of the six types would align the fields
// representing November 22, 2015 at 12:34:56 in the afternoon. (Note: the
// string format used here is not important; it's just a shorthand way of
// showing the six YMDHMS fields.)
//
//   civil_second  2015-11-22 12:34:56
//   civil_minute  2015-11-22 12:34:00
//   civil_hour    2015-11-22 12:00:00
//   civil_day     2015-11-22 00:00:00
//   civil_month   2015-11-01 00:00:00
//   civil_year    2015-01-01 00:00:00
//
// Each civil-time type performs arithmetic on the field to which it is
// aligned. This means that adding 1 to a civil_day increments the day field
// (normalizing as necessary), and subtracting 7 from a civil_month operates
// on the month field (normalizing as necessary). All arithmetic produces a
// valid civil time. Difference requires two similarly aligned civil-time
// objects and returns the scalar answer in units of the objects' alignment.
// For example, the difference between two civil_hour objects will give an
// answer in units of civil hours.
//
// In addition to the six civil-time types just described, there are
// a handful of helper functions and algorithms for performing common
// calculations. These are described below.
//
// Note: In C++14 and later, this library is usable in a constexpr context.
//
// CONSTRUCTION:
//
// Each of the civil-time types can be constructed in two ways: by directly
// passing to the constructor up to six (optional) integers representing the
// YMDHMS fields, or by copying the YMDHMS fields from a differently aligned
// civil-time type.
//
//   civil_day default_value;  // 1970-01-01 00:00:00
//
//   civil_day a(2015, 2, 3);           // 2015-02-03 00:00:00
//   civil_day b(2015, 2, 3, 4, 5, 6);  // 2015-02-03 00:00:00
//   civil_day c(2015);                 // 2015-01-01 00:00:00
//
//   civil_second ss(2015, 2, 3, 4, 5, 6);  // 2015-02-03 04:05:06
//   civil_minute mm(ss);                   // 2015-02-03 04:05:00
//   civil_hour hh(mm);                     // 2015-02-03 04:00:00
//   civil_day d(hh);                       // 2015-02-03 00:00:00
//   civil_month m(d);                      // 2015-02-01 00:00:00
//   civil_year y(m);                       // 2015-01-01 00:00:00
//
//   m = civil_month(y);     // 2015-01-01 00:00:00
//   d = civil_day(m);       // 2015-01-01 00:00:00
//   hh = civil_hour(d);     // 2015-01-01 00:00:00
//   mm = civil_minute(hh);  // 2015-01-01 00:00:00
//   ss = civil_second(mm);  // 2015-01-01 00:00:00
//
// ALIGNMENT CONVERSION:
//
// The alignment of a civil-time object cannot change, but the object may be
// used to construct a new object with a different alignment. This is referred
// to as "realigning". When realigning to a type with the same or more
// precision (e.g., civil_day -> civil_second), the conversion may be
// performed implicitly since no information is lost. However, if information
// could be discarded (e.g., civil_second -> civil_day), the conversion must
// be explicit at the call site.
//
//   void fun(const civil_day& day);
//
//   civil_second cs;
//   fun(cs);  // Won't compile because data may be discarded
//   fun(civil_day(cs));  // OK: explicit conversion
//
//   civil_day cd;
//   fun(cd);  // OK: no conversion needed
//
//   civil_month cm;
//   fun(cm);  // OK: implicit conversion to civil_day
//
// NORMALIZATION:
//
// Integer arguments passed to the constructor may be out-of-range, in which
// case they are normalized to produce a valid civil-time object. This enables
// natural arithmetic on constructor arguments without worrying about the
// field's range. Normalization guarantees that there are no invalid
// civil-time objects.
//
//   civil_day d(2016, 10, 32);  // Out-of-range day; normalized to 2016-11-01
//
// Note: If normalization is undesired, you can signal an error by comparing
// the constructor arguments to the normalized values returned by the YMDHMS
// properties.
//
// PROPERTIES:
//
// All civil-time types have accessors for all six of the civil-time fields:
// year, month, day, hour, minute, and second. Recall that fields inferior to
// the type's alignment will be set to their minimum valid value.
//
//   civil_day d(2015, 6, 28);
//   // d.year() == 2015
//   // d.month() == 6
//   // d.day() == 28
//   // d.hour() == 0
//   // d.minute() == 0
//   // d.second() == 0
//
// COMPARISON:
//
// Comparison always considers all six YMDHMS fields, regardless of the type's
// alignment. Comparison between differently aligned civil-time types is
// allowed.
//
//   civil_day feb_3(2015, 2, 3);  // 2015-02-03 00:00:00
//   civil_day mar_4(2015, 3, 4);  // 2015-03-04 00:00:00
//   // feb_3 < mar_4
//   // civil_year(feb_3) == civil_year(mar_4)
//
//   civil_second feb_3_noon(2015, 2, 3, 12, 0, 0);  // 2015-02-03 12:00:00
//   // feb_3 < feb_3_noon
//   // feb_3 == civil_day(feb_3_noon)
//
//   // Iterates all the days of February 2015.
//   for (civil_day d(2015, 2, 1); d < civil_month(2015, 3); ++d) {
//     // ...
//   }
//
// STREAMING:
//
// Each civil-time type may be sent to an output stream using operator<<().
// The output format follows the pattern "YYYY-MM-DDThh:mm:ss" where fields
// inferior to the type's alignment are omitted.
//
//   civil_second cs(2015, 2, 3, 4, 5, 6);
//   std::cout << cs << "\n";  // Outputs: 2015-02-03T04:05:06
//
//   civil_day cd(cs);
//   std::cout << cd << "\n";  // Outputs: 2015-02-03
//
//   civil_year cy(cs);
//   std::cout << cy << "\n";  // Outputs: 2015
//
// ARITHMETIC:
//
// Civil-time types support natural arithmetic operators such as addition,
// subtraction, and difference. Arithmetic operates on the civil-time field
// indicated in the type's name. Difference requires arguments with the same
// alignment and returns the answer in units of the alignment.
//
//   civil_day a(2015, 2, 3);
//   ++a;                         // 2015-02-04 00:00:00
//   --a;                         // 2015-02-03 00:00:00
//   civil_day b = a + 1;         // 2015-02-04 00:00:00
//   civil_day c = 1 + b;         // 2015-02-05 00:00:00
//   int n = c - a;               // n = 2 (civil days)
//   int m = c - civil_month(c);  // Won't compile: different types.
//
// EXAMPLE: Adding a month to January 31.
//
// One of the classic questions that arises when considering a civil-time
// library (or a date library or a date/time library) is this: "What happens
// when you add a month to January 31?" This is an interesting question
// because there could be a number of possible answers:
//
//   1. March 3 (or 2 if a leap year). This may make sense if the operation
//      wants the equivalent of February 31.
//   2. February 28 (or 29 if a leap year). This may make sense if the operation
//      wants the last day of January to go to the last day of February.
//   3. Error. The caller may get some error, an exception, an invalid date
//      object, or maybe false is returned. This may make sense because there is
//      no single unambiguously correct answer to the question.
//
// Practically speaking, any answer that is not what the programmer intended
// is the wrong answer.
//
// This civil-time library avoids the problem by making it impossible to ask
// ambiguous questions. All civil-time objects are aligned to a particular
// civil-field boundary (such as aligned to a year, month, day, hour, minute,
// or second), and arithmetic operates on the field to which the object is
// aligned. This means that in order to "add a month" the object must first be
// aligned to a month boundary, which is equivalent to the first day of that
// month.
//
// Of course, there are ways to compute an answer the question at hand using
// this civil-time library, but they require the programmer to be explicit
// about the answer they expect. To illustrate, let's see how to compute all
// three of the above possible answers to the question of "Jan 31 plus 1
// month":
//
//   const civil_day d(2015, 1, 31);
//
//   // Answer 1:
//   // Add 1 to the month field in the constructor, and rely on normalization.
//   const auto ans_normalized = civil_day(d.year(), d.month() + 1, d.day());
//   // ans_normalized == 2015-03-03 (aka Feb 31)
//
//   // Answer 2:
//   // Add 1 to month field, capping to the end of next month.
//   const auto next_month = civil_month(d) + 1;
//   const auto last_day_of_next_month = civil_day(next_month + 1) - 1;
//   const auto ans_capped = std::min(ans_normalized, last_day_of_next_month);
//   // ans_capped == 2015-02-28
//
//   // Answer 3:
//   // Signal an error if the normalized answer is not in next month.
//   if (civil_month(ans_normalized) != next_month) {
//     // error, month overflow
//   }
//
using civil_year = detail::civil_year;
using civil_month = detail::civil_month;
using civil_day = detail::civil_day;
using civil_hour = detail::civil_hour;
using civil_minute = detail::civil_minute;
using civil_second = detail::civil_second;

// An enum class with members monday, tuesday, wednesday, thursday, friday,
// saturday, and sunday. These enum values may be sent to an output stream
// using operator<<(). The result is the full weekday name in English with a
// leading capital letter.
//
//   weekday wd = weekday::thursday;
//   std::cout << wd << "\n";  // Outputs: Thursday
//
using detail::weekday;

// Returns the weekday for the given civil-time value.
//
//   civil_day a(2015, 8, 13);
//   weekday wd = get_weekday(a);  // wd == weekday::thursday
//
using detail::get_weekday;

// Returns the civil_day that strictly follows or precedes the given
// civil_day, and that falls on the given weekday.
//
// For example, given:
//
//     August 2015
// Su Mo Tu We Th Fr Sa
//                    1
//  2  3  4  5  6  7  8
//  9 10 11 12 13 14 15
// 16 17 18 19 20 21 22
// 23 24 25 26 27 28 29
// 30 31
//
//   civil_day a(2015, 8, 13);  // get_weekday(a) == weekday::thursday
//   civil_day b = next_weekday(a, weekday::thursday);  // b = 2015-08-20
//   civil_day c = prev_weekday(a, weekday::thursday);  // c = 2015-08-06
//
//   civil_day d = ...
//   // Gets the following Thursday if d is not already Thursday
//   civil_day thurs1 = next_weekday(d - 1, weekday::thursday);
//   // Gets the previous Thursday if d is not already Thursday
//   civil_day thurs2 = prev_weekday(d + 1, weekday::thursday);
//
using detail::next_weekday;
using detail::prev_weekday;

// Returns the day-of-year for the given civil-time value.
//
//   civil_day a(2015, 1, 1);
//   int yd_jan_1 = get_yearday(a);   // yd_jan_1 = 1
//   civil_day b(2015, 12, 31);
//   int yd_dec_31 = get_yearday(b);  // yd_dec_31 = 365
//
using detail::get_yearday;

}  // namespace cctz
}  // namespace time_internal
ABSL_NAMESPACE_END
}  // namespace absl

#endif  // ABSL_TIME_INTERNAL_CCTZ_CIVIL_TIME_H_
