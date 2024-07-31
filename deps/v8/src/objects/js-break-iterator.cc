// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#include "src/objects/js-break-iterator.h"

#include "src/objects/intl-objects.h"
#include "src/objects/js-break-iterator-inl.h"
#include "src/objects/managed-inl.h"
#include "src/objects/option-utils.h"
#include "unicode/brkiter.h"

namespace v8 {
namespace internal {

namespace {
enum class Type { CHARACTER, WORD, SENTENCE, LINE };
}  // anonymous namespace

MaybeHandle<JSV8BreakIterator> JSV8BreakIterator::New(
    Isolate* isolate, Handle<Map> map, Handle<Object> locales,
    Handle<Object> options_obj, const char* service) {
  Factory* factory = isolate->factory();

  // 1. Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_requested_locales =
      Intl::CanonicalizeLocaleList(isolate, locales);
  MAYBE_RETURN(maybe_requested_locales, MaybeHandle<JSV8BreakIterator>());
  std::vector<std::string> requested_locales =
      maybe_requested_locales.FromJust();

  Handle<JSReceiver> options;
  if (IsUndefined(*options_obj, isolate)) {
    options = factory->NewJSObjectWithNullProto();
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, options,
                               Object::ToObject(isolate, options_obj, service),
                               JSV8BreakIterator);
  }

  // Extract locale string
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options, service);
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSV8BreakIterator>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  Maybe<Intl::ResolvedLocale> maybe_resolve_locale =
      Intl::ResolveLocale(isolate, JSV8BreakIterator::GetAvailableLocales(),
                          requested_locales, matcher, {});
  if (maybe_resolve_locale.IsNothing()) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError),
                    JSV8BreakIterator);
  }
  Intl::ResolvedLocale r = maybe_resolve_locale.FromJust();

  // Extract type from options
  Maybe<Type> maybe_type = GetStringOption<Type>(
      isolate, options, "type", service,
      {"word", "character", "sentence", "line"},
      {Type::WORD, Type::CHARACTER, Type::SENTENCE, Type::LINE}, Type::WORD);
  MAYBE_RETURN(maybe_type, MaybeHandle<JSV8BreakIterator>());
  Type type_enum = maybe_type.FromJust();

  icu::Locale icu_locale = r.icu_locale;
  DCHECK(!icu_locale.isBogus());

  // Construct break_iterator using icu_locale and type
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::BreakIterator> break_iterator = nullptr;
  switch (type_enum) {
    case Type::CHARACTER:
      break_iterator.reset(
          icu::BreakIterator::createCharacterInstance(icu_locale, status));
      break;
    case Type::SENTENCE:
      break_iterator.reset(
          icu::BreakIterator::createSentenceInstance(icu_locale, status));
      break;
    case Type::LINE:
      isolate->CountUsage(
          v8::Isolate::UseCounterFeature::kBreakIteratorTypeLine);
      break_iterator.reset(
          icu::BreakIterator::createLineInstance(icu_locale, status));
      break;
    default:
      isolate->CountUsage(
          v8::Isolate::UseCounterFeature::kBreakIteratorTypeWord);
      break_iterator.reset(
          icu::BreakIterator::createWordInstance(icu_locale, status));
      break;
  }

  // Error handling for break_iterator
  if (U_FAILURE(status) || break_iterator == nullptr) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError),
                    JSV8BreakIterator);
  }
  isolate->CountUsage(v8::Isolate::UseCounterFeature::kBreakIterator);

  // Construct managed objects from pointers
  Handle<Managed<icu::BreakIterator>> managed_break_iterator =
      Managed<icu::BreakIterator>::FromUniquePtr(isolate, 0,
                                                 std::move(break_iterator));
  Handle<Managed<icu::UnicodeString>> managed_unicode_string =
      Managed<icu::UnicodeString>::FromRawPtr(isolate, 0, nullptr);

  Handle<String> locale_str =
      isolate->factory()->NewStringFromAsciiChecked(r.locale.c_str());

  // Now all properties are ready, so we can allocate the result object.
  Handle<JSV8BreakIterator> break_iterator_holder =
      Handle<JSV8BreakIterator>::cast(
          isolate->factory()->NewFastOrSlowJSObjectFromMap(map));
  DisallowGarbageCollection no_gc;
  break_iterator_holder->set_locale(*locale_str);
  break_iterator_holder->set_break_iterator(*managed_break_iterator);
  break_iterator_holder->set_unicode_string(*managed_unicode_string);

  // Return break_iterator_holder
  return break_iterator_holder;
}

namespace {

Type GetType(icu::BreakIterator* break_iterator) {
  // Since the developer calling the Intl.v8BreakIterator already know the type,
  // we usually do not need to know the type unless the resolvedOptions() is
  // called, we use the following trick to figure out the type instead of
  // storing it with the JSV8BreakIterator object to save memory.
  // This routine is not fast but should be seldomly used only.

  // We need to clone a copy of break iteator because we need to setText to it.
  std::unique_ptr<icu::BreakIterator> cloned_break_iterator(
      break_iterator->clone());
  // Use a magic string "He is." to call next().
  //  character type: will return 1 for "H"
  //  word type: will return 2 for "He"
  //  line type: will return 3 for "He "
  //  sentence type: will return 6 for "He is."
  icu::UnicodeString data("He is.");
  cloned_break_iterator->setText(data);
  switch (cloned_break_iterator->next()) {
    case 1:  // After "H"
      return Type::CHARACTER;
    case 2:  // After "He"
      return Type::WORD;
    case 3:  // After "He "
      return Type::LINE;
    case 6:  // After "He is."
      return Type::SENTENCE;
    default:
      UNREACHABLE();
  }
}

Handle<String> TypeAsString(Isolate* isolate, Type type) {
  switch (type) {
    case Type::CHARACTER:
      return ReadOnlyRoots(isolate).character_string_handle();
    case Type::WORD:
      return ReadOnlyRoots(isolate).word_string_handle();
    case Type::SENTENCE:
      return ReadOnlyRoots(isolate).sentence_string_handle();
    case Type::LINE:
      return ReadOnlyRoots(isolate).line_string_handle();
  }
  UNREACHABLE();
}

}  // anonymous namespace

Handle<JSObject> JSV8BreakIterator::ResolvedOptions(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator) {
  Factory* factory = isolate->factory();

  Type type = GetType(break_iterator->break_iterator()->raw());

  Handle<JSObject> result = factory->NewJSObject(isolate->object_function());
  Handle<String> locale(break_iterator->locale(), isolate);

  JSObject::AddProperty(isolate, result, factory->locale_string(), locale,
                        NONE);
  JSObject::AddProperty(isolate, result, factory->type_string(),
                        TypeAsString(isolate, type), NONE);
  return result;
}

void JSV8BreakIterator::AdoptText(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator_holder,
    Handle<String> text) {
  icu::BreakIterator* break_iterator =
      break_iterator_holder->break_iterator()->raw();
  DCHECK_NOT_NULL(break_iterator);
  Handle<Managed<icu::UnicodeString>> unicode_string =
      Intl::SetTextToBreakIterator(isolate, text, break_iterator);
  break_iterator_holder->set_unicode_string(*unicode_string);
}

Handle<Object> JSV8BreakIterator::Current(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator) {
  return isolate->factory()->NewNumberFromInt(
      break_iterator->break_iterator()->raw()->current());
}

Handle<Object> JSV8BreakIterator::First(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator) {
  return isolate->factory()->NewNumberFromInt(
      break_iterator->break_iterator()->raw()->first());
}

Handle<Object> JSV8BreakIterator::Next(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator) {
  return isolate->factory()->NewNumberFromInt(
      break_iterator->break_iterator()->raw()->next());
}

Tagged<String> JSV8BreakIterator::BreakType(
    Isolate* isolate, Handle<JSV8BreakIterator> break_iterator) {
  int32_t status = break_iterator->break_iterator()->raw()->getRuleStatus();
  // Keep return values in sync with JavaScript BreakType enum.
  if (status >= UBRK_WORD_NONE && status < UBRK_WORD_NONE_LIMIT) {
    return ReadOnlyRoots(isolate).none_string();
  }
  if (status >= UBRK_WORD_NUMBER && status < UBRK_WORD_NUMBER_LIMIT) {
    return ReadOnlyRoots(isolate).number_string();
  }
  if (status >= UBRK_WORD_LETTER && status < UBRK_WORD_LETTER_LIMIT) {
    return ReadOnlyRoots(isolate).letter_string();
  }
  if (status >= UBRK_WORD_KANA && status < UBRK_WORD_KANA_LIMIT) {
    return ReadOnlyRoots(isolate).kana_string();
  }
  if (status >= UBRK_WORD_IDEO && status < UBRK_WORD_IDEO_LIMIT) {
    return ReadOnlyRoots(isolate).ideo_string();
  }
  return ReadOnlyRoots(isolate).unknown_string();
}

const std::set<std::string>& JSV8BreakIterator::GetAvailableLocales() {
  return Intl::GetAvailableLocales();
}

}  // namespace internal
}  // namespace v8
