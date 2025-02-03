// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2012-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationfcd.h
*
* created on: 2012aug18
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONFCD_H__
#define __COLLATIONFCD_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/utf16.h"

U_NAMESPACE_BEGIN

/**
 * Data and functions for the FCD check fast path.
 *
 * The fast path looks at a pair of 16-bit code units and checks
 * whether there is an FCD boundary between them;
 * there is if the first unit has a trailing ccc=0 (!hasTccc(first))
 * or the second unit has a leading ccc=0 (!hasLccc(second)),
 * or both.
 * When the fast path finds a possible non-boundary,
 * then the FCD check slow path looks at the actual sequence of FCD values.
 *
 * This is a pure optimization.
 * The fast path must at least find all possible non-boundaries.
 * If the fast path is too pessimistic, it costs performance.
 *
 * For a pair of BMP characters, the fast path tests are precise (1 bit per character).
 *
 * For a supplementary code point, the two units are its lead and trail surrogates.
 * We set hasTccc(lead)=true if any of its 1024 associated supplementary code points
 * has lccc!=0 or tccc!=0.
 * We set hasLccc(trail)=true for all trail surrogates.
 * As a result, we leave the fast path if the lead surrogate might start a
 * supplementary code point that is not FCD-inert.
 * (So the fast path need not detect that there is a surrogate pair,
 * nor look ahead to the next full code point.)
 *
 * hasLccc(lead)=true if any of its 1024 associated supplementary code points
 * has lccc!=0, for fast boundary checking between BMP & supplementary.
 *
 * hasTccc(trail)=false:
 * It should only be tested for unpaired trail surrogates which are FCD-inert.
 */
class U_I18N_API CollationFCD {
public:
    static inline UBool hasLccc(UChar32 c) {
        // assert c <= 0xffff
        // c can be negative, e.g., U_SENTINEL from UCharIterator;
        // that is handled in the first test.
        int32_t i;
        return
            // U+0300 is the first character with lccc!=0.
            c >= 0x300 &&
            (i = lcccIndex[c >> 5]) != 0 &&
            (lcccBits[i] & (static_cast<uint32_t>(1) << (c & 0x1f))) != 0;
    }

    static inline UBool hasTccc(UChar32 c) {
        // assert c <= 0xffff
        // c can be negative, e.g., U_SENTINEL from UCharIterator;
        // that is handled in the first test.
        int32_t i;
        return
            // U+00C0 is the first character with tccc!=0.
            c >= 0xc0 &&
            (i = tcccIndex[c >> 5]) != 0 &&
            (tcccBits[i] & (static_cast<uint32_t>(1) << (c & 0x1f))) != 0;
    }

    static inline UBool mayHaveLccc(UChar32 c) {
        // Handles all of Unicode 0..10FFFF.
        // c can be negative, e.g., U_SENTINEL.
        // U+0300 is the first character with lccc!=0.
        if(c < 0x300) { return false; }
        if(c > 0xffff) { c = U16_LEAD(c); }
        int32_t i;
        return
            (i = lcccIndex[c >> 5]) != 0 &&
            (lcccBits[i] & (static_cast<uint32_t>(1) << (c & 0x1f))) != 0;
    }

    /**
     * Tibetan composite vowel signs (U+0F73, U+0F75, U+0F81)
     * must be decomposed before reaching the core collation code,
     * or else some sequences including them, even ones passing the FCD check,
     * do not yield canonically equivalent results.
     *
     * This is a fast and imprecise test.
     *
     * @param c a code point
     * @return true if c is U+0F73, U+0F75 or U+0F81 or one of several other Tibetan characters
     */
    static inline UBool maybeTibetanCompositeVowel(UChar32 c) {
        return (c & 0x1fff01) == 0xf01;
    }

    /**
     * Tibetan composite vowel signs (U+0F73, U+0F75, U+0F81)
     * must be decomposed before reaching the core collation code,
     * or else some sequences including them, even ones passing the FCD check,
     * do not yield canonically equivalent results.
     *
     * They have distinct lccc/tccc combinations: 129/130 or 129/132.
     *
     * @param fcd16 the FCD value (lccc/tccc combination) of a code point
     * @return true if fcd16 is from U+0F73, U+0F75 or U+0F81
     */
    static inline UBool isFCD16OfTibetanCompositeVowel(uint16_t fcd16) {
        return fcd16 == 0x8182 || fcd16 == 0x8184;
    }

private:
    CollationFCD() = delete;  // No instantiation.

    static const uint8_t lcccIndex[2048];
    static const uint8_t tcccIndex[2048];
    static const uint32_t lcccBits[];
    static const uint32_t tcccBits[];
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONFCD_H__
