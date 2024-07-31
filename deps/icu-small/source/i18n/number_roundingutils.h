// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_ROUNDINGUTILS_H__
#define __NUMBER_ROUNDINGUTILS_H__

#include "number_types.h"
#include "string_segment.h"

U_NAMESPACE_BEGIN
namespace number::impl {
namespace roundingutils {

enum Section {
    SECTION_LOWER_EDGE = -1,
    SECTION_UPPER_EDGE = -2,
    SECTION_LOWER = 1,
    SECTION_MIDPOINT = 2,
    SECTION_UPPER = 3
};

/**
 * Converts a rounding mode and metadata about the quantity being rounded to a boolean determining
 * whether the value should be rounded toward infinity or toward zero.
 *
 * <p>The parameters are of type int because benchmarks on an x86-64 processor against OpenJDK
 * showed that ints were demonstrably faster than enums in switch statements.
 *
 * @param isEven Whether the digit immediately before the rounding magnitude is even.
 * @param isNegative Whether the quantity is negative.
 * @param section Whether the part of the quantity to the right of the rounding magnitude is
 *     exactly halfway between two digits, whether it is in the lower part (closer to zero), or
 *     whether it is in the upper part (closer to infinity). See {@link #SECTION_LOWER}, {@link
 *     #SECTION_MIDPOINT}, and {@link #SECTION_UPPER}.
 * @param roundingMode The integer version of the {@link RoundingMode}, which you can get via
 *     {@link RoundingMode#ordinal}.
 * @param status Error code, set to U_FORMAT_INEXACT_ERROR if the rounding mode is kRoundUnnecessary.
 * @return true if the number should be rounded toward zero; false if it should be rounded toward
 *     infinity.
 */
inline bool
getRoundingDirection(bool isEven, bool isNegative, Section section, RoundingMode roundingMode,
                     UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    switch (roundingMode) {
        case RoundingMode::UNUM_ROUND_UP:
            // round away from zero
            return false;

        case RoundingMode::UNUM_ROUND_DOWN:
            // round toward zero
            return true;

        case RoundingMode::UNUM_ROUND_CEILING:
            // round toward positive infinity
            return isNegative;

        case RoundingMode::UNUM_ROUND_FLOOR:
            // round toward negative infinity
            return !isNegative;

        case RoundingMode::UNUM_ROUND_HALFUP:
            switch (section) {
                case SECTION_MIDPOINT:
                    return false;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        case RoundingMode::UNUM_ROUND_HALFDOWN:
            switch (section) {
                case SECTION_MIDPOINT:
                    return true;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        case RoundingMode::UNUM_ROUND_HALFEVEN:
            switch (section) {
                case SECTION_MIDPOINT:
                    return isEven;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        case RoundingMode::UNUM_ROUND_HALF_ODD:
            switch (section) {
                case SECTION_MIDPOINT:
                    return !isEven;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        case RoundingMode::UNUM_ROUND_HALF_CEILING:
            switch (section) {
                case SECTION_MIDPOINT:
                    return isNegative;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        case RoundingMode::UNUM_ROUND_HALF_FLOOR:
            switch (section) {
                case SECTION_MIDPOINT:
                    return !isNegative;
                case SECTION_LOWER:
                    return true;
                case SECTION_UPPER:
                    return false;
                default:
                    break;
            }
            break;

        default:
            break;
    }

    status = U_FORMAT_INEXACT_ERROR;
    return false;
}

/**
 * Gets whether the given rounding mode's rounding boundary is at the midpoint. The rounding
 * boundary is the point at which a number switches from being rounded down to being rounded up.
 * For example, with rounding mode HALF_EVEN, HALF_UP, or HALF_DOWN, the rounding boundary is at
 * the midpoint, and this function would return true. However, for UP, DOWN, CEILING, and FLOOR,
 * the rounding boundary is at the "edge", and this function would return false.
 *
 * @param roundingMode The integer version of the {@link RoundingMode}.
 * @return true if rounding mode is HALF_EVEN, HALF_UP, or HALF_DOWN; false otherwise.
 */
inline bool roundsAtMidpoint(int roundingMode) {
    switch (roundingMode) {
        case RoundingMode::UNUM_ROUND_UP:
        case RoundingMode::UNUM_ROUND_DOWN:
        case RoundingMode::UNUM_ROUND_CEILING:
        case RoundingMode::UNUM_ROUND_FLOOR:
            return false;

        default:
            return true;
    }
}

} // namespace roundingutils


/**
 * Encapsulates a Precision and a RoundingMode and performs rounding on a DecimalQuantity.
 *
 * This class does not exist in Java: instead, the base Precision class is used.
 */
class RoundingImpl {
  public:
    RoundingImpl() = default;  // defaults to pass-through rounder

    RoundingImpl(const Precision& precision, UNumberFormatRoundingMode roundingMode,
                 const CurrencyUnit& currency, UErrorCode& status);

    static RoundingImpl passThrough();

    /** Required for ScientificFormatter */
    bool isSignificantDigits() const;

    /**
     * Rounding endpoint used by Engineering and Compact notation. Chooses the most appropriate multiplier (magnitude
     * adjustment), applies the adjustment, rounds, and returns the chosen multiplier.
     *
     * <p>
     * In most cases, this is simple. However, when rounding the number causes it to cross a multiplier boundary, we
     * need to re-do the rounding. For example, to display 999,999 in Engineering notation with 2 sigfigs, first you
     * guess the multiplier to be -3. However, then you end up getting 1000E3, which is not the correct output. You then
     * change your multiplier to be -6, and you get 1.0E6, which is correct.
     *
     * @param input The quantity to process.
     * @param producer Function to call to return a multiplier based on a magnitude.
     * @return The number of orders of magnitude the input was adjusted by this method.
     */
    int32_t
    chooseMultiplierAndApply(impl::DecimalQuantity &input, const impl::MultiplierProducer &producer,
                             UErrorCode &status);

    void apply(impl::DecimalQuantity &value, UErrorCode &status) const;

    /** Version of {@link #apply} that obeys minInt constraints. Used for scientific notation compatibility mode. */
    void apply(impl::DecimalQuantity &value, int32_t minInt, UErrorCode status);

  private:
    Precision fPrecision;
    UNumberFormatRoundingMode fRoundingMode;
    bool fPassThrough = true;  // default value

    // Permits access to fPrecision.
    friend class units::UnitsRouter;

    // Permits access to fPrecision.
    friend class UnitConversionHandler;
};

/**
 * Parses Precision-related skeleton strings without knowledge of MacroProps
 * - see blueprint_helpers::parseIncrementOption().
 *
 * Referencing MacroProps means needing to pull in the .o files that have the
 * destructors for the SymbolsWrapper, StringProp, and Scale classes.
 */
void parseIncrementOption(const StringSegment &segment, Precision &outPrecision, UErrorCode &status);

} // namespace number::impl
U_NAMESPACE_END

#endif //__NUMBER_ROUNDINGUTILS_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
