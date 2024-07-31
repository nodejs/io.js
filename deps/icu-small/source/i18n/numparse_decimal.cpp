// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numparse_types.h"
#include "numparse_decimal.h"
#include "static_unicode_sets.h"
#include "numparse_utils.h"
#include "unicode/uchar.h"
#include "putilimp.h"
#include "number_decimalquantity.h"
#include "string_segment.h"

using namespace icu;
using namespace icu::numparse;
using namespace icu::numparse::impl;


DecimalMatcher::DecimalMatcher(const DecimalFormatSymbols& symbols, const Grouper& grouper,
                               parse_flags_t parseFlags) {
    if (0 != (parseFlags & PARSE_FLAG_MONETARY_SEPARATORS)) {
        groupingSeparator = symbols.getConstSymbol(DecimalFormatSymbols::kMonetaryGroupingSeparatorSymbol);
        decimalSeparator = symbols.getConstSymbol(DecimalFormatSymbols::kMonetarySeparatorSymbol);
    } else {
        groupingSeparator = symbols.getConstSymbol(DecimalFormatSymbols::kGroupingSeparatorSymbol);
        decimalSeparator = symbols.getConstSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol);
    }
    bool strictSeparators = 0 != (parseFlags & PARSE_FLAG_STRICT_SEPARATORS);
    unisets::Key groupingKey = strictSeparators ? unisets::STRICT_ALL_SEPARATORS
                                                : unisets::ALL_SEPARATORS;

    // Attempt to find separators in the static cache

    groupingUniSet = unisets::get(groupingKey);
    unisets::Key decimalKey = unisets::chooseFrom(
            decimalSeparator,
            strictSeparators ? unisets::STRICT_COMMA : unisets::COMMA,
            strictSeparators ? unisets::STRICT_PERIOD : unisets::PERIOD);
    if (decimalKey >= 0) {
        decimalUniSet = unisets::get(decimalKey);
    } else if (!decimalSeparator.isEmpty()) {
        auto* set = new UnicodeSet();
        set->add(decimalSeparator.char32At(0));
        set->freeze();
        decimalUniSet = set;
        fLocalDecimalUniSet.adoptInstead(set);
    } else {
        decimalUniSet = unisets::get(unisets::EMPTY);
    }

    if (groupingKey >= 0 && decimalKey >= 0) {
        // Everything is available in the static cache
        separatorSet = groupingUniSet;
        leadSet = unisets::get(
                strictSeparators ? unisets::DIGITS_OR_ALL_SEPARATORS
                                 : unisets::DIGITS_OR_STRICT_ALL_SEPARATORS);
    } else {
        auto* set = new UnicodeSet();
        set->addAll(*groupingUniSet);
        set->addAll(*decimalUniSet);
        set->freeze();
        separatorSet = set;
        fLocalSeparatorSet.adoptInstead(set);
        leadSet = nullptr;
    }

    UChar32 cpZero = symbols.getCodePointZero();
    if (cpZero == -1 || !u_isdigit(cpZero) || u_digit(cpZero, 10) != 0) {
        // Uncommon case: okay to allocate.
        auto* digitStrings = new UnicodeString[10];
        fLocalDigitStrings.adoptInstead(digitStrings);
        for (int32_t i = 0; i <= 9; i++) {
            digitStrings[i] = symbols.getConstDigitSymbol(i);
        }
    }

    requireGroupingMatch = 0 != (parseFlags & PARSE_FLAG_STRICT_GROUPING_SIZE);
    groupingDisabled = 0 != (parseFlags & PARSE_FLAG_GROUPING_DISABLED);
    integerOnly = 0 != (parseFlags & PARSE_FLAG_INTEGER_ONLY);
    grouping1 = grouper.getPrimary();
    grouping2 = grouper.getSecondary();

    // Fraction grouping parsing is disabled for now but could be enabled later.
    // See https://unicode-org.atlassian.net/browse/ICU-10794
    // fractionGrouping = 0 != (parseFlags & PARSE_FLAG_FRACTION_GROUPING_ENABLED);
}

bool DecimalMatcher::match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const {
    return match(segment, result, 0, status);
}

bool DecimalMatcher::match(StringSegment& segment, ParsedNumber& result, int8_t exponentSign,
                           UErrorCode&) const {
    if (result.seenNumber() && exponentSign == 0) {
        // A number has already been consumed.
        return false;
    } else if (exponentSign != 0) {
        // scientific notation always comes after the number
        U_ASSERT(!result.quantity.bogus);
    }

    // Initial offset before any character consumption.
    int32_t initialOffset = segment.getOffset();

    // Return value: whether to ask for more characters.
    bool maybeMore = false;

    // All digits consumed so far.
    number::impl::DecimalQuantity digitsConsumed;
    digitsConsumed.bogus = true;

    // The total number of digits after the decimal place, used for scaling the result.
    int32_t digitsAfterDecimalPlace = 0;

    // The actual grouping and decimal separators used in the string.
    // If non-null, we have seen that token.
    UnicodeString actualGroupingString;
    UnicodeString actualDecimalString;
    actualGroupingString.setToBogus();
    actualDecimalString.setToBogus();

    // Information for two groups: the previous group and the current group.
    //
    // Each group has three pieces of information:
    //
    // Offset: the string position of the beginning of the group, including a leading separator
    // if there was a leading separator. This is needed in case we need to rewind the parse to
    // that position.
    //
    // Separator type:
    // 0 => beginning of string
    // 1 => lead separator is a grouping separator
    // 2 => lead separator is a decimal separator
    //
    // Count: the number of digits in the group. If -1, the group has been validated.
    int32_t currGroupOffset = 0;
    int32_t currGroupSepType = 0;
    int32_t currGroupCount = 0;
    int32_t prevGroupOffset = -1;
    int32_t prevGroupSepType = -1;
    int32_t prevGroupCount = -1;

    while (segment.length() > 0) {
        maybeMore = false;

        // Attempt to match a digit.
        int8_t digit = -1;

        // Try by code point digit value.
        UChar32 cp = segment.getCodePoint();
        if (u_isdigit(cp)) {
            segment.adjustOffset(U16_LENGTH(cp));
            digit = static_cast<int8_t>(u_digit(cp, 10));
        }

        // Try by digit string.
        if (digit == -1 && !fLocalDigitStrings.isNull()) {
            for (int32_t i = 0; i < 10; i++) {
                const UnicodeString& str = fLocalDigitStrings[i];
                if (str.isEmpty()) {
                    continue;
                }
                int32_t overlap = segment.getCommonPrefixLength(str);
                if (overlap == str.length()) {
                    segment.adjustOffset(overlap);
                    digit = static_cast<int8_t>(i);
                    break;
                }
                maybeMore = maybeMore || (overlap == segment.length());
            }
        }

        if (digit >= 0) {
            // Digit was found.
            if (digitsConsumed.bogus) {
                digitsConsumed.bogus = false;
                digitsConsumed.clear();
            }
            digitsConsumed.appendDigit(digit, 0, true);
            currGroupCount++;
            if (!actualDecimalString.isBogus()) {
                digitsAfterDecimalPlace++;
            }
            continue;
        }

        // Attempt to match a literal grouping or decimal separator.
        bool isDecimal = false;
        bool isGrouping = false;

        // 1) Attempt the decimal separator string literal.
        // if (we have not seen a decimal separator yet) { ... }
        if (actualDecimalString.isBogus() && !decimalSeparator.isEmpty()) {
            int32_t overlap = segment.getCommonPrefixLength(decimalSeparator);
            maybeMore = maybeMore || (overlap == segment.length());
            if (overlap == decimalSeparator.length()) {
                isDecimal = true;
                actualDecimalString = decimalSeparator;
            }
        }

        // 2) Attempt to match the actual grouping string literal.
        if (!actualGroupingString.isBogus()) {
            int32_t overlap = segment.getCommonPrefixLength(actualGroupingString);
            maybeMore = maybeMore || (overlap == segment.length());
            if (overlap == actualGroupingString.length()) {
                isGrouping = true;
            }
        }

        // 2.5) Attempt to match a new the grouping separator string literal.
        // if (we have not seen a grouping or decimal separator yet) { ... }
        if (!groupingDisabled && actualGroupingString.isBogus() && actualDecimalString.isBogus() &&
            !groupingSeparator.isEmpty()) {
            int32_t overlap = segment.getCommonPrefixLength(groupingSeparator);
            maybeMore = maybeMore || (overlap == segment.length());
            if (overlap == groupingSeparator.length()) {
                isGrouping = true;
                actualGroupingString = groupingSeparator;
            }
        }

        // 3) Attempt to match a decimal separator from the equivalence set.
        // if (we have not seen a decimal separator yet) { ... }
        // The !isGrouping is to confirm that we haven't yet matched the current character.
        if (!isGrouping && actualDecimalString.isBogus()) {
            if (decimalUniSet->contains(cp)) {
                isDecimal = true;
                actualDecimalString = UnicodeString(cp);
            }
        }

        // 4) Attempt to match a grouping separator from the equivalence set.
        // if (we have not seen a grouping or decimal separator yet) { ... }
        if (!groupingDisabled && actualGroupingString.isBogus() && actualDecimalString.isBogus()) {
            if (groupingUniSet->contains(cp)) {
                isGrouping = true;
                actualGroupingString = UnicodeString(cp);
            }
        }

        // Leave if we failed to match this as a separator.
        if (!isDecimal && !isGrouping) {
            break;
        }

        // Check for conditions when we don't want to accept the separator.
        if (isDecimal && integerOnly) {
            break;
        } else if (currGroupSepType == 2 && isGrouping) {
            // Fraction grouping
            break;
        }

        // Validate intermediate grouping sizes.
        bool prevValidSecondary = validateGroup(prevGroupSepType, prevGroupCount, false);
        bool currValidPrimary = validateGroup(currGroupSepType, currGroupCount, true);
        if (!prevValidSecondary || (isDecimal && !currValidPrimary)) {
            // Invalid grouping sizes.
            if (isGrouping && currGroupCount == 0) {
                // Trailing grouping separators: these are taken care of below
                U_ASSERT(currGroupSepType == 1);
            } else if (requireGroupingMatch) {
                // Strict mode: reject the parse
                digitsConsumed.clear();
                digitsConsumed.bogus = true;
            }
            break;
        } else if (requireGroupingMatch && currGroupCount == 0 && currGroupSepType == 1) {
            break;
        } else {
            // Grouping sizes OK so far.
            prevGroupOffset = currGroupOffset;
            prevGroupCount = currGroupCount;
            if (isDecimal) {
                // Do not validate this group any more.
                prevGroupSepType = -1;
            } else {
                prevGroupSepType = currGroupSepType;
            }
        }

        // OK to accept the separator.
        // Special case: don't update currGroup if it is empty; this allows two grouping
        // separators in a row in lenient mode.
        if (currGroupCount != 0) {
            currGroupOffset = segment.getOffset();
        }
        currGroupSepType = isGrouping ? 1 : 2;
        currGroupCount = 0;
        if (isGrouping) {
            segment.adjustOffset(actualGroupingString.length());
        } else {
            segment.adjustOffset(actualDecimalString.length());
        }
    }

    // End of main loop.
    // Back up if there was a trailing grouping separator.
    // Shift prev -> curr so we can check it as a final group.
    if (currGroupSepType != 2 && currGroupCount == 0) {
        maybeMore = true;
        segment.setOffset(currGroupOffset);
        currGroupOffset = prevGroupOffset;
        currGroupSepType = prevGroupSepType;
        currGroupCount = prevGroupCount;
        prevGroupOffset = -1;
        prevGroupSepType = 0;
        prevGroupCount = 1;
    }

    // Validate final grouping sizes.
    bool prevValidSecondary = validateGroup(prevGroupSepType, prevGroupCount, false);
    bool currValidPrimary = validateGroup(currGroupSepType, currGroupCount, true);
    if (!requireGroupingMatch) {
        // The cases we need to handle here are lone digits.
        // Examples: "1,1"  "1,1,"  "1,1,1"  "1,1,1,"  ",1" (all parse as 1)
        // See more examples in numberformattestspecification.txt
        int32_t digitsToRemove = 0;
        if (!prevValidSecondary) {
            segment.setOffset(prevGroupOffset);
            digitsToRemove += prevGroupCount;
            digitsToRemove += currGroupCount;
        } else if (!currValidPrimary && (prevGroupSepType != 0 || prevGroupCount != 0)) {
            maybeMore = true;
            segment.setOffset(currGroupOffset);
            digitsToRemove += currGroupCount;
        }
        if (digitsToRemove != 0) {
            digitsConsumed.adjustMagnitude(-digitsToRemove);
            digitsConsumed.truncate();
        }
        prevValidSecondary = true;
        currValidPrimary = true;
    }
    if (currGroupSepType != 2 && (!prevValidSecondary || !currValidPrimary)) {
        // Grouping failure.
        digitsConsumed.bogus = true;
    }

    // Strings that start with a separator but have no digits,
    // or strings that failed a grouping size check.
    if (digitsConsumed.bogus) {
        maybeMore = maybeMore || (segment.length() == 0);
        segment.setOffset(initialOffset);
        return maybeMore;
    }

    // We passed all inspections. Start post-processing.

    // Adjust for fraction part.
    digitsConsumed.adjustMagnitude(-digitsAfterDecimalPlace);

    // Set the digits, either normal or exponent.
    if (exponentSign != 0 && segment.getOffset() != initialOffset) {
        bool overflow = false;
        if (digitsConsumed.fitsInLong()) {
            int64_t exponentLong = digitsConsumed.toLong(false);
            U_ASSERT(exponentLong >= 0);
            if (exponentLong <= INT32_MAX) {
                auto exponentInt = static_cast<int32_t>(exponentLong);
                if (result.quantity.adjustMagnitude(exponentSign * exponentInt)) {
                    overflow = true;
                }
            } else {
                overflow = true;
            }
        } else {
            overflow = true;
        }
        if (overflow) {
            if (exponentSign == -1) {
                // Set to zero
                result.quantity.clear();
            } else {
                // Set to infinity
                result.quantity.bogus = true;
                result.flags |= FLAG_INFINITY;
            }
        }
    } else {
        result.quantity = digitsConsumed;
    }

    // Set other information into the result and return.
    if (!actualDecimalString.isBogus()) {
        result.flags |= FLAG_HAS_DECIMAL_SEPARATOR;
    }
    result.setCharsConsumed(segment);
    return segment.length() == 0 || maybeMore;
}

bool DecimalMatcher::validateGroup(int32_t sepType, int32_t count, bool isPrimary) const {
    if (requireGroupingMatch) {
        if (sepType == -1) {
            // No such group (prevGroup before first shift).
            return true;
        } else if (sepType == 0) {
            // First group.
            if (isPrimary) {
                // No grouping separators is OK.
                return true;
            } else {
                return count != 0 && count <= grouping2;
            }
        } else if (sepType == 1) {
            // Middle group.
            if (isPrimary) {
                return count == grouping1;
            } else {
                return count == grouping2;
            }
        } else {
            U_ASSERT(sepType == 2);
            // After the decimal separator.
            return true;
        }
    } else {
        if (sepType == 1) {
            // #11230: don't accept middle groups with only 1 digit.
            return count != 1;
        } else {
            return true;
        }
    }
}

bool DecimalMatcher::smokeTest(const StringSegment& segment) const {
    // The common case uses a static leadSet for efficiency.
    if (fLocalDigitStrings.isNull() && leadSet != nullptr) {
        return segment.startsWith(*leadSet);
    }
    if (segment.startsWith(*separatorSet) || u_isdigit(segment.getCodePoint())) {
        return true;
    }
    if (fLocalDigitStrings.isNull()) {
        return false;
    }
    for (int32_t i = 0; i < 10; i++) {
        if (segment.startsWith(fLocalDigitStrings[i])) {
            return true;
        }
    }
    return false;
}

UnicodeString DecimalMatcher::toString() const {
    return u"<Decimal>";
}


#endif /* #if !UCONFIG_NO_FORMATTING */
