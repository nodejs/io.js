// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationrootelements.cpp
*
* created on: 2013mar05
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "collation.h"
#include "collationrootelements.h"
#include "uassert.h"

U_NAMESPACE_BEGIN

int64_t
CollationRootElements::lastCEWithPrimaryBefore(uint32_t p) const {
    if(p == 0) { return 0; }
    U_ASSERT(p > elements[elements[IX_FIRST_PRIMARY_INDEX]]);
    int32_t index = findP(p);
    uint32_t q = elements[index];
    uint32_t secTer;
    if(p == (q & 0xffffff00)) {
        // p == elements[index] is a root primary. Find the CE before it.
        // We must not be in a primary range.
        U_ASSERT((q & PRIMARY_STEP_MASK) == 0);
        secTer = elements[index - 1];
        if((secTer & SEC_TER_DELTA_FLAG) == 0) {
            // Primary CE just before p.
            p = secTer & 0xffffff00;
            secTer = Collation::COMMON_SEC_AND_TER_CE;
        } else {
            // secTer = last secondary & tertiary for the previous primary
            index -= 2;
            for(;;) {
                p = elements[index];
                if((p & SEC_TER_DELTA_FLAG) == 0) {
                    p &= 0xffffff00;
                    break;
                }
                --index;
            }
        }
    } else {
        // p > elements[index] which is the previous primary.
        // Find the last secondary & tertiary weights for it.
        p = q & 0xffffff00;
        secTer = Collation::COMMON_SEC_AND_TER_CE;
        for(;;) {
            q = elements[++index];
            if((q & SEC_TER_DELTA_FLAG) == 0) {
                // We must not be in a primary range.
                U_ASSERT((q & PRIMARY_STEP_MASK) == 0);
                break;
            }
            secTer = q;
        }
    }
    return (static_cast<int64_t>(p) << 32) | (secTer & ~SEC_TER_DELTA_FLAG);
}

int64_t
CollationRootElements::firstCEWithPrimaryAtLeast(uint32_t p) const {
    if(p == 0) { return 0; }
    int32_t index = findP(p);
    if(p != (elements[index] & 0xffffff00)) {
        for(;;) {
            p = elements[++index];
            if((p & SEC_TER_DELTA_FLAG) == 0) {
                // First primary after p. We must not be in a primary range.
                U_ASSERT((p & PRIMARY_STEP_MASK) == 0);
                break;
            }
        }
    }
    // The code above guarantees that p has at most 3 bytes: (p & 0xff) == 0.
    return (static_cast<int64_t>(p) << 32) | Collation::COMMON_SEC_AND_TER_CE;
}

uint32_t
CollationRootElements::getPrimaryBefore(uint32_t p, UBool isCompressible) const {
    int32_t index = findPrimary(p);
    int32_t step;
    uint32_t q = elements[index];
    if(p == (q & 0xffffff00)) {
        // Found p itself. Return the previous primary.
        // See if p is at the end of a previous range.
        step = static_cast<int32_t>(q) & PRIMARY_STEP_MASK;
        if(step == 0) {
            // p is not at the end of a range. Look for the previous primary.
            do {
                p = elements[--index];
            } while((p & SEC_TER_DELTA_FLAG) != 0);
            return p & 0xffffff00;
        }
    } else {
        // p is in a range, and not at the start.
        uint32_t nextElement = elements[index + 1];
        U_ASSERT(isEndOfPrimaryRange(nextElement));
        step = static_cast<int32_t>(nextElement) & PRIMARY_STEP_MASK;
    }
    // Return the previous range primary.
    if((p & 0xffff) == 0) {
        return Collation::decTwoBytePrimaryByOneStep(p, isCompressible, step);
    } else {
        return Collation::decThreeBytePrimaryByOneStep(p, isCompressible, step);
    }
}

uint32_t
CollationRootElements::getSecondaryBefore(uint32_t p, uint32_t s) const {
    int32_t index;
    uint32_t previousSec, sec;
    if(p == 0) {
        index = static_cast<int32_t>(elements[IX_FIRST_SECONDARY_INDEX]);
        // Gap at the beginning of the secondary CE range.
        previousSec = 0;
        sec = elements[index] >> 16;
    } else {
        index = findPrimary(p) + 1;
        previousSec = Collation::BEFORE_WEIGHT16;
        sec = getFirstSecTerForPrimary(index) >> 16;
    }
    U_ASSERT(s >= sec);
    while(s > sec) {
        previousSec = sec;
        U_ASSERT((elements[index] & SEC_TER_DELTA_FLAG) != 0);
        sec = elements[index++] >> 16;
    }
    U_ASSERT(sec == s);
    return previousSec;
}

uint32_t
CollationRootElements::getTertiaryBefore(uint32_t p, uint32_t s, uint32_t t) const {
    U_ASSERT((t & ~Collation::ONLY_TERTIARY_MASK) == 0);
    int32_t index;
    uint32_t previousTer, secTer;
    if(p == 0) {
        if(s == 0) {
            index = static_cast<int32_t>(elements[IX_FIRST_TERTIARY_INDEX]);
            // Gap at the beginning of the tertiary CE range.
            previousTer = 0;
        } else {
            index = static_cast<int32_t>(elements[IX_FIRST_SECONDARY_INDEX]);
            previousTer = Collation::BEFORE_WEIGHT16;
        }
        secTer = elements[index] & ~SEC_TER_DELTA_FLAG;
    } else {
        index = findPrimary(p) + 1;
        previousTer = Collation::BEFORE_WEIGHT16;
        secTer = getFirstSecTerForPrimary(index);
    }
    uint32_t st = (s << 16) | t;
    while(st > secTer) {
        if((secTer >> 16) == s) { previousTer = secTer; }
        U_ASSERT((elements[index] & SEC_TER_DELTA_FLAG) != 0);
        secTer = elements[index++] & ~SEC_TER_DELTA_FLAG;
    }
    U_ASSERT(secTer == st);
    return previousTer & 0xffff;
}

uint32_t
CollationRootElements::getPrimaryAfter(uint32_t p, int32_t index, UBool isCompressible) const {
    U_ASSERT(p == (elements[index] & 0xffffff00) || isEndOfPrimaryRange(elements[index + 1]));
    uint32_t q = elements[++index];
    int32_t step;
    if ((q & SEC_TER_DELTA_FLAG) == 0 && (step = static_cast<int32_t>(q) & PRIMARY_STEP_MASK) != 0) {
        // Return the next primary in this range.
        if((p & 0xffff) == 0) {
            return Collation::incTwoBytePrimaryByOffset(p, isCompressible, step);
        } else {
            return Collation::incThreeBytePrimaryByOffset(p, isCompressible, step);
        }
    } else {
        // Return the next primary in the list.
        while((q & SEC_TER_DELTA_FLAG) != 0) {
            q = elements[++index];
        }
        U_ASSERT((q & PRIMARY_STEP_MASK) == 0);
        return q;
    }
}

uint32_t
CollationRootElements::getSecondaryAfter(int32_t index, uint32_t s) const {
    uint32_t secTer;
    uint32_t secLimit;
    if(index == 0) {
        // primary = 0
        U_ASSERT(s != 0);
        index = static_cast<int32_t>(elements[IX_FIRST_SECONDARY_INDEX]);
        secTer = elements[index];
        // Gap at the end of the secondary CE range.
        secLimit = 0x10000;
    } else {
        U_ASSERT(index >= (int32_t)elements[IX_FIRST_PRIMARY_INDEX]);
        secTer = getFirstSecTerForPrimary(index + 1);
        // If this is an explicit sec/ter unit, then it will be read once more.
        // Gap for secondaries of primary CEs.
        secLimit = getSecondaryBoundary();
    }
    for(;;) {
        uint32_t sec = secTer >> 16;
        if(sec > s) { return sec; }
        secTer = elements[++index];
        if((secTer & SEC_TER_DELTA_FLAG) == 0) { return secLimit; }
    }
}

uint32_t
CollationRootElements::getTertiaryAfter(int32_t index, uint32_t s, uint32_t t) const {
    uint32_t secTer;
    uint32_t terLimit;
    if(index == 0) {
        // primary = 0
        if(s == 0) {
            U_ASSERT(t != 0);
            index = static_cast<int32_t>(elements[IX_FIRST_TERTIARY_INDEX]);
            // Gap at the end of the tertiary CE range.
            terLimit = 0x4000;
        } else {
            index = static_cast<int32_t>(elements[IX_FIRST_SECONDARY_INDEX]);
            // Gap for tertiaries of primary/secondary CEs.
            terLimit = getTertiaryBoundary();
        }
        secTer = elements[index] & ~SEC_TER_DELTA_FLAG;
    } else {
        U_ASSERT(index >= (int32_t)elements[IX_FIRST_PRIMARY_INDEX]);
        secTer = getFirstSecTerForPrimary(index + 1);
        // If this is an explicit sec/ter unit, then it will be read once more.
        terLimit = getTertiaryBoundary();
    }
    uint32_t st = (s << 16) | t;
    for(;;) {
        if(secTer > st) {
            U_ASSERT((secTer >> 16) == s);
            return secTer & 0xffff;
        }
        secTer = elements[++index];
        // No tertiary greater than t for this primary+secondary.
        if((secTer & SEC_TER_DELTA_FLAG) == 0 || (secTer >> 16) > s) { return terLimit; }
        secTer &= ~SEC_TER_DELTA_FLAG;
    }
}

uint32_t
CollationRootElements::getFirstSecTerForPrimary(int32_t index) const {
    uint32_t secTer = elements[index];
    if((secTer & SEC_TER_DELTA_FLAG) == 0) {
        // No sec/ter delta.
        return Collation::COMMON_SEC_AND_TER_CE;
    }
    secTer &= ~SEC_TER_DELTA_FLAG;
    if(secTer > Collation::COMMON_SEC_AND_TER_CE) {
        // Implied sec/ter.
        return Collation::COMMON_SEC_AND_TER_CE;
    }
    // Explicit sec/ter below common/common.
    return secTer;
}

int32_t
CollationRootElements::findPrimary(uint32_t p) const {
    // Requirement: p must occur as a root primary.
    U_ASSERT((p & 0xff) == 0);  // at most a 3-byte primary
    int32_t index = findP(p);
    // If p is in a range, then we just assume that p is an actual primary in this range.
    // (Too cumbersome/expensive to check.)
    // Otherwise, it must be an exact match.
    U_ASSERT(isEndOfPrimaryRange(elements[index + 1]) || p == (elements[index] & 0xffffff00));
    return index;
}

int32_t
CollationRootElements::findP(uint32_t p) const {
    // p need not occur as a root primary.
    // For example, it might be a reordering group boundary.
    U_ASSERT((p >> 24) != Collation::UNASSIGNED_IMPLICIT_BYTE);
    // modified binary search
    int32_t start = static_cast<int32_t>(elements[IX_FIRST_PRIMARY_INDEX]);
    U_ASSERT(p >= elements[start]);
    int32_t limit = length - 1;
    U_ASSERT(elements[limit] >= PRIMARY_SENTINEL);
    U_ASSERT(p < elements[limit]);
    while((start + 1) < limit) {
        // Invariant: elements[start] and elements[limit] are primaries,
        // and elements[start]<=p<=elements[limit].
        int32_t i = (start + limit) / 2;
        uint32_t q = elements[i];
        if((q & SEC_TER_DELTA_FLAG) != 0) {
            // Find the next primary.
            int32_t j = i + 1;
            for(;;) {
                if(j == limit) { break; }
                q = elements[j];
                if((q & SEC_TER_DELTA_FLAG) == 0) {
                    i = j;
                    break;
                }
                ++j;
            }
            if((q & SEC_TER_DELTA_FLAG) != 0) {
                // Find the preceding primary.
                j = i - 1;
                for(;;) {
                    if(j == start) { break; }
                    q = elements[j];
                    if((q & SEC_TER_DELTA_FLAG) == 0) {
                        i = j;
                        break;
                    }
                    --j;
                }
                if((q & SEC_TER_DELTA_FLAG) != 0) {
                    // No primary between start and limit.
                    break;
                }
            }
        }
        if(p < (q & 0xffffff00)) {  // Reset the "step" bits of a range end primary.
            limit = i;
        } else {
            start = i;
        }
    }
    return start;
}

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
