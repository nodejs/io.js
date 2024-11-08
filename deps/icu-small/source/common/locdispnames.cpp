// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 1997-2016, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  locdispnames.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2010feb25
*   created by: Markus W. Scherer
*
*   Code for locale display names, separated out from other .cpp files
*   that then do not depend on resource bundle code and display name data.
*/

#include "unicode/utypes.h"
#include "unicode/brkiter.h"
#include "unicode/locid.h"
#include "unicode/uenum.h"
#include "unicode/uloc.h"
#include "unicode/ures.h"
#include "unicode/ustring.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "putilimp.h"
#include "ulocimp.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "ustr_imp.h"

// C++ API ----------------------------------------------------------------- ***

U_NAMESPACE_BEGIN

UnicodeString&
Locale::getDisplayLanguage(UnicodeString& dispLang) const
{
    return this->getDisplayLanguage(getDefault(), dispLang);
}

/*We cannot make any assumptions on the size of the output display strings
* Yet, since we are calling through to a C API, we need to set limits on
* buffer size. For all the following getDisplay functions we first attempt
* to fill up a stack allocated buffer. If it is to small we heap allocated
* the exact buffer we need copy it to the UnicodeString and delete it*/

UnicodeString&
Locale::getDisplayLanguage(const Locale &displayLocale,
                           UnicodeString &result) const {
    char16_t *buffer;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length;

    buffer=result.getBuffer(ULOC_FULLNAME_CAPACITY);
    if (buffer == nullptr) {
        result.truncate(0);
        return result;
    }

    length=uloc_getDisplayLanguage(fullName, displayLocale.fullName,
                                   buffer, result.getCapacity(),
                                   &errorCode);
    result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);

    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        buffer=result.getBuffer(length);
        if (buffer == nullptr) {
            result.truncate(0);
            return result;
        }
        errorCode=U_ZERO_ERROR;
        length=uloc_getDisplayLanguage(fullName, displayLocale.fullName,
                                       buffer, result.getCapacity(),
                                       &errorCode);
        result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);
    }

    return result;
}

UnicodeString&
Locale::getDisplayScript(UnicodeString& dispScript) const
{
    return this->getDisplayScript(getDefault(), dispScript);
}

UnicodeString&
Locale::getDisplayScript(const Locale &displayLocale,
                          UnicodeString &result) const {
    char16_t *buffer;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length;

    buffer=result.getBuffer(ULOC_FULLNAME_CAPACITY);
    if (buffer == nullptr) {
        result.truncate(0);
        return result;
    }

    length=uloc_getDisplayScript(fullName, displayLocale.fullName,
                                  buffer, result.getCapacity(),
                                  &errorCode);
    result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);

    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        buffer=result.getBuffer(length);
        if (buffer == nullptr) {
            result.truncate(0);
            return result;
        }
        errorCode=U_ZERO_ERROR;
        length=uloc_getDisplayScript(fullName, displayLocale.fullName,
                                      buffer, result.getCapacity(),
                                      &errorCode);
        result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);
    }

    return result;
}

UnicodeString&
Locale::getDisplayCountry(UnicodeString& dispCntry) const
{
    return this->getDisplayCountry(getDefault(), dispCntry);
}

UnicodeString&
Locale::getDisplayCountry(const Locale &displayLocale,
                          UnicodeString &result) const {
    char16_t *buffer;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length;

    buffer=result.getBuffer(ULOC_FULLNAME_CAPACITY);
    if (buffer == nullptr) {
        result.truncate(0);
        return result;
    }

    length=uloc_getDisplayCountry(fullName, displayLocale.fullName,
                                  buffer, result.getCapacity(),
                                  &errorCode);
    result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);

    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        buffer=result.getBuffer(length);
        if (buffer == nullptr) {
            result.truncate(0);
            return result;
        }
        errorCode=U_ZERO_ERROR;
        length=uloc_getDisplayCountry(fullName, displayLocale.fullName,
                                      buffer, result.getCapacity(),
                                      &errorCode);
        result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);
    }

    return result;
}

UnicodeString&
Locale::getDisplayVariant(UnicodeString& dispVar) const
{
    return this->getDisplayVariant(getDefault(), dispVar);
}

UnicodeString&
Locale::getDisplayVariant(const Locale &displayLocale,
                          UnicodeString &result) const {
    char16_t *buffer;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length;

    buffer=result.getBuffer(ULOC_FULLNAME_CAPACITY);
    if (buffer == nullptr) {
        result.truncate(0);
        return result;
    }

    length=uloc_getDisplayVariant(fullName, displayLocale.fullName,
                                  buffer, result.getCapacity(),
                                  &errorCode);
    result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);

    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        buffer=result.getBuffer(length);
        if (buffer == nullptr) {
            result.truncate(0);
            return result;
        }
        errorCode=U_ZERO_ERROR;
        length=uloc_getDisplayVariant(fullName, displayLocale.fullName,
                                      buffer, result.getCapacity(),
                                      &errorCode);
        result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);
    }

    return result;
}

UnicodeString&
Locale::getDisplayName( UnicodeString& name ) const
{
    return this->getDisplayName(getDefault(), name);
}

UnicodeString&
Locale::getDisplayName(const Locale &displayLocale,
                       UnicodeString &result) const {
    char16_t *buffer;
    UErrorCode errorCode=U_ZERO_ERROR;
    int32_t length;

    buffer=result.getBuffer(ULOC_FULLNAME_CAPACITY);
    if (buffer == nullptr) {
        result.truncate(0);
        return result;
    }

    length=uloc_getDisplayName(fullName, displayLocale.fullName,
                               buffer, result.getCapacity(),
                               &errorCode);
    result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);

    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        buffer=result.getBuffer(length);
        if (buffer == nullptr) {
            result.truncate(0);
            return result;
        }
        errorCode=U_ZERO_ERROR;
        length=uloc_getDisplayName(fullName, displayLocale.fullName,
                                   buffer, result.getCapacity(),
                                   &errorCode);
        result.releaseBuffer(U_SUCCESS(errorCode) ? length : 0);
    }

    return result;
}

#if !UCONFIG_NO_BREAK_ITERATION

// -------------------------------------
// Gets the objectLocale display name in the default locale language.
UnicodeString& U_EXPORT2
BreakIterator::getDisplayName(const Locale& objectLocale,
                             UnicodeString& name)
{
    return objectLocale.getDisplayName(name);
}

// -------------------------------------
// Gets the objectLocale display name in the displayLocale language.
UnicodeString& U_EXPORT2
BreakIterator::getDisplayName(const Locale& objectLocale,
                             const Locale& displayLocale,
                             UnicodeString& name)
{
    return objectLocale.getDisplayName(displayLocale, name);
}

#endif


U_NAMESPACE_END

// C API ------------------------------------------------------------------- ***

U_NAMESPACE_USE

namespace {

/* ### Constants **************************************************/

/* These strings describe the resources we attempt to load from
 the locale ResourceBundle data file.*/
constexpr char _kLanguages[]       = "Languages";
constexpr char _kScripts[]         = "Scripts";
constexpr char _kScriptsStandAlone[] = "Scripts%stand-alone";
constexpr char _kCountries[]       = "Countries";
constexpr char _kVariants[]        = "Variants";
constexpr char _kKeys[]            = "Keys";
constexpr char _kTypes[]           = "Types";
//constexpr char _kRootName[]        = "root";
constexpr char _kCurrency[]        = "currency";
constexpr char _kCurrencies[]      = "Currencies";
constexpr char _kLocaleDisplayPattern[] = "localeDisplayPattern";
constexpr char _kPattern[]         = "pattern";
constexpr char _kSeparator[]       = "separator";

/* ### Display name **************************************************/

int32_t
_getStringOrCopyKey(const char *path, const char *locale,
                    const char *tableKey, 
                    const char* subTableKey,
                    const char *itemKey,
                    const char *substitute,
                    char16_t *dest, int32_t destCapacity,
                    UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return 0; }
    const char16_t *s = nullptr;
    int32_t length = 0;

    if(itemKey==nullptr) {
        /* top-level item: normal resource bundle access */
        icu::LocalUResourceBundlePointer rb(ures_open(path, locale, &errorCode));

        if(U_SUCCESS(errorCode)) {
            s=ures_getStringByKey(rb.getAlias(), tableKey, &length, &errorCode);
            /* see comment about closing rb near "return item;" in _res_getTableStringWithFallback() */
        }
    } else {
        bool isLanguageCode = (uprv_strncmp(tableKey, _kLanguages, 9) == 0);
        /* Language code should not be a number. If it is, set the error code. */
        if (isLanguageCode && uprv_strtol(itemKey, nullptr, 10)) {
            errorCode = U_MISSING_RESOURCE_ERROR;
        } else {
            /* second-level item, use special fallback */
            s=uloc_getTableStringWithFallback(path, locale,
                                               tableKey,
                                               subTableKey,
                                               itemKey,
                                               &length,
                                               &errorCode);
            if (U_FAILURE(errorCode) && isLanguageCode && itemKey != nullptr) {
                // convert itemKey locale code to canonical form and try again, ICU-20870
                errorCode = U_ZERO_ERROR;
                Locale canonKey = Locale::createCanonical(itemKey);
                s=uloc_getTableStringWithFallback(path, locale,
                                                    tableKey,
                                                    subTableKey,
                                                    canonKey.getName(),
                                                    &length,
                                                    &errorCode);
            }
        }
    }

    if(U_SUCCESS(errorCode)) {
        int32_t copyLength=uprv_min(length, destCapacity);
        if(copyLength>0 && s != nullptr) {
            u_memcpy(dest, s, copyLength);
        }
    } else {
        /* no string from a resource bundle: convert the substitute */
        length = static_cast<int32_t>(uprv_strlen(substitute));
        u_charsToUChars(substitute, dest, uprv_min(length, destCapacity));
        errorCode = U_USING_DEFAULT_WARNING;
    }

    return u_terminateUChars(dest, destCapacity, length, &errorCode);
}

using UDisplayNameGetter = icu::CharString(const char*, UErrorCode&);

int32_t
_getDisplayNameForComponent(const char *locale,
                            const char *displayLocale,
                            char16_t *dest, int32_t destCapacity,
                            UDisplayNameGetter *getter,
                            const char *tag,
                            UErrorCode &errorCode) {
    if (U_FAILURE(errorCode)) { return 0; }
    UErrorCode localStatus;
    const char* root = nullptr;

    if(destCapacity<0 || (destCapacity>0 && dest==nullptr)) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    localStatus = U_ZERO_ERROR;
    icu::CharString localeBuffer = (*getter)(locale, localStatus);
    if (U_FAILURE(localStatus)) {
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    if (localeBuffer.isEmpty()) {
        // For the display name, we treat this as unknown language (ICU-20273).
        if (getter == ulocimp_getLanguage) {
            localeBuffer.append("und", errorCode);
        } else {
            return u_terminateUChars(dest, destCapacity, 0, &errorCode);
        }
    }

    root = tag == _kCountries ? U_ICUDATA_REGION : U_ICUDATA_LANG;

    return _getStringOrCopyKey(root, displayLocale,
                               tag, nullptr, localeBuffer.data(),
                               localeBuffer.data(),
                               dest, destCapacity,
                               errorCode);
}

}  // namespace

U_CAPI int32_t U_EXPORT2
uloc_getDisplayLanguage(const char *locale,
                        const char *displayLocale,
                        char16_t *dest, int32_t destCapacity,
                        UErrorCode *pErrorCode) {
    return _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                ulocimp_getLanguage, _kLanguages, *pErrorCode);
}

U_CAPI int32_t U_EXPORT2
uloc_getDisplayScript(const char* locale,
                      const char* displayLocale,
                      char16_t *dest, int32_t destCapacity,
                      UErrorCode *pErrorCode)
{
    if (U_FAILURE(*pErrorCode)) { return 0; }
    UErrorCode err = U_ZERO_ERROR;
    int32_t res = _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                ulocimp_getScript, _kScriptsStandAlone, err);

    if (destCapacity == 0 && err == U_BUFFER_OVERFLOW_ERROR) {
        // For preflight, return the max of the value and the fallback.
        int32_t fallback_res = _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                                                           ulocimp_getScript, _kScripts, *pErrorCode);
        return (fallback_res > res) ? fallback_res : res;
    }
    if ( err == U_USING_DEFAULT_WARNING ) {
        return _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                                           ulocimp_getScript, _kScripts, *pErrorCode);
    } else {
        *pErrorCode = err;
        return res;
    }
}

static int32_t
uloc_getDisplayScriptInContext(const char* locale,
                      const char* displayLocale,
                      char16_t *dest, int32_t destCapacity,
                      UErrorCode *pErrorCode)
{
    return _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                    ulocimp_getScript, _kScripts, *pErrorCode);
}

U_CAPI int32_t U_EXPORT2
uloc_getDisplayCountry(const char *locale,
                       const char *displayLocale,
                       char16_t *dest, int32_t destCapacity,
                       UErrorCode *pErrorCode) {
    return _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                ulocimp_getRegion, _kCountries, *pErrorCode);
}

/*
 * TODO separate variant1_variant2_variant3...
 * by getting each tag's display string and concatenating them with ", "
 * in between - similar to uloc_getDisplayName()
 */
U_CAPI int32_t U_EXPORT2
uloc_getDisplayVariant(const char *locale,
                       const char *displayLocale,
                       char16_t *dest, int32_t destCapacity,
                       UErrorCode *pErrorCode) {
    return _getDisplayNameForComponent(locale, displayLocale, dest, destCapacity,
                ulocimp_getVariant, _kVariants, *pErrorCode);
}

/* Instead of having a separate pass for 'special' patterns, reintegrate the two
 * so we don't get bitten by preflight bugs again.  We can be reasonably efficient
 * without two separate code paths, this code isn't that performance-critical.
 *
 * This code is general enough to deal with patterns that have a prefix or swap the
 * language and remainder components, since we gave developers enough rope to do such
 * things if they futz with the pattern data.  But since we don't give them a way to
 * specify a pattern for arbitrary combinations of components, there's not much use in
 * that.  I don't think our data includes such patterns, the only variable I know if is
 * whether there is a space before the open paren, or not.  Oh, and zh uses different
 * chars than the standard open/close paren (which ja and ko use, btw).
 */
U_CAPI int32_t U_EXPORT2
uloc_getDisplayName(const char *locale,
                    const char *displayLocale,
                    char16_t *dest, int32_t destCapacity,
                    UErrorCode *pErrorCode)
{
    static const char16_t defaultSeparator[9] = { 0x007b, 0x0030, 0x007d, 0x002c, 0x0020, 0x007b, 0x0031, 0x007d, 0x0000 }; /* "{0}, {1}" */
    static const char16_t sub0[4] = { 0x007b, 0x0030, 0x007d , 0x0000 } ; /* {0} */
    static const char16_t sub1[4] = { 0x007b, 0x0031, 0x007d , 0x0000 } ; /* {1} */
    static const int32_t subLen = 3;
    static const char16_t defaultPattern[10] = {
        0x007b, 0x0030, 0x007d, 0x0020, 0x0028, 0x007b, 0x0031, 0x007d, 0x0029, 0x0000
    }; /* {0} ({1}) */
    static const int32_t defaultPatLen = 9;
    static const int32_t defaultSub0Pos = 0;
    static const int32_t defaultSub1Pos = 5;

    int32_t length; /* of formatted result */

    const char16_t *separator;
    int32_t sepLen = 0;
    const char16_t *pattern;
    int32_t patLen = 0;
    int32_t sub0Pos, sub1Pos;
    
    char16_t formatOpenParen         = 0x0028; // (
    char16_t formatReplaceOpenParen  = 0x005B; // [
    char16_t formatCloseParen        = 0x0029; // )
    char16_t formatReplaceCloseParen = 0x005D; // ]

    UBool haveLang = true; /* assume true, set false if we find we don't have
                              a lang component in the locale */
    UBool haveRest = true; /* assume true, set false if we find we don't have
                              any other component in the locale */
    UBool retry = false; /* set true if we need to retry, see below */

    int32_t langi = 0; /* index of the language substitution (0 or 1), virtually always 0 */

    if(pErrorCode==nullptr || U_FAILURE(*pErrorCode)) {
        return 0;
    }

    if(destCapacity<0 || (destCapacity>0 && dest==nullptr)) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    {
        UErrorCode status = U_ZERO_ERROR;

        icu::LocalUResourceBundlePointer locbundle(
                ures_open(U_ICUDATA_LANG, displayLocale, &status));
        icu::LocalUResourceBundlePointer dspbundle(
                ures_getByKeyWithFallback(locbundle.getAlias(), _kLocaleDisplayPattern, nullptr, &status));

        separator=ures_getStringByKeyWithFallback(dspbundle.getAlias(), _kSeparator, &sepLen, &status);
        pattern=ures_getStringByKeyWithFallback(dspbundle.getAlias(), _kPattern, &patLen, &status);
    }

    /* If we couldn't find any data, then use the defaults */
    if(sepLen == 0) {
       separator = defaultSeparator;
    }
    /* #10244: Even though separator is now a pattern, it is awkward to handle it as such
     * here since we are trying to build the display string in place in the dest buffer,
     * and to handle it as a pattern would entail having separate storage for the
     * substrings that need to be combined (the first of which may be the result of
     * previous such combinations). So for now we continue to treat the portion between
     * {0} and {1} as a string to be appended when joining substrings, ignoring anything
     * that is before {0} or after {1} (no existing separator pattern has any such thing).
     * This is similar to how pattern is handled below.
     */
    {
        char16_t *p0=u_strstr(separator, sub0);
        char16_t *p1=u_strstr(separator, sub1);
        if (p0==nullptr || p1==nullptr || p1<p0) {
            *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
        separator = (const char16_t *)p0 + subLen;
        sepLen = static_cast<int32_t>(p1 - separator);
    }

    if(patLen==0 || (patLen==defaultPatLen && !u_strncmp(pattern, defaultPattern, patLen))) {
        pattern=defaultPattern;
        patLen=defaultPatLen;
        sub0Pos=defaultSub0Pos;
        sub1Pos=defaultSub1Pos;
        // use default formatOpenParen etc. set above
    } else { /* non-default pattern */
        char16_t *p0=u_strstr(pattern, sub0);
        char16_t *p1=u_strstr(pattern, sub1);
        if (p0==nullptr || p1==nullptr) {
            *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
            return 0;
        }
        sub0Pos = static_cast<int32_t>(p0-pattern);
        sub1Pos = static_cast<int32_t>(p1-pattern);
        if (sub1Pos < sub0Pos) { /* a very odd pattern */
            int32_t t=sub0Pos; sub0Pos=sub1Pos; sub1Pos=t;
            langi=1;
        }
        if (u_strchr(pattern, 0xFF08) != nullptr) {
            formatOpenParen         = 0xFF08; // fullwidth (
            formatReplaceOpenParen  = 0xFF3B; // fullwidth [
            formatCloseParen        = 0xFF09; // fullwidth )
            formatReplaceCloseParen = 0xFF3D; // fullwidth ]
        }
    }

    /* We loop here because there is one case in which after the first pass we could need to
     * reextract the data.  If there's initial padding before the first element, we put in
     * the padding and then write that element.  If it turns out there's no second element,
     * we didn't need the padding.  If we do need the data (no preflight), and the first element
     * would have fit but for the padding, we need to reextract.  In this case (only) we
     * adjust the parameters so padding is not added, and repeat.
     */
    do {
        char16_t* p=dest;
        int32_t patPos=0; /* position in the pattern, used for non-substitution portions */
        int32_t langLen=0; /* length of language substitution */
        int32_t langPos=0; /* position in output of language substitution */
        int32_t restLen=0; /* length of 'everything else' substitution */
        int32_t restPos=0; /* position in output of 'everything else' substitution */
        icu::LocalUEnumerationPointer kenum; /* keyword enumeration */

        /* prefix of pattern, extremely likely to be empty */
        if(sub0Pos) {
            if(destCapacity >= sub0Pos) {
                while (patPos < sub0Pos) {
                    *p++ = pattern[patPos++];
                }
            } else {
                patPos=sub0Pos;
            }
            length=sub0Pos;
        } else {
            length=0;
        }

        for(int32_t subi=0,resti=0;subi<2;) { /* iterate through patterns 0 and 1*/
            UBool subdone = false; /* set true when ready to move to next substitution */

            /* prep p and cap for calls to get display components, pin cap to 0 since
               they complain if cap is negative */
            int32_t cap=destCapacity-length;
            if (cap <= 0) {
                cap=0;
            } else {
                p=dest+length;
            }

            if (subi == langi) { /* {0}*/
                if(haveLang) {
                    langPos=length;
                    langLen=uloc_getDisplayLanguage(locale, displayLocale, p, cap, pErrorCode);
                    length+=langLen;
                    haveLang=langLen>0;
                }
                subdone=true;
            } else { /* {1} */
                if(!haveRest) {
                    subdone=true;
                } else {
                    int32_t len; /* length of component (plus other stuff) we just fetched */
                    switch(resti++) {
                        case 0:
                            restPos=length;
                            len=uloc_getDisplayScriptInContext(locale, displayLocale, p, cap, pErrorCode);
                            break;
                        case 1:
                            len=uloc_getDisplayCountry(locale, displayLocale, p, cap, pErrorCode);
                            break;
                        case 2:
                            len=uloc_getDisplayVariant(locale, displayLocale, p, cap, pErrorCode);
                            break;
                        case 3:
                            kenum.adoptInstead(uloc_openKeywords(locale, pErrorCode));
                            U_FALLTHROUGH;
                        default: {
                            const char* kw=uenum_next(kenum.getAlias(), &len, pErrorCode);
                            if (kw == nullptr) {
                                len=0; /* mark that we didn't add a component */
                                subdone=true;
                            } else {
                                /* incorporating this behavior into the loop made it even more complex,
                                   so just special case it here */
                                len = uloc_getDisplayKeyword(kw, displayLocale, p, cap, pErrorCode);
                                if(len) {
                                    if(len < cap) {
                                        p[len]=0x3d; /* '=', assume we'll need it */
                                    }
                                    len+=1;

                                    /* adjust for call to get keyword */
                                    cap-=len;
                                    if(cap <= 0) {
                                        cap=0;
                                    } else {
                                        p+=len;
                                    }
                                }
                                /* reset for call below */
                                if(*pErrorCode == U_BUFFER_OVERFLOW_ERROR) {
                                    *pErrorCode=U_ZERO_ERROR;
                                }
                                int32_t vlen = uloc_getDisplayKeywordValue(locale, kw, displayLocale,
                                                                           p, cap, pErrorCode);
                                if(len) {
                                    if(vlen==0) {
                                        --len; /* remove unneeded '=' */
                                    }
                                    /* restore cap and p to what they were at start */
                                    cap=destCapacity-length;
                                    if(cap <= 0) {
                                        cap=0;
                                    } else {
                                        p=dest+length;
                                    }
                                }
                                len+=vlen; /* total we added for key + '=' + value */
                            }
                        } break;
                    } /* end switch */

                    if (len>0) {
                        /* we added a component, so add separator and write it if there's room. */
                        if(len+sepLen<=cap) {
                            const char16_t * plimit = p + len;
                            for (; p < plimit; p++) {
                                if (*p == formatOpenParen) {
                                    *p = formatReplaceOpenParen;
                                } else if (*p == formatCloseParen) {
                                    *p = formatReplaceCloseParen;
                                }
                            }
                            for(int32_t i=0;i<sepLen;++i) {
                                *p++=separator[i];
                            }
                        }
                        length+=len+sepLen;
                    } else if(subdone) {
                        /* remove separator if we added it */
                        if (length!=restPos) {
                            length-=sepLen;
                        }
                        restLen=length-restPos;
                        haveRest=restLen>0;
                    }
                }
            }

            if(*pErrorCode == U_BUFFER_OVERFLOW_ERROR) {
                *pErrorCode=U_ZERO_ERROR;
            }

            if(subdone) {
                if(haveLang && haveRest) {
                    /* append internal portion of pattern, the first time,
                       or last portion of pattern the second time */
                    int32_t padLen;
                    patPos+=subLen;
                    padLen=(subi==0 ? sub1Pos : patLen)-patPos;
                    if(length+padLen <= destCapacity) {
                        p=dest+length;
                        for(int32_t i=0;i<padLen;++i) {
                            *p++=pattern[patPos++];
                        }
                    } else {
                        patPos+=padLen;
                    }
                    length+=padLen;
                } else if(subi==0) {
                    /* don't have first component, reset for second component */
                    sub0Pos=0;
                    length=0;
                } else if(length>0) {
                    /* true length is the length of just the component we got. */
                    length=haveLang?langLen:restLen;
                    if(dest && sub0Pos!=0) {
                        if (sub0Pos+length<=destCapacity) {
                            /* first component not at start of result,
                               but we have full component in buffer. */
                            u_memmove(dest, dest+(haveLang?langPos:restPos), length);
                        } else {
                            /* would have fit, but didn't because of pattern prefix. */
                            sub0Pos=0; /* stops initial padding (and a second retry,
                                          so we won't end up here again) */
                            retry=true;
                        }
                    }
                }

                ++subi; /* move on to next substitution */
            }
        }
    } while(retry);

    return u_terminateUChars(dest, destCapacity, length, pErrorCode);
}

U_CAPI int32_t U_EXPORT2
uloc_getDisplayKeyword(const char* keyword,
                       const char* displayLocale,
                       char16_t* dest,
                       int32_t destCapacity,
                       UErrorCode* status){

    /* argument checking */
    if(status==nullptr || U_FAILURE(*status)) {
        return 0;
    }

    if(destCapacity<0 || (destCapacity>0 && dest==nullptr)) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }


    /* pass itemKey=nullptr to look for a top-level item */
    return _getStringOrCopyKey(U_ICUDATA_LANG, displayLocale,
                               _kKeys, nullptr,
                               keyword, 
                               keyword,      
                               dest, destCapacity,
                               *status);

}


#define UCURRENCY_DISPLAY_NAME_INDEX 1

U_CAPI int32_t U_EXPORT2
uloc_getDisplayKeywordValue(   const char* locale,
                               const char* keyword,
                               const char* displayLocale,
                               char16_t* dest,
                               int32_t destCapacity,
                               UErrorCode* status){


    /* argument checking */
    if(status==nullptr || U_FAILURE(*status)) {
        return 0;
    }

    if(destCapacity<0 || (destCapacity>0 && dest==nullptr)) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    /* get the keyword value */
    CharString keywordValue;
    if (keyword != nullptr && *keyword != '\0') {
        keywordValue = ulocimp_getKeywordValue(locale, keyword, *status);
    }

    /* 
     * if the keyword is equal to currency .. then to get the display name 
     * we need to do the fallback ourselves
     */
    if(uprv_stricmp(keyword, _kCurrency)==0){

        int32_t dispNameLen = 0;
        const char16_t *dispName = nullptr;

        icu::LocalUResourceBundlePointer bundle(
                ures_open(U_ICUDATA_CURR, displayLocale, status));
        icu::LocalUResourceBundlePointer currencies(
                ures_getByKey(bundle.getAlias(), _kCurrencies, nullptr, status));
        icu::LocalUResourceBundlePointer currency(
                ures_getByKeyWithFallback(currencies.getAlias(), keywordValue.data(), nullptr, status));

        dispName = ures_getStringByIndex(currency.getAlias(), UCURRENCY_DISPLAY_NAME_INDEX, &dispNameLen, status);

        if(U_FAILURE(*status)){
            if(*status == U_MISSING_RESOURCE_ERROR){
                /* we just want to write the value over if nothing is available */
                *status = U_USING_DEFAULT_WARNING;
            }else{
                return 0;
            }
        }

        /* now copy the dispName over if not nullptr */
        if(dispName != nullptr){
            if(dispNameLen <= destCapacity){
                u_memcpy(dest, dispName, dispNameLen);
                return u_terminateUChars(dest, destCapacity, dispNameLen, status);
            }else{
                *status = U_BUFFER_OVERFLOW_ERROR;
                return dispNameLen;
            }
        }else{
            /* we have not found the display name for the value .. just copy over */
            if(keywordValue.length() <= destCapacity){
                u_charsToUChars(keywordValue.data(), dest, keywordValue.length());
                return u_terminateUChars(dest, destCapacity, keywordValue.length(), status);
            }else{
                 *status = U_BUFFER_OVERFLOW_ERROR;
                return keywordValue.length();
            }
        }

        
    }else{

        return _getStringOrCopyKey(U_ICUDATA_LANG, displayLocale,
                                   _kTypes, keyword, 
                                   keywordValue.data(),
                                   keywordValue.data(),
                                   dest, destCapacity,
                                   *status);
    }
}
