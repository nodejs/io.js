// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2012-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationdata.cpp
*
* created on: 2012jul28
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "unicode/udata.h"
#include "unicode/uscript.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "uassert.h"
#include "utrie2.h"
#include "uvectr32.h"

U_NAMESPACE_BEGIN

uint32_t
CollationData::getIndirectCE32(uint32_t ce32) const {
    U_ASSERT(Collation::isSpecialCE32(ce32));
    int32_t tag = Collation::tagFromCE32(ce32);
    if(tag == Collation::DIGIT_TAG) {
        // Fetch the non-numeric-collation CE32.
        ce32 = ce32s[Collation::indexFromCE32(ce32)];
    } else if(tag == Collation::LEAD_SURROGATE_TAG) {
        ce32 = Collation::UNASSIGNED_CE32;
    } else if(tag == Collation::U0000_TAG) {
        // Fetch the normal ce32 for U+0000.
        ce32 = ce32s[0];
    }
    return ce32;
}

uint32_t
CollationData::getFinalCE32(uint32_t ce32) const {
    if(Collation::isSpecialCE32(ce32)) {
        ce32 = getIndirectCE32(ce32);
    }
    return ce32;
}

int64_t
CollationData::getSingleCE(UChar32 c, UErrorCode &errorCode) const {
    if(U_FAILURE(errorCode)) { return 0; }
    // Keep parallel with CollationDataBuilder::getSingleCE().
    const CollationData *d;
    uint32_t ce32 = getCE32(c);
    if(ce32 == Collation::FALLBACK_CE32) {
        d = base;
        ce32 = base->getCE32(c);
    } else {
        d = this;
    }
    while(Collation::isSpecialCE32(ce32)) {
        switch(Collation::tagFromCE32(ce32)) {
        case Collation::LATIN_EXPANSION_TAG:
        case Collation::BUILDER_DATA_TAG:
        case Collation::PREFIX_TAG:
        case Collation::CONTRACTION_TAG:
        case Collation::HANGUL_TAG:
        case Collation::LEAD_SURROGATE_TAG:
            errorCode = U_UNSUPPORTED_ERROR;
            return 0;
        case Collation::FALLBACK_TAG:
        case Collation::RESERVED_TAG_3:
            errorCode = U_INTERNAL_PROGRAM_ERROR;
            return 0;
        case Collation::LONG_PRIMARY_TAG:
            return Collation::ceFromLongPrimaryCE32(ce32);
        case Collation::LONG_SECONDARY_TAG:
            return Collation::ceFromLongSecondaryCE32(ce32);
        case Collation::EXPANSION32_TAG:
            if(Collation::lengthFromCE32(ce32) == 1) {
                ce32 = d->ce32s[Collation::indexFromCE32(ce32)];
                break;
            } else {
                errorCode = U_UNSUPPORTED_ERROR;
                return 0;
            }
        case Collation::EXPANSION_TAG: {
            if(Collation::lengthFromCE32(ce32) == 1) {
                return d->ces[Collation::indexFromCE32(ce32)];
            } else {
                errorCode = U_UNSUPPORTED_ERROR;
                return 0;
            }
        }
        case Collation::DIGIT_TAG:
            // Fetch the non-numeric-collation CE32 and continue.
            ce32 = d->ce32s[Collation::indexFromCE32(ce32)];
            break;
        case Collation::U0000_TAG:
            U_ASSERT(c == 0);
            // Fetch the normal ce32 for U+0000 and continue.
            ce32 = d->ce32s[0];
            break;
        case Collation::OFFSET_TAG:
            return d->getCEFromOffsetCE32(c, ce32);
        case Collation::IMPLICIT_TAG:
            return Collation::unassignedCEFromCodePoint(c);
        }
    }
    return Collation::ceFromSimpleCE32(ce32);
}

uint32_t
CollationData::getFirstPrimaryForGroup(int32_t script) const {
    int32_t index = getScriptIndex(script);
    return index == 0 ? 0 : static_cast<uint32_t>(scriptStarts[index]) << 16;
}

uint32_t
CollationData::getLastPrimaryForGroup(int32_t script) const {
    int32_t index = getScriptIndex(script);
    if(index == 0) {
        return 0;
    }
    uint32_t limit = scriptStarts[index + 1];
    return (limit << 16) - 1;
}

int32_t
CollationData::getGroupForPrimary(uint32_t p) const {
    p >>= 16;
    if(p < scriptStarts[1] || scriptStarts[scriptStartsLength - 1] <= p) {
        return -1;
    }
    int32_t index = 1;
    while(p >= scriptStarts[index + 1]) { ++index; }
    for(int32_t i = 0; i < numScripts; ++i) {
        if(scriptsIndex[i] == index) {
            return i;
        }
    }
    for(int32_t i = 0; i < MAX_NUM_SPECIAL_REORDER_CODES; ++i) {
        if(scriptsIndex[numScripts + i] == index) {
            return UCOL_REORDER_CODE_FIRST + i;
        }
    }
    return -1;
}

int32_t
CollationData::getScriptIndex(int32_t script) const {
    if(script < 0) {
        return 0;
    } else if(script < numScripts) {
        return scriptsIndex[script];
    } else if(script < UCOL_REORDER_CODE_FIRST) {
        return 0;
    } else {
        script -= UCOL_REORDER_CODE_FIRST;
        if(script < MAX_NUM_SPECIAL_REORDER_CODES) {
            return scriptsIndex[numScripts + script];
        } else {
            return 0;
        }
    }
}

int32_t
CollationData::getEquivalentScripts(int32_t script,
                                    int32_t dest[], int32_t capacity,
                                    UErrorCode &errorCode) const {
    if(U_FAILURE(errorCode)) { return 0; }
    int32_t index = getScriptIndex(script);
    if(index == 0) { return 0; }
    if(script >= UCOL_REORDER_CODE_FIRST) {
        // Special groups have no aliases.
        if(capacity > 0) {
            dest[0] = script;
        } else {
            errorCode = U_BUFFER_OVERFLOW_ERROR;
        }
        return 1;
    }

    int32_t length = 0;
    for(int32_t i = 0; i < numScripts; ++i) {
        if(scriptsIndex[i] == index) {
            if(length < capacity) {
                dest[length] = i;
            }
            ++length;
        }
    }
    if(length > capacity) {
        errorCode = U_BUFFER_OVERFLOW_ERROR;
    }
    return length;
}

void
CollationData::makeReorderRanges(const int32_t *reorder, int32_t length,
                                 UVector32 &ranges, UErrorCode &errorCode) const {
    makeReorderRanges(reorder, length, false, ranges, errorCode);
}

void
CollationData::makeReorderRanges(const int32_t *reorder, int32_t length,
                                 UBool latinMustMove,
                                 UVector32 &ranges, UErrorCode &errorCode) const {
    if(U_FAILURE(errorCode)) { return; }
    ranges.removeAllElements();
    if(length == 0 || (length == 1 && reorder[0] == USCRIPT_UNKNOWN)) {
        return;
    }

    // Maps each script-or-group range to a new lead byte.
    uint8_t table[MAX_NUM_SCRIPT_RANGES];
    uprv_memset(table, 0, sizeof(table));

    {
        // Set "don't care" values for reserved ranges.
        int32_t index = scriptsIndex[
                numScripts + REORDER_RESERVED_BEFORE_LATIN - UCOL_REORDER_CODE_FIRST];
        if(index != 0) {
            table[index] = 0xff;
        }
        index = scriptsIndex[
                numScripts + REORDER_RESERVED_AFTER_LATIN - UCOL_REORDER_CODE_FIRST];
        if(index != 0) {
            table[index] = 0xff;
        }
    }

    // Never reorder special low and high primary lead bytes.
    U_ASSERT(scriptStartsLength >= 2);
    U_ASSERT(scriptStarts[0] == 0);
    int32_t lowStart = scriptStarts[1];
    U_ASSERT(lowStart == ((Collation::MERGE_SEPARATOR_BYTE + 1) << 8));
    int32_t highLimit = scriptStarts[scriptStartsLength - 1];
    U_ASSERT(highLimit == (Collation::TRAIL_WEIGHT_BYTE << 8));

    // Get the set of special reorder codes in the input list.
    // This supports a fixed number of special reorder codes;
    // it works for data with codes beyond UCOL_REORDER_CODE_LIMIT.
    uint32_t specials = 0;
    for(int32_t i = 0; i < length; ++i) {
        int32_t reorderCode = reorder[i] - UCOL_REORDER_CODE_FIRST;
        if(0 <= reorderCode && reorderCode < MAX_NUM_SPECIAL_REORDER_CODES) {
            specials |= static_cast<uint32_t>(1) << reorderCode;
        }
    }

    // Start the reordering with the special low reorder codes that do not occur in the input.
    for(int32_t i = 0; i < MAX_NUM_SPECIAL_REORDER_CODES; ++i) {
        int32_t index = scriptsIndex[numScripts + i];
        if (index != 0 && (specials & (static_cast<uint32_t>(1) << i)) == 0) {
            lowStart = addLowScriptRange(table, index, lowStart);
        }
    }

    // Skip the reserved range before Latin if Latin is the first script,
    // so that we do not move it unnecessarily.
    int32_t skippedReserved = 0;
    if(specials == 0 && reorder[0] == USCRIPT_LATIN && !latinMustMove) {
        int32_t index = scriptsIndex[USCRIPT_LATIN];
        U_ASSERT(index != 0);
        int32_t start = scriptStarts[index];
        U_ASSERT(lowStart <= start);
        skippedReserved = start - lowStart;
        lowStart = start;
    }

    // Reorder according to the input scripts, continuing from the bottom of the primary range.
    int32_t originalLength = length;  // length will be decremented if "others" is in the list.
    UBool hasReorderToEnd = false;
    for(int32_t i = 0; i < length;) {
        int32_t script = reorder[i++];
        if(script == USCRIPT_UNKNOWN) {
            // Put the remaining scripts at the top.
            hasReorderToEnd = true;
            while(i < length) {
                script = reorder[--length];
                if(script == USCRIPT_UNKNOWN ||  // Must occur at most once.
                        script == UCOL_REORDER_CODE_DEFAULT) {
                    errorCode = U_ILLEGAL_ARGUMENT_ERROR;
                    return;
                }
                int32_t index = getScriptIndex(script);
                if(index == 0) { continue; }
                if(table[index] != 0) {  // Duplicate or equivalent script.
                    errorCode = U_ILLEGAL_ARGUMENT_ERROR;
                    return;
                }
                highLimit = addHighScriptRange(table, index, highLimit);
            }
            break;
        }
        if(script == UCOL_REORDER_CODE_DEFAULT) {
            // The default code must be the only one in the list, and that is handled by the caller.
            // Otherwise it must not be used.
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        int32_t index = getScriptIndex(script);
        if(index == 0) { continue; }
        if(table[index] != 0) {  // Duplicate or equivalent script.
            errorCode = U_ILLEGAL_ARGUMENT_ERROR;
            return;
        }
        lowStart = addLowScriptRange(table, index, lowStart);
    }

    // Put all remaining scripts into the middle.
    for(int32_t i = 1; i < scriptStartsLength - 1; ++i) {
        int32_t leadByte = table[i];
        if(leadByte != 0) { continue; }
        int32_t start = scriptStarts[i];
        if(!hasReorderToEnd && start > lowStart) {
            // No need to move this script.
            lowStart = start;
        }
        lowStart = addLowScriptRange(table, i, lowStart);
    }
    if(lowStart > highLimit) {
        if((lowStart - (skippedReserved & 0xff00)) <= highLimit) {
            // Try not skipping the before-Latin reserved range.
            makeReorderRanges(reorder, originalLength, true, ranges, errorCode);
            return;
        }
        // We need more primary lead bytes than available, despite the reserved ranges.
        errorCode = U_BUFFER_OVERFLOW_ERROR;
        return;
    }

    // Turn lead bytes into a list of (limit, offset) pairs.
    // Encode each pair in one list element:
    // Upper 16 bits = limit, lower 16 = signed lead byte offset.
    int32_t offset = 0;
    for(int32_t i = 1;; ++i) {
        int32_t nextOffset = offset;
        while(i < scriptStartsLength - 1) {
            int32_t newLeadByte = table[i];
            if(newLeadByte == 0xff) {
                // "Don't care" lead byte for reserved range, continue with current offset.
            } else {
                nextOffset = newLeadByte - (scriptStarts[i] >> 8);
                if(nextOffset != offset) { break; }
            }
            ++i;
        }
        if(offset != 0 || i < scriptStartsLength - 1) {
            ranges.addElement((static_cast<int32_t>(scriptStarts[i]) << 16) | (offset & 0xffff), errorCode);
        }
        if(i == scriptStartsLength - 1) { break; }
        offset = nextOffset;
    }
}

int32_t
CollationData::addLowScriptRange(uint8_t table[], int32_t index, int32_t lowStart) const {
    int32_t start = scriptStarts[index];
    if((start & 0xff) < (lowStart & 0xff)) {
        lowStart += 0x100;
    }
    table[index] = static_cast<uint8_t>(lowStart >> 8);
    int32_t limit = scriptStarts[index + 1];
    lowStart = ((lowStart & 0xff00) + ((limit & 0xff00) - (start & 0xff00))) | (limit & 0xff);
    return lowStart;
}

int32_t
CollationData::addHighScriptRange(uint8_t table[], int32_t index, int32_t highLimit) const {
    int32_t limit = scriptStarts[index + 1];
    if((limit & 0xff) > (highLimit & 0xff)) {
        highLimit -= 0x100;
    }
    int32_t start = scriptStarts[index];
    highLimit = ((highLimit & 0xff00) - ((limit & 0xff00) - (start & 0xff00))) | (start & 0xff);
    table[index] = static_cast<uint8_t>(highLimit >> 8);
    return highLimit;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
