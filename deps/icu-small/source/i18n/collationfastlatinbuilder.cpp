// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationfastlatinbuilder.cpp
*
* created on: 2013aug09
* created by: Markus W. Scherer
*/

#define DEBUG_COLLATION_FAST_LATIN_BUILDER 0  // 0 or 1 or 2
#if DEBUG_COLLATION_FAST_LATIN_BUILDER
#include <stdio.h>
#include <string>
#endif

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "unicode/ucharstrie.h"
#include "unicode/unistr.h"
#include "unicode/uobject.h"
#include "unicode/uscript.h"
#include "cmemory.h"
#include "collation.h"
#include "collationdata.h"
#include "collationfastlatin.h"
#include "collationfastlatinbuilder.h"
#include "uassert.h"
#include "uvectr64.h"

U_NAMESPACE_BEGIN

struct CollationData;

namespace {

/**
 * Compare two signed int64_t values as if they were unsigned.
 */
int32_t
compareInt64AsUnsigned(int64_t a, int64_t b) {
    if (static_cast<uint64_t>(a) < static_cast<uint64_t>(b)) {
        return -1;
    } else if (static_cast<uint64_t>(a) > static_cast<uint64_t>(b)) {
        return 1;
    } else {
        return 0;
    }
}

// TODO: Merge this with the near-identical version in collationbasedatabuilder.cpp
/**
 * Like Java Collections.binarySearch(List, String, Comparator).
 *
 * @return the index>=0 where the item was found,
 *         or the index<0 for inserting the string at ~index in sorted order
 */
int32_t
binarySearch(const int64_t list[], int32_t limit, int64_t ce) {
    if (limit == 0) { return ~0; }
    int32_t start = 0;
    for (;;) {
        int32_t i = (start + limit) / 2;
        int32_t cmp = compareInt64AsUnsigned(ce, list[i]);
        if (cmp == 0) {
            return i;
        } else if (cmp < 0) {
            if (i == start) {
                return ~start;  // insert ce before i
            }
            limit = i;
        } else {
            if (i == start) {
                return ~(start + 1);  // insert ce after i
            }
            start = i;
        }
    }
}

}  // namespace

CollationFastLatinBuilder::CollationFastLatinBuilder(UErrorCode &errorCode)
        : ce0(0), ce1(0),
          contractionCEs(errorCode), uniqueCEs(errorCode),
          miniCEs(nullptr),
          firstDigitPrimary(0), firstLatinPrimary(0), lastLatinPrimary(0),
          firstShortPrimary(0), shortPrimaryOverflow(false),
          headerLength(0) {
}

CollationFastLatinBuilder::~CollationFastLatinBuilder() {
    uprv_free(miniCEs);
}

UBool
CollationFastLatinBuilder::forData(const CollationData &data, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    if(!result.isEmpty()) {  // This builder is not reusable.
        errorCode = U_INVALID_STATE_ERROR;
        return false;
    }
    if(!loadGroups(data, errorCode)) { return false; }

    // Fast handling of digits.
    firstShortPrimary = firstDigitPrimary;
    getCEs(data, errorCode);
    if(!encodeUniqueCEs(errorCode)) { return false; }
    if(shortPrimaryOverflow) {
        // Give digits long mini primaries,
        // so that there are more short primaries for letters.
        firstShortPrimary = firstLatinPrimary;
        resetCEs();
        getCEs(data, errorCode);
        if(!encodeUniqueCEs(errorCode)) { return false; }
    }
    // Note: If we still have a short-primary overflow but not a long-primary overflow,
    // then we could calculate how many more long primaries would fit,
    // and set the firstShortPrimary to that many after the current firstShortPrimary,
    // and try again.
    // However, this might only benefit the en_US_POSIX tailoring,
    // and it is simpler to suppress building fast Latin data for it in genrb,
    // or by returning false here if shortPrimaryOverflow.

    UBool ok = !shortPrimaryOverflow &&
            encodeCharCEs(errorCode) && encodeContractions(errorCode);
    contractionCEs.removeAllElements();  // might reduce heap memory usage
    uniqueCEs.removeAllElements();
    return ok;
}

UBool
CollationFastLatinBuilder::loadGroups(const CollationData &data, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    headerLength = 1 + NUM_SPECIAL_GROUPS;
    uint32_t r0 = (CollationFastLatin::VERSION << 8) | headerLength;
    result.append(static_cast<char16_t>(r0));
    // The first few reordering groups should be special groups
    // (space, punct, ..., digit) followed by Latn, then Grek and other scripts.
    for(int32_t i = 0; i < NUM_SPECIAL_GROUPS; ++i) {
        lastSpecialPrimaries[i] = data.getLastPrimaryForGroup(UCOL_REORDER_CODE_FIRST + i);
        if(lastSpecialPrimaries[i] == 0) {
            // missing data
            return false;
        }
        result.append(static_cast<char16_t>(0)); // reserve a slot for this group
    }

    firstDigitPrimary = data.getFirstPrimaryForGroup(UCOL_REORDER_CODE_DIGIT);
    firstLatinPrimary = data.getFirstPrimaryForGroup(USCRIPT_LATIN);
    lastLatinPrimary = data.getLastPrimaryForGroup(USCRIPT_LATIN);
    if(firstDigitPrimary == 0 || firstLatinPrimary == 0) {
        // missing data
        return false;
    }
    return true;
}

UBool
CollationFastLatinBuilder::inSameGroup(uint32_t p, uint32_t q) const {
    // Both or neither need to be encoded as short primaries,
    // so that we can test only one and use the same bit mask.
    if(p >= firstShortPrimary) {
        return q >= firstShortPrimary;
    } else if(q >= firstShortPrimary) {
        return false;
    }
    // Both or neither must be potentially-variable,
    // so that we can test only one and determine if both are variable.
    uint32_t lastVariablePrimary = lastSpecialPrimaries[NUM_SPECIAL_GROUPS - 1];
    if(p > lastVariablePrimary) {
        return q > lastVariablePrimary;
    } else if(q > lastVariablePrimary) {
        return false;
    }
    // Both will be encoded with long mini primaries.
    // They must be in the same special reordering group,
    // so that we can test only one and determine if both are variable.
    U_ASSERT(p != 0 && q != 0);
    for(int32_t i = 0;; ++i) {  // will terminate
        uint32_t lastPrimary = lastSpecialPrimaries[i];
        if(p <= lastPrimary) {
            return q <= lastPrimary;
        } else if(q <= lastPrimary) {
            return false;
        }
    }
}

void
CollationFastLatinBuilder::resetCEs() {
    contractionCEs.removeAllElements();
    uniqueCEs.removeAllElements();
    shortPrimaryOverflow = false;
    result.truncate(headerLength);
}

void
CollationFastLatinBuilder::getCEs(const CollationData &data, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    int32_t i = 0;
    for(char16_t c = 0;; ++i, ++c) {
        if(c == CollationFastLatin::LATIN_LIMIT) {
            c = CollationFastLatin::PUNCT_START;
        } else if(c == CollationFastLatin::PUNCT_LIMIT) {
            break;
        }
        const CollationData *d;
        uint32_t ce32 = data.getCE32(c);
        if(ce32 == Collation::FALLBACK_CE32) {
            d = data.base;
            ce32 = d->getCE32(c);
        } else {
            d = &data;
        }
        if(getCEsFromCE32(*d, c, ce32, errorCode)) {
            charCEs[i][0] = ce0;
            charCEs[i][1] = ce1;
            addUniqueCE(ce0, errorCode);
            addUniqueCE(ce1, errorCode);
        } else {
            // bail out for c
            charCEs[i][0] = ce0 = Collation::NO_CE;
            charCEs[i][1] = ce1 = 0;
        }
        if(c == 0 && !isContractionCharCE(ce0)) {
            // Always map U+0000 to a contraction.
            // Write a contraction list with only a default value if there is no real contraction.
            U_ASSERT(contractionCEs.isEmpty());
            addContractionEntry(CollationFastLatin::CONTR_CHAR_MASK, ce0, ce1, errorCode);
            charCEs[0][0] = (static_cast<int64_t>(Collation::NO_CE_PRIMARY) << 32) | CONTRACTION_FLAG;
            charCEs[0][1] = 0;
        }
    }
    // Terminate the last contraction list.
    contractionCEs.addElement(CollationFastLatin::CONTR_CHAR_MASK, errorCode);
}

UBool
CollationFastLatinBuilder::getCEsFromCE32(const CollationData &data, UChar32 c, uint32_t ce32,
                                          UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    ce32 = data.getFinalCE32(ce32);
    ce1 = 0;
    if(Collation::isSimpleOrLongCE32(ce32)) {
        ce0 = Collation::ceFromCE32(ce32);
    } else {
        switch(Collation::tagFromCE32(ce32)) {
        case Collation::LATIN_EXPANSION_TAG:
            ce0 = Collation::latinCE0FromCE32(ce32);
            ce1 = Collation::latinCE1FromCE32(ce32);
            break;
        case Collation::EXPANSION32_TAG: {
            const uint32_t *ce32s = data.ce32s + Collation::indexFromCE32(ce32);
            int32_t length = Collation::lengthFromCE32(ce32);
            if(length <= 2) {
                ce0 = Collation::ceFromCE32(ce32s[0]);
                if(length == 2) {
                    ce1 = Collation::ceFromCE32(ce32s[1]);
                }
                break;
            } else {
                return false;
            }
        }
        case Collation::EXPANSION_TAG: {
            const int64_t *ces = data.ces + Collation::indexFromCE32(ce32);
            int32_t length = Collation::lengthFromCE32(ce32);
            if(length <= 2) {
                ce0 = ces[0];
                if(length == 2) {
                    ce1 = ces[1];
                }
                break;
            } else {
                return false;
            }
        }
        // Note: We could support PREFIX_TAG (assert c>=0)
        // by recursing on its default CE32 and checking that none of the prefixes starts
        // with a fast Latin character.
        // However, currently (2013) there are only the L-before-middle-dot
        // prefix mappings in the Latin range, and those would be rejected anyway.
        case Collation::CONTRACTION_TAG:
            U_ASSERT(c >= 0);
            return getCEsFromContractionCE32(data, ce32, errorCode);
        case Collation::OFFSET_TAG:
            U_ASSERT(c >= 0);
            ce0 = data.getCEFromOffsetCE32(c, ce32);
            break;
        default:
            return false;
        }
    }
    // A mapping can be completely ignorable.
    if(ce0 == 0) { return ce1 == 0; }
    // We do not support an ignorable ce0 unless it is completely ignorable.
    uint32_t p0 = static_cast<uint32_t>(ce0 >> 32);
    if(p0 == 0) { return false; }
    // We only support primaries up to the Latin script.
    if(p0 > lastLatinPrimary) { return false; }
    // We support non-common secondary and case weights only together with short primaries.
    uint32_t lower32_0 = static_cast<uint32_t>(ce0);
    if(p0 < firstShortPrimary) {
        uint32_t sc0 = lower32_0 & Collation::SECONDARY_AND_CASE_MASK;
        if(sc0 != Collation::COMMON_SECONDARY_CE) { return false; }
    }
    // No below-common tertiary weights.
    if((lower32_0 & Collation::ONLY_TERTIARY_MASK) < Collation::COMMON_WEIGHT16) { return false; }
    if(ce1 != 0) {
        // Both primaries must be in the same group,
        // or both must get short mini primaries,
        // or a short-primary CE is followed by a secondary CE.
        // This is so that we can test the first primary and use the same mask for both,
        // and determine for both whether they are variable.
        uint32_t p1 = static_cast<uint32_t>(ce1 >> 32);
        if(p1 == 0 ? p0 < firstShortPrimary : !inSameGroup(p0, p1)) { return false; }
        uint32_t lower32_1 = static_cast<uint32_t>(ce1);
        // No tertiary CEs.
        if((lower32_1 >> 16) == 0) { return false; }
        // We support non-common secondary and case weights
        // only for secondary CEs or together with short primaries.
        if(p1 != 0 && p1 < firstShortPrimary) {
            uint32_t sc1 = lower32_1 & Collation::SECONDARY_AND_CASE_MASK;
            if(sc1 != Collation::COMMON_SECONDARY_CE) { return false; }
        }
        // No below-common tertiary weights.
        if((lower32_1 & Collation::ONLY_TERTIARY_MASK) < Collation::COMMON_WEIGHT16) { return false; }
    }
    // No quaternary weights.
    if(((ce0 | ce1) & Collation::QUATERNARY_MASK) != 0) { return false; }
    return true;
}

UBool
CollationFastLatinBuilder::getCEsFromContractionCE32(const CollationData &data, uint32_t ce32,
                                                     UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    const char16_t *p = data.contexts + Collation::indexFromCE32(ce32);
    ce32 = CollationData::readCE32(p);  // Default if no suffix match.
    // Since the original ce32 is not a prefix mapping,
    // the default ce32 must not be another contraction.
    U_ASSERT(!Collation::isContractionCE32(ce32));
    int32_t contractionIndex = contractionCEs.size();
    if(getCEsFromCE32(data, U_SENTINEL, ce32, errorCode)) {
        addContractionEntry(CollationFastLatin::CONTR_CHAR_MASK, ce0, ce1, errorCode);
    } else {
        // Bail out for c-without-contraction.
        addContractionEntry(CollationFastLatin::CONTR_CHAR_MASK, Collation::NO_CE, 0, errorCode);
    }
    // Handle an encodable contraction unless the next contraction is too long
    // and starts with the same character.
    int32_t prevX = -1;
    UBool addContraction = false;
    UCharsTrie::Iterator suffixes(p + 2, 0, errorCode);
    while(suffixes.next(errorCode)) {
        const UnicodeString &suffix = suffixes.getString();
        int32_t x = CollationFastLatin::getCharIndex(suffix.charAt(0));
        if(x < 0) { continue; }  // ignore anything but fast Latin text
        if(x == prevX) {
            if(addContraction) {
                // Bail out for all contractions starting with this character.
                addContractionEntry(x, Collation::NO_CE, 0, errorCode);
                addContraction = false;
            }
            continue;
        }
        if(addContraction) {
            addContractionEntry(prevX, ce0, ce1, errorCode);
        }
        ce32 = static_cast<uint32_t>(suffixes.getValue());
        if(suffix.length() == 1 && getCEsFromCE32(data, U_SENTINEL, ce32, errorCode)) {
            addContraction = true;
        } else {
            addContractionEntry(x, Collation::NO_CE, 0, errorCode);
            addContraction = false;
        }
        prevX = x;
    }
    if(addContraction) {
        addContractionEntry(prevX, ce0, ce1, errorCode);
    }
    if(U_FAILURE(errorCode)) { return false; }
    // Note: There might not be any fast Latin contractions, but
    // we need to enter contraction handling anyway so that we can bail out
    // when there is a non-fast-Latin character following.
    // For example: Danish &Y<<u+umlaut, when we compare Y vs. u\u0308 we need to see the
    // following umlaut and bail out, rather than return the difference of Y vs. u.
    ce0 = (static_cast<int64_t>(Collation::NO_CE_PRIMARY) << 32) | CONTRACTION_FLAG | contractionIndex;
    ce1 = 0;
    return true;
}

void
CollationFastLatinBuilder::addContractionEntry(int32_t x, int64_t cce0, int64_t cce1,
                                               UErrorCode &errorCode) {
    contractionCEs.addElement(x, errorCode);
    contractionCEs.addElement(cce0, errorCode);
    contractionCEs.addElement(cce1, errorCode);
    addUniqueCE(cce0, errorCode);
    addUniqueCE(cce1, errorCode);
}

void
CollationFastLatinBuilder::addUniqueCE(int64_t ce, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    if (ce == 0 || static_cast<uint32_t>(ce >> 32) == Collation::NO_CE_PRIMARY) { return; }
    ce &= ~static_cast<int64_t>(Collation::CASE_MASK); // blank out case bits
    int32_t i = binarySearch(uniqueCEs.getBuffer(), uniqueCEs.size(), ce);
    if(i < 0) {
        uniqueCEs.insertElementAt(ce, ~i, errorCode);
    }
}

uint32_t
CollationFastLatinBuilder::getMiniCE(int64_t ce) const {
    ce &= ~static_cast<int64_t>(Collation::CASE_MASK); // blank out case bits
    int32_t index = binarySearch(uniqueCEs.getBuffer(), uniqueCEs.size(), ce);
    U_ASSERT(index >= 0);
    return miniCEs[index];
}

UBool
CollationFastLatinBuilder::encodeUniqueCEs(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    uprv_free(miniCEs);
    miniCEs = static_cast<uint16_t*>(uprv_malloc(uniqueCEs.size() * 2));
    if(miniCEs == nullptr) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
    int32_t group = 0;
    uint32_t lastGroupPrimary = lastSpecialPrimaries[group];
    // The lowest unique CE must be at least a secondary CE.
    U_ASSERT(((uint32_t)uniqueCEs.elementAti(0) >> 16) != 0);
    uint32_t prevPrimary = 0;
    uint32_t prevSecondary = 0;
    uint32_t pri = 0;
    uint32_t sec = 0;
    uint32_t ter = CollationFastLatin::COMMON_TER;
    for(int32_t i = 0; i < uniqueCEs.size(); ++i) {
        int64_t ce = uniqueCEs.elementAti(i);
        // Note: At least one of the p/s/t weights changes from one unique CE to the next.
        // (uniqueCEs does not store case bits.)
        uint32_t p = static_cast<uint32_t>(ce >> 32);
        if(p != prevPrimary) {
            while(p > lastGroupPrimary) {
                U_ASSERT(pri <= CollationFastLatin::MAX_LONG);
                // Set the group's header entry to the
                // last "long primary" in or before the group.
                result.setCharAt(1 + group, static_cast<char16_t>(pri));
                if(++group < NUM_SPECIAL_GROUPS) {
                    lastGroupPrimary = lastSpecialPrimaries[group];
                } else {
                    lastGroupPrimary = 0xffffffff;
                    break;
                }
            }
            if(p < firstShortPrimary) {
                if(pri == 0) {
                    pri = CollationFastLatin::MIN_LONG;
                } else if(pri < CollationFastLatin::MAX_LONG) {
                    pri += CollationFastLatin::LONG_INC;
                } else {
#if DEBUG_COLLATION_FAST_LATIN_BUILDER
                    printf("long-primary overflow for %08x\n", p);
#endif
                    miniCEs[i] = CollationFastLatin::BAIL_OUT;
                    continue;
                }
            } else {
                if(pri < CollationFastLatin::MIN_SHORT) {
                    pri = CollationFastLatin::MIN_SHORT;
                } else if(pri < (CollationFastLatin::MAX_SHORT - CollationFastLatin::SHORT_INC)) {
                    // Reserve the highest primary weight for U+FFFF.
                    pri += CollationFastLatin::SHORT_INC;
                } else {
#if DEBUG_COLLATION_FAST_LATIN_BUILDER
                    printf("short-primary overflow for %08x\n", p);
#endif
                    shortPrimaryOverflow = true;
                    miniCEs[i] = CollationFastLatin::BAIL_OUT;
                    continue;
                }
            }
            prevPrimary = p;
            prevSecondary = Collation::COMMON_WEIGHT16;
            sec = CollationFastLatin::COMMON_SEC;
            ter = CollationFastLatin::COMMON_TER;
        }
        uint32_t lower32 = static_cast<uint32_t>(ce);
        uint32_t s = lower32 >> 16;
        if(s != prevSecondary) {
            if(pri == 0) {
                if(sec == 0) {
                    sec = CollationFastLatin::MIN_SEC_HIGH;
                } else if(sec < CollationFastLatin::MAX_SEC_HIGH) {
                    sec += CollationFastLatin::SEC_INC;
                } else {
                    miniCEs[i] = CollationFastLatin::BAIL_OUT;
                    continue;
                }
                prevSecondary = s;
                ter = CollationFastLatin::COMMON_TER;
            } else if(s < Collation::COMMON_WEIGHT16) {
                if(sec == CollationFastLatin::COMMON_SEC) {
                    sec = CollationFastLatin::MIN_SEC_BEFORE;
                } else if(sec < CollationFastLatin::MAX_SEC_BEFORE) {
                    sec += CollationFastLatin::SEC_INC;
                } else {
                    miniCEs[i] = CollationFastLatin::BAIL_OUT;
                    continue;
                }
            } else if(s == Collation::COMMON_WEIGHT16) {
                sec = CollationFastLatin::COMMON_SEC;
            } else {
                if(sec < CollationFastLatin::MIN_SEC_AFTER) {
                    sec = CollationFastLatin::MIN_SEC_AFTER;
                } else if(sec < CollationFastLatin::MAX_SEC_AFTER) {
                    sec += CollationFastLatin::SEC_INC;
                } else {
                    miniCEs[i] = CollationFastLatin::BAIL_OUT;
                    continue;
                }
            }
            prevSecondary = s;
            ter = CollationFastLatin::COMMON_TER;
        }
        U_ASSERT((lower32 & Collation::CASE_MASK) == 0);  // blanked out in uniqueCEs
        uint32_t t = lower32 & Collation::ONLY_TERTIARY_MASK;
        if(t > Collation::COMMON_WEIGHT16) {
            if(ter < CollationFastLatin::MAX_TER_AFTER) {
                ++ter;
            } else {
                miniCEs[i] = CollationFastLatin::BAIL_OUT;
                continue;
            }
        }
        if(CollationFastLatin::MIN_LONG <= pri && pri <= CollationFastLatin::MAX_LONG) {
            U_ASSERT(sec == CollationFastLatin::COMMON_SEC);
            miniCEs[i] = static_cast<uint16_t>(pri | ter);
        } else {
            miniCEs[i] = static_cast<uint16_t>(pri | sec | ter);
        }
    }
#if DEBUG_COLLATION_FAST_LATIN_BUILDER
    printf("last mini primary: %04x\n", pri);
#endif
#if DEBUG_COLLATION_FAST_LATIN_BUILDER >= 2
    for(int32_t i = 0; i < uniqueCEs.size(); ++i) {
        int64_t ce = uniqueCEs.elementAti(i);
        printf("unique CE 0x%016lx -> 0x%04x\n", ce, miniCEs[i]);
    }
#endif
    return U_SUCCESS(errorCode);
}

UBool
CollationFastLatinBuilder::encodeCharCEs(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    int32_t miniCEsStart = result.length();
    for(int32_t i = 0; i < CollationFastLatin::NUM_FAST_CHARS; ++i) {
        result.append(static_cast<char16_t>(0)); // initialize to completely ignorable
    }
    int32_t indexBase = result.length();
    for(int32_t i = 0; i < CollationFastLatin::NUM_FAST_CHARS; ++i) {
        int64_t ce = charCEs[i][0];
        if(isContractionCharCE(ce)) { continue; }  // defer contraction
        uint32_t miniCE = encodeTwoCEs(ce, charCEs[i][1]);
        if(miniCE > 0xffff) {
            // Note: There is a chance that this new expansion is the same as a previous one,
            // and if so, then we could reuse the other expansion.
            // However, that seems unlikely.
            int32_t expansionIndex = result.length() - indexBase;
            if (expansionIndex > static_cast<int32_t>(CollationFastLatin::INDEX_MASK)) {
                miniCE = CollationFastLatin::BAIL_OUT;
            } else {
                result.append(static_cast<char16_t>(miniCE >> 16)).append(static_cast<char16_t>(miniCE));
                miniCE = CollationFastLatin::EXPANSION | expansionIndex;
            }
        }
        result.setCharAt(miniCEsStart + i, static_cast<char16_t>(miniCE));
    }
    return U_SUCCESS(errorCode);
}

UBool
CollationFastLatinBuilder::encodeContractions(UErrorCode &errorCode) {
    // We encode all contraction lists so that the first word of a list
    // terminates the previous list, and we only need one additional terminator at the end.
    if(U_FAILURE(errorCode)) { return false; }
    int32_t indexBase = headerLength + CollationFastLatin::NUM_FAST_CHARS;
    int32_t firstContractionIndex = result.length();
    for(int32_t i = 0; i < CollationFastLatin::NUM_FAST_CHARS; ++i) {
        int64_t ce = charCEs[i][0];
        if(!isContractionCharCE(ce)) { continue; }
        int32_t contractionIndex = result.length() - indexBase;
        if (contractionIndex > static_cast<int32_t>(CollationFastLatin::INDEX_MASK)) {
            result.setCharAt(headerLength + i, CollationFastLatin::BAIL_OUT);
            continue;
        }
        UBool firstTriple = true;
        for (int32_t index = static_cast<int32_t>(ce) & 0x7fffffff;; index += 3) {
            int32_t x = static_cast<int32_t>(contractionCEs.elementAti(index));
            if (static_cast<uint32_t>(x) == CollationFastLatin::CONTR_CHAR_MASK && !firstTriple) { break; }
            int64_t cce0 = contractionCEs.elementAti(index + 1);
            int64_t cce1 = contractionCEs.elementAti(index + 2);
            uint32_t miniCE = encodeTwoCEs(cce0, cce1);
            if(miniCE == CollationFastLatin::BAIL_OUT) {
                result.append(static_cast<char16_t>(x | (1 << CollationFastLatin::CONTR_LENGTH_SHIFT)));
            } else if(miniCE <= 0xffff) {
                result.append(static_cast<char16_t>(x | (2 << CollationFastLatin::CONTR_LENGTH_SHIFT)));
                result.append(static_cast<char16_t>(miniCE));
            } else {
                result.append(static_cast<char16_t>(x | (3 << CollationFastLatin::CONTR_LENGTH_SHIFT)));
                result.append(static_cast<char16_t>(miniCE >> 16)).append(static_cast<char16_t>(miniCE));
            }
            firstTriple = false;
        }
        // Note: There is a chance that this new contraction list is the same as a previous one,
        // and if so, then we could truncate the result and reuse the other list.
        // However, that seems unlikely.
        result.setCharAt(headerLength + i,
                         static_cast<char16_t>(CollationFastLatin::CONTRACTION | contractionIndex));
    }
    if(result.length() > firstContractionIndex) {
        // Terminate the last contraction list.
        result.append(static_cast<char16_t>(CollationFastLatin::CONTR_CHAR_MASK));
    }
    if(result.isBogus()) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return false;
    }
#if DEBUG_COLLATION_FAST_LATIN_BUILDER
    printf("** fast Latin %d * 2 = %d bytes\n", result.length(), result.length() * 2);
    puts("   header & below-digit groups map");
    int32_t i = 0;
    for(; i < headerLength; ++i) {
        printf(" %04x", result[i]);
    }
    printf("\n   char mini CEs");
    U_ASSERT(CollationFastLatin::NUM_FAST_CHARS % 16 == 0);
    for(; i < indexBase; i += 16) {
        UChar32 c = i - headerLength;
        if(c >= CollationFastLatin::LATIN_LIMIT) {
            c = CollationFastLatin::PUNCT_START + c - CollationFastLatin::LATIN_LIMIT;
        }
        printf("\n %04x:", c);
        for(int32_t j = 0; j < 16; ++j) {
            printf(" %04x", result[i + j]);
        }
    }
    printf("\n   expansions & contractions");
    for(; i < result.length(); ++i) {
        if((i - indexBase) % 16 == 0) { puts(""); }
        printf(" %04x", result[i]);
    }
    puts("");
#endif
    return true;
}

uint32_t
CollationFastLatinBuilder::encodeTwoCEs(int64_t first, int64_t second) const {
    if(first == 0) {
        return 0;  // completely ignorable
    }
    if(first == Collation::NO_CE) {
        return CollationFastLatin::BAIL_OUT;
    }
    U_ASSERT((uint32_t)(first >> 32) != Collation::NO_CE_PRIMARY);

    uint32_t miniCE = getMiniCE(first);
    if(miniCE == CollationFastLatin::BAIL_OUT) { return miniCE; }
    if(miniCE >= CollationFastLatin::MIN_SHORT) {
        // Extract & copy the case bits.
        // Shift them from normal CE bits 15..14 to mini CE bits 4..3.
        uint32_t c = ((static_cast<uint32_t>(first) & Collation::CASE_MASK) >> (14 - 3));
        // Only in mini CEs: Ignorable case bits = 0, lowercase = 1.
        c += CollationFastLatin::LOWER_CASE;
        miniCE |= c;
    }
    if(second == 0) { return miniCE; }

    uint32_t miniCE1 = getMiniCE(second);
    if(miniCE1 == CollationFastLatin::BAIL_OUT) { return miniCE1; }

    uint32_t case1 = static_cast<uint32_t>(second) & Collation::CASE_MASK;
    if(miniCE >= CollationFastLatin::MIN_SHORT &&
            (miniCE & CollationFastLatin::SECONDARY_MASK) == CollationFastLatin::COMMON_SEC) {
        // Try to combine the two mini CEs into one.
        uint32_t sec1 = miniCE1 & CollationFastLatin::SECONDARY_MASK;
        uint32_t ter1 = miniCE1 & CollationFastLatin::TERTIARY_MASK;
        if(sec1 >= CollationFastLatin::MIN_SEC_HIGH && case1 == 0 &&
                ter1 == CollationFastLatin::COMMON_TER) {
            // sec1>=sec_high implies pri1==0.
            return (miniCE & ~CollationFastLatin::SECONDARY_MASK) | sec1;
        }
    }

    if(miniCE1 <= CollationFastLatin::SECONDARY_MASK || CollationFastLatin::MIN_SHORT <= miniCE1) {
        // Secondary CE, or a CE with a short primary, copy the case bits.
        case1 = (case1 >> (14 - 3)) + CollationFastLatin::LOWER_CASE;
        miniCE1 |= case1;
    }
    return (miniCE << 16) | miniCE1;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
