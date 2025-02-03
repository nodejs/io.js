// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*  
*******************************************************************************
*
*   Copyright (C) 1999-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  collationweights.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2001mar08 as ucol_wgt.cpp
*   created by: Markus W. Scherer
*
*   This file contains code for allocating n collation element weights
*   between two exclusive limits.
*   It is used only internally by the collation tailoring builder.
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "cmemory.h"
#include "collation.h"
#include "collationweights.h"
#include "uarrsort.h"
#include "uassert.h"

#ifdef UCOL_DEBUG
#   include <stdio.h>
#endif

U_NAMESPACE_BEGIN

/* collation element weight allocation -------------------------------------- */

/* helper functions for CE weights */

static inline uint32_t
getWeightTrail(uint32_t weight, int32_t length) {
    return (weight >> (8 * (4 - length))) & 0xff;
}

static inline uint32_t
setWeightTrail(uint32_t weight, int32_t length, uint32_t trail) {
    length=8*(4-length);
    return static_cast<uint32_t>((weight & (0xffffff00 << length)) | (trail << length));
}

static inline uint32_t
getWeightByte(uint32_t weight, int32_t idx) {
    return getWeightTrail(weight, idx); /* same calculation */
}

static inline uint32_t
setWeightByte(uint32_t weight, int32_t idx, uint32_t byte) {
    uint32_t mask; /* 0xffffffff except a 00 "hole" for the index-th byte */

    idx*=8;
    if(idx<32) {
        mask = (static_cast<uint32_t>(0xffffffff)) >> idx;
    } else {
        // Do not use uint32_t>>32 because on some platforms that does not shift at all
        // while we need it to become 0.
        // PowerPC: 0xffffffff>>32 = 0           (wanted)
        // x86:     0xffffffff>>32 = 0xffffffff  (not wanted)
        //
        // ANSI C99 6.5.7 Bitwise shift operators:
        // "If the value of the right operand is negative
        // or is greater than or equal to the width of the promoted left operand,
        // the behavior is undefined."
        mask=0;
    }
    idx=32-idx;
    mask|=0xffffff00<<idx;
    return ((weight & mask) | (byte << idx));
}

static inline uint32_t
truncateWeight(uint32_t weight, int32_t length) {
    return static_cast<uint32_t>(weight & (0xffffffff << (8 * (4 - length))));
}

static inline uint32_t
incWeightTrail(uint32_t weight, int32_t length) {
    return static_cast<uint32_t>(weight + (1UL << (8 * (4 - length))));
}

static inline uint32_t
decWeightTrail(uint32_t weight, int32_t length) {
    return static_cast<uint32_t>(weight - (1UL << (8 * (4 - length))));
}

CollationWeights::CollationWeights()
        : middleLength(0), rangeIndex(0), rangeCount(0) {
    for(int32_t i = 0; i < 5; ++i) {
        minBytes[i] = maxBytes[i] = 0;
    }
}

void
CollationWeights::initForPrimary(UBool compressible) {
    middleLength=1;
    minBytes[1] = Collation::MERGE_SEPARATOR_BYTE + 1;
    maxBytes[1] = Collation::TRAIL_WEIGHT_BYTE;
    if(compressible) {
        minBytes[2] = Collation::PRIMARY_COMPRESSION_LOW_BYTE + 1;
        maxBytes[2] = Collation::PRIMARY_COMPRESSION_HIGH_BYTE - 1;
    } else {
        minBytes[2] = 2;
        maxBytes[2] = 0xff;
    }
    minBytes[3] = 2;
    maxBytes[3] = 0xff;
    minBytes[4] = 2;
    maxBytes[4] = 0xff;
}

void
CollationWeights::initForSecondary() {
    // We use only the lower 16 bits for secondary weights.
    middleLength=3;
    minBytes[1] = 0;
    maxBytes[1] = 0;
    minBytes[2] = 0;
    maxBytes[2] = 0;
    minBytes[3] = Collation::LEVEL_SEPARATOR_BYTE + 1;
    maxBytes[3] = 0xff;
    minBytes[4] = 2;
    maxBytes[4] = 0xff;
}

void
CollationWeights::initForTertiary() {
    // We use only the lower 16 bits for tertiary weights.
    middleLength=3;
    minBytes[1] = 0;
    maxBytes[1] = 0;
    minBytes[2] = 0;
    maxBytes[2] = 0;
    // We use only 6 bits per byte.
    // The other bits are used for case & quaternary weights.
    minBytes[3] = Collation::LEVEL_SEPARATOR_BYTE + 1;
    maxBytes[3] = 0x3f;
    minBytes[4] = 2;
    maxBytes[4] = 0x3f;
}

uint32_t
CollationWeights::incWeight(uint32_t weight, int32_t length) const {
    for(;;) {
        uint32_t byte=getWeightByte(weight, length);
        if(byte<maxBytes[length]) {
            return setWeightByte(weight, length, byte+1);
        } else {
            // Roll over, set this byte to the minimum and increment the previous one.
            weight=setWeightByte(weight, length, minBytes[length]);
            --length;
            U_ASSERT(length > 0);
        }
    }
}

uint32_t
CollationWeights::incWeightByOffset(uint32_t weight, int32_t length, int32_t offset) const {
    for(;;) {
        offset += getWeightByte(weight, length);
        if (static_cast<uint32_t>(offset) <= maxBytes[length]) {
            return setWeightByte(weight, length, offset);
        } else {
            // Split the offset between this byte and the previous one.
            offset -= minBytes[length];
            weight = setWeightByte(weight, length, minBytes[length] + offset % countBytes(length));
            offset /= countBytes(length);
            --length;
            U_ASSERT(length > 0);
        }
    }
}

void
CollationWeights::lengthenRange(WeightRange &range) const {
    int32_t length=range.length+1;
    range.start=setWeightTrail(range.start, length, minBytes[length]);
    range.end=setWeightTrail(range.end, length, maxBytes[length]);
    range.count*=countBytes(length);
    range.length=length;
}

/* for uprv_sortArray: sort ranges in weight order */
static int32_t U_CALLCONV
compareRanges(const void * /*context*/, const void *left, const void *right) {
    uint32_t l, r;

    l = static_cast<const CollationWeights::WeightRange*>(left)->start;
    r = static_cast<const CollationWeights::WeightRange*>(right)->start;
    if(l<r) {
        return -1;
    } else if(l>r) {
        return 1;
    } else {
        return 0;
    }
}

UBool
CollationWeights::getWeightRanges(uint32_t lowerLimit, uint32_t upperLimit) {
    U_ASSERT(lowerLimit != 0);
    U_ASSERT(upperLimit != 0);

    /* get the lengths of the limits */
    int32_t lowerLength=lengthOfWeight(lowerLimit);
    int32_t upperLength=lengthOfWeight(upperLimit);

#ifdef UCOL_DEBUG
    printf("length of lower limit 0x%08lx is %ld\n", lowerLimit, lowerLength);
    printf("length of upper limit 0x%08lx is %ld\n", upperLimit, upperLength);
#endif
    U_ASSERT(lowerLength>=middleLength);
    // Permit upperLength<middleLength: The upper limit for secondaries is 0x10000.

    if(lowerLimit>=upperLimit) {
#ifdef UCOL_DEBUG
        printf("error: no space between lower & upper limits\n");
#endif
        return false;
    }

    /* check that neither is a prefix of the other */
    if(lowerLength<upperLength) {
        if(lowerLimit==truncateWeight(upperLimit, lowerLength)) {
#ifdef UCOL_DEBUG
            printf("error: lower limit 0x%08lx is a prefix of upper limit 0x%08lx\n", lowerLimit, upperLimit);
#endif
            return false;
        }
    }
    /* if the upper limit is a prefix of the lower limit then the earlier test lowerLimit>=upperLimit has caught it */

    WeightRange lower[5], middle, upper[5]; /* [0] and [1] are not used - this simplifies indexing */
    uprv_memset(lower, 0, sizeof(lower));
    uprv_memset(&middle, 0, sizeof(middle));
    uprv_memset(upper, 0, sizeof(upper));

    /*
     * With the limit lengths of 1..4, there are up to 7 ranges for allocation:
     * range     minimum length
     * lower[4]  4
     * lower[3]  3
     * lower[2]  2
     * middle    1
     * upper[2]  2
     * upper[3]  3
     * upper[4]  4
     *
     * We are now going to calculate up to 7 ranges.
     * Some of them will typically overlap, so we will then have to merge and eliminate ranges.
     */
    uint32_t weight=lowerLimit;
    for(int32_t length=lowerLength; length>middleLength; --length) {
        uint32_t trail=getWeightTrail(weight, length);
        if(trail<maxBytes[length]) {
            lower[length].start=incWeightTrail(weight, length);
            lower[length].end=setWeightTrail(weight, length, maxBytes[length]);
            lower[length].length=length;
            lower[length].count=maxBytes[length]-trail;
        }
        weight=truncateWeight(weight, length-1);
    }
    if(weight<0xff000000) {
        middle.start=incWeightTrail(weight, middleLength);
    } else {
        // Prevent overflow for primary lead byte FF
        // which would yield a middle range starting at 0.
        middle.start=0xffffffff;  // no middle range
    }

    weight=upperLimit;
    for(int32_t length=upperLength; length>middleLength; --length) {
        uint32_t trail=getWeightTrail(weight, length);
        if(trail>minBytes[length]) {
            upper[length].start=setWeightTrail(weight, length, minBytes[length]);
            upper[length].end=decWeightTrail(weight, length);
            upper[length].length=length;
            upper[length].count=trail-minBytes[length];
        }
        weight=truncateWeight(weight, length-1);
    }
    middle.end=decWeightTrail(weight, middleLength);

    /* set the middle range */
    middle.length=middleLength;
    if(middle.end>=middle.start) {
        middle.count = static_cast<int32_t>((middle.end - middle.start) >> (8 * (4 - middleLength))) + 1;
    } else {
        /* no middle range, eliminate overlaps */
        for(int32_t length=4; length>middleLength; --length) {
            if(lower[length].count>0 && upper[length].count>0) {
                // Note: The lowerEnd and upperStart weights are versions of
                // lowerLimit and upperLimit (which are lowerLimit<upperLimit),
                // truncated (still less-or-equal)
                // and then with their last bytes changed to the
                // maxByte (for lowerEnd) or minByte (for upperStart).
                const uint32_t lowerEnd=lower[length].end;
                const uint32_t upperStart=upper[length].start;
                UBool merged=false;

                if(lowerEnd>upperStart) {
                    // These two lower and upper ranges collide.
                    // Since lowerLimit<upperLimit and lowerEnd and upperStart
                    // are versions with only their last bytes modified
                    // (and following ones removed/reset to 0),
                    // lowerEnd>upperStart is only possible
                    // if the leading bytes are equal
                    // and lastByte(lowerEnd)>lastByte(upperStart).
                    U_ASSERT(truncateWeight(lowerEnd, length-1)==
                            truncateWeight(upperStart, length-1));
                    // Intersect these two ranges.
                    lower[length].end=upper[length].end;
                    lower[length].count=
                            static_cast<int32_t>(getWeightTrail(lower[length].end, length)) -
                            static_cast<int32_t>(getWeightTrail(lower[length].start, length)) + 1;
                    // count might be <=0 in which case there is no room,
                    // and the range-collecting code below will ignore this range.
                    merged=true;
                } else if(lowerEnd==upperStart) {
                    // Not possible, unless minByte==maxByte which is not allowed.
                    U_ASSERT(minBytes[length]<maxBytes[length]);
                } else /* lowerEnd<upperStart */ {
                    if(incWeight(lowerEnd, length)==upperStart) {
                        // Merge adjacent ranges.
                        lower[length].end=upper[length].end;
                        lower[length].count+=upper[length].count;  // might be >countBytes
                        merged=true;
                    }
                }
                if(merged) {
                    // Remove all shorter ranges.
                    // There was no room available for them between the ranges we just merged.
                    upper[length].count=0;
                    while(--length>middleLength) {
                        lower[length].count=upper[length].count=0;
                    }
                    break;
                }
            }
        }
    }

#ifdef UCOL_DEBUG
    /* print ranges */
    for(int32_t length=4; length>=2; --length) {
        if(lower[length].count>0) {
            printf("lower[%ld] .start=0x%08lx .end=0x%08lx .count=%ld\n", length, lower[length].start, lower[length].end, lower[length].count);
        }
    }
    if(middle.count>0) {
        printf("middle   .start=0x%08lx .end=0x%08lx .count=%ld\n", middle.start, middle.end, middle.count);
    }
    for(int32_t length=2; length<=4; ++length) {
        if(upper[length].count>0) {
            printf("upper[%ld] .start=0x%08lx .end=0x%08lx .count=%ld\n", length, upper[length].start, upper[length].end, upper[length].count);
        }
    }
#endif

    /* copy the ranges, shortest first, into the result array */
    rangeCount=0;
    if(middle.count>0) {
        uprv_memcpy(ranges, &middle, sizeof(WeightRange));
        rangeCount=1;
    }
    for(int32_t length=middleLength+1; length<=4; ++length) {
        /* copy upper first so that later the middle range is more likely the first one to use */
        if(upper[length].count>0) {
            uprv_memcpy(ranges+rangeCount, upper+length, sizeof(WeightRange));
            ++rangeCount;
        }
        if(lower[length].count>0) {
            uprv_memcpy(ranges+rangeCount, lower+length, sizeof(WeightRange));
            ++rangeCount;
        }
    }
    return rangeCount>0;
}

UBool
CollationWeights::allocWeightsInShortRanges(int32_t n, int32_t minLength) {
    // See if the first few minLength and minLength+1 ranges have enough weights.
    for(int32_t i = 0; i < rangeCount && ranges[i].length <= (minLength + 1); ++i) {
        if(n <= ranges[i].count) {
            // Use the first few minLength and minLength+1 ranges.
            if(ranges[i].length > minLength) {
                // Reduce the number of weights from the last minLength+1 range
                // which might sort before some minLength ranges,
                // so that we use all weights in the minLength ranges.
                ranges[i].count = n;
            }
            rangeCount = i + 1;
#ifdef UCOL_DEBUG
            printf("take first %ld ranges\n", rangeCount);
#endif

            if(rangeCount>1) {
                /* sort the ranges by weight values */
                UErrorCode errorCode=U_ZERO_ERROR;
                uprv_sortArray(ranges, rangeCount, sizeof(WeightRange),
                               compareRanges, nullptr, false, &errorCode);
                /* ignore error code: we know that the internal sort function will not fail here */
            }
            return true;
        }
        n -= ranges[i].count;  // still >0
    }
    return false;
}

UBool
CollationWeights::allocWeightsInMinLengthRanges(int32_t n, int32_t minLength) {
    // See if the minLength ranges have enough weights
    // when we split one and lengthen the following ones.
    int32_t count = 0;
    int32_t minLengthRangeCount;
    for(minLengthRangeCount = 0;
            minLengthRangeCount < rangeCount &&
                ranges[minLengthRangeCount].length == minLength;
            ++minLengthRangeCount) {
        count += ranges[minLengthRangeCount].count;
    }

    int32_t nextCountBytes = countBytes(minLength + 1);
    if(n > count * nextCountBytes) { return false; }

    // Use the minLength ranges. Merge them, and then split again as necessary.
    uint32_t start = ranges[0].start;
    uint32_t end = ranges[0].end;
    for(int32_t i = 1; i < minLengthRangeCount; ++i) {
        if(ranges[i].start < start) { start = ranges[i].start; }
        if(ranges[i].end > end) { end = ranges[i].end; }
    }

    // Calculate how to split the range between minLength (count1) and minLength+1 (count2).
    // Goal:
    //   count1 + count2 * nextCountBytes = n
    //   count1 + count2 = count
    // These turn into
    //   (count - count2) + count2 * nextCountBytes = n
    // and then into the following count1 & count2 computations.
    int32_t count2 = (n - count) / (nextCountBytes - 1);  // number of weights to be lengthened
    int32_t count1 = count - count2;  // number of minLength weights
    if(count2 == 0 || (count1 + count2 * nextCountBytes) < n) {
        // round up
        ++count2;
        --count1;
        U_ASSERT((count1 + count2 * nextCountBytes) >= n);
    }

    ranges[0].start = start;

    if(count1 == 0) {
        // Make one long range.
        ranges[0].end = end;
        ranges[0].count = count;
        lengthenRange(ranges[0]);
        rangeCount = 1;
    } else {
        // Split the range, lengthen the second part.
#ifdef UCOL_DEBUG
        printf("split the range number %ld (out of %ld minLength ranges) by %ld:%ld\n",
               splitRange, rangeCount, count1, count2);
#endif

        // Next start = start + count1. First end = 1 before that.
        ranges[0].end = incWeightByOffset(start, minLength, count1 - 1);
        ranges[0].count = count1;

        ranges[1].start = incWeight(ranges[0].end, minLength);
        ranges[1].end = end;
        ranges[1].length = minLength;  // +1 when lengthened
        ranges[1].count = count2;  // *countBytes when lengthened
        lengthenRange(ranges[1]);
        rangeCount = 2;
    }
    return true;
}

/*
 * call getWeightRanges and then determine heuristically
 * which ranges to use for a given number of weights between (excluding)
 * two limits
 */
UBool
CollationWeights::allocWeights(uint32_t lowerLimit, uint32_t upperLimit, int32_t n) {
#ifdef UCOL_DEBUG
    puts("");
#endif

    if(!getWeightRanges(lowerLimit, upperLimit)) {
#ifdef UCOL_DEBUG
        printf("error: unable to get Weight ranges\n");
#endif
        return false;
    }

    /* try until we find suitably large ranges */
    for(;;) {
        /* get the smallest number of bytes in a range */
        int32_t minLength=ranges[0].length;

        if(allocWeightsInShortRanges(n, minLength)) { break; }

        if(minLength == 4) {
#ifdef UCOL_DEBUG
            printf("error: the maximum number of %ld weights is insufficient for n=%ld\n",
                   minLengthCount, n);
#endif
            return false;
        }

        if(allocWeightsInMinLengthRanges(n, minLength)) { break; }

        /* no good match, lengthen all minLength ranges and iterate */
#ifdef UCOL_DEBUG
        printf("lengthen the short ranges from %ld bytes to %ld and iterate\n", minLength, minLength+1);
#endif
        for(int32_t i=0; i<rangeCount && ranges[i].length==minLength; ++i) {
            lengthenRange(ranges[i]);
        }
    }

#ifdef UCOL_DEBUG
    puts("final ranges:");
    for(int32_t i=0; i<rangeCount; ++i) {
        printf("ranges[%ld] .start=0x%08lx .end=0x%08lx .length=%ld .count=%ld\n",
               i, ranges[i].start, ranges[i].end, ranges[i].length, ranges[i].count);
    }
#endif

    rangeIndex = 0;
    return true;
}

uint32_t
CollationWeights::nextWeight() {
    if(rangeIndex >= rangeCount) {
        return 0xffffffff;
    } else {
        /* get the next weight */
        WeightRange &range = ranges[rangeIndex];
        uint32_t weight = range.start;
        if(--range.count == 0) {
            /* this range is finished */
            ++rangeIndex;
        } else {
            /* increment the weight for the next value */
            range.start = incWeight(weight, range.length);
            U_ASSERT(range.start <= range.end);
        }

        return weight;
    }
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_COLLATION */
