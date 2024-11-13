// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationfastlatin.cpp
*
* created on: 2013aug18
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "collationdata.h"
#include "collationfastlatin.h"
#include "collationsettings.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

int32_t
CollationFastLatin::getOptions(const CollationData *data, const CollationSettings &settings,
                               uint16_t *primaries, int32_t capacity) {
    const uint16_t *table = data->fastLatinTable;
    if(table == nullptr) { return -1; }
    U_ASSERT(capacity == LATIN_LIMIT);
    if(capacity != LATIN_LIMIT) { return -1; }

    uint32_t miniVarTop;
    if((settings.options & CollationSettings::ALTERNATE_MASK) == 0) {
        // No mini primaries are variable, set a variableTop just below the
        // lowest long mini primary.
        miniVarTop = MIN_LONG - 1;
    } else {
        int32_t headerLength = *table & 0xff;
        int32_t i = 1 + settings.getMaxVariable();
        if(i >= headerLength) {
            return -1;  // variableTop >= digits, should not occur
        }
        miniVarTop = table[i];
    }

    UBool digitsAreReordered = false;
    if(settings.hasReordering()) {
        uint32_t prevStart = 0;
        uint32_t beforeDigitStart = 0;
        uint32_t digitStart = 0;
        uint32_t afterDigitStart = 0;
        for(int32_t group = UCOL_REORDER_CODE_FIRST;
                group < UCOL_REORDER_CODE_FIRST + CollationData::MAX_NUM_SPECIAL_REORDER_CODES;
                ++group) {
            uint32_t start = data->getFirstPrimaryForGroup(group);
            start = settings.reorder(start);
            if(group == UCOL_REORDER_CODE_DIGIT) {
                beforeDigitStart = prevStart;
                digitStart = start;
            } else if(start != 0) {
                if(start < prevStart) {
                    // The permutation affects the groups up to Latin.
                    return -1;
                }
                // In the future, there might be a special group between digits & Latin.
                if(digitStart != 0 && afterDigitStart == 0 && prevStart == beforeDigitStart) {
                    afterDigitStart = start;
                }
                prevStart = start;
            }
        }
        uint32_t latinStart = data->getFirstPrimaryForGroup(USCRIPT_LATIN);
        latinStart = settings.reorder(latinStart);
        if(latinStart < prevStart) {
            return -1;
        }
        if(afterDigitStart == 0) {
            afterDigitStart = latinStart;
        }
        if(!(beforeDigitStart < digitStart && digitStart < afterDigitStart)) {
            digitsAreReordered = true;
        }
    }

    table += (table[0] & 0xff);  // skip the header
    for(UChar32 c = 0; c < LATIN_LIMIT; ++c) {
        uint32_t p = table[c];
        if(p >= MIN_SHORT) {
            p &= SHORT_PRIMARY_MASK;
        } else if(p > miniVarTop) {
            p &= LONG_PRIMARY_MASK;
        } else {
            p = 0;
        }
        primaries[c] = static_cast<uint16_t>(p);
    }
    if(digitsAreReordered || (settings.options & CollationSettings::NUMERIC) != 0) {
        // Bail out for digits.
        for(UChar32 c = 0x30; c <= 0x39; ++c) { primaries[c] = 0; }
    }

    // Shift the miniVarTop above other options.
    return (static_cast<int32_t>(miniVarTop) << 16) | settings.options;
}

int32_t
CollationFastLatin::compareUTF16(const uint16_t *table, const uint16_t *primaries, int32_t options,
                                 const char16_t *left, int32_t leftLength,
                                 const char16_t *right, int32_t rightLength) {
    // This is a modified copy of CollationCompare::compareUpToQuaternary(),
    // optimized for common Latin text.
    // Keep them in sync!
    // Keep compareUTF16() and compareUTF8() in sync very closely!

    U_ASSERT((table[0] >> 8) == VERSION);
    table += (table[0] & 0xff);  // skip the header
    uint32_t variableTop = static_cast<uint32_t>(options) >> 16; // see getOptions()
    options &= 0xffff;  // needed for CollationSettings::getStrength() to work

    // Check for supported characters, fetch mini CEs, and compare primaries.
    int32_t leftIndex = 0, rightIndex = 0;
    /**
     * Single mini CE or a pair.
     * The current mini CE is in the lower 16 bits, the next one is in the upper 16 bits.
     * If there is only one, then it is in the lower bits, and the upper bits are 0.
     */
    uint32_t leftPair = 0, rightPair = 0;
    for(;;) {
        // We fetch CEs until we get a non-ignorable primary or reach the end.
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            if(c <= LATIN_MAX) {
                leftPair = primaries[c];
                if(leftPair != 0) { break; }
                if(c <= 0x39 && c >= 0x30 && (options & CollationSettings::NUMERIC) != 0) {
                    return BAIL_OUT_RESULT;
                }
                leftPair = table[c];
            } else if(PUNCT_START <= c && c < PUNCT_LIMIT) {
                leftPair = table[c - PUNCT_START + LATIN_LIMIT];
            } else {
                leftPair = lookup(table, c);
            }
            if(leftPair >= MIN_SHORT) {
                leftPair &= SHORT_PRIMARY_MASK;
                break;
            } else if(leftPair > variableTop) {
                leftPair &= LONG_PRIMARY_MASK;
                break;
            } else {
                leftPair = nextPair(table, c, leftPair, left, nullptr, leftIndex, leftLength);
                if(leftPair == BAIL_OUT) { return BAIL_OUT_RESULT; }
                leftPair = getPrimaries(variableTop, leftPair);
            }
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            if(c <= LATIN_MAX) {
                rightPair = primaries[c];
                if(rightPair != 0) { break; }
                if(c <= 0x39 && c >= 0x30 && (options & CollationSettings::NUMERIC) != 0) {
                    return BAIL_OUT_RESULT;
                }
                rightPair = table[c];
            } else if(PUNCT_START <= c && c < PUNCT_LIMIT) {
                rightPair = table[c - PUNCT_START + LATIN_LIMIT];
            } else {
                rightPair = lookup(table, c);
            }
            if(rightPair >= MIN_SHORT) {
                rightPair &= SHORT_PRIMARY_MASK;
                break;
            } else if(rightPair > variableTop) {
                rightPair &= LONG_PRIMARY_MASK;
                break;
            } else {
                rightPair = nextPair(table, c, rightPair, right, nullptr, rightIndex, rightLength);
                if(rightPair == BAIL_OUT) { return BAIL_OUT_RESULT; }
                rightPair = getPrimaries(variableTop, rightPair);
            }
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftPrimary = leftPair & 0xffff;
        uint32_t rightPrimary = rightPair & 0xffff;
        if(leftPrimary != rightPrimary) {
            // Return the primary difference.
            return (leftPrimary < rightPrimary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    // In the following, we need to re-fetch each character because we did not buffer the CEs,
    // but we know that the string is well-formed and
    // only contains supported characters and mappings.

    // We might skip the secondary level but continue with the case level
    // which is turned on separately.
    if(CollationSettings::getStrength(options) >= UCOL_SECONDARY) {
        leftIndex = rightIndex = 0;
        leftPair = rightPair = 0;
        for(;;) {
            while(leftPair == 0) {
                if(leftIndex == leftLength) {
                    leftPair = EOS;
                    break;
                }
                UChar32 c = left[leftIndex++];
                if(c <= LATIN_MAX) {
                    leftPair = table[c];
                } else if(PUNCT_START <= c && c < PUNCT_LIMIT) {
                    leftPair = table[c - PUNCT_START + LATIN_LIMIT];
                } else {
                    leftPair = lookup(table, c);
                }
                if(leftPair >= MIN_SHORT) {
                    leftPair = getSecondariesFromOneShortCE(leftPair);
                    break;
                } else if(leftPair > variableTop) {
                    leftPair = COMMON_SEC_PLUS_OFFSET;
                    break;
                } else {
                    leftPair = nextPair(table, c, leftPair, left, nullptr, leftIndex, leftLength);
                    leftPair = getSecondaries(variableTop, leftPair);
                }
            }

            while(rightPair == 0) {
                if(rightIndex == rightLength) {
                    rightPair = EOS;
                    break;
                }
                UChar32 c = right[rightIndex++];
                if(c <= LATIN_MAX) {
                    rightPair = table[c];
                } else if(PUNCT_START <= c && c < PUNCT_LIMIT) {
                    rightPair = table[c - PUNCT_START + LATIN_LIMIT];
                } else {
                    rightPair = lookup(table, c);
                }
                if(rightPair >= MIN_SHORT) {
                    rightPair = getSecondariesFromOneShortCE(rightPair);
                    break;
                } else if(rightPair > variableTop) {
                    rightPair = COMMON_SEC_PLUS_OFFSET;
                    break;
                } else {
                    rightPair = nextPair(table, c, rightPair, right, nullptr, rightIndex, rightLength);
                    rightPair = getSecondaries(variableTop, rightPair);
                }
            }

            if(leftPair == rightPair) {
                if(leftPair == EOS) { break; }
                leftPair = rightPair = 0;
                continue;
            }
            uint32_t leftSecondary = leftPair & 0xffff;
            uint32_t rightSecondary = rightPair & 0xffff;
            if(leftSecondary != rightSecondary) {
                if((options & CollationSettings::BACKWARD_SECONDARY) != 0) {
                    // Full support for backwards secondary requires backwards contraction matching
                    // and moving backwards between merge separators.
                    return BAIL_OUT_RESULT;
                }
                return (leftSecondary < rightSecondary) ? UCOL_LESS : UCOL_GREATER;
            }
            if(leftPair == EOS) { break; }
            leftPair >>= 16;
            rightPair >>= 16;
        }
    }

    if((options & CollationSettings::CASE_LEVEL) != 0) {
        UBool strengthIsPrimary = CollationSettings::getStrength(options) == UCOL_PRIMARY;
        leftIndex = rightIndex = 0;
        leftPair = rightPair = 0;
        for(;;) {
            while(leftPair == 0) {
                if(leftIndex == leftLength) {
                    leftPair = EOS;
                    break;
                }
                UChar32 c = left[leftIndex++];
                leftPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
                if(leftPair < MIN_LONG) {
                    leftPair = nextPair(table, c, leftPair, left, nullptr, leftIndex, leftLength);
                }
                leftPair = getCases(variableTop, strengthIsPrimary, leftPair);
            }

            while(rightPair == 0) {
                if(rightIndex == rightLength) {
                    rightPair = EOS;
                    break;
                }
                UChar32 c = right[rightIndex++];
                rightPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
                if(rightPair < MIN_LONG) {
                    rightPair = nextPair(table, c, rightPair, right, nullptr, rightIndex, rightLength);
                }
                rightPair = getCases(variableTop, strengthIsPrimary, rightPair);
            }

            if(leftPair == rightPair) {
                if(leftPair == EOS) { break; }
                leftPair = rightPair = 0;
                continue;
            }
            uint32_t leftCase = leftPair & 0xffff;
            uint32_t rightCase = rightPair & 0xffff;
            if(leftCase != rightCase) {
                if((options & CollationSettings::UPPER_FIRST) == 0) {
                    return (leftCase < rightCase) ? UCOL_LESS : UCOL_GREATER;
                } else {
                    return (leftCase < rightCase) ? UCOL_GREATER : UCOL_LESS;
                }
            }
            if(leftPair == EOS) { break; }
            leftPair >>= 16;
            rightPair >>= 16;
        }
    }
    if(CollationSettings::getStrength(options) <= UCOL_SECONDARY) { return UCOL_EQUAL; }

    // Remove the case bits from the tertiary weight when caseLevel is on or caseFirst is off.
    UBool withCaseBits = CollationSettings::isTertiaryWithCaseBits(options);

    leftIndex = rightIndex = 0;
    leftPair = rightPair = 0;
    for(;;) {
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            leftPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
            if(leftPair < MIN_LONG) {
                leftPair = nextPair(table, c, leftPair, left, nullptr, leftIndex, leftLength);
            }
            leftPair = getTertiaries(variableTop, withCaseBits, leftPair);
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            rightPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
            if(rightPair < MIN_LONG) {
                rightPair = nextPair(table, c, rightPair, right, nullptr, rightIndex, rightLength);
            }
            rightPair = getTertiaries(variableTop, withCaseBits, rightPair);
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftTertiary = leftPair & 0xffff;
        uint32_t rightTertiary = rightPair & 0xffff;
        if(leftTertiary != rightTertiary) {
            if(CollationSettings::sortsTertiaryUpperCaseFirst(options)) {
                // Pass through EOS and MERGE_WEIGHT
                // and keep real tertiary weights larger than the MERGE_WEIGHT.
                // Tertiary CEs (secondary ignorables) are not supported in fast Latin.
                if(leftTertiary > MERGE_WEIGHT) {
                    leftTertiary ^= CASE_MASK;
                }
                if(rightTertiary > MERGE_WEIGHT) {
                    rightTertiary ^= CASE_MASK;
                }
            }
            return (leftTertiary < rightTertiary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    if(CollationSettings::getStrength(options) <= UCOL_TERTIARY) { return UCOL_EQUAL; }

    leftIndex = rightIndex = 0;
    leftPair = rightPair = 0;
    for(;;) {
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            leftPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
            if(leftPair < MIN_LONG) {
                leftPair = nextPair(table, c, leftPair, left, nullptr, leftIndex, leftLength);
            }
            leftPair = getQuaternaries(variableTop, leftPair);
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            rightPair = (c <= LATIN_MAX) ? table[c] : lookup(table, c);
            if(rightPair < MIN_LONG) {
                rightPair = nextPair(table, c, rightPair, right, nullptr, rightIndex, rightLength);
            }
            rightPair = getQuaternaries(variableTop, rightPair);
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftQuaternary = leftPair & 0xffff;
        uint32_t rightQuaternary = rightPair & 0xffff;
        if(leftQuaternary != rightQuaternary) {
            return (leftQuaternary < rightQuaternary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    return UCOL_EQUAL;
}

int32_t
CollationFastLatin::compareUTF8(const uint16_t *table, const uint16_t *primaries, int32_t options,
                                 const uint8_t *left, int32_t leftLength,
                                 const uint8_t *right, int32_t rightLength) {
    // Keep compareUTF16() and compareUTF8() in sync very closely!

    U_ASSERT((table[0] >> 8) == VERSION);
    table += (table[0] & 0xff);  // skip the header
    uint32_t variableTop = static_cast<uint32_t>(options) >> 16; // see RuleBasedCollator::getFastLatinOptions()
    options &= 0xffff;  // needed for CollationSettings::getStrength() to work

    // Check for supported characters, fetch mini CEs, and compare primaries.
    int32_t leftIndex = 0, rightIndex = 0;
    /**
     * Single mini CE or a pair.
     * The current mini CE is in the lower 16 bits, the next one is in the upper 16 bits.
     * If there is only one, then it is in the lower bits, and the upper bits are 0.
     */
    uint32_t leftPair = 0, rightPair = 0;
    // Note: There is no need to assemble the code point.
    // We only need to look up the table entry for the character,
    // and nextPair() looks for whether c==0.
    for(;;) {
        // We fetch CEs until we get a non-ignorable primary or reach the end.
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            uint8_t t;
            if(c <= 0x7f) {
                leftPair = primaries[c];
                if(leftPair != 0) { break; }
                if(c <= 0x39 && c >= 0x30 && (options & CollationSettings::NUMERIC) != 0) {
                    return BAIL_OUT_RESULT;
                }
                leftPair = table[c];
            } else if(c <= LATIN_MAX_UTF8_LEAD && 0xc2 <= c && leftIndex != leftLength &&
                    0x80 <= (t = left[leftIndex]) && t <= 0xbf) {
                ++leftIndex;
                c = ((c - 0xc2) << 6) + t;
                leftPair = primaries[c];
                if(leftPair != 0) { break; }
                leftPair = table[c];
            } else {
                leftPair = lookupUTF8(table, c, left, leftIndex, leftLength);
            }
            if(leftPair >= MIN_SHORT) {
                leftPair &= SHORT_PRIMARY_MASK;
                break;
            } else if(leftPair > variableTop) {
                leftPair &= LONG_PRIMARY_MASK;
                break;
            } else {
                leftPair = nextPair(table, c, leftPair, nullptr, left, leftIndex, leftLength);
                if(leftPair == BAIL_OUT) { return BAIL_OUT_RESULT; }
                leftPair = getPrimaries(variableTop, leftPair);
            }
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            uint8_t t;
            if(c <= 0x7f) {
                rightPair = primaries[c];
                if(rightPair != 0) { break; }
                if(c <= 0x39 && c >= 0x30 && (options & CollationSettings::NUMERIC) != 0) {
                    return BAIL_OUT_RESULT;
                }
                rightPair = table[c];
            } else if(c <= LATIN_MAX_UTF8_LEAD && 0xc2 <= c && rightIndex != rightLength &&
                    0x80 <= (t = right[rightIndex]) && t <= 0xbf) {
                ++rightIndex;
                c = ((c - 0xc2) << 6) + t;
                rightPair = primaries[c];
                if(rightPair != 0) { break; }
                rightPair = table[c];
            } else {
                rightPair = lookupUTF8(table, c, right, rightIndex, rightLength);
            }
            if(rightPair >= MIN_SHORT) {
                rightPair &= SHORT_PRIMARY_MASK;
                break;
            } else if(rightPair > variableTop) {
                rightPair &= LONG_PRIMARY_MASK;
                break;
            } else {
                rightPair = nextPair(table, c, rightPair, nullptr, right, rightIndex, rightLength);
                if(rightPair == BAIL_OUT) { return BAIL_OUT_RESULT; }
                rightPair = getPrimaries(variableTop, rightPair);
            }
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftPrimary = leftPair & 0xffff;
        uint32_t rightPrimary = rightPair & 0xffff;
        if(leftPrimary != rightPrimary) {
            // Return the primary difference.
            return (leftPrimary < rightPrimary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    // In the following, we need to re-fetch each character because we did not buffer the CEs,
    // but we know that the string is well-formed and
    // only contains supported characters and mappings.

    // We might skip the secondary level but continue with the case level
    // which is turned on separately.
    if(CollationSettings::getStrength(options) >= UCOL_SECONDARY) {
        leftIndex = rightIndex = 0;
        leftPair = rightPair = 0;
        for(;;) {
            while(leftPair == 0) {
                if(leftIndex == leftLength) {
                    leftPair = EOS;
                    break;
                }
                UChar32 c = left[leftIndex++];
                if(c <= 0x7f) {
                    leftPair = table[c];
                } else if(c <= LATIN_MAX_UTF8_LEAD) {
                    leftPair = table[((c - 0xc2) << 6) + left[leftIndex++]];
                } else {
                    leftPair = lookupUTF8Unsafe(table, c, left, leftIndex);
                }
                if(leftPair >= MIN_SHORT) {
                    leftPair = getSecondariesFromOneShortCE(leftPair);
                    break;
                } else if(leftPair > variableTop) {
                    leftPair = COMMON_SEC_PLUS_OFFSET;
                    break;
                } else {
                    leftPair = nextPair(table, c, leftPair, nullptr, left, leftIndex, leftLength);
                    leftPair = getSecondaries(variableTop, leftPair);
                }
            }

            while(rightPair == 0) {
                if(rightIndex == rightLength) {
                    rightPair = EOS;
                    break;
                }
                UChar32 c = right[rightIndex++];
                if(c <= 0x7f) {
                    rightPair = table[c];
                } else if(c <= LATIN_MAX_UTF8_LEAD) {
                    rightPair = table[((c - 0xc2) << 6) + right[rightIndex++]];
                } else {
                    rightPair = lookupUTF8Unsafe(table, c, right, rightIndex);
                }
                if(rightPair >= MIN_SHORT) {
                    rightPair = getSecondariesFromOneShortCE(rightPair);
                    break;
                } else if(rightPair > variableTop) {
                    rightPair = COMMON_SEC_PLUS_OFFSET;
                    break;
                } else {
                    rightPair = nextPair(table, c, rightPair, nullptr, right, rightIndex, rightLength);
                    rightPair = getSecondaries(variableTop, rightPair);
                }
            }

            if(leftPair == rightPair) {
                if(leftPair == EOS) { break; }
                leftPair = rightPair = 0;
                continue;
            }
            uint32_t leftSecondary = leftPair & 0xffff;
            uint32_t rightSecondary = rightPair & 0xffff;
            if(leftSecondary != rightSecondary) {
                if((options & CollationSettings::BACKWARD_SECONDARY) != 0) {
                    // Full support for backwards secondary requires backwards contraction matching
                    // and moving backwards between merge separators.
                    return BAIL_OUT_RESULT;
                }
                return (leftSecondary < rightSecondary) ? UCOL_LESS : UCOL_GREATER;
            }
            if(leftPair == EOS) { break; }
            leftPair >>= 16;
            rightPair >>= 16;
        }
    }

    if((options & CollationSettings::CASE_LEVEL) != 0) {
        UBool strengthIsPrimary = CollationSettings::getStrength(options) == UCOL_PRIMARY;
        leftIndex = rightIndex = 0;
        leftPair = rightPair = 0;
        for(;;) {
            while(leftPair == 0) {
                if(leftIndex == leftLength) {
                    leftPair = EOS;
                    break;
                }
                UChar32 c = left[leftIndex++];
                leftPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, left, leftIndex);
                if(leftPair < MIN_LONG) {
                    leftPair = nextPair(table, c, leftPair, nullptr, left, leftIndex, leftLength);
                }
                leftPair = getCases(variableTop, strengthIsPrimary, leftPair);
            }

            while(rightPair == 0) {
                if(rightIndex == rightLength) {
                    rightPair = EOS;
                    break;
                }
                UChar32 c = right[rightIndex++];
                rightPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, right, rightIndex);
                if(rightPair < MIN_LONG) {
                    rightPair = nextPair(table, c, rightPair, nullptr, right, rightIndex, rightLength);
                }
                rightPair = getCases(variableTop, strengthIsPrimary, rightPair);
            }

            if(leftPair == rightPair) {
                if(leftPair == EOS) { break; }
                leftPair = rightPair = 0;
                continue;
            }
            uint32_t leftCase = leftPair & 0xffff;
            uint32_t rightCase = rightPair & 0xffff;
            if(leftCase != rightCase) {
                if((options & CollationSettings::UPPER_FIRST) == 0) {
                    return (leftCase < rightCase) ? UCOL_LESS : UCOL_GREATER;
                } else {
                    return (leftCase < rightCase) ? UCOL_GREATER : UCOL_LESS;
                }
            }
            if(leftPair == EOS) { break; }
            leftPair >>= 16;
            rightPair >>= 16;
        }
    }
    if(CollationSettings::getStrength(options) <= UCOL_SECONDARY) { return UCOL_EQUAL; }

    // Remove the case bits from the tertiary weight when caseLevel is on or caseFirst is off.
    UBool withCaseBits = CollationSettings::isTertiaryWithCaseBits(options);

    leftIndex = rightIndex = 0;
    leftPair = rightPair = 0;
    for(;;) {
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            leftPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, left, leftIndex);
            if(leftPair < MIN_LONG) {
                leftPair = nextPair(table, c, leftPair, nullptr, left, leftIndex, leftLength);
            }
            leftPair = getTertiaries(variableTop, withCaseBits, leftPair);
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            rightPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, right, rightIndex);
            if(rightPair < MIN_LONG) {
                rightPair = nextPair(table, c, rightPair, nullptr, right, rightIndex, rightLength);
            }
            rightPair = getTertiaries(variableTop, withCaseBits, rightPair);
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftTertiary = leftPair & 0xffff;
        uint32_t rightTertiary = rightPair & 0xffff;
        if(leftTertiary != rightTertiary) {
            if(CollationSettings::sortsTertiaryUpperCaseFirst(options)) {
                // Pass through EOS and MERGE_WEIGHT
                // and keep real tertiary weights larger than the MERGE_WEIGHT.
                // Tertiary CEs (secondary ignorables) are not supported in fast Latin.
                if(leftTertiary > MERGE_WEIGHT) {
                    leftTertiary ^= CASE_MASK;
                }
                if(rightTertiary > MERGE_WEIGHT) {
                    rightTertiary ^= CASE_MASK;
                }
            }
            return (leftTertiary < rightTertiary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    if(CollationSettings::getStrength(options) <= UCOL_TERTIARY) { return UCOL_EQUAL; }

    leftIndex = rightIndex = 0;
    leftPair = rightPair = 0;
    for(;;) {
        while(leftPair == 0) {
            if(leftIndex == leftLength) {
                leftPair = EOS;
                break;
            }
            UChar32 c = left[leftIndex++];
            leftPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, left, leftIndex);
            if(leftPair < MIN_LONG) {
                leftPair = nextPair(table, c, leftPair, nullptr, left, leftIndex, leftLength);
            }
            leftPair = getQuaternaries(variableTop, leftPair);
        }

        while(rightPair == 0) {
            if(rightIndex == rightLength) {
                rightPair = EOS;
                break;
            }
            UChar32 c = right[rightIndex++];
            rightPair = (c <= 0x7f) ? table[c] : lookupUTF8Unsafe(table, c, right, rightIndex);
            if(rightPair < MIN_LONG) {
                rightPair = nextPair(table, c, rightPair, nullptr, right, rightIndex, rightLength);
            }
            rightPair = getQuaternaries(variableTop, rightPair);
        }

        if(leftPair == rightPair) {
            if(leftPair == EOS) { break; }
            leftPair = rightPair = 0;
            continue;
        }
        uint32_t leftQuaternary = leftPair & 0xffff;
        uint32_t rightQuaternary = rightPair & 0xffff;
        if(leftQuaternary != rightQuaternary) {
            return (leftQuaternary < rightQuaternary) ? UCOL_LESS : UCOL_GREATER;
        }
        if(leftPair == EOS) { break; }
        leftPair >>= 16;
        rightPair >>= 16;
    }
    return UCOL_EQUAL;
}

uint32_t
CollationFastLatin::lookup(const uint16_t *table, UChar32 c) {
    U_ASSERT(c > LATIN_MAX);
    if(PUNCT_START <= c && c < PUNCT_LIMIT) {
        return table[c - PUNCT_START + LATIN_LIMIT];
    } else if(c == 0xfffe) {
        return MERGE_WEIGHT;
    } else if(c == 0xffff) {
        return MAX_SHORT | COMMON_SEC | LOWER_CASE | COMMON_TER;
    } else {
        return BAIL_OUT;
    }
}

uint32_t
CollationFastLatin::lookupUTF8(const uint16_t *table, UChar32 c,
                               const uint8_t *s8, int32_t &sIndex, int32_t sLength) {
    // The caller handled ASCII and valid/supported Latin.
    U_ASSERT(c > 0x7f);
    int32_t i2 = sIndex + 1;
    if(i2 < sLength || sLength < 0) {
        uint8_t t1 = s8[sIndex];
        uint8_t t2 = s8[i2];
        sIndex += 2;
        if(c == 0xe2 && t1 == 0x80 && 0x80 <= t2 && t2 <= 0xbf) {
            return table[(LATIN_LIMIT - 0x80) + t2];  // 2000..203F -> 0180..01BF
        } else if(c == 0xef && t1 == 0xbf) {
            if(t2 == 0xbe) {
                return MERGE_WEIGHT;  // U+FFFE
            } else if(t2 == 0xbf) {
                return MAX_SHORT | COMMON_SEC | LOWER_CASE | COMMON_TER;  // U+FFFF
            }
        }
    }
    return BAIL_OUT;
}

uint32_t
CollationFastLatin::lookupUTF8Unsafe(const uint16_t *table, UChar32 c,
                                     const uint8_t *s8, int32_t &sIndex) {
    // The caller handled ASCII.
    // The string is well-formed and contains only supported characters.
    U_ASSERT(c > 0x7f);
    if(c <= LATIN_MAX_UTF8_LEAD) {
        return table[((c - 0xc2) << 6) + s8[sIndex++]];  // 0080..017F
    }
    uint8_t t2 = s8[sIndex + 1];
    sIndex += 2;
    if(c == 0xe2) {
        return table[(LATIN_LIMIT - 0x80) + t2];  // 2000..203F -> 0180..01BF
    } else if(t2 == 0xbe) {
        return MERGE_WEIGHT;  // U+FFFE
    } else {
        return MAX_SHORT | COMMON_SEC | LOWER_CASE | COMMON_TER;  // U+FFFF
    }
}

uint32_t
CollationFastLatin::nextPair(const uint16_t *table, UChar32 c, uint32_t ce,
                             const char16_t *s16, const uint8_t *s8, int32_t &sIndex, int32_t &sLength) {
    if(ce >= MIN_LONG || ce < CONTRACTION) {
        return ce;  // simple or special mini CE
    } else if(ce >= EXPANSION) {
        int32_t index = NUM_FAST_CHARS + (ce & INDEX_MASK);
        return (static_cast<uint32_t>(table[index + 1]) << 16) | table[index];
    } else /* ce >= CONTRACTION */ {
        if(c == 0 && sLength < 0) {
            sLength = sIndex - 1;
            return EOS;
        }
        // Contraction list: Default mapping followed by
        // 0 or more single-character contraction suffix mappings.
        int32_t index = NUM_FAST_CHARS + (ce & INDEX_MASK);
        if(sIndex != sLength) {
            // Read the next character.
            int32_t c2;
            int32_t nextIndex = sIndex;
            if(s16 != nullptr) {
                c2 = s16[nextIndex++];
                if(c2 > LATIN_MAX) {
                    if(PUNCT_START <= c2 && c2 < PUNCT_LIMIT) {
                        c2 = c2 - PUNCT_START + LATIN_LIMIT;  // 2000..203F -> 0180..01BF
                    } else if(c2 == 0xfffe || c2 == 0xffff) {
                        c2 = -1;  // U+FFFE & U+FFFF cannot occur in contractions.
                    } else {
                        return BAIL_OUT;
                    }
                }
            } else {
                c2 = s8[nextIndex++];
                if(c2 > 0x7f) {
                    uint8_t t;
                    if(c2 <= 0xc5 && 0xc2 <= c2 && nextIndex != sLength &&
                            0x80 <= (t = s8[nextIndex]) && t <= 0xbf) {
                        c2 = ((c2 - 0xc2) << 6) + t;  // 0080..017F
                        ++nextIndex;
                    } else {
                        int32_t i2 = nextIndex + 1;
                        if(i2 < sLength || sLength < 0) {
                            if(c2 == 0xe2 && s8[nextIndex] == 0x80 &&
                                    0x80 <= (t = s8[i2]) && t <= 0xbf) {
                                c2 = (LATIN_LIMIT - 0x80) + t;  // 2000..203F -> 0180..01BF
                            } else if(c2 == 0xef && s8[nextIndex] == 0xbf &&
                                    ((t = s8[i2]) == 0xbe || t == 0xbf)) {
                                c2 = -1;  // U+FFFE & U+FFFF cannot occur in contractions.
                            } else {
                                return BAIL_OUT;
                            }
                        } else {
                            return BAIL_OUT;
                        }
                        nextIndex += 2;
                    }
                }
            }
            if(c2 == 0 && sLength < 0) {
                sLength = sIndex;
                c2 = -1;
            }
            // Look for the next character in the contraction suffix list,
            // which is in ascending order of single suffix characters.
            int32_t i = index;
            int32_t head = table[i];  // first skip the default mapping
            int32_t x;
            do {
                i += head >> CONTR_LENGTH_SHIFT;
                head = table[i];
                x = head & CONTR_CHAR_MASK;
            } while(x < c2);
            if(x == c2) {
                index = i;
                sIndex = nextIndex;
            }
        }
        // Return the CE or CEs for the default or contraction mapping.
        int32_t length = table[index] >> CONTR_LENGTH_SHIFT;
        if(length == 1) {
            return BAIL_OUT;
        }
        ce = table[index + 1];
        if(length == 2) {
            return ce;
        } else {
            return (static_cast<uint32_t>(table[index + 2]) << 16) | ce;
        }
    }
}

uint32_t
CollationFastLatin::getSecondaries(uint32_t variableTop, uint32_t pair) {
    if(pair <= 0xffff) {
        // one mini CE
        if(pair >= MIN_SHORT) {
            pair = getSecondariesFromOneShortCE(pair);
        } else if(pair > variableTop) {
            pair = COMMON_SEC_PLUS_OFFSET;
        } else if(pair >= MIN_LONG) {
            pair = 0;  // variable
        }
        // else special mini CE
    } else {
        uint32_t ce = pair & 0xffff;
        if(ce >= MIN_SHORT) {
            pair = (pair & TWO_SECONDARIES_MASK) + TWO_SEC_OFFSETS;
        } else if(ce > variableTop) {
            pair = TWO_COMMON_SEC_PLUS_OFFSET;
        } else {
            U_ASSERT(ce >= MIN_LONG);
            pair = 0;  // variable
        }
    }
    return pair;
}

uint32_t
CollationFastLatin::getCases(uint32_t variableTop, UBool strengthIsPrimary, uint32_t pair) {
    // Primary+caseLevel: Ignore case level weights of primary ignorables.
    // Otherwise: Ignore case level weights of secondary ignorables.
    // For details see the comments in the CollationCompare class.
    // Tertiary CEs (secondary ignorables) are not supported in fast Latin.
    if(pair <= 0xffff) {
        // one mini CE
        if(pair >= MIN_SHORT) {
            // A high secondary weight means we really have two CEs,
            // a primary CE and a secondary CE.
            uint32_t ce = pair;
            pair &= CASE_MASK;  // explicit weight of primary CE
            if(!strengthIsPrimary && (ce & SECONDARY_MASK) >= MIN_SEC_HIGH) {
                pair |= LOWER_CASE << 16;  // implied weight of secondary CE
            }
        } else if(pair > variableTop) {
            pair = LOWER_CASE;
        } else if(pair >= MIN_LONG) {
            pair = 0;  // variable
        }
        // else special mini CE
    } else {
        // two mini CEs, same primary groups, neither expands like above
        uint32_t ce = pair & 0xffff;
        if(ce >= MIN_SHORT) {
            if(strengthIsPrimary && (pair & (SHORT_PRIMARY_MASK << 16)) == 0) {
                pair &= CASE_MASK;
            } else {
                pair &= TWO_CASES_MASK;
            }
        } else if(ce > variableTop) {
            pair = TWO_LOWER_CASES;
        } else {
            U_ASSERT(ce >= MIN_LONG);
            pair = 0;  // variable
        }
    }
    return pair;
}

uint32_t
CollationFastLatin::getTertiaries(uint32_t variableTop, UBool withCaseBits, uint32_t pair) {
    if(pair <= 0xffff) {
        // one mini CE
        if(pair >= MIN_SHORT) {
            // A high secondary weight means we really have two CEs,
            // a primary CE and a secondary CE.
            uint32_t ce = pair;
            if(withCaseBits) {
                pair = (pair & CASE_AND_TERTIARY_MASK) + TER_OFFSET;
                if((ce & SECONDARY_MASK) >= MIN_SEC_HIGH) {
                    pair |= (LOWER_CASE | COMMON_TER_PLUS_OFFSET) << 16;
                }
            } else {
                pair = (pair & TERTIARY_MASK) + TER_OFFSET;
                if((ce & SECONDARY_MASK) >= MIN_SEC_HIGH) {
                    pair |= COMMON_TER_PLUS_OFFSET << 16;
                }
            }
        } else if(pair > variableTop) {
            pair = (pair & TERTIARY_MASK) + TER_OFFSET;
            if(withCaseBits) {
                pair |= LOWER_CASE;
            }
        } else if(pair >= MIN_LONG) {
            pair = 0;  // variable
        }
        // else special mini CE
    } else {
        // two mini CEs, same primary groups, neither expands like above
        uint32_t ce = pair & 0xffff;
        if(ce >= MIN_SHORT) {
            if(withCaseBits) {
                pair &= TWO_CASES_MASK | TWO_TERTIARIES_MASK;
            } else {
                pair &= TWO_TERTIARIES_MASK;
            }
            pair += TWO_TER_OFFSETS;
        } else if(ce > variableTop) {
            pair = (pair & TWO_TERTIARIES_MASK) + TWO_TER_OFFSETS;
            if(withCaseBits) {
                pair |= TWO_LOWER_CASES;
            }
        } else {
            U_ASSERT(ce >= MIN_LONG);
            pair = 0;  // variable
        }
    }
    return pair;
}

uint32_t
CollationFastLatin::getQuaternaries(uint32_t variableTop, uint32_t pair) {
    // Return the primary weight of a variable CE,
    // or the maximum primary weight for a non-variable, not-completely-ignorable CE.
    if(pair <= 0xffff) {
        // one mini CE
        if(pair >= MIN_SHORT) {
            // A high secondary weight means we really have two CEs,
            // a primary CE and a secondary CE.
            if((pair & SECONDARY_MASK) >= MIN_SEC_HIGH) {
                pair = TWO_SHORT_PRIMARIES_MASK;
            } else {
                pair = SHORT_PRIMARY_MASK;
            }
        } else if(pair > variableTop) {
            pair = SHORT_PRIMARY_MASK;
        } else if(pair >= MIN_LONG) {
            pair &= LONG_PRIMARY_MASK;  // variable
        }
        // else special mini CE
    } else {
        // two mini CEs, same primary groups, neither expands like above
        uint32_t ce = pair & 0xffff;
        if(ce > variableTop) {
            pair = TWO_SHORT_PRIMARIES_MASK;
        } else {
            U_ASSERT(ce >= MIN_LONG);
            pair &= TWO_LONG_PRIMARIES_MASK;  // variable
        }
    }
    return pair;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
