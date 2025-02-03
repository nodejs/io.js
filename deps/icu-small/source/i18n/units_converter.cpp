// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "charstr.h"
#include "cmemory.h"
#include "double-conversion-string-to-double.h"
#include "measunit_impl.h"
#include "putilimp.h"
#include "uassert.h"
#include "unicode/errorcode.h"
#include "unicode/localpointer.h"
#include "unicode/stringpiece.h"
#include "units_converter.h"
#include <algorithm>
#include <cmath>
#include <stdlib.h>
#include <utility>

U_NAMESPACE_BEGIN
namespace units {

void U_I18N_API Factor::multiplyBy(const Factor &rhs) {
    factorNum *= rhs.factorNum;
    factorDen *= rhs.factorDen;
    for (int i = 0; i < CONSTANTS_COUNT; i++) {
        constantExponents[i] += rhs.constantExponents[i];
    }

    // NOTE
    //  We need the offset when the source and the target are simple units. e.g. the source is
    //  celsius and the target is Fahrenheit. Therefore, we just keep the value using `std::max`.
    offset = std::max(rhs.offset, offset);
}

void U_I18N_API Factor::divideBy(const Factor &rhs) {
    factorNum *= rhs.factorDen;
    factorDen *= rhs.factorNum;
    for (int i = 0; i < CONSTANTS_COUNT; i++) {
        constantExponents[i] -= rhs.constantExponents[i];
    }

    // NOTE
    //  We need the offset when the source and the target are simple units. e.g. the source is
    //  celsius and the target is Fahrenheit. Therefore, we just keep the value using `std::max`.
    offset = std::max(rhs.offset, offset);
}

void U_I18N_API Factor::power(int32_t power) {
    // multiply all the constant by the power.
    for (int i = 0; i < CONSTANTS_COUNT; i++) {
        constantExponents[i] *= power;
    }

    bool shouldFlip = power < 0; // This means that after applying the absolute power, we should flip
                                 // the Numerator and Denominator.

    factorNum = std::pow(factorNum, std::abs(power));
    factorDen = std::pow(factorDen, std::abs(power));

    if (shouldFlip) {
        // Flip Numerator and Denominator.
        std::swap(factorNum, factorDen);
    }
}

void U_I18N_API Factor::applyPrefix(UMeasurePrefix unitPrefix) {
    if (unitPrefix == UMeasurePrefix::UMEASURE_PREFIX_ONE) {
        // No need to do anything
        return;
    }

    int32_t prefixPower = umeas_getPrefixPower(unitPrefix);
    double prefixFactor = std::pow(static_cast<double>(umeas_getPrefixBase(unitPrefix)),
                                   static_cast<double>(std::abs(prefixPower)));
    if (prefixPower >= 0) {
        factorNum *= prefixFactor;
    } else {
        factorDen *= prefixFactor;
    }
}

void U_I18N_API Factor::substituteConstants() {
    for (int i = 0; i < CONSTANTS_COUNT; i++) {
        if (this->constantExponents[i] == 0) {
            continue;
        }

        auto absPower = std::abs(this->constantExponents[i]);
        Signum powerSig = this->constantExponents[i] < 0 ? Signum::NEGATIVE : Signum::POSITIVE;
        double absConstantValue = std::pow(constantsValues[i], absPower);

        if (powerSig == Signum::NEGATIVE) {
            this->factorDen *= absConstantValue;
        } else {
            this->factorNum *= absConstantValue;
        }

        this->constantExponents[i] = 0;
    }
}

namespace {

/* Helpers */

using icu::double_conversion::StringToDoubleConverter;

// TODO: Make this a shared-utility function.
// Returns `double` from a scientific number(i.e. "1", "2.01" or "3.09E+4")
double strToDouble(StringPiece strNum, UErrorCode &status) {
    // We are processing well-formed input, so we don't need any special options to
    // StringToDoubleConverter.
    StringToDoubleConverter converter(0, 0, 0, "", "");
    int32_t count;
    double result = converter.StringToDouble(strNum.data(), strNum.length(), &count);
    if (count != strNum.length()) {
        status = U_INVALID_FORMAT_ERROR;
    }

    return result;
}

// Returns `double` from a scientific number that could has a division sign (i.e. "1", "2.01", "3.09E+4"
// or "2E+2/3")
double strHasDivideSignToDouble(StringPiece strWithDivide, UErrorCode &status) {
    int divisionSignInd = -1;
    for (int i = 0, n = strWithDivide.length(); i < n; ++i) {
        if (strWithDivide.data()[i] == '/') {
            divisionSignInd = i;
            break;
        }
    }

    if (divisionSignInd >= 0) {
        return strToDouble(strWithDivide.substr(0, divisionSignInd), status) /
               strToDouble(strWithDivide.substr(divisionSignInd + 1), status);
    }

    return strToDouble(strWithDivide, status);
}

/*
  Adds single factor to a `Factor` object. Single factor means "23^2", "23.3333", "ft2m^3" ...etc.
  However, complex factor are not included, such as "ft2m^3*200/3"
*/
void addFactorElement(Factor &factor, StringPiece elementStr, Signum signum, UErrorCode &status) {
    StringPiece baseStr;
    StringPiece powerStr;
    int32_t power =
        1; // In case the power is not written, then, the power is equal 1 ==> `ft2m^1` == `ft2m`

    // Search for the power part
    int32_t powerInd = -1;
    for (int32_t i = 0, n = elementStr.length(); i < n; ++i) {
        if (elementStr.data()[i] == '^') {
            powerInd = i;
            break;
        }
    }

    if (powerInd > -1) {
        // There is power
        baseStr = elementStr.substr(0, powerInd);
        powerStr = elementStr.substr(powerInd + 1);

        power = static_cast<int32_t>(strToDouble(powerStr, status));
    } else {
        baseStr = elementStr;
    }

    addSingleFactorConstant(baseStr, power, signum, factor, status);
}

/*
 * Extracts `Factor` from a complete string factor. e.g. "ft2m^3*1007/cup2m3*3"
 */
Factor extractFactorConversions(StringPiece stringFactor, UErrorCode &status) {
    Factor result;
    Signum signum = Signum::POSITIVE;
    const auto* factorData = stringFactor.data();
    for (int32_t i = 0, start = 0, n = stringFactor.length(); i < n; i++) {
        if (factorData[i] == '*' || factorData[i] == '/') {
            StringPiece factorElement = stringFactor.substr(start, i - start);
            addFactorElement(result, factorElement, signum, status);

            start = i + 1; // Set `start` to point to the start of the new element.
        } else if (i == n - 1) {
            // Last element
            addFactorElement(result, stringFactor.substr(start, i + 1), signum, status);
        }

        if (factorData[i] == '/') {
            signum = Signum::NEGATIVE; // Change the signum because we reached the Denominator.
        }
    }

    return result;
}

// Load factor for a single source
Factor loadSingleFactor(StringPiece source, const ConversionRates &ratesInfo, UErrorCode &status) {
    const auto* const conversionUnit = ratesInfo.extractConversionInfo(source, status);
    if (U_FAILURE(status)) return {};
    if (conversionUnit == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return {};
    }

    Factor result = extractFactorConversions(conversionUnit->factor.toStringPiece(), status);
    result.offset = strHasDivideSignToDouble(conversionUnit->offset.toStringPiece(), status);

    return result;
}

// Load Factor of a compound source unit.
// In ICU4J, this is a pair of ConversionRates.getFactorToBase() functions.
Factor loadCompoundFactor(const MeasureUnitImpl &source, const ConversionRates &ratesInfo,
                          UErrorCode &status) {

    Factor result;
    for (int32_t i = 0, n = source.singleUnits.length(); i < n; i++) {
        SingleUnitImpl singleUnit = *source.singleUnits[i];

        Factor singleFactor = loadSingleFactor(singleUnit.getSimpleUnitID(), ratesInfo, status);
        if (U_FAILURE(status)) return result;

        // Prefix before power, because:
        // - square-kilometer to square-meter: (1000)^2
        // - square-kilometer to square-foot (approximate): (3.28*1000)^2
        singleFactor.applyPrefix(singleUnit.unitPrefix);

        // Apply the power of the `dimensionality`
        singleFactor.power(singleUnit.dimensionality);

        result.multiplyBy(singleFactor);
    }

    return result;
}

/**
 * Checks if the source unit and the target unit are simple. For example celsius or fahrenheit. But not
 * square-celsius or square-fahrenheit.
 *
 * NOTE:
 *  Empty unit means simple unit.
 *
 * In ICU4J, this is ConversionRates.checkSimpleUnit().
 */
UBool checkSimpleUnit(const MeasureUnitImpl &unit, UErrorCode &status) {
    if (U_FAILURE(status)) return false;

    if (unit.complexity != UMEASURE_UNIT_SINGLE) {
        return false;
    }
    if (unit.singleUnits.length() == 0) {
        // Empty units means simple unit.
        return true;
    }

    auto singleUnit = *(unit.singleUnits[0]);

    if (singleUnit.dimensionality != 1 || singleUnit.unitPrefix != UMEASURE_PREFIX_ONE) {
        return false;
    }

    return true;
}

// Map the MeasureUnitImpl for a simpleUnit to a SingleUnitImpl, then use that
// SingleUnitImpl's simpleUnitID to get the corresponding ConversionRateInfo;
// from that we get the specialMappingName (which may be empty if the simple unit
// converts to base using factor + offset instelad of a special mapping).
CharString getSpecialMappingName(const MeasureUnitImpl &simpleUnit, const ConversionRates &ratesInfo,
                          UErrorCode &status) {
    if (!checkSimpleUnit(simpleUnit, status)) {
        return {};
    }
    SingleUnitImpl singleUnit = *simpleUnit.singleUnits[0];
    const auto* const conversionUnit =
        ratesInfo.extractConversionInfo(singleUnit.getSimpleUnitID(), status);
    if (U_FAILURE(status)) {
        return {};
    }
    if (conversionUnit == nullptr) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return {};
    }
    CharString result;
    result.copyFrom(conversionUnit->specialMappingName, status);
    return result;
}

/**
 *  Extract conversion rate from `source` to `target`
 */
// In ICU4J, this function is partially inlined in the UnitsConverter constructor.
// TODO ICU-22683: Consider splitting handling of special mappings into separate class
void loadConversionRate(ConversionRate &conversionRate, const MeasureUnitImpl &source,
                        const MeasureUnitImpl &target, Convertibility unitsState,
                        const ConversionRates &ratesInfo, UErrorCode &status) {

    conversionRate.specialSource = getSpecialMappingName(source, ratesInfo, status);
    conversionRate.specialTarget = getSpecialMappingName(target, ratesInfo, status);
    
    if (conversionRate.specialSource.isEmpty() && conversionRate.specialTarget.isEmpty()) {
        // Represents the conversion factor from the source to the target.
        Factor finalFactor;

        // Represents the conversion factor from the source to the base unit that specified in the conversion
        // data which is considered as the root of the source and the target.
        Factor sourceToBase = loadCompoundFactor(source, ratesInfo, status);
        Factor targetToBase = loadCompoundFactor(target, ratesInfo, status);

        // Merger Factors
        finalFactor.multiplyBy(sourceToBase);
        if (unitsState == Convertibility::CONVERTIBLE) {
            finalFactor.divideBy(targetToBase);
        } else if (unitsState == Convertibility::RECIPROCAL) {
            finalFactor.multiplyBy(targetToBase);
        } else {
            status = UErrorCode::U_ARGUMENT_TYPE_MISMATCH;
            return;
        }

        finalFactor.substituteConstants();

        conversionRate.factorNum = finalFactor.factorNum;
        conversionRate.factorDen = finalFactor.factorDen;

        // This code corresponds to ICU4J's ConversionRates.getOffset().
        // In case of simple units (such as: celsius or fahrenheit), offsets are considered.
        if (checkSimpleUnit(source, status) && checkSimpleUnit(target, status)) {
            conversionRate.sourceOffset =
                sourceToBase.offset * sourceToBase.factorDen / sourceToBase.factorNum;
            conversionRate.targetOffset =
                targetToBase.offset * targetToBase.factorDen / targetToBase.factorNum;
        }
        // TODO(icu-units#127): should we consider failure if there's an offset for
        // a not-simple-unit? What about kilokelvin / kilocelsius?

        conversionRate.reciprocal = unitsState == Convertibility::RECIPROCAL;
    } else if (conversionRate.specialSource.isEmpty() || conversionRate.specialTarget.isEmpty()) {
        // Still need to set factorNum/factorDen for either source to base or base to target
        if (unitsState != Convertibility::CONVERTIBLE) {
            status = UErrorCode::U_ARGUMENT_TYPE_MISMATCH;
            return;
        }
        Factor finalFactor;
        if (conversionRate.specialSource.isEmpty()) {
            // factorNum/factorDen is for source to base only
            finalFactor = loadCompoundFactor(source, ratesInfo, status);
        } else {
            // factorNum/factorDen is for base to target only
            finalFactor = loadCompoundFactor(target, ratesInfo, status);
        }
        finalFactor.substituteConstants();
        conversionRate.factorNum = finalFactor.factorNum;
        conversionRate.factorDen = finalFactor.factorDen;
    }
}

struct UnitIndexAndDimension : UMemory {
    int32_t index = 0;
    int32_t dimensionality = 0;

    UnitIndexAndDimension(const SingleUnitImpl &singleUnit, int32_t multiplier) {
        index = singleUnit.index;
        dimensionality = singleUnit.dimensionality * multiplier;
    }
};

void mergeSingleUnitWithDimension(MaybeStackVector<UnitIndexAndDimension> &unitIndicesWithDimension,
                                  const SingleUnitImpl &shouldBeMerged, int32_t multiplier) {
    for (int32_t i = 0; i < unitIndicesWithDimension.length(); i++) {
        auto &unitWithIndex = *unitIndicesWithDimension[i];
        if (unitWithIndex.index == shouldBeMerged.index) {
            unitWithIndex.dimensionality += shouldBeMerged.dimensionality * multiplier;
            return;
        }
    }

    unitIndicesWithDimension.emplaceBack(shouldBeMerged, multiplier);
}

void mergeUnitsAndDimensions(MaybeStackVector<UnitIndexAndDimension> &unitIndicesWithDimension,
                             const MeasureUnitImpl &shouldBeMerged, int32_t multiplier) {
    for (int32_t unit_i = 0; unit_i < shouldBeMerged.singleUnits.length(); unit_i++) {
        auto singleUnit = *shouldBeMerged.singleUnits[unit_i];
        mergeSingleUnitWithDimension(unitIndicesWithDimension, singleUnit, multiplier);
    }
}

UBool checkAllDimensionsAreZeros(const MaybeStackVector<UnitIndexAndDimension> &dimensionVector) {
    for (int32_t i = 0; i < dimensionVector.length(); i++) {
        if (dimensionVector[i]->dimensionality != 0) {
            return false;
        }
    }

    return true;
}

} // namespace

// Conceptually, this modifies factor: factor *= baseStr^(signum*power).
//
// baseStr must be a known constant or a value that strToDouble() is able to
// parse.
void U_I18N_API addSingleFactorConstant(StringPiece baseStr, int32_t power, Signum signum,
                                        Factor &factor, UErrorCode &status) {
    if (baseStr == "ft_to_m") {
        factor.constantExponents[CONSTANT_FT2M] += power * signum;
    } else if (baseStr == "ft2_to_m2") {
        factor.constantExponents[CONSTANT_FT2M] += 2 * power * signum;
    } else if (baseStr == "ft3_to_m3") {
        factor.constantExponents[CONSTANT_FT2M] += 3 * power * signum;
    } else if (baseStr == "in3_to_m3") {
        factor.constantExponents[CONSTANT_FT2M] += 3 * power * signum;
        factor.factorDen *= std::pow(12 * 12 * 12, power * signum);
    } else if (baseStr == "gal_to_m3") {
        factor.constantExponents[CONSTANT_FT2M] += 3 * power * signum;
        factor.factorNum *= std::pow(231, power * signum);
        factor.factorDen *= std::pow(12 * 12 * 12, power * signum);
    } else if (baseStr == "gal_imp_to_m3") {
        factor.constantExponents[CONSTANT_GAL_IMP2M3] += power * signum;
    } else if (baseStr == "G") {
        factor.constantExponents[CONSTANT_G] += power * signum;
    } else if (baseStr == "gravity") {
        factor.constantExponents[CONSTANT_GRAVITY] += power * signum;
    } else if (baseStr == "lb_to_kg") {
        factor.constantExponents[CONSTANT_LB2KG] += power * signum;
    } else if (baseStr == "glucose_molar_mass") {
        factor.constantExponents[CONSTANT_GLUCOSE_MOLAR_MASS] += power * signum;
    } else if (baseStr == "item_per_mole") {
        factor.constantExponents[CONSTANT_ITEM_PER_MOLE] += power * signum;
    } else if (baseStr == "meters_per_AU") {
        factor.constantExponents[CONSTANT_METERS_PER_AU] += power * signum;
    } else if (baseStr == "PI") {
        factor.constantExponents[CONSTANT_PI] += power * signum;
    } else if (baseStr == "sec_per_julian_year") {
        factor.constantExponents[CONSTANT_SEC_PER_JULIAN_YEAR] += power * signum;
    } else if (baseStr == "speed_of_light_meters_per_second") {
        factor.constantExponents[CONSTANT_SPEED_OF_LIGHT_METERS_PER_SECOND] += power * signum;
    } else if (baseStr == "sho_to_m3") {
        factor.constantExponents[CONSTANT_SHO_TO_M3] += power * signum;
    } else if (baseStr == "tsubo_to_m2") {
        factor.constantExponents[CONSTANT_TSUBO_TO_M2] += power * signum;
    } else if (baseStr == "shaku_to_m") {
        factor.constantExponents[CONSTANT_SHAKU_TO_M] += power * signum;
    } else if (baseStr == "AMU") {
        factor.constantExponents[CONSTANT_AMU] += power * signum;
    } else {
        if (signum == Signum::NEGATIVE) {
            factor.factorDen *= std::pow(strToDouble(baseStr, status), power);
        } else {
            factor.factorNum *= std::pow(strToDouble(baseStr, status), power);
        }
    }
}

/**
 * Extracts the compound base unit of a compound unit (`source`). For example, if the source unit is
 * `square-mile-per-hour`, the compound base unit will be `square-meter-per-second`
 */
MeasureUnitImpl U_I18N_API extractCompoundBaseUnit(const MeasureUnitImpl &source,
                                                   const ConversionRates &conversionRates,
                                                   UErrorCode &status) {

    MeasureUnitImpl result;
    if (U_FAILURE(status)) return result;

    const auto &singleUnits = source.singleUnits;
    for (int i = 0, count = singleUnits.length(); i < count; ++i) {
        const auto &singleUnit = *singleUnits[i];
        // Extract `ConversionRateInfo` using the absolute unit. For example: in case of `square-meter`,
        // we will use `meter`
        const auto* const rateInfo =
            conversionRates.extractConversionInfo(singleUnit.getSimpleUnitID(), status);
        if (U_FAILURE(status)) {
            return result;
        }
        if (rateInfo == nullptr) {
            status = U_INTERNAL_PROGRAM_ERROR;
            return result;
        }

        // Multiply the power of the singleUnit by the power of the baseUnit. For example, square-hectare
        // must be pow4-meter. (NOTE: hectare --> square-meter)
        auto baseUnits =
            MeasureUnitImpl::forIdentifier(rateInfo->baseUnit.toStringPiece(), status).singleUnits;
        for (int32_t i = 0, baseUnitsCount = baseUnits.length(); i < baseUnitsCount; i++) {
            baseUnits[i]->dimensionality *= singleUnit.dimensionality;
            // TODO: Deal with SI-prefix
            result.appendSingleUnit(*baseUnits[i], status);

            if (U_FAILURE(status)) {
                return result;
            }
        }
    }

    return result;
}

/**
 * Determine the convertibility between `source` and `target`.
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
                                                UErrorCode &status) {

    if (source.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED ||
        target.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED) {
        status = U_ARGUMENT_TYPE_MISMATCH;
        return UNCONVERTIBLE;
    }

    MeasureUnitImpl sourceBaseUnit = extractCompoundBaseUnit(source, conversionRates, status);
    MeasureUnitImpl targetBaseUnit = extractCompoundBaseUnit(target, conversionRates, status);
    if (U_FAILURE(status)) return UNCONVERTIBLE;

    MaybeStackVector<UnitIndexAndDimension> convertible;
    MaybeStackVector<UnitIndexAndDimension> reciprocal;

    mergeUnitsAndDimensions(convertible, sourceBaseUnit, 1);
    mergeUnitsAndDimensions(reciprocal, sourceBaseUnit, 1);

    mergeUnitsAndDimensions(convertible, targetBaseUnit, -1);
    mergeUnitsAndDimensions(reciprocal, targetBaseUnit, 1);

    if (checkAllDimensionsAreZeros(convertible)) {
        return CONVERTIBLE;
    }

    if (checkAllDimensionsAreZeros(reciprocal)) {
        return RECIPROCAL;
    }

    return UNCONVERTIBLE;
}

UnitsConverter::UnitsConverter(const MeasureUnitImpl &source, const MeasureUnitImpl &target,
                               const ConversionRates &ratesInfo, UErrorCode &status)
    : conversionRate_(source.copy(status), target.copy(status)) {
    this->init(ratesInfo, status);
}

UnitsConverter::UnitsConverter(StringPiece sourceIdentifier, StringPiece targetIdentifier,
                               UErrorCode &status)
    : conversionRate_(MeasureUnitImpl::forIdentifier(sourceIdentifier, status),
                      MeasureUnitImpl::forIdentifier(targetIdentifier, status)) {
    if (U_FAILURE(status)) {
        return;
    }

    ConversionRates ratesInfo(status);
    this->init(ratesInfo, status);
}

void UnitsConverter::init(const ConversionRates &ratesInfo, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }

    if (this->conversionRate_.source.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED ||
        this->conversionRate_.target.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED) {
        status = U_ARGUMENT_TYPE_MISMATCH;
        return;
    }

    Convertibility unitsState = extractConvertibility(this->conversionRate_.source,
                                                      this->conversionRate_.target, ratesInfo, status);
    if (U_FAILURE(status)) return;
    if (unitsState == Convertibility::UNCONVERTIBLE) {
        status = U_ARGUMENT_TYPE_MISMATCH;
        return;
    }

    loadConversionRate(conversionRate_, conversionRate_.source, conversionRate_.target, unitsState,
                       ratesInfo, status);

}

int32_t UnitsConverter::compareTwoUnits(const MeasureUnitImpl &firstUnit,
                                        const MeasureUnitImpl &secondUnit,
                                        const ConversionRates &ratesInfo, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return 0;
    }

    if (firstUnit.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED ||
        secondUnit.complexity == UMeasureUnitComplexity::UMEASURE_UNIT_MIXED) {
        status = U_ARGUMENT_TYPE_MISMATCH;
        return 0;
    }

    Convertibility unitsState = extractConvertibility(firstUnit, secondUnit, ratesInfo, status);
    if (U_FAILURE(status)) {
        return 0;
    }

    if (unitsState == Convertibility::UNCONVERTIBLE || unitsState == Convertibility::RECIPROCAL) {
        status = U_ARGUMENT_TYPE_MISMATCH;
        return 0;
    }

    CharString firstSpecial = getSpecialMappingName(firstUnit, ratesInfo, status);
    CharString secondSpecial = getSpecialMappingName(secondUnit, ratesInfo, status);
    if (!firstSpecial.isEmpty() || !secondSpecial.isEmpty()) {
        if (firstSpecial.isEmpty()) {
            // non-specials come first
            return -1;
        }
        if (secondSpecial.isEmpty()) {
            // non-specials come first
            return 1;
        }
        // both are specials, compare lexicographically
        StringPiece firstSpecialPiece = firstSpecial.toStringPiece();
        StringPiece secondSpecialPiece = secondSpecial.toStringPiece();
        return firstSpecialPiece.compare(secondSpecialPiece);
    }

    // Represents the conversion factor from the firstUnit to the base
    // unit that specified in the conversion data which is considered as
    // the root of the firstUnit and the secondUnit.
    Factor firstUnitToBase = loadCompoundFactor(firstUnit, ratesInfo, status);
    Factor secondUnitToBase = loadCompoundFactor(secondUnit, ratesInfo, status);

    firstUnitToBase.substituteConstants();
    secondUnitToBase.substituteConstants();

    double firstUnitToBaseConversionRate = firstUnitToBase.factorNum / firstUnitToBase.factorDen;
    double secondUnitToBaseConversionRate = secondUnitToBase.factorNum / secondUnitToBase.factorDen;

    double diff = firstUnitToBaseConversionRate - secondUnitToBaseConversionRate;
    if (diff > 0) {
        return 1;
    }

    if (diff < 0) {
        return -1;
    }

    return 0;
}

// TODO per CLDR-17421 and ICU-22683: consider getting the data below from CLDR
static double minMetersPerSecForBeaufort[] = {
    // Minimum m/s (base) values for each Bft value, plus an extra artificial value;
    // when converting from Bft to m/s, the middle of the range will be used
    // (Values from table in Wikipedia, except for artificial value).
    // Since this is 0 based, max Beaufort value is thus array dimension minus 2.
    0.0, // 0 Bft
    0.3, // 1
    1.6, // 2
    3.4, // 3
    5.5, // 4
    8.0, // 5
    10.8, // 6
    13.9, // 7
    17.2, // 8
    20.8, // 9
    24.5, // 10
    28.5, // 11
    32.7, // 12
    36.9, // 13
    41.4, // 14
    46.1, // 15
    51.1, // 16
    55.8, // 17
    61.4, // artificial end of range 17 to give reasonable midpoint
};

static int maxBeaufort = UPRV_LENGTHOF(minMetersPerSecForBeaufort) - 2;

// Convert from what should be discrete scale values for a particular unit like beaufort
// to a corresponding value in the base unit (which can have any decimal value, like meters/sec).
// First we round the scale value to the nearest integer (in case it is specified with a fractional value),
// then we map that to a value in middle of the range of corresponding base values.
// This can handle different scales, specified by minBaseForScaleValues[].
double UnitsConverter::scaleToBase(double scaleValue, double minBaseForScaleValues[], int scaleMax) const {
    if (scaleValue < 0) {
        scaleValue = -scaleValue;
    }
    scaleValue += 0.5; // adjust up for later truncation
    if (scaleValue > static_cast<double>(scaleMax)) {
        scaleValue = static_cast<double>(scaleMax);
    }
    int scaleInt = static_cast<int>(scaleValue);
    return (minBaseForScaleValues[scaleInt] + minBaseForScaleValues[scaleInt+1])/2.0;
}

// Binary search to find the range that includes key;
// if key (non-negative) is in the range rangeStarts[i] to just under rangeStarts[i+1],
// then we return i; if key is >= rangeStarts[max] then we return max.
// Note that max is the maximum scale value, not the number of elements in the array
// (which should be larger than max).
// The ranges for index 0 start at 0.0.
static int bsearchRanges(double rangeStarts[], int max, double key) {
    if (key >= rangeStarts[max]) {
        return max;
    }
    int beg = 0, mid = 0, end = max + 1;
    while (beg < end) {
        mid = (beg + end) / 2;
        if (key < rangeStarts[mid]) {
            end = mid;
        } else if (key > rangeStarts[mid+1]) {
            beg = mid+1;
        } else {
            break;
        }
    }
    return mid;
}

// Convert from a value in the base unit (which can have any decimal value, like meters/sec) to a corresponding
// discrete value in a scale (like beaufort), where each scale value represents a range of base values.
// We binary-search the ranges to find the one that contains the specified base value, and return its index.
// This can handle different scales, specified by minBaseForScaleValues[].
double UnitsConverter::baseToScale(double baseValue, double minBaseForScaleValues[], int scaleMax) const {
    if (baseValue < 0) {
        baseValue = -baseValue;
    }
    int scaleIndex = bsearchRanges(minBaseForScaleValues, scaleMax, baseValue);
    return static_cast<double>(scaleIndex);
}

double UnitsConverter::convert(double inputValue) const {
    double result = inputValue;
    if (!conversionRate_.specialSource.isEmpty() || !conversionRate_.specialTarget.isEmpty()) {
        double base = inputValue;
        // convert input (=source) to base
        if (!conversionRate_.specialSource.isEmpty()) {
            // We  have a special mapping from source to base (not using factor, offset).
            // Currently the only supported mapping is a scale-based mapping for beaufort.
            base = (conversionRate_.specialSource == StringPiece("beaufort"))?
                scaleToBase(inputValue, minMetersPerSecForBeaufort, maxBeaufort): inputValue;
        } else {
            // Standard mapping (using factor) from source to base.
            base = inputValue * conversionRate_.factorNum / conversionRate_.factorDen;
        }
        // convert base to result (=target)
        if (!conversionRate_.specialTarget.isEmpty()) {
            // We  have a special mapping from base to target (not using factor, offset).
            // Currently the only supported mapping is a scale-based mapping for beaufort.
            result = (conversionRate_.specialTarget == StringPiece("beaufort"))?
                baseToScale(base, minMetersPerSecForBeaufort, maxBeaufort): base;
        } else {
            // Standard mapping (using factor) from base to target.
            result = base * conversionRate_.factorDen / conversionRate_.factorNum;
        }
        return result;
    }
    result =
        inputValue + conversionRate_.sourceOffset; // Reset the input to the target zero index.
    // Convert the quantity to from the source scale to the target scale.
    result *= conversionRate_.factorNum / conversionRate_.factorDen;

    result -= conversionRate_.targetOffset; // Set the result to its index.

    if (conversionRate_.reciprocal) {
        if (result == 0) {
            return uprv_getInfinity();
        }
        result = 1.0 / result;
    }

    return result;
}

double UnitsConverter::convertInverse(double inputValue) const {
    double result = inputValue;
    if (!conversionRate_.specialSource.isEmpty() || !conversionRate_.specialTarget.isEmpty()) {
        double base = inputValue;
        // convert input (=target) to base
        if (!conversionRate_.specialTarget.isEmpty()) {
            // We  have a special mapping from target to base (not using factor).
            // Currently the only supported mapping is a scale-based mapping for beaufort.
            base = (conversionRate_.specialTarget == StringPiece("beaufort"))?
                scaleToBase(inputValue, minMetersPerSecForBeaufort, maxBeaufort): inputValue;
        } else {
            // Standard mapping (using factor) from target to base.
            base = inputValue * conversionRate_.factorNum / conversionRate_.factorDen;
        }
        // convert base to result (=source)
        if (!conversionRate_.specialSource.isEmpty()) {
            // We  have a special mapping from base to source (not using factor).
            // Currently the only supported mapping is a scale-based mapping for beaufort.
            result = (conversionRate_.specialSource == StringPiece("beaufort"))?
                baseToScale(base, minMetersPerSecForBeaufort, maxBeaufort): base;
        } else {
            // Standard mapping (using factor) from base to source.
            result = base * conversionRate_.factorDen / conversionRate_.factorNum;
        }
        return result;
    }
    if (conversionRate_.reciprocal) {
        if (result == 0) {
            return uprv_getInfinity();
        }
        result = 1.0 / result;
    }
    result += conversionRate_.targetOffset;
    result *= conversionRate_.factorDen / conversionRate_.factorNum;
    result -= conversionRate_.sourceOffset;
    return result;
}

ConversionInfo UnitsConverter::getConversionInfo() const {
    ConversionInfo result;
    result.conversionRate = conversionRate_.factorNum / conversionRate_.factorDen;
    result.offset =
        (conversionRate_.sourceOffset * (conversionRate_.factorNum / conversionRate_.factorDen)) -
        conversionRate_.targetOffset;
    result.reciprocal = conversionRate_.reciprocal;

    return result;
}

} // namespace units
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
