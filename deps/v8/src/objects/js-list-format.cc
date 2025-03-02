// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-list-format.h"

#include <memory>
#include <vector>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/elements-inl.h"
#include "src/objects/elements.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-array-inl.h"
#include "src/objects/js-list-format-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/fieldpos.h"
#include "unicode/fpositer.h"
#include "unicode/listformatter.h"
#include "unicode/ulistformatter.h"

namespace v8 {
namespace internal {

namespace {

UListFormatterWidth GetIcuWidth(JSListFormat::Style style) {
  switch (style) {
    case JSListFormat::Style::LONG:
      return ULISTFMT_WIDTH_WIDE;
    case JSListFormat::Style::SHORT:
      return ULISTFMT_WIDTH_SHORT;
    case JSListFormat::Style::NARROW:
      return ULISTFMT_WIDTH_NARROW;
  }
  UNREACHABLE();
}

UListFormatterType GetIcuType(JSListFormat::Type type) {
  switch (type) {
    case JSListFormat::Type::CONJUNCTION:
      return ULISTFMT_TYPE_AND;
    case JSListFormat::Type::DISJUNCTION:
      return ULISTFMT_TYPE_OR;
    case JSListFormat::Type::UNIT:
      return ULISTFMT_TYPE_UNITS;
  }
  UNREACHABLE();
}

}  // namespace

MaybeDirectHandle<JSListFormat> JSListFormat::New(
    Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
    DirectHandle<Object> input_options) {
  // 3. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, DirectHandle<JSListFormat>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  DirectHandle<JSReceiver> options;
  const char* service = "Intl.ListFormat";
  // 4. Let options be GetOptionsObject(_options_).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, input_options, service));

  // Note: No need to create a record. It's not observable.
  // 6. Let opt be a new Record.

  // 7. Let matcher be ? GetOption(options, "localeMatcher", "string", «
  // "lookup", "best fit" », "best fit").
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeDirectHandle<JSListFormat>());

  // 8. Set opt.[[localeMatcher]] to matcher.
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // 10. Let r be ResolveLocale(%ListFormat%.[[AvailableLocales]],
  // requestedLocales, opt, undefined, localeData).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSListFormat::GetAvailableLocales(),
                          requested_locales, matcher, {});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(r.locale.c_str());

  // 12. Let t be GetOption(options, "type", "string", «"conjunction",
  //    "disjunction", "unit"», "conjunction").
  Maybe<Type> maybe_type = GetStringOption<Type>(
      isolate, options, "type", service, {"conjunction", "disjunction", "unit"},
      {Type::CONJUNCTION, Type::DISJUNCTION, Type::UNIT}, Type::CONJUNCTION);
  MAYBE_RETURN(maybe_type, MaybeDirectHandle<JSListFormat>());
  Type type_enum = maybe_type.FromJust();

  // 14. Let s be ? GetOption(options, "style", "string",
  //                          «"long", "short", "narrow"», "long").
  Maybe<Style> maybe_style = GetStringOption<Style>(
      isolate, options, "style", service, {"long", "short", "narrow"},
      {Style::LONG, Style::SHORT, Style::NARROW}, Style::LONG);
  MAYBE_RETURN(maybe_style, MaybeDirectHandle<JSListFormat>());
  Style style_enum = maybe_style.FromJust();

  icu::Locale icu_locale = r.icu_locale;
  UErrorCode status = U_ZERO_ERROR;
  std::shared_ptr<icu::ListFormatter> formatter{
      icu::ListFormatter::createInstance(icu_locale, GetIcuType(type_enum),
                                         GetIcuWidth(style_enum), status)};
  if (U_FAILURE(status) || formatter == nullptr) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }

  DirectHandle<Managed<icu::ListFormatter>> managed_formatter =
      Managed<icu::ListFormatter>::From(isolate, 0, std::move(formatter));

  // Now all properties are ready, so we can allocate the result object.
  DirectHandle<JSListFormat> list_format =
      Cast<JSListFormat>(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  list_format->set_flags(0);
  list_format->set_icu_formatter(*managed_formatter);

  // 11. Set listFormat.[[Locale]] to r.[[Locale]].
  list_format->set_locale(*locale_str);

  // 13. Set listFormat.[[Type]] to t.
  list_format->set_type(type_enum);

  // 15. Set listFormat.[[Style]] to s.
  list_format->set_style(style_enum);

  return list_format;
}

// ecma402 #sec-intl.pluralrules.prototype.resolvedoptions
DirectHandle<JSObject> JSListFormat::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSListFormat> format) {
  Factory* factory = isolate->factory();
  // 4. Let options be ! ObjectCreate(%ObjectPrototype%).
  DirectHandle<JSObject> result =
      factory->NewJSObject(isolate->object_function());

  // 5.  For each row of Table 1, except the header row, do
  //  Table 1: Resolved Options of ListFormat Instances
  //  Internal Slot    Property
  //  [[Locale]]       "locale"
  //  [[Type]]         "type"
  //  [[Style]]        "style"
  DirectHandle<String> locale(format->locale(), isolate);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->type_string(),
                        format->TypeAsString(isolate), NONE);
  JSObject::AddProperty(isolate, result, factory->style_string(),
                        format->StyleAsString(isolate), NONE);
  // 6. Return options.
  return result;
}

Handle<String> JSListFormat::StyleAsString(Isolate* isolate) const {
  switch (style()) {
    case Style::LONG:
      return isolate->factory()->long_string();
    case Style::SHORT:
      return isolate->factory()->short_string();
    case Style::NARROW:
      return isolate->factory()->narrow_string();
  }
  UNREACHABLE();
}

Handle<String> JSListFormat::TypeAsString(Isolate* isolate) const {
  switch (type()) {
    case Type::CONJUNCTION:
      return isolate->factory()->conjunction_string();
    case Type::DISJUNCTION:
      return isolate->factory()->disjunction_string();
    case Type::UNIT:
      return isolate->factory()->unit_string();
  }
  UNREACHABLE();
}

namespace {

// Extract String from FixedArray into array of UnicodeString
Maybe<std::vector<icu::UnicodeString>> ToUnicodeStringArray(
    Isolate* isolate, DirectHandle<FixedArray> array) {
  int length = array->length();
  std::vector<icu::UnicodeString> result;
  for (int i = 0; i < length; i++) {
    Handle<Object> item(array->get(i), isolate);
    DCHECK(IsString(*item));
    Handle<String> item_str = Cast<String>(item);
    if (!item_str->IsFlat()) item_str = String::Flatten(isolate, item_str);
    result.push_back(Intl::ToICUUnicodeString(isolate, item_str));
  }
  return Just(result);
}

template <typename T>
MaybeDirectHandle<T> FormatListCommon(
    Isolate* isolate, DirectHandle<JSListFormat> format,
    DirectHandle<FixedArray> list,
    const std::function<MaybeDirectHandle<T>(
        Isolate*, const icu::FormattedValue&)>& formatToResult) {
  DCHECK(!IsUndefined(*list));
  Maybe<std::vector<icu::UnicodeString>> maybe_array =
      ToUnicodeStringArray(isolate, list);
  MAYBE_RETURN(maybe_array, DirectHandle<T>());
  std::vector<icu::UnicodeString> array = maybe_array.FromJust();

  icu::ListFormatter* formatter = format->icu_formatter()->raw();
  DCHECK_NOT_NULL(formatter);

  UErrorCode status = U_ZERO_ERROR;
  icu::FormattedList formatted = formatter->formatStringsToValue(
      array.data(), static_cast<int32_t>(array.size()), status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return formatToResult(isolate, formatted);
}

DirectHandle<String> IcuFieldIdToType(Isolate* isolate, int32_t field_id) {
  switch (field_id) {
    case ULISTFMT_LITERAL_FIELD:
      return isolate->factory()->literal_string();
    case ULISTFMT_ELEMENT_FIELD:
      return isolate->factory()->element_string();
    default:
      UNREACHABLE();
  }
}

// A helper function to convert the FormattedList to a
// MaybeHandle<JSArray> for the implementation of formatToParts.
MaybeDirectHandle<JSArray> FormattedListToJSArray(
    Isolate* isolate, const icu::FormattedValue& formatted) {
  DirectHandle<JSArray> array = isolate->factory()->NewJSArray(0);
  icu::ConstrainedFieldPosition cfpos;
  cfpos.constrainCategory(UFIELD_CATEGORY_LIST);
  int index = 0;
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString string = formatted.toString(status);
  DirectHandle<String> substring;
  while (formatted.nextPosition(cfpos, status) && U_SUCCESS(status)) {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, substring,
        Intl::ToString(isolate, string, cfpos.getStart(), cfpos.getLimit()));
    Intl::AddElement(isolate, array, index++,
                     IcuFieldIdToType(isolate, cfpos.getField()), substring);
  }
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  JSObject::ValidateElements(*array);
  return array;
}

}  // namespace

// ecma402 #sec-formatlist
MaybeDirectHandle<String> JSListFormat::FormatList(
    Isolate* isolate, DirectHandle<JSListFormat> format,
    DirectHandle<FixedArray> list) {
  return FormatListCommon<String>(isolate, format, list,
                                  Intl::FormattedToString);
}

// ecma42 #sec-formatlisttoparts
MaybeDirectHandle<JSArray> JSListFormat::FormatListToParts(
    Isolate* isolate, DirectHandle<JSListFormat> format,
    DirectHandle<FixedArray> list) {
  return FormatListCommon<JSArray>(isolate, format, list,
                                   FormattedListToJSArray);
}

namespace {

struct CheckListPattern {
  static const char* key() { return "listPattern"; }
  static const char* path() { return nullptr; }
};

}  // namespace

const std::set<std::string>& JSListFormat::GetAvailableLocales() {
  static base::LazyInstance<Intl::AvailableLocales<CheckListPattern>>::type
      available_locales = LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

}  // namespace internal
}  // namespace v8
