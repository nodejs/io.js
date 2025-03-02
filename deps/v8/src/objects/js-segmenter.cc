// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-segmenter.h"

#include <map>
#include <memory>
#include <string>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/objects/intl-objects.h"
#include "src/objects/js-segmenter-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/brkiter.h"

namespace v8 {
namespace internal {

MaybeDirectHandle<JSSegmenter> JSSegmenter::New(
    Isolate* isolate, DirectHandle<Map> map, DirectHandle<Object> locales,
    DirectHandle<Object> input_options) {
  // 4. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, DirectHandle<JSSegmenter>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  DirectHandle<JSReceiver> options;
  const char* service = "Intl.Segmenter";
  // 5. Let options be GetOptionsObject(_options_).
  ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                             GetOptionsObject(isolate, input_options, service));

  // 7. Let opt be a new Record.
  // 8. Let matcher be ? GetOption(options, "localeMatcher", "string",
  // « "lookup", "best fit" », "best fit").
  // 9. Set opt.[[localeMatcher]] to matcher.
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeDirectHandle<JSSegmenter>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // 10. Let localeData be %Segmenter%.[[LocaleData]].

  // 11. Let r be ResolveLocale(%Segmenter%.[[AvailableLocales]],
  // requestedLocales, opt, %Segmenter%.[[RelevantExtensionKeys]]).
  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSSegmenter::GetAvailableLocales(),
                          requested_locales, matcher, {});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  // 12. Set segmenter.[[Locale]] to the value of r.[[locale]].
  DirectHandle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(r.locale.c_str());

  // 13. Let granularity be ? GetOption(options, "granularity", "string", «
  // "grapheme", "word", "sentence" », "grapheme").
  Maybe<Granularity> maybe_granularity = GetStringOption<Granularity>(
      isolate, options, "granularity", service,
      {"grapheme", "word", "sentence"},
      {Granularity::GRAPHEME, Granularity::WORD, Granularity::SENTENCE},
      Granularity::GRAPHEME);
  MAYBE_RETURN(maybe_granularity, MaybeDirectHandle<JSSegmenter>());
  Granularity granularity_enum = maybe_granularity.FromJust();

  icu::Locale icu_locale = r.icu_locale;
  DCHECK(!icu_locale.isBogus());

  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> icu_break_iterator;

  switch (granularity_enum) {
    case Granularity::GRAPHEME:
      icu_break_iterator.reset(
          icu::BreakIterator::createCharacterInstance(icu_locale, status));
      break;
    case Granularity::WORD:
      icu_break_iterator.reset(
          icu::BreakIterator::createWordInstance(icu_locale, status));
      break;
    case Granularity::SENTENCE:
      icu_break_iterator.reset(
          icu::BreakIterator::createSentenceInstance(icu_locale, status));
      break;
  }

  DCHECK(U_SUCCESS(status));
  DCHECK_NOT_NULL(icu_break_iterator.get());

  DirectHandle<Managed<icu::BreakIterator>> managed_break_iterator =
      Managed<icu::BreakIterator>::From(isolate, 0,
                                        std::move(icu_break_iterator));

  // Now all properties are ready, so we can allocate the result object.
  DirectHandle<JSSegmenter> segmenter =
      Cast<JSSegmenter>(isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  segmenter->set_flags(0);

  // 12. Set segmenter.[[Locale]] to the value of r.[[Locale]].
  segmenter->set_locale(*locale_str);

  // 14. Set segmenter.[[SegmenterGranularity]] to granularity.
  segmenter->set_granularity(granularity_enum);

  segmenter->set_icu_break_iterator(*managed_break_iterator);

  // 15. Return segmenter.
  return segmenter;
}

// ecma402 #sec-Intl.Segmenter.prototype.resolvedOptions
DirectHandle<JSObject> JSSegmenter::ResolvedOptions(
    Isolate* isolate, DirectHandle<JSSegmenter> segmenter) {
  Factory* factory = isolate->factory();
  // 3. Let options be ! ObjectCreate(%ObjectPrototype%).
  DirectHandle<JSObject> result =
      factory->NewJSObject(isolate->object_function());
  // 4. For each row of Table 1, except the header row, do
  // a. Let p be the Property value of the current row.
  // b. Let v be the value of pr's internal slot whose name is the Internal Slot
  //    value of the current row.
  //
  // c. If v is not undefined, then
  //  i. Perform ! CreateDataPropertyOrThrow(options, p, v).
  //    Table 1: Resolved Options of Segmenter Instances
  //     Internal Slot                 Property
  //     [[Locale]]                    "locale"
  //     [[SegmenterGranularity]]      "granularity"

  DirectHandle<String> locale(segmenter->locale(), isolate);
  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->granularity_string(),
                        segmenter->GranularityAsString(isolate), NONE);
  // 5. Return options.
  return result;
}

Handle<String> JSSegmenter::GranularityAsString(Isolate* isolate) const {
  return GetGranularityString(isolate, granularity());
}

Handle<String> JSSegmenter::GetGranularityString(Isolate* isolate,
                                                 Granularity granularity) {
  Factory* factory = isolate->factory();
  switch (granularity) {
    case Granularity::GRAPHEME:
      return factory->grapheme_string();
    case Granularity::WORD:
      return factory->word_string();
    case Granularity::SENTENCE:
      return factory->sentence_string();
  }
  UNREACHABLE();
}

const std::set<std::string>& JSSegmenter::GetAvailableLocales() {
  return Intl::GetAvailableLocales();
}

}  // namespace internal
}  // namespace v8
