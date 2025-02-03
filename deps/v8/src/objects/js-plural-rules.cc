// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-plural-rules.h"

#include "src/execution/isolate-inl.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-number-format.h"
#include "src/objects/js-plural-rules-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/locid.h"
#include "unicode/numberformatter.h"
#include "unicode/numberrangeformatter.h"
#include "unicode/plurrule.h"
#include "unicode/unumberformatter.h"

namespace v8 {
namespace internal {

namespace {

bool CreateICUPluralRules(Isolate* isolate, const icu::Locale& icu_locale,
                          JSPluralRules::Type type,
                          std::unique_ptr<icu::PluralRules>* pl) {
  // Make formatter from options. Numbering system is added
  // to the locale as Unicode extension (if it was specified at all).
  UErrorCode status = U_ZERO_ERROR;

  UPluralType icu_type = UPLURAL_TYPE_CARDINAL;
  if (type == JSPluralRules::Type::ORDINAL) {
    icu_type = UPLURAL_TYPE_ORDINAL;
  } else {
    DCHECK_EQ(JSPluralRules::Type::CARDINAL, type);
  }

  std::unique_ptr<icu::PluralRules> plural_rules(
      icu::PluralRules::forLocale(icu_locale, icu_type, status));
  if (U_FAILURE(status)) {
    return false;
  }
  DCHECK_NOT_NULL(plural_rules.get());

  *pl = std::move(plural_rules);
  return true;
}

}  // namespace

Handle<String> JSPluralRules::TypeAsString() const {
  switch (type()) {
    case Type::CARDINAL:
      return GetReadOnlyRoots().cardinal_string_handle();
    case Type::ORDINAL:
      return GetReadOnlyRoots().ordinal_string_handle();
  }
  UNREACHABLE();
}

// static
MaybeHandle<JSPluralRules> JSPluralRules::New(Isolate* isolate,
                                              DirectHandle<Map> map,
                                              Handle<Object> locales,
                                              Handle<Object> options_obj) {
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, Handle<JSPluralRules>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  // 2. Set options to ? CoerceOptionsToObject(options).
  Handle<JSReceiver> options;
  const char* service = "Intl.PluralRules";
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, CoerceOptionsToObject(isolate, options_obj, service));

  // 5. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 6. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSPluralRules>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // 7. Let t be ? GetOption(options, "type", "string", « "cardinal",
  // "ordinal" », "cardinal").
  Maybe<Type> maybe_type = GetStringOption<Type>(
      isolate, options, "type", service, {"cardinal", "ordinal"},
      {Type::CARDINAL, Type::ORDINAL}, Type::CARDINAL);
  MAYBE_RETURN(maybe_type, MaybeHandle<JSPluralRules>());
  Type type = maybe_type.FromJust();

  // Note: The spec says we should do ResolveLocale after performing
  // SetNumberFormatDigitOptions but we need the locale to create all
  // the ICU data structures.
  //
  // This isn't observable so we aren't violating the spec.

  // 11. Let r be ResolveLocale(%PluralRules%.[[AvailableLocales]],
  // requestedLocales, opt, %PluralRules%.[[RelevantExtensionKeys]],
  // localeData).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSPluralRules::GetAvailableLocales(),
                          requested_locales, matcher, {});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(r.locale.c_str());

  icu::Locale icu_locale = r.icu_locale;
  icu::number::UnlocalizedNumberFormatter settings =
      icu::number::UnlocalizedNumberFormatter().roundingMode(UNUM_ROUND_HALFUP);

  std::unique_ptr<icu::PluralRules> icu_plural_rules;
  bool success =
      CreateICUPluralRules(isolate, r.icu_locale, type, &icu_plural_rules);
  if (!success || icu_plural_rules == nullptr) {
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    success = CreateICUPluralRules(isolate, no_extension_locale, type,
                                   &icu_plural_rules);
    icu_locale = no_extension_locale;

    if (!success || icu_plural_rules == nullptr) {
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
    }
  }

  // 9. Perform ? SetNumberFormatDigitOptions(pluralRules, options, 0, 3).
  Maybe<Intl::NumberFormatDigitOptions> maybe_digit_options =
      Intl::SetNumberFormatDigitOptions(isolate, options, 0, 3, false, service);
  MAYBE_RETURN(maybe_digit_options, MaybeHandle<JSPluralRules>());
  Intl::NumberFormatDigitOptions digit_options = maybe_digit_options.FromJust();
  settings =
      JSNumberFormat::SetDigitOptionsToFormatter(settings, digit_options);

  icu::number::LocalizedNumberFormatter icu_number_formatter =
      settings.locale(icu_locale);

  DirectHandle<Managed<icu::PluralRules>> managed_plural_rules =
      Managed<icu::PluralRules>::From(isolate, 0, std::move(icu_plural_rules));

  DirectHandle<Managed<icu::number::LocalizedNumberFormatter>>
      managed_number_formatter =
          Managed<icu::number::LocalizedNumberFormatter>::From(
              isolate, 0,
              std::make_shared<icu::number::LocalizedNumberFormatter>(
                  icu_number_formatter));

  // Now all properties are ready, so we can allocate the result object.
  Handle<JSPluralRules> plural_rules = Cast<JSPluralRules>(
      isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  plural_rules->set_flags(0);

  // 8. Set pluralRules.[[Type]] to t.
  plural_rules->set_type(type);

  // 12. Set pluralRules.[[Locale]] to the value of r.[[locale]].
  plural_rules->set_locale(*locale_str);

  plural_rules->set_icu_plural_rules(*managed_plural_rules);
  plural_rules->set_icu_number_formatter(*managed_number_formatter);

  // 13. Return pluralRules.
  return plural_rules;
}

MaybeHandle<String> JSPluralRules::ResolvePlural(
    Isolate* isolate, DirectHandle<JSPluralRules> plural_rules, double number) {
  icu::PluralRules* icu_plural_rules = plural_rules->icu_plural_rules()->raw();
  DCHECK_NOT_NULL(icu_plural_rules);

  icu::number::LocalizedNumberFormatter* fmt =
      plural_rules->icu_number_formatter()->raw();
  DCHECK_NOT_NULL(fmt);

  UErrorCode status = U_ZERO_ERROR;
  icu::number::FormattedNumber formatted_number =
      fmt->formatDouble(number, status);
  DCHECK(U_SUCCESS(status));

  icu::UnicodeString result =
      icu_plural_rules->select(formatted_number, status);
  DCHECK(U_SUCCESS(status));

  return Intl::ToString(isolate, result);
}

MaybeHandle<String> JSPluralRules::ResolvePluralRange(
    Isolate* isolate, DirectHandle<JSPluralRules> plural_rules, double x,
    double y) {
  icu::PluralRules* icu_plural_rules = plural_rules->icu_plural_rules()->raw();
  DCHECK_NOT_NULL(icu_plural_rules);

  Maybe<icu::number::LocalizedNumberRangeFormatter> maybe_range_formatter =
      JSNumberFormat::GetRangeFormatter(
          isolate, plural_rules->locale(),
          *plural_rules->icu_number_formatter()->raw());
  MAYBE_RETURN(maybe_range_formatter, MaybeHandle<String>());

  icu::number::LocalizedNumberRangeFormatter nrfmt =
      maybe_range_formatter.FromJust();

  UErrorCode status = U_ZERO_ERROR;
  icu::number::FormattedNumberRange formatted = nrfmt.formatFormattableRange(
      icu::Formattable(x), icu::Formattable(y), status);

  DCHECK(U_SUCCESS(status));
  icu::UnicodeString result = icu_plural_rules->select(formatted, status);
  DCHECK(U_SUCCESS(status));

  return Intl::ToString(isolate, result);
}

namespace {

void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  Handle<Object> value, const char* key) {
  Handle<String> key_str = isolate->factory()->NewStringFromAsciiChecked(key);

  // This is a brand new JSObject that shouldn't already have the same
  // key so this shouldn't fail.
  Maybe<bool> maybe = JSReceiver::CreateDataProperty(isolate, options, key_str,
                                                     value, Just(kDontThrow));
  DCHECK(maybe.FromJust());
  USE(maybe);
}

void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  int value, const char* key) {
  Handle<Smi> value_smi(Smi::FromInt(value), isolate);
  CreateDataPropertyForOptions(isolate, options, value_smi, key);
}

}  // namespace

Handle<JSObject> JSPluralRules::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSPluralRules> plural_rules) {
  Handle<JSObject> options =
      isolate->factory()->NewJSObject(isolate->object_function());

  Handle<String> locale_value(plural_rules->locale(), isolate);
  CreateDataPropertyForOptions(isolate, options, locale_value, "locale");

  CreateDataPropertyForOptions(isolate, options, plural_rules->TypeAsString(),
                               "type");

  UErrorCode status = U_ZERO_ERROR;
  icu::number::LocalizedNumberFormatter* icu_number_formatter =
      plural_rules->icu_number_formatter()->raw();
  icu::UnicodeString skeleton = icu_number_formatter->toSkeleton(status);
  DCHECK(U_SUCCESS(status));

  CreateDataPropertyForOptions(
      isolate, options,
      JSNumberFormat::MinimumIntegerDigitsFromSkeleton(skeleton),
      "minimumIntegerDigits");
  int32_t min = 0, max = 0;

  if (JSNumberFormat::SignificantDigitsFromSkeleton(skeleton, &min, &max)) {
    CreateDataPropertyForOptions(isolate, options, min,
                                 "minimumSignificantDigits");
    CreateDataPropertyForOptions(isolate, options, max,
                                 "maximumSignificantDigits");
  } else {
    JSNumberFormat::FractionDigitsFromSkeleton(skeleton, &min, &max);
    CreateDataPropertyForOptions(isolate, options, min,
                                 "minimumFractionDigits");
    CreateDataPropertyForOptions(isolate, options, max,
                                 "maximumFractionDigits");
  }

  // 6. Let pluralCategories be a List of Strings representing the
  // possible results of PluralRuleSelect for the selected locale pr.
  icu::PluralRules* icu_plural_rules = plural_rules->icu_plural_rules()->raw();
  DCHECK_NOT_NULL(icu_plural_rules);

  std::unique_ptr<icu::StringEnumeration> categories(
      icu_plural_rules->getKeywords(status));
  DCHECK(U_SUCCESS(status));
  int32_t count = categories->count(status);
  DCHECK(U_SUCCESS(status));

  Factory* factory = isolate->factory();
  DirectHandle<FixedArray> plural_categories = factory->NewFixedArray(count);
  for (int32_t i = 0; i < count; i++) {
    const icu::UnicodeString* category = categories->snext(status);
    DCHECK(U_SUCCESS(status));
    if (category == nullptr) break;

    std::string keyword;
    DirectHandle<String> value = factory->NewStringFromAsciiChecked(
        category->toUTF8String(keyword).data());
    plural_categories->set(i, *value);
  }

  // 7. Perform ! CreateDataProperty(options, "pluralCategories",
  // CreateArrayFromList(pluralCategories)).
  Handle<JSArray> plural_categories_value =
      factory->NewJSArrayWithElements(plural_categories);
  CreateDataPropertyForOptions(isolate, options, plural_categories_value,
                               "pluralCategories");

  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->roundingIncrement_string(),
            JSNumberFormat::RoundingIncrement(isolate, skeleton),
            Just(kDontThrow))
            .FromJust());
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->roundingMode_string(),
            JSNumberFormat::RoundingModeString(isolate, skeleton),
            Just(kDontThrow))
            .FromJust());
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->roundingPriority_string(),
            JSNumberFormat::RoundingPriorityString(isolate, skeleton),
            Just(kDontThrow))
            .FromJust());
  CHECK(JSReceiver::CreateDataProperty(
            isolate, options, factory->trailingZeroDisplay_string(),
            JSNumberFormat::TrailingZeroDisplayString(isolate, skeleton),
            Just(kDontThrow))
            .FromJust());

  return options;
}

namespace {

class PluralRulesAvailableLocales {
 public:
  PluralRulesAvailableLocales() {
    UErrorCode status = U_ZERO_ERROR;
    std::unique_ptr<icu::StringEnumeration> locales(
        icu::PluralRules::getAvailableLocales(status));
    DCHECK(U_SUCCESS(status));
    int32_t len = 0;
    const char* locale = nullptr;
    while ((locale = locales->next(&len, status)) != nullptr &&
           U_SUCCESS(status)) {
      std::string str(locale);
      if (len > 3) {
        std::replace(str.begin(), str.end(), '_', '-');
      }
      set_.insert(std::move(str));
    }
  }
  const std::set<std::string>& Get() const { return set_; }

 private:
  std::set<std::string> set_;
};

}  // namespace

const std::set<std::string>& JSPluralRules::GetAvailableLocales() {
  static base::LazyInstance<PluralRulesAvailableLocales>::type
      available_locales = LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

}  // namespace internal
}  // namespace v8
