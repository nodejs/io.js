// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 ********************************************************************************
 *   Copyright (C) 2005-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 ********************************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION
#include "unicode/ucsdet.h"
#include "csdetect.h"
#include "csmatch.h"
#include "csrsbcs.h"
#include "csrmbcs.h"
#include "csrutf8.h"
#include "csrucode.h"
#include "csr2022.h"

#include "cmemory.h"

U_NAMESPACE_USE

#define NEW_ARRAY(type,count) (type *) uprv_malloc((count) * sizeof(type))
#define DELETE_ARRAY(array) uprv_free((void *) (array))

U_CDECL_BEGIN

U_CAPI UCharsetDetector * U_EXPORT2
ucsdet_open(UErrorCode   *status)
{
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    CharsetDetector* csd = new CharsetDetector(*status);

    if (U_FAILURE(*status)) {
        delete csd;
        csd = nullptr;
    }

    return (UCharsetDetector *) csd;
}

U_CAPI void U_EXPORT2
ucsdet_close(UCharsetDetector *ucsd)
{
    CharsetDetector *csd = (CharsetDetector *) ucsd;
    delete csd;
}

U_CAPI void U_EXPORT2
ucsdet_setText(UCharsetDetector *ucsd, const char *textIn, int32_t len, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return;
    }

    ((CharsetDetector *) ucsd)->setText(textIn, len);
}

U_CAPI const char * U_EXPORT2
ucsdet_getName(const UCharsetMatch *ucsm, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    return ((CharsetMatch *) ucsm)->getName();
}

U_CAPI int32_t U_EXPORT2
ucsdet_getConfidence(const UCharsetMatch *ucsm, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return 0;
    }

    return ((CharsetMatch *) ucsm)->getConfidence();
}

U_CAPI const char * U_EXPORT2
ucsdet_getLanguage(const UCharsetMatch *ucsm, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    return ((CharsetMatch *) ucsm)->getLanguage();
}

U_CAPI const UCharsetMatch * U_EXPORT2
ucsdet_detect(UCharsetDetector *ucsd, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    return (const UCharsetMatch *) ((CharsetDetector *) ucsd)->detect(*status);
}

U_CAPI void U_EXPORT2
ucsdet_setDeclaredEncoding(UCharsetDetector *ucsd, const char *encoding, int32_t length, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return;
    }

    ((CharsetDetector *) ucsd)->setDeclaredEncoding(encoding,length);
}

U_CAPI const UCharsetMatch**
ucsdet_detectAll(UCharsetDetector *ucsd,
                 int32_t *maxMatchesFound, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return nullptr;
    }

    CharsetDetector *csd = (CharsetDetector *) ucsd;

    return (const UCharsetMatch**)csd->detectAll(*maxMatchesFound,*status);
}

// U_CAPI  const char * U_EXPORT2
// ucsdet_getDetectableCharsetName(const UCharsetDetector *csd, int32_t index, UErrorCode *status)
// {
//     if(U_FAILURE(*status)) {
//         return 0;
//     }
//     return csd->getCharsetName(index,*status);
// }

// U_CAPI  int32_t U_EXPORT2
// ucsdet_getDetectableCharsetsCount(const UCharsetDetector *csd, UErrorCode *status)
// {
//     if(U_FAILURE(*status)) {
//         return -1;
//     }
//     return UCharsetDetector::getDetectableCount();
// }

U_CAPI  UBool U_EXPORT2
ucsdet_isInputFilterEnabled(const UCharsetDetector *ucsd)
{
    // todo: could use an error return...
    if (ucsd == nullptr) {
        return false;
    }

    return ((CharsetDetector *) ucsd)->getStripTagsFlag();
}

U_CAPI  UBool U_EXPORT2
ucsdet_enableInputFilter(UCharsetDetector *ucsd, UBool filter)
{
    // todo: could use an error return...
    if (ucsd == nullptr) {
        return false;
    }

    CharsetDetector *csd = (CharsetDetector *) ucsd;
    UBool prev = csd->getStripTagsFlag();

    csd->setStripTagsFlag(filter);

    return prev;
}

U_CAPI  int32_t U_EXPORT2
ucsdet_getUChars(const UCharsetMatch *ucsm,
                 char16_t *buf, int32_t cap, UErrorCode *status)
{
    if(U_FAILURE(*status)) {
        return 0;
    }

    return ((CharsetMatch *) ucsm)->getUChars(buf, cap, status);
}

U_CAPI void U_EXPORT2
ucsdet_setDetectableCharset(UCharsetDetector *ucsd, const char *encoding, UBool enabled, UErrorCode *status)
{
    ((CharsetDetector *)ucsd)->setDetectableCharset(encoding, enabled, *status);
}

U_CAPI  UEnumeration * U_EXPORT2
ucsdet_getAllDetectableCharsets(const UCharsetDetector * /*ucsd*/, UErrorCode *status)
{
    return CharsetDetector::getAllDetectableCharsets(*status);
}

U_CAPI UEnumeration * U_EXPORT2
ucsdet_getDetectableCharsets(const UCharsetDetector *ucsd,  UErrorCode *status)
{
    return ((CharsetDetector *)ucsd)->getDetectableCharsets(*status);
}

U_CDECL_END


#endif
