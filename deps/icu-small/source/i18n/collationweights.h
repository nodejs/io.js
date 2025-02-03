// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*  
*******************************************************************************
*
*   Copyright (C) 1999-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  collationweights.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2001mar08 as ucol_wgt.h
*   created by: Markus W. Scherer
*/

#ifndef __COLLATIONWEIGHTS_H__
#define __COLLATIONWEIGHTS_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/uobject.h"

U_NAMESPACE_BEGIN

/**
 * Allocates n collation element weights between two exclusive limits.
 * Used only internally by the collation tailoring builder.
 */
class U_I18N_API CollationWeights : public UMemory {
public:
    CollationWeights();

    static inline int32_t lengthOfWeight(uint32_t weight) {
        if((weight&0xffffff)==0) {
            return 1;
        } else if((weight&0xffff)==0) {
            return 2;
        } else if((weight&0xff)==0) {
            return 3;
        } else {
            return 4;
        }
    }

    void initForPrimary(UBool compressible);
    void initForSecondary();
    void initForTertiary();

    /**
     * Determine heuristically
     * what ranges to use for a given number of weights between (excluding)
     * two limits.
     *
     * @param lowerLimit A collation element weight; the ranges will be filled to cover
     *                   weights greater than this one.
     * @param upperLimit A collation element weight; the ranges will be filled to cover
     *                   weights less than this one.
     * @param n          The number of collation element weights w necessary such that
     *                   lowerLimit<w<upperLimit in lexical order.
     * @return true if it is possible to fit n elements between the limits
     */
    UBool allocWeights(uint32_t lowerLimit, uint32_t upperLimit, int32_t n);

    /**
     * Given a set of ranges calculated by allocWeights(),
     * iterate through the weights.
     * The ranges are modified to keep the current iteration state.
     *
     * @return The next weight in the ranges, or 0xffffffff if there is none left.
     */
    uint32_t nextWeight();

    /** @internal */
    struct WeightRange {
        uint32_t start, end;
        int32_t length, count;
    };

private:
    /** @return number of usable byte values for byte idx */
    inline int32_t countBytes(int32_t idx) const {
        return static_cast<int32_t>(maxBytes[idx] - minBytes[idx] + 1);
    }

    uint32_t incWeight(uint32_t weight, int32_t length) const;
    uint32_t incWeightByOffset(uint32_t weight, int32_t length, int32_t offset) const;
    void lengthenRange(WeightRange &range) const;
    /**
     * Takes two CE weights and calculates the
     * possible ranges of weights between the two limits, excluding them.
     * For weights with up to 4 bytes there are up to 2*4-1=7 ranges.
     */
    UBool getWeightRanges(uint32_t lowerLimit, uint32_t upperLimit);
    UBool allocWeightsInShortRanges(int32_t n, int32_t minLength);
    UBool allocWeightsInMinLengthRanges(int32_t n, int32_t minLength);

    int32_t middleLength;
    uint32_t minBytes[5];  // for byte 1, 2, 3, 4
    uint32_t maxBytes[5];
    WeightRange ranges[7];
    int32_t rangeIndex;
    int32_t rangeCount;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONWEIGHTS_H__
