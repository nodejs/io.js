// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __SOURCE_NUMBER_CURRENCYSYMBOLS_H__
#define __SOURCE_NUMBER_CURRENCYSYMBOLS_H__

#include "numparse_types.h"
#include "charstr.h"
#include "number_decimfmtprops.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// Exported as U_I18N_API for tests
class U_I18N_API CurrencySymbols : public UMemory {
  public:
    CurrencySymbols() = default; // default constructor: leaves class in valid but undefined state

    /** Creates an instance in which all symbols are loaded from data. */
    CurrencySymbols(CurrencyUnit currency, const Locale& locale, UErrorCode& status);

    /** Creates an instance in which some symbols might be pre-populated. */
    CurrencySymbols(CurrencyUnit currency, const Locale& locale, const DecimalFormatSymbols& symbols,
                    UErrorCode& status);

    const char16_t* getIsoCode() const;

    UnicodeString getNarrowCurrencySymbol(UErrorCode& status) const;

    UnicodeString getFormalCurrencySymbol(UErrorCode& status) const;

    UnicodeString getVariantCurrencySymbol(UErrorCode& status) const;

    UnicodeString getCurrencySymbol(UErrorCode& status) const;

    UnicodeString getIntlCurrencySymbol(UErrorCode& status) const;

    UnicodeString getPluralName(StandardPlural::Form plural, UErrorCode& status) const;

    bool hasEmptyCurrencySymbol() const;

  protected:
    // Required fields:
    CurrencyUnit fCurrency;
    CharString fLocaleName;

    // Optional fields:
    UnicodeString fCurrencySymbol;
    UnicodeString fIntlCurrencySymbol;

    UnicodeString loadSymbol(UCurrNameStyle selector, UErrorCode& status) const;
};


/**
 * Resolves the effective currency from the property bag.
 */
CurrencyUnit
resolveCurrency(const DecimalFormatProperties& properties, const Locale& locale, UErrorCode& status);

} // namespace number::impl
U_NAMESPACE_END

#endif //__SOURCE_NUMBER_CURRENCYSYMBOLS_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
