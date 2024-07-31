// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_PATTERNSTRING_H__
#define __NUMBER_PATTERNSTRING_H__


#include <cstdint>
#include "unicode/unum.h"
#include "unicode/unistr.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "number_decimfmtprops.h"
#include "number_affixutils.h"

U_NAMESPACE_BEGIN
namespace number::impl {

// Forward declaration
class PatternParser;

// Note: the order of fields in this enum matters for parsing.
enum PatternSignType {
    /** Render using normal positive subpattern rules */
    PATTERN_SIGN_TYPE_POS,
    /** Render using rules to force the display of a plus sign */
    PATTERN_SIGN_TYPE_POS_SIGN,
    /** Render using negative subpattern rules */
    PATTERN_SIGN_TYPE_NEG,
    /** Count for looping over the possibilities */
    PATTERN_SIGN_TYPE_COUNT
};

// Exported as U_I18N_API because it is a public member field of exported ParsedSubpatternInfo
struct U_I18N_API Endpoints {
    int32_t start = 0;
    int32_t end = 0;
};

// Exported as U_I18N_API because it is a public member field of exported ParsedPatternInfo
struct U_I18N_API ParsedSubpatternInfo {
    uint64_t groupingSizes = 0x0000ffffffff0000L;
    int32_t integerLeadingHashSigns = 0;
    int32_t integerTrailingHashSigns = 0;
    int32_t integerNumerals = 0;
    int32_t integerAtSigns = 0;
    int32_t integerTotal = 0; // for convenience
    int32_t fractionNumerals = 0;
    int32_t fractionHashSigns = 0;
    int32_t fractionTotal = 0; // for convenience
    bool hasDecimal = false;
    int32_t widthExceptAffixes = 0;
    // Note: NullableValue causes issues here with std::move.
    bool hasPadding = false;
    UNumberFormatPadPosition paddingLocation = UNUM_PAD_BEFORE_PREFIX;
    DecimalQuantity rounding;
    bool exponentHasPlusSign = false;
    int32_t exponentZeros = 0;
    bool hasPercentSign = false;
    bool hasPerMilleSign = false;
    bool hasCurrencySign = false;
    bool hasCurrencyDecimal = false;
    bool hasMinusSign = false;
    bool hasPlusSign = false;

    Endpoints prefixEndpoints;
    Endpoints suffixEndpoints;
    Endpoints paddingEndpoints;
};

// Exported as U_I18N_API because it is needed for the unit test PatternStringTest
struct U_I18N_API ParsedPatternInfo : public AffixPatternProvider, public UMemory {
    UnicodeString pattern;
    ParsedSubpatternInfo positive;
    ParsedSubpatternInfo negative;

    ParsedPatternInfo()
            : state(this->pattern), currentSubpattern(nullptr) {}

    ~ParsedPatternInfo() override = default;

    // Need to declare this explicitly because of the destructor
    ParsedPatternInfo& operator=(ParsedPatternInfo&& src) noexcept = default;

    static int32_t getLengthFromEndpoints(const Endpoints& endpoints);

    char16_t charAt(int32_t flags, int32_t index) const override;

    int32_t length(int32_t flags) const override;

    UnicodeString getString(int32_t flags) const override;

    bool positiveHasPlusSign() const override;

    bool hasNegativeSubpattern() const override;

    bool negativeHasMinusSign() const override;

    bool hasCurrencySign() const override;

    bool containsSymbolType(AffixPatternType type, UErrorCode& status) const override;

    bool hasBody() const override;

    bool currencyAsDecimal() const override;

  private:
    struct U_I18N_API ParserState {
        const UnicodeString& pattern; // reference to the parent
        int32_t offset = 0;

        explicit ParserState(const UnicodeString& _pattern)
                : pattern(_pattern) {}

        ParserState& operator=(ParserState&& src) noexcept {
            // Leave pattern reference alone; it will continue to point to the same place in memory,
            // which gets overwritten by ParsedPatternInfo's implicit move assignment.
            offset = src.offset;
            return *this;
        }

        /** Returns the next code point, or -1 if string is too short. */
        UChar32 peek();

        /** Returns the code point after the next code point, or -1 if string is too short. */
        UChar32 peek2();

        /** Returns the next code point and then steps forward. */
        UChar32 next();

        // TODO: We don't currently do anything with the message string.
        // This method is here as a shell for Java compatibility.
        inline void toParseException(const char16_t* message) { (void) message; }
    } state;

    // NOTE: In Java, these are written as pure functions.
    // In C++, they're written as methods.
    // The behavior is the same.

    // Mutable transient pointer:
    ParsedSubpatternInfo* currentSubpattern;

    // In Java, "negative == null" tells us whether or not we had a negative subpattern.
    // In C++, we need to remember in another boolean.
    bool fHasNegativeSubpattern = false;

    const Endpoints& getEndpoints(int32_t flags) const;

    /** Run the recursive descent parser. */
    void consumePattern(const UnicodeString& patternString, UErrorCode& status);

    void consumeSubpattern(UErrorCode& status);

    void consumePadding(PadPosition paddingLocation, UErrorCode& status);

    void consumeAffix(Endpoints& endpoints, UErrorCode& status);

    void consumeLiteral(UErrorCode& status);

    void consumeFormat(UErrorCode& status);

    void consumeIntegerFormat(UErrorCode& status);

    void consumeFractionFormat(UErrorCode& status);

    void consumeExponent(UErrorCode& status);

    friend class PatternParser;
};

enum IgnoreRounding {
    IGNORE_ROUNDING_NEVER = 0, IGNORE_ROUNDING_IF_CURRENCY = 1, IGNORE_ROUNDING_ALWAYS = 2
};

class U_I18N_API PatternParser {
  public:
    /**
     * Runs the recursive descent parser on the given pattern string, returning a data structure with raw information
     * about the pattern string.
     *
     * <p>
     * To obtain a more useful form of the data, consider using {@link #parseToProperties} instead.
     *
     * TODO: Change argument type to const char16_t* instead of UnicodeString?
     *
     * @param patternString
     *            The LDML decimal format pattern (Excel-style pattern) to parse.
     * @return The results of the parse.
     */
    static void parseToPatternInfo(const UnicodeString& patternString, ParsedPatternInfo& patternInfo,
                                   UErrorCode& status);

    /**
     * Parses a pattern string into a new property bag.
     *
     * @param pattern
     *            The pattern string, like "#,##0.00"
     * @param ignoreRounding
     *            Whether to leave out rounding information (minFrac, maxFrac, and rounding increment) when parsing the
     *            pattern. This may be desirable if a custom rounding mode, such as CurrencyUsage, is to be used
     *            instead.
     * @return A property bag object.
     * @throws IllegalArgumentException
     *             If there is a syntax error in the pattern string.
     */
    static DecimalFormatProperties parseToProperties(const UnicodeString& pattern,
                                                     IgnoreRounding ignoreRounding, UErrorCode& status);

    static DecimalFormatProperties parseToProperties(const UnicodeString& pattern, UErrorCode& status);

    /**
     * Parses a pattern string into an existing property bag. All properties that can be encoded into a pattern string
     * will be overwritten with either their default value or with the value coming from the pattern string. Properties
     * that cannot be encoded into a pattern string, such as rounding mode, are not modified.
     *
     * @param pattern
     *            The pattern string, like "#,##0.00"
     * @param properties
     *            The property bag object to overwrite.
     * @param ignoreRounding
     *            See {@link #parseToProperties(String pattern, int ignoreRounding)}.
     * @throws IllegalArgumentException
     *             If there was a syntax error in the pattern string.
     */
    static void parseToExistingProperties(const UnicodeString& pattern,
                                          DecimalFormatProperties& properties,
                                          IgnoreRounding ignoreRounding, UErrorCode& status);

  private:
    static void parseToExistingPropertiesImpl(const UnicodeString& pattern,
                                              DecimalFormatProperties& properties,
                                              IgnoreRounding ignoreRounding, UErrorCode& status);

    /** Finalizes the temporary data stored in the ParsedPatternInfo to the Properties. */
    static void patternInfoToProperties(DecimalFormatProperties& properties,
                                        ParsedPatternInfo& patternInfo, IgnoreRounding _ignoreRounding,
                                        UErrorCode& status);
};

class U_I18N_API PatternStringUtils {
  public:
    /**
     * Determine whether a given roundingIncrement should be ignored for formatting
     * based on the current maxFrac value (maximum fraction digits). For example a
     * roundingIncrement of 0.01 should be ignored if maxFrac is 1, but not if maxFrac
     * is 2 or more. Note that roundingIncrements are rounded up in significance, so
     * a roundingIncrement of 0.006 is treated like 0.01 for this determination, i.e.
     * it should not be ignored if maxFrac is 2 or more (but a roundingIncrement of
     * 0.005 is treated like 0.001 for significance).
     *
     * This test is needed for both NumberPropertyMapper::oldToNew and 
     * PatternStringUtils::propertiesToPatternString. In Java it cannot be
     * exported by NumberPropertyMapper (package private) so it is in
     * PatternStringUtils, do the same in C.
     *
     * @param roundIncr
     *            The roundingIncrement to be checked. Must be non-zero.
     * @param maxFrac
     *            The current maximum fraction digits value.
     * @return true if roundIncr should be ignored for formatting.
     */
     static bool ignoreRoundingIncrement(double roundIncr, int32_t maxFrac);

    /**
     * Creates a pattern string from a property bag.
     *
     * <p>
     * Since pattern strings support only a subset of the functionality available in a property bag, a new property bag
     * created from the string returned by this function may not be the same as the original property bag.
     *
     * @param properties
     *            The property bag to serialize.
     * @return A pattern string approximately serializing the property bag.
     */
    static UnicodeString propertiesToPatternString(const DecimalFormatProperties& properties,
                                                   UErrorCode& status);


    /**
     * Converts a pattern between standard notation and localized notation. Localized notation means that instead of
     * using generic placeholders in the pattern, you use the corresponding locale-specific characters instead. For
     * example, in locale <em>fr-FR</em>, the period in the pattern "0.000" means "decimal" in standard notation (as it
     * does in every other locale), but it means "grouping" in localized notation.
     *
     * <p>
     * A greedy string-substitution strategy is used to substitute locale symbols. If two symbols are ambiguous or have
     * the same prefix, the result is not well-defined.
     *
     * <p>
     * Locale symbols are not allowed to contain the ASCII quote character.
     *
     * <p>
     * This method is provided for backwards compatibility and should not be used in any new code.
     *
     * TODO(C++): This method is not yet implemented.
     *
     * @param input
     *            The pattern to convert.
     * @param symbols
     *            The symbols corresponding to the localized pattern.
     * @param toLocalized
     *            true to convert from standard to localized notation; false to convert from localized to standard
     *            notation.
     * @return The pattern expressed in the other notation.
     */
    static UnicodeString convertLocalized(const UnicodeString& input, const DecimalFormatSymbols& symbols,
                                          bool toLocalized, UErrorCode& status);

    /**
     * This method contains the heart of the logic for rendering LDML affix strings. It handles
     * sign-always-shown resolution, whether to use the positive or negative subpattern, permille
     * substitution, and plural forms for CurrencyPluralInfo.
     */
    static void patternInfoToStringBuilder(const AffixPatternProvider& patternInfo, bool isPrefix,
                                           PatternSignType patternSignType,
                                           bool approximately,
                                           StandardPlural::Form plural,
                                           bool perMilleReplacesPercent,
                                           bool dropCurrencySymbols,
                                           UnicodeString& output);

    static PatternSignType resolveSignDisplay(UNumberSignDisplay signDisplay, Signum signum);

  private:
    /** @return The number of chars inserted. */
    static int escapePaddingString(UnicodeString input, UnicodeString& output, int startIndex,
                                   UErrorCode& status);
};

} // namespace number::impl
U_NAMESPACE_END


#endif //__NUMBER_PATTERNSTRING_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
