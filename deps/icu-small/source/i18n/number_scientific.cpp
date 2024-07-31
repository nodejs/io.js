// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include <cstdlib>
#include "number_scientific.h"
#include "number_utils.h"
#include "formatted_string_builder.h"
#include "unicode/unum.h"
#include "number_microprops.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;

// NOTE: The object lifecycle of ScientificModifier and ScientificHandler differ greatly in Java and C++.
//
// During formatting, we need to provide an object with state (the exponent) as the inner modifier.
//
// In Java, where the priority is put on reducing object creations, the unsafe code path re-uses the
// ScientificHandler as a ScientificModifier, and the safe code path pre-computes 25 ScientificModifier
// instances.  This scheme reduces the number of object creations by 1 in both safe and unsafe.
//
// In C++, MicroProps provides a pre-allocated ScientificModifier, and ScientificHandler simply populates
// the state (the exponent) into that ScientificModifier. There is no difference between safe and unsafe.

ScientificModifier::ScientificModifier() : fExponent(0), fHandler(nullptr) {}

void ScientificModifier::set(int32_t exponent, const ScientificHandler *handler) {
    // ScientificModifier should be set only once.
    U_ASSERT(fHandler == nullptr);
    fExponent = exponent;
    fHandler = handler;
}

int32_t ScientificModifier::apply(FormattedStringBuilder &output, int32_t /*leftIndex*/, int32_t rightIndex,
                                  UErrorCode &status) const {
    // FIXME: Localized exponent separator location.
    int i = rightIndex;
    // Append the exponent separator and sign
    i += output.insert(
            i,
            fHandler->fSymbols->getSymbol(DecimalFormatSymbols::ENumberFormatSymbol::kExponentialSymbol),
            {UFIELD_CATEGORY_NUMBER, UNUM_EXPONENT_SYMBOL_FIELD},
            status);
    if (fExponent < 0 && fHandler->fSettings.fExponentSignDisplay != UNUM_SIGN_NEVER) {
        i += output.insert(
                i,
                fHandler->fSymbols
                        ->getSymbol(DecimalFormatSymbols::ENumberFormatSymbol::kMinusSignSymbol),
                {UFIELD_CATEGORY_NUMBER, UNUM_EXPONENT_SIGN_FIELD},
                status);
    } else if (fExponent >= 0 && fHandler->fSettings.fExponentSignDisplay == UNUM_SIGN_ALWAYS) {
        i += output.insert(
                i,
                fHandler->fSymbols
                        ->getSymbol(DecimalFormatSymbols::ENumberFormatSymbol::kPlusSignSymbol),
                {UFIELD_CATEGORY_NUMBER, UNUM_EXPONENT_SIGN_FIELD},
                status);
    }
    // Append the exponent digits (using a simple inline algorithm)
    int32_t disp = std::abs(fExponent);
    for (int j = 0; j < fHandler->fSettings.fMinExponentDigits || disp > 0; j++, disp /= 10) {
        auto d = static_cast<int8_t>(disp % 10);
        i += utils::insertDigitFromSymbols(
                output,
                i - j,
                d,
                *fHandler->fSymbols,
                {UFIELD_CATEGORY_NUMBER, UNUM_EXPONENT_FIELD},
                status);
    }
    return i - rightIndex;
}

int32_t ScientificModifier::getPrefixLength() const {
    // TODO: Localized exponent separator location.
    return 0;
}

int32_t ScientificModifier::getCodePointCount() const {
    // NOTE: This method is only called one place, NumberRangeFormatterImpl.
    // The call site only cares about != 0 and != 1.
    // Return a very large value so that if this method is used elsewhere, we should notice.
    return 999;
}

bool ScientificModifier::isStrong() const {
    // Scientific is always strong
    return true;
}

bool ScientificModifier::containsField(Field field) const {
    (void)field;
    // This method is not used for inner modifiers.
    UPRV_UNREACHABLE_EXIT;
}

void ScientificModifier::getParameters(Parameters& output) const {
    // Not part of any plural sets
    output.obj = nullptr;
}

bool ScientificModifier::strictEquals(const Modifier& other) const {
    const auto* _other = dynamic_cast<const ScientificModifier*>(&other);
    if (_other == nullptr) {
        return false;
    }
    // TODO: Check for locale symbols and settings as well? Could be less efficient.
    return fExponent == _other->fExponent;
}

// Note: Visual Studio does not compile this function without full name space. Why?
icu::number::impl::ScientificHandler::ScientificHandler(const Notation *notation, const DecimalFormatSymbols *symbols,
	const MicroPropsGenerator *parent) : 
	fSettings(notation->fUnion.scientific), fSymbols(symbols), fParent(parent) {}

void ScientificHandler::processQuantity(DecimalQuantity &quantity, MicroProps &micros,
                                        UErrorCode &status) const {
    fParent->processQuantity(quantity, micros, status);
    if (U_FAILURE(status)) { return; }

    // Do not apply scientific notation to special doubles
    if (quantity.isInfinite() || quantity.isNaN()) {
        micros.modInner = &micros.helpers.emptyStrongModifier;
        return;
    }

    // Treat zero as if it had magnitude 0
    int32_t exponent;
    if (quantity.isZeroish()) {
        if (fSettings.fRequireMinInt && micros.rounder.isSignificantDigits()) {
            // Show "00.000E0" on pattern "00.000E0"
            micros.rounder.apply(quantity, fSettings.fEngineeringInterval, status);
            exponent = 0;
        } else {
            micros.rounder.apply(quantity, status);
            exponent = 0;
        }
    } else {
        exponent = -micros.rounder.chooseMultiplierAndApply(quantity, *this, status);
    }

    // Use MicroProps's helper ScientificModifier and save it as the modInner.
    ScientificModifier &mod = micros.helpers.scientificModifier;
    mod.set(exponent, this);
    micros.modInner = &mod;

    // Change the exponent only after we select appropriate plural form
    // for formatting purposes so that we preserve expected formatted
    // string behavior.
    quantity.adjustExponent(exponent);

    // We already performed rounding. Do not perform it again.
    micros.rounder = RoundingImpl::passThrough();
}

int32_t ScientificHandler::getMultiplier(int32_t magnitude) const {
    int32_t interval = fSettings.fEngineeringInterval;
    int32_t digitsShown;
    if (fSettings.fRequireMinInt) {
        // For patterns like "000.00E0" and ".00E0"
        digitsShown = interval;
    } else if (interval <= 1) {
        // For patterns like "0.00E0" and "@@@E0"
        digitsShown = 1;
    } else {
        // For patterns like "##0.00"
        digitsShown = ((magnitude % interval + interval) % interval) + 1;
    }
    return digitsShown - magnitude - 1;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
