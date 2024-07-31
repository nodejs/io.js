// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_AFFIXES_H__
#define __NUMPARSE_AFFIXES_H__

#include "cmemory.h"

#include "numparse_types.h"
#include "numparse_symbols.h"
#include "numparse_currency.h"
#include "number_affixutils.h"
#include "number_currencysymbols.h"

U_NAMESPACE_BEGIN

namespace numparse::impl {

// Forward-declaration of implementation classes for friending
class AffixPatternMatcherBuilder;
class AffixPatternMatcher;

using ::icu::number::impl::AffixPatternProvider;
using ::icu::number::impl::TokenConsumer;
using ::icu::number::impl::CurrencySymbols;


class U_I18N_API CodePointMatcher : public NumberParseMatcher, public UMemory {
  public:
    CodePointMatcher() = default;  // WARNING: Leaves the object in an unusable state

    CodePointMatcher(UChar32 cp);

    bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const override;

    bool smokeTest(const StringSegment& segment) const override;

    UnicodeString toString() const override;

  private:
    UChar32 fCp;
};

} // namespace numparse::impl

// Export a explicit template instantiations of MaybeStackArray, MemoryPool and CompactUnicodeString.
// When building DLLs for Windows this is required even though no direct access leaks out of the i18n library.
// (See digitlst.h, pluralaffix.h, datefmt.h, and others for similar examples.)
// Note: These need to be outside of the numparse::impl namespace, or Clang will generate a compile error.
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API MaybeStackArray<numparse::impl::CodePointMatcher*, 8>; 
template class U_I18N_API MaybeStackArray<char16_t, 4>;
template class U_I18N_API MemoryPool<numparse::impl::CodePointMatcher, 8>;
template class U_I18N_API numparse::impl::CompactUnicodeString<4>;
#endif

namespace numparse::impl {

struct AffixTokenMatcherSetupData {
    const CurrencySymbols& currencySymbols;
    const DecimalFormatSymbols& dfs;
    IgnorablesMatcher& ignorables;
    const Locale& locale;
    parse_flags_t parseFlags;
};


/**
 * Small helper class that generates matchers for individual tokens for AffixPatternMatcher.
 *
 * In Java, this is called AffixTokenMatcherFactory (a "factory"). However, in C++, it is called a
 * "warehouse", because in addition to generating the matchers, it also retains ownership of them. The
 * warehouse must stay in scope for the whole lifespan of the AffixPatternMatcher that uses matchers from
 * the warehouse.
 *
 * @author sffc
 */
// Exported as U_I18N_API for tests
class U_I18N_API AffixTokenMatcherWarehouse : public UMemory {
  public:
    AffixTokenMatcherWarehouse() = default;  // WARNING: Leaves the object in an unusable state

    AffixTokenMatcherWarehouse(const AffixTokenMatcherSetupData* setupData);

    NumberParseMatcher& minusSign();

    NumberParseMatcher& plusSign();

    NumberParseMatcher& percent();

    NumberParseMatcher& permille();

    NumberParseMatcher& currency(UErrorCode& status);

    IgnorablesMatcher& ignorables();

    NumberParseMatcher* nextCodePointMatcher(UChar32 cp, UErrorCode& status);

    bool hasEmptyCurrencySymbol() const;

  private:
    // NOTE: The following field may be unsafe to access after construction is done!
    const AffixTokenMatcherSetupData* fSetupData;

    // NOTE: These are default-constructed and should not be used until initialized.
    MinusSignMatcher fMinusSign;
    PlusSignMatcher fPlusSign;
    PercentMatcher fPercent;
    PermilleMatcher fPermille;
    CombinedCurrencyMatcher fCurrency;

    // Use a child class for code point matchers, since it requires non-default operators.
    MemoryPool<CodePointMatcher> fCodePoints;

    friend class AffixPatternMatcherBuilder;
    friend class AffixPatternMatcher;
};


class AffixPatternMatcherBuilder : public TokenConsumer, public MutableMatcherCollection {
  public:
    AffixPatternMatcherBuilder(const UnicodeString& pattern, AffixTokenMatcherWarehouse& warehouse,
                               IgnorablesMatcher* ignorables);

    void consumeToken(::icu::number::impl::AffixPatternType type, UChar32 cp, UErrorCode& status) override;

    /** NOTE: You can build only once! */
    AffixPatternMatcher build(UErrorCode& status);

  private:
    ArraySeriesMatcher::MatcherArray fMatchers;
    int32_t fMatchersLen;
    int32_t fLastTypeOrCp;

    const UnicodeString& fPattern;
    AffixTokenMatcherWarehouse& fWarehouse;
    IgnorablesMatcher* fIgnorables;

    void addMatcher(NumberParseMatcher& matcher) override;
};


// Exported as U_I18N_API for tests
class U_I18N_API AffixPatternMatcher : public ArraySeriesMatcher {
  public:
    AffixPatternMatcher() = default;  // WARNING: Leaves the object in an unusable state

    static AffixPatternMatcher fromAffixPattern(const UnicodeString& affixPattern,
                                                AffixTokenMatcherWarehouse& warehouse,
                                                parse_flags_t parseFlags, bool* success,
                                                UErrorCode& status);

    UnicodeString getPattern() const;

    bool operator==(const AffixPatternMatcher& other) const;

  private:
    CompactUnicodeString<4> fPattern;

    AffixPatternMatcher(MatcherArray& matchers, int32_t matchersLen, const UnicodeString& pattern,
                        UErrorCode& status);

    friend class AffixPatternMatcherBuilder;
};


class AffixMatcher : public NumberParseMatcher, public UMemory {
  public:
    AffixMatcher() = default;  // WARNING: Leaves the object in an unusable state

    AffixMatcher(AffixPatternMatcher* prefix, AffixPatternMatcher* suffix, result_flags_t flags);

    bool match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const override;

    void postProcess(ParsedNumber& result) const override;

    bool smokeTest(const StringSegment& segment) const override;

    int8_t compareTo(const AffixMatcher& rhs) const;

    UnicodeString toString() const override;

  private:
    AffixPatternMatcher* fPrefix;
    AffixPatternMatcher* fSuffix;
    result_flags_t fFlags;
};


/**
 * A C++-only class to retain ownership of the AffixMatchers needed for parsing.
 */
class AffixMatcherWarehouse {
  public:
    AffixMatcherWarehouse() = default;  // WARNING: Leaves the object in an unusable state

    AffixMatcherWarehouse(AffixTokenMatcherWarehouse* tokenWarehouse);

    void createAffixMatchers(const AffixPatternProvider& patternInfo, MutableMatcherCollection& output,
                             const IgnorablesMatcher& ignorables, parse_flags_t parseFlags,
                             UErrorCode& status);

  private:
    // 18 is the limit: positive, zero, and negative, each with prefix, suffix, and prefix+suffix,
    // and doubled since there may be an empty currency symbol
    AffixMatcher fAffixMatchers[18];
    // 6 is the limit: positive, zero, and negative, a prefix and a suffix for each,
    // and doubled since there may be an empty currency symbol
    AffixPatternMatcher fAffixPatternMatchers[12];
    // Reference to the warehouse for tokens used by the AffixPatternMatchers
    AffixTokenMatcherWarehouse* fTokenWarehouse;

    friend class AffixMatcher;

    static bool isInteresting(const AffixPatternProvider& patternInfo, const IgnorablesMatcher& ignorables,
                              parse_flags_t parseFlags, UErrorCode& status);
};

} // namespace numparse::impl

U_NAMESPACE_END

#endif //__NUMPARSE_AFFIXES_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
