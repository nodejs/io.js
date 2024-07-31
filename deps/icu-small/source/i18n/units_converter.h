// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __UNITS_CONVERTER_H__
#define __UNITS_CONVERTER_H__

#include "cmemory.h"
#include "measunit_impl.h"
#include "unicode/errorcode.h"
#include "unicode/stringpiece.h"
#include "unicode/uobject.h"
#include "units_converter.h"
#include "units_data.h"

U_NAMESPACE_BEGIN
namespace units {

/* Internal Structure */

// Constants corresponding to unitConstants in CLDR's units.xml.
enum Constants {
    CONSTANT_FT2M,       // ft_to_m
    CONSTANT_PI,         // PI
    CONSTANT_GRAVITY,    // Gravity of earth (9.80665 m/s^2), "g".
    CONSTANT_G,          // Newtonian constant of gravitation, "G".
    CONSTANT_GAL_IMP2M3, // Gallon imp to m3
    CONSTANT_LB2KG,      // Pound to Kilogram
    CONSTANT_GLUCOSE_MOLAR_MASS,
    CONSTANT_ITEM_PER_MOLE,
    CONSTANT_METERS_PER_AU,
    CONSTANT_SEC_PER_JULIAN_YEAR,
    CONSTANT_SPEED_OF_LIGHT_METERS_PER_SECOND,
    CONSTANT_SHO_TO_M3,   // https://en.wikipedia.org/wiki/Japanese_units_of_measurement
    CONSTANT_TSUBO_TO_M2, // https://en.wikipedia.org/wiki/Japanese_units_of_measurement
    CONSTANT_SHAKU_TO_M,  // https://en.wikipedia.org/wiki/Japanese_units_of_measurement
    CONSTANT_AMU,         // Atomic Mass Unit https://www.nist.gov/pml/special-publication-811/nist-guide-si-chapter-5-units-outside-si#table7

    // Must be the last element.
    CONSTANTS_COUNT
};

// These values are a hard-coded subset of unitConstants in the units
// resources file. A unit test checks that all constants in the resource
// file are at least recognised by the code. Derived constants' values or
// hard-coded derivations are not checked.
// In ICU4J, these constants live in UnitConverter.Factor.getConversionRate().
static const double constantsValues[CONSTANTS_COUNT] = {
    0.3048,                    // CONSTANT_FT2M
    411557987.0 / 131002976.0, // CONSTANT_PI
    9.80665,                   // CONSTANT_GRAVITY
    6.67408E-11,               // CONSTANT_G
    0.00454609,                // CONSTANT_GAL_IMP2M3
    0.45359237,                // CONSTANT_LB2KG
    180.1557,                  // CONSTANT_GLUCOSE_MOLAR_MASS
    6.02214076E+23,            // CONSTANT_ITEM_PER_MOLE
    149597870700,              // CONSTANT_METERS_PER_AU
    31557600,                  // CONSTANT_SEC_PER_JULIAN_YEAR
    299792458,                 // CONSTANT_SPEED_OF_LIGHT_METERS_PER_SECOND
    2401.0 / (1331.0 * 1000.0),
    400.0 / 121.0,
    4.0 / 121.0,
    1.66053878283E-27,         // CONSTANT_AMU
};

typedef enum Signum {
    NEGATIVE = -1,
    POSITIVE = 1,
} Signum;

/* Represents a conversion factor */
struct U_I18N_API Factor {
    double factorNum = 1;
    double factorDen = 1;
    double offset = 0;
    bool reciprocal = false;

    // Exponents for the symbolic constants
    int32_t constantExponents[CONSTANTS_COUNT] = {};

    void multiplyBy(const Factor &rhs);
    void divideBy(const Factor &rhs);

    // Apply the power to the factor.
    void power(int32_t power);

    // Apply SI or binary prefix to the Factor.
    void applyPrefix(UMeasurePrefix unitPrefix);

    // Does an in-place substitution of the "symbolic constants" based on
    // constantExponents (resetting the exponents).
    //
    // In ICU4J, see UnitConverter.Factor.getConversionRate().
    void substituteConstants();
};

struct U_I18N_API ConversionInfo {
    double conversionRate;
    double offset;
    bool reciprocal;
};

/*
 * Adds a single factor element to the `Factor`. e.g "ft3m", "2.333" or "cup2m3". But not "cup2m3^3".
 */
void U_I18N_API addSingleFactorConstant(StringPiece baseStr, int32_t power, Signum sigNum,
                                        Factor &factor, UErrorCode &status);

/**
 * Represents the conversion rate between `source` and `target`.
 * TODO ICU-22683: COnsider moving the handling of special mappings (e.g. beaufort) to a separate
 * struct.
 */
struct U_I18N_API ConversionRate : public UMemory {
    const MeasureUnitImpl source;
    const MeasureUnitImpl target;
    CharString specialSource;
    CharString specialTarget;
    double factorNum = 1;
    double factorDen = 1;
    double sourceOffset = 0;
    double targetOffset = 0;
    bool reciprocal = false;

    ConversionRate(MeasureUnitImpl &&source, MeasureUnitImpl &&target)
        : source(std::move(source)), target(std::move(target)), specialSource(), specialTarget() {}
};

enum Convertibility {
    RECIPROCAL,
    CONVERTIBLE,
    UNCONVERTIBLE,
};

MeasureUnitImpl U_I18N_API extractCompoundBaseUnit(const MeasureUnitImpl &source,
                                                   const ConversionRates &conversionRates,
                                                   UErrorCode &status);

/**
 * Check if the convertibility between `source` and `target`.
 * For example:
 *    `meter` and `foot` are `CONVERTIBLE`.
 *    `meter-per-second` and `second-per-meter` are `RECIPROCAL`.
 *    `meter` and `pound` are `UNCONVERTIBLE`.
 *
 * NOTE:
 *    Only works with SINGLE and COMPOUND units. If one of the units is a
 *    MIXED unit, an error will occur. For more information, see UMeasureUnitComplexity.
 */
Convertibility U_I18N_API extractConvertibility(const MeasureUnitImpl &source,
                                                const MeasureUnitImpl &target,
                                                const ConversionRates &conversionRates,
                                                UErrorCode &status);

/**
 * Converts from a source `MeasureUnit` to a target `MeasureUnit`.
 *
 * NOTE:
 *    Only works with SINGLE and COMPOUND units. If one of the units is a
 *    MIXED unit, an error will occur. For more information, see UMeasureUnitComplexity.
 */
class U_I18N_API UnitsConverter : public UMemory {
  public:
    /**
     * Constructor of `UnitConverter`.
     * NOTE:
     *   - source and target must be under the same category
     *      - e.g. meter to mile --> both of them are length units.
     * NOTE:
     *    This constructor creates an instance of `ConversionRates` internally.
     *
     * @param sourceIdentifier represents the source unit identifier.
     * @param targetIdentifier represents the target unit identifier.
     * @param status
     */
    UnitsConverter(StringPiece sourceIdentifier, StringPiece targetIdentifier, UErrorCode &status);

    /**
     * Constructor of `UnitConverter`.
     * NOTE:
     *   - source and target must be under the same category
     *      - e.g. meter to mile --> both of them are length units.
     *
     * @param source represents the source unit.
     * @param target represents the target unit.
     * @param ratesInfo Contains all the needed conversion rates.
     * @param status
     */
    UnitsConverter(const MeasureUnitImpl &source, const MeasureUnitImpl &target,
                  const ConversionRates &ratesInfo, UErrorCode &status);

    /**
     * Compares two single units and returns 1 if the first one is greater, -1 if the second
     * one is greater and 0 if they are equal.
     *
     * NOTE:
     *  Compares only single units that are convertible.
     */
    static int32_t compareTwoUnits(const MeasureUnitImpl &firstUnit, const MeasureUnitImpl &SecondUnit,
                                   const ConversionRates &ratesInfo, UErrorCode &status);

    /**
     * Convert a measurement expressed in the source unit to a measurement
     * expressed in the target unit.
     *
     * @param inputValue the value to be converted.
     * @return the converted value.
     */
    double convert(double inputValue) const;

    /**
     * The inverse of convert(): convert a measurement expressed in the target
     * unit to a measurement expressed in the source unit.
     *
     * @param inputValue the value to be converted.
     * @return the converted value.
     */
    double convertInverse(double inputValue) const;

    ConversionInfo getConversionInfo() const;

  private:
    ConversionRate conversionRate_;

    /**
     * Initialises the object.
     */
    void init(const ConversionRates &ratesInfo, UErrorCode &status);

    /**
     * Convert from what should be discrete scale values for a particular unit like beaufort
     * to a corresponding value in the base unit (which can have any decimal value, like meters/sec).
     * This can handle different scales, specified by minBaseForScaleValues[].
     */
    double scaleToBase(double scaleValue, double minBaseForScaleValues[], int scaleMax) const;

    /**
     * Convert from a value in the base unit (which can have any decimal value, like meters/sec) to a corresponding
     * discrete value in a scale (like beaufort), where each scale value represents a range of base values.
     * This can handle different scales, specified by minBaseForScaleValues[].
     */
    double baseToScale(double baseValue, double minBaseForScaleValues[], int scaleMax) const;

};

} // namespace units
U_NAMESPACE_END

#endif //__UNITS_CONVERTER_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
