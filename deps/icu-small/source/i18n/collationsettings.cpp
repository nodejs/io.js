// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationsettings.cpp
*
* created on: 2013feb07
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "collationsettings.h"
#include "sharedobject.h"
#include "uassert.h"
#include "umutex.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN

CollationSettings::CollationSettings(const CollationSettings &other)
        : SharedObject(other),
          options(other.options), variableTop(other.variableTop),
          reorderTable(nullptr),
          minHighNoReorder(other.minHighNoReorder),
          reorderRanges(nullptr), reorderRangesLength(0),
          reorderCodes(nullptr), reorderCodesLength(0), reorderCodesCapacity(0),
          fastLatinOptions(other.fastLatinOptions) {
    UErrorCode errorCode = U_ZERO_ERROR;
    copyReorderingFrom(other, errorCode);
    if(fastLatinOptions >= 0) {
        uprv_memcpy(fastLatinPrimaries, other.fastLatinPrimaries, sizeof(fastLatinPrimaries));
    }
}

CollationSettings::~CollationSettings() {
    if(reorderCodesCapacity != 0) {
        uprv_free(const_cast<int32_t *>(reorderCodes));
    }
}

bool
CollationSettings::operator==(const CollationSettings &other) const {
    if(options != other.options) { return false; }
    if((options & ALTERNATE_MASK) != 0 && variableTop != other.variableTop) { return false; }
    if(reorderCodesLength != other.reorderCodesLength) { return false; }
    for(int32_t i = 0; i < reorderCodesLength; ++i) {
        if(reorderCodes[i] != other.reorderCodes[i]) { return false; }
    }
    return true;
}

int32_t
CollationSettings::hashCode() const {
    int32_t h = options << 8;
    if((options & ALTERNATE_MASK) != 0) { h ^= variableTop; }
    h ^= reorderCodesLength;
    for(int32_t i = 0; i < reorderCodesLength; ++i) {
        h ^= (reorderCodes[i] << i);
    }
    return h;
}

void
CollationSettings::resetReordering() {
    // When we turn off reordering, we want to set a nullptr permutation
    // rather than a no-op permutation.
    // Keep the memory via reorderCodes and its capacity.
    reorderTable = nullptr;
    minHighNoReorder = 0;
    reorderRangesLength = 0;
    reorderCodesLength = 0;
}

void
CollationSettings::aliasReordering(const CollationData &data, const int32_t *codes, int32_t length,
                                   const uint32_t *ranges, int32_t rangesLength,
                                   const uint8_t *table, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    if(table != nullptr &&
            (rangesLength == 0 ?
                    !reorderTableHasSplitBytes(table) :
                    rangesLength >= 2 &&
                    // The first offset must be 0. The last offset must not be 0.
                    (ranges[0] & 0xffff) == 0 && (ranges[rangesLength - 1] & 0xffff) != 0)) {
        // We need to release the memory before setting the alias pointer.
        if(reorderCodesCapacity != 0) {
            uprv_free(const_cast<int32_t *>(reorderCodes));
            reorderCodesCapacity = 0;
        }
        reorderTable = table;
        reorderCodes = codes;
        reorderCodesLength = length;
        // Drop ranges before the first split byte. They are reordered by the table.
        // This then speeds up reordering of the remaining ranges.
        int32_t firstSplitByteRangeIndex = 0;
        while(firstSplitByteRangeIndex < rangesLength &&
                (ranges[firstSplitByteRangeIndex] & 0xff0000) == 0) {
            // The second byte of the primary limit is 0.
            ++firstSplitByteRangeIndex;
        }
        if(firstSplitByteRangeIndex == rangesLength) {
            U_ASSERT(!reorderTableHasSplitBytes(table));
            minHighNoReorder = 0;
            reorderRanges = nullptr;
            reorderRangesLength = 0;
        } else {
            U_ASSERT(table[ranges[firstSplitByteRangeIndex] >> 24] == 0);
            minHighNoReorder = ranges[rangesLength - 1] & 0xffff0000;
            reorderRanges = ranges + firstSplitByteRangeIndex;
            reorderRangesLength = rangesLength - firstSplitByteRangeIndex;
        }
        return;
    }
    // Regenerate missing data.
    setReordering(data, codes, length, errorCode);
}

void
CollationSettings::setReordering(const CollationData &data,
                                 const int32_t *codes, int32_t codesLength,
                                 UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    if(codesLength == 0 || (codesLength == 1 && codes[0] == UCOL_REORDER_CODE_NONE)) {
        resetReordering();
        return;
    }
    UVector32 rangesList(errorCode);
    data.makeReorderRanges(codes, codesLength, rangesList, errorCode);
    if(U_FAILURE(errorCode)) { return; }
    int32_t rangesLength = rangesList.size();
    if(rangesLength == 0) {
        resetReordering();
        return;
    }
    const uint32_t *ranges = reinterpret_cast<uint32_t *>(rangesList.getBuffer());
    // ranges[] contains at least two (limit, offset) pairs.
    // The first offset must be 0. The last offset must not be 0.
    // Separators (at the low end) and trailing weights (at the high end)
    // are never reordered.
    U_ASSERT(rangesLength >= 2);
    U_ASSERT((ranges[0] & 0xffff) == 0 && (ranges[rangesLength - 1] & 0xffff) != 0);
    minHighNoReorder = ranges[rangesLength - 1] & 0xffff0000;

    // Write the lead byte permutation table.
    // Set a 0 for each lead byte that has a range boundary in the middle.
    uint8_t table[256];
    int32_t b = 0;
    int32_t firstSplitByteRangeIndex = -1;
    for(int32_t i = 0; i < rangesLength; ++i) {
        uint32_t pair = ranges[i];
        int32_t limit1 = static_cast<int32_t>(pair >> 24);
        while(b < limit1) {
            table[b] = static_cast<uint8_t>(b + pair);
            ++b;
        }
        // Check the second byte of the limit.
        if((pair & 0xff0000) != 0) {
            table[limit1] = 0;
            b = limit1 + 1;
            if(firstSplitByteRangeIndex < 0) {
                firstSplitByteRangeIndex = i;
            }
        }
    }
    while(b <= 0xff) {
        table[b] = static_cast<uint8_t>(b);
        ++b;
    }
    if(firstSplitByteRangeIndex < 0) {
        // The lead byte permutation table alone suffices for reordering.
        rangesLength = 0;
    } else {
        // Remove the ranges below the first split byte.
        ranges += firstSplitByteRangeIndex;
        rangesLength -= firstSplitByteRangeIndex;
    }
    setReorderArrays(codes, codesLength, ranges, rangesLength, table, errorCode);
}

void
CollationSettings::setReorderArrays(const int32_t *codes, int32_t codesLength,
                                    const uint32_t *ranges, int32_t rangesLength,
                                    const uint8_t *table, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t *ownedCodes;
    int32_t totalLength = codesLength + rangesLength;
    U_ASSERT(totalLength > 0);
    if(totalLength <= reorderCodesCapacity) {
        ownedCodes = const_cast<int32_t *>(reorderCodes);
    } else {
        // Allocate one memory block for the codes, the ranges, and the 16-aligned table.
        int32_t capacity = (totalLength + 3) & ~3;  // round up to a multiple of 4 ints
        ownedCodes = static_cast<int32_t*>(uprv_malloc(capacity * 4 + 256));
        if(ownedCodes == nullptr) {
            resetReordering();
            errorCode = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        if(reorderCodesCapacity != 0) {
            uprv_free(const_cast<int32_t *>(reorderCodes));
        }
        reorderCodes = ownedCodes;
        reorderCodesCapacity = capacity;
    }
    uprv_memcpy(ownedCodes + reorderCodesCapacity, table, 256);
    uprv_memcpy(ownedCodes, codes, codesLength * 4);
    uprv_memcpy(ownedCodes + codesLength, ranges, rangesLength * 4);
    reorderTable = reinterpret_cast<const uint8_t *>(reorderCodes + reorderCodesCapacity);
    reorderCodesLength = codesLength;
    reorderRanges = reinterpret_cast<uint32_t *>(ownedCodes) + codesLength;
    reorderRangesLength = rangesLength;
}

void
CollationSettings::copyReorderingFrom(const CollationSettings &other, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    if(!other.hasReordering()) {
        resetReordering();
        return;
    }
    minHighNoReorder = other.minHighNoReorder;
    if(other.reorderCodesCapacity == 0) {
        // The reorder arrays are aliased to memory-mapped data.
        reorderTable = other.reorderTable;
        reorderRanges = other.reorderRanges;
        reorderRangesLength = other.reorderRangesLength;
        reorderCodes = other.reorderCodes;
        reorderCodesLength = other.reorderCodesLength;
    } else {
        setReorderArrays(other.reorderCodes, other.reorderCodesLength,
                         other.reorderRanges, other.reorderRangesLength,
                         other.reorderTable, errorCode);
    }
}

UBool
CollationSettings::reorderTableHasSplitBytes(const uint8_t table[256]) {
    U_ASSERT(table[0] == 0);
    for(int32_t i = 1; i < 256; ++i) {
        if(table[i] == 0) {
            return true;
        }
    }
    return false;
}

uint32_t
CollationSettings::reorderEx(uint32_t p) const {
    if(p >= minHighNoReorder) { return p; }
    // Round up p so that its lower 16 bits are >= any offset bits.
    // Then compare q directly with (limit, offset) pairs.
    uint32_t q = p | 0xffff;
    uint32_t r;
    const uint32_t *ranges = reorderRanges;
    while(q >= (r = *ranges)) { ++ranges; }
    return p + (r << 24);
}

void
CollationSettings::setStrength(int32_t value, int32_t defaultOptions, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t noStrength = options & ~STRENGTH_MASK;
    switch(value) {
    case UCOL_PRIMARY:
    case UCOL_SECONDARY:
    case UCOL_TERTIARY:
    case UCOL_QUATERNARY:
    case UCOL_IDENTICAL:
        options = noStrength | (value << STRENGTH_SHIFT);
        break;
    case UCOL_DEFAULT:
        options = noStrength | (defaultOptions & STRENGTH_MASK);
        break;
    default:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        break;
    }
}

void
CollationSettings::setFlag(int32_t bit, UColAttributeValue value,
                           int32_t defaultOptions, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    switch(value) {
    case UCOL_ON:
        options |= bit;
        break;
    case UCOL_OFF:
        options &= ~bit;
        break;
    case UCOL_DEFAULT:
        options = (options & ~bit) | (defaultOptions & bit);
        break;
    default:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        break;
    }
}

void
CollationSettings::setCaseFirst(UColAttributeValue value,
                                int32_t defaultOptions, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t noCaseFirst = options & ~CASE_FIRST_AND_UPPER_MASK;
    switch(value) {
    case UCOL_OFF:
        options = noCaseFirst;
        break;
    case UCOL_LOWER_FIRST:
        options = noCaseFirst | CASE_FIRST;
        break;
    case UCOL_UPPER_FIRST:
        options = noCaseFirst | CASE_FIRST_AND_UPPER_MASK;
        break;
    case UCOL_DEFAULT:
        options = noCaseFirst | (defaultOptions & CASE_FIRST_AND_UPPER_MASK);
        break;
    default:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        break;
    }
}

void
CollationSettings::setAlternateHandling(UColAttributeValue value,
                                        int32_t defaultOptions, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t noAlternate = options & ~ALTERNATE_MASK;
    switch(value) {
    case UCOL_NON_IGNORABLE:
        options = noAlternate;
        break;
    case UCOL_SHIFTED:
        options = noAlternate | SHIFTED;
        break;
    case UCOL_DEFAULT:
        options = noAlternate | (defaultOptions & ALTERNATE_MASK);
        break;
    default:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        break;
    }
}

void
CollationSettings::setMaxVariable(int32_t value, int32_t defaultOptions, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t noMax = options & ~MAX_VARIABLE_MASK;
    switch(value) {
    case MAX_VAR_SPACE:
    case MAX_VAR_PUNCT:
    case MAX_VAR_SYMBOL:
    case MAX_VAR_CURRENCY:
        options = noMax | (value << MAX_VARIABLE_SHIFT);
        break;
    case UCOL_DEFAULT:
        options = noMax | (defaultOptions & MAX_VARIABLE_MASK);
        break;
    default:
        errorCode = U_ILLEGAL_ARGUMENT_ERROR;
        break;
    }
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
