// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_INTL_OBJECTS_H_
#define V8_OBJECTS_INTL_OBJECTS_H_

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include "src/base/timezone-cache.h"
#include "src/objects/contexts.h"
#include "src/objects/managed.h"
#include "src/objects/objects.h"
#include "unicode/locid.h"
#include "unicode/uversion.h"

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#define V8_MINIMUM_ICU_VERSION 73

namespace U_ICU_NAMESPACE {
class BreakIterator;
class Locale;
class ListFormatter;
class RelativeDateTimeFormatter;
class SimpleDateFormat;
class DateIntervalFormat;
class PluralRules;
class Collator;
class FormattedValue;
class StringEnumeration;
class TimeZone;
class UnicodeString;
namespace number {
class LocalizedNumberFormatter;
}  //  namespace number
}  // namespace U_ICU_NAMESPACE

namespace v8::internal {

#define ICU_EXTERNAL_POINTER_TAG_LIST(V)                              \
  V(icu::UnicodeString, kIcuUnicodeStringTag)                         \
  V(icu::BreakIterator, kIcuBreakIteratorTag)                         \
  V(icu::Locale, kIcuLocaleTag)                                       \
  V(icu::SimpleDateFormat, kIcuSimpleDateFormatTag)                   \
  V(icu::DateIntervalFormat, kIcuDateIntervalFormatTag)               \
  V(icu::RelativeDateTimeFormatter, kIcuRelativeDateTimeFormatterTag) \
  V(icu::ListFormatter, kIcuListFormatterTag)                         \
  V(icu::Collator, kIcuCollatorTag)                                   \
  V(icu::PluralRules, kIcuPluralRulesTag)                             \
  V(icu::number::LocalizedNumberFormatter, kIcuLocalizedNumberFormatterTag)
ICU_EXTERNAL_POINTER_TAG_LIST(ASSIGN_EXTERNAL_POINTER_TAG_FOR_MANAGED)
#undef ICU_EXTERNAL_POINTER_TAG_LIST

struct NumberFormatSpan {
  int32_t field_id;
  int32_t begin_pos;
  int32_t end_pos;

  NumberFormatSpan() = default;
  NumberFormatSpan(int32_t field_id, int32_t begin_pos, int32_t end_pos)
      : field_id(field_id), begin_pos(begin_pos), end_pos(end_pos) {}
};

V8_EXPORT_PRIVATE std::vector<NumberFormatSpan> FlattenRegionsToParts(
    std::vector<NumberFormatSpan>* regions);

class JSCollator;

class Intl {
 public:
  enum class BoundFunctionContextSlot {
    kBoundFunction = Context::MIN_CONTEXT_SLOTS,
    kLength
  };

  enum class FormatRangeSource { kShared, kStartRange, kEndRange };

  class FormatRangeSourceTracker {
   public:
    FormatRangeSourceTracker();
    void Add(int32_t field, int32_t start, int32_t limit);
    FormatRangeSource GetSource(int32_t start, int32_t limit) const;

   private:
    int32_t start_[2];
    int32_t limit_[2];

    bool FieldContains(int32_t field, int32_t start, int32_t limit) const;
  };

  static DirectHandle<String> SourceString(Isolate* isolate,
                                           FormatRangeSource source);

  // Build a set of ICU locales from a list of Locales. If there is a locale
  // with a script tag then the locales also include a locale without the
  // script; eg, pa_Guru_IN (language=Panjabi, script=Gurmukhi, country-India)
  // would include pa_IN.
  static std::set<std::string> BuildLocaleSet(
      const std::vector<std::string>& locales, const char* path,
      const char* validate_key);

  static Maybe<std::string> ToLanguageTag(const icu::Locale& locale);

  // Get the name of the numbering system from locale.
  // ICU doesn't expose numbering system in any way, so we have to assume that
  // for given locale NumberingSystem constructor produces the same digits as
  // NumberFormat/Calendar would.
  static std::string GetNumberingSystem(const icu::Locale& icu_locale);

  static V8_WARN_UNUSED_RESULT MaybeDirectHandle<JSObject> SupportedLocalesOf(
      Isolate* isolate, const char* method_name,
      const std::set<std::string>& available_locales,
      DirectHandle<Object> locales_in, DirectHandle<Object> options_in);

  // https://tc39.github.io/ecma402/#sec-canonicalizelocalelist
  // {only_return_one_result} is an optimization for callers that only
  // care about the first result.
  static Maybe<std::vector<std::string>> CanonicalizeLocaleList(
      Isolate* isolate, DirectHandle<Object> locales,
      bool only_return_one_result = false);

  // ecma-402 #sec-intl.getcanonicallocales
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> GetCanonicalLocales(
      Isolate* isolate, DirectHandle<Object> locales);

  // ecma-402 #sec-intl.supportedvaluesof
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> SupportedValuesOf(
      Isolate* isolate, DirectHandle<Object> key);

  // For locale sensitive functions
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String>
  StringLocaleConvertCase(Isolate* isolate, DirectHandle<String> s,
                          bool is_upper, DirectHandle<Object> locales);

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ConvertToUpper(
      Isolate* isolate, DirectHandle<String> s);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ConvertToLower(
      Isolate* isolate, DirectHandle<String> s);

  V8_WARN_UNUSED_RESULT static std::optional<int> StringLocaleCompare(
      Isolate* isolate, DirectHandle<String> s1, DirectHandle<String> s2,
      DirectHandle<Object> locales, DirectHandle<Object> options,
      const char* method_name);

  enum class CompareStringsOptions {
    kNone,
    kTryFastPath,
  };
  template <class IsolateT>
  V8_EXPORT_PRIVATE static CompareStringsOptions CompareStringsOptionsFor(
      IsolateT* isolate, DirectHandle<Object> locales,
      DirectHandle<Object> options);
  V8_EXPORT_PRIVATE V8_WARN_UNUSED_RESULT static int CompareStrings(
      Isolate* isolate, const icu::Collator& collator, DirectHandle<String> s1,
      DirectHandle<String> s2,
      CompareStringsOptions compare_strings_options =
          CompareStringsOptions::kNone);

  // ecma402/#sup-properties-of-the-number-prototype-object
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> NumberToLocaleString(
      Isolate* isolate, Handle<Object> num, DirectHandle<Object> locales,
      DirectHandle<Object> options, const char* method_name);

  // [[RoundingPriority]] is one of the String values "auto", "morePrecision",
  // or "lessPrecision", specifying the rounding priority for the number.
  enum class RoundingPriority {
    kAuto,
    kMorePrecision,
    kLessPrecision,
  };

  enum class RoundingType {
    kFractionDigits,
    kSignificantDigits,
    kMorePrecision,
    kLessPrecision,
  };

  // [[RoundingMode]] is one of the String values "ceil", "floor", "expand",
  // "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", or "halfEven",
  // specifying the rounding strategy for the number.
  enum class RoundingMode {
    kCeil,
    kFloor,
    kExpand,
    kTrunc,
    kHalfCeil,
    kHalfFloor,
    kHalfExpand,
    kHalfTrunc,
    kHalfEven,
  };

  // [[TrailingZeroDisplay]] is one of the String values "auto" or
  // "stripIfInteger", specifying the strategy for displaying trailing zeros on
  // whole number.
  enum class TrailingZeroDisplay {
    kAuto,
    kStripIfInteger,
  };

  // ecma402/#sec-setnfdigitoptions
  struct NumberFormatDigitOptions {
    int minimum_integer_digits;
    int minimum_fraction_digits;
    int maximum_fraction_digits;
    int minimum_significant_digits;
    int maximum_significant_digits;
    RoundingPriority rounding_priority;
    RoundingType rounding_type;
    int rounding_increment;
    RoundingMode rounding_mode;
    TrailingZeroDisplay trailing_zero_display;
  };
  V8_WARN_UNUSED_RESULT static Maybe<NumberFormatDigitOptions>
  SetNumberFormatDigitOptions(Isolate* isolate,
                              DirectHandle<JSReceiver> options,
                              int mnfd_default, int mxfd_default,
                              bool notation_is_compact, const char* service);

  // Helper function to convert a UnicodeString to a Handle<String>
  V8_WARN_UNUSED_RESULT static MaybeHandle<String> ToString(
      Isolate* isolate, const icu::UnicodeString& string);

  // Helper function to convert a substring of UnicodeString to a Handle<String>
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> ToString(
      Isolate* isolate, const icu::UnicodeString& string, int32_t begin,
      int32_t end);

  // Helper function to convert a FormattedValue to String
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> FormattedToString(
      Isolate* isolate, const icu::FormattedValue& formatted);

  // Helper function to convert number field id to type string.
  static DirectHandle<String> NumberFieldToType(Isolate* isolate,
                                                const NumberFormatSpan& part,
                                                const icu::UnicodeString& text,
                                                bool is_nan);

  // A helper function to implement formatToParts which add element to array as
  // $array[$index] = { type: $field_type_string, value: $value }
  static void AddElement(Isolate* isolate, DirectHandle<JSArray> array,
                         int index, DirectHandle<String> field_type_string,
                         DirectHandle<String> value);

  // A helper function to implement formatToParts which add element to array as
  // $array[$index] = {
  //   type: $field_type_string, value: $value,
  //   $additional_property_name: $additional_property_value
  // }
  static void AddElement(Isolate* isolate, DirectHandle<JSArray> array,
                         int index, DirectHandle<String> field_type_string,
                         DirectHandle<String> value,
                         DirectHandle<String> additional_property_name,
                         DirectHandle<String> additional_property_value);

  // A helper function to implement formatToParts which add element to array
  static Maybe<int> AddNumberElements(Isolate* isolate,
                                      const icu::FormattedValue& formatted,
                                      DirectHandle<JSArray> result,
                                      int start_index,
                                      DirectHandle<String> unit);

  // In ECMA 402 v1, Intl constructors supported a mode of operation
  // where calling them with an existing object as a receiver would
  // transform the receiver into the relevant Intl instance with all
  // internal slots. In ECMA 402 v2, this capability was removed, to
  // avoid adding internal slots on existing objects. In ECMA 402 v3,
  // the capability was re-added as "normative optional" in a mode
  // which chains the underlying Intl instance on any object, when the
  // constructor is called
  //
  // See ecma402/#legacy-constructor.
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<Object> LegacyUnwrapReceiver(
      Isolate* isolate, DirectHandle<JSReceiver> receiver,
      DirectHandle<JSFunction> constructor, bool has_initialized_slot);

  // enum for "localeMatcher" option: shared by many Intl objects.
  enum class MatcherOption { kBestFit, kLookup };

  // Shared function to read the "localeMatcher" option.
  V8_WARN_UNUSED_RESULT static Maybe<MatcherOption> GetLocaleMatcher(
      Isolate* isolate, DirectHandle<JSReceiver> options,
      const char* method_name);

  // Shared function to read the "numberingSystem" option.
  V8_WARN_UNUSED_RESULT static Maybe<bool> GetNumberingSystem(
      Isolate* isolate, DirectHandle<JSReceiver> options,
      const char* method_name, std::unique_ptr<char[]>* result);

  // Check the calendar is valid or not for that locale.
  static bool IsValidCalendar(const icu::Locale& locale,
                              const std::string& value);

  // Check the collation is valid or not for that locale.
  static bool IsValidCollation(const icu::Locale& locale,
                               const std::string& value);

  // Check the numberingSystem is valid.
  static bool IsValidNumberingSystem(const std::string& value);

  // Check the calendar is well formed.
  static bool IsWellFormedCalendar(const std::string& value);

  // Check the currency is well formed.
  static bool IsWellFormedCurrency(const std::string& value);

  struct ResolvedLocale {
    std::string locale;
    icu::Locale icu_locale;
    std::map<std::string, std::string> extensions;
  };

  static Maybe<ResolvedLocale> ResolveLocale(
      Isolate* isolate, const std::set<std::string>& available_locales,
      const std::vector<std::string>& requested_locales, MatcherOption options,
      const std::set<std::string>& relevant_extension_keys);

  // A helper template to implement the GetAvailableLocales
  // Usage in src/objects/js-XXX.cc
  // const std::set<std::string>& JSXxx::GetAvailableLocales() {
  //   static base::LazyInstance<Intl::AvailableLocales<icu::YYY>>::type
  //       available_locales = LAZY_INSTANCE_INITIALIZER;
  //   return available_locales.Pointer()->Get();
  // }

  struct SkipResourceCheck {
    static const char* key() { return nullptr; }
    static const char* path() { return nullptr; }
  };

  template <typename C = SkipResourceCheck>
  class AvailableLocales {
   public:
    AvailableLocales() {
      UErrorCode status = U_ZERO_ERROR;
      UEnumeration* uenum =
          uloc_openAvailableByType(ULOC_AVAILABLE_WITH_LEGACY_ALIASES, &status);
      DCHECK(U_SUCCESS(status));

      std::vector<std::string> all_locales;
      const char* loc;
      while ((loc = uenum_next(uenum, nullptr, &status)) != nullptr) {
        DCHECK(U_SUCCESS(status));
        std::string locstr(loc);
        std::replace(locstr.begin(), locstr.end(), '_', '-');
        // Handle special case
        if (locstr == "en-US-POSIX") locstr = "en-US-u-va-posix";
        all_locales.push_back(locstr);
      }
      uenum_close(uenum);

      set_ = Intl::BuildLocaleSet(all_locales, C::path(), C::key());
    }
    const std::set<std::string>& Get() const { return set_; }

   private:
    std::set<std::string> set_;
  };

  // Utility function to set text to BreakIterator.
  static DirectHandle<Managed<icu::UnicodeString>> SetTextToBreakIterator(
      Isolate* isolate, DirectHandle<String> text,
      icu::BreakIterator* break_iterator);

  // ecma262 #sec-string.prototype.normalize
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<String> Normalize(
      Isolate* isolate, DirectHandle<String> string,
      DirectHandle<Object> form_input);
  static base::TimezoneCache* CreateTimeZoneCache();

  // Convert a Handle<String> to icu::UnicodeString
  static icu::UnicodeString ToICUUnicodeString(Isolate* isolate,
                                               DirectHandle<String> string,
                                               int offset = 0);

  static const uint8_t* ToLatin1LowerTable();

  static const uint8_t* AsciiCollationWeightsL1();
  static const uint8_t* AsciiCollationWeightsL3();
  static const int kAsciiCollationWeightsLength;

  static Tagged<String> ConvertOneByteToLower(Tagged<String> src,
                                              Tagged<String> dst);

  static const std::set<std::string>& GetAvailableLocales();

  static const std::set<std::string>& GetAvailableLocalesForDateFormat();

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> ToJSArray(
      Isolate* isolate, const char* unicode_key,
      icu::StringEnumeration* enumeration,
      const std::function<bool(const char*)>& removes, bool sort);

  static bool RemoveCollation(const char* collation);

  static std::set<std::string> SanctionedSimpleUnits();

  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSArray> AvailableCalendars(
      Isolate* isolate);

  V8_WARN_UNUSED_RESULT static bool IsValidTimeZoneName(
      const icu::TimeZone& tz);
  V8_WARN_UNUSED_RESULT static bool IsValidTimeZoneName(Isolate* isolate,
                                                        const std::string& id);
  V8_WARN_UNUSED_RESULT static bool IsValidTimeZoneName(
      Isolate* isolate, DirectHandle<String> id);

  // Function to support Temporal
  V8_WARN_UNUSED_RESULT static std::string TimeZoneIdFromIndex(int32_t index);

  // Return the index of timezone which later could be used with
  // TimeZoneIdFromIndex. Returns -1 while the identifier is not a built-in
  // TimeZone name.
  static int32_t GetTimeZoneIndex(Isolate* isolate,
                                  DirectHandle<String> identifier);

  enum class Transition { kNext, kPrevious };

  // Functions to support Temporal

  // Return the epoch of transition in BigInt or null if there are no
  // transition.
  static DirectHandle<Object> GetTimeZoneOffsetTransitionNanoseconds(
      Isolate* isolate, int32_t time_zone_index,
      DirectHandle<BigInt> nanosecond_epoch, Transition transition);

  // Return the Time Zone offset, in the unit of nanosecond by int64_t, during
  // the time of the nanosecond_epoch.
  static int64_t GetTimeZoneOffsetNanoseconds(
      Isolate* isolate, int32_t time_zone_index,
      DirectHandle<BigInt> nanosecond_epoch);

  // This function may return the result, the std::vector<int64_t> in one of
  // the following three conditions:
  // 1. While nanosecond_epoch fall into the daylight saving time change
  // moment that skipped one (or two or even six, in some Time Zone) hours
  // later in local time:
  //    [],
  // 2. In other moment not during daylight saving time change:
  //    [offset_former], and
  // 3. when nanosecond_epoch fall into they daylight saving time change hour
  // which the clock time roll back one (or two or six, in some Time Zone) hour:
  //    [offset_former, offset_later]
  // The unit of the return values in BigInt is nanosecond.
  static DirectHandleVector<BigInt> GetTimeZonePossibleOffsetNanoseconds(
      Isolate* isolate, int32_t time_zone_index,
      DirectHandle<BigInt> nanosecond_epoch);

  static DirectHandle<String> DefaultTimeZone(Isolate* isolate);

  V8_WARN_UNUSED_RESULT static MaybeHandle<String> CanonicalizeTimeZoneName(
      Isolate* isolate, DirectHandle<String> identifier);

  // ecma402/#sec-coerceoptionstoobject
  V8_WARN_UNUSED_RESULT static MaybeDirectHandle<JSReceiver>
  CoerceOptionsToObject(Isolate* isolate, DirectHandle<Object> options,
                        const char* service);
};

}  // namespace v8::internal

#endif  // V8_OBJECTS_INTL_OBJECTS_H_
