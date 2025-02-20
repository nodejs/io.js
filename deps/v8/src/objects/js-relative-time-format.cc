// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-relative-time-format.h"

#include <map>
#include <memory>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-number-format.h"
#include "src/objects/js-relative-time-format-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/decimfmt.h"
#include "unicode/numfmt.h"
#include "unicode/reldatefmt.h"
#include "unicode/unum.h"

namespace v8 {
namespace internal {

namespace {
// Style: identifying the relative time format style used.
//
// ecma402/#sec-properties-of-intl-relativetimeformat-instances

enum class Style {
  LONG,   // Everything spelled out.
  SHORT,  // Abbreviations used when possible.
  NARROW  // Use the shortest possible form.
};

UDateRelativeDateTimeFormatterStyle toIcuStyle(Style style) {
  switch (style) {
    case Style::LONG:
      return UDAT_STYLE_LONG;
    case Style::SHORT:
      return UDAT_STYLE_SHORT;
    case Style::NARROW:
      return UDAT_STYLE_NARROW;
  }
  UNREACHABLE();
}

Style fromIcuStyle(UDateRelativeDateTimeFormatterStyle icu_style) {
  switch (icu_style) {
    case UDAT_STYLE_LONG:
      return Style::LONG;
    case UDAT_STYLE_SHORT:
      return Style::SHORT;
    case UDAT_STYLE_NARROW:
      return Style::NARROW;
    case UDAT_STYLE_COUNT:
      UNREACHABLE();
  }
  UNREACHABLE();
}
}  // namespace

MaybeDirectHandle<JSRelativeTimeFormat> JSRelativeTimeFormat::New(
    Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
    DirectHandle<Object> input_options) {
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, DirectHandle<JSRelativeTimeFormat>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  // 2. Set options to ? CoerceOptionsToObject(options).
  DirectHandle<JSReceiver> options;
  const char* service = "Intl.RelativeTimeFormat";
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, CoerceOptionsToObject(isolate, input_options, service));

  // 4. Let opt be a new Record.
  // 5. Let matcher be ? GetOption(options, "localeMatcher", "string", «
  // "lookup", "best fit" », "best fit").
  // 6. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeDirectHandle<JSRelativeTimeFormat>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // 7. Let _numberingSystem_ be ? GetOption(_options_, `"numberingSystem"`,
  //    `"string"`, *undefined*, *undefined*).
  std::unique_ptr<char[]> numbering_system_str = nullptr;
  Maybe<bool> maybe_numberingSystem = Intl::GetNumberingSystem(
      isolate, options, service, &numbering_system_str);
  // 8. If _numberingSystem_ is not *undefined*, then
  // a. If _numberingSystem_ does not match the
  //    `(3*8alphanum) *("-" (3*8alphanum))` sequence, throw a *RangeError*
  //     exception.
  MAYBE_RETURN(maybe_numberingSystem,
               MaybeDirectHandle<JSRelativeTimeFormat>());

  // 9. Set _opt_.[[nu]] to _numberingSystem_.

  // 10. Let localeData be %RelativeTimeFormat%.[[LocaleData]].
  // 11. Let r be
  // ResolveLocale(%RelativeTimeFormat%.[[AvailableLocales]],
  //               requestedLocales, opt,
  //               %RelativeTimeFormat%.[[RelevantExtensionKeys]], localeData).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSRelativeTimeFormat::GetAvailableLocales(),
                          requested_locales, matcher, {"nu"});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  UErrorCode status = U_ZERO_ERROR;

  icu::Locale icu_locale = r.icu_locale;
  if (numbering_system_str != nullptr) {
    auto nu_extension_it = r.extensions.find("nu");
    if (nu_extension_it != r.extensions.end() &&
        nu_extension_it->second != numbering_system_str.get()) {
      icu_locale.setUnicodeKeywordValue("nu", nullptr, status);
      DCHECK(U_SUCCESS(status));
    }
  }
  // 12. Let locale be r.[[Locale]].
  Maybe<std::string> maybe_locale_str = Intl::ToLanguageTag(icu_locale);
  MAYBE_RETURN(maybe_locale_str, MaybeDirectHandle<JSRelativeTimeFormat>());

  // 13. Set relativeTimeFormat.[[Locale]] to locale.
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(
          maybe_locale_str.FromJust().c_str());

  // 14. Set relativeTimeFormat.[[NumberingSystem]] to r.[[nu]].
  if (numbering_system_str != nullptr &&
      Intl::IsValidNumberingSystem(numbering_system_str.get())) {
    icu_locale.setUnicodeKeywordValue("nu", numbering_system_str.get(), status);
    DCHECK(U_SUCCESS(status));
  }
  // 15. Let dataLocale be r.[[DataLocale]].

  // 16. Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow"», "long").
  Maybe<Style> maybe_style = GetStringOption<Style>(
      isolate, options, "style", service, {"long", "short", "narrow"},
      {Style::LONG, Style::SHORT, Style::NARROW}, Style::LONG);
  MAYBE_RETURN(maybe_style, MaybeDirectHandle<JSRelativeTimeFormat>());
  Style style_enum = maybe_style.FromJust();

  // 17. Set relativeTimeFormat.[[Style]] to s.

  // 18. Let numeric be ? GetOption(options, "numeric", "string",
  //                                «"always", "auto"», "always").
  Maybe<Numeric> maybe_numeric = GetStringOption<Numeric>(
      isolate, options, "numeric", service, {"always", "auto"},
      {Numeric::ALWAYS, Numeric::AUTO}, Numeric::ALWAYS);
  MAYBE_RETURN(maybe_numeric, MaybeDirectHandle<JSRelativeTimeFormat>());
  Numeric numeric_enum = maybe_numeric.FromJust();

  // 19. Set relativeTimeFormat.[[Numeric]] to numeric.

  // 23. Let relativeTimeFormat.[[NumberFormat]] be
  //     ? Construct(%NumberFormat%, « nfLocale, nfOptions »).
  icu::NumberFormat* number_format =
      icu::NumberFormat::createInstance(icu_locale, UNUM_DECIMAL, status);
  if (U_FAILURE(status)) {
    // Data build filter files excluded data in "rbnf_tree" since ECMA402 does
    // not support "algorithmic" numbering systems. Therefore we may get the
    // U_MISSING_RESOURCE_ERROR here. Fallback to locale without the numbering
    // system and create the object again.
    if (status == U_MISSING_RESOURCE_ERROR) {
      delete number_format;
      status = U_ZERO_ERROR;
      icu_locale.setUnicodeKeywordValue("nu", nullptr, status);
      DCHECK(U_SUCCESS(status));
      number_format =
          icu::NumberFormat::createInstance(icu_locale, UNUM_DECIMAL, status);
    }
    if (U_FAILURE(status) || number_format == nullptr) {
      delete number_format;
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
    }
  }

  if (number_format->getDynamicClassID() ==
      icu::DecimalFormat::getStaticClassID()) {
    icu::DecimalFormat* decimal_format =
        static_cast<icu::DecimalFormat*>(number_format);
    decimal_format->setMinimumGroupingDigits(-2);
  }

  // Change UDISPCTX_CAPITALIZATION_NONE to other values if
  // ECMA402 later include option to change capitalization.
  // Ref: https://github.com/tc39/proposal-intl-relative-time/issues/11
  std::shared_ptr<icu::RelativeDateTimeFormatter> icu_formatter =
      std::make_shared<icu::RelativeDateTimeFormatter>(
          icu_locale, number_format, toIcuStyle(style_enum),
          UDISPCTX_CAPITALIZATION_NONE, status);
  if (U_FAILURE(status) || icu_formatter == nullptr) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }

  DirectHandle<String> numbering_system_string =
      isolate->factory()->NewStringFromAsciiChecked(
          Intl::GetNumberingSystem(icu_locale).c_str());

  DirectHandle<Managed<icu::RelativeDateTimeFormatter>> managed_formatter =
      Managed<icu::RelativeDateTimeFormatter>::From(isolate, 0,
                                                    std::move(icu_formatter));

  DirectHandle<JSRelativeTimeFormat> relative_time_format_holder =
      Cast<JSRelativeTimeFormat>(
          isolate->factory()->NewFastOrSlowJSObjectFromMap(map));

  DisallowGarbageCollection no_gc;
  relative_time_format_holder->set_flags(0);
  relative_time_format_holder->set_locale(*locale_str);
  relative_time_format_holder->set_numberingSystem(*numbering_system_string);
  relative_time_format_holder->set_numeric(numeric_enum);
  relative_time_format_holder->set_icu_formatter(*managed_formatter);

  // 25. Return relativeTimeFormat.
  return relative_time_format_holder;
}

namespace {

DirectHandle<String> StyleAsString(Isolate* isolate, Style style) {
  switch (style) {
    case Style::LONG:
      return isolate->factory()->long_string();
    case Style::SHORT:
      return isolate->factory()->short_string();
    case Style::NARROW:
      return isolate->factory()->narrow_string();
  }
  UNREACHABLE();
}

}  // namespace

DirectHandle<JSObject> JSRelativeTimeFormat::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSRelativeTimeFormat> format_holder) {
  Factory* factory = isolate->factory();
  icu::RelativeDateTimeFormatter* formatter =
      format_holder->icu_formatter()->raw();
  DCHECK_NOT_NULL(formatter);
  DirectHandle<JSObject> result =
      factory->NewJSObject(isolate->object_function());
  DirectHandle<String> locale(format_holder->locale(), isolate);
  DirectHandle<String> numberingSystem(format_holder->numberingSystem(),
                                       isolate);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(
      isolate, result, factory->style_string(),
      StyleAsString(isolate, fromIcuStyle(formatter->getFormatStyle())), NONE);
  JSObject::AddProperty(isolate, result, factory->numeric_string(),
                        format_holder->NumericAsString(isolate), NONE);
  JSObject::AddProperty(isolate, result, factory->numberingSystem_string(),
                        numberingSystem, NONE);
  return result;
}

Handle<String> JSRelativeTimeFormat::NumericAsString(Isolate* isolate) const {
  switch (numeric()) {
    case Numeric::ALWAYS:
      return isolate->factory()->always_string();
    case Numeric::AUTO:
      return isolate->factory()->auto_string();
  }
  UNREACHABLE();
}

namespace {

DirectHandle<String> UnitAsString(Isolate* isolate,
                                  URelativeDateTimeUnit unit_enum) {
  Factory* factory = isolate->factory();
  switch (unit_enum) {
    case UDAT_REL_UNIT_SECOND:
      return factory->second_string();
    case UDAT_REL_UNIT_MINUTE:
      return factory->minute_string();
    case UDAT_REL_UNIT_HOUR:
      return factory->hour_string();
    case UDAT_REL_UNIT_DAY:
      return factory->day_string();
    case UDAT_REL_UNIT_WEEK:
      return factory->week_string();
    case UDAT_REL_UNIT_MONTH:
      return factory->month_string();
    case UDAT_REL_UNIT_QUARTER:
      return factory->quarter_string();
    case UDAT_REL_UNIT_YEAR:
      return factory->year_string();
    default:
      UNREACHABLE();
  }
}

bool GetURelativeDateTimeUnit(DirectHandle<String> unit,
                              URelativeDateTimeUnit* unit_enum) {
  std::unique_ptr<char[]> unit_str = unit->ToCString();
  if ((strcmp("second", unit_str.get()) == 0) ||
      (strcmp("seconds", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_SECOND;
  } else if ((strcmp("minute", unit_str.get()) == 0) ||
             (strcmp("minutes", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_MINUTE;
  } else if ((strcmp("hour", unit_str.get()) == 0) ||
             (strcmp("hours", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_HOUR;
  } else if ((strcmp("day", unit_str.get()) == 0) ||
             (strcmp("days", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_DAY;
  } else if ((strcmp("week", unit_str.get()) == 0) ||
             (strcmp("weeks", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_WEEK;
  } else if ((strcmp("month", unit_str.get()) == 0) ||
             (strcmp("months", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_MONTH;
  } else if ((strcmp("quarter", unit_str.get()) == 0) ||
             (strcmp("quarters", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_QUARTER;
  } else if ((strcmp("year", unit_str.get()) == 0) ||
             (strcmp("years", unit_str.get()) == 0)) {
    *unit_enum = UDAT_REL_UNIT_YEAR;
  } else {
    return false;
  }
  return true;
}

template <typename T>
MaybeDirectHandle<T> FormatCommon(
    Isolate* isolate, DirectHandle<JSRelativeTimeFormat> format,
    Handle<Object> value_obj, Handle<Object> unit_obj, const char* func_name,
    MaybeDirectHandle<T> (*formatToResult)(
        Isolate*, const icu::FormattedRelativeDateTime&, DirectHandle<String>,
        bool)) {
  // 3. Let value be ? ToNumber(value).
  DirectHandle<Object> value;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                             Object::ToNumber(isolate, value_obj));
  double number = Object::NumberValue(*value);
  // 4. Let unit be ? ToString(unit).
  Handle<String> unit;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, unit,
                             Object::ToString(isolate, unit_obj));
  // 4. If isFinite(value) is false, then throw a RangeError exception.
  if (!std::isfinite(number)) {
    THROW_NEW_ERROR(
        isolate, NewRangeError(
                     MessageTemplate::kNotFiniteNumber,
                     isolate->factory()->NewStringFromAsciiChecked(func_name)));
  }
  icu::RelativeDateTimeFormatter* formatter = format->icu_formatter()->raw();
  DCHECK_NOT_NULL(formatter);
  URelativeDateTimeUnit unit_enum;
  if (!GetURelativeDateTimeUnit(unit, &unit_enum)) {
    THROW_NEW_ERROR(
        isolate,
        NewRangeError(MessageTemplate::kInvalidUnit,
                      isolate->factory()->NewStringFromAsciiChecked(func_name),
                      unit));
  }
  UErrorCode status = U_ZERO_ERROR;
  icu::FormattedRelativeDateTime formatted =
      (format->numeric() == JSRelativeTimeFormat::Numeric::ALWAYS)
          ? formatter->formatNumericToValue(number, unit_enum, status)
          : formatter->formatToValue(number, unit_enum, status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return formatToResult(isolate, formatted, UnitAsString(isolate, unit_enum),
                        IsNaN(*value));
}

MaybeDirectHandle<String> FormatToString(
    Isolate* isolate, const icu::FormattedRelativeDateTime& formatted,
    DirectHandle<String> unit, bool is_nan) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return Intl::ToString(isolate, result);
}

Maybe<bool> AddLiteral(Isolate* isolate, DirectHandle<JSArray> array,
                       const icu::UnicodeString& string, int32_t index,
                       int32_t start, int32_t limit) {
  DirectHandle<String> substring;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, substring, Intl::ToString(isolate, string, start, limit),
      Nothing<bool>());
  Intl::AddElement(isolate, array, index, isolate->factory()->literal_string(),
                   substring);
  return Just(true);
}

Maybe<bool> AddUnit(Isolate* isolate, DirectHandle<JSArray> array,
                    const icu::UnicodeString& string, int32_t index,
                    const NumberFormatSpan& part, DirectHandle<String> unit,
                    bool is_nan) {
  DirectHandle<String> substring;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, substring,
      Intl::ToString(isolate, string, part.begin_pos, part.end_pos),
      Nothing<bool>());
  Intl::AddElement(isolate, array, index,
                   Intl::NumberFieldToType(isolate, part, string, is_nan),
                   substring, isolate->factory()->unit_string(), unit);
  return Just(true);
}

MaybeDirectHandle<JSArray> FormatToJSArray(
    Isolate* isolate, const icu::FormattedRelativeDateTime& formatted,
    DirectHandle<String> unit, bool is_nan) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString string = formatted.toString(status);

  Factory* factory = isolate->factory();
  DirectHandle<JSArray> array = factory->NewJSArray(0);
  icu::ConstrainedFieldPosition cfpos;
  cfpos.constrainCategory(UFIELD_CATEGORY_NUMBER);
  int32_t index = 0;

  int32_t previous_end = 0;
  DirectHandle<String> substring;
  std::vector<std::pair<int32_t, int32_t>> groups;
  while (formatted.nextPosition(cfpos, status) && U_SUCCESS(status)) {
    int32_t category = cfpos.getCategory();
    int32_t field = cfpos.getField();
    int32_t start = cfpos.getStart();
    int32_t limit = cfpos.getLimit();
    if (category == UFIELD_CATEGORY_NUMBER) {
      if (field == UNUM_GROUPING_SEPARATOR_FIELD) {
        groups.push_back(std::pair<int32_t, int32_t>(start, limit));
        continue;
      }
      if (start > previous_end) {
        Maybe<bool> maybe_added =
            AddLiteral(isolate, array, string, index++, previous_end, start);
        MAYBE_RETURN(maybe_added, DirectHandle<JSArray>());
      }
      if (field == UNUM_INTEGER_FIELD) {
        for (auto start_limit : groups) {
          if (start_limit.first > start) {
            Maybe<bool> maybe_added =
                AddUnit(isolate, array, string, index++,
                        NumberFormatSpan(field, start, start_limit.first), unit,
                        is_nan);
            MAYBE_RETURN(maybe_added, DirectHandle<JSArray>());
            maybe_added =
                AddUnit(isolate, array, string, index++,
                        NumberFormatSpan(UNUM_GROUPING_SEPARATOR_FIELD,
                                         start_limit.first, start_limit.second),
                        unit, is_nan);
            MAYBE_RETURN(maybe_added, DirectHandle<JSArray>());
            start = start_limit.second;
          }
        }
      }
      Maybe<bool> maybe_added =
          AddUnit(isolate, array, string, index++,
                  NumberFormatSpan(field, start, limit), unit, is_nan);
      MAYBE_RETURN(maybe_added, DirectHandle<JSArray>());
      previous_end = limit;
    }
  }
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  if (string.length() > previous_end) {
    Maybe<bool> maybe_added = AddLiteral(isolate, array, string, index,
                                         previous_end, string.length());
    MAYBE_RETURN(maybe_added, DirectHandle<JSArray>());
  }

  JSObject::ValidateElements(*array);
  return array;
}

}  // namespace

MaybeDirectHandle<String> JSRelativeTimeFormat::Format(
    Isolate* isolate, Handle<Object> value_obj, Handle<Object> unit_obj,
    DirectHandle<JSRelativeTimeFormat> format) {
  return FormatCommon<String>(isolate, format, value_obj, unit_obj,
                              "Intl.RelativeTimeFormat.prototype.format",
                              FormatToString);
}

MaybeDirectHandle<JSArray> JSRelativeTimeFormat::FormatToParts(
    Isolate* isolate, Handle<Object> value_obj, Handle<Object> unit_obj,
    DirectHandle<JSRelativeTimeFormat> format) {
  return FormatCommon<JSArray>(
      isolate, format, value_obj, unit_obj,
      "Intl.RelativeTimeFormat.prototype.formatToParts", FormatToJSArray);
}

const std::set<std::string>& JSRelativeTimeFormat::GetAvailableLocales() {
  // Since RelativeTimeFormatter does not have a method to list all
  // available locales, work around by calling the DateFormat.
  return Intl::GetAvailableLocalesForDateFormat();
}

}  // namespace internal
}  // namespace v8
