// © 2020 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "number_usageprefs.h"
#include "cstring.h"
#include "number_decimalquantity.h"
#include "number_microprops.h"
#include "number_roundingutils.h"
#include "number_skeletons.h"
#include "unicode/char16ptr.h"
#include "unicode/currunit.h"
#include "unicode/fmtable.h"
#include "unicode/measure.h"
#include "unicode/numberformatter.h"
#include "unicode/platform.h"
#include "unicode/unum.h"
#include "unicode/urename.h"
#include "units_data.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;
using icu::StringSegment;
using icu::units::ConversionRates;

// Copy constructor
StringProp::StringProp(const StringProp &other) : StringProp() {
    this->operator=(other);
}

// Copy assignment operator
StringProp &StringProp::operator=(const StringProp &other) {
    if (this == &other) { return *this; }  // self-assignment: no-op
    fLength = 0;
    fError = other.fError;
    if (fValue != nullptr) {
        uprv_free(fValue);
        fValue = nullptr;
    }
    if (other.fValue == nullptr) {
        return *this;
    }
    if (U_FAILURE(other.fError)) {
        // We don't bother trying to allocating memory if we're in any case busy
        // copying an errored StringProp.
        return *this;
    }
    fValue = static_cast<char*>(uprv_malloc(other.fLength + 1));
    if (fValue == nullptr) {
        fError = U_MEMORY_ALLOCATION_ERROR;
        return *this;
    }
    fLength = other.fLength;
    uprv_strncpy(fValue, other.fValue, fLength + 1);
    return *this;
}

// Move constructor
StringProp::StringProp(StringProp &&src) noexcept : fValue(src.fValue),
                                                      fLength(src.fLength),
                                                      fError(src.fError) {
    // Take ownership away from src if necessary
    src.fValue = nullptr;
}

// Move assignment operator
StringProp &StringProp::operator=(StringProp &&src) noexcept {
    if (this == &src) {
        return *this;
    }
    if (fValue != nullptr) {
        uprv_free(fValue);
    }
    fValue = src.fValue;
    fLength = src.fLength;
    fError = src.fError;
    // Take ownership away from src if necessary
    src.fValue = nullptr;
    return *this;
}

StringProp::~StringProp() {
    if (fValue != nullptr) {
        uprv_free(fValue);
        fValue = nullptr;
    }
}

void StringProp::set(StringPiece value) {
    if (fValue != nullptr) {
        uprv_free(fValue);
        fValue = nullptr;
    }
    fLength = value.length();
    fValue = static_cast<char*>(uprv_malloc(fLength + 1));
    if (fValue == nullptr) {
        fLength = 0;
        fError = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    if (fLength > 0) {
        uprv_strncpy(fValue, value.data(), fLength);
    }
    fValue[fLength] = 0;
}

// Populates micros.mixedMeasures and modifies quantity, based on the values in
// measures.
void mixedMeasuresToMicros(const MaybeStackVector<Measure> &measures, DecimalQuantity *quantity,
                           MicroProps *micros, UErrorCode status) {
    micros->mixedMeasuresCount = measures.length();

    if (micros->mixedMeasures.getCapacity() < micros->mixedMeasuresCount) {
        if (micros->mixedMeasures.resize(micros->mixedMeasuresCount) == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
    }

    for (int32_t i = 0; i < micros->mixedMeasuresCount; i++) {
        switch (measures[i]->getNumber().getType()) {
        case Formattable::kInt64:
            micros->mixedMeasures[i] = measures[i]->getNumber().getInt64();
            break;

        case Formattable::kDouble:
            U_ASSERT(micros->indexOfQuantity < 0);
            quantity->setToDouble(measures[i]->getNumber().getDouble());
            micros->indexOfQuantity = i;
            break;

        default:
            U_ASSERT(0 == "Found a Measure Number which is neither a double nor an int");
            UPRV_UNREACHABLE_EXIT;
            break;
        }

        if (U_FAILURE(status)) {
            return;
        }
    }

    if (micros->indexOfQuantity < 0) {
        // There is no quantity.
        status = U_INTERNAL_PROGRAM_ERROR;
    }
}

UsagePrefsHandler::UsagePrefsHandler(const Locale &locale,
                                     const MeasureUnit &inputUnit,
                                     const StringPiece usage,
                                     const MicroPropsGenerator *parent,
                                     UErrorCode &status)
    : fUnitsRouter(inputUnit, locale, usage, status),
      fParent(parent) {
}

void UsagePrefsHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                        UErrorCode &status) const {
    fParent->processQuantity(quantity, micros, status);
    if (U_FAILURE(status)) {
        return;
    }

    quantity.roundToInfinity(); // Enables toDouble
    const units::RouteResult routed = fUnitsRouter.route(quantity.toDouble(), &micros.rounder, status);
    if (U_FAILURE(status)) {
        return;
    }
    const MaybeStackVector<Measure>& routedMeasures = routed.measures;
    micros.outputUnit = routed.outputUnit.copy(status).build(status);
    if (U_FAILURE(status)) {
        return;
    }

    mixedMeasuresToMicros(routedMeasures, &quantity, &micros, status);
}

UnitConversionHandler::UnitConversionHandler(const MeasureUnit &targetUnit,
                                             const MicroPropsGenerator *parent, UErrorCode &status)
    : fOutputUnit(targetUnit), fParent(parent) {
    MeasureUnitImpl tempInput, tempOutput;

    ConversionRates conversionRates(status);
    if (U_FAILURE(status)) {
        return;
    }

    const MeasureUnitImpl &targetUnitImpl =
        MeasureUnitImpl::forMeasureUnit(targetUnit, tempOutput, status);
    fUnitConverter.adoptInsteadAndCheckErrorCode(
        new ComplexUnitsConverter(targetUnitImpl, conversionRates, status), status);
}

void UnitConversionHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                            UErrorCode &status) const {
    fParent->processQuantity(quantity, micros, status);
    if (U_FAILURE(status)) {
        return;
    }
    quantity.roundToInfinity(); // Enables toDouble
    MaybeStackVector<Measure> measures =
        fUnitConverter->convert(quantity.toDouble(), &micros.rounder, status);
    micros.outputUnit = fOutputUnit;
    if (U_FAILURE(status)) {
        return;
    }

    mixedMeasuresToMicros(measures, &quantity, &micros, status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
