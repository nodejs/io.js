// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2016, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationfastlatinbuilder.h
*
* created on: 2013aug09
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONFASTLATINBUILDER_H__
#define __COLLATIONFASTLATINBUILDER_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "unicode/unistr.h"
#include "unicode/uobject.h"
#include "collation.h"
#include "collationfastlatin.h"
#include "uvectr64.h"

U_NAMESPACE_BEGIN

struct CollationData;

class U_I18N_API CollationFastLatinBuilder : public UObject {
public:
    CollationFastLatinBuilder(UErrorCode &errorCode);
    ~CollationFastLatinBuilder();

    UBool forData(const CollationData &data, UErrorCode &errorCode);

    const uint16_t *getTable() const {
        return reinterpret_cast<const uint16_t *>(result.getBuffer());
    }
    int32_t lengthOfTable() const { return result.length(); }

private:
    // space, punct, symbol, currency (not digit)
    enum { NUM_SPECIAL_GROUPS = UCOL_REORDER_CODE_CURRENCY - UCOL_REORDER_CODE_FIRST + 1 };

    UBool loadGroups(const CollationData &data, UErrorCode &errorCode);
    UBool inSameGroup(uint32_t p, uint32_t q) const;

    void resetCEs();
    void getCEs(const CollationData &data, UErrorCode &errorCode);
    UBool getCEsFromCE32(const CollationData &data, UChar32 c, uint32_t ce32,
                         UErrorCode &errorCode);
    UBool getCEsFromContractionCE32(const CollationData &data, uint32_t ce32,
                                    UErrorCode &errorCode);
    void addContractionEntry(int32_t x, int64_t cce0, int64_t cce1, UErrorCode &errorCode);
    void addUniqueCE(int64_t ce, UErrorCode &errorCode);
    uint32_t getMiniCE(int64_t ce) const;
    UBool encodeUniqueCEs(UErrorCode &errorCode);
    UBool encodeCharCEs(UErrorCode &errorCode);
    UBool encodeContractions(UErrorCode &errorCode);
    uint32_t encodeTwoCEs(int64_t first, int64_t second) const;

    static UBool isContractionCharCE(int64_t ce) {
        return static_cast<uint32_t>(ce >> 32) == Collation::NO_CE_PRIMARY && ce != Collation::NO_CE;
    }

    static const uint32_t CONTRACTION_FLAG = 0x80000000;

    // temporary "buffer"
    int64_t ce0, ce1;

    int64_t charCEs[CollationFastLatin::NUM_FAST_CHARS][2];

    UVector64 contractionCEs;
    UVector64 uniqueCEs;

    /** One 16-bit mini CE per unique CE. */
    uint16_t *miniCEs;

    // These are constant for a given root collator.
    uint32_t lastSpecialPrimaries[NUM_SPECIAL_GROUPS];
    uint32_t firstDigitPrimary;
    uint32_t firstLatinPrimary;
    uint32_t lastLatinPrimary;
    // This determines the first normal primary weight which is mapped to
    // a short mini primary. It must be >=firstDigitPrimary.
    uint32_t firstShortPrimary;

    UBool shortPrimaryOverflow;

    UnicodeString result;
    int32_t headerLength;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONFASTLATINBUILDER_H__
