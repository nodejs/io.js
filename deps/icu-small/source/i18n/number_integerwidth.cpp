// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/numberformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;

IntegerWidth::IntegerWidth(digits_t minInt, digits_t maxInt, bool formatFailIfMoreThanMaxDigits) {
    fUnion.minMaxInt.fMinInt = minInt;
    fUnion.minMaxInt.fMaxInt = maxInt;
    fUnion.minMaxInt.fFormatFailIfMoreThanMaxDigits = formatFailIfMoreThanMaxDigits;
}

IntegerWidth IntegerWidth::zeroFillTo(int32_t minInt) {
    if (minInt >= 0 && minInt <= kMaxIntFracSig) {
        return {static_cast<digits_t>(minInt), -1, false};
    } else {
        return {U_NUMBER_ARG_OUTOFBOUNDS_ERROR};
    }
}

IntegerWidth IntegerWidth::truncateAt(int32_t maxInt) {
    if (fHasError) { return *this; }  // No-op on error
    digits_t minInt = fUnion.minMaxInt.fMinInt;
    if (maxInt >= 0 && maxInt <= kMaxIntFracSig && minInt <= maxInt) {
        return {minInt, static_cast<digits_t>(maxInt), false};
    } else if (maxInt == -1) {
        return {minInt, -1, false};
    } else {
        return {U_NUMBER_ARG_OUTOFBOUNDS_ERROR};
    }
}

void IntegerWidth::apply(impl::DecimalQuantity& quantity, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if (fHasError) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    } else if (fUnion.minMaxInt.fMaxInt == -1) {
        quantity.increaseMinIntegerTo(fUnion.minMaxInt.fMinInt);
    } else {
        // Enforce the backwards-compatibility feature "FormatFailIfMoreThanMaxDigits"
        if (fUnion.minMaxInt.fFormatFailIfMoreThanMaxDigits &&
            fUnion.minMaxInt.fMaxInt < quantity.getMagnitude()) {
            status = U_ILLEGAL_ARGUMENT_ERROR;
        }
        quantity.increaseMinIntegerTo(fUnion.minMaxInt.fMinInt);
        quantity.applyMaxInteger(fUnion.minMaxInt.fMaxInt);
    }
}

bool IntegerWidth::operator==(const IntegerWidth& other) const {
    // Private operator==; do error and bogus checking first!
    U_ASSERT(!fHasError);
    U_ASSERT(!other.fHasError);
    U_ASSERT(!isBogus());
    U_ASSERT(!other.isBogus());
    return fUnion.minMaxInt.fMinInt == other.fUnion.minMaxInt.fMinInt &&
           fUnion.minMaxInt.fMaxInt == other.fUnion.minMaxInt.fMaxInt;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
