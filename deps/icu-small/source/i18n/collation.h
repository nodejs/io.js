// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collation.h
*
* created on: 2010oct27
* created by: Markus W. Scherer
*/

#ifndef __COLLATION_H__
#define __COLLATION_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

U_NAMESPACE_BEGIN

/**
 * Collation v2 basic definitions and static helper functions.
 *
 * Data structures except for expansion tables store 32-bit CEs which are
 * either specials (see tags below) or are compact forms of 64-bit CEs.
 */
class U_I18N_API Collation {
public:
    // Special sort key bytes for all levels.
    static const uint8_t TERMINATOR_BYTE = 0;
    static const uint8_t LEVEL_SEPARATOR_BYTE = 1;

    /** The secondary/tertiary lower limit for tailoring before any root elements. */
    static const uint32_t BEFORE_WEIGHT16 = 0x0100;

    /**
     * Merge-sort-key separator.
     * Same as the unique primary and identical-level weights of U+FFFE.
     * Must not be used as primary compression low terminator.
     * Otherwise usable.
     */
    static const uint8_t MERGE_SEPARATOR_BYTE = 2;
    static const uint32_t MERGE_SEPARATOR_PRIMARY = 0x02000000;  // U+FFFE
    static const uint32_t MERGE_SEPARATOR_CE32 = 0x02000505;  // U+FFFE

    /**
     * Primary compression low terminator, must be greater than MERGE_SEPARATOR_BYTE.
     * Reserved value in primary second byte if the lead byte is compressible.
     * Otherwise usable in all CE weight bytes.
     */
    static const uint8_t PRIMARY_COMPRESSION_LOW_BYTE = 3;
    /**
     * Primary compression high terminator.
     * Reserved value in primary second byte if the lead byte is compressible.
     * Otherwise usable in all CE weight bytes.
     */
    static const uint8_t PRIMARY_COMPRESSION_HIGH_BYTE = 0xff;

    /** Default secondary/tertiary weight lead byte. */
    static const uint8_t COMMON_BYTE = 5;
    static const uint32_t COMMON_WEIGHT16 = 0x0500;
    /** Middle 16 bits of a CE with a common secondary weight. */
    static const uint32_t COMMON_SECONDARY_CE = 0x05000000;
    /** Lower 16 bits of a CE with a common tertiary weight. */
    static const uint32_t COMMON_TERTIARY_CE = 0x0500;
    /** Lower 32 bits of a CE with common secondary and tertiary weights. */
    static const uint32_t COMMON_SEC_AND_TER_CE = 0x05000500;

    static const uint32_t SECONDARY_MASK = 0xffff0000;
    static const uint32_t CASE_MASK = 0xc000;
    static const uint32_t SECONDARY_AND_CASE_MASK = SECONDARY_MASK | CASE_MASK;
    /** Only the 2*6 bits for the pure tertiary weight. */
    static const uint32_t ONLY_TERTIARY_MASK = 0x3f3f;
    /** Only the secondary & tertiary bits; no case, no quaternary. */
    static const uint32_t ONLY_SEC_TER_MASK = SECONDARY_MASK | ONLY_TERTIARY_MASK;
    /** Case bits and tertiary bits. */
    static const uint32_t CASE_AND_TERTIARY_MASK = CASE_MASK | ONLY_TERTIARY_MASK;
    static const uint32_t QUATERNARY_MASK = 0xc0;
    /** Case bits and quaternary bits. */
    static const uint32_t CASE_AND_QUATERNARY_MASK = CASE_MASK | QUATERNARY_MASK;

    static const uint8_t UNASSIGNED_IMPLICIT_BYTE = 0xfe;  // compressible
    /**
     * First unassigned: AlphabeticIndex overflow boundary.
     * We want a 3-byte primary so that it fits into the root elements table.
     *
     * This 3-byte primary will not collide with
     * any unassigned-implicit 4-byte primaries because
     * the first few hundred Unicode code points all have real mappings.
     */
    static const uint32_t FIRST_UNASSIGNED_PRIMARY = 0xfe040200;

    static const uint8_t TRAIL_WEIGHT_BYTE = 0xff;  // not compressible
    static const uint32_t FIRST_TRAILING_PRIMARY = 0xff020200;  // [first trailing]
    static const uint32_t MAX_PRIMARY = 0xffff0000;  // U+FFFF
    static const uint32_t MAX_REGULAR_CE32 = 0xffff0505;  // U+FFFF

    // CE32 value for U+FFFD as well as illegal UTF-8 byte sequences (which behave like U+FFFD).
    // We use the third-highest primary weight for U+FFFD (as in UCA 6.3+).
    static const uint32_t FFFD_PRIMARY = MAX_PRIMARY - 0x20000;
    static const uint32_t FFFD_CE32 = MAX_REGULAR_CE32 - 0x20000;

    /**
     * A CE32 is special if its low byte is this or greater.
     * Impossible case bits 11 mark special CE32s.
     * This value itself is used to indicate a fallback to the base collator.
     */
    static const uint8_t SPECIAL_CE32_LOW_BYTE = 0xc0;
    static const uint32_t FALLBACK_CE32 = SPECIAL_CE32_LOW_BYTE;
    /**
     * Low byte of a long-primary special CE32.
     */
    static const uint8_t LONG_PRIMARY_CE32_LOW_BYTE = 0xc1;  // SPECIAL_CE32_LOW_BYTE | LONG_PRIMARY_TAG

    static const uint32_t UNASSIGNED_CE32 = 0xffffffff;  // Compute an unassigned-implicit CE.

    static const uint32_t NO_CE32 = 1;

    /** No CE: End of input. Only used in runtime code, not stored in data. */
    static const uint32_t NO_CE_PRIMARY = 1;  // not a left-adjusted weight
    static const uint32_t NO_CE_WEIGHT16 = 0x0100;  // weight of LEVEL_SEPARATOR_BYTE
    static const int64_t NO_CE = INT64_C(0x101000100);  // NO_CE_PRIMARY, NO_CE_WEIGHT16, NO_CE_WEIGHT16

    /** Sort key levels. */
    enum Level {
        /** Unspecified level. */
        NO_LEVEL,
        PRIMARY_LEVEL,
        SECONDARY_LEVEL,
        CASE_LEVEL,
        TERTIARY_LEVEL,
        QUATERNARY_LEVEL,
        IDENTICAL_LEVEL,
        /** Beyond sort key bytes. */
        ZERO_LEVEL
    };

    /**
     * Sort key level flags: xx_FLAG = 1 << xx_LEVEL.
     * In Java, use enum Level with flag() getters, or use EnumSet rather than hand-made bit sets.
     */
    static const uint32_t NO_LEVEL_FLAG = 1;
    static const uint32_t PRIMARY_LEVEL_FLAG = 2;
    static const uint32_t SECONDARY_LEVEL_FLAG = 4;
    static const uint32_t CASE_LEVEL_FLAG = 8;
    static const uint32_t TERTIARY_LEVEL_FLAG = 0x10;
    static const uint32_t QUATERNARY_LEVEL_FLAG = 0x20;
    static const uint32_t IDENTICAL_LEVEL_FLAG = 0x40;
    static const uint32_t ZERO_LEVEL_FLAG = 0x80;

    /**
     * Special-CE32 tags, from bits 3..0 of a special 32-bit CE.
     * Bits 31..8 are available for tag-specific data.
     * Bits  5..4: Reserved. May be used in the future to indicate lccc!=0 and tccc!=0.
     */
    enum {
        /**
         * Fall back to the base collator.
         * This is the tag value in SPECIAL_CE32_LOW_BYTE and FALLBACK_CE32.
         * Bits 31..8: Unused, 0.
         */
        FALLBACK_TAG = 0,
        /**
         * Long-primary CE with COMMON_SEC_AND_TER_CE.
         * Bits 31..8: Three-byte primary.
         */
        LONG_PRIMARY_TAG = 1,
        /**
         * Long-secondary CE with zero primary.
         * Bits 31..16: Secondary weight.
         * Bits 15.. 8: Tertiary weight.
         */
        LONG_SECONDARY_TAG = 2,
        /**
         * Unused.
         * May be used in the future for single-byte secondary CEs (SHORT_SECONDARY_TAG),
         * storing the secondary in bits 31..24, the ccc in bits 23..16,
         * and the tertiary in bits 15..8.
         */
        RESERVED_TAG_3 = 3,
        /**
         * Latin mini expansions of two simple CEs [pp, 05, tt] [00, ss, 05].
         * Bits 31..24: Single-byte primary weight pp of the first CE.
         * Bits 23..16: Tertiary weight tt of the first CE.
         * Bits 15.. 8: Secondary weight ss of the second CE.
         */
        LATIN_EXPANSION_TAG = 4,
        /**
         * Points to one or more simple/long-primary/long-secondary 32-bit CE32s.
         * Bits 31..13: Index into uint32_t table.
         * Bits 12.. 8: Length=1..31.
         */
        EXPANSION32_TAG = 5,
        /**
         * Points to one or more 64-bit CEs.
         * Bits 31..13: Index into CE table.
         * Bits 12.. 8: Length=1..31.
         */
        EXPANSION_TAG = 6,
        /**
         * Builder data, used only in the CollationDataBuilder, not in runtime data.
         *
         * If bit 8 is 0: Builder context, points to a list of context-sensitive mappings.
         * Bits 31..13: Index to the builder's list of ConditionalCE32 for this character.
         * Bits 12.. 9: Unused, 0.
         *
         * If bit 8 is 1 (IS_BUILDER_JAMO_CE32): Builder-only jamoCE32 value.
         * The builder fetches the Jamo CE32 from the trie.
         * Bits 31..13: Jamo code point.
         * Bits 12.. 9: Unused, 0.
         */
        BUILDER_DATA_TAG = 7,
        /**
         * Points to prefix trie.
         * Bits 31..13: Index into prefix/contraction data.
         * Bits 12.. 8: Unused, 0.
         */
        PREFIX_TAG = 8,
        /**
         * Points to contraction data.
         * Bits 31..13: Index into prefix/contraction data.
         * Bit      12: Unused, 0.
         * Bit      11: CONTRACT_HAS_STARTER flag. (Used by ICU4X only.)
         * Bit      10: CONTRACT_TRAILING_CCC flag.
         * Bit       9: CONTRACT_NEXT_CCC flag.
         * Bit       8: CONTRACT_SINGLE_CP_NO_MATCH flag.
         */
        CONTRACTION_TAG = 9,
        /**
         * Decimal digit.
         * Bits 31..13: Index into uint32_t table for non-numeric-collation CE32.
         * Bit      12: Unused, 0.
         * Bits 11.. 8: Digit value 0..9.
         */
        DIGIT_TAG = 10,
        /**
         * Tag for U+0000, for moving the NUL-termination handling
         * from the regular fastpath into specials-handling code.
         * Bits 31..8: Unused, 0.
         */
        U0000_TAG = 11,
        /**
         * Tag for a Hangul syllable.
         * Bits 31..9: Unused, 0.
         * Bit      8: HANGUL_NO_SPECIAL_JAMO flag.
         */
        HANGUL_TAG = 12,
        /**
         * Tag for a lead surrogate code unit.
         * Optional optimization for UTF-16 string processing.
         * Bits 31..10: Unused, 0.
         *       9.. 8: =0: All associated supplementary code points are unassigned-implicit.
         *              =1: All associated supplementary code points fall back to the base data.
         *              else: (Normally 2) Look up the data for the supplementary code point.
         */
        LEAD_SURROGATE_TAG = 13,
        /**
         * Tag for CEs with primary weights in code point order.
         * Bits 31..13: Index into CE table, for one data "CE".
         * Bits 12.. 8: Unused, 0.
         *
         * This data "CE" has the following bit fields:
         * Bits 63..32: Three-byte primary pppppp00.
         *      31.. 8: Start/base code point of the in-order range.
         *           7: Flag isCompressible primary.
         *       6.. 0: Per-code point primary-weight increment.
         */
        OFFSET_TAG = 14,
        /**
         * Implicit CE tag. Compute an unassigned-implicit CE.
         * All bits are set (UNASSIGNED_CE32=0xffffffff).
         */
        IMPLICIT_TAG = 15
    };

    static UBool isAssignedCE32(uint32_t ce32) {
        return ce32 != FALLBACK_CE32 && ce32 != UNASSIGNED_CE32;
    }

    /**
     * We limit the number of CEs in an expansion
     * so that we can use a small number of length bits in the data structure,
     * and so that an implementation can copy CEs at runtime without growing a destination buffer.
     */
    static const int32_t MAX_EXPANSION_LENGTH = 31;
    static const int32_t MAX_INDEX = 0x7ffff;

    /**
     * Set if there is no match for the single (no-suffix) character itself.
     * This is only possible if there is a prefix.
     * In this case, discontiguous contraction matching cannot add combining marks
     * starting from an empty suffix.
     * The default CE32 is used anyway if there is no suffix match.
     */
    static const uint32_t CONTRACT_SINGLE_CP_NO_MATCH = 0x100;
    /** Set if the first character of every contraction suffix has lccc!=0. */
    static const uint32_t CONTRACT_NEXT_CCC = 0x200;
    /** Set if any contraction suffix ends with lccc!=0. */
    static const uint32_t CONTRACT_TRAILING_CCC = 0x400;
    /** Set if any contraction suffix contains a starter. (Used by ICU4X only.) */
    static const uint32_t CONTRACT_HAS_STARTER = 0x800;

    /** For HANGUL_TAG: None of its Jamo CE32s isSpecialCE32(). */
    static const uint32_t HANGUL_NO_SPECIAL_JAMO = 0x100;

    static const uint32_t LEAD_ALL_UNASSIGNED = 0;
    static const uint32_t LEAD_ALL_FALLBACK = 0x100;
    static const uint32_t LEAD_MIXED = 0x200;
    static const uint32_t LEAD_TYPE_MASK = 0x300;

    static uint32_t makeLongPrimaryCE32(uint32_t p) { return p | LONG_PRIMARY_CE32_LOW_BYTE; }

    /** Turns the long-primary CE32 into a primary weight pppppp00. */
    static inline uint32_t primaryFromLongPrimaryCE32(uint32_t ce32) {
        return ce32 & 0xffffff00;
    }
    static inline int64_t ceFromLongPrimaryCE32(uint32_t ce32) {
        return (static_cast<int64_t>(ce32 & 0xffffff00) << 32) | COMMON_SEC_AND_TER_CE;
    }

    static uint32_t makeLongSecondaryCE32(uint32_t lower32) {
        return lower32 | SPECIAL_CE32_LOW_BYTE | LONG_SECONDARY_TAG;
    }
    static inline int64_t ceFromLongSecondaryCE32(uint32_t ce32) {
        return ce32 & 0xffffff00;
    }

    /** Makes a special CE32 with tag, index and length. */
    static uint32_t makeCE32FromTagIndexAndLength(int32_t tag, int32_t index, int32_t length) {
        return (index << 13) | (length << 8) | SPECIAL_CE32_LOW_BYTE | tag;
    }
    /** Makes a special CE32 with only tag and index. */
    static uint32_t makeCE32FromTagAndIndex(int32_t tag, int32_t index) {
        return (index << 13) | SPECIAL_CE32_LOW_BYTE | tag;
    }

    static inline UBool isSpecialCE32(uint32_t ce32) {
        return (ce32 & 0xff) >= SPECIAL_CE32_LOW_BYTE;
    }

    static inline int32_t tagFromCE32(uint32_t ce32) {
        return static_cast<int32_t>(ce32 & 0xf);
    }

    static inline UBool hasCE32Tag(uint32_t ce32, int32_t tag) {
        return isSpecialCE32(ce32) && tagFromCE32(ce32) == tag;
    }

    static inline UBool isLongPrimaryCE32(uint32_t ce32) {
        return hasCE32Tag(ce32, LONG_PRIMARY_TAG);
    }

    static UBool isSimpleOrLongCE32(uint32_t ce32) {
        return !isSpecialCE32(ce32) ||
                tagFromCE32(ce32) == LONG_PRIMARY_TAG ||
                tagFromCE32(ce32) == LONG_SECONDARY_TAG;
    }

    /**
     * @return true if the ce32 yields one or more CEs without further data lookups
     */
    static UBool isSelfContainedCE32(uint32_t ce32) {
        return !isSpecialCE32(ce32) ||
                tagFromCE32(ce32) == LONG_PRIMARY_TAG ||
                tagFromCE32(ce32) == LONG_SECONDARY_TAG ||
                tagFromCE32(ce32) == LATIN_EXPANSION_TAG;
    }

    static inline UBool isPrefixCE32(uint32_t ce32) {
        return hasCE32Tag(ce32, PREFIX_TAG);
    }

    static inline UBool isContractionCE32(uint32_t ce32) {
        return hasCE32Tag(ce32, CONTRACTION_TAG);
    }

    static inline UBool ce32HasContext(uint32_t ce32) {
        return isSpecialCE32(ce32) &&
                (tagFromCE32(ce32) == PREFIX_TAG ||
                tagFromCE32(ce32) == CONTRACTION_TAG);
    }

    /**
     * Get the first of the two Latin-expansion CEs encoded in ce32.
     * @see LATIN_EXPANSION_TAG
     */
    static inline int64_t latinCE0FromCE32(uint32_t ce32) {
        return (static_cast<int64_t>(ce32 & 0xff000000) << 32) | COMMON_SECONDARY_CE | ((ce32 & 0xff0000) >> 8);
    }

    /**
     * Get the second of the two Latin-expansion CEs encoded in ce32.
     * @see LATIN_EXPANSION_TAG
     */
    static inline int64_t latinCE1FromCE32(uint32_t ce32) {
        return ((ce32 & 0xff00) << 16) | COMMON_TERTIARY_CE;
    }

    /**
     * Returns the data index from a special CE32.
     */
    static inline int32_t indexFromCE32(uint32_t ce32) {
        return static_cast<int32_t>(ce32 >> 13);
    }

    /**
     * Returns the data length from a ce32.
     */
    static inline int32_t lengthFromCE32(uint32_t ce32) {
        return (ce32 >> 8) & 31;
    }

    /**
     * Returns the digit value from a DIGIT_TAG ce32.
     */
    static inline char digitFromCE32(uint32_t ce32) {
        return static_cast<char>((ce32 >> 8) & 0xf);
    }

    /** Returns a 64-bit CE from a simple CE32 (not special). */
    static inline int64_t ceFromSimpleCE32(uint32_t ce32) {
        // normal form ppppsstt -> pppp0000ss00tt00
        // assert (ce32 & 0xff) < SPECIAL_CE32_LOW_BYTE
        return (static_cast<int64_t>(ce32 & 0xffff0000) << 32) | ((ce32 & 0xff00) << 16) | ((ce32 & 0xff) << 8);
    }

    /** Returns a 64-bit CE from a simple/long-primary/long-secondary CE32. */
    static inline int64_t ceFromCE32(uint32_t ce32) {
        uint32_t tertiary = ce32 & 0xff;
        if(tertiary < SPECIAL_CE32_LOW_BYTE) {
            // normal form ppppsstt -> pppp0000ss00tt00
            return (static_cast<int64_t>(ce32 & 0xffff0000) << 32) | ((ce32 & 0xff00) << 16) | (tertiary << 8);
        } else {
            ce32 -= tertiary;
            if((tertiary & 0xf) == LONG_PRIMARY_TAG) {
                // long-primary form ppppppC1 -> pppppp00050000500
                return (static_cast<int64_t>(ce32) << 32) | COMMON_SEC_AND_TER_CE;
            } else {
                // long-secondary form ssssttC2 -> 00000000sssstt00
                // assert (tertiary & 0xf) == LONG_SECONDARY_TAG
                return ce32;
            }
        }
    }

    /** Creates a CE from a primary weight. */
    static inline int64_t makeCE(uint32_t p) {
        return (static_cast<int64_t>(p) << 32) | COMMON_SEC_AND_TER_CE;
    }
    /**
     * Creates a CE from a primary weight,
     * 16-bit secondary/tertiary weights, and a 2-bit quaternary.
     */
    static inline int64_t makeCE(uint32_t p, uint32_t s, uint32_t t, uint32_t q) {
        return (static_cast<int64_t>(p) << 32) | (s << 16) | t | (q << 6);
    }

    /**
     * Increments a 2-byte primary by a code point offset.
     */
    static uint32_t incTwoBytePrimaryByOffset(uint32_t basePrimary, UBool isCompressible,
                                              int32_t offset);

    /**
     * Increments a 3-byte primary by a code point offset.
     */
    static uint32_t incThreeBytePrimaryByOffset(uint32_t basePrimary, UBool isCompressible,
                                                int32_t offset);

    /**
     * Decrements a 2-byte primary by one range step (1..0x7f).
     */
    static uint32_t decTwoBytePrimaryByOneStep(uint32_t basePrimary, UBool isCompressible, int32_t step);

    /**
     * Decrements a 3-byte primary by one range step (1..0x7f).
     */
    static uint32_t decThreeBytePrimaryByOneStep(uint32_t basePrimary, UBool isCompressible, int32_t step);

    /**
     * Computes a 3-byte primary for c's OFFSET_TAG data "CE".
     */
    static uint32_t getThreeBytePrimaryForOffsetData(UChar32 c, int64_t dataCE);

    /**
     * Returns the unassigned-character implicit primary weight for any valid code point c.
     */
    static uint32_t unassignedPrimaryFromCodePoint(UChar32 c);

    static inline int64_t unassignedCEFromCodePoint(UChar32 c) {
        return makeCE(unassignedPrimaryFromCodePoint(c));
    }

private:
    Collation() = delete;  // No instantiation.
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATION_H__
