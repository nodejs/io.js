// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_DECIMFMTPROPS_H__
#define __NUMBER_DECIMFMTPROPS_H__

#include "unicode/unistr.h"
#include <cstdint>
#include "unicode/plurrule.h"
#include "unicode/currpinf.h"
#include "unicode/unum.h"
#include "unicode/localpointer.h"
#include "number_types.h"

U_NAMESPACE_BEGIN

// Export an explicit template instantiation of the LocalPointer that is used as a
// data member of CurrencyPluralInfoWrapper.
// (When building DLLs for Windows this is required.)
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API LocalPointerBase<CurrencyPluralInfo>;
template class U_I18N_API LocalPointer<CurrencyPluralInfo>;
#endif

namespace number::impl {

// Exported as U_I18N_API because it is a public member field of exported DecimalFormatProperties
// Using this wrapper is rather unfortunate, but is needed on Windows platforms in order to allow
// for DLL-exporting a fully specified template instantiation.
class U_I18N_API CurrencyPluralInfoWrapper {
public:
    LocalPointer<CurrencyPluralInfo> fPtr;

    CurrencyPluralInfoWrapper() = default;

    CurrencyPluralInfoWrapper(const CurrencyPluralInfoWrapper& other) {
        if (!other.fPtr.isNull()) {
            fPtr.adoptInstead(new CurrencyPluralInfo(*other.fPtr));
        }
    }

    CurrencyPluralInfoWrapper& operator=(const CurrencyPluralInfoWrapper& other) {
        if (this != &other &&  // self-assignment: no-op
                !other.fPtr.isNull()) {
            fPtr.adoptInstead(new CurrencyPluralInfo(*other.fPtr));
        }
        return *this;
    }
};

/** Controls the set of rules for parsing a string from the old DecimalFormat API. */
enum ParseMode {
    /**
     * Lenient mode should be used if you want to accept malformed user input. It will use heuristics
     * to attempt to parse through typographical errors in the string.
     */
            PARSE_MODE_LENIENT,

    /**
     * Strict mode should be used if you want to require that the input is well-formed. More
     * specifically, it differs from lenient mode in the following ways:
     *
     * <ul>
     * <li>Grouping widths must match the grouping settings. For example, "12,3,45" will fail if the
     * grouping width is 3, as in the pattern "#,##0".
     * <li>The string must contain a complete prefix and suffix. For example, if the pattern is
     * "{#};(#)", then "{123}" or "(123)" would match, but "{123", "123}", and "123" would all fail.
     * (The latter strings would be accepted in lenient mode.)
     * <li>Whitespace may not appear at arbitrary places in the string. In lenient mode, whitespace
     * is allowed to occur arbitrarily before and after prefixes and exponent separators.
     * <li>Leading grouping separators are not allowed, as in ",123".
     * <li>Minus and plus signs can only appear if specified in the pattern. In lenient mode, a plus
     * or minus sign can always precede a number.
     * <li>The set of characters that can be interpreted as a decimal or grouping separator is
     * smaller.
     * <li><strong>If currency parsing is enabled,</strong> currencies must only appear where
     * specified in either the current pattern string or in a valid pattern string for the current
     * locale. For example, if the pattern is "¤0.00", then "$1.23" would match, but "1.23$" would
     * fail to match.
     * </ul>
     */
            PARSE_MODE_STRICT,
};

// Exported as U_I18N_API because it is needed for the unit test PatternStringTest
struct U_I18N_API DecimalFormatProperties : public UMemory {

  public:
    NullableValue<UNumberCompactStyle> compactStyle;
    NullableValue<CurrencyUnit> currency;
    CurrencyPluralInfoWrapper currencyPluralInfo;
    NullableValue<UCurrencyUsage> currencyUsage;
    bool decimalPatternMatchRequired;
    bool decimalSeparatorAlwaysShown;
    bool exponentSignAlwaysShown;
    bool currencyAsDecimal;
    bool formatFailIfMoreThanMaxDigits; // ICU4C-only
    int32_t formatWidth;
    int32_t groupingSize;
    bool groupingUsed;
    int32_t magnitudeMultiplier; // internal field like multiplierScale but separate to avoid conflict
    int32_t maximumFractionDigits;
    int32_t maximumIntegerDigits;
    int32_t maximumSignificantDigits;
    int32_t minimumExponentDigits;
    int32_t minimumFractionDigits;
    int32_t minimumGroupingDigits;
    int32_t minimumIntegerDigits;
    int32_t minimumSignificantDigits;
    int32_t multiplier;
    int32_t multiplierScale; // ICU4C-only
    UnicodeString negativePrefix;
    UnicodeString negativePrefixPattern;
    UnicodeString negativeSuffix;
    UnicodeString negativeSuffixPattern;
    NullableValue<PadPosition> padPosition;
    UnicodeString padString;
    bool parseCaseSensitive;
    bool parseIntegerOnly;
    NullableValue<ParseMode> parseMode;
    bool parseNoExponent;
    bool parseToBigDecimal; // TODO: Not needed in ICU4C?
    UNumberFormatAttributeValue parseAllInput; // ICU4C-only
    //PluralRules pluralRules;
    UnicodeString positivePrefix;
    UnicodeString positivePrefixPattern;
    UnicodeString positiveSuffix;
    UnicodeString positiveSuffixPattern;
    double roundingIncrement;
    NullableValue<RoundingMode> roundingMode;
    int32_t secondaryGroupingSize;
    bool signAlwaysShown;

    DecimalFormatProperties();

    inline bool operator==(const DecimalFormatProperties& other) const {
        return _equals(other, false);
    }

    void clear();

    /**
     * Checks for equality to the default DecimalFormatProperties, but ignores the prescribed set of
     * options for fast-path formatting.
     */
    bool equalsDefaultExceptFastFormat() const;

    /**
     * Returns the default DecimalFormatProperties instance.
     */
    static const DecimalFormatProperties& getDefault();

  private:
    bool _equals(const DecimalFormatProperties& other, bool ignoreForFastFormat) const;
};

} // namespace number::impl

U_NAMESPACE_END


#endif //__NUMBER_DECIMFMTPROPS_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
