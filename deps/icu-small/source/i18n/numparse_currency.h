// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_CURRENCY_H__
#define __NUMPARSE_CURRENCY_H__

#include "numparse_types.h"
#include "numparse_compositions.h"
#include "charstr.h"
#include "number_currencysymbols.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN
namespace numparse::impl {

using ::icu::number::impl::CurrencySymbols;

/**
 * Matches a currency, either a custom currency or one from the data bundle. The class is called
 * "combined" to emphasize that the currency string may come from one of multiple sources.
 *
 * Will match currency spacing either before or after the number depending on whether we are currently in
 * the prefix or suffix.
 *
 * The implementation of this class is slightly different between J and C. See #13584 for a follow-up.
 *
 * @author sffc
 */
// Exported as U_I18N_API for tests
class U_I18N_API CombinedCurrencyMatcher : public NumberParseMatcher, public UMemory {
  public:
    CombinedCurrencyMatcher() = default;  // WARNING: Leaves the object in an unusable state

    CombinedCurrencyMatcher(const CurrencySymbols& currencySymbols, const DecimalFormatSymbols& dfs,
                            parse_flags_t parseFlags, UErrorCode& status);

    bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const override;

    bool smokeTest(const StringSegment& segment) const override;

    UnicodeString toString() const override;

  private:
    char16_t fCurrencyCode[4];
    UnicodeString fCurrency1;
    UnicodeString fCurrency2;

    bool fUseFullCurrencyData;
    UnicodeString fLocalLongNames[StandardPlural::COUNT];

    UnicodeString afterPrefixInsert;
    UnicodeString beforeSuffixInsert;

    // We could use Locale instead of CharString here, but
    // Locale has a non-trivial default constructor.
    CharString fLocaleName;

    // TODO: See comments in constructor in numparse_currency.cpp
    // UnicodeSet fLeadCodePoints;

    /** Matches the currency string without concern for currency spacing. */
    bool matchCurrency(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const;
};

} // namespace numparse::impl
U_NAMESPACE_END

#endif //__NUMPARSE_CURRENCY_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
