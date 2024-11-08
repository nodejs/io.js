// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2009-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  udatpg.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2007jul30
*   created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/udatpg.h"
#include "unicode/uenum.h"
#include "unicode/strenum.h"
#include "unicode/dtptngen.h"
#include "ustrenum.h"

U_NAMESPACE_USE

U_CAPI UDateTimePatternGenerator * U_EXPORT2
udatpg_open(const char *locale, UErrorCode *pErrorCode) {
    if(locale==nullptr) {
        return (UDateTimePatternGenerator *)DateTimePatternGenerator::createInstance(*pErrorCode);
    } else {
        return (UDateTimePatternGenerator *)DateTimePatternGenerator::createInstance(Locale(locale), *pErrorCode);
    }
}

U_CAPI UDateTimePatternGenerator * U_EXPORT2
udatpg_openEmpty(UErrorCode *pErrorCode) {
    return (UDateTimePatternGenerator *)DateTimePatternGenerator::createEmptyInstance(*pErrorCode);
}

U_CAPI void U_EXPORT2
udatpg_close(UDateTimePatternGenerator *dtpg) {
    delete (DateTimePatternGenerator *)dtpg;
}

U_CAPI UDateTimePatternGenerator * U_EXPORT2
udatpg_clone(const UDateTimePatternGenerator *dtpg, UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return nullptr;
    }
    return (UDateTimePatternGenerator *)(((const DateTimePatternGenerator *)dtpg)->clone());
}

U_CAPI int32_t U_EXPORT2
udatpg_getBestPattern(UDateTimePatternGenerator *dtpg,
                      const char16_t *skeleton, int32_t length,
                      char16_t *bestPattern, int32_t capacity,
                      UErrorCode *pErrorCode) {
    return udatpg_getBestPatternWithOptions(dtpg, skeleton, length,
                                            UDATPG_MATCH_NO_OPTIONS,
                                            bestPattern, capacity, pErrorCode);
}

U_CAPI int32_t U_EXPORT2
udatpg_getBestPatternWithOptions(UDateTimePatternGenerator *dtpg,
                                 const char16_t *skeleton, int32_t length,
                                 UDateTimePatternMatchOptions options,
                                 char16_t *bestPattern, int32_t capacity,
                                 UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return 0;
    }
    if(skeleton==nullptr && length!=0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString skeletonString(length < 0, skeleton, length);
    UnicodeString result=((DateTimePatternGenerator *)dtpg)->getBestPattern(skeletonString, options, *pErrorCode);
    return result.extract(bestPattern, capacity, *pErrorCode);
}

U_CAPI int32_t U_EXPORT2
udatpg_getSkeleton(UDateTimePatternGenerator * /* dtpg */,
                   const char16_t *pattern, int32_t length,
                   char16_t *skeleton, int32_t capacity,
                   UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return 0;
    }
    if(pattern==nullptr && length!=0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString patternString(length < 0, pattern, length);
    UnicodeString result=DateTimePatternGenerator::staticGetSkeleton(
            patternString, *pErrorCode);
    return result.extract(skeleton, capacity, *pErrorCode);
}

U_CAPI int32_t U_EXPORT2
udatpg_getBaseSkeleton(UDateTimePatternGenerator * /* dtpg */,
                       const char16_t *pattern, int32_t length,
                       char16_t *skeleton, int32_t capacity,
                       UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return 0;
    }
    if(pattern==nullptr && length!=0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString patternString(length < 0, pattern, length);
    UnicodeString result=DateTimePatternGenerator::staticGetBaseSkeleton(
            patternString, *pErrorCode);
    return result.extract(skeleton, capacity, *pErrorCode);
}

U_CAPI UDateTimePatternConflict U_EXPORT2
udatpg_addPattern(UDateTimePatternGenerator *dtpg,
                  const char16_t *pattern, int32_t patternLength,
                  UBool override,
                  char16_t *conflictingPattern, int32_t capacity, int32_t *pLength,
                  UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return UDATPG_NO_CONFLICT;
    }
    if(pattern==nullptr && patternLength!=0) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return UDATPG_NO_CONFLICT;
    }
    UnicodeString patternString(patternLength < 0, pattern, patternLength);
    UnicodeString conflictingPatternString;
    UDateTimePatternConflict result=((DateTimePatternGenerator *)dtpg)->
            addPattern(patternString, override, conflictingPatternString, *pErrorCode);
    int32_t length=conflictingPatternString.extract(conflictingPattern, capacity, *pErrorCode);
    if(pLength!=nullptr) {
        *pLength=length;
    }
    return result;
}

U_CAPI void U_EXPORT2
udatpg_setAppendItemFormat(UDateTimePatternGenerator *dtpg,
                           UDateTimePatternField field,
                           const char16_t *value, int32_t length) {
    UnicodeString valueString(length < 0, value, length);
    ((DateTimePatternGenerator *)dtpg)->setAppendItemFormat(field, valueString);
}

U_CAPI const char16_t * U_EXPORT2
udatpg_getAppendItemFormat(const UDateTimePatternGenerator *dtpg,
                           UDateTimePatternField field,
                           int32_t *pLength) {
    const UnicodeString &result=((const DateTimePatternGenerator *)dtpg)->getAppendItemFormat(field);
    if(pLength!=nullptr) {
        *pLength=result.length();
    }
    return result.getBuffer();
}

U_CAPI void U_EXPORT2
udatpg_setAppendItemName(UDateTimePatternGenerator *dtpg,
                         UDateTimePatternField field,
                         const char16_t *value, int32_t length) {
    UnicodeString valueString(length < 0, value, length);
    ((DateTimePatternGenerator *)dtpg)->setAppendItemName(field, valueString);
}

U_CAPI const char16_t * U_EXPORT2
udatpg_getAppendItemName(const UDateTimePatternGenerator *dtpg,
                         UDateTimePatternField field,
                         int32_t *pLength) {
    const UnicodeString &result=((const DateTimePatternGenerator *)dtpg)->getAppendItemName(field);
    if(pLength!=nullptr) {
        *pLength=result.length();
    }
    return result.getBuffer();
}

U_CAPI int32_t U_EXPORT2
udatpg_getFieldDisplayName(const UDateTimePatternGenerator *dtpg,
                           UDateTimePatternField field,
                           UDateTimePGDisplayWidth width,
                           char16_t *fieldName, int32_t capacity,
                           UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode))
        return -1;
    if (fieldName == nullptr ? capacity != 0 : capacity < 0) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return -1;
    }
    UnicodeString result = ((const DateTimePatternGenerator *)dtpg)->getFieldDisplayName(field,width);
    if (fieldName == nullptr) {
        return result.length();
    }
    return result.extract(fieldName, capacity, *pErrorCode);
}

U_CAPI void U_EXPORT2
udatpg_setDateTimeFormat(const UDateTimePatternGenerator *dtpg,
                         const char16_t *dtFormat, int32_t length) {
    UnicodeString dtFormatString(length < 0, dtFormat, length);
    ((DateTimePatternGenerator *)dtpg)->setDateTimeFormat(dtFormatString);
}

U_CAPI const char16_t * U_EXPORT2
udatpg_getDateTimeFormat(const UDateTimePatternGenerator *dtpg,
                         int32_t *pLength) {
    UErrorCode status = U_ZERO_ERROR;
    return udatpg_getDateTimeFormatForStyle(dtpg, UDAT_MEDIUM, pLength, &status);
}

U_CAPI void U_EXPORT2
udatpg_setDateTimeFormatForStyle(UDateTimePatternGenerator *udtpg,
                        UDateFormatStyle style,
                        const char16_t *dateTimeFormat, int32_t length,
                        UErrorCode *pErrorCode) {
    if (U_FAILURE(*pErrorCode)) {
        return;
    } else if (dateTimeFormat==nullptr) {
        *pErrorCode = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    DateTimePatternGenerator *dtpg = reinterpret_cast<DateTimePatternGenerator *>(udtpg);
    UnicodeString dtFormatString(length < 0, dateTimeFormat, length);
    dtpg->setDateTimeFormat(style, dtFormatString, *pErrorCode);
}

U_CAPI const char16_t* U_EXPORT2
udatpg_getDateTimeFormatForStyle(const UDateTimePatternGenerator *udtpg,
                        UDateFormatStyle style, int32_t *pLength,
                        UErrorCode *pErrorCode) {
    static const char16_t emptyString[] = { (char16_t)0 };
    if (U_FAILURE(*pErrorCode)) {
        if (pLength !=nullptr) {
            *pLength = 0;
        }
        return emptyString;
    }
    const DateTimePatternGenerator *dtpg = reinterpret_cast<const DateTimePatternGenerator *>(udtpg);
    const UnicodeString &result = dtpg->getDateTimeFormat(style, *pErrorCode);
    if (pLength != nullptr) {
        *pLength=result.length();
    }
    // Note: The UnicodeString for the dateTimeFormat string in the DateTimePatternGenerator
    // was NUL-terminated what it was set, to avoid doing it here which could re-allocate
    // the buffe and affect and cont references to the string or its buffer.
    return result.getBuffer();
 }

U_CAPI void U_EXPORT2
udatpg_setDecimal(UDateTimePatternGenerator *dtpg,
                  const char16_t *decimal, int32_t length) {
    UnicodeString decimalString(length < 0, decimal, length);
    ((DateTimePatternGenerator *)dtpg)->setDecimal(decimalString);
}

U_CAPI const char16_t * U_EXPORT2
udatpg_getDecimal(const UDateTimePatternGenerator *dtpg,
                  int32_t *pLength) {
    const UnicodeString &result=((const DateTimePatternGenerator *)dtpg)->getDecimal();
    if(pLength!=nullptr) {
        *pLength=result.length();
    }
    return result.getBuffer();
}

U_CAPI int32_t U_EXPORT2
udatpg_replaceFieldTypes(UDateTimePatternGenerator *dtpg,
                         const char16_t *pattern, int32_t patternLength,
                         const char16_t *skeleton, int32_t skeletonLength,
                         char16_t *dest, int32_t destCapacity,
                         UErrorCode *pErrorCode) {
    return udatpg_replaceFieldTypesWithOptions(dtpg, pattern, patternLength, skeleton, skeletonLength,
                                               UDATPG_MATCH_NO_OPTIONS,
                                               dest, destCapacity, pErrorCode);
}

U_CAPI int32_t U_EXPORT2
udatpg_replaceFieldTypesWithOptions(UDateTimePatternGenerator *dtpg,
                                    const char16_t *pattern, int32_t patternLength,
                                    const char16_t *skeleton, int32_t skeletonLength,
                                    UDateTimePatternMatchOptions options,
                                    char16_t *dest, int32_t destCapacity,
                                    UErrorCode *pErrorCode) {
    if(U_FAILURE(*pErrorCode)) {
        return 0;
    }
    if((pattern==nullptr && patternLength!=0) || (skeleton==nullptr && skeletonLength!=0)) {
        *pErrorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString patternString(patternLength < 0, pattern, patternLength);
    UnicodeString skeletonString(skeletonLength < 0, skeleton, skeletonLength);
    UnicodeString result=((DateTimePatternGenerator *)dtpg)->replaceFieldTypes(patternString, skeletonString, options, *pErrorCode);
    return result.extract(dest, destCapacity, *pErrorCode);
}

U_CAPI UEnumeration * U_EXPORT2
udatpg_openSkeletons(const UDateTimePatternGenerator *dtpg, UErrorCode *pErrorCode) {
    return uenum_openFromStringEnumeration(
                ((DateTimePatternGenerator *)dtpg)->getSkeletons(*pErrorCode),
                pErrorCode);
}

U_CAPI UEnumeration * U_EXPORT2
udatpg_openBaseSkeletons(const UDateTimePatternGenerator *dtpg, UErrorCode *pErrorCode) {
    return uenum_openFromStringEnumeration(
                ((DateTimePatternGenerator *)dtpg)->getBaseSkeletons(*pErrorCode),
                pErrorCode);
}

U_CAPI const char16_t * U_EXPORT2
udatpg_getPatternForSkeleton(const UDateTimePatternGenerator *dtpg,
                             const char16_t *skeleton, int32_t skeletonLength,
                             int32_t *pLength) {
    UnicodeString skeletonString(skeletonLength < 0, skeleton, skeletonLength);
    const UnicodeString &result=((const DateTimePatternGenerator *)dtpg)->getPatternForSkeleton(skeletonString);
    if(pLength!=nullptr) {
        *pLength=result.length();
    }
    return result.getBuffer();
}

U_CAPI UDateFormatHourCycle U_EXPORT2
udatpg_getDefaultHourCycle(const UDateTimePatternGenerator *dtpg, UErrorCode* pErrorCode) {
    return ((const DateTimePatternGenerator *)dtpg)->getDefaultHourCycle(*pErrorCode);
}

#endif
