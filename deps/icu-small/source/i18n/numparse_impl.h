// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_IMPL_H__
#define __NUMPARSE_IMPL_H__

#include "numparse_types.h"
#include "numparse_decimal.h"
#include "numparse_symbols.h"
#include "numparse_scientific.h"
#include "unicode/uniset.h"
#include "numparse_currency.h"
#include "numparse_affixes.h"
#include "number_decimfmtprops.h"
#include "unicode/localpointer.h"
#include "numparse_validators.h"
#include "number_multiplier.h"
#include "string_segment.h"

U_NAMESPACE_BEGIN

// Export an explicit template instantiation of the MaybeStackArray that is used as a data member of NumberParserImpl.
// When building DLLs for Windows this is required even though no direct access to the MaybeStackArray leaks out of the i18n library.
// (See numparse_compositions.h, numparse_affixes.h, datefmt.h, and others for similar examples.)
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API MaybeStackArray<const numparse::impl::NumberParseMatcher*, 10>;
#endif

namespace numparse::impl {

// Exported as U_I18N_API for tests
class U_I18N_API NumberParserImpl : public MutableMatcherCollection, public UMemory {
  public:
    virtual ~NumberParserImpl();

    static NumberParserImpl* createSimpleParser(const Locale& locale, const UnicodeString& patternString,
                                                parse_flags_t parseFlags, UErrorCode& status);

    static NumberParserImpl* createParserFromProperties(
            const number::impl::DecimalFormatProperties& properties, const DecimalFormatSymbols& symbols,
            bool parseCurrency, UErrorCode& status);

    /**
     * Does NOT take ownership of the matcher. The matcher MUST remain valid for the lifespan of the
     * NumberParserImpl.
     * @param matcher The matcher to reference.
     */
    void addMatcher(NumberParseMatcher& matcher) override;

    void freeze();

    parse_flags_t getParseFlags() const;

    void parse(const UnicodeString& input, bool greedy, ParsedNumber& result, UErrorCode& status) const;

    void parse(const UnicodeString& input, int32_t start, bool greedy, ParsedNumber& result,
               UErrorCode& status) const;

    UnicodeString toString() const;

  private:
    parse_flags_t fParseFlags;
    int32_t fNumMatchers = 0;
    // NOTE: The stack capacity for fMatchers and fLeads should be the same
    MaybeStackArray<const NumberParseMatcher*, 10> fMatchers;
    bool fFrozen = false;

    // WARNING: All of these matchers start in an undefined state (default-constructed).
    // You must use an assignment operator on them before using.
    struct {
        IgnorablesMatcher ignorables;
        InfinityMatcher infinity;
        MinusSignMatcher minusSign;
        NanMatcher nan;
        PaddingMatcher padding;
        PercentMatcher percent;
        PermilleMatcher permille;
        PlusSignMatcher plusSign;
        DecimalMatcher decimal;
        ScientificMatcher scientific;
        CombinedCurrencyMatcher currency;
        AffixMatcherWarehouse affixMatcherWarehouse;
        AffixTokenMatcherWarehouse affixTokenMatcherWarehouse;
    } fLocalMatchers;
    struct {
        RequireAffixValidator affix;
        RequireCurrencyValidator currency;
        RequireDecimalSeparatorValidator decimalSeparator;
        RequireNumberValidator number;
        MultiplierParseHandler multiplier;
    } fLocalValidators;

    explicit NumberParserImpl(parse_flags_t parseFlags);

    void parseGreedy(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const;

    void parseLongestRecursive(
        StringSegment& segment, ParsedNumber& result, int32_t recursionLevels, UErrorCode& status) const;
};

} // namespace numparse::impl

U_NAMESPACE_END

#endif //__NUMPARSE_IMPL_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
