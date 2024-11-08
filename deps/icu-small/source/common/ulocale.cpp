// © 2023 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
//
#include "unicode/bytestream.h"
#include "unicode/errorcode.h"
#include "unicode/stringpiece.h"
#include "unicode/utypes.h"
#include "unicode/ustring.h"
#include "unicode/ulocale.h"
#include "unicode/locid.h"

#include "bytesinkutil.h"
#include "charstr.h"
#include "cmemory.h"

U_NAMESPACE_USE
#define EXTERNAL(i) (reinterpret_cast<ULocale*>(i))
#define CONST_INTERNAL(e) (reinterpret_cast<const icu::Locale*>(e))
#define INTERNAL(e) (reinterpret_cast<icu::Locale*>(e))

ULocale*
ulocale_openForLocaleID(const char* localeID, int32_t length, UErrorCode* err) {
    if (U_FAILURE(*err)) { return nullptr; }
    if (length < 0) {
        return EXTERNAL(icu::Locale::createFromName(localeID).clone());
    }
    CharString str(localeID, length, *err);  // Make a NUL terminated copy.
    if (U_FAILURE(*err)) { return nullptr; }
    return EXTERNAL(icu::Locale::createFromName(str.data()).clone());
}

ULocale*
ulocale_openForLanguageTag(const char* tag, int32_t length, UErrorCode* err) {
  if (U_FAILURE(*err)) { return nullptr; }
  Locale l = icu::Locale::forLanguageTag(length < 0 ? StringPiece(tag) : StringPiece(tag, length), *err);
  if (U_FAILURE(*err)) { return nullptr; }
  return EXTERNAL(l.clone());
}

void
ulocale_close(ULocale* locale) {
    delete INTERNAL(locale);
}

#define IMPL_ULOCALE_STRING_GETTER(N1, N2) \
const char* ulocale_get ## N1(const ULocale* locale) { \
    if (locale == nullptr) return nullptr; \
    return CONST_INTERNAL(locale)->get ## N2(); \
}

#define IMPL_ULOCALE_STRING_IDENTICAL_GETTER(N) IMPL_ULOCALE_STRING_GETTER(N, N)

#define IMPL_ULOCALE_GET_KEYWORD_VALUE(N) \
int32_t ulocale_get ##N ( \
    const ULocale* locale, const char* keyword, int32_t keywordLength, \
    char* valueBuffer, int32_t bufferCapacity, UErrorCode *err) { \
    if (U_FAILURE(*err)) return 0; \
    if (locale == nullptr) { \
        *err = U_ILLEGAL_ARGUMENT_ERROR; \
        return 0; \
    } \
    return ByteSinkUtil::viaByteSinkToTerminatedChars( \
        valueBuffer, bufferCapacity, \
        [&](ByteSink& sink, UErrorCode& status) { \
            CONST_INTERNAL(locale)->get ## N( \
                keywordLength < 0 ? StringPiece(keyword) : StringPiece(keyword, keywordLength), \
                sink, status); \
        }, \
        *err); \
}

#define IMPL_ULOCALE_GET_KEYWORDS(N) \
UEnumeration* ulocale_get ## N(const ULocale* locale, UErrorCode *err) { \
    if (U_FAILURE(*err)) return nullptr; \
    if (locale == nullptr) { \
        *err = U_ILLEGAL_ARGUMENT_ERROR; \
        return nullptr; \
    } \
    return uenum_openFromStringEnumeration( \
        CONST_INTERNAL(locale)->create ## N(*err), err); \
}

IMPL_ULOCALE_STRING_IDENTICAL_GETTER(Language)
IMPL_ULOCALE_STRING_IDENTICAL_GETTER(Script)
IMPL_ULOCALE_STRING_GETTER(Region, Country)
IMPL_ULOCALE_STRING_IDENTICAL_GETTER(Variant)
IMPL_ULOCALE_STRING_GETTER(LocaleID, Name)
IMPL_ULOCALE_STRING_IDENTICAL_GETTER(BaseName)
IMPL_ULOCALE_GET_KEYWORD_VALUE(KeywordValue)
IMPL_ULOCALE_GET_KEYWORD_VALUE(UnicodeKeywordValue)
IMPL_ULOCALE_GET_KEYWORDS(Keywords)
IMPL_ULOCALE_GET_KEYWORDS(UnicodeKeywords)

bool ulocale_isBogus(const ULocale* locale) {
    if (locale == nullptr) return false;
    return CONST_INTERNAL(locale)->isBogus();
}

/*eof*/
