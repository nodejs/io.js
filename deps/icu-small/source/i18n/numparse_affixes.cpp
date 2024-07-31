// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numparse_types.h"
#include "numparse_affixes.h"
#include "numparse_utils.h"
#include "number_utils.h"
#include "string_segment.h"

using namespace icu;
using namespace icu::numparse;
using namespace icu::numparse::impl;
using namespace icu::number;
using namespace icu::number::impl;


namespace {

/**
 * Helper method to return whether the given AffixPatternMatcher equals the given pattern string.
 * Either both arguments must be null or the pattern string inside the AffixPatternMatcher must equal
 * the given pattern string.
 */
bool matched(const AffixPatternMatcher* affix, const UnicodeString& patternString) {
    return (affix == nullptr && patternString.isBogus()) ||
           (affix != nullptr && affix->getPattern() == patternString);
}

/**
 * Helper method to return the length of the given AffixPatternMatcher. Returns 0 for null.
 */
int32_t length(const AffixPatternMatcher* matcher) {
    return matcher == nullptr ? 0 : matcher->getPattern().length();
}

/**
 * Helper method to return whether (1) both lhs and rhs are null/invalid, or (2) if they are both
 * valid, whether they are equal according to operator==.  Similar to Java Objects.equals()
 */
bool equals(const AffixPatternMatcher* lhs, const AffixPatternMatcher* rhs) {
    if (lhs == nullptr && rhs == nullptr) {
        return true;
    }
    if (lhs == nullptr || rhs == nullptr) {
        return false;
    }
    return *lhs == *rhs;
}

}


AffixPatternMatcherBuilder::AffixPatternMatcherBuilder(const UnicodeString& pattern,
                                                       AffixTokenMatcherWarehouse& warehouse,
                                                       IgnorablesMatcher* ignorables)
        : fMatchersLen(0),
          fLastTypeOrCp(0),
          fPattern(pattern),
          fWarehouse(warehouse),
          fIgnorables(ignorables) {}

void AffixPatternMatcherBuilder::consumeToken(AffixPatternType type, UChar32 cp, UErrorCode& status) {
    // This is called by AffixUtils.iterateWithConsumer() for each token.

    // Add an ignorables matcher between tokens except between two literals, and don't put two
    // ignorables matchers in a row.
    if (fIgnorables != nullptr && fMatchersLen > 0 &&
        (fLastTypeOrCp < 0 || !fIgnorables->getSet()->contains(fLastTypeOrCp))) {
        addMatcher(*fIgnorables);
    }

    if (type != TYPE_CODEPOINT) {
        // Case 1: the token is a symbol.
        switch (type) {
            case TYPE_MINUS_SIGN:
                addMatcher(fWarehouse.minusSign());
                break;
            case TYPE_PLUS_SIGN:
                addMatcher(fWarehouse.plusSign());
                break;
            case TYPE_PERCENT:
                addMatcher(fWarehouse.percent());
                break;
            case TYPE_PERMILLE:
                addMatcher(fWarehouse.permille());
                break;
            case TYPE_CURRENCY_SINGLE:
            case TYPE_CURRENCY_DOUBLE:
            case TYPE_CURRENCY_TRIPLE:
            case TYPE_CURRENCY_QUAD:
            case TYPE_CURRENCY_QUINT:
                // All currency symbols use the same matcher
                addMatcher(fWarehouse.currency(status));
                break;
            default:
                UPRV_UNREACHABLE_EXIT;
        }

    } else if (fIgnorables != nullptr && fIgnorables->getSet()->contains(cp)) {
        // Case 2: the token is an ignorable literal.
        // No action necessary: the ignorables matcher has already been added.

    } else {
        // Case 3: the token is a non-ignorable literal.
        if (auto* ptr = fWarehouse.nextCodePointMatcher(cp, status)) {
            addMatcher(*ptr);
        } else {
            // OOM; unwind the stack
            return;
        }
    }
    fLastTypeOrCp = type != TYPE_CODEPOINT ? type : cp;
}

void AffixPatternMatcherBuilder::addMatcher(NumberParseMatcher& matcher) {
    if (fMatchersLen >= fMatchers.getCapacity()) {
        fMatchers.resize(fMatchersLen * 2, fMatchersLen);
    }
    fMatchers[fMatchersLen++] = &matcher;
}

AffixPatternMatcher AffixPatternMatcherBuilder::build(UErrorCode& status) {
    return AffixPatternMatcher(fMatchers, fMatchersLen, fPattern, status);
}

AffixTokenMatcherWarehouse::AffixTokenMatcherWarehouse(const AffixTokenMatcherSetupData* setupData)
        : fSetupData(setupData) {}

NumberParseMatcher& AffixTokenMatcherWarehouse::minusSign() {
    return fMinusSign = {fSetupData->dfs, true};
}

NumberParseMatcher& AffixTokenMatcherWarehouse::plusSign() {
    return fPlusSign = {fSetupData->dfs, true};
}

NumberParseMatcher& AffixTokenMatcherWarehouse::percent() {
    return fPercent = {fSetupData->dfs};
}

NumberParseMatcher& AffixTokenMatcherWarehouse::permille() {
    return fPermille = {fSetupData->dfs};
}

NumberParseMatcher& AffixTokenMatcherWarehouse::currency(UErrorCode& status) {
    return fCurrency = {fSetupData->currencySymbols, fSetupData->dfs, fSetupData->parseFlags, status};
}

IgnorablesMatcher& AffixTokenMatcherWarehouse::ignorables() {
    return fSetupData->ignorables;
}

NumberParseMatcher* AffixTokenMatcherWarehouse::nextCodePointMatcher(UChar32 cp, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    auto* result = fCodePoints.create(cp);
    if (result == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }
    return result;
}

bool AffixTokenMatcherWarehouse::hasEmptyCurrencySymbol() const {
    return fSetupData->currencySymbols.hasEmptyCurrencySymbol();
}


CodePointMatcher::CodePointMatcher(UChar32 cp)
        : fCp(cp) {}

bool CodePointMatcher::match(StringSegment& segment, ParsedNumber& result, UErrorCode&) const {
    if (segment.startsWith(fCp)) {
        segment.adjustOffsetByCodePoint();
        result.setCharsConsumed(segment);
    }
    return false;
}

bool CodePointMatcher::smokeTest(const StringSegment& segment) const {
    return segment.startsWith(fCp);
}

UnicodeString CodePointMatcher::toString() const {
    return u"<CodePoint>";
}


AffixPatternMatcher AffixPatternMatcher::fromAffixPattern(const UnicodeString& affixPattern,
                                                          AffixTokenMatcherWarehouse& tokenWarehouse,
                                                          parse_flags_t parseFlags, bool* success,
                                                          UErrorCode& status) {
    if (affixPattern.isEmpty()) {
        *success = false;
        return {};
    }
    *success = true;

    IgnorablesMatcher* ignorables;
    if (0 != (parseFlags & PARSE_FLAG_EXACT_AFFIX)) {
        ignorables = nullptr;
    } else {
        ignorables = &tokenWarehouse.ignorables();
    }

    AffixPatternMatcherBuilder builder(affixPattern, tokenWarehouse, ignorables);
    AffixUtils::iterateWithConsumer(affixPattern, builder, status);
    return builder.build(status);
}

AffixPatternMatcher::AffixPatternMatcher(MatcherArray& matchers, int32_t matchersLen,
                                         const UnicodeString& pattern, UErrorCode& status)
    : ArraySeriesMatcher(matchers, matchersLen), fPattern(pattern, status) {
}

UnicodeString AffixPatternMatcher::getPattern() const {
    return fPattern.toAliasedUnicodeString();
}

bool AffixPatternMatcher::operator==(const AffixPatternMatcher& other) const {
    return fPattern == other.fPattern;
}


AffixMatcherWarehouse::AffixMatcherWarehouse(AffixTokenMatcherWarehouse* tokenWarehouse)
        : fTokenWarehouse(tokenWarehouse) {
}

bool AffixMatcherWarehouse::isInteresting(const AffixPatternProvider& patternInfo,
                                          const IgnorablesMatcher& ignorables, parse_flags_t parseFlags,
                                          UErrorCode& status) {
    UnicodeString posPrefixString = patternInfo.getString(AffixPatternProvider::AFFIX_POS_PREFIX);
    UnicodeString posSuffixString = patternInfo.getString(AffixPatternProvider::AFFIX_POS_SUFFIX);
    UnicodeString negPrefixString;
    UnicodeString negSuffixString;
    if (patternInfo.hasNegativeSubpattern()) {
        negPrefixString = patternInfo.getString(AffixPatternProvider::AFFIX_NEG_PREFIX);
        negSuffixString = patternInfo.getString(AffixPatternProvider::AFFIX_NEG_SUFFIX);
    }

    if (0 == (parseFlags & PARSE_FLAG_USE_FULL_AFFIXES) &&
        AffixUtils::containsOnlySymbolsAndIgnorables(posPrefixString, *ignorables.getSet(), status) &&
        AffixUtils::containsOnlySymbolsAndIgnorables(posSuffixString, *ignorables.getSet(), status) &&
        AffixUtils::containsOnlySymbolsAndIgnorables(negPrefixString, *ignorables.getSet(), status) &&
        AffixUtils::containsOnlySymbolsAndIgnorables(negSuffixString, *ignorables.getSet(), status)
        // HACK: Plus and minus sign are a special case: we accept them trailing only if they are
        // trailing in the pattern string.
        && !AffixUtils::containsType(posSuffixString, TYPE_PLUS_SIGN, status) &&
        !AffixUtils::containsType(posSuffixString, TYPE_MINUS_SIGN, status) &&
        !AffixUtils::containsType(negSuffixString, TYPE_PLUS_SIGN, status) &&
        !AffixUtils::containsType(negSuffixString, TYPE_MINUS_SIGN, status)) {
        // The affixes contain only symbols and ignorables.
        // No need to generate affix matchers.
        return false;
    }
    return true;
}

void AffixMatcherWarehouse::createAffixMatchers(const AffixPatternProvider& patternInfo,
                                                MutableMatcherCollection& output,
                                                const IgnorablesMatcher& ignorables,
                                                parse_flags_t parseFlags, UErrorCode& status) {
    if (!isInteresting(patternInfo, ignorables, parseFlags, status)) {
        return;
    }

    // The affixes have interesting characters, or we are in strict mode.
    // Use initial capacity of 6, the highest possible number of AffixMatchers.
    UnicodeString sb;
    bool includeUnpaired = 0 != (parseFlags & PARSE_FLAG_INCLUDE_UNPAIRED_AFFIXES);

    int32_t numAffixMatchers = 0;
    int32_t numAffixPatternMatchers = 0;

    AffixPatternMatcher* posPrefix = nullptr;
    AffixPatternMatcher* posSuffix = nullptr;

    // Pre-process the affix strings to resolve LDML rules like sign display.
    for (int8_t typeInt = 0; typeInt < PATTERN_SIGN_TYPE_COUNT * 2; typeInt++) {
        auto type = static_cast<PatternSignType>(typeInt / 2);
        bool dropCurrencySymbols = (typeInt % 2) == 1;

        if (dropCurrencySymbols && !patternInfo.hasCurrencySign()) {
            continue;
        }
        if (dropCurrencySymbols && !fTokenWarehouse->hasEmptyCurrencySymbol()) {
            continue;
        }

        // Skip affixes in some cases
        if (type == PATTERN_SIGN_TYPE_POS
                && 0 != (parseFlags & PARSE_FLAG_PLUS_SIGN_ALLOWED)) {
            continue;
        }
        if (type == PATTERN_SIGN_TYPE_POS_SIGN
                && 0 == (parseFlags & PARSE_FLAG_PLUS_SIGN_ALLOWED)) {
            continue;
        }

        // Generate Prefix
        // TODO: Handle approximately sign?
        bool hasPrefix = false;
        PatternStringUtils::patternInfoToStringBuilder(
                patternInfo, true, type, false, StandardPlural::OTHER, false, dropCurrencySymbols, sb);
        fAffixPatternMatchers[numAffixPatternMatchers] = AffixPatternMatcher::fromAffixPattern(
                sb, *fTokenWarehouse, parseFlags, &hasPrefix, status);
        AffixPatternMatcher* prefix = hasPrefix ? &fAffixPatternMatchers[numAffixPatternMatchers++]
                                                : nullptr;

        // Generate Suffix
        // TODO: Handle approximately sign?
        bool hasSuffix = false;
        PatternStringUtils::patternInfoToStringBuilder(
                patternInfo, false, type, false, StandardPlural::OTHER, false, dropCurrencySymbols, sb);
        fAffixPatternMatchers[numAffixPatternMatchers] = AffixPatternMatcher::fromAffixPattern(
                sb, *fTokenWarehouse, parseFlags, &hasSuffix, status);
        AffixPatternMatcher* suffix = hasSuffix ? &fAffixPatternMatchers[numAffixPatternMatchers++]
                                                : nullptr;

        if (type == PATTERN_SIGN_TYPE_POS) {
            posPrefix = prefix;
            posSuffix = suffix;
        } else if (equals(prefix, posPrefix) && equals(suffix, posSuffix)) {
            // Skip adding these matchers (we already have equivalents)
            continue;
        }

        // Flags for setting in the ParsedNumber; the token matchers may add more.
        int flags = (type == PATTERN_SIGN_TYPE_NEG) ? FLAG_NEGATIVE : 0;

        // Note: it is indeed possible for posPrefix and posSuffix to both be null.
        // We still need to add that matcher for strict mode to work.
        fAffixMatchers[numAffixMatchers++] = {prefix, suffix, flags};
        if (includeUnpaired && prefix != nullptr && suffix != nullptr) {
            // The following if statements are designed to prevent adding two identical matchers.
            if (type == PATTERN_SIGN_TYPE_POS || !equals(prefix, posPrefix)) {
                fAffixMatchers[numAffixMatchers++] = {prefix, nullptr, flags};
            }
            if (type == PATTERN_SIGN_TYPE_POS || !equals(suffix, posSuffix)) {
                fAffixMatchers[numAffixMatchers++] = {nullptr, suffix, flags};
            }
        }
    }

    // Put the AffixMatchers in order, and then add them to the output.
    // Since there are at most 9 elements, do a simple-to-implement bubble sort.
    bool madeChanges;
    do {
        madeChanges = false;
        for (int32_t i = 1; i < numAffixMatchers; i++) {
            if (fAffixMatchers[i - 1].compareTo(fAffixMatchers[i]) > 0) {
                madeChanges = true;
                AffixMatcher temp = std::move(fAffixMatchers[i - 1]);
                fAffixMatchers[i - 1] = std::move(fAffixMatchers[i]);
                fAffixMatchers[i] = std::move(temp);
            }
        }
    } while (madeChanges);

    for (int32_t i = 0; i < numAffixMatchers; i++) {
        // Enable the following line to debug affixes
        //std::cout << "Adding affix matcher: " << CStr(fAffixMatchers[i].toString())() << std::endl;
        output.addMatcher(fAffixMatchers[i]);
    }
}


AffixMatcher::AffixMatcher(AffixPatternMatcher* prefix, AffixPatternMatcher* suffix, result_flags_t flags)
        : fPrefix(prefix), fSuffix(suffix), fFlags(flags) {}

bool AffixMatcher::match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const {
    if (!result.seenNumber()) {
        // Prefix
        // Do not match if:
        // 1. We have already seen a prefix (result.prefix != null)
        // 2. The prefix in this AffixMatcher is empty (prefix == null)
        if (!result.prefix.isBogus() || fPrefix == nullptr) {
            return false;
        }

        // Attempt to match the prefix.
        int initialOffset = segment.getOffset();
        bool maybeMore = fPrefix->match(segment, result, status);
        if (initialOffset != segment.getOffset()) {
            result.prefix = fPrefix->getPattern();
        }
        return maybeMore;

    } else {
        // Suffix
        // Do not match if:
        // 1. We have already seen a suffix (result.suffix != null)
        // 2. The suffix in this AffixMatcher is empty (suffix == null)
        // 3. The matched prefix does not equal this AffixMatcher's prefix
        if (!result.suffix.isBogus() || fSuffix == nullptr || !matched(fPrefix, result.prefix)) {
            return false;
        }

        // Attempt to match the suffix.
        int initialOffset = segment.getOffset();
        bool maybeMore = fSuffix->match(segment, result, status);
        if (initialOffset != segment.getOffset()) {
            result.suffix = fSuffix->getPattern();
        }
        return maybeMore;
    }
}

bool AffixMatcher::smokeTest(const StringSegment& segment) const {
    return (fPrefix != nullptr && fPrefix->smokeTest(segment)) ||
           (fSuffix != nullptr && fSuffix->smokeTest(segment));
}

void AffixMatcher::postProcess(ParsedNumber& result) const {
    // Check to see if our affix is the one that was matched. If so, set the flags in the result.
    if (matched(fPrefix, result.prefix) && matched(fSuffix, result.suffix)) {
        // Fill in the result prefix and suffix with non-null values (empty string).
        // Used by strict mode to determine whether an entire affix pair was matched.
        if (result.prefix.isBogus()) {
            result.prefix = UnicodeString();
        }
        if (result.suffix.isBogus()) {
            result.suffix = UnicodeString();
        }
        result.flags |= fFlags;
        if (fPrefix != nullptr) {
            fPrefix->postProcess(result);
        }
        if (fSuffix != nullptr) {
            fSuffix->postProcess(result);
        }
    }
}

int8_t AffixMatcher::compareTo(const AffixMatcher& rhs) const {
    const AffixMatcher& lhs = *this;
    if (length(lhs.fPrefix) != length(rhs.fPrefix)) {
        return length(lhs.fPrefix) > length(rhs.fPrefix) ? -1 : 1;
    } else if (length(lhs.fSuffix) != length(rhs.fSuffix)) {
        return length(lhs.fSuffix) > length(rhs.fSuffix) ? -1 : 1;
    } else {
        return 0;
    }
}

UnicodeString AffixMatcher::toString() const {
    bool isNegative = 0 != (fFlags & FLAG_NEGATIVE);
    return UnicodeString(u"<Affix") + (isNegative ? u":negative " : u" ") +
           (fPrefix ? fPrefix->getPattern() : u"null") + u"#" +
           (fSuffix ? fSuffix->getPattern() : u"null") + u">";

}


#endif /* #if !UCONFIG_NO_FORMATTING */
