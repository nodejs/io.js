// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1997-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  loclikely.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2010feb25
*   created by: Markus W. Scherer
*
*   Code for likely and minimized locale subtags, separated out from other .cpp files
*   that then do not depend on resource bundle code and likely-subtags data.
*/

#include <utility>

#include "unicode/bytestream.h"
#include "unicode/utypes.h"
#include "unicode/locid.h"
#include "unicode/putil.h"
#include "unicode/uchar.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"
#include "unicode/uscript.h"
#include "bytesinkutil.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "loclikelysubtags.h"
#include "ulocimp.h"

namespace {

/**
 * Create a tag string from the supplied parameters.  The lang, script and region
 * parameters may be nullptr pointers. If they are, their corresponding length parameters
 * must be less than or equal to 0.
 *
 * If an illegal argument is provided, the function returns the error
 * U_ILLEGAL_ARGUMENT_ERROR.
 *
 * @param lang The language tag to use.
 * @param langLength The length of the language tag.
 * @param script The script tag to use.
 * @param scriptLength The length of the script tag.
 * @param region The region tag to use.
 * @param regionLength The length of the region tag.
 * @param variant The region tag to use.
 * @param variantLength The length of the region tag.
 * @param trailing Any trailing data to append to the new tag.
 * @param trailingLength The length of the trailing data.
 * @param sink The output sink receiving the tag string.
 * @param err A pointer to a UErrorCode for error reporting.
 **/
void U_CALLCONV
createTagStringWithAlternates(
    const char* lang,
    int32_t langLength,
    const char* script,
    int32_t scriptLength,
    const char* region,
    int32_t regionLength,
    const char* variant,
    int32_t variantLength,
    const char* trailing,
    int32_t trailingLength,
    icu::ByteSink& sink,
    UErrorCode& err) {
    if (U_FAILURE(err)) {
        return;
    }

    if (langLength >= ULOC_LANG_CAPACITY ||
            scriptLength >= ULOC_SCRIPT_CAPACITY ||
            regionLength >= ULOC_COUNTRY_CAPACITY) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (langLength > 0) {
        sink.Append(lang, langLength);
    }

    if (scriptLength > 0) {
        sink.Append("_", 1);
        sink.Append(script, scriptLength);
    }

    if (regionLength > 0) {
        sink.Append("_", 1);
        sink.Append(region, regionLength);
    }

    if (variantLength > 0) {
        if (regionLength == 0) {
            /* extra separator is required */
            sink.Append("_", 1);
        }
        sink.Append("_", 1);
        sink.Append(variant, variantLength);
    }

    if (trailingLength > 0) {
        /*
         * Copy the trailing data into the supplied buffer.
         */
        sink.Append(trailing, trailingLength);
    }
}

bool CHECK_TRAILING_VARIANT_SIZE(const char* variant, int32_t variantLength) {
    int32_t count = 0;
    for (int32_t i = 0; i < variantLength; i++) {
        if (_isIDSeparator(variant[i])) {
            count = 0;
        } else if (count == 8) {
            return false;
        } else {
            count++;
        }
    }
    return true;
}

void
_uloc_addLikelySubtags(const char* localeID,
                       icu::ByteSink& sink,
                       UErrorCode& err) {
    if (U_FAILURE(err)) {
        return;
    }

    if (localeID == nullptr) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    icu::CharString lang;
    icu::CharString script;
    icu::CharString region;
    icu::CharString variant;
    const char* trailing = nullptr;
    ulocimp_getSubtags(localeID, &lang, &script, &region, &variant, &trailing, err);
    if (U_FAILURE(err)) {
        return;
    }

    if (!CHECK_TRAILING_VARIANT_SIZE(variant.data(), variant.length())) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    if (lang.length() == 4) {
        if (script.isEmpty()) {
            script = std::move(lang);
            lang.clear();
        } else {
            err = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
    } else if (lang.length() > 8) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    int32_t trailingLength = (int32_t)uprv_strlen(trailing);

    const icu::LikelySubtags* likelySubtags = icu::LikelySubtags::getSingleton(err);
    if (U_FAILURE(err)) {
        return;
    }
    // We need to keep l on the stack because lsr may point into internal
    // memory of l.
    icu::Locale l = icu::Locale::createFromName(localeID);
    if (l.isBogus()) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    icu::LSR lsr = likelySubtags->makeMaximizedLsrFrom(l, true, err);
    if (U_FAILURE(err)) {
        return;
    }
    const char* language = lsr.language;
    if (uprv_strcmp(language, "und") == 0) {
        language = "";
    }
    createTagStringWithAlternates(
        language,
        (int32_t)uprv_strlen(language),
        lsr.script,
        (int32_t)uprv_strlen(lsr.script),
        lsr.region,
        (int32_t)uprv_strlen(lsr.region),
        variant.data(),
        variant.length(),
        trailing,
        trailingLength,
        sink,
        err);
}

void
_uloc_minimizeSubtags(const char* localeID,
                      icu::ByteSink& sink,
                      bool favorScript,
                      UErrorCode& err) {
    if (U_FAILURE(err)) {
        return;
    }

    if (localeID == nullptr) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    icu::CharString lang;
    icu::CharString script;
    icu::CharString region;
    icu::CharString variant;
    const char* trailing = nullptr;
    ulocimp_getSubtags(localeID, &lang, &script, &region, &variant, &trailing, err);
    if (U_FAILURE(err)) {
        return;
    }

    if (!CHECK_TRAILING_VARIANT_SIZE(variant.data(), variant.length())) {
        err = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }

    int32_t trailingLength = (int32_t)uprv_strlen(trailing);

    const icu::LikelySubtags* likelySubtags = icu::LikelySubtags::getSingleton(err);
    if (U_FAILURE(err)) {
        return;
    }
    icu::LSR lsr = likelySubtags->minimizeSubtags(
        lang.toStringPiece(),
        script.toStringPiece(),
        region.toStringPiece(),
        favorScript,
        err);
    if (U_FAILURE(err)) {
        return;
    }
    const char* language = lsr.language;
    if (uprv_strcmp(language, "und") == 0) {
        language = "";
    }
    createTagStringWithAlternates(
        language,
        (int32_t)uprv_strlen(language),
        lsr.script,
        (int32_t)uprv_strlen(lsr.script),
        lsr.region,
        (int32_t)uprv_strlen(lsr.region),
        variant.data(),
        variant.length(),
        trailing,
        trailingLength,
        sink,
        err);
}

}  // namespace

U_CAPI int32_t U_EXPORT2
uloc_addLikelySubtags(const char* localeID,
                      char* maximizedLocaleID,
                      int32_t maximizedLocaleIDCapacity,
                      UErrorCode* status) {
    return icu::ByteSinkUtil::viaByteSinkToTerminatedChars(
        maximizedLocaleID, maximizedLocaleIDCapacity,
        [&](icu::ByteSink& sink, UErrorCode& status) {
            ulocimp_addLikelySubtags(localeID, sink, status);
        },
        *status);
}

U_EXPORT icu::CharString
ulocimp_addLikelySubtags(const char* localeID,
                         UErrorCode& status) {
    return icu::ByteSinkUtil::viaByteSinkToCharString(
        [&](icu::ByteSink& sink, UErrorCode& status) {
            ulocimp_addLikelySubtags(localeID, sink, status);
        },
        status);
}

U_EXPORT void
ulocimp_addLikelySubtags(const char* localeID,
                         icu::ByteSink& sink,
                         UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    icu::CharString localeBuffer = ulocimp_canonicalize(localeID, status);
    _uloc_addLikelySubtags(localeBuffer.data(), sink, status);
}

U_CAPI int32_t U_EXPORT2
uloc_minimizeSubtags(const char* localeID,
                     char* minimizedLocaleID,
                     int32_t minimizedLocaleIDCapacity,
                     UErrorCode* status) {
    return icu::ByteSinkUtil::viaByteSinkToTerminatedChars(
        minimizedLocaleID, minimizedLocaleIDCapacity,
        [&](icu::ByteSink& sink, UErrorCode& status) {
            ulocimp_minimizeSubtags(localeID, sink, false, status);
        },
        *status);
}

U_EXPORT icu::CharString
ulocimp_minimizeSubtags(const char* localeID,
                        bool favorScript,
                        UErrorCode& status) {
    return icu::ByteSinkUtil::viaByteSinkToCharString(
        [&](icu::ByteSink& sink, UErrorCode& status) {
            ulocimp_minimizeSubtags(localeID, sink, favorScript, status);
        },
        status);
}

U_EXPORT void
ulocimp_minimizeSubtags(const char* localeID,
                        icu::ByteSink& sink,
                        bool favorScript,
                        UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    icu::CharString localeBuffer = ulocimp_canonicalize(localeID, status);
    _uloc_minimizeSubtags(localeBuffer.data(), sink, favorScript, status);
}

// Pairs of (language subtag, + or -) for finding out fast if common languages
// are LTR (minus) or RTL (plus).
static const char LANG_DIR_STRING[] =
        "root-en-es-pt-zh-ja-ko-de-fr-it-ar+he+fa+ru-nl-pl-th-tr-";

// Implemented here because this calls ulocimp_addLikelySubtags().
U_CAPI UBool U_EXPORT2
uloc_isRightToLeft(const char *locale) {
    UErrorCode errorCode = U_ZERO_ERROR;
    icu::CharString lang;
    icu::CharString script;
    ulocimp_getSubtags(locale, &lang, &script, nullptr, nullptr, nullptr, errorCode);
    if (U_FAILURE(errorCode) || script.isEmpty()) {
        // Fastpath: We know the likely scripts and their writing direction
        // for some common languages.
        if (!lang.isEmpty()) {
            const char* langPtr = uprv_strstr(LANG_DIR_STRING, lang.data());
            if (langPtr != nullptr) {
                switch (langPtr[lang.length()]) {
                case '-': return false;
                case '+': return true;
                default: break;  // partial match of a longer code
                }
            }
        }
        // Otherwise, find the likely script.
        errorCode = U_ZERO_ERROR;
        icu::CharString likely = ulocimp_addLikelySubtags(locale, errorCode);
        if (U_FAILURE(errorCode)) {
            return false;
        }
        ulocimp_getSubtags(likely.data(), nullptr, &script, nullptr, nullptr, nullptr, errorCode);
        if (U_FAILURE(errorCode) || script.isEmpty()) {
            return false;
        }
    }
    UScriptCode scriptCode = (UScriptCode)u_getPropertyValueEnum(UCHAR_SCRIPT, script.data());
    return uscript_isRightToLeft(scriptCode);
}

U_NAMESPACE_BEGIN

UBool
Locale::isRightToLeft() const {
    return uloc_isRightToLeft(getBaseName());
}

U_NAMESPACE_END

namespace {
icu::CharString
GetRegionFromKey(const char* localeID, const char* key, UErrorCode& status) {
    icu::CharString result;

    // First check for keyword value
    icu::CharString kw = ulocimp_getKeywordValue(localeID, key, status);
    int32_t len = kw.length();
    if (U_SUCCESS(status) && len >= 3 && len <= 7) {
        // chop off the subdivision code (which will generally be "zzzz" anyway)
        const char* const data = kw.data();
        if (uprv_isASCIILetter(data[0])) {
            result.append(uprv_toupper(data[0]), status);
            result.append(uprv_toupper(data[1]), status);
        } else {
            // assume three-digit region code
            result.append(data, 3, status);
        }
    }
    return result;
}
}  // namespace

U_EXPORT icu::CharString
ulocimp_getRegionForSupplementalData(const char *localeID, bool inferRegion,
                                     UErrorCode& status) {
    if (U_FAILURE(status)) {
        return {};
    }
    icu::CharString rgBuf = GetRegionFromKey(localeID, "rg", status);
    if (U_SUCCESS(status) && rgBuf.isEmpty()) {
        // No valid rg keyword value, try for unicode_region_subtag
        rgBuf = ulocimp_getRegion(localeID, status);
        if (U_SUCCESS(status) && rgBuf.isEmpty() && inferRegion) {
            // Second check for sd keyword value
            rgBuf = GetRegionFromKey(localeID, "sd", status);
            if (U_SUCCESS(status) && rgBuf.isEmpty()) {
                // no unicode_region_subtag but inferRegion true, try likely subtags
                UErrorCode rgStatus = U_ZERO_ERROR;
                icu::CharString locBuf = ulocimp_addLikelySubtags(localeID, rgStatus);
                if (U_SUCCESS(rgStatus)) {
                    rgBuf = ulocimp_getRegion(locBuf.data(), status);
                }
            }
        }
    }

    return rgBuf;
}
