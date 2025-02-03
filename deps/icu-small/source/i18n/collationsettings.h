// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2013-2015, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationsettings.h
*
* created on: 2013feb07
* created by: Markus W. Scherer
*/

#ifndef __COLLATIONSETTINGS_H__
#define __COLLATIONSETTINGS_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/ucol.h"
#include "collation.h"
#include "sharedobject.h"
#include "umutex.h"

U_NAMESPACE_BEGIN

struct CollationData;

/**
 * Collation settings/options/attributes.
 * These are the values that can be changed via API.
 */
struct U_I18N_API CollationSettings : public SharedObject {
    /**
     * Options bit 0: Perform the FCD check on the input text and deliver normalized text.
     */
    static const int32_t CHECK_FCD = 1;
    /**
     * Options bit 1: Numeric collation.
     * Also known as CODAN = COllate Digits As Numbers.
     *
     * Treat digit sequences as numbers with CE sequences in numeric order,
     * rather than returning a normal CE for each digit.
     */
    static const int32_t NUMERIC = 2;
    /**
     * "Shifted" alternate handling, see ALTERNATE_MASK.
     */
    static const int32_t SHIFTED = 4;
    /**
     * Options bits 3..2: Alternate-handling mask. 0 for non-ignorable.
     * Reserve values 8 and 0xc for shift-trimmed and blanked.
     */
    static const int32_t ALTERNATE_MASK = 0xc;
    /**
     * Options bits 6..4: The 3-bit maxVariable value bit field is shifted by this value.
     */
    static const int32_t MAX_VARIABLE_SHIFT = 4;
    /** maxVariable options bit mask before shifting. */
    static const int32_t MAX_VARIABLE_MASK = 0x70;
    /** Options bit 7: Reserved/unused/0. */
    /**
     * Options bit 8: Sort uppercase first if caseLevel or caseFirst is on.
     */
    static const int32_t UPPER_FIRST = 0x100;
    /**
     * Options bit 9: Keep the case bits in the tertiary weight (they trump other tertiary values)
     * unless case level is on (when they are *moved* into the separate case level).
     * By default, the case bits are removed from the tertiary weight (ignored).
     *
     * When CASE_FIRST is off, UPPER_FIRST must be off too, corresponding to
     * the tri-value UCOL_CASE_FIRST attribute: UCOL_OFF vs. UCOL_LOWER_FIRST vs. UCOL_UPPER_FIRST.
     */
    static const int32_t CASE_FIRST = 0x200;
    /**
     * Options bit mask for caseFirst and upperFirst, before shifting.
     * Same value as caseFirst==upperFirst.
     */
    static const int32_t CASE_FIRST_AND_UPPER_MASK = CASE_FIRST | UPPER_FIRST;
    /**
     * Options bit 10: Insert the case level between the secondary and tertiary levels.
     */
    static const int32_t CASE_LEVEL = 0x400;
    /**
     * Options bit 11: Compare secondary weights backwards. ("French secondary")
     */
    static const int32_t BACKWARD_SECONDARY = 0x800;
    /**
     * Options bits 15..12: The 4-bit strength value bit field is shifted by this value.
     * It is the top used bit field in the options. (No need to mask after shifting.)
     */
    static const int32_t STRENGTH_SHIFT = 12;
    /** Strength options bit mask before shifting. */
    static const int32_t STRENGTH_MASK = 0xf000;

    /** maxVariable values */
    enum MaxVariable {
        MAX_VAR_SPACE,
        MAX_VAR_PUNCT,
        MAX_VAR_SYMBOL,
        MAX_VAR_CURRENCY
    };

    CollationSettings()
            : options((UCOL_DEFAULT_STRENGTH << STRENGTH_SHIFT) |
                      (MAX_VAR_PUNCT << MAX_VARIABLE_SHIFT)),
              variableTop(0),
              reorderTable(nullptr),
              minHighNoReorder(0),
              reorderRanges(nullptr), reorderRangesLength(0),
              reorderCodes(nullptr), reorderCodesLength(0), reorderCodesCapacity(0),
              fastLatinOptions(-1) {}

    CollationSettings(const CollationSettings &other);
    virtual ~CollationSettings();

    bool operator==(const CollationSettings &other) const;

    inline bool operator!=(const CollationSettings &other) const {
        return !operator==(other);
    }

    int32_t hashCode() const;

    void resetReordering();
    void aliasReordering(const CollationData &data, const int32_t *codes, int32_t length,
                         const uint32_t *ranges, int32_t rangesLength,
                         const uint8_t *table, UErrorCode &errorCode);
    void setReordering(const CollationData &data, const int32_t *codes, int32_t codesLength,
                       UErrorCode &errorCode);
    void copyReorderingFrom(const CollationSettings &other, UErrorCode &errorCode);

    inline UBool hasReordering() const { return reorderTable != nullptr; }
    static UBool reorderTableHasSplitBytes(const uint8_t table[256]);
    inline uint32_t reorder(uint32_t p) const {
        uint8_t b = reorderTable[p >> 24];
        if(b != 0 || p <= Collation::NO_CE_PRIMARY) {
            return (static_cast<uint32_t>(b) << 24) | (p & 0xffffff);
        } else {
            return reorderEx(p);
        }
    }

    void setStrength(int32_t value, int32_t defaultOptions, UErrorCode &errorCode);

    static int32_t getStrength(int32_t options) {
        return options >> STRENGTH_SHIFT;
    }

    int32_t getStrength() const {
        return getStrength(options);
    }

    /** Sets the options bit for an on/off attribute. */
    void setFlag(int32_t bit, UColAttributeValue value,
                 int32_t defaultOptions, UErrorCode &errorCode);

    UColAttributeValue getFlag(int32_t bit) const {
        return ((options & bit) != 0) ? UCOL_ON : UCOL_OFF;
    }

    void setCaseFirst(UColAttributeValue value, int32_t defaultOptions, UErrorCode &errorCode);

    UColAttributeValue getCaseFirst() const {
        int32_t option = options & CASE_FIRST_AND_UPPER_MASK;
        return (option == 0) ? UCOL_OFF :
                (option == CASE_FIRST) ? UCOL_LOWER_FIRST : UCOL_UPPER_FIRST;
    }

    void setAlternateHandling(UColAttributeValue value,
                              int32_t defaultOptions, UErrorCode &errorCode);

    UColAttributeValue getAlternateHandling() const {
        return ((options & ALTERNATE_MASK) == 0) ? UCOL_NON_IGNORABLE : UCOL_SHIFTED;
    }

    void setMaxVariable(int32_t value, int32_t defaultOptions, UErrorCode &errorCode);

    MaxVariable getMaxVariable() const {
        return static_cast<MaxVariable>((options & MAX_VARIABLE_MASK) >> MAX_VARIABLE_SHIFT);
    }

    /**
     * Include case bits in the tertiary level if caseLevel=off and caseFirst!=off.
     */
    static inline UBool isTertiaryWithCaseBits(int32_t options) {
        return (options & (CASE_LEVEL | CASE_FIRST)) == CASE_FIRST;
    }
    static uint32_t getTertiaryMask(int32_t options) {
        // Remove the case bits from the tertiary weight when caseLevel is on or caseFirst is off.
        return isTertiaryWithCaseBits(options) ?
                Collation::CASE_AND_TERTIARY_MASK : Collation::ONLY_TERTIARY_MASK;
    }

    static UBool sortsTertiaryUpperCaseFirst(int32_t options) {
        // On tertiary level, consider case bits and sort uppercase first
        // if caseLevel is off and caseFirst==upperFirst.
        return (options & (CASE_LEVEL | CASE_FIRST_AND_UPPER_MASK)) == CASE_FIRST_AND_UPPER_MASK;
    }

    inline UBool dontCheckFCD() const {
        return (options & CHECK_FCD) == 0;
    }

    inline UBool hasBackwardSecondary() const {
        return (options & BACKWARD_SECONDARY) != 0;
    }

    inline UBool isNumeric() const {
        return (options & NUMERIC) != 0;
    }

    /** CHECK_FCD etc. */
    int32_t options;
    /** Variable-top primary weight. */
    uint32_t variableTop;
    /**
     * 256-byte table for reordering permutation of primary lead bytes; nullptr if no reordering.
     * A 0 entry at a non-zero index means that the primary lead byte is "split"
     * (there are different offsets for primaries that share that lead byte)
     * and the reordering offset must be determined via the reorderRanges.
     */
    const uint8_t *reorderTable;
    /** Limit of last reordered range. 0 if no reordering or no split bytes. */
    uint32_t minHighNoReorder;
    /**
     * Primary-weight ranges for script reordering,
     * to be used by reorder(p) for split-reordered primary lead bytes.
     *
     * Each entry is a (limit, offset) pair.
     * The upper 16 bits of the entry are the upper 16 bits of the
     * exclusive primary limit of a range.
     * Primaries between the previous limit and this one have their lead bytes
     * modified by the signed offset (-0xff..+0xff) stored in the lower 16 bits.
     *
     * CollationData::makeReorderRanges() writes a full list where the first range
     * (at least for terminators and separators) has a 0 offset.
     * The last range has a non-zero offset.
     * minHighNoReorder is set to the limit of that last range.
     *
     * In the settings object, the initial ranges before the first split lead byte
     * are omitted for efficiency; they are handled by reorder(p) via the reorderTable.
     * If there are no split-reordered lead bytes, then no ranges are needed.
     */
    const uint32_t *reorderRanges;
    int32_t reorderRangesLength;
    /** Array of reorder codes; ignored if reorderCodesLength == 0. */
    const int32_t *reorderCodes;
    /** Number of reorder codes; 0 if no reordering. */
    int32_t reorderCodesLength;
    /**
     * Capacity of reorderCodes.
     * If 0, then the codes, the ranges, and the table are aliases.
     * Otherwise, this object owns the memory via the reorderCodes pointer;
     * the codes, the ranges, and the table are in the same memory block, in that order.
     */
    int32_t reorderCodesCapacity;

    /** Options for CollationFastLatin. Negative if disabled. */
    int32_t fastLatinOptions;
    uint16_t fastLatinPrimaries[0x180];

private:
    void setReorderArrays(const int32_t *codes, int32_t codesLength,
                          const uint32_t *ranges, int32_t rangesLength,
                          const uint8_t *table, UErrorCode &errorCode);
    uint32_t reorderEx(uint32_t p) const;
};

U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
#endif  // __COLLATIONSETTINGS_H__
