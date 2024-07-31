// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "measunit_impl.h"
#include "number_decimalquantity.h"
#include "number_roundingutils.h"
#include "resource.h"
#include "unicode/measure.h"
#include "units_data.h"
#include "units_router.h"
#include <cmath>

U_NAMESPACE_BEGIN
namespace units {

using number::Precision;
using number::impl::parseIncrementOption;

Precision UnitsRouter::parseSkeletonToPrecision(icu::UnicodeString precisionSkeleton,
                                                UErrorCode &status) {
    if (U_FAILURE(status)) {
        // As a member of UsagePrefsHandler, which is a friend of Precision, we
        // get access to the default constructor.
        return {};
    }
    constexpr int32_t kSkelPrefixLen = 20;
    if (!precisionSkeleton.startsWith(UNICODE_STRING_SIMPLE("precision-increment/"))) {
        status = U_INVALID_FORMAT_ERROR;
        return {};
    }
    U_ASSERT(precisionSkeleton[kSkelPrefixLen - 1] == u'/');
    StringSegment segment(precisionSkeleton, false);
    segment.adjustOffset(kSkelPrefixLen);
    Precision result;
    parseIncrementOption(segment, result, status);
    return result;
}

UnitsRouter::UnitsRouter(StringPiece inputUnitIdentifier, const Locale &locale, StringPiece usage,
                         UErrorCode &status) {
    this->init(MeasureUnit::forIdentifier(inputUnitIdentifier, status), locale, usage, status);
}

UnitsRouter::UnitsRouter(const MeasureUnit &inputUnit, const Locale &locale, StringPiece usage,
                         UErrorCode &status) {
    this->init(std::move(inputUnit), locale, usage, status);
}

void UnitsRouter::init(const MeasureUnit &inputUnit, const Locale &locale, StringPiece usage,
                       UErrorCode &status) {

    if (U_FAILURE(status)) {
        return;
    }

    // TODO: do we want to pass in ConversionRates and UnitPreferences instead
    // of loading in each UnitsRouter instance? (Or make global?)
    ConversionRates conversionRates(status);
    UnitPreferences prefs(status);

    MeasureUnitImpl inputUnitImpl = MeasureUnitImpl::forMeasureUnitMaybeCopy(inputUnit, status);
    MeasureUnitImpl baseUnitImpl =
        (extractCompoundBaseUnit(inputUnitImpl, conversionRates, status));
    CharString category = getUnitQuantity(baseUnitImpl, status);
    if (U_FAILURE(status)) {
        return;
    }

    const MaybeStackVector<UnitPreference> unitPrefs =
        prefs.getPreferencesFor(category.toStringPiece(), usage, locale, status);
    for (int32_t i = 0, n = unitPrefs.length(); i < n; ++i) {
        U_ASSERT(unitPrefs[i] != nullptr);
        const auto* const preference = unitPrefs[i];

        MeasureUnitImpl complexTargetUnitImpl =
            MeasureUnitImpl::forIdentifier(preference->unit.data(), status);
        if (U_FAILURE(status)) {
            return;
        }

        UnicodeString precision = preference->skeleton;

        // For now, we only have "precision-increment" in Units Preferences skeleton.
        // Therefore, we check if the skeleton starts with "precision-increment" and force the program to
        // fail otherwise.
        // NOTE:
        //  It is allowed to have an empty precision.
        if (!precision.isEmpty() && !precision.startsWith(u"precision-increment", 19)) {
            status = U_INTERNAL_PROGRAM_ERROR;
            return;
        }

        outputUnits_.emplaceBackAndCheckErrorCode(status,
                                                  complexTargetUnitImpl.copy(status).build(status));
        converterPreferences_.emplaceBackAndCheckErrorCode(status, inputUnitImpl, complexTargetUnitImpl,
                                                           preference->geq, std::move(precision),
                                                           conversionRates, status);

        if (U_FAILURE(status)) {
            return;
        }
    }
}

RouteResult UnitsRouter::route(double quantity, icu::number::impl::RoundingImpl *rounder, UErrorCode &status) const {
    // Find the matching preference
    const ConverterPreference *converterPreference = nullptr;
    for (int32_t i = 0, n = converterPreferences_.length(); i < n; i++) {
        converterPreference = converterPreferences_[i];
        if (converterPreference->converter.greaterThanOrEqual(std::abs(quantity) * (1 + DBL_EPSILON),
                                                              converterPreference->limit)) {
            break;
        }
    }
    U_ASSERT(converterPreference != nullptr);

    // Set up the rounder for this preference's precision
    if (rounder != nullptr && rounder->fPrecision.isBogus()) {
        if (converterPreference->precision.length() > 0) {
            rounder->fPrecision = parseSkeletonToPrecision(converterPreference->precision, status);
        } else {
            // We use the same rounding mode as COMPACT notation: known to be a
            // human-friendly rounding mode: integers, but add a decimal digit
            // as needed to ensure we have at least 2 significant digits.
            rounder->fPrecision = Precision::integer().withMinDigits(2);
        }
    }

    return RouteResult(converterPreference->converter.convert(quantity, rounder, status),
                       converterPreference->targetUnit.copy(status));
}

const MaybeStackVector<MeasureUnit> *UnitsRouter::getOutputUnits() const {
    // TODO: consider pulling this from converterPreferences_ and dropping
    // outputUnits_?
    return &outputUnits_;
}

} // namespace units
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
