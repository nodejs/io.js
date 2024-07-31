// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_UTILS_H__
#define __NUMBER_UTILS_H__

#include "unicode/numberformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "number_scientific.h"
#include "number_patternstring.h"
#include "number_modifiers.h"
#include "number_multiplier.h"
#include "number_roundingutils.h"
#include "decNumber.h"
#include "charstr.h"
#include "formatted_string_builder.h"

U_NAMESPACE_BEGIN

namespace number::impl {

enum CldrPatternStyle {
    CLDR_PATTERN_STYLE_DECIMAL,
    CLDR_PATTERN_STYLE_CURRENCY,
    CLDR_PATTERN_STYLE_ACCOUNTING,
    CLDR_PATTERN_STYLE_PERCENT,
    CLDR_PATTERN_STYLE_SCIENTIFIC,
    CLDR_PATTERN_STYLE_COUNT,
};

// Namespace for naked functions
namespace utils {

inline int32_t insertDigitFromSymbols(FormattedStringBuilder& output, int32_t index, int8_t digit,
                                      const DecimalFormatSymbols& symbols, Field field,
                                      UErrorCode& status) {
    if (symbols.getCodePointZero() != -1) {
        return output.insertCodePoint(index, symbols.getCodePointZero() + digit, field, status);
    }
    return output.insert(index, symbols.getConstDigitSymbol(digit), field, status);
}

inline bool unitIsCurrency(const MeasureUnit& unit) {
    return uprv_strcmp("currency", unit.getType()) == 0;
}

inline bool unitIsBaseUnit(const MeasureUnit& unit) {
    return unit == MeasureUnit();
}

inline bool unitIsPercent(const MeasureUnit& unit) {
    return uprv_strcmp("percent", unit.getSubtype()) == 0;
}

inline bool unitIsPermille(const MeasureUnit& unit) {
    return uprv_strcmp("permille", unit.getSubtype()) == 0;
}

// NOTE: In Java, this method is in NumberFormat.java
const char16_t*
getPatternForStyle(const Locale& locale, const char* nsName, CldrPatternStyle style, UErrorCode& status);

/**
 * Computes the plural form for this number based on the specified set of rules.
 *
 * @param rules A {@link PluralRules} object representing the set of rules.
 * @return The {@link StandardPlural} according to the PluralRules. If the plural form is not in
 *     the set of standard plurals, {@link StandardPlural#OTHER} is returned instead.
 */
inline StandardPlural::Form getStandardPlural(const PluralRules *rules,
                                              const IFixedDecimal &fdec) {
    if (rules == nullptr) {
        // Fail gracefully if the user didn't provide a PluralRules
        return StandardPlural::Form::OTHER;
    } else {
        UnicodeString ruleString = rules->select(fdec);
        return StandardPlural::orOtherFromString(ruleString);
    }
}

/**
 * Computes the plural form after copying the number and applying rounding rules.
 */
inline StandardPlural::Form getPluralSafe(
        const RoundingImpl& rounder,
        const PluralRules* rules,
        const DecimalQuantity& dq,
        UErrorCode& status) {
    // TODO(ICU-20500): Avoid the copy?
    DecimalQuantity copy(dq);
    rounder.apply(copy, status);
    if (U_FAILURE(status)) {
        return StandardPlural::Form::OTHER;
    }
    return getStandardPlural(rules, copy);
}

} // namespace utils

} // namespace number::impl

U_NAMESPACE_END

#endif //__NUMBER_UTILS_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
