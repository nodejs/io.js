// © 2023 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include <utility>

#include "unicode/bytestream.h"
#include "unicode/localebuilder.h"
#include "unicode/locid.h"
#include "unicode/stringpiece.h"
#include "unicode/umachine.h"
#include "unicode/ulocbuilder.h"
#include "bytesinkutil.h"
#include "cstring.h"
#include "ustr_imp.h"

using icu::StringPiece;

#define EXTERNAL(i) (reinterpret_cast<ULocaleBuilder*>(i))
#define INTERNAL(e) (reinterpret_cast<icu::LocaleBuilder*>(e))
#define CONST_INTERNAL(e) (reinterpret_cast<const icu::LocaleBuilder*>(e))

ULocaleBuilder* ulocbld_open() {
    return EXTERNAL(new icu::LocaleBuilder());
}

void ulocbld_close(ULocaleBuilder* builder) {
    if (builder == nullptr) return;
    delete INTERNAL(builder);
}

void ulocbld_setLocale(ULocaleBuilder* builder, const char* locale, int32_t length) {
    if (builder == nullptr) return;
    icu::Locale l;
    if (length < 0 || locale[length] == '\0') {
        l = icu::Locale(locale);
    } else {
        if (length >= ULOC_FULLNAME_CAPACITY) {
            l.setToBogus();
        } else {
            // locale is not null termined but Locale API require one.
            // Create a null termined version in buf.
            char buf[ULOC_FULLNAME_CAPACITY];
            uprv_memcpy(buf, locale, length);
            buf[length] = '\0';
            l = icu::Locale(buf);
        }
    }
    INTERNAL(builder)->setLocale(l);
}

void
ulocbld_adoptULocale(ULocaleBuilder* builder, ULocale* locale) {
    if (builder == nullptr) return;
    INTERNAL(builder)->setLocale(*(reinterpret_cast<const icu::Locale*>(locale)));
    ulocale_close(locale);
}

#define STRING_PIECE(s, l) ((l)<0 ? StringPiece(s) : StringPiece((s), (l)))

#define IMPL_ULOCBLD_SETTER(N) \
void ulocbld_##N(ULocaleBuilder* bld, const char* s, int32_t l) { \
    if (bld == nullptr) return; \
    INTERNAL(bld)->N(STRING_PIECE(s,l)); \
}

IMPL_ULOCBLD_SETTER(setLanguageTag)
IMPL_ULOCBLD_SETTER(setLanguage)
IMPL_ULOCBLD_SETTER(setScript)
IMPL_ULOCBLD_SETTER(setRegion)
IMPL_ULOCBLD_SETTER(setVariant)
IMPL_ULOCBLD_SETTER(addUnicodeLocaleAttribute)
IMPL_ULOCBLD_SETTER(removeUnicodeLocaleAttribute)

void ulocbld_setExtension(ULocaleBuilder* builder, char key, const char* value, int32_t length) {
    if (builder == nullptr) return;
    INTERNAL(builder)->setExtension(key, STRING_PIECE(value, length));
}

void ulocbld_setUnicodeLocaleKeyword(
    ULocaleBuilder* builder, const char* key, int32_t keyLength,
    const char* type, int32_t typeLength) {
    if (builder == nullptr) return;
    INTERNAL(builder)->setUnicodeLocaleKeyword(
        STRING_PIECE(key, keyLength), STRING_PIECE(type, typeLength));
}

void ulocbld_clear(ULocaleBuilder* builder) {
    if (builder == nullptr) return;
    INTERNAL(builder)->clear();
}

void ulocbld_clearExtensions(ULocaleBuilder* builder) {
    if (builder == nullptr) return;
    INTERNAL(builder)->clearExtensions();
}


ULocale* ulocbld_buildULocale(ULocaleBuilder* builder, UErrorCode* err) {
    if (builder == nullptr) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    icu::Locale l = INTERNAL(builder)->build(*err);
    if (U_FAILURE(*err)) return nullptr;
    icu::Locale* r = l.clone();
    if (r == nullptr) {
        *err = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return reinterpret_cast<ULocale*>(r);
}

int32_t ulocbld_buildLocaleID(ULocaleBuilder* builder,
                              char* buffer, int32_t bufferCapacity, UErrorCode* err) {
    if (U_FAILURE(*err)) { return 0; }
    if (builder == nullptr) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    icu::Locale l = INTERNAL(builder)->build(*err);
    if (U_FAILURE(*err)) { return 0; }
    int32_t length = static_cast<int32_t>(uprv_strlen(l.getName()));
    if (0 < length && length <= bufferCapacity) {
        uprv_memcpy(buffer, l.getName(), length);
    }
    return u_terminateChars(buffer, bufferCapacity, length, err);
}

int32_t ulocbld_buildLanguageTag(ULocaleBuilder* builder,
                  char* buffer, int32_t bufferCapacity, UErrorCode* err) {
    if (U_FAILURE(*err)) { return 0; }
    if (builder == nullptr) {
        *err = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    icu::Locale l = INTERNAL(builder)->build(*err);
    return icu::ByteSinkUtil::viaByteSinkToTerminatedChars(
        buffer, bufferCapacity,
        [&](icu::ByteSink& sink, UErrorCode& status) {
            l.toLanguageTag(sink, status);
        },
        *err);
}

UBool ulocbld_copyErrorTo(const ULocaleBuilder* builder, UErrorCode *outErrorCode) {
    if (builder == nullptr) {
        *outErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return true;
    }
    return CONST_INTERNAL(builder)->copyErrorTo(*outErrorCode);
}
