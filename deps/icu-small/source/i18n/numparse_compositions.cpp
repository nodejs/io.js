// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

// Allow implicit conversion from char16_t* to UnicodeString for this file:
// Helpful in toString methods and elsewhere.
#define UNISTR_FROM_STRING_EXPLICIT

#include "numparse_types.h"
#include "numparse_compositions.h"
#include "string_segment.h"
#include "unicode/uniset.h"

using namespace icu;
using namespace icu::numparse;
using namespace icu::numparse::impl;


bool SeriesMatcher::match(StringSegment& segment, ParsedNumber& result, UErrorCode& status) const {
    ParsedNumber backup(result);

    int32_t initialOffset = segment.getOffset();
    bool maybeMore = true;
    for (const auto* it = begin(); it < end();) {
        const NumberParseMatcher* matcher = *it;
        int matcherOffset = segment.getOffset();
        if (segment.length() != 0) {
            maybeMore = matcher->match(segment, result, status);
        } else {
            // Nothing for this matcher to match; ask for more.
            maybeMore = true;
        }

        bool success = (segment.getOffset() != matcherOffset);
        bool isFlexible = matcher->isFlexible();
        if (success && isFlexible) {
            // Match succeeded, and this is a flexible matcher. Re-run it.
        } else if (success) {
            // Match succeeded, and this is NOT a flexible matcher. Proceed to the next matcher.
            it++;
            // Small hack: if there is another matcher coming, do not accept trailing weak chars.
            // Needed for proper handling of currency spacing.
            if (it < end() && segment.getOffset() != result.charEnd && result.charEnd > matcherOffset) {
                segment.setOffset(result.charEnd);
            }
        } else if (isFlexible) {
            // Match failed, and this is a flexible matcher. Try again with the next matcher.
            it++;
        } else {
            // Match failed, and this is NOT a flexible matcher. Exit.
            segment.setOffset(initialOffset);
            result = backup;
            return maybeMore;
        }
    }

    // All matchers in the series succeeded.
    return maybeMore;
}

bool SeriesMatcher::smokeTest(const StringSegment& segment) const {
    // NOTE: The range-based for loop calls the virtual begin() and end() methods.
    // NOTE: We only want the first element. Use the for loop for boundary checking.
    for (const auto& matcher : *this) {
        // SeriesMatchers are never allowed to start with a Flexible matcher.
        U_ASSERT(!matcher->isFlexible());
        return matcher->smokeTest(segment);
    }
    return false;
}

void SeriesMatcher::postProcess(ParsedNumber& result) const {
    // NOTE: The range-based for loop calls the virtual begin() and end() methods.
    for (const auto* matcher : *this) {
        matcher->postProcess(result);
    }
}


ArraySeriesMatcher::ArraySeriesMatcher()
        : fMatchersLen(0) {
}

ArraySeriesMatcher::ArraySeriesMatcher(MatcherArray& matchers, int32_t matchersLen)
        : fMatchers(std::move(matchers)), fMatchersLen(matchersLen) {
}

int32_t ArraySeriesMatcher::length() const {
    return fMatchersLen;
}

const NumberParseMatcher* const* ArraySeriesMatcher::begin() const {
    return fMatchers.getAlias();
}

const NumberParseMatcher* const* ArraySeriesMatcher::end() const {
    return fMatchers.getAlias() + fMatchersLen;
}

UnicodeString ArraySeriesMatcher::toString() const {
    return u"<ArraySeries>";
}


#endif /* #if !UCONFIG_NO_FORMATTING */
