// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "number_decnum.h"
#include "number_types.h"
#include "number_multiplier.h"
#include "numparse_validators.h"
#include "number_utils.h"
#include "decNumber.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;
using namespace icu::numparse::impl;


Scale::Scale(int32_t magnitude, DecNum* arbitraryToAdopt)
        : fMagnitude(magnitude), fArbitrary(arbitraryToAdopt), fError(U_ZERO_ERROR) {
    if (fArbitrary != nullptr) {
        // Attempt to convert the DecNum to a magnitude multiplier.
        fArbitrary->normalize();
        if (fArbitrary->getRawDecNumber()->digits == 1 && fArbitrary->getRawDecNumber()->lsu[0] == 1 &&
            !fArbitrary->isNegative()) {
            // Success!
            fMagnitude += fArbitrary->getRawDecNumber()->exponent;
            delete fArbitrary;
            fArbitrary = nullptr;
        }
    }
}

Scale::Scale(const Scale& other)
        : fMagnitude(other.fMagnitude), fArbitrary(nullptr), fError(other.fError) {
    if (other.fArbitrary != nullptr) {
        UErrorCode localStatus = U_ZERO_ERROR;
        fArbitrary = new DecNum(*other.fArbitrary, localStatus);
    }
}

Scale& Scale::operator=(const Scale& other) {
    if (this == &other) { return *this; }  // self-assignment: no-op
    fMagnitude = other.fMagnitude;
    if (other.fArbitrary != nullptr) {
        UErrorCode localStatus = U_ZERO_ERROR;
        fArbitrary = new DecNum(*other.fArbitrary, localStatus);
    } else {
        fArbitrary = nullptr;
    }
    fError = other.fError;
    return *this;
}

Scale::Scale(Scale&& src) noexcept
        : fMagnitude(src.fMagnitude), fArbitrary(src.fArbitrary), fError(src.fError) {
    // Take ownership away from src if necessary
    src.fArbitrary = nullptr;
}

Scale& Scale::operator=(Scale&& src) noexcept {
    fMagnitude = src.fMagnitude;
    delete fArbitrary;
    fArbitrary = src.fArbitrary;
    fError = src.fError;
    // Take ownership away from src if necessary
    src.fArbitrary = nullptr;
    return *this;
}

Scale::~Scale() {
    delete fArbitrary;
}


Scale Scale::none() {
    return {0, nullptr};
}

Scale Scale::powerOfTen(int32_t power) {
    return {power, nullptr};
}

Scale Scale::byDecimal(StringPiece multiplicand) {
    UErrorCode localError = U_ZERO_ERROR;
    LocalPointer<DecNum> decnum(new DecNum(), localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    decnum->setTo(multiplicand, localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    return {0, decnum.orphan()};
}

Scale Scale::byDouble(double multiplicand) {
    UErrorCode localError = U_ZERO_ERROR;
    LocalPointer<DecNum> decnum(new DecNum(), localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    decnum->setTo(multiplicand, localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    return {0, decnum.orphan()};
}

Scale Scale::byDoubleAndPowerOfTen(double multiplicand, int32_t power) {
    UErrorCode localError = U_ZERO_ERROR;
    LocalPointer<DecNum> decnum(new DecNum(), localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    decnum->setTo(multiplicand, localError);
    if (U_FAILURE(localError)) {
        return {localError};
    }
    return {power, decnum.orphan()};
}

void Scale::applyTo(impl::DecimalQuantity& quantity) const {
    quantity.adjustMagnitude(fMagnitude);
    if (fArbitrary != nullptr) {
        UErrorCode localStatus = U_ZERO_ERROR;
        quantity.multiplyBy(*fArbitrary, localStatus);
    }
}

void Scale::applyReciprocalTo(impl::DecimalQuantity& quantity) const {
    quantity.adjustMagnitude(-fMagnitude);
    if (fArbitrary != nullptr) {
        UErrorCode localStatus = U_ZERO_ERROR;
        quantity.divideBy(*fArbitrary, localStatus);
    }
}


void
MultiplierFormatHandler::setAndChain(const Scale& multiplier, const MicroPropsGenerator* parent) {
    fMultiplier = multiplier;
    fParent = parent;
}

void MultiplierFormatHandler::processQuantity(DecimalQuantity& quantity, MicroProps& micros,
                                              UErrorCode& status) const {
    fParent->processQuantity(quantity, micros, status);
    fMultiplier.applyTo(quantity);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
