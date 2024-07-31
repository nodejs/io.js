// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
***************************************************************************
* Copyright (C) 2008-2015, International Business Machines Corporation
* and others. All Rights Reserved.
***************************************************************************
*   file name:  uspoof.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2008Feb13
*   created by: Andy Heninger
*
*   Unicode Spoof Detection
*/
#include "unicode/ubidi.h"
#include "unicode/utypes.h"
#include "unicode/normalizer2.h"
#include "unicode/uspoof.h"
#include "unicode/ustring.h"
#include "unicode/utf16.h"
#include "cmemory.h"
#include "cstring.h"
#include "mutex.h"
#include "scriptset.h"
#include "uassert.h"
#include "ucln_in.h"
#include "uspoof_impl.h"
#include "umutex.h"


#if !UCONFIG_NO_NORMALIZATION

U_NAMESPACE_USE


//
// Static Objects used by the spoof impl, their thread safe initialization and their cleanup.
//
static UnicodeSet *gInclusionSet = nullptr;
static UnicodeSet *gRecommendedSet = nullptr;
static const Normalizer2 *gNfdNormalizer = nullptr;
static UInitOnce gSpoofInitStaticsOnce {};

namespace {

UBool U_CALLCONV
uspoof_cleanup() {
    delete gInclusionSet;
    gInclusionSet = nullptr;
    delete gRecommendedSet;
    gRecommendedSet = nullptr;
    gNfdNormalizer = nullptr;
    gSpoofInitStaticsOnce.reset();
    return true;
}

void U_CALLCONV initializeStatics(UErrorCode &status) {
    gInclusionSet = new UnicodeSet();
    gRecommendedSet = new UnicodeSet();
    if (gInclusionSet == nullptr || gRecommendedSet == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete gInclusionSet;
        gInclusionSet = nullptr;
        delete gRecommendedSet;
        gRecommendedSet = nullptr;
        return;
    }
    gInclusionSet->applyIntPropertyValue(UCHAR_IDENTIFIER_TYPE, U_ID_TYPE_INCLUSION, status);
    gRecommendedSet->applyIntPropertyValue(UCHAR_IDENTIFIER_TYPE, U_ID_TYPE_RECOMMENDED, status);
    if (U_FAILURE(status)) {
        delete gInclusionSet;
        gInclusionSet = nullptr;
        delete gRecommendedSet;
        gRecommendedSet = nullptr;
        return;
    }
    gInclusionSet->freeze();
    gRecommendedSet->freeze();
    gNfdNormalizer = Normalizer2::getNFDInstance(status);
    ucln_i18n_registerCleanup(UCLN_I18N_SPOOF, uspoof_cleanup);
}

}  // namespace

U_CFUNC void uspoof_internalInitStatics(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
}

U_CAPI USpoofChecker * U_EXPORT2
uspoof_open(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    if (U_FAILURE(*status)) {
        return nullptr;
    }
    SpoofImpl *si = new SpoofImpl(*status);
    if (si == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (U_FAILURE(*status)) {
        delete si;
        return nullptr;
    }
    return si->asUSpoofChecker();
}


U_CAPI USpoofChecker * U_EXPORT2
uspoof_openFromSerialized(const void *data, int32_t length, int32_t *pActualLength,
                          UErrorCode *status) {
    if (U_FAILURE(*status)) {
        return nullptr;
    }

    if (data == nullptr) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    if (U_FAILURE(*status))
    {
        return nullptr;
    }

    SpoofData *sd = new SpoofData(data, length, *status);
    if (sd == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }

    if (U_FAILURE(*status)) {
        delete sd;
        return nullptr;
    }

    SpoofImpl *si = new SpoofImpl(sd, *status);
    if (si == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        delete sd; // explicit delete as the destructor for si won't be called.
        return nullptr;
    }

    if (U_FAILURE(*status)) {
        delete si; // no delete for sd, as the si destructor will delete it.
        return nullptr;
    }

    if (pActualLength != nullptr) {
        *pActualLength = sd->size();
    }
    return si->asUSpoofChecker();
}


U_CAPI USpoofChecker * U_EXPORT2
uspoof_clone(const USpoofChecker *sc, UErrorCode *status) {
    const SpoofImpl *src = SpoofImpl::validateThis(sc, *status);
    if (src == nullptr) {
        return nullptr;
    }
    SpoofImpl *result = new SpoofImpl(*src, *status);   // copy constructor
    if (result == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    if (U_FAILURE(*status)) {
        delete result;
        result = nullptr;
    }
    return result->asUSpoofChecker();
}


U_CAPI void U_EXPORT2
uspoof_close(USpoofChecker *sc) {
    UErrorCode status = U_ZERO_ERROR;
    SpoofImpl *This = SpoofImpl::validateThis(sc, status);
    delete This;
}


U_CAPI void U_EXPORT2
uspoof_setChecks(USpoofChecker *sc, int32_t checks, UErrorCode *status) {
    SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return;
    }

    // Verify that the requested checks are all ones (bits) that 
    //   are acceptable, known values.
    if (checks & ~(USPOOF_ALL_CHECKS | USPOOF_AUX_INFO)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR; 
        return;
    }

    This->fChecks = checks;
}


U_CAPI int32_t U_EXPORT2
uspoof_getChecks(const USpoofChecker *sc, UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return 0;
    }
    return This->fChecks;
}

U_CAPI void U_EXPORT2
uspoof_setRestrictionLevel(USpoofChecker *sc, URestrictionLevel restrictionLevel) {
    UErrorCode status = U_ZERO_ERROR;
    SpoofImpl *This = SpoofImpl::validateThis(sc, status);
    if (This != nullptr) {
        This->fRestrictionLevel = restrictionLevel;
        This->fChecks |= USPOOF_RESTRICTION_LEVEL;
    }
}

U_CAPI URestrictionLevel U_EXPORT2
uspoof_getRestrictionLevel(const USpoofChecker *sc) {
    UErrorCode status = U_ZERO_ERROR;
    const SpoofImpl *This = SpoofImpl::validateThis(sc, status);
    if (This == nullptr) {
        return USPOOF_UNRESTRICTIVE;
    }
    return This->fRestrictionLevel;
}

U_CAPI void U_EXPORT2
uspoof_setAllowedLocales(USpoofChecker *sc, const char *localesList, UErrorCode *status) {
    SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return;
    }
    This->setAllowedLocales(localesList, *status);
}

U_CAPI const char * U_EXPORT2
uspoof_getAllowedLocales(USpoofChecker *sc, UErrorCode *status) {
    SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return nullptr;
    }
    return This->getAllowedLocales(*status);
}


U_CAPI const USet * U_EXPORT2
uspoof_getAllowedChars(const USpoofChecker *sc, UErrorCode *status) {
    const UnicodeSet *result = uspoof_getAllowedUnicodeSet(sc, status);
    return result->toUSet();
}

U_CAPI const UnicodeSet * U_EXPORT2
uspoof_getAllowedUnicodeSet(const USpoofChecker *sc, UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return nullptr;
    }
    return This->fAllowedCharsSet;
}


U_CAPI void U_EXPORT2
uspoof_setAllowedChars(USpoofChecker *sc, const USet *chars, UErrorCode *status) {
    const UnicodeSet *set = UnicodeSet::fromUSet(chars);
    uspoof_setAllowedUnicodeSet(sc, set, status);
}


U_CAPI void U_EXPORT2
uspoof_setAllowedUnicodeSet(USpoofChecker *sc, const UnicodeSet *chars, UErrorCode *status) {
    SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return;
    }
    if (chars->isBogus()) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return;
    }
    UnicodeSet *clonedSet = chars->clone();
    if (clonedSet == nullptr || clonedSet->isBogus()) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    clonedSet->freeze();
    delete This->fAllowedCharsSet;
    This->fAllowedCharsSet = clonedSet;
    This->fChecks |= USPOOF_CHAR_LIMIT;
}


U_CAPI int32_t U_EXPORT2
uspoof_check(const USpoofChecker *sc,
             const char16_t *id, int32_t length,
             int32_t *position,
             UErrorCode *status) {

    // Backwards compatibility:
    if (position != nullptr) {
        *position = 0;
    }

    // Delegate to uspoof_check2
    return uspoof_check2(sc, id, length, nullptr, status);
}


U_CAPI int32_t U_EXPORT2
uspoof_check2(const USpoofChecker *sc,
    const char16_t* id, int32_t length,
    USpoofCheckResult* checkResult,
    UErrorCode *status) {

    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return 0;
    }
    if (length < -1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString idStr((length == -1), id, length);  // Aliasing constructor.
    int32_t result = uspoof_check2UnicodeString(sc, idStr, checkResult, status);
    return result;
}


U_CAPI int32_t U_EXPORT2
uspoof_checkUTF8(const USpoofChecker *sc,
                 const char *id, int32_t length,
                 int32_t *position,
                 UErrorCode *status) {

    // Backwards compatibility:
    if (position != nullptr) {
        *position = 0;
    }

    // Delegate to uspoof_check2
    return uspoof_check2UTF8(sc, id, length, nullptr, status);
}


U_CAPI int32_t U_EXPORT2
uspoof_check2UTF8(const USpoofChecker *sc,
    const char *id, int32_t length,
    USpoofCheckResult* checkResult,
    UErrorCode *status) {

    if (U_FAILURE(*status)) {
        return 0;
    }
    UnicodeString idStr = UnicodeString::fromUTF8(StringPiece(id, length>=0 ? length : static_cast<int32_t>(uprv_strlen(id))));
    int32_t result = uspoof_check2UnicodeString(sc, idStr, checkResult, status);
    return result;
}


U_CAPI int32_t U_EXPORT2
uspoof_areConfusable(const USpoofChecker *sc,
                     const char16_t *id1, int32_t length1,
                     const char16_t *id2, int32_t length2,
                     UErrorCode *status) {
    SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (length1 < -1 || length2 < -1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
        
    UnicodeString id1Str((length1==-1), id1, length1);  // Aliasing constructor
    UnicodeString id2Str((length2==-1), id2, length2);  // Aliasing constructor
    return uspoof_areConfusableUnicodeString(sc, id1Str, id2Str, status);
}


U_CAPI int32_t U_EXPORT2
uspoof_areConfusableUTF8(const USpoofChecker *sc,
                         const char *id1, int32_t length1,
                         const char *id2, int32_t length2,
                         UErrorCode *status) {
    SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (length1 < -1 || length2 < -1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString id1Str = UnicodeString::fromUTF8(StringPiece(id1, length1>=0? length1 : static_cast<int32_t>(uprv_strlen(id1))));
    UnicodeString id2Str = UnicodeString::fromUTF8(StringPiece(id2, length2>=0? length2 : static_cast<int32_t>(uprv_strlen(id2))));
    int32_t results = uspoof_areConfusableUnicodeString(sc, id1Str, id2Str, status);
    return results;
}
 

U_CAPI int32_t U_EXPORT2
uspoof_areConfusableUnicodeString(const USpoofChecker *sc,
                                  const icu::UnicodeString &id1,
                                  const icu::UnicodeString &id2,
                                  UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    //
    // See section 4 of UAX 39 for the algorithm for checking whether two strings are confusable,
    //   and for definitions of the types (single, whole, mixed-script) of confusables.
    
    // We only care about a few of the check flags.  Ignore the others.
    // If no tests relevant to this function have been specified, return an error.
    // TODO:  is this really the right thing to do?  It's probably an error on the caller's part,
    //        but logically we would just return 0 (no error).
    if ((This->fChecks & USPOOF_CONFUSABLE) == 0) {
        *status = U_INVALID_STATE_ERROR;
        return 0;
    }

    // Compute the skeletons and check for confusability.
    UnicodeString id1Skeleton;
    uspoof_getSkeletonUnicodeString(sc, 0 /* deprecated */, id1, id1Skeleton, status);
    UnicodeString id2Skeleton;
    uspoof_getSkeletonUnicodeString(sc, 0 /* deprecated */, id2, id2Skeleton, status);
    if (U_FAILURE(*status)) { return 0; }
    if (id1Skeleton != id2Skeleton) {
        return 0;
    }

    // If we get here, the strings are confusable.  Now we just need to set the flags for the appropriate classes
    // of confusables according to UTS 39 section 4.
    // Start by computing the resolved script sets of id1 and id2.
    ScriptSet id1RSS;
    This->getResolvedScriptSet(id1, id1RSS, *status);
    ScriptSet id2RSS;
    This->getResolvedScriptSet(id2, id2RSS, *status);

    // Turn on all applicable flags
    int32_t result = 0;
    if (id1RSS.intersects(id2RSS)) {
        result |= USPOOF_SINGLE_SCRIPT_CONFUSABLE;
    } else {
        result |= USPOOF_MIXED_SCRIPT_CONFUSABLE;
        if (!id1RSS.isEmpty() && !id2RSS.isEmpty()) {
            result |= USPOOF_WHOLE_SCRIPT_CONFUSABLE;
        }
    }

    // Turn off flags that the user doesn't want
    if ((This->fChecks & USPOOF_SINGLE_SCRIPT_CONFUSABLE) == 0) {
        result &= ~USPOOF_SINGLE_SCRIPT_CONFUSABLE;
    }
    if ((This->fChecks & USPOOF_MIXED_SCRIPT_CONFUSABLE) == 0) {
        result &= ~USPOOF_MIXED_SCRIPT_CONFUSABLE;
    }
    if ((This->fChecks & USPOOF_WHOLE_SCRIPT_CONFUSABLE) == 0) {
        result &= ~USPOOF_WHOLE_SCRIPT_CONFUSABLE;
    }

    return result;
}

U_CAPI uint32_t U_EXPORT2 uspoof_areBidiConfusable(const USpoofChecker *sc, UBiDiDirection direction,
                                                  const char16_t *id1, int32_t length1,
                                                  const char16_t *id2, int32_t length2,
                                                   UErrorCode *status) {
    UnicodeString id1Str((length1 == -1), id1, length1); // Aliasing constructor
    UnicodeString id2Str((length2 == -1), id2, length2); // Aliasing constructor
    if (id1Str.isBogus() || id2Str.isBogus()) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    return uspoof_areBidiConfusableUnicodeString(sc, direction, id1Str, id2Str, status);
}

U_CAPI uint32_t U_EXPORT2 uspoof_areBidiConfusableUTF8(const USpoofChecker *sc, UBiDiDirection direction,
                                                      const char *id1, int32_t length1, const char *id2,
                                                      int32_t length2, UErrorCode *status) {
    if (length1 < -1 || length2 < -1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString id1Str = UnicodeString::fromUTF8(
        StringPiece(id1, length1 >= 0 ? length1 : static_cast<int32_t>(uprv_strlen(id1))));
    UnicodeString id2Str = UnicodeString::fromUTF8(
        StringPiece(id2, length2 >= 0 ? length2 : static_cast<int32_t>(uprv_strlen(id2))));
    return uspoof_areBidiConfusableUnicodeString(sc, direction, id1Str, id2Str, status);
}

U_CAPI uint32_t U_EXPORT2 uspoof_areBidiConfusableUnicodeString(const USpoofChecker *sc,
                                                               UBiDiDirection direction,
                                                               const icu::UnicodeString &id1,
                                                               const icu::UnicodeString &id2,
                                                               UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    //
    // See section 4 of UTS 39 for the algorithm for checking whether two strings are confusable,
    //   and for definitions of the types (single, whole, mixed-script) of confusables.

    // We only care about a few of the check flags.  Ignore the others.
    // If no tests relevant to this function have been specified, return an error.
    // TODO:  is this really the right thing to do?  It's probably an error on the caller's part,
    //        but logically we would just return 0 (no error).
    if ((This->fChecks & USPOOF_CONFUSABLE) == 0) {
        *status = U_INVALID_STATE_ERROR;
        return 0;
    }

    // Compute the skeletons and check for confusability.
    UnicodeString id1Skeleton;
    uspoof_getBidiSkeletonUnicodeString(sc, direction, id1, id1Skeleton, status);
    UnicodeString id2Skeleton;
    uspoof_getBidiSkeletonUnicodeString(sc, direction, id2, id2Skeleton, status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (id1Skeleton != id2Skeleton) {
        return 0;
    }

    // If we get here, the strings are confusable.  Now we just need to set the flags for the appropriate
    // classes of confusables according to UTS 39 section 4. Start by computing the resolved script sets
    // of id1 and id2.
    ScriptSet id1RSS;
    This->getResolvedScriptSet(id1, id1RSS, *status);
    ScriptSet id2RSS;
    This->getResolvedScriptSet(id2, id2RSS, *status);

    // Turn on all applicable flags
    uint32_t result = 0;
    if (id1RSS.intersects(id2RSS)) {
        result |= USPOOF_SINGLE_SCRIPT_CONFUSABLE;
    } else {
        result |= USPOOF_MIXED_SCRIPT_CONFUSABLE;
        if (!id1RSS.isEmpty() && !id2RSS.isEmpty()) {
            result |= USPOOF_WHOLE_SCRIPT_CONFUSABLE;
        }
    }

    // Turn off flags that the user doesn't want
    return result & This->fChecks;
}


U_CAPI int32_t U_EXPORT2
uspoof_checkUnicodeString(const USpoofChecker *sc,
                          const icu::UnicodeString &id,
                          int32_t *position,
                          UErrorCode *status) {

    // Backwards compatibility:
    if (position != nullptr) {
        *position = 0;
    }

    // Delegate to uspoof_check2
    return uspoof_check2UnicodeString(sc, id, nullptr, status);
}

namespace {

int32_t checkImpl(const SpoofImpl* This, const UnicodeString& id, CheckResult* checkResult, UErrorCode* status) {
    U_ASSERT(This != nullptr);
    U_ASSERT(checkResult != nullptr);
    checkResult->clear();
    int32_t result = 0;

    if (0 != (This->fChecks & USPOOF_RESTRICTION_LEVEL)) {
        URestrictionLevel idRestrictionLevel = This->getRestrictionLevel(id, *status);
        if (idRestrictionLevel > This->fRestrictionLevel) {
            result |= USPOOF_RESTRICTION_LEVEL;
        }
        checkResult->fRestrictionLevel = idRestrictionLevel;
    }

    if (0 != (This->fChecks & USPOOF_MIXED_NUMBERS)) {
        UnicodeSet numerics;
        This->getNumerics(id, numerics, *status);
        if (numerics.size() > 1) {
            result |= USPOOF_MIXED_NUMBERS;
        }
        checkResult->fNumerics = numerics;  // UnicodeSet::operator=
    }

    if (0 != (This->fChecks & USPOOF_HIDDEN_OVERLAY)) {
        int32_t index = This->findHiddenOverlay(id, *status);
        if (index != -1) {
            result |= USPOOF_HIDDEN_OVERLAY;
        }
    }


    if (0 != (This->fChecks & USPOOF_CHAR_LIMIT)) {
        int32_t i;
        UChar32 c;
        int32_t length = id.length();
        for (i=0; i<length ;) {
            c = id.char32At(i);
            i += U16_LENGTH(c);
            if (!This->fAllowedCharsSet->contains(c)) {
                result |= USPOOF_CHAR_LIMIT;
                break;
            }
        }
    }

    if (0 != (This->fChecks & USPOOF_INVISIBLE)) {
        // This check needs to be done on NFD input
        UnicodeString nfdText;
        gNfdNormalizer->normalize(id, nfdText, *status);
        int32_t nfdLength = nfdText.length();

        // scan for more than one occurrence of the same non-spacing mark
        // in a sequence of non-spacing marks.
        int32_t     i;
        UChar32     c;
        UChar32     firstNonspacingMark = 0;
        UBool       haveMultipleMarks = false;  
        UnicodeSet  marksSeenSoFar;   // Set of combining marks in a single combining sequence.
        
        for (i=0; i<nfdLength ;) {
            c = nfdText.char32At(i);
            i += U16_LENGTH(c);
            if (u_charType(c) != U_NON_SPACING_MARK) {
                firstNonspacingMark = 0;
                if (haveMultipleMarks) {
                    marksSeenSoFar.clear();
                    haveMultipleMarks = false;
                }
                continue;
            }
            if (firstNonspacingMark == 0) {
                firstNonspacingMark = c;
                continue;
            }
            if (!haveMultipleMarks) {
                marksSeenSoFar.add(firstNonspacingMark);
                haveMultipleMarks = true;
            }
            if (marksSeenSoFar.contains(c)) {
                // report the error, and stop scanning.
                // No need to find more than the first failure.
                result |= USPOOF_INVISIBLE;
                break;
            }
            marksSeenSoFar.add(c);
        }
    }

    checkResult->fChecks = result;
    return checkResult->toCombinedBitmask(This->fChecks);
}

}  // namespace

U_CAPI int32_t U_EXPORT2
uspoof_check2UnicodeString(const USpoofChecker *sc,
                          const icu::UnicodeString &id,
                          USpoofCheckResult* checkResult,
                          UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        return false;
    }

    if (checkResult != nullptr) {
        CheckResult* ThisCheckResult = CheckResult::validateThis(checkResult, *status);
        if (ThisCheckResult == nullptr) {
            return false;
        }
        return checkImpl(This, id, ThisCheckResult, status);
    } else {
        // Stack-allocate the checkResult since this method doesn't return it
        CheckResult stackCheckResult;
        return checkImpl(This, id, &stackCheckResult, status);
    }
}


U_CAPI int32_t U_EXPORT2
uspoof_getSkeleton(const USpoofChecker *sc,
                   uint32_t type,
                   const char16_t *id,  int32_t length,
                   char16_t *dest, int32_t destCapacity,
                   UErrorCode *status) {

    SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (length<-1 || destCapacity<0 || (destCapacity==0 && dest!=nullptr)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    UnicodeString idStr((length==-1), id, length);  // Aliasing constructor
    UnicodeString destStr;
    uspoof_getSkeletonUnicodeString(sc, type, idStr, destStr, status);
    destStr.extract(dest, destCapacity, *status);
    return destStr.length();
}

U_CAPI int32_t U_EXPORT2 uspoof_getBidiSkeleton(const USpoofChecker *sc, UBiDiDirection direction,
                                                const UChar *id, int32_t length, UChar *dest,
                                                int32_t destCapacity, UErrorCode *status) {
    UnicodeString idStr((length == -1), id, length); // Aliasing constructor
    if (idStr.isBogus()) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }
    UnicodeString destStr;
    uspoof_getBidiSkeletonUnicodeString(sc, direction, idStr, destStr, status);
    return destStr.extract(dest, destCapacity, *status);
}



U_I18N_API UnicodeString &U_EXPORT2 uspoof_getBidiSkeletonUnicodeString(const USpoofChecker *sc,
                                                                        UBiDiDirection direction,
                                                                        const UnicodeString &id,
                                                                        UnicodeString &dest,
                                                                        UErrorCode *status) {
    dest.remove();
    if (direction != UBIDI_LTR && direction != UBIDI_RTL) {
      *status = U_ILLEGAL_ARGUMENT_ERROR;
      return dest;
    }
    UBiDi *bidi = ubidi_open();
    ubidi_setPara(bidi, id.getBuffer(), id.length(), direction,
                  /*embeddingLevels*/ nullptr, status);
    if (U_FAILURE(*status)) {
        ubidi_close(bidi);
        return dest;
    }
    UnicodeString reordered;
    int32_t const size = ubidi_getProcessedLength(bidi);
    UChar* const reorderedBuffer = reordered.getBuffer(size);
    if (reorderedBuffer == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        ubidi_close(bidi);
        return dest;
    }
    ubidi_writeReordered(bidi, reorderedBuffer, size,
                         UBIDI_KEEP_BASE_COMBINING | UBIDI_DO_MIRRORING, status);
    reordered.releaseBuffer(size);
    ubidi_close(bidi);

    if (U_FAILURE(*status)) {
        return dest;
    }

    // The type parameter is deprecated since ICU 58; any number may be passed.
    constexpr uint32_t deprecatedType = 58;
    return uspoof_getSkeletonUnicodeString(sc, deprecatedType, reordered, dest, status);
}



U_I18N_API UnicodeString &  U_EXPORT2
uspoof_getSkeletonUnicodeString(const USpoofChecker *sc,
                                uint32_t /*type*/,
                                const UnicodeString &id,
                                UnicodeString &dest,
                                UErrorCode *status) {
    const SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return dest;
    }

    UnicodeString nfdId;
    gNfdNormalizer->normalize(id, nfdId, *status);

    // Apply the skeleton mapping to the NFD normalized input string
    // Accumulate the skeleton, possibly unnormalized, in a UnicodeString.
    int32_t inputIndex = 0;
    UnicodeString skelStr;
    int32_t normalizedLen = nfdId.length();
    for (inputIndex=0; inputIndex < normalizedLen; ) {
        UChar32 c = nfdId.char32At(inputIndex);
        inputIndex += U16_LENGTH(c);
        if (!u_hasBinaryProperty(c, UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) {
            This->fSpoofData->confusableLookup(c, skelStr);
        }
    }

    gNfdNormalizer->normalize(skelStr, dest, *status);
    return dest;
}

U_CAPI int32_t U_EXPORT2 uspoof_getSkeletonUTF8(const USpoofChecker *sc, uint32_t type, const char *id,
                                                int32_t length, char *dest, int32_t destCapacity,
                       UErrorCode *status) {
    SpoofImpl::validateThis(sc, *status);
    if (U_FAILURE(*status)) {
        return 0;
    }
    if (length<-1 || destCapacity<0 || (destCapacity==0 && dest!=nullptr)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    UnicodeString srcStr = UnicodeString::fromUTF8(
        StringPiece(id, length >= 0 ? length : static_cast<int32_t>(uprv_strlen(id))));
    UnicodeString destStr;
    uspoof_getSkeletonUnicodeString(sc, type, srcStr, destStr, status);
    if (U_FAILURE(*status)) {
        return 0;
    }

    int32_t lengthInUTF8 = 0;
    u_strToUTF8(dest, destCapacity, &lengthInUTF8, destStr.getBuffer(), destStr.length(), status);
    return lengthInUTF8;
}

U_CAPI int32_t U_EXPORT2 uspoof_getBidiSkeletonUTF8(const USpoofChecker *sc, UBiDiDirection direction,
                                                    const char *id, int32_t length, char *dest,
                                                    int32_t destCapacity, UErrorCode *status) {
    if (length < -1) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    UnicodeString srcStr = UnicodeString::fromUTF8(
        StringPiece(id, length >= 0 ? length : static_cast<int32_t>(uprv_strlen(id))));
    UnicodeString destStr;
    uspoof_getBidiSkeletonUnicodeString(sc, direction, srcStr, destStr, status);
    if (U_FAILURE(*status)) {
        return 0;
    }

    int32_t lengthInUTF8 = 0;
    u_strToUTF8(dest, destCapacity, &lengthInUTF8, destStr.getBuffer(), destStr.length(), status);
    return lengthInUTF8;
}


U_CAPI int32_t U_EXPORT2
uspoof_serialize(USpoofChecker *sc,void *buf, int32_t capacity, UErrorCode *status) {
    SpoofImpl *This = SpoofImpl::validateThis(sc, *status);
    if (This == nullptr) {
        U_ASSERT(U_FAILURE(*status));
        return 0;
    }

    return This->fSpoofData->serialize(buf, capacity, *status);
}

U_CAPI const USet * U_EXPORT2
uspoof_getInclusionSet(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    return gInclusionSet->toUSet();
}

U_CAPI const USet * U_EXPORT2
uspoof_getRecommendedSet(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    return gRecommendedSet->toUSet();
}

U_I18N_API const UnicodeSet * U_EXPORT2
uspoof_getInclusionUnicodeSet(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    return gInclusionSet;
}

U_I18N_API const UnicodeSet * U_EXPORT2
uspoof_getRecommendedUnicodeSet(UErrorCode *status) {
    umtx_initOnce(gSpoofInitStaticsOnce, &initializeStatics, *status);
    return gRecommendedSet;
}

//------------------
// CheckResult APIs
//------------------

U_CAPI USpoofCheckResult* U_EXPORT2
uspoof_openCheckResult(UErrorCode *status) {
    CheckResult* checkResult = new CheckResult();
    if (checkResult == nullptr) {
        *status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return checkResult->asUSpoofCheckResult();
}

U_CAPI void U_EXPORT2
uspoof_closeCheckResult(USpoofCheckResult* checkResult) {
    UErrorCode status = U_ZERO_ERROR;
    CheckResult* This = CheckResult::validateThis(checkResult, status);
    delete This;
}

U_CAPI int32_t U_EXPORT2
uspoof_getCheckResultChecks(const USpoofCheckResult *checkResult, UErrorCode *status) {
    const CheckResult* This = CheckResult::validateThis(checkResult, *status);
    if (U_FAILURE(*status)) { return 0; }
    return This->fChecks;
}

U_CAPI URestrictionLevel U_EXPORT2
uspoof_getCheckResultRestrictionLevel(const USpoofCheckResult *checkResult, UErrorCode *status) {
    const CheckResult* This = CheckResult::validateThis(checkResult, *status);
    if (U_FAILURE(*status)) { return USPOOF_UNRESTRICTIVE; }
    return This->fRestrictionLevel;
}

U_CAPI const USet* U_EXPORT2
uspoof_getCheckResultNumerics(const USpoofCheckResult *checkResult, UErrorCode *status) {
    const CheckResult* This = CheckResult::validateThis(checkResult, *status);
    if (U_FAILURE(*status)) { return nullptr; }
    return This->fNumerics.toUSet();
}



#endif // !UCONFIG_NO_NORMALIZATION
