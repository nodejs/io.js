// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-collator.h"

#include "src/execution/isolate.h"
#include "src/objects/js-collator-inl.h"
#include "src/objects/js-locale.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/coll.h"
#include "unicode/locid.h"
#include "unicode/strenum.h"
#include "unicode/ucol.h"
#include "unicode/udata.h"
#include "unicode/uloc.h"
#include "unicode/utypes.h"

namespace v8 {
namespace internal {

namespace {

enum class Usage {
  SORT,
  SEARCH,
};

enum class Sensitivity {
  kBase,
  kAccent,
  kCase,
  kVariant,
  kUndefined,
};

// enum for "caseFirst" option.
enum class CaseFirst { kUndefined, kUpper, kLower, kFalse };

Maybe<CaseFirst> GetCaseFirst(Isolate* isolate, Handle<JSReceiver> options,
                              const char* method_name) {
  return GetStringOption<CaseFirst>(
      isolate, options, "caseFirst", method_name, {"upper", "lower", "false"},
      {CaseFirst::kUpper, CaseFirst::kLower, CaseFirst::kFalse},
      CaseFirst::kUndefined);
}

// TODO(gsathya): Consider internalizing the value strings.
void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  Handle<String> key, const char* value) {
  DCHECK_NOT_NULL(value);
  Handle<String> value_str =
      isolate->factory()->NewStringFromAsciiChecked(value);

  // This is a brand new JSObject that shouldn't already have the same
  // key so this shouldn't fail.
  Maybe<bool> maybe = JSReceiver::CreateDataProperty(
      isolate, options, key, value_str, Just(kDontThrow));
  DCHECK(maybe.FromJust());
  USE(maybe);
}

void CreateDataPropertyForOptions(Isolate* isolate, Handle<JSObject> options,
                                  Handle<String> key, bool value) {
  Handle<Object> value_obj = isolate->factory()->ToBoolean(value);

  // This is a brand new JSObject that shouldn't already have the same
  // key so this shouldn't fail.
  Maybe<bool> maybe = JSReceiver::CreateDataProperty(
      isolate, options, key, value_obj, Just(kDontThrow));
  DCHECK(maybe.FromJust());
  USE(maybe);
}

}  // anonymous namespace

// static
Handle<JSObject> JSCollator::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSCollator> collator) {
  Handle<JSObject> options =
      isolate->factory()->NewJSObject(isolate->object_function());

  icu::Collator* icu_collator = collator->icu_collator()->raw();
  DCHECK_NOT_NULL(icu_collator);

  UErrorCode status = U_ZERO_ERROR;
  bool numeric =
      icu_collator->getAttribute(UCOL_NUMERIC_COLLATION, status) == UCOL_ON;
  DCHECK(U_SUCCESS(status));

  const char* case_first = nullptr;
  status = U_ZERO_ERROR;
  switch (icu_collator->getAttribute(UCOL_CASE_FIRST, status)) {
    case UCOL_LOWER_FIRST:
      case_first = "lower";
      break;
    case UCOL_UPPER_FIRST:
      case_first = "upper";
      break;
    default:
      case_first = "false";
  }
  DCHECK(U_SUCCESS(status));

  const char* sensitivity = nullptr;
  status = U_ZERO_ERROR;
  switch (icu_collator->getAttribute(UCOL_STRENGTH, status)) {
    case UCOL_PRIMARY: {
      DCHECK(U_SUCCESS(status));
      status = U_ZERO_ERROR;
      // case level: true + s1 -> case, s1 -> base.
      if (UCOL_ON == icu_collator->getAttribute(UCOL_CASE_LEVEL, status)) {
        sensitivity = "case";
      } else {
        sensitivity = "base";
      }
      DCHECK(U_SUCCESS(status));
      break;
    }
    case UCOL_SECONDARY:
      sensitivity = "accent";
      break;
    case UCOL_TERTIARY:
      sensitivity = "variant";
      break;
    case UCOL_QUATERNARY:
      // We shouldn't get quaternary and identical from ICU, but if we do
      // put them into variant.
      sensitivity = "variant";
      break;
    default:
      sensitivity = "variant";
  }
  DCHECK(U_SUCCESS(status));

  status = U_ZERO_ERROR;
  bool ignore_punctuation = icu_collator->getAttribute(UCOL_ALTERNATE_HANDLING,
                                                       status) == UCOL_SHIFTED;
  DCHECK(U_SUCCESS(status));

  status = U_ZERO_ERROR;

  icu::Locale icu_locale(icu_collator->getLocale(ULOC_VALID_LOCALE, status));
  DCHECK(U_SUCCESS(status));

  const char* collation = "default";
  const char* usage = "sort";
  const char* collation_key = "co";
  status = U_ZERO_ERROR;
  std::string collation_value =
      icu_locale.getUnicodeKeywordValue<std::string>(collation_key, status);

  std::string locale;
  if (U_SUCCESS(status)) {
    if (collation_value == "search") {
      usage = "search";

      // Search is disallowed as a collation value per spec. Let's
      // use `default`, instead.
      //
      // https://tc39.github.io/ecma402/#sec-properties-of-intl-collator-instances
      collation = "default";

      // We clone the icu::Locale because we don't want the
      // icu_collator to be affected when we remove the collation key
      // below.
      icu::Locale new_icu_locale = icu_locale;

      // The spec forbids the search as a collation value in the
      // locale tag, so let's filter it out.
      status = U_ZERO_ERROR;
      new_icu_locale.setUnicodeKeywordValue(collation_key, nullptr, status);
      DCHECK(U_SUCCESS(status));

      locale = Intl::ToLanguageTag(new_icu_locale).FromJust();
    } else {
      collation = collation_value.c_str();
      locale = Intl::ToLanguageTag(icu_locale).FromJust();
    }
  } else {
    locale = Intl::ToLanguageTag(icu_locale).FromJust();
  }

  // 5. For each row of Table 2, except the header row, in table order, do
  //    ...
  // Table 2: Resolved Options of Collator Instances
  //  Internal Slot            Property               Extension Key
  //    [[Locale]                "locale"
  //    [[Usage]                 "usage"
  //    [[Sensitivity]]          "sensitivity"
  //    [[IgnorePunctuation]]    "ignorePunctuation"
  //    [[Collation]]            "collation"
  //    [[Numeric]]              "numeric"              kn
  //    [[CaseFirst]]            "caseFirst"            kf

  // If the collator return the locale differ from what got requested, we stored
  // it in the collator->locale. Otherwise, we just use the one from the
  // collator.
  if (collator->locale()->length() != 0) {
    // Get the locale from collator->locale() since we know in some cases
    // collator won't be able to return the requested one, such as zh_CN.
    Handle<String> locale_from_collator(collator->locale(), isolate);
    Maybe<bool> maybe = JSReceiver::CreateDataProperty(
        isolate, options, isolate->factory()->locale_string(),
        locale_from_collator, Just(kDontThrow));
    DCHECK(maybe.FromJust());
    USE(maybe);
  } else {
    // Just return from the collator for most of the cases that we can recover
    // from the collator.
    CreateDataPropertyForOptions(
        isolate, options, isolate->factory()->locale_string(), locale.c_str());
  }

  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->usage_string(), usage);
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->sensitivity_string(), sensitivity);
  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->ignorePunctuation_string(),
                               ignore_punctuation);
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->collation_string(), collation);
  CreateDataPropertyForOptions(isolate, options,
                               isolate->factory()->numeric_string(), numeric);
  CreateDataPropertyForOptions(
      isolate, options, isolate->factory()->caseFirst_string(), case_first);
  return options;
}

namespace {

CaseFirst ToCaseFirst(const char* str) {
  if (strcmp(str, "upper") == 0) return CaseFirst::kUpper;
  if (strcmp(str, "lower") == 0) return CaseFirst::kLower;
  if (strcmp(str, "false") == 0) return CaseFirst::kFalse;
  return CaseFirst::kUndefined;
}

UColAttributeValue ToUColAttributeValue(CaseFirst case_first) {
  switch (case_first) {
    case CaseFirst::kUpper:
      return UCOL_UPPER_FIRST;
    case CaseFirst::kLower:
      return UCOL_LOWER_FIRST;
    case CaseFirst::kFalse:
    case CaseFirst::kUndefined:
      return UCOL_OFF;
  }
}

void SetNumericOption(icu::Collator* icu_collator, bool numeric) {
  DCHECK_NOT_NULL(icu_collator);
  UErrorCode status = U_ZERO_ERROR;
  icu_collator->setAttribute(UCOL_NUMERIC_COLLATION,
                             numeric ? UCOL_ON : UCOL_OFF, status);
  DCHECK(U_SUCCESS(status));
}

void SetCaseFirstOption(icu::Collator* icu_collator, CaseFirst case_first) {
  DCHECK_NOT_NULL(icu_collator);
  UErrorCode status = U_ZERO_ERROR;
  icu_collator->setAttribute(UCOL_CASE_FIRST, ToUColAttributeValue(case_first),
                             status);
  DCHECK(U_SUCCESS(status));
}

}  // anonymous namespace

// static
MaybeHandle<JSCollator> JSCollator::New(Isolate* isolate, DirectHandle<Map> map,
                                        Handle<Object> locales,
                                        Handle<Object> options_obj,
                                        const char* service) {
  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, Handle<JSCollator>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  // 2. Set options to ? CoerceOptionsToObject(options).
  Handle<JSReceiver> options;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options, CoerceOptionsToObject(isolate, options_obj, service));

  // 4. Let usage be ? GetOption(options, "usage", "string", « "sort",
  // "search" », "sort").
  Maybe<Usage> maybe_usage = GetStringOption<Usage>(
      isolate, options, "usage", service, {"sort", "search"},
      {Usage::SORT, Usage::SEARCH}, Usage::SORT);
  MAYBE_RETURN(maybe_usage, MaybeHandle<JSCollator>());
  Usage usage = maybe_usage.FromJust();

  // 9. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 10. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSCollator>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // x. Let _collation_ be ? GetOption(_options_, *"collation"*, *"string"*,
  // *undefined*, *undefined*).
  std::unique_ptr<char[]> collation_str = nullptr;
  const std::vector<const char*> empty_values = {};
  Maybe<bool> maybe_collation = GetStringOption(
      isolate, options, "collation", empty_values, service, &collation_str);
  MAYBE_RETURN(maybe_collation, MaybeHandle<JSCollator>());
  // x. If _collation_ is not *undefined*, then
  if (maybe_collation.FromJust() && collation_str != nullptr) {
    // 1. If _collation_ does not match the Unicode Locale Identifier `type`
    // nonterminal, throw a *RangeError* exception.
    if (!JSLocale::Is38AlphaNumList(collation_str.get())) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kInvalid,
                        isolate->factory()->collation_string(),
                        isolate->factory()->NewStringFromAsciiChecked(
                            collation_str.get())),
          MaybeHandle<JSCollator>());
    }
  }
  // x. Set _opt_.[[co]] to _collation_.

  // 11. Let numeric be ? GetOption(options, "numeric", "boolean",
  // undefined, undefined).
  // 12. If numeric is not undefined, then
  //    a. Let numeric be ! ToString(numeric).
  //
  // Note: We omit the ToString(numeric) operation as it's not
  // observable. GetBoolOption returns a Boolean and
  // ToString(Boolean) is not side-effecting.
  //
  // 13. Set opt.[[kn]] to numeric.
  bool numeric;
  Maybe<bool> found_numeric =
      GetBoolOption(isolate, options, "numeric", service, &numeric);
  MAYBE_RETURN(found_numeric, MaybeHandle<JSCollator>());

  // 14. Let caseFirst be ? GetOption(options, "caseFirst", "string",
  //     « "upper", "lower", "false" », undefined).
  Maybe<CaseFirst> maybe_case_first = GetCaseFirst(isolate, options, service);
  MAYBE_RETURN(maybe_case_first, MaybeHandle<JSCollator>());
  CaseFirst case_first = maybe_case_first.FromJust();

  // The relevant unicode extensions accepted by Collator as specified here:
  // https://tc39.github.io/ecma402/#sec-intl-collator-internal-slots
  //
  // 16. Let relevantExtensionKeys be %Collator%.[[RelevantExtensionKeys]].
  std::set<std::string> relevant_extension_keys{"co", "kn", "kf"};

  // 17. Let r be ResolveLocale(%Collator%.[[AvailableLocales]],
  // requestedLocales, opt, %Collator%.[[RelevantExtensionKeys]],
  // localeData).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSCollator::GetAvailableLocales(),
                          requested_locales, matcher, relevant_extension_keys);
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  // 18. Set collator.[[Locale]] to r.[[locale]].
  icu::Locale icu_locale = r.icu_locale;
  DCHECK(!icu_locale.isBogus());

  // 19. Let collation be r.[[co]].
  UErrorCode status = U_ZERO_ERROR;
  if (collation_str != nullptr) {
    auto co_extension_it = r.extensions.find("co");
    if (co_extension_it != r.extensions.end() &&
        co_extension_it->second != collation_str.get()) {
      icu_locale.setUnicodeKeywordValue("co", nullptr, status);
      DCHECK(U_SUCCESS(status));
    }
  }

  // 5. Set collator.[[Usage]] to usage.
  //
  // 6. If usage is "sort", then
  //    a. Let localeData be %Collator%.[[SortLocaleData]].
  // 7. Else,
  //    a. Let localeData be %Collator%.[[SearchLocaleData]].
  //
  // The Intl spec doesn't allow us to use "search" as an extension
  // value for collation as per:
  // https://tc39.github.io/ecma402/#sec-intl-collator-internal-slots
  //
  // But the only way to pass the value "search" for collation from
  // the options object to ICU is to use the 'co' extension keyword.
  //
  // This will need to be filtered out when creating the
  // resolvedOptions object.
  if (usage == Usage::SEARCH) {
    UErrorCode set_status = U_ZERO_ERROR;
    icu_locale.setUnicodeKeywordValue("co", "search", set_status);
    DCHECK(U_SUCCESS(set_status));
  } else {
    if (collation_str != nullptr &&
        Intl::IsValidCollation(icu_locale, collation_str.get())) {
      icu_locale.setUnicodeKeywordValue("co", collation_str.get(), status);
      DCHECK(U_SUCCESS(status));
    }
  }

  // 20. If collation is null, let collation be "default".
  // 21. Set collator.[[Collation]] to collation.
  //
  // We don't store the collation value as per the above two steps
  // here. The collation value can be looked up from icu::Collator on
  // demand, as part of Intl.Collator.prototype.resolvedOptions.

  std::unique_ptr<icu::Collator> icu_collator(
      icu::Collator::createInstance(icu_locale, status));
  if (U_FAILURE(status) || icu_collator == nullptr) {
    status = U_ZERO_ERROR;
    // Remove extensions and try again.
    icu::Locale no_extension_locale(icu_locale.getBaseName());
    icu_collator.reset(
        icu::Collator::createInstance(no_extension_locale, status));

    if (U_FAILURE(status) || icu_collator == nullptr) {
      THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
    }
  }
  DCHECK(U_SUCCESS(status));

  icu::Locale collator_locale(
      icu_collator->getLocale(ULOC_VALID_LOCALE, status));

  // 22. If relevantExtensionKeys contains "kn", then
  //     a. Set collator.[[Numeric]] to ! SameValue(r.[[kn]], "true").
  //
  // If the numeric value is passed in through the options object,
  // then we use it. Otherwise, we check if the numeric value is
  // passed in through the unicode extensions.
  status = U_ZERO_ERROR;
  if (found_numeric.FromJust()) {
    SetNumericOption(icu_collator.get(), numeric);
  } else {
    auto kn_extension_it = r.extensions.find("kn");
    if (kn_extension_it != r.extensions.end()) {
      SetNumericOption(icu_collator.get(), (kn_extension_it->second == "true"));
    }
  }

  // 23. If relevantExtensionKeys contains "kf", then
  //     a. Set collator.[[CaseFirst]] to r.[[kf]].
  //
  // If the caseFirst value is passed in through the options object,
  // then we use it. Otherwise, we check if the caseFirst value is
  // passed in through the unicode extensions.
  if (case_first != CaseFirst::kUndefined) {
    SetCaseFirstOption(icu_collator.get(), case_first);
  } else {
    auto kf_extension_it = r.extensions.find("kf");
    if (kf_extension_it != r.extensions.end()) {
      SetCaseFirstOption(icu_collator.get(),
                         ToCaseFirst(kf_extension_it->second.c_str()));
    }
  }

  // Normalization is always on, by the spec. We are free to optimize
  // if the strings are already normalized (but we don't have a way to tell
  // that right now).
  status = U_ZERO_ERROR;
  icu_collator->setAttribute(UCOL_NORMALIZATION_MODE, UCOL_ON, status);
  DCHECK(U_SUCCESS(status));

  // 24. Let sensitivity be ? GetOption(options, "sensitivity",
  // "string", « "base", "accent", "case", "variant" », undefined).
  Maybe<Sensitivity> maybe_sensitivity =
      GetStringOption<Sensitivity>(isolate, options, "sensitivity", service,
                                   {"base", "accent", "case", "variant"},
                                   {Sensitivity::kBase, Sensitivity::kAccent,
                                    Sensitivity::kCase, Sensitivity::kVariant},
                                   Sensitivity::kUndefined);
  MAYBE_RETURN(maybe_sensitivity, MaybeHandle<JSCollator>());
  Sensitivity sensitivity = maybe_sensitivity.FromJust();

  // 25. If sensitivity is undefined, then
  if (sensitivity == Sensitivity::kUndefined) {
    // 25. a. If usage is "sort", then
    if (usage == Usage::SORT) {
      // 25. a. i. Let sensitivity be "variant".
      sensitivity = Sensitivity::kVariant;
    }
  }
  // 26. Set collator.[[Sensitivity]] to sensitivity.
  switch (sensitivity) {
    case Sensitivity::kBase:
      icu_collator->setStrength(icu::Collator::PRIMARY);
      break;
    case Sensitivity::kAccent:
      icu_collator->setStrength(icu::Collator::SECONDARY);
      break;
    case Sensitivity::kCase:
      icu_collator->setStrength(icu::Collator::PRIMARY);
      status = U_ZERO_ERROR;
      icu_collator->setAttribute(UCOL_CASE_LEVEL, UCOL_ON, status);
      DCHECK(U_SUCCESS(status));
      break;
    case Sensitivity::kVariant:
      icu_collator->setStrength(icu::Collator::TERTIARY);
      break;
    case Sensitivity::kUndefined:
      break;
  }

  // 27.Let ignorePunctuation be ? GetOption(options,
  // "ignorePunctuation", "boolean", undefined, false).
  bool ignore_punctuation = false;
  Maybe<bool> found_ignore_punctuation = GetBoolOption(
      isolate, options, "ignorePunctuation", service, &ignore_punctuation);
  MAYBE_RETURN(found_ignore_punctuation, MaybeHandle<JSCollator>());

  // 28. Set collator.[[IgnorePunctuation]] to ignorePunctuation.

  // Note: The following implementation does not strictly follow the spec text
  // due to https://github.com/tc39/ecma402/issues/832
  // If the ignorePunctuation is not defined, instead of fall back
  // to default false, we just depend on ICU to default based on the
  // built in locale collation rule, which in "th" locale that is true
  // but false on other locales.
  if (found_ignore_punctuation.FromJust()) {
    status = U_ZERO_ERROR;
    icu_collator->setAttribute(
        UCOL_ALTERNATE_HANDLING,
        ignore_punctuation ? UCOL_SHIFTED : UCOL_NON_IGNORABLE, status);
    DCHECK(U_SUCCESS(status));
  }

  DirectHandle<Managed<icu::Collator>> managed_collator =
      Managed<icu::Collator>::From(isolate, 0, std::move(icu_collator));

  // We only need to do so if it is different from the collator would return.
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(
          (collator_locale != icu_locale) ? r.locale.c_str() : "");
  // Now all properties are ready, so we can allocate the result object.
  Handle<JSCollator> collator =
      Cast<JSCollator>(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  collator->set_icu_collator(*managed_collator);
  collator->set_locale(*locale_str);

  // 29. Return collator.
  return collator;
}

namespace {

class CollatorAvailableLocales {
 public:
  CollatorAvailableLocales() {
    int32_t num_locales = 0;
    const icu::Locale* icu_available_locales =
        icu::Collator::getAvailableLocales(num_locales);
    std::vector<std::string> locales;
    locales.reserve(num_locales);
    for (int32_t i = 0; i < num_locales; ++i) {
      locales.push_back(
          Intl::ToLanguageTag(icu_available_locales[i]).FromJust());
    }
#define U_ICUDATA_COLL U_ICUDATA_NAME U_TREE_SEPARATOR_STRING "coll"
    set_ = Intl::BuildLocaleSet(locales, U_ICUDATA_COLL, nullptr);
#undef U_ICUDATA_COLL
  }
  virtual ~CollatorAvailableLocales() = default;
  const std::set<std::string>& Get() const { return set_; }

 private:
  std::set<std::string> set_;
};

}  // namespace

const std::set<std::string>& JSCollator::GetAvailableLocales() {
  static base::LazyInstance<CollatorAvailableLocales>::type available_locales =
      LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

}  // namespace internal
}  // namespace v8
