// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/intl-objects.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "src/api/api-inl.h"
#include "src/base/logging.h"
#include "src/base/strings.h"
#include "src/common/globals.h"
#include "src/date/date.h"
#include "src/execution/isolate.h"
#include "src/execution/local-isolate.h"
#include "src/handles/global-handles.h"
#include "src/heap/factory.h"
#include "src/objects/js-collator-inl.h"
#include "src/objects/js-date-time-format-inl.h"
#include "src/objects/js-locale-inl.h"
#include "src/objects/js-locale.h"
#include "src/objects/js-number-format-inl.h"
#include "src/objects/js-temporal-objects.h"
#include "src/objects/managed-inl.h"
#include "src/objects/objects-inl.h"
#include "src/objects/option-utils.h"
#include "src/objects/property-descriptor.h"
#include "src/objects/smi.h"
#include "src/objects/string.h"
#include "src/strings/string-case.h"
#include "unicode/basictz.h"
#include "unicode/brkiter.h"
#include "unicode/calendar.h"
#include "unicode/coll.h"
#include "unicode/datefmt.h"
#include "unicode/decimfmt.h"
#include "unicode/formattedvalue.h"
#include "unicode/localebuilder.h"
#include "unicode/localematcher.h"
#include "unicode/locid.h"
#include "unicode/normalizer2.h"
#include "unicode/numberformatter.h"
#include "unicode/numfmt.h"
#include "unicode/numsys.h"
#include "unicode/timezone.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "unicode/uvernum.h"  // U_ICU_VERSION_MAJOR_NUM

#ifndef V8_INTL_SUPPORT
#error Internationalization is expected to be enabled.
#endif  // V8_INTL_SUPPORT

#define XSTR(s) STR(s)
#define STR(s) #s
static_assert(
    V8_MINIMUM_ICU_VERSION <= U_ICU_VERSION_MAJOR_NUM,
    "v8 is required to build with ICU " XSTR(V8_MINIMUM_ICU_VERSION) " and up");
#undef STR
#undef XSTR

namespace v8::internal {

namespace {

inline constexpr uint8_t AsOneByte(uint16_t ch) {
  DCHECK_LE(ch, kMaxUInt8);
  return static_cast<uint8_t>(ch);
}

constexpr uint8_t kToLower[256] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B,
    0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B,
    0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
    0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3,
    0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
    0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB,
    0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xD7,
    0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3,
    0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB,
    0xFC, 0xFD, 0xFE, 0xFF,
};

inline constexpr uint8_t ToLatin1Lower(uint8_t ch) {
  static_assert(std::numeric_limits<decltype(ch)>::max() < arraysize(kToLower));
  return kToLower[ch];
}
// Ensure callers explicitly truncate uint16_t.
inline constexpr uint8_t ToLatin1Lower(uint16_t ch) = delete;

// Does not work for U+00DF (sharp-s), U+00B5 (micron), U+00FF, or two-byte
// values.
inline constexpr uint8_t ToLatin1Upper(uint8_t ch) {
  DCHECK(ch != 0xDF && ch != 0xB5 && ch != 0xFF);
  return ch &
         ~((IsAsciiLower(ch) || (((ch & 0xE0) == 0xE0) && ch != 0xF7)) << 5);
}
// Ensure callers explicitly truncate uint16_t.
inline constexpr uint8_t ToLatin1Upper(uint16_t ch) = delete;

bool ToUpperFastASCII(base::Vector<const uint16_t> src,
                      DirectHandle<SeqOneByteString> result) {
  // Do a faster loop for the case where all the characters are ASCII.
  uint16_t ored = 0;
  int32_t index = 0;
  for (const uint16_t* it = src.begin(); it != src.end(); ++it) {
    uint16_t ch = *it;
    ored |= ch;
    result->SeqOneByteStringSet(index++, ToAsciiUpper(ch));
  }
  return !(ored & ~0x7F);
}

const uint16_t sharp_s = 0xDF;

template <typename Char>
bool ToUpperOneByte(base::Vector<const Char> src, uint8_t* dest,
                    int* sharp_s_count) {
  // Still pretty-fast path for the input with non-ASCII Latin-1 characters.

  // There are two special cases.
  //  1. U+00B5 and U+00FF are mapped to a character beyond U+00FF.
  //  2. Lower case sharp-S converts to "SS" (two characters)
  *sharp_s_count = 0;
  for (auto it = src.begin(); it != src.end(); ++it) {
    uint8_t ch = AsOneByte(*it);
    if (V8_UNLIKELY(ch == sharp_s)) {
      ++(*sharp_s_count);
      continue;
    }
    if (V8_UNLIKELY(ch == 0xB5 || ch == 0xFF)) {
      // Since this upper-cased character does not fit in an 8-bit string, we
      // need to take the 16-bit path.
      return false;
    }
    *dest++ = ToLatin1Upper(ch);
  }

  return true;
}

template <typename Char>
void ToUpperWithSharpS(base::Vector<const Char> src,
                       DirectHandle<SeqOneByteString> result) {
  int32_t dest_index = 0;
  for (auto it = src.begin(); it != src.end(); ++it) {
    uint8_t ch = AsOneByte(*it);
    if (ch == sharp_s) {
      result->SeqOneByteStringSet(dest_index++, 'S');
      result->SeqOneByteStringSet(dest_index++, 'S');
    } else {
      result->SeqOneByteStringSet(dest_index++, ToLatin1Upper(ch));
    }
  }
}

inline int FindFirstUpperOrNonAscii(Tagged<String> s, int length) {
  for (int index = 0; index < length; ++index) {
    uint16_t ch = s->Get(index);
    if (V8_UNLIKELY(IsAsciiUpper(ch) || ch & ~0x7F)) {
      return index;
    }
  }
  return length;
}

const UChar* GetUCharBufferFromFlat(const String::FlatContent& flat,
                                    std::unique_ptr<base::uc16[]>* dest,
                                    int32_t length) {
  DCHECK(flat.IsFlat());
  if (flat.IsOneByte()) {
    if (!*dest) {
      dest->reset(NewArray<base::uc16>(length));
      CopyChars(dest->get(), flat.ToOneByteVector().begin(), length);
    }
    return reinterpret_cast<const UChar*>(dest->get());
  } else {
    return reinterpret_cast<const UChar*>(flat.ToUC16Vector().begin());
  }
}

template <typename T>
MaybeHandle<T> New(Isolate* isolate, Handle<JSFunction> constructor,
                   Handle<Object> locales, Handle<Object> options,
                   const char* method_name) {
  Handle<Map> map;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, map,
      JSFunction::GetDerivedMap(isolate, constructor, constructor));
  return T::New(isolate, map, locales, options, method_name);
}
}  // namespace

const uint8_t* Intl::ToLatin1LowerTable() { return &kToLower[0]; }

icu::UnicodeString Intl::ToICUUnicodeString(Isolate* isolate,
                                            DirectHandle<String> string,
                                            int offset) {
  DCHECK(string->IsFlat());
  DisallowGarbageCollection no_gc;
  std::unique_ptr<base::uc16[]> sap;
  // Short one-byte strings can be expanded on the stack to avoid allocating a
  // temporary buffer.
  constexpr unsigned int kShortStringSize = 80;
  UChar short_string_buffer[kShortStringSize];
  const UChar* uchar_buffer = nullptr;
  const String::FlatContent& flat = string->GetFlatContent(no_gc);
  // We read the length from the heap, so it may be untrusted (in the sandbox
  // attacker model) and we therefore need to use an unsigned int here when
  // comparing it against the kShortStringSize.
  // TODO(saelo): consider using uint32_t for the size in String objects.
  uint32_t length = string->length();
  DCHECK_LE(offset, length);
  if (flat.IsOneByte() && length <= kShortStringSize) {
    CopyChars(short_string_buffer, flat.ToOneByteVector().begin(), length);
    uchar_buffer = short_string_buffer;
  } else {
    uchar_buffer = GetUCharBufferFromFlat(flat, &sap, length);
  }
  return icu::UnicodeString(uchar_buffer + offset, length - offset);
}

namespace {

icu::StringPiece ToICUStringPiece(Isolate* isolate, DirectHandle<String> string,
                                  int offset = 0) {
  DCHECK(string->IsFlat());
  DisallowGarbageCollection no_gc;

  const String::FlatContent& flat = string->GetFlatContent(no_gc);
  if (!flat.IsOneByte()) return icu::StringPiece();

  int32_t length = string->length();
  const char* char_buffer =
      reinterpret_cast<const char*>(flat.ToOneByteVector().begin());
  if (!String::IsAscii(char_buffer, length)) {
    return icu::StringPiece();
  }

  return icu::StringPiece(char_buffer + offset, length - offset);
}

MaybeHandle<String> LocaleConvertCase(Isolate* isolate, DirectHandle<String> s,
                                      bool is_to_upper, const char* lang) {
  auto case_converter = is_to_upper ? u_strToUpper : u_strToLower;
  int32_t src_length = s->length();
  int32_t dest_length = src_length;
  UErrorCode status;
  Handle<SeqTwoByteString> result;
  std::unique_ptr<base::uc16[]> sap;

  if (dest_length == 0) return ReadOnlyRoots(isolate).empty_string_handle();

  // This is not a real loop. It'll be executed only once (no overflow) or
  // twice (overflow).
  for (int i = 0; i < 2; ++i) {
    // Case conversion can increase the string length (e.g. sharp-S => SS) so
    // that we have to handle RangeError exceptions here.
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result, isolate->factory()->NewRawTwoByteString(dest_length));
    DisallowGarbageCollection no_gc;
    DCHECK(s->IsFlat());
    String::FlatContent flat = s->GetFlatContent(no_gc);
    const UChar* src = GetUCharBufferFromFlat(flat, &sap, src_length);
    status = U_ZERO_ERROR;
    dest_length =
        case_converter(reinterpret_cast<UChar*>(result->GetChars(no_gc)),
                       dest_length, src, src_length, lang, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR) break;
  }

  // In most cases, the output will fill the destination buffer completely
  // leading to an unterminated string (U_STRING_NOT_TERMINATED_WARNING).
  // Only in rare cases, it'll be shorter than the destination buffer and
  // |result| has to be truncated.
  DCHECK(U_SUCCESS(status));
  if (V8_LIKELY(status == U_STRING_NOT_TERMINATED_WARNING)) {
    DCHECK(dest_length == result->length());
    return result;
  }
  DCHECK(dest_length < result->length());
  return SeqString::Truncate(isolate, result, dest_length);
}

}  // namespace

// A stripped-down version of ConvertToLower that can only handle flat one-byte
// strings and does not allocate. Note that {src} could still be, e.g., a
// one-byte sliced string with a two-byte parent string.
// Called from TF builtins.
Tagged<String> Intl::ConvertOneByteToLower(Tagged<String> src,
                                           Tagged<String> dst) {
  DCHECK_EQ(src->length(), dst->length());
  DCHECK(src->IsOneByteRepresentation());
  DCHECK(src->IsFlat());
  DCHECK(IsSeqOneByteString(dst));

  DisallowGarbageCollection no_gc;

  const int length = src->length();
  String::FlatContent src_flat = src->GetFlatContent(no_gc);
  uint8_t* dst_data = Cast<SeqOneByteString>(dst)->GetChars(no_gc);

  if (src_flat.IsOneByte()) {
    const uint8_t* src_data = src_flat.ToOneByteVector().begin();

    bool has_changed_character = false;
    int index_to_first_unprocessed =
        FastAsciiConvert<true>(reinterpret_cast<char*>(dst_data),
                               reinterpret_cast<const char*>(src_data), length,
                               &has_changed_character);

    if (index_to_first_unprocessed == length) {
      return has_changed_character ? dst : src;
    }

    // If not ASCII, we keep the result up to index_to_first_unprocessed and
    // process the rest.
    for (int index = index_to_first_unprocessed; index < length; ++index) {
      dst_data[index] = ToLatin1Lower(src_data[index]);
    }
  } else {
    DCHECK(src_flat.IsTwoByte());
    int index_to_first_unprocessed = FindFirstUpperOrNonAscii(src, length);
    if (index_to_first_unprocessed == length) return src;

    const uint16_t* src_data = src_flat.ToUC16Vector().begin();
    CopyChars(dst_data, src_data, index_to_first_unprocessed);
    for (int index = index_to_first_unprocessed; index < length; ++index) {
      // Truncating cast of two-byte src character to one-byte value. For valid
      // cases (where a one-byte sliced string points to a two-byte parent) this
      // will not lose any information, but we need to truncate anyway to
      // avoid undefined behavior if the parent string is corrupted.
      dst_data[index] = ToLatin1Lower(AsOneByte(src_data[index]));
    }
  }

  return dst;
}

MaybeHandle<String> Intl::ConvertToLower(Isolate* isolate, Handle<String> s) {
  if (!s->IsOneByteRepresentation()) {
    // Use a slower implementation for strings with characters beyond U+00FF.
    return LocaleConvertCase(isolate, s, false, "");
  }

  int length = s->length();

  // We depend here on the invariant that the length of a Latin1
  // string is invariant under ToLowerCase, and the result always
  // fits in the Latin1 range in the *root locale*. It does not hold
  // for ToUpperCase even in the root locale.

  // Scan the string for uppercase and non-ASCII characters for strings
  // shorter than a machine-word without any memory allocation overhead.
  // TODO(jshin): Apply this to a longer input by breaking FastAsciiConvert()
  // to two parts, one for scanning the prefix with no change and the other for
  // handling ASCII-only characters.

  bool is_short = length < static_cast<int>(sizeof(uintptr_t));
  if (is_short) {
    bool is_lower_ascii = FindFirstUpperOrNonAscii(*s, length) == length;
    if (is_lower_ascii) return s;
  }

  DirectHandle<SeqOneByteString> result =
      isolate->factory()->NewRawOneByteString(length).ToHandleChecked();

  return Handle<String>(Intl::ConvertOneByteToLower(*s, *result), isolate);
}

MaybeHandle<String> Intl::ConvertToUpper(Isolate* isolate, Handle<String> s) {
  int32_t length = s->length();
  if (s->IsOneByteRepresentation() && length > 0) {
    Handle<SeqOneByteString> result =
        isolate->factory()->NewRawOneByteString(length).ToHandleChecked();

    DCHECK(s->IsFlat());
    int sharp_s_count;
    bool is_result_single_byte;
    {
      DisallowGarbageCollection no_gc;
      String::FlatContent flat = s->GetFlatContent(no_gc);
      uint8_t* dest = result->GetChars(no_gc);
      if (flat.IsOneByte()) {
        base::Vector<const uint8_t> src = flat.ToOneByteVector();
        bool has_changed_character = false;
        int index_to_first_unprocessed = FastAsciiConvert<false>(
            reinterpret_cast<char*>(result->GetChars(no_gc)),
            reinterpret_cast<const char*>(src.begin()), length,
            &has_changed_character);
        if (index_to_first_unprocessed == length) {
          return has_changed_character ? result : s;
        }
        // If not ASCII, we keep the result up to index_to_first_unprocessed and
        // process the rest.
        is_result_single_byte =
            ToUpperOneByte(src.SubVector(index_to_first_unprocessed, length),
                           dest + index_to_first_unprocessed, &sharp_s_count);
      } else {
        DCHECK(flat.IsTwoByte());
        base::Vector<const uint16_t> src = flat.ToUC16Vector();
        if (ToUpperFastASCII(src, result)) return result;
        is_result_single_byte = ToUpperOneByte(src, dest, &sharp_s_count);
      }
    }

    // Go to the full Unicode path if there are characters whose uppercase
    // is beyond the Latin-1 range (cannot be represented in OneByteString).
    if (V8_UNLIKELY(!is_result_single_byte)) {
      return LocaleConvertCase(isolate, s, true, "");
    }

    if (sharp_s_count == 0) return result;

    // We have sharp_s_count sharp-s characters, but the result is still
    // in the Latin-1 range.
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        isolate->factory()->NewRawOneByteString(length + sharp_s_count));
    DisallowGarbageCollection no_gc;
    String::FlatContent flat = s->GetFlatContent(no_gc);
    if (flat.IsOneByte()) {
      ToUpperWithSharpS(flat.ToOneByteVector(), result);
    } else {
      ToUpperWithSharpS(flat.ToUC16Vector(), result);
    }

    return result;
  }

  return LocaleConvertCase(isolate, s, true, "");
}

std::string Intl::GetNumberingSystem(const icu::Locale& icu_locale) {
  // Ugly hack. ICU doesn't expose numbering system in any way, so we have
  // to assume that for given locale NumberingSystem constructor produces the
  // same digits as NumberFormat/Calendar would.
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::NumberingSystem> numbering_system(
      icu::NumberingSystem::createInstance(icu_locale, status));
  if (U_SUCCESS(status) && !numbering_system->isAlgorithmic()) {
    return numbering_system->getName();
  }
  return "latn";
}

namespace {

Maybe<icu::Locale> CreateICULocale(const std::string& bcp47_locale) {
  DisallowGarbageCollection no_gc;

  // Convert BCP47 into ICU locale format.
  UErrorCode status = U_ZERO_ERROR;

  icu::Locale icu_locale = icu::Locale::forLanguageTag(bcp47_locale, status);
  if (U_FAILURE(status) || icu_locale.isBogus()) {
    return Nothing<icu::Locale>();
  }

  return Just(icu_locale);
}

}  // anonymous namespace

// static

MaybeHandle<String> Intl::ToString(Isolate* isolate,
                                   const icu::UnicodeString& string) {
  return isolate->factory()->NewStringFromTwoByte(base::Vector<const uint16_t>(
      reinterpret_cast<const uint16_t*>(string.getBuffer()), string.length()));
}

MaybeHandle<String> Intl::ToString(Isolate* isolate,
                                   const icu::UnicodeString& string,
                                   int32_t begin, int32_t end) {
  return Intl::ToString(isolate, string.tempSubStringBetween(begin, end));
}

namespace {

Handle<JSObject> InnerAddElement(Isolate* isolate, Handle<JSArray> array,
                                 int index,
                                 DirectHandle<String> field_type_string,
                                 DirectHandle<String> value) {
  // let element = $array[$index] = {
  //   type: $field_type_string,
  //   value: $value
  // }
  // return element;
  Factory* factory = isolate->factory();
  Handle<JSObject> element = factory->NewJSObject(isolate->object_function());
  JSObject::AddProperty(isolate, element, factory->type_string(),
                        field_type_string, NONE);

  JSObject::AddProperty(isolate, element, factory->value_string(), value, NONE);
  // TODO(victorgomes): Temporarily forcing a fatal error here in case of
  // overflow, until Intl::AddElement can handle exceptions.
  if (JSObject::AddDataElement(array, index, element, NONE).IsNothing()) {
    FATAL("Fatal JavaScript invalid size error when adding element");
    UNREACHABLE();
  }
  return element;
}

}  // namespace

void Intl::AddElement(Isolate* isolate, Handle<JSArray> array, int index,
                      DirectHandle<String> field_type_string,
                      DirectHandle<String> value) {
  // Same as $array[$index] = {type: $field_type_string, value: $value};
  InnerAddElement(isolate, array, index, field_type_string, value);
}

void Intl::AddElement(Isolate* isolate, Handle<JSArray> array, int index,
                      DirectHandle<String> field_type_string,
                      DirectHandle<String> value,
                      Handle<String> additional_property_name,
                      DirectHandle<String> additional_property_value) {
  // Same as $array[$index] = {
  //   type: $field_type_string, value: $value,
  //   $additional_property_name: $additional_property_value
  // }
  Handle<JSObject> element =
      InnerAddElement(isolate, array, index, field_type_string, value);
  JSObject::AddProperty(isolate, element, additional_property_name,
                        additional_property_value, NONE);
}

namespace {

// Build the shortened locale; eg, convert xx_Yyyy_ZZ  to xx_ZZ.
//
// If locale has a script tag then return true and the locale without the
// script else return false and an empty string.
bool RemoveLocaleScriptTag(const std::string& icu_locale,
                           std::string* locale_less_script) {
  icu::Locale new_locale = icu::Locale::createCanonical(icu_locale.c_str());
  const char* icu_script = new_locale.getScript();
  if (icu_script == nullptr || strlen(icu_script) == 0) {
    *locale_less_script = std::string();
    return false;
  }

  const char* icu_language = new_locale.getLanguage();
  const char* icu_country = new_locale.getCountry();
  icu::Locale short_locale = icu::Locale(icu_language, icu_country);
  *locale_less_script = short_locale.getName();
  return true;
}

bool ValidateResource(const icu::Locale locale, const char* path,
                      const char* key) {
  bool result = false;
  UErrorCode status = U_ZERO_ERROR;
  UResourceBundle* bundle = ures_open(path, locale.getName(), &status);
  if (bundle != nullptr && status == U_ZERO_ERROR) {
    if (key == nullptr) {
      result = true;
    } else {
      UResourceBundle* key_bundle =
          ures_getByKey(bundle, key, nullptr, &status);
      result = key_bundle != nullptr && (status == U_ZERO_ERROR);
      ures_close(key_bundle);
    }
  }
  ures_close(bundle);
  if (!result) {
    if ((locale.getCountry()[0] != '\0') && (locale.getScript()[0] != '\0')) {
      // Fallback to try without country.
      std::string without_country(locale.getLanguage());
      without_country = without_country.append("-").append(locale.getScript());
      return ValidateResource(without_country.c_str(), path, key);
    } else if ((locale.getCountry()[0] != '\0') ||
               (locale.getScript()[0] != '\0')) {
      // Fallback to try with only language.
      std::string language(locale.getLanguage());
      return ValidateResource(language.c_str(), path, key);
    }
  }
  return result;
}

}  // namespace

std::set<std::string> Intl::BuildLocaleSet(
    const std::vector<std::string>& icu_available_locales, const char* path,
    const char* validate_key) {
  std::set<std::string> locales;
  for (const std::string& locale : icu_available_locales) {
    if (path != nullptr || validate_key != nullptr) {
      if (!ValidateResource(icu::Locale(locale.c_str()), path, validate_key)) {
        // FIXME(chromium:1215606) Find a beter fix for nb->no fallback
        if (locale != "nb") {
          continue;
        }
        // Try no for nb
        if (!ValidateResource(icu::Locale("no"), path, validate_key)) {
          continue;
        }
      }
    }
    locales.insert(locale);
    std::string shortened_locale;
    if (RemoveLocaleScriptTag(locale, &shortened_locale)) {
      std::replace(shortened_locale.begin(), shortened_locale.end(), '_', '-');
      locales.insert(shortened_locale);
    }
  }
  return locales;
}

Maybe<std::string> Intl::ToLanguageTag(const icu::Locale& locale) {
  UErrorCode status = U_ZERO_ERROR;
  std::string res = locale.toLanguageTag<std::string>(status);
  if (U_FAILURE(status)) {
    return Nothing<std::string>();
  }
  DCHECK(U_SUCCESS(status));
  return Just(res);
}

// See ecma402/#legacy-constructor.
MaybeHandle<Object> Intl::LegacyUnwrapReceiver(Isolate* isolate,
                                               Handle<JSReceiver> receiver,
                                               Handle<JSFunction> constructor,
                                               bool has_initialized_slot) {
  Handle<Object> obj_ordinary_has_instance;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, obj_ordinary_has_instance,
      Object::OrdinaryHasInstance(isolate, constructor, receiver));
  bool ordinary_has_instance =
      Object::BooleanValue(*obj_ordinary_has_instance, isolate);

  // 2. If receiver does not have an [[Initialized...]] internal slot
  //    and ? OrdinaryHasInstance(constructor, receiver) is true, then
  if (!has_initialized_slot && ordinary_has_instance) {
    // 2. a. Let new_receiver be ? Get(receiver, %Intl%.[[FallbackSymbol]]).
    Handle<Object> new_receiver;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, new_receiver,
        JSReceiver::GetProperty(isolate, receiver,
                                isolate->factory()->intl_fallback_symbol()));
    return new_receiver;
  }

  return receiver;
}

namespace {

bool IsTwoLetterLanguage(const std::string& locale) {
  // Two letters, both in range 'a'-'z'...
  return locale.length() == 2 && IsAsciiLower(locale[0]) &&
         IsAsciiLower(locale[1]);
}

bool IsDeprecatedOrLegacyLanguage(const std::string& locale) {
  //  Check if locale is one of the deprecated language tags:
  return locale == "in" || locale == "iw" || locale == "ji" || locale == "jw" ||
         locale == "mo" ||
         //  Check if locale is one of the legacy language tags:
         locale == "sh" || locale == "tl" || locale == "no";
}

bool IsStructurallyValidLanguageTag(const std::string& tag) {
  return JSLocale::StartsWithUnicodeLanguageId(tag);
}

// Canonicalize the locale.
// https://tc39.github.io/ecma402/#sec-canonicalizelanguagetag,
// including type check and structural validity check.
Maybe<std::string> CanonicalizeLanguageTag(Isolate* isolate,
                                           const std::string& locale_in) {
  std::string locale = locale_in;

  if (locale.empty() ||
      !String::IsAscii(locale.data(), static_cast<int>(locale.length()))) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kInvalidLanguageTag,
            isolate->factory()->NewStringFromAsciiChecked(locale.c_str())),
        Nothing<std::string>());
  }

  // Optimize for the most common case: a 2-letter language code in the
  // canonical form/lowercase that is not one of the deprecated codes
  // (in, iw, ji, jw). Don't check for ~70 of 3-letter deprecated language
  // codes. Instead, let them be handled by ICU in the slow path. However,
  // fast-track 'fil' (3-letter canonical code).
  if ((IsTwoLetterLanguage(locale) && !IsDeprecatedOrLegacyLanguage(locale)) ||
      locale == "fil") {
    return Just(locale);
  }

  // Because per BCP 47 2.1.1 language tags are case-insensitive, lowercase
  // the input before any more check.
  std::transform(locale.begin(), locale.end(), locale.begin(), ToAsciiLower);

  // // ECMA 402 6.2.3
  // TODO(jshin): uloc_{for,to}TanguageTag can fail even for a structually valid
  // language tag if it's too long (much longer than 100 chars). Even if we
  // allocate a longer buffer, ICU will still fail if it's too long. Either
  // propose to Ecma 402 to put a limit on the locale length or change ICU to
  // handle long locale names better. See
  // https://unicode-org.atlassian.net/browse/ICU-13417
  UErrorCode error = U_ZERO_ERROR;
  // uloc_forLanguageTag checks the structrual validity. If the input BCP47
  // language tag is parsed all the way to the end, it indicates that the input
  // is structurally valid. Due to a couple of bugs, we can't use it
  // without Chromium patches or ICU 62 or earlier.
  icu::Locale icu_locale = icu::Locale::forLanguageTag(locale.c_str(), error);

  if (U_FAILURE(error) || icu_locale.isBogus()) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kInvalidLanguageTag,
            isolate->factory()->NewStringFromAsciiChecked(locale.c_str())),
        Nothing<std::string>());
  }

  // Use LocaleBuilder to validate locale.
  icu_locale = icu::LocaleBuilder().setLocale(icu_locale).build(error);
  icu_locale.canonicalize(error);
  if (U_FAILURE(error) || icu_locale.isBogus()) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kInvalidLanguageTag,
            isolate->factory()->NewStringFromAsciiChecked(locale.c_str())),
        Nothing<std::string>());
  }
  Maybe<std::string> maybe_to_language_tag = Intl::ToLanguageTag(icu_locale);
  if (maybe_to_language_tag.IsNothing()) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(
            MessageTemplate::kInvalidLanguageTag,
            isolate->factory()->NewStringFromAsciiChecked(locale.c_str())),
        Nothing<std::string>());
  }

  return maybe_to_language_tag;
}

Maybe<std::string> CanonicalizeLanguageTag(Isolate* isolate,
                                           Handle<Object> locale_in) {
  Handle<String> locale_str;
  // This does part of the validity checking spec'ed in CanonicalizeLocaleList:
  // 7c ii. If Type(kValue) is not String or Object, throw a TypeError
  // exception.
  // 7c iii. Let tag be ? ToString(kValue).
  // 7c iv. If IsStructurallyValidLanguageTag(tag) is false, throw a
  // RangeError exception.

  if (IsString(*locale_in)) {
    locale_str = Cast<String>(locale_in);
  } else if (IsJSReceiver(*locale_in)) {
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, locale_str,
                                     Object::ToString(isolate, locale_in),
                                     Nothing<std::string>());
  } else {
    THROW_NEW_ERROR_RETURN_VALUE(isolate,
                                 NewTypeError(MessageTemplate::kLanguageID),
                                 Nothing<std::string>());
  }
  std::string locale(locale_str->ToCString().get());

  if (!IsStructurallyValidLanguageTag(locale)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate, NewRangeError(MessageTemplate::kLocaleBadParameters),
        Nothing<std::string>());
  }
  return CanonicalizeLanguageTag(isolate, locale);
}

}  // anonymous namespace

Maybe<std::vector<std::string>> Intl::CanonicalizeLocaleList(
    Isolate* isolate, Handle<Object> locales, bool only_return_one_result) {
  // 1. If locales is undefined, then
  if (IsUndefined(*locales, isolate)) {
    // 1a. Return a new empty List.
    return Just(std::vector<std::string>());
  }
  // 2. Let seen be a new empty List.
  std::vector<std::string> seen;
  // 3. If Type(locales) is String or locales has an [[InitializedLocale]]
  // internal slot,  then
  if (IsJSLocale(*locales)) {
    // Since this value came from JSLocale, which is already went though the
    // CanonializeLanguageTag process once, therefore there are no need to
    // call CanonializeLanguageTag again.
    seen.push_back(JSLocale::ToString(Cast<JSLocale>(locales)));
    return Just(seen);
  }
  if (IsString(*locales)) {
    // 3a. Let O be CreateArrayFromList(« locales »).
    // Instead of creating a one-element array and then iterating over it,
    // we inline the body of the iteration:
    std::string canonicalized_tag;
    if (!CanonicalizeLanguageTag(isolate, locales).To(&canonicalized_tag)) {
      return Nothing<std::vector<std::string>>();
    }
    seen.push_back(canonicalized_tag);
    return Just(seen);
  }
  // 4. Else,
  // 4a. Let O be ? ToObject(locales).
  Handle<JSReceiver> o;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, o,
                                   Object::ToObject(isolate, locales),
                                   Nothing<std::vector<std::string>>());
  // 5. Let len be ? ToLength(? Get(O, "length")).
  Handle<Object> length_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, length_obj,
                                   Object::GetLengthFromArrayLike(isolate, o),
                                   Nothing<std::vector<std::string>>());
  // TODO(jkummerow): Spec violation: strictly speaking, we have to iterate
  // up to 2^53-1 if {length_obj} says so. Since cases above 2^32 probably
  // don't happen in practice (and would be very slow if they do), we'll keep
  // the code simple for now by using a saturating to-uint32 conversion.
  double raw_length = Object::NumberValue(*length_obj);
  uint32_t len =
      raw_length >= kMaxUInt32 ? kMaxUInt32 : static_cast<uint32_t>(raw_length);
  // 6. Let k be 0.
  // 7. Repeat, while k < len
  for (uint32_t k = 0; k < len; k++) {
    // 7a. Let Pk be ToString(k).
    // 7b. Let kPresent be ? HasProperty(O, Pk).
    LookupIterator it(isolate, o, k);
    Maybe<bool> maybe_found = JSReceiver::HasProperty(&it);
    MAYBE_RETURN(maybe_found, Nothing<std::vector<std::string>>());
    // 7c. If kPresent is true, then
    if (!maybe_found.FromJust()) continue;
    // 7c i. Let kValue be ? Get(O, Pk).
    Handle<Object> k_value;
    ASSIGN_RETURN_ON_EXCEPTION_VALUE(isolate, k_value, Object::GetProperty(&it),
                                     Nothing<std::vector<std::string>>());
    // 7c ii. If Type(kValue) is not String or Object, throw a TypeError
    // exception.
    // 7c iii. If Type(kValue) is Object and kValue has an [[InitializedLocale]]
    // internal slot, then
    std::string canonicalized_tag;
    if (IsJSLocale(*k_value)) {
      // 7c iii. 1. Let tag be kValue.[[Locale]].
      canonicalized_tag = JSLocale::ToString(Cast<JSLocale>(k_value));
      // 7c iv. Else,
    } else {
      // 7c iv 1. Let tag be ? ToString(kValue).
      // 7c v. If IsStructurallyValidLanguageTag(tag) is false, throw a
      // RangeError exception.
      // 7c vi. Let canonicalizedTag be CanonicalizeLanguageTag(tag).
      if (!CanonicalizeLanguageTag(isolate, k_value).To(&canonicalized_tag)) {
        return Nothing<std::vector<std::string>>();
      }
    }
    // 7c vi. If canonicalizedTag is not an element of seen, append
    // canonicalizedTag as the last element of seen.
    if (std::find(seen.begin(), seen.end(), canonicalized_tag) == seen.end()) {
      seen.push_back(canonicalized_tag);
    }
    // 7d. Increase k by 1. (See loop header.)
    // Optimization: some callers only need one result.
    if (only_return_one_result) return Just(seen);
  }
  // 8. Return seen.
  return Just(seen);
}

// ecma402 #sup-string.prototype.tolocalelowercase
// ecma402 #sup-string.prototype.tolocaleuppercase
MaybeHandle<String> Intl::StringLocaleConvertCase(Isolate* isolate,
                                                  Handle<String> s,
                                                  bool to_upper,
                                                  Handle<Object> locales) {
  std::vector<std::string> requested_locales;
  if (!CanonicalizeLocaleList(isolate, locales, true).To(&requested_locales)) {
    return MaybeHandle<String>();
  }
  std::string requested_locale = requested_locales.empty()
                                     ? isolate->DefaultLocale()
                                     : requested_locales[0];
  size_t dash = requested_locale.find('-');
  if (dash != std::string::npos) {
    requested_locale = requested_locale.substr(0, dash);
  }

  // Primary language tag can be up to 8 characters long in theory.
  // https://tools.ietf.org/html/bcp47#section-2.2.1
  DCHECK_LE(requested_locale.length(), 8);
  s = String::Flatten(isolate, s);

  // All the languages requiring special-handling have two-letter codes.
  // Note that we have to check for '!= 2' here because private-use language
  // tags (x-foo) or grandfathered irregular tags (e.g. i-enochian) would have
  // only 'x' or 'i' when they get here.
  if (V8_UNLIKELY(requested_locale.length() != 2)) {
    if (to_upper) {
      return ConvertToUpper(isolate, s);
    }
    return ConvertToLower(isolate, s);
  }
  // TODO(jshin): Consider adding a fast path for ASCII or Latin-1. The fastpath
  // in the root locale needs to be adjusted for az, lt and tr because even case
  // mapping of ASCII range characters are different in those locales.
  // Greek (el) does not require any adjustment.
  if (V8_UNLIKELY((requested_locale == "tr") || (requested_locale == "el") ||
                  (requested_locale == "lt") || (requested_locale == "az"))) {
    return LocaleConvertCase(isolate, s, to_upper, requested_locale.c_str());
  } else {
    if (to_upper) {
      return ConvertToUpper(isolate, s);
    }
    return ConvertToLower(isolate, s);
  }
}

// static
template <class IsolateT>
Intl::CompareStringsOptions Intl::CompareStringsOptionsFor(
    IsolateT* isolate, DirectHandle<Object> locales,
    DirectHandle<Object> options) {
  if (!IsUndefined(*options, isolate)) {
    return CompareStringsOptions::kNone;
  }

  // Lists all of the available locales that are statically known to fulfill
  // fast path conditions. See the StringLocaleCompareFastPath test as a
  // starting point to update this list.
  //
  // Locale entries are roughly sorted s.t. common locales come first.
  //
  // The actual conditions are verified in debug builds in
  // CollatorAllowsFastComparison.
  static const char* const kFastLocales[] = {
      "en-US", "en", "fr", "es",    "de",    "pt",    "it", "ca",
      "de-AT", "fi", "id", "id-ID", "ms",    "nl",    "pl", "ro",
      "sl",    "sv", "sw", "vi",    "en-DE", "en-GB",
  };

  if (IsUndefined(*locales, isolate)) {
    const std::string& default_locale = isolate->DefaultLocale();
    for (const char* fast_locale : kFastLocales) {
      if (strcmp(fast_locale, default_locale.c_str()) == 0) {
        return CompareStringsOptions::kTryFastPath;
      }
    }

    return CompareStringsOptions::kNone;
  }

  if (!IsString(*locales)) return CompareStringsOptions::kNone;

  auto locales_string = Cast<String>(locales);
  for (const char* fast_locale : kFastLocales) {
    if (locales_string->IsEqualTo(base::CStrVector(fast_locale), isolate)) {
      return CompareStringsOptions::kTryFastPath;
    }
  }

  return CompareStringsOptions::kNone;
}

// Instantiations.
template Intl::CompareStringsOptions Intl::CompareStringsOptionsFor(
    Isolate*, DirectHandle<Object>, DirectHandle<Object>);
template Intl::CompareStringsOptions Intl::CompareStringsOptionsFor(
    LocalIsolate*, DirectHandle<Object>, DirectHandle<Object>);

std::optional<int> Intl::StringLocaleCompare(
    Isolate* isolate, Handle<String> string1, Handle<String> string2,
    Handle<Object> locales, Handle<Object> options, const char* method_name) {
  // We only cache the instance when locales is a string/undefined and
  // options is undefined, as that is the only case when the specified
  // side-effects of examining those arguments are unobservable.
  const bool can_cache =
      (IsString(*locales) || IsUndefined(*locales, isolate)) &&
      IsUndefined(*options, isolate);
  // We may be able to take the fast path, depending on the `locales` and
  // `options` arguments.
  const CompareStringsOptions compare_strings_options =
      CompareStringsOptionsFor(isolate, locales, options);
  if (can_cache) {
    // Both locales and options are undefined, check the cache.
    icu::Collator* cached_icu_collator =
        static_cast<icu::Collator*>(isolate->get_cached_icu_object(
            Isolate::ICUObjectCacheType::kDefaultCollator, locales));
    // We may use the cached icu::Collator for a fast path.
    if (cached_icu_collator != nullptr) {
      return Intl::CompareStrings(isolate, *cached_icu_collator, string1,
                                  string2, compare_strings_options);
    }
  }

  Handle<JSFunction> constructor = Handle<JSFunction>(
      Cast<JSFunction>(
          isolate->context()->native_context()->intl_collator_function()),
      isolate);

  Handle<JSCollator> collator;
  MaybeHandle<JSCollator> maybe_collator =
      New<JSCollator>(isolate, constructor, locales, options, method_name);
  if (!maybe_collator.ToHandle(&collator)) return {};
  if (can_cache) {
    isolate->set_icu_object_in_cache(
        Isolate::ICUObjectCacheType::kDefaultCollator, locales,
        std::static_pointer_cast<icu::UMemory>(
            collator->icu_collator()->get()));
  }
  icu::Collator* icu_collator = collator->icu_collator()->raw();
  return Intl::CompareStrings(isolate, *icu_collator, string1, string2,
                              compare_strings_options);
}

namespace {

// Weights for the Unicode Collation Algorithm for charcodes [0x00,0x7F].
// https://unicode.org/reports/tr10/.
//
// Generated from:
//
// $ wget http://www.unicode.org/Public/UCA/latest/allkeys.txt
// $ cat ~/allkeys.txt | grep '^00[0-7].  ;' | sort | sed 's/[*.]/ /g' |\
//   sed 's/.*\[ \(.*\)\].*/\1/' | python ~/gen_weights.py
//
// Where gen_weights.py does an ordinal rank s.t. weights fit in a uint8_t:
//
//   import sys
//
//   def to_ordinal(ws):
//       weight_map = {}
//       weights_uniq_sorted = sorted(set(ws))
//       for i in range(0, len(weights_uniq_sorted)):
//           weight_map[weights_uniq_sorted[i]] = i
//       return [weight_map[x] for x in ws]
//
//   def print_weight_list(array_name, ws):
//       print("constexpr uint8_t %s[256] = {" % array_name, end = "")
//       i = 0
//       for w in ws:
//           if (i % 16) == 0:
//               print("\n  ", end = "")
//           print("%3d," % w, end = "")
//           i += 1
//       print("\n};\n")
//
//   if __name__ == "__main__":
//       l1s = []
//       l3s = []
//       for line in sys.stdin:
//           weights = line.split()
//           l1s.append(int(weights[0], 16))
//           l3s.append(int(weights[2], 16))
//       print_weight_list("kCollationWeightsL1", to_ordinal(l1s))
//       print_weight_list("kCollationWeightsL3", to_ordinal(l3s))

// clang-format off
constexpr uint8_t kCollationWeightsL1[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  3,  4,  5,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    6, 12, 16, 28, 38, 29, 27, 15, 17, 18, 24, 32,  9,  8, 14, 25,
   39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 11, 10, 33, 34, 35, 13,
   23, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
   64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 19, 26, 20, 31,  7,
   30, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
   64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 21, 36, 22, 37,  0,
};
constexpr uint8_t kCollationWeightsL3[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
    2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  0,
};
constexpr int kCollationWeightsLength = arraysize(kCollationWeightsL1);
static_assert(kCollationWeightsLength == arraysize(kCollationWeightsL3));
// clang-format on

// Normalize a comparison delta (usually `lhs - rhs`) to UCollationResult
// values.
constexpr UCollationResult ToUCollationResult(int delta) {
  return delta < 0 ? UCollationResult::UCOL_LESS
                   : (delta > 0 ? UCollationResult::UCOL_GREATER
                                : UCollationResult::UCOL_EQUAL);
}

struct FastCompareStringsData {
  UCollationResult l1_result = UCollationResult::UCOL_EQUAL;
  UCollationResult l3_result = UCollationResult::UCOL_EQUAL;
  int processed_until = 0;
  int first_diff_at = 0;  // The first relevant diff (L1 if exists, else L3).
  bool has_diff = false;

  std::optional<UCollationResult> FastCompareFailed(
      int* processed_until_out) const {
    if (has_diff) {
      // Found some difference, continue there to ensure the generic algorithm
      // picks it up.
      *processed_until_out = first_diff_at;
    } else {
      // No difference found, reprocess the last processed character since it
      // may be followed by a unicode combining character (which alters it's
      // meaning).
      *processed_until_out = std::max(processed_until - 1, 0);
    }
    return {};
  }
};

template <class CharT>
constexpr bool CanFastCompare(CharT c) {
  return c < kCollationWeightsLength && kCollationWeightsL1[c] != 0;
}

template <class Char1T, class Char2T>
bool FastCompareFlatString(const Char1T* lhs, const Char2T* rhs, int length,
                           FastCompareStringsData* d) {
  for (int i = 0; i < length; i++) {
    const Char1T l = lhs[i];
    const Char2T r = rhs[i];
    if (!CanFastCompare(l) || !CanFastCompare(r)) {
      d->processed_until = i;
      return false;
    }
    UCollationResult l1_result =
        ToUCollationResult(kCollationWeightsL1[l] - kCollationWeightsL1[r]);
    if (l1_result != UCollationResult::UCOL_EQUAL) {
      d->has_diff = true;
      d->first_diff_at = i;
      d->processed_until = i;
      d->l1_result = l1_result;
      return true;
    }
    if (l != r && d->l3_result == UCollationResult::UCOL_EQUAL) {
      // Collapse the two-pass algorithm into one: if we find a difference in
      // L1 weights, that is our result. If not, use the first L3 weight
      // difference.
      UCollationResult l3_result =
          ToUCollationResult(kCollationWeightsL3[l] - kCollationWeightsL3[r]);
      d->l3_result = l3_result;
      if (!d->has_diff) {
        d->has_diff = true;
        d->first_diff_at = i;
      }
    }
  }
  d->processed_until = length;
  return true;
}

bool FastCompareStringFlatContent(const String::FlatContent& lhs,
                                  const String::FlatContent& rhs, int length,
                                  FastCompareStringsData* d) {
  if (lhs.IsOneByte()) {
    base::Vector<const uint8_t> l = lhs.ToOneByteVector();
    if (rhs.IsOneByte()) {
      base::Vector<const uint8_t> r = rhs.ToOneByteVector();
      return FastCompareFlatString(l.data(), r.data(), length, d);
    } else {
      base::Vector<const uint16_t> r = rhs.ToUC16Vector();
      return FastCompareFlatString(l.data(), r.data(), length, d);
    }
  } else {
    base::Vector<const uint16_t> l = lhs.ToUC16Vector();
    if (rhs.IsOneByte()) {
      base::Vector<const uint8_t> r = rhs.ToOneByteVector();
      return FastCompareFlatString(l.data(), r.data(), length, d);
    } else {
      base::Vector<const uint16_t> r = rhs.ToUC16Vector();
      return FastCompareFlatString(l.data(), r.data(), length, d);
    }
  }
  UNREACHABLE();
}

bool CharIsAsciiOrOutOfBounds(const String::FlatContent& string,
                              int string_length, int index) {
  DCHECK_EQ(string.length(), string_length);
  return index >= string_length || isascii(string.Get(index));
}

bool CharCanFastCompareOrOutOfBounds(const String::FlatContent& string,
                                     int string_length, int index) {
  DCHECK_EQ(string.length(), string_length);
  return index >= string_length || CanFastCompare(string.Get(index));
}

#ifdef DEBUG
bool USetContainsAllAsciiItem(USet* set) {
  static constexpr int kBufferSize = 64;
  UChar buffer[kBufferSize];

  const int length = uset_getItemCount(set);
  for (int i = 0; i < length; i++) {
    UChar32 start, end;
    UErrorCode status = U_ZERO_ERROR;
    const int item_length =
        uset_getItem(set, i, &start, &end, buffer, kBufferSize, &status);
    CHECK(U_SUCCESS(status));
    DCHECK_GE(item_length, 0);

    if (item_length == 0) {
      // Empty string or a range.
      if (isascii(start)) return true;
    } else {
      // A non-empty string.
      bool all_ascii = true;
      for (int j = 0; j < item_length; j++) {
        if (!isascii(buffer[j])) {
          all_ascii = false;
          break;
        }
      }

      if (all_ascii) return true;
    }
  }

  return false;
}

bool CollatorAllowsFastComparison(const icu::Collator& icu_collator) {
  UErrorCode status = U_ZERO_ERROR;

  icu::Locale icu_locale(icu_collator.getLocale(ULOC_VALID_LOCALE, status));
  DCHECK(U_SUCCESS(status));

  static constexpr int kBufferSize = 64;
  char buffer[kBufferSize];
  const int collation_keyword_length =
      icu_locale.getKeywordValue("collation", buffer, kBufferSize, status);
  DCHECK(U_SUCCESS(status));
  if (collation_keyword_length != 0) return false;

  // These attributes must be set to the expected value for fast comparisons.
  static constexpr struct {
    UColAttribute attribute;
    UColAttributeValue legal_value;
  } kAttributeChecks[] = {
      {UCOL_ALTERNATE_HANDLING, UCOL_NON_IGNORABLE},
      {UCOL_CASE_FIRST, UCOL_OFF},
      {UCOL_CASE_LEVEL, UCOL_OFF},
      {UCOL_FRENCH_COLLATION, UCOL_OFF},
      {UCOL_NUMERIC_COLLATION, UCOL_OFF},
      {UCOL_STRENGTH, UCOL_TERTIARY},
  };

  for (const auto& check : kAttributeChecks) {
    if (icu_collator.getAttribute(check.attribute, status) !=
        check.legal_value) {
      return false;
    }
    DCHECK(U_SUCCESS(status));
  }

  // No reordering codes are allowed.
  int num_reorder_codes =
      ucol_getReorderCodes(icu_collator.toUCollator(), nullptr, 0, &status);
  if (num_reorder_codes != 0) return false;
  DCHECK(U_SUCCESS(status));  // Must check *after* num_reorder_codes != 0.

  // No tailored rules are allowed.
  int32_t rules_length = 0;
  ucol_getRules(icu_collator.toUCollator(), &rules_length);
  if (rules_length != 0) return false;

  USet* tailored_set = ucol_getTailoredSet(icu_collator.toUCollator(), &status);
  DCHECK(U_SUCCESS(status));
  if (USetContainsAllAsciiItem(tailored_set)) return false;
  uset_close(tailored_set);

  // No ASCII contractions or expansions are allowed.
  USet* contractions = uset_openEmpty();
  USet* expansions = uset_openEmpty();
  ucol_getContractionsAndExpansions(icu_collator.toUCollator(), contractions,
                                    expansions, true, &status);
  if (USetContainsAllAsciiItem(contractions)) return false;
  if (USetContainsAllAsciiItem(expansions)) return false;
  DCHECK(U_SUCCESS(status));
  uset_close(contractions);
  uset_close(expansions);

  return true;
}
#endif  // DEBUG

// Fast comparison is implemented for charcodes for which the L1 collation
// weight (see kCollactionWeightsL1 above) is not 0.
//
// Note it's possible to partially process strings as long as their leading
// characters all satisfy the above criteria. In that case, and if the L3
// result is EQUAL, we set `processed_until_out` to the first non-processed
// index - future processing can begin at that offset.
//
// This fast path looks somewhat complex; mostly because it combines multiple
// passes into one. The pseudo-code for simplified multi-pass algorithm is:
//
// {
//   // We can only fast-compare a certain subset of the ASCII range.
//   // Additionally, unicode characters can change the meaning of preceding
//   // characters, for example: "o\u0308" is treated like "ö".
//   //
//   // Note, in the actual single-pass algorithm below, we tolerate non-ASCII
//   // contents outside the relevant range.
//   for (int i = 0; i < string1.length; i++) {
//     if (!CanFastCompare(string1[i])) return {};
//   }
//   for (int i = 0; i < string2.length; i++) {
//     if (!CanFastCompare(string2[i])) return {};
//   }
//
//   // Apply L1 weights.
//   for (int i = 0; i < common_length; i++) {
//     Char1T c1 = string1[i];
//     Char2T c2 = string2[i];
//     if (L1Weight[c1] != L1Weight[c2]) {
//       return L1Weight[c1] - L1Weight[c2];
//     }
//   }
//
//   // Strings are L1-equal up to the common length; if lengths differ, the
//   // longer string is treated as 'greater'.
//   if (string1.length != string2.length) string1.length - string2.length;
//
//   // Apply L3 weights.
//   for (int i = 0; i < common_length; i++) {
//     Char1T c1 = string1[i];
//     Char2T c2 = string2[i];
//     if (L3Weight[c1] != L3Weight[c2]) {
//       return L3Weight[c1] - L3Weight[c2];
//     }
//   }
//
//   return UCOL_EQUAL;
// }
std::optional<UCollationResult> TryFastCompareStrings(
    Isolate* isolate, const icu::Collator& icu_collator,
    DirectHandle<String> string1, DirectHandle<String> string2,
    int* processed_until_out) {
  // TODO(jgruber): We could avoid the flattening (done by the caller) as well
  // by implementing comparison through string iteration. This has visible
  // performance benefits (e.g. 7% on CDJS) but complicates the code. Consider
  // doing this in the future.
  DCHECK(string1->IsFlat());
  DCHECK(string2->IsFlat());

  *processed_until_out = 0;

#ifdef DEBUG
  // Checked by the caller, see CompareStringsOptionsFor.
  SLOW_DCHECK(CollatorAllowsFastComparison(icu_collator));
  USE(CollatorAllowsFastComparison);
#endif  // DEBUG

  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(*string1));
  DCHECK(!SharedStringAccessGuardIfNeeded::IsNeeded(*string2));

  const int length1 = string1->length();
  const int length2 = string2->length();
  int common_length = std::min(length1, length2);

  FastCompareStringsData d;
  DisallowGarbageCollection no_gc;
  const String::FlatContent& flat1 = string1->GetFlatContent(no_gc);
  const String::FlatContent& flat2 = string2->GetFlatContent(no_gc);
  if (!FastCompareStringFlatContent(flat1, flat2, common_length, &d)) {
    DCHECK_EQ(d.l1_result, UCollationResult::UCOL_EQUAL);
    return d.FastCompareFailed(processed_until_out);
  }

  // The result is only valid if the last processed character is not followed
  // by a unicode combining character (we are overly strict and restrict to
  // ASCII).
  if (!CharIsAsciiOrOutOfBounds(flat1, length1, d.processed_until + 1) ||
      !CharIsAsciiOrOutOfBounds(flat2, length2, d.processed_until + 1)) {
    return d.FastCompareFailed(processed_until_out);
  }

  if (d.l1_result != UCollationResult::UCOL_EQUAL) {
    return d.l1_result;
  }

  // Strings are L1-equal up to their common length, length differences win.
  UCollationResult length_result = ToUCollationResult(length1 - length2);
  if (length_result != UCollationResult::UCOL_EQUAL) {
    // Strings of different lengths may still compare as equal if the longer
    // string has a fully ignored suffix, e.g. "a" vs. "a\u{1}".
    if (!CharCanFastCompareOrOutOfBounds(flat1, length1, common_length) ||
        !CharCanFastCompareOrOutOfBounds(flat2, length2, common_length)) {
      return d.FastCompareFailed(processed_until_out);
    }
    return length_result;
  }

  // L1-equal and same length, the L3 result wins.
  return d.l3_result;
}

}  // namespace

// static
const uint8_t* Intl::AsciiCollationWeightsL1() {
  return &kCollationWeightsL1[0];
}

// static
const uint8_t* Intl::AsciiCollationWeightsL3() {
  return &kCollationWeightsL3[0];
}

// static
const int Intl::kAsciiCollationWeightsLength = kCollationWeightsLength;

// ecma402/#sec-collator-comparestrings
int Intl::CompareStrings(Isolate* isolate, const icu::Collator& icu_collator,
                         Handle<String> string1, Handle<String> string2,
                         CompareStringsOptions compare_strings_options) {
  // Early return for identical strings.
  if (string1.is_identical_to(string2)) {
    return UCollationResult::UCOL_EQUAL;
  }

  // We cannot return early for 0-length strings because of Unicode
  // ignorable characters. See also crbug.com/1347690.

  string1 = String::Flatten(isolate, string1);
  string2 = String::Flatten(isolate, string2);

  int processed_until = 0;
  if (compare_strings_options == CompareStringsOptions::kTryFastPath) {
    std::optional<int> maybe_result = TryFastCompareStrings(
        isolate, icu_collator, string1, string2, &processed_until);
    if (maybe_result.has_value()) return maybe_result.value();
  }

  UCollationResult result;
  UErrorCode status = U_ZERO_ERROR;
  icu::StringPiece string_piece1 =
      ToICUStringPiece(isolate, string1, processed_until);
  if (!string_piece1.empty()) {
    icu::StringPiece string_piece2 =
        ToICUStringPiece(isolate, string2, processed_until);
    if (!string_piece2.empty()) {
      result = icu_collator.compareUTF8(string_piece1, string_piece2, status);
      DCHECK(U_SUCCESS(status));
      return result;
    }
  }

  icu::UnicodeString string_val1 =
      Intl::ToICUUnicodeString(isolate, string1, processed_until);
  icu::UnicodeString string_val2 =
      Intl::ToICUUnicodeString(isolate, string2, processed_until);
  result = icu_collator.compare(string_val1, string_val2, status);
  DCHECK(U_SUCCESS(status));
  return result;
}

// ecma402/#sup-properties-of-the-number-prototype-object
MaybeHandle<String> Intl::NumberToLocaleString(Isolate* isolate,
                                               Handle<Object> num,
                                               Handle<Object> locales,
                                               Handle<Object> options,
                                               const char* method_name) {
  Handle<Object> numeric_obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, numeric_obj,
                             Object::ToNumeric(isolate, num));

  // We only cache the instance when locales is a string/undefined and
  // options is undefined, as that is the only case when the specified
  // side-effects of examining those arguments are unobservable.
  bool can_cache = (IsString(*locales) || IsUndefined(*locales, isolate)) &&
                   IsUndefined(*options, isolate);
  if (can_cache) {
    icu::number::LocalizedNumberFormatter* cached_number_format =
        static_cast<icu::number::LocalizedNumberFormatter*>(
            isolate->get_cached_icu_object(
                Isolate::ICUObjectCacheType::kDefaultNumberFormat, locales));
    // We may use the cached icu::NumberFormat for a fast path.
    if (cached_number_format != nullptr) {
      return JSNumberFormat::FormatNumeric(isolate, *cached_number_format,
                                           numeric_obj);
    }
  }

  Handle<JSFunction> constructor = Handle<JSFunction>(
      Cast<JSFunction>(
          isolate->context()->native_context()->intl_number_format_function()),
      isolate);
  Handle<JSNumberFormat> number_format;
  // 2. Let numberFormat be ? Construct(%NumberFormat%, « locales, options »).
  StackLimitCheck stack_check(isolate);
  // New<JSNumberFormat>() requires a lot of stack space.
  const int kStackSpaceRequiredForNewJSNumberFormat = 16 * KB;
  if (stack_check.JsHasOverflowed(kStackSpaceRequiredForNewJSNumberFormat)) {
    isolate->StackOverflow();
    return MaybeHandle<String>();
  }
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, number_format,
      New<JSNumberFormat>(isolate, constructor, locales, options, method_name));

  if (can_cache) {
    isolate->set_icu_object_in_cache(
        Isolate::ICUObjectCacheType::kDefaultNumberFormat, locales,
        std::static_pointer_cast<icu::UMemory>(
            number_format->icu_number_formatter()->get()));
  }

  // Return FormatNumber(numberFormat, x).
  icu::number::LocalizedNumberFormatter* icu_number_format =
      number_format->icu_number_formatter()->raw();
  return JSNumberFormat::FormatNumeric(isolate, *icu_number_format,
                                       numeric_obj);
}

namespace {

// 22. is in « 1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500,
// 5000 »
bool IsValidRoundingIncrement(int value) {
  switch (value) {
    case 1:
    case 2:
    case 5:
    case 10:
    case 20:
    case 25:
    case 50:
    case 100:
    case 200:
    case 250:
    case 500:
    case 1000:
    case 2000:
    case 2500:
    case 5000:
      return true;
    default:
      return false;
  }
}

}  // namespace

Maybe<Intl::NumberFormatDigitOptions> Intl::SetNumberFormatDigitOptions(
    Isolate* isolate, Handle<JSReceiver> options, int mnfd_default,
    int mxfd_default, bool notation_is_compact, const char* service) {
  Factory* factory = isolate->factory();
  Intl::NumberFormatDigitOptions digit_options;

  // 1. Let mnid be ? GetNumberOption(options, "minimumIntegerDigits,", 1, 21,
  // 1).
  int mnid = 1;
  if (!GetNumberOption(isolate, options, factory->minimumIntegerDigits_string(),
                       1, 21, 1)
           .To(&mnid)) {
    return Nothing<NumberFormatDigitOptions>();
  }

  // 2. Let mnfd be ? Get(options, "minimumFractionDigits").
  Handle<Object> mnfd_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mnfd_obj,
      JSReceiver::GetProperty(isolate, options,
                              factory->minimumFractionDigits_string()),
      Nothing<NumberFormatDigitOptions>());

  // 3. Let mxfd be ? Get(options, "maximumFractionDigits").
  Handle<Object> mxfd_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mxfd_obj,
      JSReceiver::GetProperty(isolate, options,
                              factory->maximumFractionDigits_string()),
      Nothing<NumberFormatDigitOptions>());

  // 4.  Let mnsd be ? Get(options, "minimumSignificantDigits").
  Handle<Object> mnsd_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mnsd_obj,
      JSReceiver::GetProperty(isolate, options,
                              factory->minimumSignificantDigits_string()),
      Nothing<NumberFormatDigitOptions>());

  // 5. Let mxsd be ? Get(options, "maximumSignificantDigits").
  Handle<Object> mxsd_obj;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, mxsd_obj,
      JSReceiver::GetProperty(isolate, options,
                              factory->maximumSignificantDigits_string()),
      Nothing<NumberFormatDigitOptions>());

  digit_options.rounding_priority = RoundingPriority::kAuto;
  digit_options.minimum_significant_digits = 0;
  digit_options.maximum_significant_digits = 0;

  // 6. Set intlObj.[[MinimumIntegerDigits]] to mnid.
  digit_options.minimum_integer_digits = mnid;

  // 7. Let roundingIncrement be ? GetNumberOption(options, "roundingIncrement",
  // 1, 5000, 1).
  Maybe<int> maybe_rounding_increment = GetNumberOption(
      isolate, options, factory->roundingIncrement_string(), 1, 5000, 1);
  if (!maybe_rounding_increment.To(&digit_options.rounding_increment)) {
    return Nothing<NumberFormatDigitOptions>();
  }
  // 8. If roundingIncrement is not in « 1, 2, 5, 10, 20, 25, 50, 100, 200, 250,
  // 500, 1000, 2000, 2500, 5000 », throw a RangeError exception.
  if (!IsValidRoundingIncrement(digit_options.rounding_increment)) {
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewRangeError(MessageTemplate::kPropertyValueOutOfRange,
                      factory->roundingIncrement_string()),
        Nothing<NumberFormatDigitOptions>());
  }

  // 9. Let roundingMode be ? GetOption(options, "roundingMode", string, «
  // "ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand",
  // "halfTrunc", "halfEven" », "halfExpand").
  Maybe<RoundingMode> maybe_rounding_mode = GetStringOption<RoundingMode>(
      isolate, options, "roundingMode", service,
      {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor",
       "halfExpand", "halfTrunc", "halfEven"},
      {RoundingMode::kCeil, RoundingMode::kFloor, RoundingMode::kExpand,
       RoundingMode::kTrunc, RoundingMode::kHalfCeil, RoundingMode::kHalfFloor,
       RoundingMode::kHalfExpand, RoundingMode::kHalfTrunc,
       RoundingMode::kHalfEven},
      RoundingMode::kHalfExpand);
  MAYBE_RETURN(maybe_rounding_mode, Nothing<NumberFormatDigitOptions>());
  digit_options.rounding_mode = maybe_rounding_mode.FromJust();

  // 10. Let roundingPriority be ? GetOption(options, "roundingPriority",
  // "string", « "auto", "morePrecision", "lessPrecision" », "auto").

  Maybe<RoundingPriority> maybe_rounding_priority =
      GetStringOption<RoundingPriority>(
          isolate, options, "roundingPriority", service,
          {"auto", "morePrecision", "lessPrecision"},
          {RoundingPriority::kAuto, RoundingPriority::kMorePrecision,
           RoundingPriority::kLessPrecision},
          RoundingPriority::kAuto);
  MAYBE_RETURN(maybe_rounding_priority, Nothing<NumberFormatDigitOptions>());
  digit_options.rounding_priority = maybe_rounding_priority.FromJust();

  // 11. Let trailingZeroDisplay be ? GetOption(options, "trailingZeroDisplay",
  // string, « "auto", "stripIfInteger" », "auto").
  Maybe<TrailingZeroDisplay> maybe_trailing_zero_display =
      GetStringOption<TrailingZeroDisplay>(
          isolate, options, "trailingZeroDisplay", service,
          {"auto", "stripIfInteger"},
          {TrailingZeroDisplay::kAuto, TrailingZeroDisplay::kStripIfInteger},
          TrailingZeroDisplay::kAuto);
  MAYBE_RETURN(maybe_trailing_zero_display,
               Nothing<NumberFormatDigitOptions>());
  digit_options.trailing_zero_display = maybe_trailing_zero_display.FromJust();

  // 12. NOTE: All fields required by SetNumberFormatDigitOptions have now been
  // read from options. The remainder of this AO interprets the options and may
  // throw exceptions.

  // 17. If mnsd is not undefined or mxsd is not undefined, then
  // a. Set hasSd to true.
  // 18. Else,
  // a. Set hasSd to false.
  bool has_sd =
      (!IsUndefined(*mnsd_obj, isolate)) || (!IsUndefined(*mxsd_obj, isolate));

  // 19. If mnfd is not undefined or mxfd is not undefined, then
  // a. Set hasFd to true.
  // 22. Else,
  // a. Set hasFd to false.
  bool has_fd =
      (!IsUndefined(*mnfd_obj, isolate)) || (!IsUndefined(*mxfd_obj, isolate));

  // 21. Let needSd be true.
  bool need_sd = true;
  // 22. Let needFd be true.
  bool need_fd = true;
  // 23. If roundingPriority is "auto", then
  if (RoundingPriority::kAuto == digit_options.rounding_priority) {
    // a. Set needSd to hasSd.
    need_sd = has_sd;
    // b. If needSd is true, or hasFd is false and notation is "compact", then
    if (need_sd || ((!has_fd) && notation_is_compact)) {
      // i. Set needFd to false.
      need_fd = false;
    }
  }
  // 24. If needSd is true, then
  if (need_sd) {
    // 24.a If hasSd is true, then
    if (has_sd) {
      // i. Set intlObj.[[MinimumSignificantDigits]] to ?
      // DefaultNumberOption(mnsd, 1, 21, 1).
      int mnsd;
      if (!DefaultNumberOption(isolate, mnsd_obj, 1, 21, 1,
                               factory->minimumSignificantDigits_string())
               .To(&mnsd)) {
        return Nothing<NumberFormatDigitOptions>();
      }
      digit_options.minimum_significant_digits = mnsd;
      // ii. Set intlObj.[[MaximumSignificantDigits]] to ?
      // DefaultNumberOption(mxsd, intlObj.[[MinimumSignificantDigits]], 21,
      // 21).
      int mxsd;
      if (!DefaultNumberOption(isolate, mxsd_obj, mnsd, 21, 21,
                               factory->maximumSignificantDigits_string())
               .To(&mxsd)) {
        return Nothing<NumberFormatDigitOptions>();
      }
      digit_options.maximum_significant_digits = mxsd;
    } else {
      // 24.b Else
      // 24.b.i Set intlObj.[[MinimumSignificantDigits]] to 1.
      digit_options.minimum_significant_digits = 1;
      // 24.b.ii Set intlObj.[[MaximumSignificantDigits]] to 21.
      digit_options.maximum_significant_digits = 21;
    }
  }

  Handle<String> mxfd_str = factory->maximumFractionDigits_string();
  // 25. If needFd is true, then
  if (need_fd) {
    // a. If hasFd is true, then
    if (has_fd) {
      Handle<String> mnfd_str = factory->minimumFractionDigits_string();
      // i. Let mnfd be ? DefaultNumberOption(mnfd, 0, 100, undefined).
      int mnfd;
      if (!DefaultNumberOption(isolate, mnfd_obj, 0, 100, -1, mnfd_str)
               .To(&mnfd)) {
        return Nothing<NumberFormatDigitOptions>();
      }
      // ii. Let mxfd be ? DefaultNumberOption(mxfd, 0, 100, undefined).
      int mxfd;
      if (!DefaultNumberOption(isolate, mxfd_obj, 0, 100, -1, mxfd_str)
               .To(&mxfd)) {
        return Nothing<NumberFormatDigitOptions>();
      }
      // iii. If mnfd is undefined, set mnfd to min(mnfdDefault, mxfd).
      if (IsUndefined(*mnfd_obj, isolate)) {
        mnfd = std::min(mnfd_default, mxfd);
      } else if (IsUndefined(*mxfd_obj, isolate)) {
        // iv. Else if mxfd is undefined, set mxfd to max(mxfdDefault,
        // mnfd).
        mxfd = std::max(mxfd_default, mnfd);
      } else if (mnfd > mxfd) {
        // v. Else if mnfd is greater than mxfd, throw a RangeError
        // exception.
        THROW_NEW_ERROR_RETURN_VALUE(
            isolate,
            NewRangeError(MessageTemplate::kPropertyValueOutOfRange, mxfd_str),
            Nothing<NumberFormatDigitOptions>());
      }
      // vi. Set intlObj.[[MinimumFractionDigits]] to mnfd.
      digit_options.minimum_fraction_digits = mnfd;
      // vii. Set intlObj.[[MaximumFractionDigits]] to mxfd.
      digit_options.maximum_fraction_digits = mxfd;
    } else {  // b. Else
      // i. Set intlObj.[[MinimumFractionDigits]] to mnfdDefault.
      digit_options.minimum_fraction_digits = mnfd_default;
      // ii. Set intlObj.[[MaximumFractionDigits]] to mxfdDefault.
      digit_options.maximum_fraction_digits = mxfd_default;
    }
  }

  // 26. If needSd is false and needFd is false, then
  if ((!need_sd) && (!need_fd)) {
    // a. Set intlObj.[[MinimumFractionDigits]] to 0.
    digit_options.minimum_fraction_digits = 0;
    // b. Set intlObj.[[MaximumFractionDigits]] to 0.
    digit_options.maximum_fraction_digits = 0;
    // c. Set intlObj.[[MinimumSignificantDigits]] to 1.
    digit_options.minimum_significant_digits = 1;
    // d. Set intlObj.[[MaximumSignificantDigits]] to 2.
    digit_options.maximum_significant_digits = 2;
    // e. Set intlObj.[[RoundingType]] to morePrecision.
    digit_options.rounding_type = RoundingType::kMorePrecision;
    // 27. Else if roundingPriority is "morePrecision", then
  } else if (digit_options.rounding_priority ==
             RoundingPriority::kMorePrecision) {
    // i. Set intlObj.[[RoundingType]] to morePrecision.
    digit_options.rounding_type = RoundingType::kMorePrecision;
    // 28. Else if roundingPriority is "lessPrecision", then
  } else if (digit_options.rounding_priority ==
             RoundingPriority::kLessPrecision) {
    // i. Set intlObj.[[RoundingType]] to lessPrecision.
    digit_options.rounding_type = RoundingType::kLessPrecision;
    // 29. Else if hasSd, then
  } else if (has_sd) {
    // i. Set intlObj.[[RoundingType]] to significantDigits.
    digit_options.rounding_type = RoundingType::kSignificantDigits;
    // 30. Else,
  } else {
    // i.Set intlObj.[[RoundingType]] to fractionDigits.
    digit_options.rounding_type = RoundingType::kFractionDigits;
  }
  // 31. If roundingIncrement is not 1, then
  if (digit_options.rounding_increment != 1) {
    // a. If intlObj.[[RoundingType]] is not fractionDigits, throw a TypeError
    // exception.
    if (digit_options.rounding_type != RoundingType::kFractionDigits) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate, NewTypeError(MessageTemplate::kBadRoundingType),
          Nothing<NumberFormatDigitOptions>());
    }
    // b. If intlObj.[[MaximumFractionDigits]] is not equal to
    // intlObj.[[MinimumFractionDigits]], throw a RangeError exception.
    if (digit_options.maximum_fraction_digits !=
        digit_options.minimum_fraction_digits) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(MessageTemplate::kPropertyValueOutOfRange, mxfd_str),
          Nothing<NumberFormatDigitOptions>());
    }
  }
  return Just(digit_options);
}

namespace {

// ecma402/#sec-bestavailablelocale
std::string BestAvailableLocale(const std::set<std::string>& available_locales,
                                const std::string& locale) {
  // 1. Let candidate be locale.
  std::string candidate = locale;

  // 2. Repeat,
  while (true) {
    // 2.a. If availableLocales contains an element equal to candidate, return
    //      candidate.
    if (available_locales.find(candidate) != available_locales.end()) {
      return candidate;
    }

    // 2.b. Let pos be the character index of the last occurrence of "-"
    //      (U+002D) within candidate. If that character does not occur, return
    //      undefined.
    size_t pos = candidate.rfind('-');
    if (pos == std::string::npos) {
      return std::string();
    }

    // 2.c. If pos ≥ 2 and the character "-" occurs at index pos-2 of candidate,
    //      decrease pos by 2.
    if (pos >= 2 && candidate[pos - 2] == '-') {
      pos -= 2;
    }

    // 2.d. Let candidate be the substring of candidate from position 0,
    //      inclusive, to position pos, exclusive.
    candidate = candidate.substr(0, pos);
  }
}

struct ParsedLocale {
  std::string no_extensions_locale;
  std::string extension;
};

// Returns a struct containing a bcp47 tag without unicode extensions
// and the removed unicode extensions.
//
// For example, given 'en-US-u-co-emoji' returns 'en-US' and
// 'u-co-emoji'.
ParsedLocale ParseBCP47Locale(const std::string& locale) {
  size_t length = locale.length();
  ParsedLocale parsed_locale;

  // Privateuse or grandfathered locales have no extension sequences.
  if ((length > 1) && (locale[1] == '-')) {
    // Check to make sure that this really is a grandfathered or
    // privateuse extension. ICU can sometimes mess up the
    // canonicalization.
    DCHECK(locale[0] == 'x' || locale[0] == 'i');
    parsed_locale.no_extensions_locale = locale;
    return parsed_locale;
  }

  size_t unicode_extension_start = locale.find("-u-");

  // No unicode extensions found.
  if (unicode_extension_start == std::string::npos) {
    parsed_locale.no_extensions_locale = locale;
    return parsed_locale;
  }

  size_t private_extension_start = locale.find("-x-");

  // Unicode extensions found within privateuse subtags don't count.
  if (private_extension_start != std::string::npos &&
      private_extension_start < unicode_extension_start) {
    parsed_locale.no_extensions_locale = locale;
    return parsed_locale;
  }

  const std::string beginning = locale.substr(0, unicode_extension_start);
  size_t unicode_extension_end = length;
  DCHECK_GT(length, 2);

  // Find the end of the extension production as per the bcp47 grammar
  // by looking for '-' followed by 2 chars and then another '-'.
  for (size_t i = unicode_extension_start + 1; i < length - 2; i++) {
    if (locale[i] != '-') continue;

    if (locale[i + 2] == '-') {
      unicode_extension_end = i;
      break;
    }

    i += 2;
  }

  const std::string end = locale.substr(unicode_extension_end);
  parsed_locale.no_extensions_locale = beginning + end;
  parsed_locale.extension = locale.substr(
      unicode_extension_start, unicode_extension_end - unicode_extension_start);
  return parsed_locale;
}

// ecma402/#sec-lookupsupportedlocales
std::vector<std::string> LookupSupportedLocales(
    const std::set<std::string>& available_locales,
    const std::vector<std::string>& requested_locales) {
  // 1. Let subset be a new empty List.
  std::vector<std::string> subset;

  // 2. For each element locale of requestedLocales in List order, do
  for (const std::string& locale : requested_locales) {
    // 2. a. Let noExtensionsLocale be the String value that is locale
    //       with all Unicode locale extension sequences removed.
    std::string no_extension_locale =
        ParseBCP47Locale(locale).no_extensions_locale;

    // 2. b. Let availableLocale be
    //       BestAvailableLocale(availableLocales, noExtensionsLocale).
    std::string available_locale =
        BestAvailableLocale(available_locales, no_extension_locale);

    // 2. c. If availableLocale is not undefined, append locale to the
    //       end of subset.
    if (!available_locale.empty()) {
      subset.push_back(locale);
    }
  }

  // 3. Return subset.
  return subset;
}

icu::LocaleMatcher BuildLocaleMatcher(
    Isolate* isolate, const std::set<std::string>& available_locales,
    UErrorCode* status) {
  icu::Locale default_locale =
      icu::Locale::forLanguageTag(isolate->DefaultLocale(), *status);
  icu::LocaleMatcher::Builder builder;
  if (U_FAILURE(*status)) {
    return builder.build(*status);
  }
  builder.setDefaultLocale(&default_locale);
  for (auto it = available_locales.begin(); it != available_locales.end();
       ++it) {
    *status = U_ZERO_ERROR;
    icu::Locale l = icu::Locale::forLanguageTag(it->c_str(), *status);
    // skip invalid locale such as no-NO-NY
    if (U_SUCCESS(*status)) {
      builder.addSupportedLocale(l);
    }
  }
  return builder.build(*status);
}

class Iterator : public icu::Locale::Iterator {
 public:
  Iterator(std::vector<std::string>::const_iterator begin,
           std::vector<std::string>::const_iterator end)
      : iter_(begin), end_(end) {}
  ~Iterator() override = default;

  UBool hasNext() const override { return iter_ != end_; }

  const icu::Locale& next() override {
    UErrorCode status = U_ZERO_ERROR;
    locale_ = icu::Locale::forLanguageTag(iter_->c_str(), status);
    DCHECK(U_SUCCESS(status));
    ++iter_;
    return locale_;
  }

 private:
  std::vector<std::string>::const_iterator iter_;
  std::vector<std::string>::const_iterator end_;
  icu::Locale locale_;
};

// ecma402/#sec-bestfitmatcher
// The BestFitMatcher abstract operation compares requestedLocales, which must
// be a List as returned by CanonicalizeLocaleList, against the locales in
// availableLocales and determines the best available language to meet the
// request. The algorithm is implementation dependent, but should produce
// results that a typical user of the requested locales would perceive
// as at least as good as those produced by the LookupMatcher abstract
// operation. Options specified through Unicode locale extension sequences must
// be ignored by the algorithm. Information about such subsequences is returned
// separately. The abstract operation returns a record with a [[locale]] field,
// whose value is the language tag of the selected locale, which must be an
// element of availableLocales. If the language tag of the request locale that
// led to the selected locale contained a Unicode locale extension sequence,
// then the returned record also contains an [[extension]] field whose value is
// the first Unicode locale extension sequence within the request locale
// language tag.
std::string BestFitMatcher(Isolate* isolate,
                           const std::set<std::string>& available_locales,
                           const std::vector<std::string>& requested_locales) {
  UErrorCode status = U_ZERO_ERROR;
  Iterator iter(requested_locales.cbegin(), requested_locales.cend());
  std::string bestfit = BuildLocaleMatcher(isolate, available_locales, &status)
                            .getBestMatchResult(iter, status)
                            .makeResolvedLocale(status)
                            .toLanguageTag<std::string>(status);
  DCHECK(U_SUCCESS(status));
  return bestfit;
}

// ECMA 402 9.2.8 BestFitSupportedLocales(availableLocales, requestedLocales)
// https://tc39.github.io/ecma402/#sec-bestfitsupportedlocales
std::vector<std::string> BestFitSupportedLocales(
    Isolate* isolate, const std::set<std::string>& available_locales,
    const std::vector<std::string>& requested_locales) {
  UErrorCode status = U_ZERO_ERROR;
  icu::LocaleMatcher matcher =
      BuildLocaleMatcher(isolate, available_locales, &status);
  std::vector<std::string> result;
  if (U_SUCCESS(status)) {
    for (auto it = requested_locales.cbegin(); it != requested_locales.cend();
         it++) {
      status = U_ZERO_ERROR;
      icu::Locale desired = icu::Locale::forLanguageTag(it->c_str(), status);
      icu::LocaleMatcher::Result matched =
          matcher.getBestMatchResult(desired, status);
      if (U_FAILURE(status)) continue;
      if (matched.getSupportedIndex() < 0) continue;

      // The BestFitSupportedLocales abstract operation returns the *SUBSET* of
      // the provided BCP 47 language priority list requestedLocales for which
      // availableLocales has a matching locale when using the Best Fit Matcher
      // algorithm. Locales appear in the same order in the returned list as in
      // requestedLocales. The steps taken are implementation dependent.
      std::string bestfit = desired.toLanguageTag<std::string>(status);
      if (U_FAILURE(status)) continue;
      result.push_back(bestfit);
    }
  }
  return result;
}

// ecma262 #sec-createarrayfromlist
MaybeHandle<JSArray> CreateArrayFromList(Isolate* isolate,
                                         std::vector<std::string> elements,
                                         PropertyAttributes attr) {
  Factory* factory = isolate->factory();
  // Let array be ! ArrayCreate(0).
  Handle<JSArray> array = factory->NewJSArray(0);

  uint32_t length = static_cast<uint32_t>(elements.size());
  // 3. Let n be 0.
  // 4. For each element e of elements, do
  for (uint32_t i = 0; i < length; i++) {
    // a. Let status be CreateDataProperty(array, ! ToString(n), e).
    const std::string& part = elements[i];
    DirectHandle<String> value =
        factory->NewStringFromUtf8(base::CStrVector(part.c_str()))
            .ToHandleChecked();
    MAYBE_RETURN(JSObject::AddDataElement(array, i, value, attr),
                 MaybeHandle<JSArray>());
  }
  // 5. Return array.
  return MaybeHandle<JSArray>(array);
}

// ECMA 402 9.2.9 SupportedLocales(availableLocales, requestedLocales, options)
// https://tc39.github.io/ecma402/#sec-supportedlocales
MaybeHandle<JSObject> SupportedLocales(
    Isolate* isolate, const char* method_name,
    const std::set<std::string>& available_locales,
    const std::vector<std::string>& requested_locales, Handle<Object> options) {
  std::vector<std::string> supported_locales;

  // 1. Set options to ? CoerceOptionsToObject(options).
  Handle<JSReceiver> options_obj;
  ASSIGN_RETURN_ON_EXCEPTION(
      isolate, options_obj,
      CoerceOptionsToObject(isolate, options, method_name));

  // 2. Let matcher be ? GetOption(options, "localeMatcher", "string",
  //       « "lookup", "best fit" », "best fit").
  Maybe<Intl::MatcherOption> maybe_locale_matcher =
      Intl::GetLocaleMatcher(isolate, options_obj, method_name);
  MAYBE_RETURN(maybe_locale_matcher, MaybeHandle<JSObject>());
  Intl::MatcherOption matcher = maybe_locale_matcher.FromJust();

  // 3. If matcher is "best fit", then
  //    a. Let supportedLocales be BestFitSupportedLocales(availableLocales,
  //       requestedLocales).
  if (matcher == Intl::MatcherOption::kBestFit &&
      v8_flags.harmony_intl_best_fit_matcher) {
    supported_locales =
        BestFitSupportedLocales(isolate, available_locales, requested_locales);
  } else {
    // 4. Else,
    //    a. Let supportedLocales be LookupSupportedLocales(availableLocales,
    //       requestedLocales).
    supported_locales =
        LookupSupportedLocales(available_locales, requested_locales);
  }

  // 5. Return CreateArrayFromList(supportedLocales).
  return CreateArrayFromList(isolate, supported_locales,
                             PropertyAttributes::NONE);
}

}  // namespace

// ecma-402 #sec-intl.getcanonicallocales
MaybeHandle<JSArray> Intl::GetCanonicalLocales(Isolate* isolate,
                                               Handle<Object> locales) {
  // 1. Let ll be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> maybe_ll =
      CanonicalizeLocaleList(isolate, locales, false);
  MAYBE_RETURN(maybe_ll, MaybeHandle<JSArray>());

  // 2. Return CreateArrayFromList(ll).
  return CreateArrayFromList(isolate, maybe_ll.FromJust(),
                             PropertyAttributes::NONE);
}

namespace {

MaybeHandle<JSArray> AvailableCollations(Isolate* isolate) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::Collator::getKeywordValues("collation", status));
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  return Intl::ToJSArray(isolate, "co", enumeration.get(),
                         Intl::RemoveCollation, true);
}

MaybeHandle<JSArray> VectorToJSArray(Isolate* isolate,
                                     const std::vector<std::string>& array) {
  Factory* factory = isolate->factory();
  DirectHandle<FixedArray> fixed_array =
      factory->NewFixedArray(static_cast<int32_t>(array.size()));
  int32_t index = 0;
  for (const std::string& item : array) {
    DirectHandle<String> str = factory->NewStringFromAsciiChecked(item.c_str());
    fixed_array->set(index++, *str);
  }
  return factory->NewJSArrayWithElements(fixed_array);
}

namespace {

class ResourceAvailableCurrencies {
 public:
  ResourceAvailableCurrencies() {
    UErrorCode status = U_ZERO_ERROR;
    UEnumeration* uenum =
        ucurr_openISOCurrencies(UCURR_COMMON | UCURR_NON_DEPRECATED, &status);
    DCHECK(U_SUCCESS(status));
    const char* next = nullptr;
    while (U_SUCCESS(status) &&
           (next = uenum_next(uenum, nullptr, &status)) != nullptr) {
      // Work around the issue that we do not support VEF currency code
      // in DisplayNames by not reporting it.
      if (strcmp(next, "VEF") == 0) continue;
      AddIfAvailable(next);
    }
    // Work around the issue that we do support the following currency codes
    // in DisplayNames but the ICU API is not reporting it.
    AddIfAvailable("SVC");
    AddIfAvailable("XDR");
    AddIfAvailable("XSU");
    AddIfAvailable("ZWL");
    std::sort(list_.begin(), list_.end());
    uenum_close(uenum);
  }

  const std::vector<std::string>& Get() const { return list_; }

  void AddIfAvailable(const char* currency) {
    icu::UnicodeString code(currency, -1, US_INV);
    UErrorCode status = U_ZERO_ERROR;
    int32_t len = 0;
    const UChar* result =
        ucurr_getName(code.getTerminatedBuffer(), "en", UCURR_LONG_NAME,
                      nullptr, &len, &status);
    if (U_SUCCESS(status) &&
        u_strcmp(result, code.getTerminatedBuffer()) != 0) {
      list_.push_back(currency);
    }
  }

 private:
  std::vector<std::string> list_;
};

const std::vector<std::string>& GetAvailableCurrencies() {
  static base::LazyInstance<ResourceAvailableCurrencies>::type
      available_currencies = LAZY_INSTANCE_INITIALIZER;
  return available_currencies.Pointer()->Get();
}
}  // namespace

MaybeHandle<JSArray> AvailableCurrencies(Isolate* isolate) {
  return VectorToJSArray(isolate, GetAvailableCurrencies());
}

MaybeHandle<JSArray> AvailableNumberingSystems(Isolate* isolate) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::NumberingSystem::getAvailableNames(status));
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  // Need to filter out isAlgorithmic
  return Intl::ToJSArray(
      isolate, "nu", enumeration.get(),
      [](const char* value) {
        UErrorCode status = U_ZERO_ERROR;
        std::unique_ptr<icu::NumberingSystem> numbering_system(
            icu::NumberingSystem::createInstanceByName(value, status));
        // Skip algorithmic one since chrome filter out the resource.
        return U_FAILURE(status) || numbering_system->isAlgorithmic();
      },
      true);
}

MaybeHandle<JSArray> AvailableTimeZones(Isolate* isolate) {
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::TimeZone::createTimeZoneIDEnumeration(
          UCAL_ZONE_TYPE_CANONICAL_LOCATION, nullptr, nullptr, status));
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewRangeError(MessageTemplate::kIcuError));
  }
  return Intl::ToJSArray(isolate, nullptr, enumeration.get(), nullptr, true);
}

MaybeHandle<JSArray> AvailableUnits(Isolate* isolate) {
  Factory* factory = isolate->factory();
  std::set<std::string> sanctioned(Intl::SanctionedSimpleUnits());
  DirectHandle<FixedArray> fixed_array =
      factory->NewFixedArray(static_cast<int32_t>(sanctioned.size()));
  int32_t index = 0;
  for (const std::string& item : sanctioned) {
    DirectHandle<String> str = factory->NewStringFromAsciiChecked(item.c_str());
    fixed_array->set(index++, *str);
  }
  return factory->NewJSArrayWithElements(fixed_array);
}

}  // namespace

// ecma-402 #sec-intl.supportedvaluesof
MaybeHandle<JSArray> Intl::SupportedValuesOf(Isolate* isolate,
                                             Handle<Object> key_obj) {
  Factory* factory = isolate->factory();
  // 1. 1. Let key be ? ToString(key).
  Handle<String> key_str;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, key_str,
                             Object::ToString(isolate, key_obj));
  // 2. If key is "calendar", then
  if (factory->calendar_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableCalendars( ).
    return Intl::AvailableCalendars(isolate);
  }
  // 3. Else if key is "collation", then
  if (factory->collation_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableCollations( ).
    return AvailableCollations(isolate);
  }
  // 4. Else if key is "currency", then
  if (factory->currency_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableCurrencies( ).
    return AvailableCurrencies(isolate);
  }
  // 5. Else if key is "numberingSystem", then
  if (factory->numberingSystem_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableNumberingSystems( ).
    return AvailableNumberingSystems(isolate);
  }
  // 6. Else if key is "timeZone", then
  if (factory->timeZone_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableTimeZones( ).
    return AvailableTimeZones(isolate);
  }
  // 7. Else if key is "unit", then
  if (factory->unit_string()->Equals(*key_str)) {
    // a. Let list be ! AvailableUnits( ).
    return AvailableUnits(isolate);
  }
  // 8. Else,
  // a. Throw a RangeError exception.
  // 9. Return ! CreateArrayFromList( list ).

  THROW_NEW_ERROR(
      isolate,
      NewRangeError(MessageTemplate::kInvalid,
                    factory->NewStringFromStaticChars("key"), key_str));
}

// ECMA 402 Intl.*.supportedLocalesOf
MaybeHandle<JSObject> Intl::SupportedLocalesOf(
    Isolate* isolate, const char* method_name,
    const std::set<std::string>& available_locales, Handle<Object> locales,
    Handle<Object> options) {
  // Let availableLocales be %Collator%.[[AvailableLocales]].

  // Let requestedLocales be ? CanonicalizeLocaleList(locales).
  Maybe<std::vector<std::string>> requested_locales =
      CanonicalizeLocaleList(isolate, locales, false);
  MAYBE_RETURN(requested_locales, MaybeHandle<JSObject>());

  // Return ? SupportedLocales(availableLocales, requestedLocales, options).
  return SupportedLocales(isolate, method_name, available_locales,
                          requested_locales.FromJust(), options);
}

namespace {

template <typename T>
bool IsValidExtension(const icu::Locale& locale, const char* key,
                      const std::string& value) {
  const char* legacy_type = uloc_toLegacyType(key, value.c_str());
  if (legacy_type == nullptr) {
    return false;
  }
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::StringEnumeration> enumeration(
      T::getKeywordValuesForLocale(key, icu::Locale(locale.getBaseName()),
                                   false, status));
  if (U_FAILURE(status)) {
    return false;
  }
  int32_t length;
  for (const char* item = enumeration->next(&length, status);
       U_SUCCESS(status) && item != nullptr;
       item = enumeration->next(&length, status)) {
    if (strcmp(legacy_type, item) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool Intl::IsValidCollation(const icu::Locale& locale,
                            const std::string& value) {
  std::set<std::string> invalid_values = {"standard", "search"};
  if (invalid_values.find(value) != invalid_values.end()) return false;
  return IsValidExtension<icu::Collator>(locale, "collation", value);
}

bool Intl::IsWellFormedCalendar(const std::string& value) {
  return JSLocale::Is38AlphaNumList(value);
}

// ecma402/#sec-iswellformedcurrencycode
bool Intl::IsWellFormedCurrency(const std::string& currency) {
  return JSLocale::Is3Alpha(currency);
}

bool Intl::IsValidCalendar(const icu::Locale& locale,
                           const std::string& value) {
  return IsValidExtension<icu::Calendar>(locale, "calendar", value);
}

bool Intl::IsValidNumberingSystem(const std::string& value) {
  std::set<std::string> invalid_values = {"native", "traditio", "finance"};
  if (invalid_values.find(value) != invalid_values.end()) return false;
  UErrorCode status = U_ZERO_ERROR;
  std::unique_ptr<icu::NumberingSystem> numbering_system(
      icu::NumberingSystem::createInstanceByName(value.c_str(), status));
  return U_SUCCESS(status) && numbering_system != nullptr &&
         !numbering_system->isAlgorithmic();
}

namespace {

bool IsWellFormedNumberingSystem(const std::string& value) {
  return JSLocale::Is38AlphaNumList(value);
}

std::map<std::string, std::string> LookupAndValidateUnicodeExtensions(
    icu::Locale* icu_locale, const std::set<std::string>& relevant_keys) {
  std::map<std::string, std::string> extensions;

  UErrorCode status = U_ZERO_ERROR;
  icu::LocaleBuilder builder;
  builder.setLocale(*icu_locale).clearExtensions();
  std::unique_ptr<icu::StringEnumeration> keywords(
      icu_locale->createKeywords(status));
  if (U_FAILURE(status)) return extensions;

  if (!keywords) return extensions;
  char value[ULOC_FULLNAME_CAPACITY];

  int32_t length;
  status = U_ZERO_ERROR;
  for (const char* keyword = keywords->next(&length, status);
       keyword != nullptr; keyword = keywords->next(&length, status)) {
    // Ignore failures in ICU and skip to the next keyword.
    //
    // This is fine.™
    if (U_FAILURE(status)) {
      status = U_ZERO_ERROR;
      continue;
    }

    icu_locale->getKeywordValue(keyword, value, ULOC_FULLNAME_CAPACITY, status);

    // Ignore failures in ICU and skip to the next keyword.
    //
    // This is fine.™
    if (U_FAILURE(status)) {
      status = U_ZERO_ERROR;
      continue;
    }

    const char* bcp47_key = uloc_toUnicodeLocaleKey(keyword);

    if (bcp47_key && (relevant_keys.find(bcp47_key) != relevant_keys.end())) {
      const char* bcp47_value = uloc_toUnicodeLocaleType(bcp47_key, value);
      bool is_valid_value = false;
      // 8.h.ii.1.a If keyLocaleData contains requestedValue, then
      if (strcmp("ca", bcp47_key) == 0) {
        is_valid_value = Intl::IsValidCalendar(*icu_locale, bcp47_value);
      } else if (strcmp("co", bcp47_key) == 0) {
        is_valid_value = Intl::IsValidCollation(*icu_locale, bcp47_value);
      } else if (strcmp("hc", bcp47_key) == 0) {
        // https://www.unicode.org/repos/cldr/tags/latest/common/bcp47/calendar.xml
        std::set<std::string> valid_values = {"h11", "h12", "h23", "h24"};
        is_valid_value = valid_values.find(bcp47_value) != valid_values.end();
      } else if (strcmp("lb", bcp47_key) == 0) {
        // https://www.unicode.org/repos/cldr/tags/latest/common/bcp47/segmentation.xml
        std::set<std::string> valid_values = {"strict", "normal", "loose"};
        is_valid_value = valid_values.find(bcp47_value) != valid_values.end();
      } else if (strcmp("kn", bcp47_key) == 0) {
        // https://www.unicode.org/repos/cldr/tags/latest/common/bcp47/collation.xml
        std::set<std::string> valid_values = {"true", "false"};
        is_valid_value = valid_values.find(bcp47_value) != valid_values.end();
      } else if (strcmp("kf", bcp47_key) == 0) {
        // https://www.unicode.org/repos/cldr/tags/latest/common/bcp47/collation.xml
        std::set<std::string> valid_values = {"upper", "lower", "false"};
        is_valid_value = valid_values.find(bcp47_value) != valid_values.end();
      } else if (strcmp("nu", bcp47_key) == 0) {
        is_valid_value = Intl::IsValidNumberingSystem(bcp47_value);
      }
      if (is_valid_value) {
        extensions.insert(
            std::pair<std::string, std::string>(bcp47_key, bcp47_value));
        builder.setUnicodeLocaleKeyword(bcp47_key, bcp47_value);
      }
    }
  }

  status = U_ZERO_ERROR;
  *icu_locale = builder.build(status);

  return extensions;
}

// ecma402/#sec-lookupmatcher
std::string LookupMatcher(Isolate* isolate,
                          const std::set<std::string>& available_locales,
                          const std::vector<std::string>& requested_locales) {
  // 1. Let result be a new Record.
  std::string result;

  // 2. For each element locale of requestedLocales in List order, do
  for (const std::string& locale : requested_locales) {
    // 2. a. Let noExtensionsLocale be the String value that is locale
    //       with all Unicode locale extension sequences removed.
    ParsedLocale parsed_locale = ParseBCP47Locale(locale);
    std::string no_extensions_locale = parsed_locale.no_extensions_locale;

    // 2. b. Let availableLocale be
    //       BestAvailableLocale(availableLocales, noExtensionsLocale).
    std::string available_locale =
        BestAvailableLocale(available_locales, no_extensions_locale);

    // 2. c. If availableLocale is not undefined, append locale to the
    //       end of subset.
    if (!available_locale.empty()) {
      // Note: The following steps are not performed here because we
      // can use ICU to parse the unicode locale extension sequence
      // as part of Intl::ResolveLocale.
      //
      // There's no need to separate the unicode locale extensions
      // right here. Instead just return the available locale with the
      // extensions.
      //
      // 2. c. i. Set result.[[locale]] to availableLocale.
      // 2. c. ii. If locale and noExtensionsLocale are not the same
      // String value, then
      // 2. c. ii. 1. Let extension be the String value consisting of
      // the first substring of locale that is a Unicode locale
      // extension sequence.
      // 2. c. ii. 2. Set result.[[extension]] to extension.
      // 2. c. iii. Return result.
      return available_locale + parsed_locale.extension;
    }
  }

  // 3. Let defLocale be DefaultLocale();
  // 4. Set result.[[locale]] to defLocale.
  // 5. Return result.
  return isolate->DefaultLocale();
}

}  // namespace

// This function doesn't correspond exactly with the spec. Instead
// we use ICU to do all the string manipulations that the spec
// peforms.
//
// The spec uses this function to normalize values for various
// relevant extension keys (such as disallowing "search" for
// collation). Instead of doing this here, we let the callers of
// this method perform such normalization.
//
// ecma402/#sec-resolvelocale
Maybe<Intl::ResolvedLocale> Intl::ResolveLocale(
    Isolate* isolate, const std::set<std::string>& available_locales,
    const std::vector<std::string>& requested_locales, MatcherOption matcher,
    const std::set<std::string>& relevant_extension_keys) {
  std::string locale;
  if (matcher == Intl::MatcherOption::kBestFit &&
      v8_flags.harmony_intl_best_fit_matcher) {
    locale = BestFitMatcher(isolate, available_locales, requested_locales);
  } else {
    locale = LookupMatcher(isolate, available_locales, requested_locales);
  }

  Maybe<icu::Locale> maybe_icu_locale = CreateICULocale(locale);
  MAYBE_RETURN(maybe_icu_locale, Nothing<Intl::ResolvedLocale>());
  icu::Locale icu_locale = maybe_icu_locale.FromJust();
  std::map<std::string, std::string> extensions =
      LookupAndValidateUnicodeExtensions(&icu_locale, relevant_extension_keys);

  std::string canonicalized_locale = Intl::ToLanguageTag(icu_locale).FromJust();

  // TODO(gsathya): Remove privateuse subtags from extensions.

  return Just(
      Intl::ResolvedLocale{canonicalized_locale, icu_locale, extensions});
}

Handle<Managed<icu::UnicodeString>> Intl::SetTextToBreakIterator(
    Isolate* isolate, Handle<String> text, icu::BreakIterator* break_iterator) {
  text = String::Flatten(isolate, text);
  std::shared_ptr<icu::UnicodeString> u_text{static_cast<icu::UnicodeString*>(
      Intl::ToICUUnicodeString(isolate, text).clone())};

  Handle<Managed<icu::UnicodeString>> new_u_text =
      Managed<icu::UnicodeString>::From(isolate, 0, u_text);

  break_iterator->setText(*u_text);
  return new_u_text;
}

// ecma262 #sec-string.prototype.normalize
MaybeHandle<String> Intl::Normalize(Isolate* isolate, Handle<String> string,
                                    Handle<Object> form_input) {
  const char* form_name;
  UNormalization2Mode form_mode;
  if (IsUndefined(*form_input, isolate)) {
    // default is FNC
    form_name = "nfc";
    form_mode = UNORM2_COMPOSE;
  } else {
    Handle<String> form;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, form,
                               Object::ToString(isolate, form_input));

    if (String::Equals(isolate, form, isolate->factory()->NFC_string())) {
      form_name = "nfc";
      form_mode = UNORM2_COMPOSE;
    } else if (String::Equals(isolate, form,
                              isolate->factory()->NFD_string())) {
      form_name = "nfc";
      form_mode = UNORM2_DECOMPOSE;
    } else if (String::Equals(isolate, form,
                              isolate->factory()->NFKC_string())) {
      form_name = "nfkc";
      form_mode = UNORM2_COMPOSE;
    } else if (String::Equals(isolate, form,
                              isolate->factory()->NFKD_string())) {
      form_name = "nfkc";
      form_mode = UNORM2_DECOMPOSE;
    } else {
      Handle<String> valid_forms =
          isolate->factory()->NewStringFromStaticChars("NFC, NFD, NFKC, NFKD");
      THROW_NEW_ERROR(
          isolate,
          NewRangeError(MessageTemplate::kNormalizationForm, valid_forms));
    }
  }

  int length = string->length();
  string = String::Flatten(isolate, string);
  icu::UnicodeString result;
  std::unique_ptr<base::uc16[]> sap;
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString input = ToICUUnicodeString(isolate, string);
  // Getting a singleton. Should not free it.
  const icu::Normalizer2* normalizer =
      icu::Normalizer2::getInstance(nullptr, form_name, form_mode, status);
  DCHECK(U_SUCCESS(status));
  DCHECK_NOT_NULL(normalizer);
  int32_t normalized_prefix_length =
      normalizer->spanQuickCheckYes(input, status);
  // Quick return if the input is already normalized.
  if (length == normalized_prefix_length) return string;
  icu::UnicodeString unnormalized =
      input.tempSubString(normalized_prefix_length);
  // Read-only alias of the normalized prefix.
  result.setTo(false, input.getBuffer(), normalized_prefix_length);
  // copy-on-write; normalize the suffix and append to |result|.
  normalizer->normalizeSecondAndAppend(result, unnormalized, status);

  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }

  return Intl::ToString(isolate, result);
}

// ICUTimezoneCache calls out to ICU for TimezoneCache
// functionality in a straightforward way.
class ICUTimezoneCache : public base::TimezoneCache {
 public:
  ICUTimezoneCache() : timezone_(nullptr) { Clear(TimeZoneDetection::kSkip); }

  ~ICUTimezoneCache() override { Clear(TimeZoneDetection::kSkip); }

  const char* LocalTimezone(double time_ms) override;

  double DaylightSavingsOffset(double time_ms) override;

  double LocalTimeOffset(double time_ms, bool is_utc) override;

  void Clear(TimeZoneDetection time_zone_detection) override;

 private:
  icu::TimeZone* GetTimeZone();

  bool GetOffsets(double time_ms, bool is_utc, int32_t* raw_offset,
                  int32_t* dst_offset);

  icu::TimeZone* timezone_;

  std::string timezone_name_;
  std::string dst_timezone_name_;
};

const char* ICUTimezoneCache::LocalTimezone(double time_ms) {
  bool is_dst = DaylightSavingsOffset(time_ms) != 0;
  std::string* name = is_dst ? &dst_timezone_name_ : &timezone_name_;
  if (name->empty()) {
    icu::UnicodeString result;
    GetTimeZone()->getDisplayName(is_dst, icu::TimeZone::LONG, result);
    result += '\0';

    icu::StringByteSink<std::string> byte_sink(name);
    result.toUTF8(byte_sink);
  }
  DCHECK(!name->empty());
  return name->c_str();
}

icu::TimeZone* ICUTimezoneCache::GetTimeZone() {
  if (timezone_ == nullptr) {
    timezone_ = icu::TimeZone::createDefault();
  }
  return timezone_;
}

bool ICUTimezoneCache::GetOffsets(double time_ms, bool is_utc,
                                  int32_t* raw_offset, int32_t* dst_offset) {
  UErrorCode status = U_ZERO_ERROR;
  if (is_utc) {
    GetTimeZone()->getOffset(time_ms, false, *raw_offset, *dst_offset, status);
  } else {
    // Note that casting TimeZone to BasicTimeZone is safe because we know that
    // icu::TimeZone used here is a BasicTimeZone.
    static_cast<const icu::BasicTimeZone*>(GetTimeZone())
        ->getOffsetFromLocal(time_ms, UCAL_TZ_LOCAL_FORMER,
                             UCAL_TZ_LOCAL_FORMER, *raw_offset, *dst_offset,
                             status);
  }

  return U_SUCCESS(status);
}

double ICUTimezoneCache::DaylightSavingsOffset(double time_ms) {
  int32_t raw_offset, dst_offset;
  if (!GetOffsets(time_ms, true, &raw_offset, &dst_offset)) return 0;
  return dst_offset;
}

double ICUTimezoneCache::LocalTimeOffset(double time_ms, bool is_utc) {
  int32_t raw_offset, dst_offset;
  if (!GetOffsets(time_ms, is_utc, &raw_offset, &dst_offset)) return 0;
  return raw_offset + dst_offset;
}

void ICUTimezoneCache::Clear(TimeZoneDetection time_zone_detection) {
  delete timezone_;
  timezone_ = nullptr;
  timezone_name_.clear();
  dst_timezone_name_.clear();
  if (time_zone_detection == TimeZoneDetection::kRedetect) {
    icu::TimeZone::adoptDefault(icu::TimeZone::detectHostTimeZone());
  }
}

base::TimezoneCache* Intl::CreateTimeZoneCache() {
  return v8_flags.icu_timezone_data ? new ICUTimezoneCache()
                                    : base::OS::CreateTimezoneCache();
}

Maybe<Intl::MatcherOption> Intl::GetLocaleMatcher(Isolate* isolate,
                                                  Handle<JSReceiver> options,
                                                  const char* method_name) {
  return GetStringOption<Intl::MatcherOption>(
      isolate, options, "localeMatcher", method_name, {"best fit", "lookup"},
      {Intl::MatcherOption::kBestFit, Intl::MatcherOption::kLookup},
      Intl::MatcherOption::kBestFit);
}

Maybe<bool> Intl::GetNumberingSystem(Isolate* isolate,
                                     Handle<JSReceiver> options,
                                     const char* method_name,
                                     std::unique_ptr<char[]>* result) {
  const std::vector<const char*> empty_values = {};
  Maybe<bool> maybe = GetStringOption(isolate, options, "numberingSystem",
                                      empty_values, method_name, result);
  MAYBE_RETURN(maybe, Nothing<bool>());
  if (maybe.FromJust() && *result != nullptr) {
    if (!IsWellFormedNumberingSystem(result->get())) {
      THROW_NEW_ERROR_RETURN_VALUE(
          isolate,
          NewRangeError(
              MessageTemplate::kInvalid,
              isolate->factory()->numberingSystem_string(),
              isolate->factory()->NewStringFromAsciiChecked(result->get())),
          Nothing<bool>());
    }
    return Just(true);
  }
  return Just(false);
}

const std::set<std::string>& Intl::GetAvailableLocales() {
  static base::LazyInstance<Intl::AvailableLocales<>>::type available_locales =
      LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

namespace {

struct CheckCalendar {
  static const char* key() { return "calendar"; }
  static const char* path() { return nullptr; }
};

}  // namespace

const std::set<std::string>& Intl::GetAvailableLocalesForDateFormat() {
  static base::LazyInstance<Intl::AvailableLocales<CheckCalendar>>::type
      available_locales = LAZY_INSTANCE_INITIALIZER;
  return available_locales.Pointer()->Get();
}

constexpr uint16_t kInfinityChar = 0x221e;

Handle<String> Intl::NumberFieldToType(Isolate* isolate,
                                       const NumberFormatSpan& part,
                                       const icu::UnicodeString& text,
                                       bool is_nan) {
  switch (static_cast<UNumberFormatFields>(part.field_id)) {
    case UNUM_INTEGER_FIELD:
      if (is_nan) return isolate->factory()->nan_string();
      if (text.charAt(part.begin_pos) == kInfinityChar ||
          // en-US-POSIX output "INF" for Infinity
          (part.end_pos - part.begin_pos == 3 &&
           text.tempSubString(part.begin_pos, 3) == "INF")) {
        return isolate->factory()->infinity_string();
      }
      return isolate->factory()->integer_string();
    case UNUM_FRACTION_FIELD:
      return isolate->factory()->fraction_string();
    case UNUM_DECIMAL_SEPARATOR_FIELD:
      return isolate->factory()->decimal_string();
    case UNUM_GROUPING_SEPARATOR_FIELD:
      return isolate->factory()->group_string();
    case UNUM_CURRENCY_FIELD:
      return isolate->factory()->currency_string();
    case UNUM_PERCENT_FIELD:
      return isolate->factory()->percentSign_string();
    case UNUM_SIGN_FIELD:
      return (text.charAt(part.begin_pos) == '+')
                 ? isolate->factory()->plusSign_string()
                 : isolate->factory()->minusSign_string();
    case UNUM_EXPONENT_SYMBOL_FIELD:
      return isolate->factory()->exponentSeparator_string();

    case UNUM_EXPONENT_SIGN_FIELD:
      return isolate->factory()->exponentMinusSign_string();

    case UNUM_EXPONENT_FIELD:
      return isolate->factory()->exponentInteger_string();

    case UNUM_PERMILL_FIELD:
      // We're not creating any permill formatter, and it's not even clear how
      // that would be possible with the ICU API.
      UNREACHABLE();

    case UNUM_COMPACT_FIELD:
      return isolate->factory()->compact_string();
    case UNUM_MEASURE_UNIT_FIELD:
      return isolate->factory()->unit_string();

    case UNUM_APPROXIMATELY_SIGN_FIELD:
      return isolate->factory()->approximatelySign_string();

    default:
      UNREACHABLE();
  }
}

// A helper function to convert the FormattedValue for several Intl objects.
MaybeHandle<String> Intl::FormattedToString(
    Isolate* isolate, const icu::FormattedValue& formatted) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString result = formatted.toString(status);
  if (U_FAILURE(status)) {
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kIcuError));
  }
  return Intl::ToString(isolate, result);
}

MaybeHandle<JSArray> Intl::ToJSArray(
    Isolate* isolate, const char* unicode_key,
    icu::StringEnumeration* enumeration,
    const std::function<bool(const char*)>& removes, bool sort) {
  UErrorCode status = U_ZERO_ERROR;
  std::vector<std::string> array;
  for (const char* item = enumeration->next(nullptr, status);
       U_SUCCESS(status) && item != nullptr;
       item = enumeration->next(nullptr, status)) {
    if (unicode_key != nullptr) {
      item = uloc_toUnicodeLocaleType(unicode_key, item);
    }
    if (removes == nullptr || !(removes)(item)) {
      array.push_back(item);
    }
  }

  if (sort) {
    std::sort(array.begin(), array.end());
  }
  return VectorToJSArray(isolate, array);
}

bool Intl::RemoveCollation(const char* collation) {
  return strcmp("standard", collation) == 0 || strcmp("search", collation) == 0;
}

// See the list in ecma402 #sec-issanctionedsimpleunitidentifier
std::set<std::string> Intl::SanctionedSimpleUnits() {
  return std::set<std::string>(
      {"acre",        "bit",         "byte",        "celsius",
       "centimeter",  "day",         "degree",      "fahrenheit",
       "fluid-ounce", "foot",        "gallon",      "gigabit",
       "gigabyte",    "gram",        "hectare",     "hour",
       "inch",        "kilobit",     "kilobyte",    "kilogram",
       "kilometer",   "liter",       "megabit",     "megabyte",
       "meter",       "microsecond", "mile",        "mile-scandinavian",
       "millimeter",  "milliliter",  "millisecond", "minute",
       "month",       "nanosecond",  "ounce",       "percent",
       "petabyte",    "pound",       "second",      "stone",
       "terabit",     "terabyte",    "week",        "yard",
       "year"});
}

// ecma-402/#sec-isvalidtimezonename

namespace {
bool IsUnicodeStringValidTimeZoneName(const icu::UnicodeString& id) {
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString canonical;
  icu::TimeZone::getCanonicalID(id, canonical, status);
  return U_SUCCESS(status) &&
         canonical != icu::UnicodeString("Etc/Unknown", -1, US_INV);
}
}  // namespace

MaybeHandle<String> Intl::CanonicalizeTimeZoneName(
    Isolate* isolate, DirectHandle<String> identifier) {
  UErrorCode status = U_ZERO_ERROR;
  std::string time_zone =
      JSDateTimeFormat::CanonicalizeTimeZoneID(identifier->ToCString().get());
  icu::UnicodeString time_zone_ustring =
      icu::UnicodeString(time_zone.c_str(), -1, US_INV);
  icu::UnicodeString canonical;
  icu::TimeZone::getCanonicalID(time_zone_ustring, canonical, status);
  CHECK(U_SUCCESS(status));

  return JSDateTimeFormat::TimeZoneIdToString(isolate, canonical);
}

bool Intl::IsValidTimeZoneName(Isolate* isolate, DirectHandle<String> id) {
  std::string time_zone =
      JSDateTimeFormat::CanonicalizeTimeZoneID(id->ToCString().get());
  icu::UnicodeString time_zone_ustring =
      icu::UnicodeString(time_zone.c_str(), -1, US_INV);
  return IsUnicodeStringValidTimeZoneName(time_zone_ustring);
}

bool Intl::IsValidTimeZoneName(const icu::TimeZone& tz) {
  icu::UnicodeString id;
  tz.getID(id);
  return IsUnicodeStringValidTimeZoneName(id);
}

// Function to support Temporal
std::string Intl::TimeZoneIdFromIndex(int32_t index) {
  if (index == JSTemporalTimeZone::kUTCTimeZoneIndex) {
    return "UTC";
  }
  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::TimeZone::createEnumeration());
  int32_t curr = 0;
  const char* id;

  UErrorCode status = U_ZERO_ERROR;
  while (U_SUCCESS(status) && curr < index &&
         ((id = enumeration->next(nullptr, status)) != nullptr)) {
    CHECK(U_SUCCESS(status));
    curr++;
  }
  CHECK(U_SUCCESS(status));
  CHECK(id != nullptr);
  return id;
}

int32_t Intl::GetTimeZoneIndex(Isolate* isolate,
                               DirectHandle<String> identifier) {
  if (identifier->Equals(*isolate->factory()->UTC_string())) {
    return 0;
  }

  std::string identifier_str(identifier->ToCString().get());
  std::unique_ptr<icu::TimeZone> tz(
      icu::TimeZone::createTimeZone(identifier_str.c_str()));
  if (!IsValidTimeZoneName(*tz)) {
    return -1;
  }

  std::unique_ptr<icu::StringEnumeration> enumeration(
      icu::TimeZone::createEnumeration());
  int32_t curr = 0;
  const char* id;

  UErrorCode status = U_ZERO_ERROR;
  while (U_SUCCESS(status) &&
         (id = enumeration->next(nullptr, status)) != nullptr) {
    curr++;
    if (identifier_str == id) {
      return curr;
    }
  }
  CHECK(U_SUCCESS(status));
  // We should not reach here, the !IsValidTimeZoneName should return earlier
  UNREACHABLE();
}

Intl::FormatRangeSourceTracker::FormatRangeSourceTracker() {
  start_[0] = start_[1] = limit_[0] = limit_[1] = 0;
}

void Intl::FormatRangeSourceTracker::Add(int32_t field, int32_t start,
                                         int32_t limit) {
  DCHECK_LT(field, 2);
  start_[field] = start;
  limit_[field] = limit;
}

Intl::FormatRangeSource Intl::FormatRangeSourceTracker::GetSource(
    int32_t start, int32_t limit) const {
  FormatRangeSource source = FormatRangeSource::kShared;
  if (FieldContains(0, start, limit)) {
    source = FormatRangeSource::kStartRange;
  } else if (FieldContains(1, start, limit)) {
    source = FormatRangeSource::kEndRange;
  }
  return source;
}

bool Intl::FormatRangeSourceTracker::FieldContains(int32_t field, int32_t start,
                                                   int32_t limit) const {
  DCHECK_LT(field, 2);
  return (start_[field] <= start) && (start <= limit_[field]) &&
         (start_[field] <= limit) && (limit <= limit_[field]);
}

Handle<String> Intl::SourceString(Isolate* isolate, FormatRangeSource source) {
  switch (source) {
    case FormatRangeSource::kShared:
      return ReadOnlyRoots(isolate).shared_string_handle();
    case FormatRangeSource::kStartRange:
      return ReadOnlyRoots(isolate).startRange_string_handle();
    case FormatRangeSource::kEndRange:
      return ReadOnlyRoots(isolate).endRange_string_handle();
  }
}

Handle<String> Intl::DefaultTimeZone(Isolate* isolate) {
  icu::UnicodeString id;
  {
    std::unique_ptr<icu::TimeZone> tz(icu::TimeZone::createDefault());
    tz->getID(id);
  }
  UErrorCode status = U_ZERO_ERROR;
  icu::UnicodeString canonical;
  icu::TimeZone::getCanonicalID(id, canonical, status);
  DCHECK(U_SUCCESS(status));
  return JSDateTimeFormat::TimeZoneIdToString(isolate, canonical)
      .ToHandleChecked();
}

namespace {

const icu::BasicTimeZone* CreateBasicTimeZoneFromIndex(
    int32_t time_zone_index) {
  DCHECK_NE(time_zone_index, 0);
  return static_cast<const icu::BasicTimeZone*>(
      icu::TimeZone::createTimeZone(icu::UnicodeString(
          Intl::TimeZoneIdFromIndex(time_zone_index).c_str(), -1, US_INV)));
}

// ICU only support TimeZone information in millisecond but Temporal require
// nanosecond. For most of the case, we find a approximate millisecond by
// floor to the millisecond just past the nanosecond_epoch. For negative epoch
// value, the BigInt Divide will floor closer to zero so we need to minus 1 if
// the remainder is not zero. For the case of finding previous transition, we
// need to ceil to the millisecond in the near future of the nanosecond_epoch.
enum class Direction { kPast, kFuture };
int64_t ApproximateMillisecondEpoch(Isolate* isolate,
                                    Handle<BigInt> nanosecond_epoch,
                                    Direction direction = Direction::kPast) {
  DirectHandle<BigInt> one_million = BigInt::FromUint64(isolate, 1000000);
  int64_t ms = BigInt::Divide(isolate, nanosecond_epoch, one_million)
                   .ToHandleChecked()
                   ->AsInt64();
  DirectHandle<BigInt> remainder =
      BigInt::Remainder(isolate, nanosecond_epoch, one_million)
          .ToHandleChecked();
  // If the nanosecond_epoch is not on the exact millisecond
  if (remainder->ToBoolean()) {
    if (direction == Direction::kPast) {
      if (remainder->IsNegative()) {
        // If the remaninder is negative, we know we have an negative epoch
        // We need to decrease one millisecond.
        // Move to the previous millisecond
        ms -= 1;
      }
    } else {
      if (!remainder->IsNegative()) {
        // Move to the future millisecond
        ms += 1;
      }
    }
  }
  return ms;
}

// Helper function to convert the milliseconds in int64_t
// to a BigInt in nanoseconds.
Handle<BigInt> MillisecondToNanosecond(Isolate* isolate, int64_t ms) {
  return BigInt::Multiply(isolate, BigInt::FromInt64(isolate, ms),
                          BigInt::FromUint64(isolate, 1000000))
      .ToHandleChecked();
}

}  // namespace

Handle<Object> Intl::GetTimeZoneOffsetTransitionNanoseconds(
    Isolate* isolate, int32_t time_zone_index, Handle<BigInt> nanosecond_epoch,
    Intl::Transition transition) {
  std::unique_ptr<const icu::BasicTimeZone> basic_time_zone(
      CreateBasicTimeZoneFromIndex(time_zone_index));

  icu::TimeZoneTransition icu_transition;
  UBool has_transition;
  switch (transition) {
    case Intl::Transition::kNext:
      has_transition = basic_time_zone->getNextTransition(
          ApproximateMillisecondEpoch(isolate, nanosecond_epoch), false,
          icu_transition);
      break;
    case Intl::Transition::kPrevious:
      has_transition = basic_time_zone->getPreviousTransition(
          ApproximateMillisecondEpoch(isolate, nanosecond_epoch,
                                      Direction::kFuture),
          false, icu_transition);
      break;
  }

  if (!has_transition) {
    return isolate->factory()->null_value();
  }
  // #sec-temporal-getianatimezonenexttransition and
  // #sec-temporal-getianatimezoneprevioustransition states:
  // "The operation returns null if no such transition exists for which t ≤
  // ℤ(nsMaxInstant)." and "The operation returns null if no such transition
  // exists for which t ≥ ℤ(nsMinInstant)."
  //
  // nsMinInstant = -nsMaxInstant = -8.64 × 10^21 => msMinInstant = -8.64 x
  // 10^15
  constexpr int64_t kMsMinInstant = -8.64e15;
  // nsMaxInstant = 10^8 × nsPerDay = 8.64 × 10^21 => msMaxInstant = 8.64 x
  // 10^15
  constexpr int64_t kMsMaxInstant = 8.64e15;
  int64_t time_ms = static_cast<int64_t>(icu_transition.getTime());
  if (time_ms < kMsMinInstant || time_ms > kMsMaxInstant) {
    return isolate->factory()->null_value();
  }
  return MillisecondToNanosecond(isolate, time_ms);
}

std::vector<Handle<BigInt>> Intl::GetTimeZonePossibleOffsetNanoseconds(
    Isolate* isolate, int32_t time_zone_index,
    Handle<BigInt> nanosecond_epoch) {
  std::unique_ptr<const icu::BasicTimeZone> basic_time_zone(
      CreateBasicTimeZoneFromIndex(time_zone_index));
  int64_t time_ms = ApproximateMillisecondEpoch(isolate, nanosecond_epoch);
  int32_t raw_offset;
  int32_t dst_offset;
  UErrorCode status = U_ZERO_ERROR;
  basic_time_zone->getOffsetFromLocal(time_ms, UCAL_TZ_LOCAL_FORMER,
                                      UCAL_TZ_LOCAL_FORMER, raw_offset,
                                      dst_offset, status);
  DCHECK(U_SUCCESS(status));
  // offset for time_ms interpretted as before a time zone
  // transition
  int64_t offset_former = raw_offset + dst_offset;

  basic_time_zone->getOffsetFromLocal(time_ms, UCAL_TZ_LOCAL_LATTER,
                                      UCAL_TZ_LOCAL_LATTER, raw_offset,
                                      dst_offset, status);
  DCHECK(U_SUCCESS(status));
  // offset for time_ms interpretted as after a time zone
  // transition
  int64_t offset_latter = raw_offset + dst_offset;

  std::vector<Handle<BigInt>> result;
  if (offset_former == offset_latter) {
    // For most of the time, when either interpretation are the same, we are not
    // in a moment of offset transition based on rule changing: Just return that
    // value.
    result.push_back(MillisecondToNanosecond(isolate, offset_former));
  } else if (offset_former > offset_latter) {
    // When the input represents a local time repeating multiple times at a
    // negative time zone transition (e.g. when the daylight saving time ends
    // or the time zone offset is decreased due to a time zone rule change).
    result.push_back(MillisecondToNanosecond(isolate, offset_former));
    result.push_back(MillisecondToNanosecond(isolate, offset_latter));
  } else {
    // If the offset after the transition is greater than the offset before the
    // transition, that mean it is in the moment the time "skip" an hour, or two
    // (or six in a Time Zone in south pole) in that case there are no possible
    // Time Zone offset for that moment and nothing will be added to the result.
  }
  return result;
}

int64_t Intl::GetTimeZoneOffsetNanoseconds(Isolate* isolate,
                                           int32_t time_zone_index,
                                           Handle<BigInt> nanosecond_epoch) {
  std::unique_ptr<const icu::BasicTimeZone> basic_time_zone(
      CreateBasicTimeZoneFromIndex(time_zone_index));
  int64_t time_ms = ApproximateMillisecondEpoch(isolate, nanosecond_epoch);
  int32_t raw_offset;
  int32_t dst_offset;
  UErrorCode status = U_ZERO_ERROR;
  basic_time_zone->getOffset(time_ms, false, raw_offset, dst_offset, status);
  DCHECK(U_SUCCESS(status));
  // Turn ms into ns
  return static_cast<int64_t>(raw_offset + dst_offset) * 1000000;
}

}  // namespace v8::internal
