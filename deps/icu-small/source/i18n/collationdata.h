// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2010-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationdata.h
*
* created on: 2010oct27
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONDATA_H__
#define __COLLATIONDATA_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "unicode/uniset.h"
#include "collation.h"
#include "normalizer2impl.h"
#include "utrie2.h"

struct UDataMemory;

U_NAMESPACE_BEGIN

class UVector32;

/**
 * Collation data container.
 * Immutable data created by a CollationDataBuilder, or loaded from a file,
 * or deserialized from API-provided binary data.
 *
 * Includes data for the collation base (root/default), aliased if this is not the base.
 */
struct U_I18N_API CollationData : public UMemory {
    // Note: The ucadata.icu loader could discover the reserved ranges by setting an array
    // parallel with the ranges, and resetting ranges that are indexed.
    // The reordering builder code could clone the resulting template array.
    static constexpr int32_t REORDER_RESERVED_BEFORE_LATIN = UCOL_REORDER_CODE_FIRST + 14;
    static constexpr int32_t REORDER_RESERVED_AFTER_LATIN = REORDER_RESERVED_BEFORE_LATIN + 1;

    static constexpr int32_t MAX_NUM_SPECIAL_REORDER_CODES = 8;
    /** C++ only, data reader check scriptStartsLength. */
    static constexpr int32_t MAX_NUM_SCRIPT_RANGES = 256;

    CollationData(const Normalizer2Impl &nfc)
            : trie(nullptr),
              ce32s(nullptr), ces(nullptr), contexts(nullptr), base(nullptr),
              jamoCE32s(nullptr),
              nfcImpl(nfc),
              numericPrimary(0x12000000),
              ce32sLength(0), cesLength(0), contextsLength(0),
              compressibleBytes(nullptr),
              unsafeBackwardSet(nullptr),
              fastLatinTable(nullptr), fastLatinTableLength(0),
              numScripts(0), scriptsIndex(nullptr), scriptStarts(nullptr), scriptStartsLength(0),
              rootElements(nullptr), rootElementsLength(0) {}

    uint32_t getCE32(UChar32 c) const {
        return UTRIE2_GET32(trie, c);
    }

    uint32_t getCE32FromSupplementary(UChar32 c) const {
        return UTRIE2_GET32_FROM_SUPP(trie, c);
    }

    UBool isDigit(UChar32 c) const {
        return c < 0x660 ? c <= 0x39 && 0x30 <= c :
                Collation::hasCE32Tag(getCE32(c), Collation::DIGIT_TAG);
    }

    UBool isUnsafeBackward(UChar32 c, UBool numeric) const {
        return unsafeBackwardSet->contains(c) || (numeric && isDigit(c));
    }

    UBool isCompressibleLeadByte(uint32_t b) const {
        return compressibleBytes[b];
    }

    inline UBool isCompressiblePrimary(uint32_t p) const {
        return isCompressibleLeadByte(p >> 24);
    }

    /**
     * Returns the CE32 from two contexts words.
     * Access to the defaultCE32 for contraction and prefix matching.
     */
    static uint32_t readCE32(const char16_t *p) {
        return (static_cast<uint32_t>(p[0]) << 16) | p[1];
    }

    /**
     * Returns the CE32 for an indirect special CE32 (e.g., with DIGIT_TAG).
     * Requires that ce32 is special.
     */
    uint32_t getIndirectCE32(uint32_t ce32) const;
    /**
     * Returns the CE32 for an indirect special CE32 (e.g., with DIGIT_TAG),
     * if ce32 is special.
     */
    uint32_t getFinalCE32(uint32_t ce32) const;

    /**
     * Computes a CE from c's ce32 which has the OFFSET_TAG.
     */
    int64_t getCEFromOffsetCE32(UChar32 c, uint32_t ce32) const {
        int64_t dataCE = ces[Collation::indexFromCE32(ce32)];
        return Collation::makeCE(Collation::getThreeBytePrimaryForOffsetData(c, dataCE));
    }

    /**
     * Returns the single CE that c maps to.
     * Sets U_UNSUPPORTED_ERROR if c does not map to a single CE.
     */
    int64_t getSingleCE(UChar32 c, UErrorCode &errorCode) const;

    /**
     * Returns the FCD16 value for code point c. c must be >= 0.
     */
    uint16_t getFCD16(UChar32 c) const {
        return nfcImpl.getFCD16(c);
    }

    /**
     * Returns the first primary for the script's reordering group.
     * @return the primary with only the first primary lead byte of the group
     *         (not necessarily an actual root collator primary weight),
     *         or 0 if the script is unknown
     */
    uint32_t getFirstPrimaryForGroup(int32_t script) const;

    /**
     * Returns the last primary for the script's reordering group.
     * @return the last primary of the group
     *         (not an actual root collator primary weight),
     *         or 0 if the script is unknown
     */
    uint32_t getLastPrimaryForGroup(int32_t script) const;

    /**
     * Finds the reordering group which contains the primary weight.
     * @return the first script of the group, or -1 if the weight is beyond the last group
     */
    int32_t getGroupForPrimary(uint32_t p) const;

    int32_t getEquivalentScripts(int32_t script,
                                 int32_t dest[], int32_t capacity, UErrorCode &errorCode) const;

    /**
     * Writes the permutation of primary-weight ranges
     * for the given reordering of scripts and groups.
     * The caller checks for illegal arguments and
     * takes care of [DEFAULT] and memory allocation.
     *
     * Each list element will be a (limit, offset) pair as described
     * for the CollationSettings::reorderRanges.
     * The list will be empty if no ranges are reordered.
     */
    void makeReorderRanges(const int32_t *reorder, int32_t length,
                           UVector32 &ranges, UErrorCode &errorCode) const;

    /** @see jamoCE32s */
    static const int32_t JAMO_CE32S_LENGTH = 19 + 21 + 27;

    /** Main lookup trie. */
    const UTrie2 *trie;
    /**
     * Array of CE32 values.
     * At index 0 there must be CE32(U+0000)
     * to support U+0000's special-tag for NUL-termination handling.
     */
    const uint32_t *ce32s;
    /** Array of CE values for expansions and OFFSET_TAG. */
    const int64_t *ces;
    /** Array of prefix and contraction-suffix matching data. */
    const char16_t *contexts;
    /** Base collation data, or nullptr if this data itself is a base. */
    const CollationData *base;
    /**
     * Simple array of JAMO_CE32S_LENGTH=19+21+27 CE32s, one per canonical Jamo L/V/T.
     * They are normally simple CE32s, rarely expansions.
     * For fast handling of HANGUL_TAG.
     */
    const uint32_t *jamoCE32s;
    const Normalizer2Impl &nfcImpl;
    /** The single-byte primary weight (xx000000) for numeric collation. */
    uint32_t numericPrimary;

    int32_t ce32sLength;
    int32_t cesLength;
    int32_t contextsLength;

    /** 256 flags for which primary-weight lead bytes are compressible. */
    const UBool *compressibleBytes;
    /**
     * Set of code points that are unsafe for starting string comparison after an identical prefix,
     * or in backwards CE iteration.
     */
    const UnicodeSet *unsafeBackwardSet;

    /**
     * Fast Latin table for common-Latin-text string comparisons.
     * Data structure see class CollationFastLatin.
     */
    const uint16_t *fastLatinTable;
    int32_t fastLatinTableLength;

    /**
     * Data for scripts and reordering groups.
     * Uses include building a reordering permutation table and
     * providing script boundaries to AlphabeticIndex.
     */
    int32_t numScripts;
    /**
     * The length of scriptsIndex is numScripts+16.
     * It maps from a UScriptCode or a special reorder code to an entry in scriptStarts.
     * 16 special reorder codes (not all used) are mapped starting at numScripts.
     * Up to MAX_NUM_SPECIAL_REORDER_CODES are codes for special groups like space/punct/digit.
     * There are special codes at the end for reorder-reserved primary ranges.
     *
     * Multiple scripts may share a range and index, for example Hira & Kana.
     */
    const uint16_t *scriptsIndex;
    /**
     * Start primary weight (top 16 bits only) for a group/script/reserved range
     * indexed by scriptsIndex.
     * The first range (separators & terminators) and the last range (trailing weights)
     * are not reorderable, and no scriptsIndex entry points to them.
     */
    const uint16_t *scriptStarts;
    int32_t scriptStartsLength;

    /**
     * Collation elements in the root collator.
     * Used by the CollationRootElements class. The data structure is described there.
     * nullptr in a tailoring.
     */
    const uint32_t *rootElements;
    int32_t rootElementsLength;

private:
    int32_t getScriptIndex(int32_t script) const;
    void makeReorderRanges(const int32_t *reorder, int32_t length,
                           UBool latinMustMove,
                           UVector32 &ranges, UErrorCode &errorCode) const;
    int32_t addLowScriptRange(uint8_t table[], int32_t index, int32_t lowStart) const;
    int32_t addHighScriptRange(uint8_t table[], int32_t index, int32_t highLimit) const;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONDATA_H__
