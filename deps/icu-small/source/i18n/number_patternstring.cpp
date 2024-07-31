// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT
#define UNISTR_FROM_CHAR_EXPLICIT

#include "uassert.h"
#include "number_patternstring.h"
#include "unicode/utf16.h"
#include "number_utils.h"
#include "number_roundingutils.h"
#include "number_mapper.h"

using namespace icu;
using namespace icu::number;
using namespace icu::number::impl;


void PatternParser::parseToPatternInfo(const UnicodeString& patternString, ParsedPatternInfo& patternInfo,
                                       UErrorCode& status) {
    patternInfo.consumePattern(patternString, status);
}

DecimalFormatProperties
PatternParser::parseToProperties(const UnicodeString& pattern, IgnoreRounding ignoreRounding,
                                 UErrorCode& status) {
    DecimalFormatProperties properties;
    parseToExistingPropertiesImpl(pattern, properties, ignoreRounding, status);
    return properties;
}

DecimalFormatProperties PatternParser::parseToProperties(const UnicodeString& pattern,
                                                         UErrorCode& status) {
    return parseToProperties(pattern, IGNORE_ROUNDING_NEVER, status);
}

void
PatternParser::parseToExistingProperties(const UnicodeString& pattern, DecimalFormatProperties& properties,
                                         IgnoreRounding ignoreRounding, UErrorCode& status) {
    parseToExistingPropertiesImpl(pattern, properties, ignoreRounding, status);
}


char16_t ParsedPatternInfo::charAt(int32_t flags, int32_t index) const {
    const Endpoints& endpoints = getEndpoints(flags);
    if (index < 0 || index >= endpoints.end - endpoints.start) {
        UPRV_UNREACHABLE_EXIT;
    }
    return pattern.charAt(endpoints.start + index);
}

int32_t ParsedPatternInfo::length(int32_t flags) const {
    return getLengthFromEndpoints(getEndpoints(flags));
}

int32_t ParsedPatternInfo::getLengthFromEndpoints(const Endpoints& endpoints) {
    return endpoints.end - endpoints.start;
}

UnicodeString ParsedPatternInfo::getString(int32_t flags) const {
    const Endpoints& endpoints = getEndpoints(flags);
    if (endpoints.start == endpoints.end) {
        return {};
    }
    // Create a new UnicodeString
    return UnicodeString(pattern, endpoints.start, endpoints.end - endpoints.start);
}

const Endpoints& ParsedPatternInfo::getEndpoints(int32_t flags) const {
    bool prefix = (flags & AFFIX_PREFIX) != 0;
    bool isNegative = (flags & AFFIX_NEGATIVE_SUBPATTERN) != 0;
    bool padding = (flags & AFFIX_PADDING) != 0;
    if (isNegative && padding) {
        return negative.paddingEndpoints;
    } else if (padding) {
        return positive.paddingEndpoints;
    } else if (prefix && isNegative) {
        return negative.prefixEndpoints;
    } else if (prefix) {
        return positive.prefixEndpoints;
    } else if (isNegative) {
        return negative.suffixEndpoints;
    } else {
        return positive.suffixEndpoints;
    }
}

bool ParsedPatternInfo::positiveHasPlusSign() const {
    return positive.hasPlusSign;
}

bool ParsedPatternInfo::hasNegativeSubpattern() const {
    return fHasNegativeSubpattern;
}

bool ParsedPatternInfo::negativeHasMinusSign() const {
    return negative.hasMinusSign;
}

bool ParsedPatternInfo::hasCurrencySign() const {
    return positive.hasCurrencySign || (fHasNegativeSubpattern && negative.hasCurrencySign);
}

bool ParsedPatternInfo::containsSymbolType(AffixPatternType type, UErrorCode& status) const {
    return AffixUtils::containsType(pattern, type, status);
}

bool ParsedPatternInfo::hasBody() const {
    return positive.integerTotal > 0;
}

bool ParsedPatternInfo::currencyAsDecimal() const {
    return positive.hasCurrencyDecimal;
}

/////////////////////////////////////////////////////
/// BEGIN RECURSIVE DESCENT PARSER IMPLEMENTATION ///
/////////////////////////////////////////////////////

UChar32 ParsedPatternInfo::ParserState::peek() {
    if (offset == pattern.length()) {
        return -1;
    } else {
        return pattern.char32At(offset);
    }
}

UChar32 ParsedPatternInfo::ParserState::peek2() {
    if (offset == pattern.length()) {
        return -1;
    }
    int32_t cp1 = pattern.char32At(offset);
    int32_t offset2 = offset + U16_LENGTH(cp1);
    if (offset2 == pattern.length()) {
        return -1;
    }
    return pattern.char32At(offset2);
}

UChar32 ParsedPatternInfo::ParserState::next() {
    int32_t codePoint = peek();
    offset += U16_LENGTH(codePoint);
    return codePoint;
}

void ParsedPatternInfo::consumePattern(const UnicodeString& patternString, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    this->pattern = patternString;

    // This class is not intended for writing twice!
    // Use move assignment to overwrite instead.
    U_ASSERT(state.offset == 0);

    // pattern := subpattern (';' subpattern)?
    currentSubpattern = &positive;
    consumeSubpattern(status);
    if (U_FAILURE(status)) { return; }
    if (state.peek() == u';') {
        state.next(); // consume the ';'
        // Don't consume the negative subpattern if it is empty (trailing ';')
        if (state.peek() != -1) {
            fHasNegativeSubpattern = true;
            currentSubpattern = &negative;
            consumeSubpattern(status);
            if (U_FAILURE(status)) { return; }
        }
    }
    if (state.peek() != -1) {
        state.toParseException(u"Found unquoted special character");
        status = U_UNQUOTED_SPECIAL;
    }
}

void ParsedPatternInfo::consumeSubpattern(UErrorCode& status) {
    // subpattern := literals? number exponent? literals?
    consumePadding(PadPosition::UNUM_PAD_BEFORE_PREFIX, status);
    if (U_FAILURE(status)) { return; }
    consumeAffix(currentSubpattern->prefixEndpoints, status);
    if (U_FAILURE(status)) { return; }
    consumePadding(PadPosition::UNUM_PAD_AFTER_PREFIX, status);
    if (U_FAILURE(status)) { return; }
    consumeFormat(status);
    if (U_FAILURE(status)) { return; }
    consumeExponent(status);
    if (U_FAILURE(status)) { return; }
    consumePadding(PadPosition::UNUM_PAD_BEFORE_SUFFIX, status);
    if (U_FAILURE(status)) { return; }
    consumeAffix(currentSubpattern->suffixEndpoints, status);
    if (U_FAILURE(status)) { return; }
    consumePadding(PadPosition::UNUM_PAD_AFTER_SUFFIX, status);
    if (U_FAILURE(status)) { return; }
}

void ParsedPatternInfo::consumePadding(PadPosition paddingLocation, UErrorCode& status) {
    if (state.peek() != u'*') {
        return;
    }
    if (currentSubpattern->hasPadding) {
        state.toParseException(u"Cannot have multiple pad specifiers");
        status = U_MULTIPLE_PAD_SPECIFIERS;
        return;
    }
    currentSubpattern->paddingLocation = paddingLocation;
    currentSubpattern->hasPadding = true;
    state.next(); // consume the '*'
    currentSubpattern->paddingEndpoints.start = state.offset;
    consumeLiteral(status);
    currentSubpattern->paddingEndpoints.end = state.offset;
}

void ParsedPatternInfo::consumeAffix(Endpoints& endpoints, UErrorCode& status) {
    // literals := { literal }
    endpoints.start = state.offset;
    while (true) {
        switch (state.peek()) {
            case u'#':
            case u'@':
            case u';':
            case u'*':
            case u'.':
            case u',':
            case u'0':
            case u'1':
            case u'2':
            case u'3':
            case u'4':
            case u'5':
            case u'6':
            case u'7':
            case u'8':
            case u'9':
            case -1:
                // Characters that cannot appear unquoted in a literal
                // break outer;
                goto after_outer;

            case u'%':
                currentSubpattern->hasPercentSign = true;
                break;

            case u'‰':
                currentSubpattern->hasPerMilleSign = true;
                break;

            case u'¤':
                currentSubpattern->hasCurrencySign = true;
                break;

            case u'-':
                currentSubpattern->hasMinusSign = true;
                break;

            case u'+':
                currentSubpattern->hasPlusSign = true;
                break;

            default:
                break;
        }
        consumeLiteral(status);
        if (U_FAILURE(status)) { return; }
    }
    after_outer:
    endpoints.end = state.offset;
}

void ParsedPatternInfo::consumeLiteral(UErrorCode& status) {
    if (state.peek() == -1) {
        state.toParseException(u"Expected unquoted literal but found EOL");
        status = U_PATTERN_SYNTAX_ERROR;
        return;
    } else if (state.peek() == u'\'') {
        state.next(); // consume the starting quote
        while (state.peek() != u'\'') {
            if (state.peek() == -1) {
                state.toParseException(u"Expected quoted literal but found EOL");
                status = U_PATTERN_SYNTAX_ERROR;
                return;
            } else {
                state.next(); // consume a quoted character
            }
        }
        state.next(); // consume the ending quote
    } else {
        // consume a non-quoted literal character
        state.next();
    }
}

void ParsedPatternInfo::consumeFormat(UErrorCode& status) {
    consumeIntegerFormat(status);
    if (U_FAILURE(status)) { return; }
    if (state.peek() == u'.') {
        state.next(); // consume the decimal point
        currentSubpattern->hasDecimal = true;
        currentSubpattern->widthExceptAffixes += 1;
        consumeFractionFormat(status);
        if (U_FAILURE(status)) { return; }
    } else if (state.peek() == u'¤') {
        // Check if currency is a decimal separator
        switch (state.peek2()) {
            case u'#':
            case u'0':
            case u'1':
            case u'2':
            case u'3':
            case u'4':
            case u'5':
            case u'6':
            case u'7':
            case u'8':
            case u'9':
                break;
            default:
                // Currency symbol followed by a non-numeric character;
                // treat as a normal affix.
                return;
        }
        // Currency symbol is followed by a numeric character;
        // treat as a decimal separator.
        currentSubpattern->hasCurrencySign = true;
        currentSubpattern->hasCurrencyDecimal = true;
        currentSubpattern->hasDecimal = true;
        currentSubpattern->widthExceptAffixes += 1;
        state.next(); // consume the symbol
        consumeFractionFormat(status);
        if (U_FAILURE(status)) { return; }
    }
}

void ParsedPatternInfo::consumeIntegerFormat(UErrorCode& status) {
    // Convenience reference:
    ParsedSubpatternInfo& result = *currentSubpattern;

    while (true) {
        switch (state.peek()) {
            case u',':
                result.widthExceptAffixes += 1;
                result.groupingSizes <<= 16;
                break;

            case u'#':
                if (result.integerNumerals > 0) {
                    state.toParseException(u"# cannot follow 0 before decimal point");
                    status = U_UNEXPECTED_TOKEN;
                    return;
                }
                result.widthExceptAffixes += 1;
                result.groupingSizes += 1;
                if (result.integerAtSigns > 0) {
                    result.integerTrailingHashSigns += 1;
                } else {
                    result.integerLeadingHashSigns += 1;
                }
                result.integerTotal += 1;
                break;

            case u'@':
                if (result.integerNumerals > 0) {
                    state.toParseException(u"Cannot mix 0 and @");
                    status = U_UNEXPECTED_TOKEN;
                    return;
                }
                if (result.integerTrailingHashSigns > 0) {
                    state.toParseException(u"Cannot nest # inside of a run of @");
                    status = U_UNEXPECTED_TOKEN;
                    return;
                }
                result.widthExceptAffixes += 1;
                result.groupingSizes += 1;
                result.integerAtSigns += 1;
                result.integerTotal += 1;
                break;

            case u'0':
            case u'1':
            case u'2':
            case u'3':
            case u'4':
            case u'5':
            case u'6':
            case u'7':
            case u'8':
            case u'9':
                if (result.integerAtSigns > 0) {
                    state.toParseException(u"Cannot mix @ and 0");
                    status = U_UNEXPECTED_TOKEN;
                    return;
                }
                result.widthExceptAffixes += 1;
                result.groupingSizes += 1;
                result.integerNumerals += 1;
                result.integerTotal += 1;
                if (!result.rounding.isZeroish() || state.peek() != u'0') {
                    result.rounding.appendDigit(static_cast<int8_t>(state.peek() - u'0'), 0, true);
                }
                break;

            default:
                goto after_outer;
        }
        state.next(); // consume the symbol
    }

    after_outer:
    // Disallow patterns with a trailing ',' or with two ',' next to each other
    auto grouping1 = static_cast<int16_t> (result.groupingSizes & 0xffff);
    auto grouping2 = static_cast<int16_t> ((result.groupingSizes >> 16) & 0xffff);
    auto grouping3 = static_cast<int16_t> ((result.groupingSizes >> 32) & 0xffff);
    if (grouping1 == 0 && grouping2 != -1) {
        state.toParseException(u"Trailing grouping separator is invalid");
        status = U_UNEXPECTED_TOKEN;
        return;
    }
    if (grouping2 == 0 && grouping3 != -1) {
        state.toParseException(u"Grouping width of zero is invalid");
        status = U_PATTERN_SYNTAX_ERROR;
        return;
    }
}

void ParsedPatternInfo::consumeFractionFormat(UErrorCode& status) {
    // Convenience reference:
    ParsedSubpatternInfo& result = *currentSubpattern;

    int32_t zeroCounter = 0;
    while (true) {
        switch (state.peek()) {
            case u'#':
                result.widthExceptAffixes += 1;
                result.fractionHashSigns += 1;
                result.fractionTotal += 1;
                zeroCounter++;
                break;

            case u'0':
            case u'1':
            case u'2':
            case u'3':
            case u'4':
            case u'5':
            case u'6':
            case u'7':
            case u'8':
            case u'9':
                if (result.fractionHashSigns > 0) {
                    state.toParseException(u"0 cannot follow # after decimal point");
                    status = U_UNEXPECTED_TOKEN;
                    return;
                }
                result.widthExceptAffixes += 1;
                result.fractionNumerals += 1;
                result.fractionTotal += 1;
                if (state.peek() == u'0') {
                    zeroCounter++;
                } else {
                    result.rounding
                            .appendDigit(static_cast<int8_t>(state.peek() - u'0'), zeroCounter, false);
                    zeroCounter = 0;
                }
                break;

            default:
                return;
        }
        state.next(); // consume the symbol
    }
}

void ParsedPatternInfo::consumeExponent(UErrorCode& status) {
    // Convenience reference:
    ParsedSubpatternInfo& result = *currentSubpattern;

    if (state.peek() != u'E') {
        return;
    }
    if ((result.groupingSizes & 0xffff0000L) != 0xffff0000L) {
        state.toParseException(u"Cannot have grouping separator in scientific notation");
        status = U_MALFORMED_EXPONENTIAL_PATTERN;
        return;
    }
    state.next(); // consume the E
    result.widthExceptAffixes++;
    if (state.peek() == u'+') {
        state.next(); // consume the +
        result.exponentHasPlusSign = true;
        result.widthExceptAffixes++;
    }
    while (state.peek() == u'0') {
        state.next(); // consume the 0
        result.exponentZeros += 1;
        result.widthExceptAffixes++;
    }
}

///////////////////////////////////////////////////
/// END RECURSIVE DESCENT PARSER IMPLEMENTATION ///
///////////////////////////////////////////////////

void PatternParser::parseToExistingPropertiesImpl(const UnicodeString& pattern,
                                                  DecimalFormatProperties& properties,
                                                  IgnoreRounding ignoreRounding, UErrorCode& status) {
    if (pattern.length() == 0) {
        // Backwards compatibility requires that we reset to the default values.
        // TODO: Only overwrite the properties that "saveToProperties" normally touches?
        properties.clear();
        return;
    }

    ParsedPatternInfo patternInfo;
    parseToPatternInfo(pattern, patternInfo, status);
    if (U_FAILURE(status)) { return; }
    patternInfoToProperties(properties, patternInfo, ignoreRounding, status);
}

void
PatternParser::patternInfoToProperties(DecimalFormatProperties& properties, ParsedPatternInfo& patternInfo,
                                       IgnoreRounding _ignoreRounding, UErrorCode& status) {
    // Translate from PatternParseResult to Properties.
    // Note that most data from "negative" is ignored per the specification of DecimalFormat.

    const ParsedSubpatternInfo& positive = patternInfo.positive;

    bool ignoreRounding;
    if (_ignoreRounding == IGNORE_ROUNDING_NEVER) {
        ignoreRounding = false;
    } else if (_ignoreRounding == IGNORE_ROUNDING_IF_CURRENCY) {
        ignoreRounding = positive.hasCurrencySign;
    } else {
        U_ASSERT(_ignoreRounding == IGNORE_ROUNDING_ALWAYS);
        ignoreRounding = true;
    }

    // Grouping settings
    auto grouping1 = static_cast<int16_t> (positive.groupingSizes & 0xffff);
    auto grouping2 = static_cast<int16_t> ((positive.groupingSizes >> 16) & 0xffff);
    auto grouping3 = static_cast<int16_t> ((positive.groupingSizes >> 32) & 0xffff);
    if (grouping2 != -1) {
        properties.groupingSize = grouping1;
        properties.groupingUsed = true;
    } else {
        properties.groupingSize = -1;
        properties.groupingUsed = false;
    }
    if (grouping3 != -1) {
        properties.secondaryGroupingSize = grouping2;
    } else {
        properties.secondaryGroupingSize = -1;
    }

    // For backwards compatibility, require that the pattern emit at least one min digit.
    int minInt, minFrac;
    if (positive.integerTotal == 0 && positive.fractionTotal > 0) {
        // patterns like ".##"
        minInt = 0;
        minFrac = uprv_max(1, positive.fractionNumerals);
    } else if (positive.integerNumerals == 0 && positive.fractionNumerals == 0) {
        // patterns like "#.##"
        minInt = 1;
        minFrac = 0;
    } else {
        minInt = positive.integerNumerals;
        minFrac = positive.fractionNumerals;
    }

    // Rounding settings
    // Don't set basic rounding when there is a currency sign; defer to CurrencyUsage
    if (positive.integerAtSigns > 0) {
        properties.minimumFractionDigits = -1;
        properties.maximumFractionDigits = -1;
        properties.roundingIncrement = 0.0;
        properties.minimumSignificantDigits = positive.integerAtSigns;
        properties.maximumSignificantDigits = positive.integerAtSigns + positive.integerTrailingHashSigns;
    } else if (!positive.rounding.isZeroish()) {
        if (!ignoreRounding) {
            properties.minimumFractionDigits = minFrac;
            properties.maximumFractionDigits = positive.fractionTotal;
            properties.roundingIncrement = positive.rounding.toDouble();
        } else {
            properties.minimumFractionDigits = -1;
            properties.maximumFractionDigits = -1;
            properties.roundingIncrement = 0.0;
        }
        properties.minimumSignificantDigits = -1;
        properties.maximumSignificantDigits = -1;
    } else {
        if (!ignoreRounding) {
            properties.minimumFractionDigits = minFrac;
            properties.maximumFractionDigits = positive.fractionTotal;
            properties.roundingIncrement = 0.0;
        } else {
            properties.minimumFractionDigits = -1;
            properties.maximumFractionDigits = -1;
            properties.roundingIncrement = 0.0;
        }
        properties.minimumSignificantDigits = -1;
        properties.maximumSignificantDigits = -1;
    }

    // If the pattern ends with a '.' then force the decimal point.
    if (positive.hasDecimal && positive.fractionTotal == 0) {
        properties.decimalSeparatorAlwaysShown = true;
    } else {
        properties.decimalSeparatorAlwaysShown = false;
    }

    // Persist the currency as decimal separator
    properties.currencyAsDecimal = positive.hasCurrencyDecimal;

    // Scientific notation settings
    if (positive.exponentZeros > 0) {
        properties.exponentSignAlwaysShown = positive.exponentHasPlusSign;
        properties.minimumExponentDigits = positive.exponentZeros;
        if (positive.integerAtSigns == 0) {
            // patterns without '@' can define max integer digits, used for engineering notation
            properties.minimumIntegerDigits = positive.integerNumerals;
            properties.maximumIntegerDigits = positive.integerTotal;
        } else {
            // patterns with '@' cannot define max integer digits
            properties.minimumIntegerDigits = 1;
            properties.maximumIntegerDigits = -1;
        }
    } else {
        properties.exponentSignAlwaysShown = false;
        properties.minimumExponentDigits = -1;
        properties.minimumIntegerDigits = minInt;
        properties.maximumIntegerDigits = -1;
    }

    // Compute the affix patterns (required for both padding and affixes)
    UnicodeString posPrefix = patternInfo.getString(AffixPatternProvider::AFFIX_PREFIX);
    UnicodeString posSuffix = patternInfo.getString(0);

    // Padding settings
    if (positive.hasPadding) {
        // The width of the positive prefix and suffix templates are included in the padding
        int paddingWidth = positive.widthExceptAffixes +
                           AffixUtils::estimateLength(posPrefix, status) +
                           AffixUtils::estimateLength(posSuffix, status);
        properties.formatWidth = paddingWidth;
        UnicodeString rawPaddingString = patternInfo.getString(AffixPatternProvider::AFFIX_PADDING);
        if (rawPaddingString.length() == 1) {
            properties.padString = rawPaddingString;
        } else if (rawPaddingString.length() == 2) {
            if (rawPaddingString.charAt(0) == u'\'') {
                properties.padString.setTo(u"'", -1);
            } else {
                properties.padString = rawPaddingString;
            }
        } else {
            properties.padString = UnicodeString(rawPaddingString, 1, rawPaddingString.length() - 2);
        }
        properties.padPosition = positive.paddingLocation;
    } else {
        properties.formatWidth = -1;
        properties.padString.setToBogus();
        properties.padPosition.nullify();
    }

    // Set the affixes
    // Always call the setter, even if the prefixes are empty, especially in the case of the
    // negative prefix pattern, to prevent default values from overriding the pattern.
    properties.positivePrefixPattern = posPrefix;
    properties.positiveSuffixPattern = posSuffix;
    if (patternInfo.fHasNegativeSubpattern) {
        properties.negativePrefixPattern = patternInfo.getString(
                AffixPatternProvider::AFFIX_NEGATIVE_SUBPATTERN | AffixPatternProvider::AFFIX_PREFIX);
        properties.negativeSuffixPattern = patternInfo.getString(
                AffixPatternProvider::AFFIX_NEGATIVE_SUBPATTERN);
    } else {
        properties.negativePrefixPattern.setToBogus();
        properties.negativeSuffixPattern.setToBogus();
    }

    // Set the magnitude multiplier
    if (positive.hasPercentSign) {
        properties.magnitudeMultiplier = 2;
    } else if (positive.hasPerMilleSign) {
        properties.magnitudeMultiplier = 3;
    } else {
        properties.magnitudeMultiplier = 0;
    }
}

///////////////////////////////////////////////////////////////////
/// End PatternStringParser.java; begin PatternStringUtils.java ///
///////////////////////////////////////////////////////////////////

// Determine whether a given roundingIncrement should be ignored for formatting
// based on the current maxFrac value (maximum fraction digits). For example a
// roundingIncrement of 0.01 should be ignored if maxFrac is 1, but not if maxFrac
// is 2 or more. Note that roundingIncrements are rounded in significance, so
// a roundingIncrement of 0.006 is treated like 0.01 for this determination, i.e.
// it should not be ignored if maxFrac is 2 or more (but a roundingIncrement of
// 0.005 is treated like 0.001 for significance). This is the reason for the
// initial doubling below.
// roundIncr must be non-zero.
bool PatternStringUtils::ignoreRoundingIncrement(double roundIncr, int32_t maxFrac) {
    if (maxFrac < 0) {
        return false;
    }
    int32_t frac = 0;
    roundIncr *= 2.0;
    for (frac = 0; frac <= maxFrac && roundIncr <= 1.0; frac++, roundIncr *= 10.0);
    return (frac > maxFrac);
}

UnicodeString PatternStringUtils::propertiesToPatternString(const DecimalFormatProperties& properties,
                                                            UErrorCode& status) {
    UnicodeString sb;

    // Convenience references
    // The uprv_min() calls prevent DoS
    int32_t dosMax = 100;
    int32_t grouping1 = uprv_max(0, uprv_min(properties.groupingSize, dosMax));
    int32_t grouping2 = uprv_max(0, uprv_min(properties.secondaryGroupingSize, dosMax));
    bool useGrouping = properties.groupingUsed;
    int32_t paddingWidth = uprv_min(properties.formatWidth, dosMax);
    NullableValue<PadPosition> paddingLocation = properties.padPosition;
    UnicodeString paddingString = properties.padString;
    int32_t minInt = uprv_max(0, uprv_min(properties.minimumIntegerDigits, dosMax));
    int32_t maxInt = uprv_min(properties.maximumIntegerDigits, dosMax);
    int32_t minFrac = uprv_max(0, uprv_min(properties.minimumFractionDigits, dosMax));
    int32_t maxFrac = uprv_min(properties.maximumFractionDigits, dosMax);
    int32_t minSig = uprv_min(properties.minimumSignificantDigits, dosMax);
    int32_t maxSig = uprv_min(properties.maximumSignificantDigits, dosMax);
    bool alwaysShowDecimal = properties.decimalSeparatorAlwaysShown;
    int32_t exponentDigits = uprv_min(properties.minimumExponentDigits, dosMax);
    bool exponentShowPlusSign = properties.exponentSignAlwaysShown;

    AutoAffixPatternProvider affixProvider(properties, status);

    // Prefixes
    sb.append(affixProvider.get().getString(AffixPatternProvider::AFFIX_POS_PREFIX));
    int32_t afterPrefixPos = sb.length();

    // Figure out the grouping sizes.
    if (!useGrouping) {
        grouping1 = 0;
        grouping2 = 0;
    } else if (grouping1 == grouping2) {
        grouping1 = 0;
    }
    int32_t groupingLength = grouping1 + grouping2 + 1;

    // Figure out the digits we need to put in the pattern.
    double increment = properties.roundingIncrement;
    UnicodeString digitsString;
    int32_t digitsStringScale = 0;
    if (maxSig != uprv_min(dosMax, -1)) {
        // Significant Digits.
        while (digitsString.length() < minSig) {
            digitsString.append(u'@');
        }
        while (digitsString.length() < maxSig) {
            digitsString.append(u'#');
        }
    } else if (increment != 0.0 && !ignoreRoundingIncrement(increment,maxFrac)) {
        // Rounding Increment.
        DecimalQuantity incrementQuantity;
        incrementQuantity.setToDouble(increment);
        incrementQuantity.roundToInfinity();
        digitsStringScale = incrementQuantity.getLowerDisplayMagnitude();
        incrementQuantity.adjustMagnitude(-digitsStringScale);
        incrementQuantity.increaseMinIntegerTo(minInt - digitsStringScale);
        UnicodeString str = incrementQuantity.toPlainString();
        if (str.charAt(0) == u'-') {
            // TODO: Unsupported operation exception or fail silently?
            digitsString.append(str, 1, str.length() - 1);
        } else {
            digitsString.append(str);
        }
    }
    while (digitsString.length() + digitsStringScale < minInt) {
        digitsString.insert(0, u'0');
    }
    while (-digitsStringScale < minFrac) {
        digitsString.append(u'0');
        digitsStringScale--;
    }

    // Write the digits to the string builder
    int32_t m0 = uprv_max(groupingLength, digitsString.length() + digitsStringScale);
    m0 = (maxInt != dosMax) ? uprv_max(maxInt, m0) - 1 : m0 - 1;
    int32_t mN = (maxFrac != dosMax) ? uprv_min(-maxFrac, digitsStringScale) : digitsStringScale;
    for (int32_t magnitude = m0; magnitude >= mN; magnitude--) {
        int32_t di = digitsString.length() + digitsStringScale - magnitude - 1;
        if (di < 0 || di >= digitsString.length()) {
            sb.append(u'#');
        } else {
            sb.append(digitsString.charAt(di));
        }
        // Decimal separator
        if (magnitude == 0 && (alwaysShowDecimal || mN < 0)) {
            if (properties.currencyAsDecimal) {
                sb.append(u'¤');
            } else {
                sb.append(u'.');
            }
        }
        if (!useGrouping) {
            continue;
        }
        // Least-significant grouping separator
        if (magnitude > 0 && magnitude == grouping1) {
            sb.append(u',');
        }
        // All other grouping separators
        if (magnitude > grouping1 && grouping2 > 0 && (magnitude - grouping1) % grouping2 == 0) {
            sb.append(u',');
        }
    }

    // Exponential notation
    if (exponentDigits != uprv_min(dosMax, -1)) {
        sb.append(u'E');
        if (exponentShowPlusSign) {
            sb.append(u'+');
        }
        for (int32_t i = 0; i < exponentDigits; i++) {
            sb.append(u'0');
        }
    }

    // Suffixes
    int32_t beforeSuffixPos = sb.length();
    sb.append(affixProvider.get().getString(AffixPatternProvider::AFFIX_POS_SUFFIX));

    // Resolve Padding
    if (paddingWidth > 0 && !paddingLocation.isNull()) {
        while (paddingWidth - sb.length() > 0) {
            sb.insert(afterPrefixPos, u'#');
            beforeSuffixPos++;
        }
        int32_t addedLength;
        switch (paddingLocation.get(status)) {
            case PadPosition::UNUM_PAD_BEFORE_PREFIX:
                addedLength = escapePaddingString(paddingString, sb, 0, status);
                sb.insert(0, u'*');
                afterPrefixPos += addedLength + 1;
                beforeSuffixPos += addedLength + 1;
                break;
            case PadPosition::UNUM_PAD_AFTER_PREFIX:
                addedLength = escapePaddingString(paddingString, sb, afterPrefixPos, status);
                sb.insert(afterPrefixPos, u'*');
                afterPrefixPos += addedLength + 1;
                beforeSuffixPos += addedLength + 1;
                break;
            case PadPosition::UNUM_PAD_BEFORE_SUFFIX:
                escapePaddingString(paddingString, sb, beforeSuffixPos, status);
                sb.insert(beforeSuffixPos, u'*');
                break;
            case PadPosition::UNUM_PAD_AFTER_SUFFIX:
                sb.append(u'*');
                escapePaddingString(paddingString, sb, sb.length(), status);
                break;
        }
        if (U_FAILURE(status)) { return sb; }
    }

    // Negative affixes
    // Ignore if the negative prefix pattern is "-" and the negative suffix is empty
    if (affixProvider.get().hasNegativeSubpattern()) {
        sb.append(u';');
        sb.append(affixProvider.get().getString(AffixPatternProvider::AFFIX_NEG_PREFIX));
        // Copy the positive digit format into the negative.
        // This is optional; the pattern is the same as if '#' were appended here instead.
        // NOTE: It is not safe to append the UnicodeString to itself, so we need to copy.
        // See https://unicode-org.atlassian.net/browse/ICU-13707
        UnicodeString copy(sb);
        sb.append(copy, afterPrefixPos, beforeSuffixPos - afterPrefixPos);
        sb.append(affixProvider.get().getString(AffixPatternProvider::AFFIX_NEG_SUFFIX));
    }

    return sb;
}

int PatternStringUtils::escapePaddingString(UnicodeString input, UnicodeString& output, int startIndex,
                                            UErrorCode& status) {
    (void) status;
    if (input.length() == 0) {
        input.setTo(kFallbackPaddingString, -1);
    }
    int startLength = output.length();
    if (input.length() == 1) {
        if (input.compare(u"'", -1) == 0) {
            output.insert(startIndex, u"''", -1);
        } else {
            output.insert(startIndex, input);
        }
    } else {
        output.insert(startIndex, u'\'');
        int offset = 1;
        for (int i = 0; i < input.length(); i++) {
            // it's okay to deal in chars here because the quote mark is the only interesting thing.
            char16_t ch = input.charAt(i);
            if (ch == u'\'') {
                output.insert(startIndex + offset, u"''", -1);
                offset += 2;
            } else {
                output.insert(startIndex + offset, ch);
                offset += 1;
            }
        }
        output.insert(startIndex + offset, u'\'');
    }
    return output.length() - startLength;
}

UnicodeString
PatternStringUtils::convertLocalized(const UnicodeString& input, const DecimalFormatSymbols& symbols,
                                     bool toLocalized, UErrorCode& status) {
    // Construct a table of strings to be converted between localized and standard.
    static constexpr int32_t LEN = 21;
    UnicodeString table[LEN][2];
    int standIdx = toLocalized ? 0 : 1;
    int localIdx = toLocalized ? 1 : 0;
    // TODO: Add approximately sign here?
    table[0][standIdx] = u"%";
    table[0][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kPercentSymbol);
    table[1][standIdx] = u"‰";
    table[1][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kPerMillSymbol);
    table[2][standIdx] = u".";
    table[2][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kDecimalSeparatorSymbol);
    table[3][standIdx] = u",";
    table[3][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kGroupingSeparatorSymbol);
    table[4][standIdx] = u"-";
    table[4][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kMinusSignSymbol);
    table[5][standIdx] = u"+";
    table[5][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kPlusSignSymbol);
    table[6][standIdx] = u";";
    table[6][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kPatternSeparatorSymbol);
    table[7][standIdx] = u"@";
    table[7][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kSignificantDigitSymbol);
    table[8][standIdx] = u"E";
    table[8][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kExponentialSymbol);
    table[9][standIdx] = u"*";
    table[9][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kPadEscapeSymbol);
    table[10][standIdx] = u"#";
    table[10][localIdx] = symbols.getConstSymbol(DecimalFormatSymbols::kDigitSymbol);
    for (int i = 0; i < 10; i++) {
        table[11 + i][standIdx] = u'0' + i;
        table[11 + i][localIdx] = symbols.getConstDigitSymbol(i);
    }

    // Special case: quotes are NOT allowed to be in any localIdx strings.
    // Substitute them with '’' instead.
    for (int32_t i = 0; i < LEN; i++) {
        table[i][localIdx].findAndReplace(u'\'', u'’');
    }

    // Iterate through the string and convert.
    // State table:
    // 0 => base state
    // 1 => first char inside a quoted sequence in input and output string
    // 2 => inside a quoted sequence in input and output string
    // 3 => first char after a close quote in input string;
    // close quote still needs to be written to output string
    // 4 => base state in input string; inside quoted sequence in output string
    // 5 => first char inside a quoted sequence in input string;
    // inside quoted sequence in output string
    UnicodeString result;
    int state = 0;
    for (int offset = 0; offset < input.length(); offset++) {
        char16_t ch = input.charAt(offset);

        // Handle a quote character (state shift)
        if (ch == u'\'') {
            if (state == 0) {
                result.append(u'\'');
                state = 1;
                continue;
            } else if (state == 1) {
                result.append(u'\'');
                state = 0;
                continue;
            } else if (state == 2) {
                state = 3;
                continue;
            } else if (state == 3) {
                result.append(u'\'');
                result.append(u'\'');
                state = 1;
                continue;
            } else if (state == 4) {
                state = 5;
                continue;
            } else {
                U_ASSERT(state == 5);
                result.append(u'\'');
                result.append(u'\'');
                state = 4;
                continue;
            }
        }

        if (state == 0 || state == 3 || state == 4) {
            for (auto& pair : table) {
                // Perform a greedy match on this symbol string
                UnicodeString temp = input.tempSubString(offset, pair[0].length());
                if (temp == pair[0]) {
                    // Skip ahead past this region for the next iteration
                    offset += pair[0].length() - 1;
                    if (state == 3 || state == 4) {
                        result.append(u'\'');
                        state = 0;
                    }
                    result.append(pair[1]);
                    goto continue_outer;
                }
            }
            // No replacement found. Check if a special quote is necessary
            for (auto& pair : table) {
                UnicodeString temp = input.tempSubString(offset, pair[1].length());
                if (temp == pair[1]) {
                    if (state == 0) {
                        result.append(u'\'');
                        state = 4;
                    }
                    result.append(ch);
                    goto continue_outer;
                }
            }
            // Still nothing. Copy the char verbatim. (Add a close quote if necessary)
            if (state == 3 || state == 4) {
                result.append(u'\'');
                state = 0;
            }
            result.append(ch);
        } else {
            U_ASSERT(state == 1 || state == 2 || state == 5);
            result.append(ch);
            state = 2;
        }
        continue_outer:;
    }
    // Resolve final quotes
    if (state == 3 || state == 4) {
        result.append(u'\'');
        state = 0;
    }
    if (state != 0) {
        // Malformed localized pattern: unterminated quote
        status = U_PATTERN_SYNTAX_ERROR;
    }
    return result;
}

void PatternStringUtils::patternInfoToStringBuilder(const AffixPatternProvider& patternInfo, bool isPrefix,
                                                    PatternSignType patternSignType,
                                                    bool approximately,
                                                    StandardPlural::Form plural,
                                                    bool perMilleReplacesPercent,
                                                    bool dropCurrencySymbols,
                                                    UnicodeString& output) {

    // Should the output render '+' where '-' would normally appear in the pattern?
    bool plusReplacesMinusSign = (patternSignType == PATTERN_SIGN_TYPE_POS_SIGN)
        && !patternInfo.positiveHasPlusSign();

    // Should we use the affix from the negative subpattern?
    // (If not, we will use the positive subpattern.)
    bool useNegativeAffixPattern = patternInfo.hasNegativeSubpattern()
        && (patternSignType == PATTERN_SIGN_TYPE_NEG
            || (patternInfo.negativeHasMinusSign() && (plusReplacesMinusSign || approximately)));

    // Resolve the flags for the affix pattern.
    int flags = 0;
    if (useNegativeAffixPattern) {
        flags |= AffixPatternProvider::AFFIX_NEGATIVE_SUBPATTERN;
    }
    if (isPrefix) {
        flags |= AffixPatternProvider::AFFIX_PREFIX;
    }
    if (plural != StandardPlural::Form::COUNT) {
        U_ASSERT(plural == (AffixPatternProvider::AFFIX_PLURAL_MASK & plural));
        flags |= plural;
    }

    // Should we prepend a sign to the pattern?
    bool prependSign;
    if (!isPrefix || useNegativeAffixPattern) {
        prependSign = false;
    } else if (patternSignType == PATTERN_SIGN_TYPE_NEG) {
        prependSign = true;
    } else {
        prependSign = plusReplacesMinusSign || approximately;
    }

    // What symbols should take the place of the sign placeholder?
    const char16_t* signSymbols = u"-";
    if (approximately) {
        if (plusReplacesMinusSign) {
            signSymbols = u"~+";
        } else if (patternSignType == PATTERN_SIGN_TYPE_NEG) {
            signSymbols = u"~-";
        } else {
            signSymbols = u"~";
        }
    } else if (plusReplacesMinusSign) {
        signSymbols = u"+";
    }

    // Compute the number of tokens in the affix pattern (signSymbols is considered one token).
    int length = patternInfo.length(flags) + (prependSign ? 1 : 0);

    // Finally, set the result into the StringBuilder.
    output.remove();
    for (int index = 0; index < length; index++) {
        char16_t candidate;
        if (prependSign && index == 0) {
            candidate = u'-';
        } else if (prependSign) {
            candidate = patternInfo.charAt(flags, index - 1);
        } else {
            candidate = patternInfo.charAt(flags, index);
        }
        if (candidate == u'-') {
            if (u_strlen(signSymbols) == 1) {
                candidate = signSymbols[0];
            } else {
                output.append(signSymbols[0]);
                candidate = signSymbols[1];
            }
        }
        if (perMilleReplacesPercent && candidate == u'%') {
            candidate = u'‰';
        }
        if (dropCurrencySymbols && candidate == u'\u00A4') {
            continue;
        }
        output.append(candidate);
    }
}

PatternSignType PatternStringUtils::resolveSignDisplay(UNumberSignDisplay signDisplay, Signum signum) {
    switch (signDisplay) {
        case UNUM_SIGN_AUTO:
        case UNUM_SIGN_ACCOUNTING:
            switch (signum) {
                case SIGNUM_NEG:
                case SIGNUM_NEG_ZERO:
                    return PATTERN_SIGN_TYPE_NEG;
                case SIGNUM_POS_ZERO:
                case SIGNUM_POS:
                    return PATTERN_SIGN_TYPE_POS;
                default:
                    break;
            }
            break;

        case UNUM_SIGN_ALWAYS:
        case UNUM_SIGN_ACCOUNTING_ALWAYS:
            switch (signum) {
                case SIGNUM_NEG:
                case SIGNUM_NEG_ZERO:
                    return PATTERN_SIGN_TYPE_NEG;
                case SIGNUM_POS_ZERO:
                case SIGNUM_POS:
                    return PATTERN_SIGN_TYPE_POS_SIGN;
                default:
                    break;
            }
            break;

        case UNUM_SIGN_EXCEPT_ZERO:
        case UNUM_SIGN_ACCOUNTING_EXCEPT_ZERO:
            switch (signum) {
                case SIGNUM_NEG:
                    return PATTERN_SIGN_TYPE_NEG;
                case SIGNUM_NEG_ZERO:
                case SIGNUM_POS_ZERO:
                    return PATTERN_SIGN_TYPE_POS;
                case SIGNUM_POS:
                    return PATTERN_SIGN_TYPE_POS_SIGN;
                default:
                    break;
            }
            break;

        case UNUM_SIGN_NEGATIVE:
        case UNUM_SIGN_ACCOUNTING_NEGATIVE:
            switch (signum) {
                case SIGNUM_NEG:
                    return PATTERN_SIGN_TYPE_NEG;
                case SIGNUM_NEG_ZERO:
                case SIGNUM_POS_ZERO:
                case SIGNUM_POS:
                    return PATTERN_SIGN_TYPE_POS;
                default:
                    break;
            }
            break;

        case UNUM_SIGN_NEVER:
            return PATTERN_SIGN_TYPE_POS;

        default:
            break;
    }

    UPRV_UNREACHABLE_EXIT;
    return PATTERN_SIGN_TYPE_POS;
}

#endif /* #if !UCONFIG_NO_FORMATTING */
