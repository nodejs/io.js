// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/js-date-time-format.h"

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "src/base/bit-field.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-date-time-format-inl.h"
#include "src/objects/js-temporal-objects-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/calendar.h"
#include "unicode/dtitvfmt.h"
#include "unicode/dtptngen.h"
#include "unicode/fieldpos.h"
#include "unicode/gregocal.h"
#include "unicode/smpdtfmt.h"
#include "unicode/unistr.h"

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

namespace v8::internal {

namespace {

std::string ToHourCycleString(JSDateTimeFormat::HourCycle hc) {
  switch (hc) {
    case JSDateTimeFormat::HourCycle::kH11:
      return "h11";
    case JSDateTimeFormat::HourCycle::kH12:
      return "h12";
    case JSDateTimeFormat::HourCycle::kH23:
      return "h23";
    case JSDateTimeFormat::HourCycle::kH24:
      return "h24";
    case JSDateTimeFormat::HourCycle::kUndefined:
      return "";
    default:
      UNREACHABLE();
  }
}

JSDateTimeFormat::HourCycle ToHourCycle(const std::string& hc) {
  if (hc == "h11") return JSDateTimeFormat::HourCycle::kH11;
  if (hc == "h12") return JSDateTimeFormat::HourCycle::kH12;
  if (hc == "h23") return JSDateTimeFormat::HourCycle::kH23;
  if (hc == "h24") return JSDateTimeFormat::HourCycle::kH24;
  return JSDateTimeFormat::HourCycle::kUndefined;
}

JSDateTimeFormat::HourCycle ToHourCycle(UDateFormatHourCycle hc) {
  switch (hc) {
    case UDAT_HOUR_CYCLE_11:
      return JSDateTimeFormat::HourCycle::kH11;
    case UDAT_HOUR_CYCLE_12:
      return JSDateTimeFormat::HourCycle::kH12;
    case UDAT_HOUR_CYCLE_23:
      return JSDateTimeFormat::HourCycle::kH23;
    case UDAT_HOUR_CYCLE_24:
      return JSDateTimeFormat::HourCycle::kH24;
    default:
      return JSDateTimeFormat::HourCycle::kUndefined;
  }
}

// The following two functions are hack until we add necessary API to ICU
// to get default hour cycle for 12 hours system (h11 or h12) or 24 hours system
// (h23 or h24).
// From timeData in third_party/icu/source/data/misc/supplementalData.txt
// we know the preferred values are either h or H.
// And all allowed values are also h or H except in JP K (h11) is listed before
// h (h12).
JSDateTimeFormat::HourCycle DefaultHourCycle12(
    const icu::Locale& locale, JSDateTimeFormat::HourCycle defaultHourCycle) {
  if (defaultHourCycle == JSDateTimeFormat::HourCycle::kH11 ||
      defaultHourCycle == JSDateTimeFormat::HourCycle::kH12) {
    return defaultHourCycle;
  }
  if (std::strcmp(locale.getCountry(), "JP") == 0) {
    return JSDateTimeFormat::HourCycle::kH11;
  }
  return JSDateTimeFormat::HourCycle::kH12;
}

JSDateTimeFormat::HourCycle DefaultHourCycle24(
    const icu::Locale& locale, JSDateTimeFormat::HourCycle defaultHourCycle) {
  if (defaultHourCycle == JSDateTimeFormat::HourCycle::kH23 ||
      defaultHourCycle == JSDateTimeFormat::HourCycle::kH24) {
    return defaultHourCycle;
  }
  return JSDateTimeFormat::HourCycle::kH23;
}

Maybe<JSDateTimeFormat::HourCycle> GetHourCycle(
    Isolate* isolate, DirectHandle<JSReceiver> options,
    const char* method_name) {
  return GetStringOption<JSDateTimeFormat::HourCycle>(
      isolate, options, "hourCycle", method_name, {"h11", "h12", "h23", "h24"},
      {JSDateTimeFormat::HourCycle::kH11, JSDateTimeFormat::HourCycle::kH12,
       JSDateTimeFormat::HourCycle::kH23, JSDateTimeFormat::HourCycle::kH24},
      JSDateTimeFormat::HourCycle::kUndefined);
}

class PatternMap {
 public:
  PatternMap(std::string pattern, std::string value)
      : pattern(std::move(pattern)), value(std::move(value)) {}
  virtual ~PatternMap() = default;
  std::string pattern;
  std::string value;
};

#define BIT_FIELDS(V, _)      \
  V(Era, bool, 1, _)          \
  V(Year, bool, 1, _)         \
  V(Month, bool, 1, _)        \
  V(Weekday, bool, 1, _)      \
  V(Day, bool, 1, _)          \
  V(DayPeriod, bool, 1, _)    \
  V(Hour, bool, 1, _)         \
  V(Minute, bool, 1, _)       \
  V(Second, bool, 1, _)       \
  V(TimeZoneName, bool, 1, _) \
  V(FractionalSecondDigits, bool, 1, _)
DEFINE_BIT_FIELDS(BIT_FIELDS)
#undef BIT_FIELDS

class PatternItem {
 public:
  PatternItem(int32_t shift, const std::string property,
              std::vector<PatternMap> pairs,
              std::vector<const char*> allowed_values)
      : bitShift(shift),
        property(std::move(property)),
        pairs(std::move(pairs)),
        allowed_values(allowed_values) {}
  virtual ~PatternItem() = default;

  int32_t bitShift;
  const std::string property;
  // It is important for the pattern in the pairs from longer one to shorter one
  // if the longer one contains substring of an shorter one.
  std::vector<PatternMap> pairs;
  std::vector<const char*> allowed_values;
};

static std::vector<PatternItem> BuildPatternItems() {
  const std::vector<const char*> kLongShort = {"long", "short"};
  const std::vector<const char*> kNarrowLongShort = {"narrow", "long", "short"};
  const std::vector<const char*> k2DigitNumeric = {"2-digit", "numeric"};
  const std::vector<const char*> kNarrowLongShort2DigitNumeric = {
      "narrow", "long", "short", "2-digit", "numeric"};
  std::vector<PatternItem> items = {
      PatternItem(Weekday::kShift, "weekday",
                  {{"EEEEE", "narrow"},
                   {"EEEE", "long"},
                   {"EEE", "short"},
                   {"ccccc", "narrow"},
                   {"cccc", "long"},
                   {"ccc", "short"}},
                  kNarrowLongShort),
      PatternItem(Era::kShift, "era",
                  {{"GGGGG", "narrow"}, {"GGGG", "long"}, {"GGG", "short"}},
                  kNarrowLongShort),
      PatternItem(Year::kShift, "year", {{"yy", "2-digit"}, {"y", "numeric"}},
                  k2DigitNumeric)};
  // Sometimes we get L instead of M for month - standalone name.
  items.push_back(PatternItem(Month::kShift, "month",
                              {{"MMMMM", "narrow"},
                               {"MMMM", "long"},
                               {"MMM", "short"},
                               {"MM", "2-digit"},
                               {"M", "numeric"},
                               {"LLLLL", "narrow"},
                               {"LLLL", "long"},
                               {"LLL", "short"},
                               {"LL", "2-digit"},
                               {"L", "numeric"}},
                              kNarrowLongShort2DigitNumeric));
  items.push_back(PatternItem(Day::kShift, "day",
                              {{"dd", "2-digit"}, {"d", "numeric"}},
                              k2DigitNumeric));
  items.push_back(PatternItem(DayPeriod::kShift, "dayPeriod",
                              {{"BBBBB", "narrow"},
                               {"bbbbb", "narrow"},
                               {"BBBB", "long"},
                               {"bbbb", "long"},
                               {"B", "short"},
                               {"b", "short"}},
                              kNarrowLongShort));
  items.push_back(PatternItem(Hour::kShift, "hour",
                              {{"HH", "2-digit"},
                               {"H", "numeric"},
                               {"hh", "2-digit"},
                               {"h", "numeric"},
                               {"kk", "2-digit"},
                               {"k", "numeric"},
                               {"KK", "2-digit"},
                               {"K", "numeric"}},
                              k2DigitNumeric));
  items.push_back(PatternItem(Minute::kShift, "minute",
                              {{"mm", "2-digit"}, {"m", "numeric"}},
                              k2DigitNumeric));
  items.push_back(PatternItem(Second::kShift, "second",
                              {{"ss", "2-digit"}, {"s", "numeric"}},
                              k2DigitNumeric));

  const std::vector<const char*> kTimezone = {"long",        "short",
                                              "longOffset",  "shortOffset",
                                              "longGeneric", "shortGeneric"};
  items.push_back(PatternItem(TimeZoneName::kShift, "timeZoneName",
                              {{"zzzz", "long"},
                               {"z", "short"},
                               {"OOOO", "longOffset"},
                               {"O", "shortOffset"},
                               {"vvvv", "longGeneric"},
                               {"v", "shortGeneric"}},
                              kTimezone));
  return items;
}

class PatternItems {
 public:
  PatternItems() : data(BuildPatternItems()) {}
  virtual ~PatternItems() = default;
  const std::vector<PatternItem>& Get() const { return data; }

 private:
  const std::vector<PatternItem> data;
};

static const std::vector<PatternItem>& GetPatternItems() {
  static base::LazyInstance<PatternItems>::type items =
      LAZY_INSTANCE_INITIALIZER;
  return items.Pointer()->Get();
}

class PatternData {
 public:
  PatternData(int32_t shift, const std::string property,
              std::vector<PatternMap> pairs,
              std::vector<const char*> allowed_values)
      : bitShift(shift),
        property(std::move(property)),
        allowed_values(allowed_values) {
    for (const auto& pair : pairs) {
      map.insert(std::make_pair(pair.value, pair.pattern));
    }
  }
  virtual ~PatternData() = default;

  int32_t bitShift;
  const std::string property;
  std::map<const std::string, const std::string> map;
  std::vector<const char*> allowed_values;
};

const std::vector<PatternData> CreateCommonData(const PatternData& hour_data) {
  std::vector<PatternData> build;
  for (const PatternItem& item : GetPatternItems()) {
    if (item.property == "hour") {
      build.push_back(hour_data);
    } else {
      build.push_back(PatternData(item.bitShift, item.property, item.pairs,
                                  item.allowed_values));
    }
  }
  return build;
}

const std::vector<PatternData> CreateData(const char* digit2,
                                          const char* numeric) {
  return CreateCommonData(PatternData(
      Hour::kShift, "hour", {{digit2, "2-digit"}, {numeric, "numeric"}},
      {"2-digit", "numeric"}));
}

// According to "Date Field Symbol Table" in
// http://userguide.icu-project.org/formatparse/datetime
// Symbol | Meaning              | Example(s)
//   h      hour in am/pm (1~12)    h    7
//                                  hh   07
//   H      hour in day (0~23)      H    0
//                                  HH   00
//   k      hour in day (1~24)      k    24
//                                  kk   24
//   K      hour in am/pm (0~11)    K    0
//                                  KK   00

class Pattern {
 public:
  Pattern(const char* d1, const char* d2) : data(CreateData(d1, d2)) {}
  virtual ~Pattern() = default;
  virtual const std::vector<PatternData>& Get() const { return data; }

 private:
  std::vector<PatternData> data;
};

#define DEFFINE_TRAIT(name, d1, d2)              \
  struct name {                                  \
    static void Construct(void* allocated_ptr) { \
      new (allocated_ptr) Pattern(d1, d2);       \
    }                                            \
  };
DEFFINE_TRAIT(H11Trait, "KK", "K")
DEFFINE_TRAIT(H12Trait, "hh", "h")
DEFFINE_TRAIT(H23Trait, "HH", "H")
DEFFINE_TRAIT(H24Trait, "kk", "k")
DEFFINE_TRAIT(HDefaultTrait, "jj", "j")
#undef DEFFINE_TRAIT

const std::vector<PatternData>& GetPatternData(
    JSDateTimeFormat::HourCycle hour_cycle) {
  switch (hour_cycle) {
    case JSDateTimeFormat::HourCycle::kH11: {
      static base::LazyInstance<Pattern, H11Trait>::type h11 =
          LAZY_INSTANCE_INITIALIZER;
      return h11.Pointer()->Get();
    }
    case JSDateTimeFormat::HourCycle::kH12: {
      static base::LazyInstance<Pattern, H12Trait>::type h12 =
          LAZY_INSTANCE_INITIALIZER;
      return h12.Pointer()->Get();
    }
    case JSDateTimeFormat::HourCycle::kH23: {
      static base::LazyInstance<Pattern, H23Trait>::type h23 =
          LAZY_INSTANCE_INITIALIZER;
      return h23.Pointer()->Get();
    }
    case JSDateTimeFormat::HourCycle::kH24: {
      static base::LazyInstance<Pattern, H24Trait>::type h24 =
          LAZY_INSTANCE_INITIALIZER;
      return h24.Pointer()->Get();
    }
    case JSDateTimeFormat::HourCycle::kUndefined: {
      static base::LazyInstance<Pattern, HDefaultTrait>::type hDefault =
          LAZY_INSTANCE_INITIALIZER;
      return hDefault.Pointer()->Get();
    }
    default:
      UNREACHABLE();
  }
}

std::string GetGMTTzID(const std::string& input) {
  std::string ret = "Etc/GMT";
  switch (input.length()) {
    case 8:
      if (input[7] == '0') return ret + '0';
      break;
    case 9:
      if ((input[7] == '+' || input[7] == '-') &&
          base::IsInRange(input[8], '0', '9')) {
        return ret + input[7] + input[8];
      }
      break;
    case 10:
      if ((input[7] == '+' || input[7] == '-') && (input[8] == '1') &&
          base::IsInRange(input[9], '0', '4')) {
        return ret + input[7] + input[8] + input[9];
      }
      break;
  }
  return "";
}

// Locale independenty version of isalpha for ascii range. This will return
// false if the ch is alpha but not in ascii range.
bool IsAsciiAlpha(char ch) {
  return base::IsInRange(ch, 'A', 'Z') || base::IsInRange(ch, 'a', 'z');
}

// Locale independent toupper for ascii range. This will not return İ (dotted I)
// for i under Turkish locale while std::toupper may.
char LocaleIndependentAsciiToUpper(char ch) {
  return (base::IsInRange(ch, 'a', 'z')) ? (ch - 'a' + 'A') : ch;
}

// Locale independent tolower for ascii range.
char LocaleIndependentAsciiToLower(char ch) {
  return (base::IsInRange(ch, 'A', 'Z')) ? (ch - 'A' + 'a') : ch;
}

// Returns titlecased location, bueNos_airES -> Buenos_Aires
// or ho_cHi_minH -> Ho_Chi_Minh. It is locale-agnostic and only
// deals with ASCII only characters.
// 'of', 'au' and 'es' are special-cased and lowercased.
// ICU's timezone parsing is case sensitive, but ECMAScript is case insensitive
std::string ToTitleCaseTimezoneLocation(const std::string& input) {
  std::string title_cased;
  int word_length = 0;
  for (char ch : input) {
    // Convert first char to upper case, the rest to lower case
    if (IsAsciiAlpha(ch)) {
      title_cased += word_length == 0 ? LocaleIndependentAsciiToUpper(ch)
                                      : LocaleIndependentAsciiToLower(ch);
      word_length++;
    } else if (ch == '_' || ch == '-' || ch == '/') {
      // Special case Au/Es/Of to be lower case.
      if (word_length == 2) {
        size_t pos = title_cased.length() - 2;
        std::string substr = title_cased.substr(pos, 2);
        if (substr == "Of" || substr == "Es" || substr == "Au") {
          title_cased[pos] = LocaleIndependentAsciiToLower(title_cased[pos]);
        }
      }
      title_cased += ch;
      word_length = 0;
    } else {
      // Invalid input
      return std::string();
    }
  }

  return title_cased;
}

class SpecialTimeZoneMap {
 public:
  SpecialTimeZoneMap() {
    Add("America/Argentina/ComodRivadavia");
    Add("America/Knox_IN");
    Add("Antarctica/DumontDUrville");
    Add("Antarctica/McMurdo");
    Add("Australia/ACT");
    Add("Australia/LHI");
    Add("Australia/NSW");
    Add("Brazil/DeNoronha");
    Add("Chile/EasterIsland");
    Add("GB");
    Add("GB-Eire");
    Add("Mexico/BajaNorte");
    Add("Mexico/BajaSur");
    Add("NZ");
    Add("NZ-CHAT");
    Add("W-SU");
  }

  std::string Find(const std::string& id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      return it->second;
    }
    return "";
  }

 private:
  void Add(const char* id) {
    std::string upper(id);
    transform(upper.begin(), upper.end(), upper.begin(),
              LocaleIndependentAsciiToUpper);
    map_.insert({upper, id});
  }
  std::map<std::string, std::string> map_;
};

}  // namespace

// Return the time zone id which match ICU's expectation of title casing
// return empty string when error.
std::string JSDateTimeFormat::CanonicalizeTimeZoneID(const std::string& input) {
  std::string upper = input;
  transform(upper.begin(), upper.end(), upper.begin(),
            LocaleIndependentAsciiToUpper);
  if (upper.length() == 3) {
    if (upper == "GMT") return "UTC";
    // For id such as "CET", return upper case.
    return upper;
  } else if (upper.length() == 7 && '0' <= upper[3] && upper[3] <= '9') {
    // For id such as "CST6CDT", return upper case.
    return upper;
  } else if (upper.length() > 3) {
    if (memcmp(upper.c_str(), "ETC", 3) == 0) {
      if (upper == "ETC/UTC" || upper == "ETC/GMT" || upper == "ETC/UCT") {
        return "UTC";
      }
      if (strncmp(upper.c_str(), "ETC/GMT", 7) == 0) {
        return GetGMTTzID(input);
      }
    } else if (memcmp(upper.c_str(), "GMT", 3) == 0) {
      if (upper == "GMT0" || upper == "GMT+0" || upper == "GMT-0") {
        return "UTC";
      }
    } else if (memcmp(upper.c_str(), "US/", 3) == 0) {
      std::string title = ToTitleCaseTimezoneLocation(input);
      if (title.length() >= 2) {
        // Change "Us/" to "US/"
        title[1] = 'S';
      }
      return title;
    } else if (strncmp(upper.c_str(), "SYSTEMV/", 8) == 0) {
      upper.replace(0, 8, "SystemV/");
      return upper;
    }
  }
  // We expect only _, '-' and / beside ASCII letters.

  static base::LazyInstance<SpecialTimeZoneMap>::type special_time_zone_map =
      LAZY_INSTANCE_INITIALIZER;

  std::string special_case = special_time_zone_map.Pointer()->Find(upper);
  if (!special_case.empty()) {
    return special_case;
  }
  return ToTitleCaseTimezoneLocation(input);
}

namespace {
DirectHandle<String> DateTimeStyleAsString(
    Isolate* isolate, JSDateTimeFormat::DateTimeStyle style) {
  switch (style) {
    case JSDateTimeFormat::DateTimeStyle::kFull:
      return isolate->factory()->full_string();
    case JSDateTimeFormat::DateTimeStyle::kLong:
      return isolate->factory()->long_string();
    case JSDateTimeFormat::DateTimeStyle::kMedium:
      return isolate->factory()->medium_string();
    case JSDateTimeFormat::DateTimeStyle::kShort:
      return isolate->factory()->short_string();
    case JSDateTimeFormat::DateTimeStyle::kUndefined:
      UNREACHABLE();
  }
  UNREACHABLE();
}

int FractionalSecondDigitsFromPattern(const std::string& pattern) {
  int result = 0;
  for (size_t i = 0; i < pattern.length() && result < 3; i++) {
    if (pattern[i] == 'S') {
      result++;
    }
  }
  return result;
}
}  // namespace

MaybeHandle<String> JSDateTimeFormat::TimeZoneIdToString(
    Isolate* isolate, const icu::UnicodeString& id) {
  // In CLDR (http://unicode.org/cldr/trac/ticket/9943), Etc/UTC is made
  // a separate timezone ID from Etc/GMT even though they're still the same
  // timezone. We have Etc/UTC because 'UTC', 'Etc/Universal',
  // 'Etc/Zulu' and others are turned to 'Etc/UTC' by ICU. Etc/GMT comes
  // from Etc/GMT0, Etc/GMT+0, Etc/GMT-0, Etc/Greenwich.
  // ecma402#sec-canonicalizetimezonename step 3
  if (id == UNICODE_STRING_SIMPLE("Etc/UTC") ||
      id == UNICODE_STRING_SIMPLE("Etc/GMT")) {
    return isolate->factory()->UTC_string();
  }
  // If the id is in the format of GMT[+-]hh:mm, change it to
  // [+-]hh:mm.
  if (id.startsWith(u"GMT", 3)) {
    return Intl::ToString(isolate, id.tempSubString(3));
  }
  return Intl::ToString(isolate, id);
}

DirectHandle<Object> JSDateTimeFormat::TimeZoneId(Isolate* isolate,
                                                  const icu::TimeZone& tz) {
  Factory* factory = isolate->factory();
  icu::UnicodeString time_zone;
  tz.getID(time_zone);
  icu::UnicodeString canonical_time_zone;
  if (time_zone == u"GMT") {
    canonical_time_zone = u"+00:00";
  } else {
    UErrorCode status = U_ZERO_ERROR;
    icu::TimeZone::getCanonicalID(time_zone, canonical_time_zone, status);
    if (U_FAILURE(status)) {
      // When the time_zone is neither a known system time zone ID nor a
      // valid custom time zone ID, the status is a failure.
      return factory->undefined_value();
    }
  }
  DirectHandle<String> timezone_value;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, timezone_value, TimeZoneIdToString(isolate, canonical_time_zone),
      {});
  return timezone_value;
}

namespace {
DirectHandle<String> GetCalendar(
    Isolate* isolate, const icu::SimpleDateFormat& simple_date_format) {
  // getType() returns legacy calendar type name instead of LDML/BCP47 calendar
  // key values. intl.js maps them to BCP47 values for key "ca".
  // TODO(jshin): Consider doing it here, instead.
  std::string calendar_str = simple_date_format.getCalendar()->getType();

  // Maps ICU calendar names to LDML/BCP47 types for key 'ca'.
  // See typeMap section in third_party/icu/source/data/misc/keyTypeData.txt
  // and
  // http://www.unicode.org/repos/cldr/tags/latest/common/bcp47/calendar.xml
  if (calendar_str == "gregorian") {
    calendar_str = "gregory";
  } else if (calendar_str == "ethiopic-amete-alem") {
    calendar_str = "ethioaa";
  }
  return isolate->factory()->NewStringFromAsciiChecked(calendar_str.c_str());
}

DirectHandle<Object> GetTimeZone(
    Isolate* isolate, const icu::SimpleDateFormat& simple_date_format) {
  return JSDateTimeFormat::TimeZoneId(
      isolate, simple_date_format.getCalendar()->getTimeZone());
}
}  // namespace

DirectHandle<String> JSDateTimeFormat::Calendar(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format) {
  return GetCalendar(isolate,
                     *(date_time_format->icu_simple_date_format()->raw()));
}

DirectHandle<Object> JSDateTimeFormat::TimeZone(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format) {
  return GetTimeZone(isolate,
                     *(date_time_format->icu_simple_date_format()->raw()));
}

// ecma402 #sec-intl.datetimeformat.prototype.resolvedoptions
MaybeDirectHandle<JSObject> JSDateTimeFormat::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format) {
  Factory* factory = isolate->factory();
  // 4. Let options be ! ObjectCreate(%ObjectPrototype%).
  DirectHandle<JSObject> options =
      factory->NewJSObject(isolate->object_function());

  DirectHandle<Object> resolved_obj;

  DirectHandle<String> locale(date_time_format->locale(), isolate);
  DCHECK(!date_time_format->icu_locale().is_null());
  DCHECK_NOT_NULL(date_time_format->icu_locale()->raw());
  icu::Locale* icu_locale = date_time_format->icu_locale()->raw();

  icu::SimpleDateFormat* icu_simple_date_format =
      date_time_format->icu_simple_date_format()->raw();
  DirectHandle<Object> timezone =
      JSDateTimeFormat::TimeZone(isolate, date_time_format);

  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat/Calendar would.
  // Tracked by https://unicode-org.atlassian.net/browse/ICU-13431
  std::string numbering_system = Intl::GetNumberingSystem(*icu_locale);

  icu::UnicodeString pattern_unicode;
  icu_simple_date_format->toPattern(pattern_unicode);
  std::string pattern;
  pattern_unicode.toUTF8String(pattern);

  // 5. For each row of Table 6, except the header row, in table order, do
  // Table 6: Resolved Options of DateTimeFormat Instances
  //  Internal Slot          Property
  //    [[Locale]]           "locale"
  //    [[Calendar]]         "calendar"
  //    [[NumberingSystem]]  "numberingSystem"
  //    [[TimeZone]]         "timeZone"
  //    [[HourCycle]]        "hourCycle"
  //                         "hour12"
  //    [[Weekday]]          "weekday"
  //    [[Era]]              "era"
  //    [[Year]]             "year"
  //    [[Month]]            "month"
  //    [[Day]]              "day"
  //    [[Hour]]             "hour"
  //    [[Minute]]           "minute"
  //    [[Second]]           "second"
  //    [[FractionalSecondDigits]]     "fractionalSecondDigits"
  //    [[TimeZoneName]]     "timeZoneName"
  Maybe<bool> maybe_create_locale = JSReceiver::CreateDataProperty(
      isolate, options, factory->locale_string(), locale, Just(kDontThrow));
  DCHECK(maybe_create_locale.FromJust());
  USE(maybe_create_locale);

  DirectHandle<String> calendar =
      JSDateTimeFormat::Calendar(isolate, date_time_format);
  Maybe<bool> maybe_create_calendar = JSReceiver::CreateDataProperty(
      isolate, options, factory->calendar_string(), calendar, Just(kDontThrow));
  DCHECK(maybe_create_calendar.FromJust());
  USE(maybe_create_calendar);

  if (!numbering_system.empty()) {
    Maybe<bool> maybe_create_numbering_system = JSReceiver::CreateDataProperty(
        isolate, options, factory->numberingSystem_string(),
        factory->NewStringFromAsciiChecked(numbering_system.c_str()),
        Just(kDontThrow));
    DCHECK(maybe_create_numbering_system.FromJust());
    USE(maybe_create_numbering_system);
  }
  Maybe<bool> maybe_create_time_zone = JSReceiver::CreateDataProperty(
      isolate, options, factory->timeZone_string(), timezone, Just(kDontThrow));
  DCHECK(maybe_create_time_zone.FromJust());
  USE(maybe_create_time_zone);

  // 5.b.i. Let hc be dtf.[[HourCycle]].
  HourCycle hc = date_time_format->hour_cycle();

  if (hc != HourCycle::kUndefined) {
    Maybe<bool> maybe_create_hour_cycle = JSReceiver::CreateDataProperty(
        isolate, options, factory->hourCycle_string(),
        date_time_format->HourCycleAsString(isolate), Just(kDontThrow));
    DCHECK(maybe_create_hour_cycle.FromJust());
    USE(maybe_create_hour_cycle);
    switch (hc) {
      //  ii. If hc is "h11" or "h12", let v be true.
      case HourCycle::kH11:
      case HourCycle::kH12: {
        Maybe<bool> maybe_create_hour12 = JSReceiver::CreateDataProperty(
            isolate, options, factory->hour12_string(), factory->true_value(),
            Just(kDontThrow));
        DCHECK(maybe_create_hour12.FromJust());
        USE(maybe_create_hour12);
      } break;
      // iii. Else if, hc is "h23" or "h24", let v be false.
      case HourCycle::kH23:
      case HourCycle::kH24: {
        Maybe<bool> maybe_create_hour12 = JSReceiver::CreateDataProperty(
            isolate, options, factory->hour12_string(), factory->false_value(),
            Just(kDontThrow));
        DCHECK(maybe_create_hour12.FromJust());
        USE(maybe_create_hour12);
      } break;
      // iv. Else, let v be undefined.
      case HourCycle::kUndefined:
        break;
    }
  }

  // If dateStyle and timeStyle are undefined, then internal slots
  // listed in "Table 1: Components of date and time formats" will be set
  // in Step 33.f.iii.1 of InitializeDateTimeFormat
  if (date_time_format->date_style() == DateTimeStyle::kUndefined &&
      date_time_format->time_style() == DateTimeStyle::kUndefined) {
    for (const auto& item : GetPatternItems()) {
      // fractionalSecondsDigits need to be added before timeZoneName
      if (item.property == "timeZoneName") {
        int fsd = FractionalSecondDigitsFromPattern(pattern);
        if (fsd > 0) {
          Maybe<bool> maybe_create_fractional_seconds_digits =
              JSReceiver::CreateDataProperty(
                  isolate, options, factory->fractionalSecondDigits_string(),
                  factory->NewNumberFromInt(fsd), Just(kDontThrow));
          DCHECK(maybe_create_fractional_seconds_digits.FromJust());
          USE(maybe_create_fractional_seconds_digits);
        }
      }
      for (const auto& pair : item.pairs) {
        if (pattern.find(pair.pattern) != std::string::npos) {
          Maybe<bool> maybe_create_property = JSReceiver::CreateDataProperty(
              isolate, options,
              factory->NewStringFromAsciiChecked(item.property.c_str()),
              factory->NewStringFromAsciiChecked(pair.value.c_str()),
              Just(kDontThrow));
          DCHECK(maybe_create_property.FromJust());
          USE(maybe_create_property);
          break;
        }
      }
    }
  }

  // dateStyle
  if (date_time_format->date_style() != DateTimeStyle::kUndefined) {
    Maybe<bool> maybe_create_date_style = JSReceiver::CreateDataProperty(
        isolate, options, factory->dateStyle_string(),
        DateTimeStyleAsString(isolate, date_time_format->date_style()),
        Just(kDontThrow));
    DCHECK(maybe_create_date_style.FromJust());
    USE(maybe_create_date_style);
  }

  // timeStyle
  if (date_time_format->time_style() != DateTimeStyle::kUndefined) {
    Maybe<bool> maybe_create_time_style = JSReceiver::CreateDataProperty(
        isolate, options, factory->timeStyle_string(),
        DateTimeStyleAsString(isolate, date_time_format->time_style()),
        Just(kDontThrow));
    DCHECK(maybe_create_time_style.FromJust());
    USE(maybe_create_time_style);
  }
  return options;
}

namespace {

// #sec-temporal-istemporalobject
bool IsTemporalObject(DirectHandle<Object> value) {
  // 1. If Type(value) is not Object, then
  if (!IsJSReceiver(*value)) {
    // a. Return false.
    return false;
  }
  // 2. If value does not have an [[InitializedTemporalDate]],
  // [[InitializedTemporalTime]], [[InitializedTemporalDateTime]],
  // [[InitializedTemporalZonedDateTime]], [[InitializedTemporalYearMonth]],
  // [[InitializedTemporalMonthDay]], or [[InitializedTemporalInstant]] internal
  // slot, then
  if (!IsJSTemporalPlainDate(*value) && !IsJSTemporalPlainTime(*value) &&
      !IsJSTemporalPlainDateTime(*value) &&
      !IsJSTemporalZonedDateTime(*value) &&
      !IsJSTemporalPlainYearMonth(*value) &&
      !IsJSTemporalPlainMonthDay(*value) && !IsJSTemporalInstant(*value)) {
    // a. Return false.
    return false;
  }
  // 3. Return true.
  return true;
}

// #sec-temporal-sametemporaltype
bool SameTemporalType(DirectHandle<Object> x, DirectHandle<Object> y) {
  // 1. If either of ! IsTemporalObject(x) or ! IsTemporalObject(y) is false,
  // return false.
  if (!IsTemporalObject(x)) return false;
  if (!IsTemporalObject(y)) return false;
  // 2. If x has an [[InitializedTemporalDate]] internal slot and y does not,
  // return false.
  if (IsJSTemporalPlainDate(*x) && !IsJSTemporalPlainDate(*y)) return false;
  // 3. If x has an [[InitializedTemporalTime]] internal slot and y does not,
  // return false.
  if (IsJSTemporalPlainTime(*x) && !IsJSTemporalPlainTime(*y)) return false;
  // 4. If x has an [[InitializedTemporalDateTime]] internal slot and y does
  // not, return false.
  if (IsJSTemporalPlainDateTime(*x) && !IsJSTemporalPlainDateTime(*y)) {
    return false;
  }
  // 5. If x has an [[InitializedTemporalZonedDateTime]] internal slot and y
  // does not, return false.
  if (IsJSTemporalZonedDateTime(*x) && !IsJSTemporalZonedDateTime(*y)) {
    return false;
  }
  // 6. If x has an [[InitializedTemporalYearMonth]] internal slot and y does
  // not, return false.
  if (IsJSTemporalPlainYearMonth(*x) && !IsJSTemporalPlainYearMonth(*y)) {
    return false;
  }
  // 7. If x has an [[InitializedTemporalMonthDay]] internal slot and y does
  // not, return false.
  if (IsJSTemporalPlainMonthDay(*x) && !IsJSTemporalPlainMonthDay(*y)) {
    return false;
  }
  // 8. If x has an [[InitializedTemporalInstant]] internal slot and y does not,
  // return false.
  if (IsJSTemporalInstant(*x) && !IsJSTemporalInstant(*y)) return false;
  // 9. Return true.
  return true;
}

enum class PatternKind {
  kDate,
  kPlainDate,
  kPlainDateTime,
  kPlainTime,
  kPlainYearMonth,
  kPlainMonthDay,
  kZonedDateTime,
  kInstant,
};
struct DateTimeValueRecord {
  double epoch_milliseconds;
  PatternKind kind;
};

DateTimeValueRecord TemporalInstantToRecord(
    Isolate* isolate, DirectHandle<JSTemporalInstant> instant,
    PatternKind kind) {
  double milliseconds =
      BigInt::Divide(isolate, direct_handle(instant->nanoseconds(), isolate),
                     BigInt::FromInt64(isolate, 1000000))
          .ToHandleChecked()
          ->AsInt64();
  return {milliseconds, kind};
}

Maybe<DateTimeValueRecord> TemporalPlainDateTimeToRecord(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    PatternKind kind, DirectHandle<JSTemporalPlainDateTime> plain_date_time,
    const char* method_name) {
  // 8. Let timeZone be ! CreateTemporalTimeZone(dateTimeFormat.[[TimeZone]]).
  DirectHandle<Object> time_zone_obj = GetTimeZone(isolate, date_time_format);
  // TODO(ftang): we should change the return type of GetTimeZone() to
  // Handle<String> by ensure it will not return undefined.
  CHECK(IsString(*time_zone_obj));
  DirectHandle<JSTemporalTimeZone> time_zone =
      temporal::CreateTemporalTimeZone(isolate, Cast<String>(time_zone_obj))
          .ToHandleChecked();
  // 9. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, plainDateTime,
  // "compatible").
  DirectHandle<JSTemporalInstant> instant;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, instant,
      temporal::BuiltinTimeZoneGetInstantForCompatible(
          isolate, time_zone, plain_date_time, method_name),
      Nothing<DateTimeValueRecord>());
  // 10. If pattern is null, throw a TypeError exception.

  // 11. Return the Record { [[pattern]]: pattern.[[pattern]],
  // [[rangePatterns]]: pattern.[[rangePatterns]], [[epochNanoseconds]]:
  // instant.[[Nanoseconds]] }.
  return Just(TemporalInstantToRecord(isolate, instant, kind));
}

template <typename T>
Maybe<DateTimeValueRecord> TemporalToRecord(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    PatternKind kind, DirectHandle<T> temporal,
    DirectHandle<JSReceiver> calendar, const char* method_name) {
  // 7. Let plainDateTime be ? CreateTemporalDateTime(temporalDate.[[ISOYear]],
  // temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], 12, 0, 0, 0, 0, 0,
  // calendarOverride).
  DirectHandle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{temporal->iso_year(), temporal->iso_month(), temporal->iso_day()},
           {12, 0, 0, 0, 0, 0}},
          calendar),
      Nothing<DateTimeValueRecord>());
  return TemporalPlainDateTimeToRecord(isolate, date_time_format, kind,
                                       plain_date_time, method_name);
}

// #sec-temporal-handledatetimevaluetemporaldate
Maybe<DateTimeValueRecord> HandleDateTimeTemporalDate(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar,
    DirectHandle<JSTemporalPlainDate> temporal_date, const char* method_name) {
  // 1. Assert: temporalDate has an [[InitializedTemporalDate]] internal slot.

  // 2. Let pattern be dateTimeFormat.[[TemporalPlainDatePattern]].

  // 3. Let calendar be ? ToString(temporalDate.[[Calendar]]).
  DirectHandle<String> calendar;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar,
      Object::ToString(isolate,
                       direct_handle(temporal_date->calendar(), isolate)),
      Nothing<DateTimeValueRecord>());

  // 4. If calendar is dateTimeFormat.[[Calendar]], then
  DirectHandle<JSReceiver> calendar_override;
  if (String::Equals(isolate, calendar, date_time_format_calendar)) {
    // a. Let calendarOverride be temporalDate.[[Calendar]].
    calendar_override = direct_handle(temporal_date->calendar(), isolate);
    // 5. Else if calendar is "iso8601", then
  } else if (String::Equals(isolate, calendar,
                            isolate->factory()->iso8601_string())) {
    // a. Let calendarOverride be ?
    // GetBuiltinCalendar(dateTimeFormat.[[Calendar]]).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(
        isolate, calendar_override,
        temporal::GetBuiltinCalendar(isolate, date_time_format_calendar),
        Nothing<DateTimeValueRecord>());
    // 6. Else,
  } else {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalid,
                      isolate->factory()->calendar_string(), calendar),
        Nothing<DateTimeValueRecord>());
  }
  return TemporalToRecord<JSTemporalPlainDate>(
      isolate, date_time_format, PatternKind::kPlainDate, temporal_date,
      calendar_override, method_name);
}

// #sec-temporal-handledatetimevaluetemporaldatetime
Maybe<DateTimeValueRecord> HandleDateTimeTemporalDateTime(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar,
    DirectHandle<JSTemporalPlainDateTime> date_time, const char* method_name) {
  // 1. Assert: dateTime has an [[InitializedTemporalDateTime]] internal slot.
  // 2. Let pattern be dateTimeFormat.[[TemporalPlainDateTimePattern]].
  // 3. Let calendar be ? ToString(dateTime.[[Calendar]]).
  DirectHandle<String> calendar;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar,
      Object::ToString(isolate, direct_handle(date_time->calendar(), isolate)),
      Nothing<DateTimeValueRecord>());
  // 4. If calendar is not "iso8601" and not equal to
  // dateTimeFormat.[[Calendar]], then
  DirectHandle<JSReceiver> calendar_override;
  if (!String::Equals(isolate, calendar,
                      isolate->factory()->iso8601_string()) &&
      !String::Equals(isolate, calendar, date_time_format_calendar)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalid,
                      isolate->factory()->calendar_string(), calendar),
        Nothing<DateTimeValueRecord>());
  }

  // 5. Let timeZone be ! CreateTemporalTimeZone(dateTimeFormat.[[TimeZone]]).
  // 6. Let instant be ? BuiltinTimeZoneGetInstantFor(timeZone, dateTime,
  // "compatible").
  // 7. If pattern is null, throw a TypeError exception.

  // 8. Return the Record { [[pattern]]: pattern.[[pattern]], [[rangePatterns]]:
  // pattern.[[rangePatterns]], [[epochNanoseconds]]: instant.[[Nanoseconds]] }.

  return TemporalPlainDateTimeToRecord(isolate, date_time_format,
                                       PatternKind::kPlainDateTime, date_time,
                                       method_name);
}

// #sec-temporal-handledatetimevaluetemporalzoneddatetime
Maybe<DateTimeValueRecord> HandleDateTimeTemporalZonedDateTime(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar,
    DirectHandle<JSTemporalZonedDateTime> zoned_date_time,
    const char* method_name) {
  // 1. Assert: zonedDateTime has an [[InitializedTemporalZonedDateTime]]
  // internal slot.
  // 2. Let pattern be dateTimeFormat.[[TemporalZonedDateTimePattern]].

  // 3. Let calendar be ? ToString(zonedDateTime.[[Calendar]]).
  DirectHandle<String> calendar;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar,
      Object::ToString(isolate,
                       direct_handle(zoned_date_time->calendar(), isolate)),
      Nothing<DateTimeValueRecord>());
  // 4. If calendar is not "iso8601" and not equal to
  // dateTimeFormat.[[Calendar]], then
  DirectHandle<JSReceiver> calendar_override;
  if (!String::Equals(isolate, calendar,
                      isolate->factory()->iso8601_string()) &&
      !String::Equals(isolate, calendar, date_time_format_calendar)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalid,
                      isolate->factory()->calendar_string(), calendar),
        Nothing<DateTimeValueRecord>());
  }
  // 5. Let timeZone be ? ToString(zonedDateTime.[[TimeZone]]).
  DirectHandle<String> time_zone;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, time_zone,
      Object::ToString(isolate,
                       direct_handle(zoned_date_time->time_zone(), isolate)),
      Nothing<DateTimeValueRecord>());
  // 6. If dateTimeFormat.[[TimeZone]] is not equal to DefaultTimeZone(), and
  // timeZone is not equal to dateTimeFormat.[[TimeZone]], then
  DirectHandle<Object> date_time_format_time_zone =
      GetTimeZone(isolate, date_time_format);
  DCHECK(IsString(*date_time_format_time_zone));
  DirectHandle<String> date_time_format_time_zone_string =
      Cast<String>(date_time_format_time_zone);
  if (!String::Equals(isolate, date_time_format_time_zone_string,
                      Intl::DefaultTimeZone(isolate)) &&
      !String::Equals(isolate, time_zone, date_time_format_time_zone_string)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalid,
                      isolate->factory()->timeZone_string(), time_zone),
        Nothing<DateTimeValueRecord>());
  }
  // 7. Let instant be ! CreateTemporalInstant(zonedDateTime.[[Nanoseconds]]).
  DirectHandle<JSTemporalInstant> instant =
      temporal::CreateTemporalInstant(
          isolate, direct_handle(zoned_date_time->nanoseconds(), isolate))
          .ToHandleChecked();
  // 8. If pattern is null, throw a TypeError exception.

  // 9. Return the Record { [[pattern]]: pattern.[[pattern]], [[rangePatterns]]:
  // pattern.[[rangePatterns]], [[epochNanoseconds]]: instant.[[Nanoseconds]] }.
  return Just(
      TemporalInstantToRecord(isolate, instant, PatternKind::kZonedDateTime));
}

// #sec-temporal-handledatetimevaluetemporalinstant
Maybe<DateTimeValueRecord> HandleDateTimeTemporalInstant(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<JSTemporalInstant> instant, const char* method_name) {
  // 1. Assert: instant has an [[InitializedTemporalInstant]] internal slot.
  // 2. Let pattern be dateTimeFormat.[[TemporalInstantPattern]].
  // 3. If pattern is null, throw a TypeError exception.

  // 4. Return the Record { [[pattern]]: pattern.[[pattern]], [[rangePatterns]]:
  // pattern.[[rangePatterns]], [[epochNanoseconds]]: instant.[[Nanoseconds]] }.
  return Just(TemporalInstantToRecord(isolate, instant, PatternKind::kInstant));
}

// #sec-temporal-handledatetimevaluetemporaltime
Maybe<DateTimeValueRecord> HandleDateTimeTemporalTime(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<JSTemporalPlainTime> temporal_time, const char* method_name) {
  // 1. Assert: temporalTime has an [[InitializedTemporalTime]] internal slot.
  // 2. Let pattern be dateTimeFormat.[[TemporalPlainTimePattern]].

  // 3. Let isoCalendar be ! GetISO8601Calendar().

  DirectHandle<JSReceiver> iso_calendar = temporal::GetISO8601Calendar(isolate);
  // 4. Let plainDateTime be ? CreateTemporalDateTime(1970, 1, 1,
  // temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]],
  // temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]],
  // temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]],
  // isoCalendar).
  DirectHandle<JSTemporalPlainDateTime> plain_date_time;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, plain_date_time,
      temporal::CreateTemporalDateTime(
          isolate,
          {{1970, 1, 1},
           {temporal_time->iso_hour(), temporal_time->iso_minute(),
            temporal_time->iso_second(), temporal_time->iso_millisecond(),
            temporal_time->iso_microsecond(), temporal_time->iso_nanosecond()}},
          iso_calendar),
      Nothing<DateTimeValueRecord>());
  return TemporalPlainDateTimeToRecord(isolate, date_time_format,
                                       PatternKind::kPlainTime, plain_date_time,
                                       method_name);
}

template <typename T>
Maybe<DateTimeValueRecord> HandleDateTimeTemporalYearMonthOrMonthDay(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar, PatternKind kind,
    DirectHandle<T> temporal, const char* method_name) {
  // 3. Let calendar be ? ToString(temporalYearMonth.[[Calendar]]).
  DirectHandle<String> calendar;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, calendar,
      Object::ToString(isolate, direct_handle(temporal->calendar(), isolate)),
      Nothing<DateTimeValueRecord>());
  // 4. If calendar is not equal to dateTimeFormat.[[Calendar]], then
  // https://github.com/tc39/proposal-temporal/issues/2364
  if (!String::Equals(isolate, calendar, date_time_format_calendar)) {
    // a. Throw a RangeError exception.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kInvalid,
                      isolate->factory()->calendar_string(), calendar),
        Nothing<DateTimeValueRecord>());
  }

  return TemporalToRecord<T>(isolate, date_time_format, kind, temporal,
                             direct_handle(temporal->calendar(), isolate),
                             method_name);
}

// #sec-temporal-handledatetimevaluetemporalyearmonth
Maybe<DateTimeValueRecord> HandleDateTimeTemporalYearMonth(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar,
    DirectHandle<JSTemporalPlainYearMonth> temporal_year_month,
    const char* method_name) {
  return HandleDateTimeTemporalYearMonthOrMonthDay<JSTemporalPlainYearMonth>(
      isolate, date_time_format, date_time_format_calendar,
      PatternKind::kPlainYearMonth, temporal_year_month, method_name);
}

// #sec-temporal-handledatetimevaluetemporalmonthday
Maybe<DateTimeValueRecord> HandleDateTimeTemporalMonthDay(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar,
    DirectHandle<JSTemporalPlainMonthDay> temporal_month_day,
    const char* method_name) {
  return HandleDateTimeTemporalYearMonthOrMonthDay<JSTemporalPlainMonthDay>(
      isolate, date_time_format, date_time_format_calendar,
      PatternKind::kPlainMonthDay, temporal_month_day, method_name);
}

// #sec-temporal-handledatetimeothers
Maybe<DateTimeValueRecord> HandleDateTimeOthers(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<Object> x_obj, const char* method_name) {
  // 1. Assert: ! IsTemporalObject(x) is false.
  DCHECK(!IsTemporalObject(x_obj));
  // 2. Let pattern be dateTimeFormat.[[Pattern]].

  // 3. Let rangePatterns be dateTimeFormat.[[RangePatterns]].

  // 4. If x is undefined, then
  double x;
  if (IsUndefined(*x_obj)) {
    // a. Set x to ! Call(%Date.now%, undefined).
    x = static_cast<double>(JSDate::CurrentTimeValue(isolate));
    // 5. Else,
  } else {
    // a. Set x to ? ToNumber(x).
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, x_obj,
                                     Object::ToNumber(isolate, x_obj),
                                     Nothing<DateTimeValueRecord>());
    x = Object::NumberValue(*x_obj);
  }
  // 6. Set x to TimeClip(x).
  // 7. If x is NaN, throw a RangeError exception.
  if (!DateCache::TryTimeClip(&x)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NewRangeError(MessageTemplate::kInvalidTimeValue),
        Nothing<DateTimeValueRecord>());
  }

  // 8. Let epochNanoseconds be ℤ(x) × 10^6ℤ.

  // 9. Return the Record { [[pattern]]: pattern, [[rangePatterns]]:
  // rangePatterns, [[epochNanoseconds]]: epochNanoseconds }.
  return Just(DateTimeValueRecord({x, PatternKind::kDate}));
}

// #sec-temporal-handledatetimevalue
Maybe<DateTimeValueRecord> HandleDateTimeValue(
    Isolate* isolate, const icu::SimpleDateFormat& date_time_format,
    DirectHandle<String> date_time_format_calendar, DirectHandle<Object> x,
    const char* method_name) {
  if (IsTemporalObject(x)) {
    // a. If x has an [[InitializedTemporalDate]] internal slot, then
    if (IsJSTemporalPlainDate(*x)) {
      // i. Return ? HandleDateTimeTemporalDate(dateTimeFormat, x).
      return HandleDateTimeTemporalDate(
          isolate, date_time_format, date_time_format_calendar,
          Cast<JSTemporalPlainDate>(x), method_name);
    }
    // b. If x has an [[InitializedTemporalYearMonth]] internal slot, then
    if (IsJSTemporalPlainYearMonth(*x)) {
      // i. Return ? HandleDateTimeTemporalYearMonth(dateTimeFormat, x).
      return HandleDateTimeTemporalYearMonth(
          isolate, date_time_format, date_time_format_calendar,
          Cast<JSTemporalPlainYearMonth>(x), method_name);
    }
    // c. If x has an [[InitializedTemporalMonthDay]] internal slot, then
    if (IsJSTemporalPlainMonthDay(*x)) {
      // i. Return ? HandleDateTimeTemporalMonthDay(dateTimeFormat, x).
      return HandleDateTimeTemporalMonthDay(
          isolate, date_time_format, date_time_format_calendar,
          Cast<JSTemporalPlainMonthDay>(x), method_name);
    }
    // d. If x has an [[InitializedTemporalTime]] internal slot, then
    if (IsJSTemporalPlainTime(*x)) {
      // i. Return ? HandleDateTimeTemporalTime(dateTimeFormat, x).
      return HandleDateTimeTemporalTime(
          isolate, date_time_format, Cast<JSTemporalPlainTime>(x), method_name);
    }
    // e. If x has an [[InitializedTemporalDateTime]] internal slot, then
    if (IsJSTemporalPlainDateTime(*x)) {
      // i. Return ? HandleDateTimeTemporalDateTime(dateTimeFormat, x).
      return HandleDateTimeTemporalDateTime(
          isolate, date_time_format, date_time_format_calendar,
          Cast<JSTemporalPlainDateTime>(x), method_name);
    }
    // f. If x has an [[InitializedTemporalInstant]] internal slot, then
    if (IsJSTemporalInstant(*x)) {
      // i. Return ? HandleDateTimeTemporalInstant(dateTimeFormat, x).
      return HandleDateTimeTemporalInstant(
          isolate, date_time_format, Cast<JSTemporalInstant>(x), method_name);
    }
    // g. Assert: x has an [[InitializedTemporalZonedDateTime]] internal slot.
    DCHECK(IsJSTemporalZonedDateTime(*x));
    // h. Return ? HandleDateTimeTemporalZonedDateTime(dateTimeFormat, x).
    return HandleDateTimeTemporalZonedDateTime(
        isolate, date_time_format, date_time_format_calendar,
        Cast<JSTemporalZonedDateTime>(x), method_name);
  }

  // 2. Return ? HandleDateTimeOthers(dateTimeFormat, x).
  return HandleDateTimeOthers(isolate, date_time_format, x, method_name);
}

// This helper function handles Supported fields and Default fields in Table 16
// ( #table-temporal-patterns ).  It remove all the fields not stated in keep
// from input, and add the fields in add_default if a skeleton in the same
// category is in the input, with considering the equivalent.
// For example, if input is "yyyyMMhm", keep is {y,M,d} and add_default is
// {y,M,d}, the output will be "yyyyMMd". For example, if input is
// "yyyyMMhmOOOO", keep is {h,m,s,z,O,v} and add_default is {h,m,s}, then the
// output will be "hmOOOOs". The meaning of the skeleton letters is stated in
// UTS35
// https://www.unicode.org/reports/tr35/tr35-dates.html#table-date-field-symbol-table
icu::UnicodeString KeepSupportedAddDefault(
    const icu::UnicodeString& input, const std::set<char16_t>& keep,
    const std::set<char16_t>& add_default) {
  const std::map<char16_t, char16_t> equivalent({{'L', 'M'},
                                                 {'h', 'j'},
                                                 {'H', 'j'},
                                                 {'k', 'j'},
                                                 {'K', 'j'},
                                                 {'O', 'z'},
                                                 {'v', 'z'}});
  std::set<char16_t> to_be_added(add_default);
  icu::UnicodeString result;
  for (int32_t i = 0; i < input.length(); i++) {
    char16_t ch = input.charAt(i);
    if (keep.find(ch) != keep.end()) {
      to_be_added.erase(ch);
      auto also = equivalent.find(ch);
      if (also != equivalent.end()) {
        to_be_added.erase(also->second);
      }
      result.append(ch);
    }
  }
  for (auto it = to_be_added.begin(); it != to_be_added.end(); ++it) {
    result.append(*it);
  }
  return result;
}

icu::UnicodeString GetSkeletonForPatternKind(const icu::UnicodeString& input,
                                             PatternKind kind) {
  // [[weekday]] skeleton could be one or more 'E' or 'c'.
  // [[era]] skeleton could be one or more 'G'.
  // [[year]] skeleton could be one or more 'y'.
  // [[month]] skeleton could be one or more 'M' or 'L'.
  // [[day]] skeleton could be one or more 'd'.
  // [[hour]] skeleton could be one or more 'h', 'H', 'k', 'K', or 'j'.
  // [[minute]] skeleton could be one or more 'm'.
  // [[second]] skeleton could be one or more 's'.
  // [[dayPeriod]] skeleton could be one or more 'b', 'B' or 'a'.
  // [[fractionalSecondDigits]] skeleton could be one or more 'S'.
  // [[timeZoneName]] skeleton could be one or more 'z', 'O', or 'v'.

  switch (kind) {
    case PatternKind::kDate:
      return input;
    case PatternKind::kPlainDate:
      return KeepSupportedAddDefault(
          // Supported fields: [[weekday]], [[era]], [[year]], [[month]],
          // [[day]]
          input, {'E', 'c', 'G', 'y', 'M', 'L', 'd'},
          // Default fields: [[year]], [[month]], [[day]]
          {'y', 'M', 'd'});
    case PatternKind::kPlainYearMonth:
      return KeepSupportedAddDefault(
          // Supported fields: [[era]], [[year]], [[month]]
          input, {'G', 'y', 'M', 'L'},
          // Default fields: [[year]], [[month]]
          {'y', 'M'});
    case PatternKind::kPlainMonthDay:
      return KeepSupportedAddDefault(
          // Supported fields: [[month]] [[day]]
          input, {'M', 'L', 'd'},
          // Default fields: [[month]] [[day]]
          {'M', 'd'});

    case PatternKind::kPlainTime:
      return KeepSupportedAddDefault(
          input,
          // Supported fields: [[hour]], [[minute]], [[second]], [[dayPeriod]],
          // [[fractionalSecondDigits]]
          {'h', 'H', 'k', 'K', 'j', 'm', 's', 'B', 'b', 'a', 'S'},
          // Default fields:  [[hour]], [[minute]],
          // [[second]]
          {'j', 'm', 's'});

    case PatternKind::kPlainDateTime:
      // Row TemporalInstantPattern is the same as TemporalPlainDateTimePattern
      // in Table 16: Supported fields for Temporal patterns
      // #table-temporal-patterns
      [[fallthrough]];
    case PatternKind::kInstant:
      return KeepSupportedAddDefault(
          input,
          // Supported fields: [[weekday]], [[era]], [[year]], [[month]],
          // [[day]], [[hour]], [[minute]], [[second]], [[dayPeriod]],
          // [[fractionalSecondDigits]]
          {'E', 'c', 'G', 'y', 'M', 'L', 'd', 'h', 'H', 'k', 'K', 'j', 'm', 's',
           'B', 'b', 'a', 'S'},
          // Default fields: [[year]], [[month]], [[day]], [[hour]], [[minute]],
          // [[second]]
          {'y', 'M', 'd', 'j', 'm', 's'});

    case PatternKind::kZonedDateTime:
      return KeepSupportedAddDefault(
          // Supported fields: [[weekday]], [[era]], [[year]], [[month]],
          // [[day]], [[hour]], [[minute]], [[second]], [[dayPeriod]],
          // [[fractionalSecondDigits]], [[timeZoneName]]
          input, {'E', 'c', 'G', 'y', 'M', 'L', 'd', 'h', 'H', 'k', 'K',
                  'j', 'm', 's', 'B', 'b', 'a', 'S', 'z', 'O', 'v'},
          // Default fields: [[year]], [[month]], [[day]], [[hour]], [[minute]],
          // [[second]], [[timeZoneName]]
          {'y', 'M', 'd', 'j', 'm', 's', 'z'});
  }
}

icu::UnicodeString SkeletonFromDateFormat(
    const icu::SimpleDateFormat& icu_date_format) {
  icu::UnicodeString pattern;
  pattern = icu_date_format.toPattern(pattern);

  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString skeleton =
      icu::DateTimePatternGenerator::staticGetSkeleton(pattern, status);
  DCHECK(U_SUCCESS(status));
  return skeleton;
}

std::unique_ptr<icu::SimpleDateFormat> GetSimpleDateTimeForTemporal(
    const icu::SimpleDateFormat& date_format, PatternKind kind) {
  DCHECK_NE(kind, PatternKind::kDate);
  icu::UnicodeString skeleton =
      GetSkeletonForPatternKind(SkeletonFromDateFormat(date_format), kind);
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::SimpleDateFormat> result(
      static_cast<icu::SimpleDateFormat*>(
          icu::DateFormat::createInstanceForSkeleton(
              skeleton, date_format.getSmpFmtLocale(), status)));
  DCHECK(result);
  DCHECK(U_SUCCESS(status));
  result->setTimeZone(date_format.getTimeZone());
  return result;
}

icu::UnicodeString CallICUFormat(const icu::SimpleDateFormat& date_format,
                                 PatternKind kind, double time_in_milliseconds,
                                 icu::FieldPositionIterator* fp_iter,
                                 UErrorCode& status) {
  icu::UnicodeString result;
  // Use the date_format directly for Date value.
  if (kind == PatternKind::kDate) {
    date_format.format(time_in_milliseconds, result, fp_iter, status);
    return result;
  }
  // For other Temporal objects, lazy generate a SimpleDateFormat for the kind.
  std::unique_ptr<icu::SimpleDateFormat> pattern(
      GetSimpleDateTimeForTemporal(date_format, kind));
  pattern->format(time_in_milliseconds, result, fp_iter, status);
  return result;
}

// ecma402/#sec-formatdatetime
// FormatDateTime( dateTimeFormat, x )
MaybeDirectHandle<String> FormatDateTime(
    Isolate* isolate, const icu::SimpleDateFormat& date_format, double x) {
  if (!DateCache::TryTimeClip(&x)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeValue));
  }

  icu::UnicodeString result;
  date_format.format(x, result);

  // Revert ICU 72 change that introduced U+202F instead of U+0020
  // to separate time from AM/PM. See https://crbug.com/1414292.
  result = result.findAndReplace(icu::UnicodeString(0x202f),
                                 icu::UnicodeString(0x20));

  return Intl::ToString(isolate, result);
}

MaybeDirectHandle<String> FormatMillisecondsByKindToString(
    Isolate* isolate, const icu::SimpleDateFormat& date_format,
    PatternKind kind, double x) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result =
      CallICUFormat(date_format, kind, x, nullptr, status);
  DCHECK(U_SUCCESS(status));

  return Intl::ToString(isolate, result);
}

MaybeDirectHandle<String> FormatDateTimeWithTemporalSupport(
    Isolate* isolate, const icu::SimpleDateFormat& date_format,
    DirectHandle<String> date_time_format_calendar, DirectHandle<Object> x,
    const char* method_name) {
  DateTimeValueRecord record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, record,
      HandleDateTimeValue(isolate, date_format, date_time_format_calendar, x,
                          method_name),
      {});
  return FormatMillisecondsByKindToString(isolate, date_format, record.kind,
                                          record.epoch_milliseconds);
}

MaybeDirectHandle<String> FormatDateTimeWithTemporalSupport(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x, const char* method_name) {
  return FormatDateTimeWithTemporalSupport(
      isolate, *(date_time_format->icu_simple_date_format()->raw()),
      JSDateTimeFormat::Calendar(isolate, date_time_format), x, method_name);
}

}  // namespace

// ecma402/#sec-datetime-format-functions
// DateTime Format Functions
MaybeDirectHandle<String> JSDateTimeFormat::DateTimeFormat(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> date, const char* method_name) {
  // 2. Assert: Type(dtf) is Object and dtf has an [[InitializedDateTimeFormat]]
  // internal slot.
  if (v8_flags.harmony_temporal) {
    return FormatDateTimeWithTemporalSupport(isolate, date_time_format, date,
                                             method_name);
  }

  // 3. If date is not provided or is undefined, then
  double x;
  if (IsUndefined(*date)) {
    // 3.a Let x be Call(%Date_now%, undefined).
    x = static_cast<double>(JSDate::CurrentTimeValue(isolate));
  } else {
    // 4. Else,
    //    a. Let x be ? ToNumber(date).
    ASSIGN_RETURN_ON_EXCEPTION(isolate, date, Object::ToNumber(isolate, date));
    DCHECK(IsNumber(*date));
    x = Object::NumberValue(*date);
  }
  // 5. Return FormatDateTime(dtf, x).
  icu::SimpleDateFormat* format =
      date_time_format->icu_simple_date_format()->raw();
  return FormatDateTime(isolate, *format, x);
}

namespace {
Isolate::ICUObjectCacheType ConvertToCacheType(
    JSDateTimeFormat::DefaultsOption type) {
  switch (type) {
    case JSDateTimeFormat::DefaultsOption::kDate:
      return Isolate::ICUObjectCacheType::kDefaultSimpleDateFormatForDate;
    case JSDateTimeFormat::DefaultsOption::kTime:
      return Isolate::ICUObjectCacheType::kDefaultSimpleDateFormatForTime;
    case JSDateTimeFormat::DefaultsOption::kAll:
      return Isolate::ICUObjectCacheType::kDefaultSimpleDateFormat;
  }
}

}  // namespace

MaybeDirectHandle<String> JSDateTimeFormat::ToLocaleDateTime(
    Isolate* isolate, DirectHandle<Object> date, DirectHandle<Object> locales,
    DirectHandle<Object> options, RequiredOption required,
    DefaultsOption defaults, const char* method_name) {
  Isolate::ICUObjectCacheType cache_type = ConvertToCacheType(defaults);

  Factory* factory = isolate->factory();
  // 1. Let x be ? thisTimeValue(this value);
  if (!IsJSDate(*date)) {
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kMethodInvokedOnWrongType,
                                 factory->Date_string()));
  }
  double const x = Cast<JSDate>(date)->value();
  // 2. If x is NaN, return "Invalid Date"
  if (std::isnan(x)) {
    return factory->Invalid_Date_string();
  }

  // We only cache the instance when locales is a string/undefined and
  // options is undefined, as that is the only case when the specified
  // side-effects of examining those arguments are unobservable.
  bool can_cache = (IsString(*locales) || IsUndefined(*locales, isolate)) &&
                   IsUndefined(*options, isolate);
  if (can_cache) {
    // Both locales and options are undefined, check the cache.
    icu::SimpleDateFormat* cached_icu_simple_date_format =
        static_cast<icu::SimpleDateFormat*>(
            isolate->get_cached_icu_object(cache_type, locales));
    if (cached_icu_simple_date_format != nullptr) {
      return FormatDateTime(isolate, *cached_icu_simple_date_format, x);
    }
  }
  // 4. Let dateFormat be ? Construct(%DateTimeFormat%, « locales, options »).
  DirectHandle<JSFunction> constructor(
      Cast<JSFunction>(isolate->context()
                           ->native_context()
                           ->intl_date_time_format_function()),
      isolate);
  DirectHandle<Map> map;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, map,
      JSFunction::GetDerivedMap(isolate, constructor, constructor));
  DirectHandle<JSDateTimeFormat> date_time_format;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_format,
      JSDateTimeFormat::CreateDateTimeFormat(isolate, map, locales, options,
                                             required, defaults, method_name));

  if (can_cache) {
    isolate->set_icu_object_in_cache(
        cache_type, locales,
        std::static_pointer_cast<icu::UMemory>(
            date_time_format->icu_simple_date_format()->get()));
  }
  // 5. Return FormatDateTime(dateFormat, x).
  icu::SimpleDateFormat* format =
      date_time_format->icu_simple_date_format()->raw();
  return FormatDateTime(isolate, *format, x);
}

MaybeDirectHandle<String> JSDateTimeFormat::TemporalToLocaleString(
    Isolate* isolate, DirectHandle<JSReceiver> x, DirectHandle<Object> locales,
    DirectHandle<Object> options, const char* method_name) {
  // 4. Let dateFormat be ? Construct(%DateTimeFormat%, « locales, options »).
  DirectHandle<JSFunction> constructor(
      isolate->context()->native_context()->intl_date_time_format_function(),
      isolate);
  DirectHandle<Map> map =
      JSFunction::GetDerivedMap(isolate, constructor, constructor)
          .ToHandleChecked();
  DirectHandle<JSDateTimeFormat> date_time_format;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, date_time_format,
      JSDateTimeFormat::New(isolate, map, locales, options, method_name));

  // 5. Return FormatDateTime(dateFormat, x).
  return FormatDateTimeWithTemporalSupport(isolate, date_time_format, x,
                                           method_name);
}

MaybeDirectHandle<JSDateTimeFormat> JSDateTimeFormat::UnwrapDateTimeFormat(
    Isolate* isolate, Handle<JSReceiver> format_holder) {
  DirectHandle<Context> native_context(isolate->context()->native_context(),
                                       isolate);
  DirectHandle<JSFunction> constructor(
      Cast<JSFunction>(native_context->intl_date_time_format_function()),
      isolate);
  DirectHandle<Object> dtf;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, dtf,
      Intl::LegacyUnwrapReceiver(isolate, format_holder, constructor,
                                 IsJSDateTimeFormat(*format_holder)));
  // 2. If Type(dtf) is not Object or dtf does not have an
  //    [[InitializedDateTimeFormat]] internal slot, then
  if (!IsJSDateTimeFormat(*dtf)) {
    // a. Throw a TypeError exception.
    THROW_NEW_ERROR(isolate,
                    NewTypeError(MessageTemplate::kIncompatibleMethodReceiver,
                                 isolate->factory()->NewStringFromAsciiChecked(
                                     "UnwrapDateTimeFormat"),
                                 format_holder));
  }
  // 3. Return dtf.
  return Cast<JSDateTimeFormat>(dtf);
}

// Convert the input in the form of
// [+-\u2212]hh:?mm  to the ID acceptable for SimpleTimeZone
// GMT[+-]hh or GMT[+-]hh:mm or empty
std::optional<std::string> GetOffsetTimeZone(Isolate* isolate,
                                             DirectHandle<String> time_zone) {
  time_zone = String::Flatten(isolate, time_zone);
  DisallowGarbageCollection no_gc;
  const String::FlatContent& flat = time_zone->GetFlatContent(no_gc);
  int32_t len = flat.length();
  if (len < 3) {
    // Error
    return std::nullopt;
  }
  std::string tz("GMT");
  switch (flat.Get(0)) {
    case 0x2212:
    case '-':
      tz += '-';
      break;
    case '+':
      tz += '+';
      break;
    default:
      // Error
      return std::nullopt;
  }
  // 00 - 23
  uint16_t h0 = flat.Get(1);
  uint16_t h1 = flat.Get(2);

  if ((h0 >= '0' && h0 <= '1' && h1 >= '0' && h1 <= '9') ||
      (h0 == '2' && h1 >= '0' && h1 <= '3')) {
    tz += h0;
    tz += h1;
  } else {
    // Error
    return std::nullopt;
  }
  if (len == 3) {
    return tz;
  }
  int32_t p = 3;
  uint16_t m0 = flat.Get(p);
  if (m0 == ':') {
    // Ignore ':'
    p++;
    if (len == p) {
      // Error
      return std::nullopt;
    }
    m0 = flat.Get(p);
  }
  if (len - p != 2) {
    // Error
    return std::nullopt;
  }
  uint16_t m1 = flat.Get(p + 1);
  if (m0 >= '0' && m0 <= '5' && m1 >= '0' && m1 <= '9') {
    tz += m0;
    tz += m1;
    return tz;
  }
  // Error
  return std::nullopt;
}
std::unique_ptr<icu::TimeZone> JSDateTimeFormat::CreateTimeZone(
    Isolate* isolate, DirectHandle<String> time_zone_string) {
  // Create time zone as specified by the user. We have to re-create time zone
  // since calendar takes ownership.
  std::optional<std::string> offsetTimeZone =
      GetOffsetTimeZone(isolate, time_zone_string);
  if (offsetTimeZone.has_value()) {
    std::unique_ptr<icu::TimeZone> tz(
        icu::TimeZone::createTimeZone(offsetTimeZone->c_str()));
    return tz;
  }
  std::unique_ptr<char[]> time_zone = time_zone_string->ToCString();
  std::string canonicalized = CanonicalizeTimeZoneID(time_zone.get());
  if (canonicalized.empty()) return std::unique_ptr<icu::TimeZone>();
  std::unique_ptr<icu::TimeZone> tz(
      icu::TimeZone::createTimeZone(canonicalized.c_str()));
  // 18.b If the result of IsValidTimeZoneName(timeZone) is false, then
  // i. Throw a RangeError exception.
  if (!Intl::IsValidTimeZoneName(*tz)) return std::unique_ptr<icu::TimeZone>();
  return tz;
}

namespace {

class CalendarCache {
 public:
  icu::Calendar* CreateCalendar(const icu::Locale& locale, icu::TimeZone* tz) {
    icu::UnicodeString tz_id;
    tz->getID(tz_id);
    std::string key;
    tz_id.toUTF8String<std::string>(key);
    key += ":";
    key += locale.getName();

    base::MutexGuard guard(&mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      delete tz;
      return it->second->clone();
    }
    // Create a calendar using locale, and apply time zone to it.
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::Calendar> calendar(
        icu::Calendar::createInstance(tz, locale, status));
    DCHECK(U_SUCCESS(status));
    DCHECK_NOT_NULL(calendar.get());

    if (calendar->getDynamicClassID() ==
            icu::GregorianCalendar::getStaticClassID() ||
        strcmp(calendar->getType(), "iso8601") == 0) {
      icu::GregorianCalendar* gc =
          static_cast<icu::GregorianCalendar*>(calendar.get());
      status = U_ZERO_ERROR;
      // The beginning of ECMAScript time, namely -(2**53)
      const double start_of_time = -9007199254740992;
      gc->setGregorianChange(start_of_time, status);
      DCHECK(U_SUCCESS(status));
    }

    if (map_.size() > 8) {  // Cache at most 8 calendars.
      map_.clear();
    }
    map_[key] = std::move(calendar);
    return map_[key]->clone();
  }

 private:
  std::map<std::string, std::unique_ptr<icu::Calendar>> map_;
  base::Mutex mutex_;
};

icu::Calendar* CreateCalendar(Isolate* isolate, const icu::Locale& icu_locale,
                              icu::TimeZone* tz) {
  static base::LazyInstance<CalendarCache>::type calendar_cache =
      LAZY_INSTANCE_INITIALIZER;
  return calendar_cache.Pointer()->CreateCalendar(icu_locale, tz);
}

icu::UnicodeString ReplaceHourCycleInPattern(icu::UnicodeString pattern,
                                             JSDateTimeFormat::HourCycle hc) {
  char16_t replacement;
  switch (hc) {
    case JSDateTimeFormat::HourCycle::kUndefined:
      return pattern;
    case JSDateTimeFormat::HourCycle::kH11:
      replacement = 'K';
      break;
    case JSDateTimeFormat::HourCycle::kH12:
      replacement = 'h';
      break;
    case JSDateTimeFormat::HourCycle::kH23:
      replacement = 'H';
      break;
    case JSDateTimeFormat::HourCycle::kH24:
      replacement = 'k';
      break;
  }
  bool replace = true;
  icu::UnicodeString result;
  char16_t last = u'\0';
  for (int32_t i = 0; i < pattern.length(); i++) {
    char16_t ch = pattern.charAt(i);
    switch (ch) {
      case '\'':
        replace = !replace;
        result.append(ch);
        break;
      case 'H':
        [[fallthrough]];
      case 'h':
        [[fallthrough]];
      case 'K':
        [[fallthrough]];
      case 'k':
        // If the previous field is a day, add a space before the hour.
        if (replace && last == u'd') {
          result.append(' ');
        }
        result.append(replace ? replacement : ch);
        break;
      default:
        result.append(ch);
        break;
    }
    last = ch;
  }
  return result;
}

std::unique_ptr<icu::SimpleDateFormat> CreateICUDateFormat(
    const icu::Locale& icu_locale, const icu::UnicodeString& skeleton,
    icu::DateTimePatternGenerator* generator, JSDateTimeFormat::HourCycle hc) {
  // See https://github.com/tc39/ecma402/issues/225 . The best pattern
  // generation needs to be done in the base locale according to the
  // current spec however odd it may be. See also crbug.com/826549 .
  // This is a temporary work-around to get v8's external behavior to match
  // the current spec, but does not follow the spec provisions mentioned
  // in the above Ecma 402 issue.
  // TODO(jshin): The spec may need to be revised because using the base
  // locale for the pattern match is not quite right. Moreover, what to
  // do with 'related year' part when 'chinese/dangi' calendar is specified
  // has to be discussed. Revisit once the spec is clarified/revised.
  icu::UnicodeString pattern;
  UErrorCode status = U_ZERO_ERROR;
  pattern = generator->getBestPattern(skeleton, UDATPG_MATCH_HOUR_FIELD_LENGTH,
                                      status);
  pattern = ReplaceHourCycleInPattern(pattern, hc);
  DCHECK(U_SUCCESS(status));

  // Make formatter from skeleton. Calendar and numbering system are added
  // to the locale as Unicode extension (if they were specified at all).
  status = U_ZERO_ERROR;
  std::unique_ptr<icu::SimpleDateFormat> date_format(
      new icu::SimpleDateFormat(pattern, icu_locale, status));
  if (U_FAILURE(status)) return std::unique_ptr<icu::SimpleDateFormat>();

  DCHECK_NOT_NULL(date_format.get());
  return date_format;
}

class DateFormatCache {
 public:
  icu::SimpleDateFormat* Create(const icu::Locale& icu_locale,
                                const icu::UnicodeString& skeleton,
                                icu::DateTimePatternGenerator* generator,
                                JSDateTimeFormat::HourCycle hc) {
    std::string key;
    skeleton.toUTF8String<std::string>(key);
    key += ":";
    key += icu_locale.getName();

    base::MutexGuard guard(&mutex_);
    auto it = map_.find(key);
    if (it != map_.end()) {
      return static_cast<icu::SimpleDateFormat*>(it->second->clone());
    }

    if (map_.size() > 8) {  // Cache at most 8 DateFormats.
      map_.clear();
    }
    std::unique_ptr<icu::SimpleDateFormat> instance(
        CreateICUDateFormat(icu_locale, skeleton, generator, hc));
    if (instance == nullptr) return nullptr;
    map_[key] = std::move(instance);
    return static_cast<icu::SimpleDateFormat*>(map_[key]->clone());
  }

 private:
  std::map<std::string, std::unique_ptr<icu::SimpleDateFormat>> map_;
  base::Mutex mutex_;
};

std::unique_ptr<icu::SimpleDateFormat> CreateICUDateFormatFromCache(
    const icu::Locale& icu_locale, const icu::UnicodeString& skeleton,
    icu::DateTimePatternGenerator* generator, JSDateTimeFormat::HourCycle hc) {
  static base::LazyInstance<DateFormatCache>::type cache =
      LAZY_INSTANCE_INITIALIZER;
  return std::unique_ptr<icu::SimpleDateFormat>(
      cache.Pointer()->Create(icu_locale, skeleton, generator, hc));
}

// We treat PatternKind::kDate different than other because most of the
// pre-existing usage are using the formatter with Date() and Temporal is
// new and not yet adopted by the web yet. We try to optimize the performance
// and memory usage for the pre-existing code so we cache for it.
// We may later consider caching Temporal one also if the usage increase.
// Right now we want to avoid making the constructor more expensive and
// increasing overhead in the object.
std::unique_ptr<icu::DateIntervalFormat> LazyCreateDateIntervalFormat(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    PatternKind kind) {
  Tagged<Managed<icu::DateIntervalFormat>> managed_format =
      date_time_format->icu_date_interval_format();
  if (kind == PatternKind::kDate && managed_format->get()) {
    return std::unique_ptr<icu::DateIntervalFormat>(
        managed_format->raw()->clone());
  }
  UErrorCode status = U_ZERO_ERROR;

  icu::Locale loc = *(date_time_format->icu_locale()->raw());
  // We need to pass in the hc to DateIntervalFormat by using Unicode 'hc'
  // extension.
  std::string hcString = ToHourCycleString(date_time_format->hour_cycle());
  if (!hcString.empty()) {
    loc.setUnicodeKeywordValue("hc", hcString, status);
  }

  icu::SimpleDateFormat* icu_simple_date_format =
      date_time_format->icu_simple_date_format()->raw();

  icu::UnicodeString skeleton = GetSkeletonForPatternKind(
      SkeletonFromDateFormat(*icu_simple_date_format), kind);

  std::unique_ptr<icu::DateIntervalFormat> date_interval_format(
      icu::DateIntervalFormat::createInstance(skeleton, loc, status));
  DCHECK(U_SUCCESS(status));
  date_interval_format->setTimeZone(icu_simple_date_format->getTimeZone());
  if (kind != PatternKind::kDate) {
    return date_interval_format;
  }
  DirectHandle<Managed<icu::DateIntervalFormat>> managed_interval_format =
      Managed<icu::DateIntervalFormat>::From(isolate, 0,
                                             std::move(date_interval_format));
  date_time_format->set_icu_date_interval_format(*managed_interval_format);
  return std::unique_ptr<icu::DateIntervalFormat>(
      managed_interval_format->raw()->clone());
}

JSDateTimeFormat::HourCycle HourCycleFromPattern(
    const icu::UnicodeString pattern) {
  bool in_quote = false;
  for (int32_t i = 0; i < pattern.length(); i++) {
    char16_t ch = pattern[i];
    switch (ch) {
      case '\'':
        in_quote = !in_quote;
        break;
      case 'K':
        if (!in_quote) return JSDateTimeFormat::HourCycle::kH11;
        break;
      case 'h':
        if (!in_quote) return JSDateTimeFormat::HourCycle::kH12;
        break;
      case 'H':
        if (!in_quote) return JSDateTimeFormat::HourCycle::kH23;
        break;
      case 'k':
        if (!in_quote) return JSDateTimeFormat::HourCycle::kH24;
        break;
    }
  }
  return JSDateTimeFormat::HourCycle::kUndefined;
}

icu::DateFormat::EStyle DateTimeStyleToEStyle(
    JSDateTimeFormat::DateTimeStyle style) {
  switch (style) {
    case JSDateTimeFormat::DateTimeStyle::kFull:
      return icu::DateFormat::EStyle::kFull;
    case JSDateTimeFormat::DateTimeStyle::kLong:
      return icu::DateFormat::EStyle::kLong;
    case JSDateTimeFormat::DateTimeStyle::kMedium:
      return icu::DateFormat::EStyle::kMedium;
    case JSDateTimeFormat::DateTimeStyle::kShort:
      return icu::DateFormat::EStyle::kShort;
    case JSDateTimeFormat::DateTimeStyle::kUndefined:
      UNREACHABLE();
  }
}

icu::UnicodeString ReplaceSkeleton(const icu::UnicodeString input,
                                   JSDateTimeFormat::HourCycle hc) {
  icu::UnicodeString result;
  char16_t to;
  switch (hc) {
    case JSDateTimeFormat::HourCycle::kH11:
      to = 'K';
      break;
    case JSDateTimeFormat::HourCycle::kH12:
      to = 'h';
      break;
    case JSDateTimeFormat::HourCycle::kH23:
      to = 'H';
      break;
    case JSDateTimeFormat::HourCycle::kH24:
      to = 'k';
      break;
    case JSDateTimeFormat::HourCycle::kUndefined:
      UNREACHABLE();
  }
  for (int32_t i = 0; i < input.length(); i++) {
    switch (input[i]) {
      // We need to skip 'a', 'b', 'B' here due to
      // https://unicode-org.atlassian.net/browse/ICU-20437
      case 'a':
        [[fallthrough]];
      case 'b':
        [[fallthrough]];
      case 'B':
        // ignore
        break;
      case 'h':
        [[fallthrough]];
      case 'H':
        [[fallthrough]];
      case 'K':
        [[fallthrough]];
      case 'k':
        result += to;
        break;
      default:
        result += input[i];
        break;
    }
  }
  return result;
}

std::unique_ptr<icu::SimpleDateFormat> DateTimeStylePattern(
    JSDateTimeFormat::DateTimeStyle date_style,
    JSDateTimeFormat::DateTimeStyle time_style, icu::Locale& icu_locale,
    JSDateTimeFormat::HourCycle hc, icu::DateTimePatternGenerator* generator) {
  std::unique_ptr<icu::SimpleDateFormat> result;
  if (date_style != JSDateTimeFormat::DateTimeStyle::kUndefined) {
    if (time_style != JSDateTimeFormat::DateTimeStyle::kUndefined) {
      result.reset(reinterpret_cast<icu::SimpleDateFormat*>(
          icu::DateFormat::createDateTimeInstance(
              DateTimeStyleToEStyle(date_style),
              DateTimeStyleToEStyle(time_style), icu_locale)));
    } else {
      result.reset(reinterpret_cast<icu::SimpleDateFormat*>(
          icu::DateFormat::createDateInstance(DateTimeStyleToEStyle(date_style),
                                              icu_locale)));
      // For instance without time, we do not need to worry about the hour cycle
      // impact so we can return directly.
      if (result != nullptr) {
        return result;
      }
    }
  } else {
    if (time_style != JSDateTimeFormat::DateTimeStyle::kUndefined) {
      result.reset(reinterpret_cast<icu::SimpleDateFormat*>(
          icu::DateFormat::createTimeInstance(DateTimeStyleToEStyle(time_style),
                                              icu_locale)));
    } else {
      UNREACHABLE();
    }
  }

  UErrorCode status = U_ZERO_ERROR;
  // Somehow we fail to create the instance.
  if (result.get() == nullptr) {
    // Fallback to the locale without "nu".
    if (!icu_locale.getUnicodeKeywordValue<std::string>("nu", status).empty()) {
      status = U_ZERO_ERROR;
      icu_locale.setUnicodeKeywordValue("nu", nullptr, status);
      return DateTimeStylePattern(date_style, time_style, icu_locale, hc,
                                  generator);
    }
    status = U_ZERO_ERROR;
    // Fallback to the locale without "hc".
    if (!icu_locale.getUnicodeKeywordValue<std::string>("hc", status).empty()) {
      status = U_ZERO_ERROR;
      icu_locale.setUnicodeKeywordValue("hc", nullptr, status);
      return DateTimeStylePattern(date_style, time_style, icu_locale, hc,
                                  generator);
    }
    status = U_ZERO_ERROR;
    // Fallback to the locale without "ca".
    if (!icu_locale.getUnicodeKeywordValue<std::string>("ca", status).empty()) {
      status = U_ZERO_ERROR;
      icu_locale.setUnicodeKeywordValue("ca", nullptr, status);
      return DateTimeStylePattern(date_style, time_style, icu_locale, hc,
                                  generator);
    }
    return nullptr;
  }
  icu::UnicodeString pattern;
  pattern = result->toPattern(pattern);

  status = U_ZERO_ERROR;
  icu::UnicodeString skeleton =
      icu::DateTimePatternGenerator::staticGetSkeleton(pattern, status);
  DCHECK(U_SUCCESS(status));

  // If the skeleton match the HourCycle, we just return it.
  if (hc == HourCycleFromPattern(pattern)) {
    return result;
  }

  return CreateICUDateFormatFromCache(icu_locale, ReplaceSkeleton(skeleton, hc),
                                      generator, hc);
}

class DateTimePatternGeneratorCache {
 public:
  // Return a clone copy that the caller have to free.
  icu::DateTimePatternGenerator* CreateGenerator(Isolate* isolate,
                                                 const icu::Locale& locale) {
    std::string key(locale.getName());
    base::MutexGuard guard(&mutex_);
    auto it = map_.find(key);
    icu::DateTimePatternGenerator* orig;
    if (it != map_.end()) {
      DCHECK(it->second != nullptr);
      orig = it->second.get();
    } else {
      UErrorCode status = U_ZERO_ERROR;
      orig = icu::DateTimePatternGenerator::createInstance(locale, status);
      // It may not be an U_MEMORY_ALLOCATION_ERROR.
      // Fallback to use "root".
      if (U_FAILURE(status)) {
        status = U_ZERO_ERROR;
        orig = icu::DateTimePatternGenerator::createInstance("root", status);
      }
      if (U_SUCCESS(status) && orig != nullptr) {
        map_[key].reset(orig);
      } else {
        DCHECK(status == U_MEMORY_ALLOCATION_ERROR);
        V8::FatalProcessOutOfMemory(
            isolate, "DateTimePatternGeneratorCache::CreateGenerator");
      }
    }
    icu::DateTimePatternGenerator* clone = orig ? orig->clone() : nullptr;
    if (clone == nullptr) {
      V8::FatalProcessOutOfMemory(
          isolate, "DateTimePatternGeneratorCache::CreateGenerator");
    }
    return clone;
  }

 private:
  std::map<std::string, std::unique_ptr<icu::DateTimePatternGenerator>> map_;
  base::Mutex mutex_;
};

}  // namespace

enum FormatMatcherOption { kBestFit, kBasic };

// ecma402/#sec-initializedatetimeformat
MaybeDirectHandle<JSDateTimeFormat> JSDateTimeFormat::New(
    Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
    DirectHandle<Object> input_options, const char* service) {
  return JSDateTimeFormat::CreateDateTimeFormat(
      isolate, map, locales, input_options, RequiredOption::kAny,
      DefaultsOption::kDate, service);
}

MaybeDirectHandle<JSDateTimeFormat> JSDateTimeFormat::CreateDateTimeFormat(
    Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
    DirectHandle<Object> input_options, RequiredOption required,
    DefaultsOption defaults, const char* service) {
  Factory* factory = isolate->factory();
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, {});
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();
  // 2. Let options be ? CoerceOptionsToObject(_options_).
  DirectHandle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, CoerceOptionsToObject(isolate, input_options, service));

  // 4. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 5. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, {});
  Intl::MatcherOption locale_matcher = maybe_locale_matcher.FromJust();

  std::unique_ptr<char[]> calendar_str = nullptr;
  std::unique_ptr<char[]> numbering_system_str = nullptr;
  const std::vector<const char*> empty_values = {};
  // 6. Let calendar be ? GetOption(options, "calendar",
  //    "string", undefined, undefined).
  Maybe<bool> maybe_calendar = GetStringOption(
      isolate, options, "calendar", empty_values, service, &calendar_str);
  MAYBE_RETURN(maybe_calendar, {});
  if (maybe_calendar.FromJust() && calendar_str != nullptr) {
    icu::Locale default_locale;
    if (!Intl::IsWellFormedCalendar(calendar_str.get())) {
      THROW_NEW_ERROR(
          isolate, NewRangeError(
                       MessageTemplate::kInvalid, factory->calendar_string(),
                       factory->NewStringFromAsciiChecked(calendar_str.get())));
    }
  }

  // 8. Let numberingSystem be ? GetOption(options, "numberingSystem",
  //    "string", undefined, undefined).
  Maybe<bool> maybe_numberingSystem = Intl::GetNumberingSystem(
      isolate, options, service, &numbering_system_str);
  MAYBE_RETURN(maybe_numberingSystem, {});

  // 6. Let hour12 be ? GetOption(options, "hour12", "boolean", undefined,
  // undefined).
  bool hour12;
  Maybe<bool> maybe_get_hour12 =
      GetBoolOption(isolate, options, "hour12", service, &hour12);
  MAYBE_RETURN(maybe_get_hour12, {});

  // 7. Let hourCycle be ? GetOption(options, "hourCycle", "string", « "h11",
  // "h12", "h23", "h24" », undefined).
  Maybe<HourCycle> maybe_hour_cycle = GetHourCycle(isolate, options, service);
  MAYBE_RETURN(maybe_hour_cycle, {});
  HourCycle hour_cycle = maybe_hour_cycle.FromJust();

  // 8. If hour12 is not undefined, then
  if (maybe_get_hour12.FromJust()) {
    // a. Let hourCycle be null.
    hour_cycle = HourCycle::kUndefined;
  }
  // 9. Set opt.[[hc]] to hourCycle.

  // ecma402/#sec-intl.datetimeformat-internal-slots
  // The value of the [[RelevantExtensionKeys]] internal slot is
  // « "ca", "nu", "hc" ».
  std::set<std::string> relevant_extension_keys = {"nu", "ca", "hc"};

  // 10. Let localeData be %DateTimeFormat%.[[LocaleData]].
  // 11. Let r be ResolveLocale( %DateTimeFormat%.[[AvailableLocales]],
  //     requestedLocales, opt, %DateTimeFormat%.[[RelevantExtensionKeys]],
  //     localeData).
  //
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale = Intl::ResolveLocale(
      isolate, JSDateTimeFormat::GetAvailableLocales(), requested_locales,
      locale_matcher, relevant_extension_keys);
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  icu::Locale icu_locale = r.icu_locale;
  DCHECK(!icu_locale.isBogus());

  UErrorCode status = U_ZERO_ERROR;
  if (calendar_str != nullptr) {
    auto ca_extension_it = r.extensions.find("ca");
    if (ca_extension_it != r.extensions.end() &&
        ca_extension_it->second != calendar_str.get()) {
      icu_locale.setUnicodeKeywordValue("ca", nullptr, status);
      DCHECK(U_SUCCESS(status));
    }
  }
  if (numbering_system_str != nullptr) {
    auto nu_extension_it = r.extensions.find("nu");
    if (nu_extension_it != r.extensions.end() &&
        nu_extension_it->second != numbering_system_str.get()) {
      icu_locale.setUnicodeKeywordValue("nu", nullptr, status);
      DCHECK(U_SUCCESS(status));
    }
  }

  // Need to keep a copy of icu_locale which not changing "ca", "nu", "hc"
  // by option.
  icu::Locale resolved_locale(icu_locale);

  if (calendar_str != nullptr &&
      Intl::IsValidCalendar(icu_locale, calendar_str.get())) {
    icu_locale.setUnicodeKeywordValue("ca", calendar_str.get(), status);
    DCHECK(U_SUCCESS(status));
  }

  if (numbering_system_str != nullptr &&
      Intl::IsValidNumberingSystem(numbering_system_str.get())) {
    icu_locale.setUnicodeKeywordValue("nu", numbering_system_str.get(), status);
    DCHECK(U_SUCCESS(status));
  }

  static base::LazyInstance<DateTimePatternGeneratorCache>::type
      generator_cache = LAZY_INSTANCE_INITIALIZER;

  std::unique_ptr<icu::DateTimePatternGenerator> generator(
      generator_cache.Pointer()->CreateGenerator(isolate, icu_locale));

  // 15.Let hcDefault be dataLocaleData.[[hourCycle]].
  HourCycle hc_default = ToHourCycle(generator->getDefaultHourCycle(status));
  DCHECK(U_SUCCESS(status));

  // 16.Let hc be r.[[hc]].
  HourCycle hc = HourCycle::kUndefined;
  if (hour_cycle == HourCycle::kUndefined) {
    auto hc_extension_it = r.extensions.find("hc");
    if (hc_extension_it != r.extensions.end()) {
      hc = ToHourCycle(hc_extension_it->second.c_str());
    }
  } else {
    hc = hour_cycle;
  }

  // 25. If hour12 is true, then
  if (maybe_get_hour12.FromJust()) {
    if (hour12) {
      // a. Let hc be dataLocaleData.[[hourCycle12]].
      hc = DefaultHourCycle12(icu_locale, hc_default);
      // 26. Else if hour12 is false, then
    } else {
      // a. Let hc be dataLocaleData.[[hourCycle24]].
      hc = DefaultHourCycle24(icu_locale, hc_default);
    }
  } else {
    // 27. Else,
    // a. Assert: hour12 is undefined.
    // b. Let hc be r.[[hc]].
    // c. If hc is null, set hc to dataLocaleData.[[hourCycle]].
    if (hc == HourCycle::kUndefined) {
      hc = hc_default;
    }
  }

  // 17. Let timeZone be ? Get(options, "timeZone").
  DirectHandle<Object> time_zone_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, time_zone_obj,
      Object::GetPropertyOrElement(isolate, options,
                                   isolate->factory()->timeZone_string()));

  std::unique_ptr<icu::TimeZone> tz;
  if (!IsUndefined(*time_zone_obj, isolate)) {
    DirectHandle<String> time_zone;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, time_zone,
                               Object::ToString(isolate, time_zone_obj));
    tz = JSDateTimeFormat::CreateTimeZone(isolate, time_zone);
  } else {
    // 19.a. Else / Let timeZone be DefaultTimeZone().
    tz.reset(icu::TimeZone::createDefault());
  }

  if (tz.get() == nullptr) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeZone,
                                           time_zone_obj));
  }

  std::unique_ptr<icu::Calendar> calendar(
      CreateCalendar(isolate, icu_locale, tz.release()));

  // 18.b If the result of IsValidTimeZoneName(timeZone) is false, then
  // i. Throw a RangeError exception.
  if (calendar.get() == nullptr) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeZone,
                                           time_zone_obj));
  }

  DateTimeStyle date_style = DateTimeStyle::kUndefined;
  DateTimeStyle time_style = DateTimeStyle::kUndefined;
  std::unique_ptr<icu::SimpleDateFormat> icu_date_format;

  // 35. Let hasExplicitFormatComponents be false.
  int32_t explicit_format_components =
      0;  // The fields which are not undefined.
  // 36. For each row of Table 1, except the header row, do
  bool has_hour_option = false;
  std::string skeleton;
  for (const PatternData& item : GetPatternData(hc)) {
    // Need to read fractionalSecondDigits before reading the timeZoneName
    if (item.property == "timeZoneName") {
      // Let _value_ be ? GetNumberOption(options, "fractionalSecondDigits", 1,
      // 3, *undefined*). The *undefined* is represented by value 0 here.
      int fsd;
      MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
          isolate, fsd,
          GetNumberOption(isolate, options,
                          factory->fractionalSecondDigits_string(), 1, 3, 0),
          {});
      if (fsd > 0) {
        explicit_format_components =
            FractionalSecondDigits::update(explicit_format_components, true);
      }
      // Convert fractionalSecondDigits to skeleton.
      for (int i = 0; i < fsd; i++) {
        skeleton += "S";
      }
    }
    std::unique_ptr<char[]> input;
    // i. Let prop be the name given in the Property column of the row.
    // ii. Let value be ? GetOption(options, prop, "string", « the strings
    // given in the Values column of the row », undefined).
    Maybe<bool> maybe_get_option =
        GetStringOption(isolate, options, item.property.c_str(),
                        item.allowed_values, service, &input);
    MAYBE_RETURN(maybe_get_option, {});
    if (maybe_get_option.FromJust()) {
      // Record which fields are not undefined into explicit_format_components.
      if (item.property == "hour") {
        has_hour_option = true;
      }
      DCHECK_NOT_NULL(input.get());
      // iii. Set opt.[[<prop>]] to value.
      skeleton += item.map.find(input.get())->second;
      // e. If value is not undefined, then
      // i. Set hasExplicitFormatComponents to true.
      explicit_format_components |= 1 << static_cast<int32_t>(item.bitShift);
    }
  }

  // 29. Let matcher be ? GetOption(options, "formatMatcher", "string", «
  // "basic", "best fit" », "best fit").
  // We implement only best fit algorithm, but still need to check
  // if the formatMatcher values are in range.
  // c. Let matcher be ? GetOption(options, "formatMatcher", "string",
  //     «  "basic", "best fit" », "best fit").
  Maybe<FormatMatcherOption> maybe_format_matcher =
      GetStringOption<FormatMatcherOption>(
          isolate, options, "formatMatcher", service, {"best fit", "basic"},
          {FormatMatcherOption::kBestFit, FormatMatcherOption::kBasic},
          FormatMatcherOption::kBestFit);
  MAYBE_RETURN(maybe_format_matcher, {});
  // TODO(ftang): uncomment the following line and handle format_matcher.
  // FormatMatcherOption format_matcher = maybe_format_matcher.FromJust();

  // 32. Let dateStyle be ? GetOption(options, "dateStyle", "string", «
  // "full", "long", "medium", "short" », undefined).
  Maybe<DateTimeStyle> maybe_date_style = GetStringOption<DateTimeStyle>(
      isolate, options, "dateStyle", service,
      {"full", "long", "medium", "short"},
      {DateTimeStyle::kFull, DateTimeStyle::kLong, DateTimeStyle::kMedium,
       DateTimeStyle::kShort},
      DateTimeStyle::kUndefined);
  MAYBE_RETURN(maybe_date_style, {});
  // 33. Set dateTimeFormat.[[DateStyle]] to dateStyle.
  date_style = maybe_date_style.FromJust();

  // 34. Let timeStyle be ? GetOption(options, "timeStyle", "string", «
  // "full", "long", "medium", "short" »).
  Maybe<DateTimeStyle> maybe_time_style = GetStringOption<DateTimeStyle>(
      isolate, options, "timeStyle", service,
      {"full", "long", "medium", "short"},
      {DateTimeStyle::kFull, DateTimeStyle::kLong, DateTimeStyle::kMedium,
       DateTimeStyle::kShort},
      DateTimeStyle::kUndefined);
  MAYBE_RETURN(maybe_time_style, {});

  // 35. Set dateTimeFormat.[[TimeStyle]] to timeStyle.
  time_style = maybe_time_style.FromJust();

  // 36. If timeStyle is not undefined, then
  HourCycle dateTimeFormatHourCycle = HourCycle::kUndefined;
  if (time_style != DateTimeStyle::kUndefined) {
    // a. Set dateTimeFormat.[[HourCycle]] to hc.
    dateTimeFormatHourCycle = hc;
  }

  // 37. If dateStyle or timeStyle are not undefined, then
  if (date_style != DateTimeStyle::kUndefined ||
      time_style != DateTimeStyle::kUndefined) {
    // a. If hasExplicitFormatComponents is true, then
    if (explicit_format_components != 0) {
      // i. Throw a TypeError exception.
      THROW_NEW_ERROR(
          isolate, NewTypeError(MessageTemplate::kInvalid,
                                factory->NewStringFromStaticChars("option"),
                                factory->NewStringFromStaticChars("option")));
    }
    // b. If required is ~date~ and timeStyle is not *undefined*, then
    if (required == RequiredOption::kDate &&
        time_style != DateTimeStyle::kUndefined) {
      // i. Throw a *TypeError* exception.
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kInvalid,
                                   factory->NewStringFromStaticChars("option"),
                                   factory->timeStyle_string()));
    }
    // c. If required is ~time~ and dateStyle is not *undefined*, then
    if (required == RequiredOption::kTime &&
        date_style != DateTimeStyle::kUndefined) {
      // i. Throw a *TypeError* exception.
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kInvalid,
                                   factory->NewStringFromStaticChars("option"),
                                   factory->dateStyle_string()));
    }
    // b. Let pattern be DateTimeStylePattern(dateStyle, timeStyle,
    // dataLocaleData, hc).
    isolate->CountUsage(
        v8::Isolate::UseCounterFeature::kDateTimeFormatDateTimeStyle);

    icu_date_format =
        DateTimeStylePattern(date_style, time_style, icu_locale,
                             dateTimeFormatHourCycle, generator.get());
    if (icu_date_format.get() == nullptr) {
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
    }
  } else {
    // a. Let needDefaults be *true*.
    bool needDefaults = true;
    // b. If required is ~date~ or ~any~, then
    if (required == RequiredOption::kDate || required == RequiredOption::kAny) {
      // 1. For each property name prop of << *"weekday"*, *"year"*, *"month"*,
      // *"day"* >>, do
      //    1. Let value be formatOptions.[[<prop>]].
      //    1. If value is not *undefined*, let needDefaults be *false*.

      needDefaults &= !Weekday::decode(explicit_format_components);
      needDefaults &= !Year::decode(explicit_format_components);
      needDefaults &= !Month::decode(explicit_format_components);
      needDefaults &= !Day::decode(explicit_format_components);
    }
    // c. If required is ~time~ or ~any~, then
    if (required == RequiredOption::kTime || required == RequiredOption::kAny) {
      // 1. For each property name prop of &laquo; *"dayPeriod"*, *"hour"*,
      // *"minute"*, *"second"*, *"fractionalSecondDigits"* &raquo;, do
      //    1. Let value be formatOptions.[[&lt;_prop_&gt;]].
      //    1. If value is not *undefined*, let _needDefaults_ be *false*.
      needDefaults &= !DayPeriod::decode(explicit_format_components);
      needDefaults &= !Hour::decode(explicit_format_components);
      needDefaults &= !Minute::decode(explicit_format_components);
      needDefaults &= !Second::decode(explicit_format_components);
      needDefaults &=
          !FractionalSecondDigits::decode(explicit_format_components);
    }
    // 1. If needDefaults is *true* and _defaults_ is either ~date~ or ~all~,
    // then
    if (needDefaults && ((DefaultsOption::kDate == defaults) ||
                         (DefaultsOption::kAll == defaults))) {
      // 1. For each property name prop of <<*"year"*, *"month"*, *"day"* >>, do
      // 1. Set formatOptions.[[<<prop>>]] to *"numeric"*.
      skeleton += "yMd";
    }
    // 1. If _needDefaults_ is *true* and defaults is either ~time~ or ~all~,
    // then
    if (needDefaults && ((DefaultsOption::kTime == defaults) ||
                         (DefaultsOption::kAll == defaults))) {
      // 1. For each property name prop of << *"hour"*, *"minute"*, *"second"*
      // >>, do
      // 1. Set _formatOptions_.[[<<prop>>]] to *"numeric"*.
      // See
      // https://unicode.org/reports/tr35/tr35.html#UnicodeHourCycleIdentifier
      switch (hc) {
        case HourCycle::kH12:
          skeleton += "hms";
          break;
        case HourCycle::kH23:
        case HourCycle::kUndefined:
          skeleton += "Hms";
          break;
        case HourCycle::kH11:
          skeleton += "Kms";
          break;
        case HourCycle::kH24:
          skeleton += "kms";
          break;
      }
    }
    // e. If dateTimeFormat.[[Hour]] is not undefined, then
    if (has_hour_option) {
      // v. Set dateTimeFormat.[[HourCycle]] to hc.
      dateTimeFormatHourCycle = hc;
    } else {
      // f. Else,
      // Set dateTimeFormat.[[HourCycle]] to undefined.
      dateTimeFormatHourCycle = HourCycle::kUndefined;
    }
    icu::UnicodeString skeleton_ustr(skeleton.c_str());
    icu_date_format = CreateICUDateFormatFromCache(
        icu_locale, skeleton_ustr, generator.get(), dateTimeFormatHourCycle);
    if (icu_date_format.get() == nullptr) {
      // Remove extensions and try again.
      icu_locale = icu::Locale(icu_locale.getBaseName());
      icu_date_format = CreateICUDateFormatFromCache(
          icu_locale, skeleton_ustr, generator.get(), dateTimeFormatHourCycle);
      if (icu_date_format.get() == nullptr) {
        THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
      }
    }
  }

  // The creation of Calendar depends on timeZone so we have to put 13 after 17.
  // Also icu_date_format is not created until here.
  // 13. Set dateTimeFormat.[[Calendar]] to r.[[ca]].
  icu_date_format->adoptCalendar(calendar.release());

  // 12.1.1 InitializeDateTimeFormat ( dateTimeFormat, locales, options )
  //
  // Steps 8-9 set opt.[[hc]] to value *other than undefined*
  // if "hour12" is set or "hourCycle" is set in the option.
  //
  // 9.2.6 ResolveLocale (... )
  // Step 8.h / 8.i and 8.k
  //
  // An hour12 option always overrides an hourCycle option.
  // Additionally hour12 and hourCycle both clear out any existing Unicode
  // extension key in the input locale.
  //
  // See details in https://github.com/tc39/test262/pull/2035
  if (maybe_get_hour12.FromJust() ||
      maybe_hour_cycle.FromJust() != HourCycle::kUndefined) {
    auto hc_extension_it = r.extensions.find("hc");
    if (hc_extension_it != r.extensions.end()) {
      if (dateTimeFormatHourCycle !=
          ToHourCycle(hc_extension_it->second.c_str())) {
        // Remove -hc- if it does not agree with what we used.
        status = U_ZERO_ERROR;
        resolved_locale.setUnicodeKeywordValue("hc", nullptr, status);
        DCHECK(U_SUCCESS(status));
      }
    }
  }

  Maybe<std::string> maybe_locale_str = Intl::ToLanguageTag(resolved_locale);
  MAYBE_RETURN(maybe_locale_str, {});
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(
          maybe_locale_str.FromJust().c_str());

  DirectHandle<Managed<icu::Locale>> managed_locale =
      Managed<icu::Locale>::From(
          isolate, 0, std::shared_ptr<icu::Locale>{icu_locale.clone()});

  DirectHandle<Managed<icu::SimpleDateFormat>> managed_format =
      Managed<icu::SimpleDateFormat>::From(isolate, 0,
                                           std::move(icu_date_format));

  DirectHandle<Managed<icu::DateIntervalFormat>> managed_interval_format =
      Managed<icu::DateIntervalFormat>::From(isolate, 0, nullptr);

  // Now all properties are ready, so we can allocate the result object.
  DirectHandle<JSDateTimeFormat> date_time_format = Cast<JSDateTimeFormat>(
      isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  date_time_format->set_flags(0);
  if (date_style != DateTimeStyle::kUndefined) {
    date_time_format->set_date_style(date_style);
  }
  if (time_style != DateTimeStyle::kUndefined) {
    date_time_format->set_time_style(time_style);
  }
  date_time_format->set_hour_cycle(dateTimeFormatHourCycle);
  date_time_format->set_locale(*locale_str);
  date_time_format->set_icu_locale(*managed_locale);
  date_time_format->set_icu_simple_date_format(*managed_format);
  date_time_format->set_icu_date_interval_format(*managed_interval_format);
  return date_time_format;
}

namespace {

// The list comes from third_party/icu/source/i18n/unicode/udat.h.
// They're mapped to DateTimeFormat components listed at
// https://tc39.github.io/ecma402/#sec-datetimeformat-abstracts .
DirectHandle<String> IcuDateFieldIdToDateType(int32_t field_id,
                                              Isolate* isolate) {
  switch (field_id) {
    case -1:
      return isolate->factory()->literal_string();
    case UDAT_YEAR_FIELD:
    case UDAT_EXTENDED_YEAR_FIELD:
      return isolate->factory()->year_string();
    case UDAT_YEAR_NAME_FIELD:
      return isolate->factory()->yearName_string();
    case UDAT_MONTH_FIELD:
    case UDAT_STANDALONE_MONTH_FIELD:
      return isolate->factory()->month_string();
    case UDAT_DATE_FIELD:
      return isolate->factory()->day_string();
    case UDAT_HOUR_OF_DAY1_FIELD:
    case UDAT_HOUR_OF_DAY0_FIELD:
    case UDAT_HOUR1_FIELD:
    case UDAT_HOUR0_FIELD:
      return isolate->factory()->hour_string();
    case UDAT_MINUTE_FIELD:
      return isolate->factory()->minute_string();
    case UDAT_SECOND_FIELD:
      return isolate->factory()->second_string();
    case UDAT_DAY_OF_WEEK_FIELD:
    case UDAT_DOW_LOCAL_FIELD:
    case UDAT_STANDALONE_DAY_FIELD:
      return isolate->factory()->weekday_string();
    case UDAT_AM_PM_FIELD:
    case UDAT_AM_PM_MIDNIGHT_NOON_FIELD:
    case UDAT_FLEXIBLE_DAY_PERIOD_FIELD:
      return isolate->factory()->dayPeriod_string();
    case UDAT_TIMEZONE_FIELD:
    case UDAT_TIMEZONE_RFC_FIELD:
    case UDAT_TIMEZONE_GENERIC_FIELD:
    case UDAT_TIMEZONE_SPECIAL_FIELD:
    case UDAT_TIMEZONE_LOCALIZED_GMT_OFFSET_FIELD:
    case UDAT_TIMEZONE_ISO_FIELD:
    case UDAT_TIMEZONE_ISO_LOCAL_FIELD:
      return isolate->factory()->timeZoneName_string();
    case UDAT_ERA_FIELD:
      return isolate->factory()->era_string();
    case UDAT_FRACTIONAL_SECOND_FIELD:
      return isolate->factory()->fractionalSecond_string();
    case UDAT_RELATED_YEAR_FIELD:
      return isolate->factory()->relatedYear_string();

    case UDAT_QUARTER_FIELD:
    case UDAT_STANDALONE_QUARTER_FIELD:
    default:
      // Other UDAT_*_FIELD's cannot show up because there is no way to specify
      // them via options of Intl.DateTimeFormat.
      UNREACHABLE();
  }
}

MaybeDirectHandle<JSArray> FieldPositionIteratorToArray(
    Isolate* isolate, const icu::UnicodeString& formatted,
    icu::FieldPositionIterator fp_iter, bool output_source);

MaybeDirectHandle<JSArray> FormatMillisecondsByKindToArray(
    Isolate* isolate, const icu::SimpleDateFormat& date_format,
    PatternKind kind, double x, bool output_source) {
  icu::FieldPositionIterator fp_iter;
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString formatted =
      CallICUFormat(date_format, kind, x, &fp_iter, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return FieldPositionIteratorToArray(isolate, formatted, fp_iter,
                                      output_source);
}

MaybeDirectHandle<JSArray> FormatMillisecondsByKindToArrayOutputSource(
    Isolate* isolate, const icu::SimpleDateFormat& date_format,
    PatternKind kind, double x) {
  return FormatMillisecondsByKindToArray(isolate, date_format, kind, x, true);
}

MaybeDirectHandle<JSArray> FormatToPartsWithTemporalSupport(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x, bool output_source, const char* method_name) {
  icu::SimpleDateFormat* format =
      date_time_format->icu_simple_date_format()->raw();
  DCHECK_NOT_NULL(format);

  // 1. Let x be ? HandleDateTimeValue(dateTimeFormat, x).
  DateTimeValueRecord x_record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, x_record,
      HandleDateTimeValue(isolate, *format, GetCalendar(isolate, *format), x,
                          method_name),
      {});

  return FormatMillisecondsByKindToArray(isolate, *format, x_record.kind,
                                         x_record.epoch_milliseconds,
                                         output_source);
}

MaybeDirectHandle<JSArray> FormatMillisecondsToArray(
    Isolate* isolate, const icu::SimpleDateFormat& format, double value,
    bool output_source) {
  icu::UnicodeString formatted;
  icu::FieldPositionIterator fp_iter;
  UErrorCode status = U_ZERO_ERROR;
  format.format(value, formatted, &fp_iter, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return FieldPositionIteratorToArray(isolate, formatted, fp_iter,
                                      output_source);
}

MaybeDirectHandle<JSArray> FormatMillisecondsToArrayOutputSource(
    Isolate* isolate, const icu::SimpleDateFormat& format, double value) {
  return FormatMillisecondsToArray(isolate, format, value, true);
}
}  // namespace

MaybeDirectHandle<JSArray> JSDateTimeFormat::FormatToParts(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x, bool output_source, const char* method_name) {
  Factory* factory = isolate->factory();
  if (v8_flags.harmony_temporal) {
    return FormatToPartsWithTemporalSupport(isolate, date_time_format, x,
                                            output_source, method_name);
  }

  if (IsUndefined(*x, isolate)) {
    x = factory->NewNumberFromInt64(JSDate::CurrentTimeValue(isolate));
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, x, Object::ToNumber(isolate, x));
  }

  double date_value = Object::NumberValue(*x);
  if (!DateCache::TryTimeClip(&date_value)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeValue));
  }
  return FormatMillisecondsToArray(
      isolate, *(date_time_format->icu_simple_date_format()->raw()), date_value,
      output_source);
}

namespace {
MaybeDirectHandle<JSArray> FieldPositionIteratorToArray(
    Isolate* isolate, const icu::UnicodeString& formatted,
    icu::FieldPositionIterator fp_iter, bool output_source) {
  Factory* factory = isolate->factory();
  icu::FieldPosition fp;
  DirectHandle<JSArray> result = factory->NewJSArray(0);
  int32_t length = formatted.length();
  if (length == 0) return result;

  int index = 0;
  int32_t previous_end_pos = 0;
  DirectHandle<String> substring;
  while (fp_iter.next(fp)) {
    int32_t begin_pos = fp.getBeginIndex();
    int32_t end_pos = fp.getEndIndex();

    if (previous_end_pos < begin_pos) {
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, substring,
          Intl::ToString(isolate, formatted, previous_end_pos, begin_pos));
      if (output_source) {
        Intl::AddElement(isolate, result, index,
                         IcuDateFieldIdToDateType(-1, isolate), substring,
                         isolate->factory()->source_string(),
                         isolate->factory()->shared_string());
      } else {
        Intl::AddElement(isolate, result, index,
                         IcuDateFieldIdToDateType(-1, isolate), substring);
      }
      ++index;
    }
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, substring,
        Intl::ToString(isolate, formatted, begin_pos, end_pos));
    if (output_source) {
      Intl::AddElement(isolate, result, index,
                       IcuDateFieldIdToDateType(fp.getField(), isolate),
                       substring, isolate->factory()->source_string(),
                       isolate->factory()->shared_string());
    } else {
      Intl::AddElement(isolate, result, index,
                       IcuDateFieldIdToDateType(fp.getField(), isolate),
                       substring);
    }
    previous_end_pos = end_pos;
    ++index;
  }
  if (previous_end_pos < length) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, substring,
        Intl::ToString(isolate, formatted, previous_end_pos, length));
    if (output_source) {
      Intl::AddElement(isolate, result, index,
                       IcuDateFieldIdToDateType(-1, isolate), substring,
                       isolate->factory()->source_string(),
                       isolate->factory()->shared_string());
    } else {
      Intl::AddElement(isolate, result, index,
                       IcuDateFieldIdToDateType(-1, isolate), substring);
    }
  }
  JSObject::ValidateElements(*result);
  return result;
}

}  // namespace

const std::set<std::string>& JSDateTimeFormat::GetAvailableLocales() {
  return Intl::GetAvailableLocalesForDateFormat();
}

Handle<String> JSDateTimeFormat::HourCycleAsString(Isolate* isolate) const {
  switch (hour_cycle()) {
    case HourCycle::kUndefined:
      return isolate->factory()->undefined_string();
    case HourCycle::kH11:
      return isolate->factory()->h11_string();
    case HourCycle::kH12:
      return isolate->factory()->h12_string();
    case HourCycle::kH23:
      return isolate->factory()->h23_string();
    case HourCycle::kH24:
      return isolate->factory()->h24_string();
    default:
      UNREACHABLE();
  }
}

namespace {

Maybe<bool> AddPartForFormatRange(
    Isolate* isolate, DirectHandle<JSArray> array,
    const icu::UnicodeString& string, int32_t index, int32_t field,
    int32_t start, int32_t end, const Intl::FormatRangeSourceTracker& tracker) {
  DirectHandle<String> substring;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, substring,
                                   Intl::ToString(isolate, string, start, end),
                                   Nothing<bool>());
  Intl::AddElement(isolate, array, index,
                   IcuDateFieldIdToDateType(field, isolate), substring,
                   isolate->factory()->source_string(),
                   Intl::SourceString(isolate, tracker.GetSource(start, end)));
  return Just(true);
}

// If this function return a value, it could be a throw of TypeError, or normal
// formatted string. If it return a nullopt the caller should call the fallback
// function.
std::optional<MaybeDirectHandle<String>> FormattedToString(
    Isolate* isolate, const icu::FormattedValue& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  icu::ConstrainedFieldPosition cfpos;
  while (formatted.nextPosition(cfpos, status)) {
    if (cfpos.getCategory() == UFIELD_CATEGORY_DATE_INTERVAL_SPAN) {
      return Intl::ToString(isolate, result);
    }
  }
  return std::nullopt;
}

// A helper function to convert the FormattedDateInterval to a
// MaybeHandle<JSArray> for the implementation of formatRangeToParts.
// If this function return a value, it could be a throw of TypeError, or normal
// formatted parts in JSArray. If it return a nullopt the caller should call
// the fallback function.
std::optional<MaybeDirectHandle<JSArray>> FormattedDateIntervalToJSArray(
    Isolate* isolate, const icu::FormattedValue& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);

  Factory* factory = isolate->factory();
  DirectHandle<JSArray> array = factory->NewJSArray(0);
  icu::ConstrainedFieldPosition cfpos;
  int index = 0;
  int32_t previous_end_pos = 0;
  Intl::FormatRangeSourceTracker tracker;
  bool output_range = false;
  while (formatted.nextPosition(cfpos, status)) {
    int32_t category = cfpos.getCategory();
    int32_t field = cfpos.getField();
    int32_t start = cfpos.getStart();
    int32_t limit = cfpos.getLimit();

    if (category == UFIELD_CATEGORY_DATE_INTERVAL_SPAN) {
      DCHECK_LE(field, 2);
      output_range = true;
      tracker.Add(field, start, limit);
    } else {
      DCHECK(category == UFIELD_CATEGORY_DATE);
      if (start > previous_end_pos) {
        // Add "literal" from the previous end position to the start if
        // necessary.
        Maybe<bool> maybe_added =
            AddPartForFormatRange(isolate, array, result, index, -1,
                                  previous_end_pos, start, tracker);
        MAYBE_RETURN(maybe_added, Handle<JSArray>());
        previous_end_pos = start;
        index++;
      }
      Maybe<bool> maybe_added = AddPartForFormatRange(
          isolate, array, result, index, field, start, limit, tracker);
      MAYBE_RETURN(maybe_added, Handle<JSArray>());
      previous_end_pos = limit;
      ++index;
    }
  }
  int32_t end = result.length();
  // Add "literal" in the end if necessary.
  if (end > previous_end_pos) {
    Maybe<bool> maybe_added = AddPartForFormatRange(
        isolate, array, result, index, -1, previous_end_pos, end, tracker);
    MAYBE_RETURN(maybe_added, Handle<JSArray>());
  }

  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }

  JSObject::ValidateElements(*array);
  if (output_range) return array;
  return std::nullopt;
}

// The shared code between formatRange and formatRangeToParts
template <typename T, std::optional<MaybeDirectHandle<T>> (*Format)(
                          Isolate*, const icu::FormattedValue&)>
std::optional<MaybeDirectHandle<T>> CallICUFormatRange(
    Isolate* isolate, const icu::DateIntervalFormat* format,
    const icu::Calendar* calendar, double x, double y);

// #sec-partitiondatetimerangepattern
template <typename T, std::optional<MaybeDirectHandle<T>> (*Format)(
                          Isolate*, const icu::FormattedValue&)>
std::optional<MaybeDirectHandle<T>> PartitionDateTimeRangePattern(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format, double x,
    double y, const char* method_name) {
  // 1. Let x be TimeClip(x).
  // 2. If x is NaN, throw a RangeError exception.
  if (!DateCache::TryTimeClip(&x)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeValue));
  }
  // 3. Let y be TimeClip(y).
  // 4. If y is NaN, throw a RangeError exception.
  if (!DateCache::TryTimeClip(&y)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kInvalidTimeValue));
  }

  std::unique_ptr<icu::DateIntervalFormat> format(LazyCreateDateIntervalFormat(
      isolate, date_time_format, PatternKind::kDate));
  if (format.get() == nullptr) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }

  icu::SimpleDateFormat* date_format =
      date_time_format->icu_simple_date_format()->raw();
  const icu::Calendar* calendar = date_format->getCalendar();

  return CallICUFormatRange<T, Format>(isolate, format.get(), calendar, x, y);
}

template <typename T, std::optional<MaybeDirectHandle<T>> (*Format)(
                          Isolate*, const icu::FormattedValue&)>
std::optional<MaybeDirectHandle<T>> CallICUFormatRange(
    Isolate* isolate, const icu::DateIntervalFormat* format,
    const icu::Calendar* calendar, double x, double y) {
  UErrorCode status = U_ZERO_ERROR;

  std::unique_ptr<icu::Calendar> c1(calendar->clone());
  std::unique_ptr<icu::Calendar> c2(calendar->clone());
  c1->setTime(x, status);
  c2->setTime(y, status);
  // We need to format by Calendar because we need the Gregorian change
  // adjustment already in the SimpleDateFormat to set the correct value of date
  // older than Oct 15, 1582.
  icu::FormattedDateInterval formatted =
      format->formatToValue(*c1, *c2, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return Format(isolate, formatted);
}

template <typename T,
          std::optional<MaybeDirectHandle<T>> (*Format)(
              Isolate*, const icu::FormattedValue&),
          MaybeDirectHandle<T> (*Fallback)(
              Isolate*, const icu::SimpleDateFormat&, PatternKind, double)>
MaybeDirectHandle<T> FormatRangeCommonWithTemporalSupport(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x_obj, DirectHandle<Object> y_obj,
    const char* method_name) {
  // 5. If either of ! IsTemporalObject(x) or ! IsTemporalObject(y) is true,
  // then
  if (IsTemporalObject(x_obj) || IsTemporalObject(y_obj)) {
    // a. If ! SameTemporalType(x, y) is false, throw a TypeError exception.
    if (!SameTemporalType(x_obj, y_obj)) {
      THROW_NEW_ERROR(
          isolate,
          NewTypeError(MessageTemplate::kInvalidArgumentForTemporal, y_obj));
    }
  }
  // 6. Let x be ? HandleDateTimeValue(dateTimeFormat, x).
  icu::SimpleDateFormat* icu_simple_date_format =
      date_time_format->icu_simple_date_format()->raw();
  DirectHandle<String> date_time_format_calendar =
      GetCalendar(isolate, *icu_simple_date_format);
  DateTimeValueRecord x_record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, x_record,
      HandleDateTimeValue(isolate, *icu_simple_date_format,
                          date_time_format_calendar, x_obj, method_name),
      {});

  // 7. Let y be ? HandleDateTimeValue(dateTimeFormat, y).
  DateTimeValueRecord y_record;
  MAYBE_ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, y_record,
      HandleDateTimeValue(isolate, *icu_simple_date_format,
                          date_time_format_calendar, y_obj, method_name),
      {});

  std::unique_ptr<icu::DateIntervalFormat> format(
      LazyCreateDateIntervalFormat(isolate, date_time_format, x_record.kind));
  if (format.get() == nullptr) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }

  const icu::Calendar* calendar =
      date_time_format->icu_simple_date_format()->raw()->getCalendar();

  std::optional<MaybeDirectHandle<T>> result = CallICUFormatRange<T, Format>(
      isolate, format.get(), calendar, x_record.epoch_milliseconds,
      y_record.epoch_milliseconds);
  if (result.has_value()) return *result;
  return Fallback(isolate, *icu_simple_date_format, x_record.kind,
                  x_record.epoch_milliseconds);
}

template <typename T,
          std::optional<MaybeDirectHandle<T>> (*Format)(
              Isolate*, const icu::FormattedValue&),
          MaybeDirectHandle<T> (*Fallback)(
              Isolate*, const icu::SimpleDateFormat&, double)>
MaybeDirectHandle<T> FormatRangeCommon(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x_obj, DirectHandle<Object> y_obj,
    const char* method_name) {
  // 4. Let x be ? ToNumber(startDate).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, x_obj, Object::ToNumber(isolate, x_obj));
  double x = Object::NumberValue(*x_obj);
  // 5. Let y be ? ToNumber(endDate).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, y_obj, Object::ToNumber(isolate, y_obj));
  double y = Object::NumberValue(*y_obj);

  std::optional<MaybeDirectHandle<T>> result =
      PartitionDateTimeRangePattern<T, Format>(isolate, date_time_format, x, y,
                                               method_name);
  if (result.has_value()) return *result;
  return Fallback(isolate, *(date_time_format->icu_simple_date_format()->raw()),
                  x);
}

}  // namespace

MaybeDirectHandle<String> JSDateTimeFormat::FormatRange(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x, DirectHandle<Object> y, const char* method_name) {
  // Track newer feature formatRange and formatRangeToParts.
  isolate->CountUsage(v8::Isolate::UseCounterFeature::kDateTimeFormatRange);
  if (v8_flags.harmony_temporal) {
    // For Temporal enable support.
    return FormatRangeCommonWithTemporalSupport<
        String, FormattedToString, FormatMillisecondsByKindToString>(
        isolate, date_time_format, x, y, method_name);
  }
  // Pre Temporal implementation.
  return FormatRangeCommon<String, FormattedToString, FormatDateTime>(
      isolate, date_time_format, x, y, method_name);
}

MaybeDirectHandle<JSArray> JSDateTimeFormat::FormatRangeToParts(
    Isolate* isolate, DirectHandle<JSDateTimeFormat> date_time_format,
    DirectHandle<Object> x, DirectHandle<Object> y, const char* method_name) {
  // Track newer feature formatRange and formatRangeToParts.
  isolate->CountUsage(v8::Isolate::UseCounterFeature::kDateTimeFormatRange);
  if (v8_flags.harmony_temporal) {
    // For Temporal enable support.
    return FormatRangeCommonWithTemporalSupport<
        JSArray, FormattedDateIntervalToJSArray,
        FormatMillisecondsByKindToArrayOutputSource>(isolate, date_time_format,
                                                     x, y, method_name);
  }
  // Pre Temporal implementation.
  return FormatRangeCommon<JSArray, FormattedDateIntervalToJSArray,
                           FormatMillisecondsToArrayOutputSource>(
      isolate, date_time_format, x, y, method_name);
}

}  // namespace v8::internal
