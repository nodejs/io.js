// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numparse_types.h"
#include "number_currencysymbols.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


CurrencySymbols::CurrencySymbols(CurrencyUnit currency, const Locale& locale, UErrorCode& status)
        : fCurrency(currency), fLocaleName(locale.getName(), status) {
    fCurrencySymbol.setToBogus();
    fIntlCurrencySymbol.setToBogus();
}

CurrencySymbols::CurrencySymbols(CurrencyUnit currency, const Locale& locale,
                                 const DecimalFormatSymbols& symbols, UErrorCode& status)
        : CurrencySymbols(currency, locale, status) {
    // If either of the overrides is present, save it in the local UnicodeString.
    if (symbols.isCustomCurrencySymbol()) {
        fCurrencySymbol = symbols.getConstSymbol(DecimalFormatSymbols::kCurrencySymbol);
    }
    if (symbols.isCustomIntlCurrencySymbol()) {
        fIntlCurrencySymbol = symbols.getConstSymbol(DecimalFormatSymbols::kIntlCurrencySymbol);
    }
}

const char16_t* CurrencySymbols::getIsoCode() const {
    return fCurrency.getISOCurrency();
}

UnicodeString CurrencySymbols::getNarrowCurrencySymbol(UErrorCode& status) const {
    // Note: currently no override is available for narrow currency symbol
    return loadSymbol(UCURR_NARROW_SYMBOL_NAME, status);
}

UnicodeString CurrencySymbols::getFormalCurrencySymbol(UErrorCode& status) const {
    // Note: currently no override is available for formal currency symbol
    return loadSymbol(UCURR_FORMAL_SYMBOL_NAME, status);
}

UnicodeString CurrencySymbols::getVariantCurrencySymbol(UErrorCode& status) const {
    // Note: currently no override is available for variant currency symbol
    return loadSymbol(UCURR_VARIANT_SYMBOL_NAME, status);
}

UnicodeString CurrencySymbols::getCurrencySymbol(UErrorCode& status) const {
    if (!fCurrencySymbol.isBogus()) {
        return fCurrencySymbol;
    }
    return loadSymbol(UCURR_SYMBOL_NAME, status);
}

UnicodeString CurrencySymbols::loadSymbol(UCurrNameStyle selector, UErrorCode& status) const {
    const char16_t* isoCode = fCurrency.getISOCurrency();
    int32_t symbolLen = 0;
    const char16_t* symbol = ucurr_getName(
            isoCode,
            fLocaleName.data(),
            selector,
            nullptr /* isChoiceFormat */,
            &symbolLen,
            &status);
    // If given an unknown currency, ucurr_getName returns the input string, which we can't alias safely!
    // Otherwise, symbol points to a resource bundle, and we can use readonly-aliasing constructor.
    if (symbol == isoCode) {
        return UnicodeString(isoCode, 3);
    } else {
        return UnicodeString(true, symbol, symbolLen);
    }
}

UnicodeString CurrencySymbols::getIntlCurrencySymbol(UErrorCode&) const {
    if (!fIntlCurrencySymbol.isBogus()) {
        return fIntlCurrencySymbol;
    }
    // Note: Not safe to use readonly-aliasing constructor here because the buffer belongs to this object,
    // which could be destructed or moved during the lifetime of the return value.
    return UnicodeString(fCurrency.getISOCurrency(), 3);
}

UnicodeString CurrencySymbols::getPluralName(StandardPlural::Form plural, UErrorCode& status) const {
    const char16_t* isoCode = fCurrency.getISOCurrency();
    int32_t symbolLen = 0;
    const char16_t* symbol = ucurr_getPluralName(
            isoCode,
            fLocaleName.data(),
            nullptr /* isChoiceFormat */,
            StandardPlural::getKeyword(plural),
            &symbolLen,
            &status);
    // If given an unknown currency, ucurr_getName returns the input string, which we can't alias safely!
    // Otherwise, symbol points to a resource bundle, and we can use readonly-aliasing constructor.
    if (symbol == isoCode) {
        return UnicodeString(isoCode, 3);
    } else {
        return UnicodeString(true, symbol, symbolLen);
    }
}

bool CurrencySymbols::hasEmptyCurrencySymbol() const {
    return !fCurrencySymbol.isBogus() && fCurrencySymbol.isEmpty();
}


CurrencyUnit
icu::number::impl::resolveCurrency(const DecimalFormatProperties& properties, const Locale& locale,
                                   UErrorCode& status) {
    if (!properties.currency.isNull()) {
        return properties.currency.getNoError();
    } else {
        UErrorCode localStatus = U_ZERO_ERROR;
        char16_t buf[4] = {};
        ucurr_forLocale(locale.getName(), buf, 4, &localStatus);
        if (U_SUCCESS(localStatus)) {
            return CurrencyUnit(buf, status);
        } else {
            // Default currency (XXX)
            return {};
        }
    }
}


#endif /* #if !UCONFIG_NO_FORMATTING */
